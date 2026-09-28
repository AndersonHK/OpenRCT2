"""Read existing MixedFixtureWorld paint traces; never run the game or fit GPU offsets.

Output is a mask-overlap observation ledger, not a correctness oracle. The source
trace omits palette/blend state and semantic owner IDs; explicit review is needed
before an observation can become a constraint for the current renderer.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def integers(values, count):
    return isinstance(values, list) and len(values) == count and all(type(x) is int for x in values)


def component_key(part):
    require(type(part.get("image")) is int and integers(part.get("screen"), 2)
            and integers(part.get("bounds"), 6), "Malformed paint component")
    return (part["image"], *part["screen"], *part["bounds"])


def local_file(root, name):
    require(isinstance(name, str) and Path(name).name == name, "Expected corpus-local filename")
    result = (root / name).resolve(strict=True)
    require(result.parent == root, "Input escaped corpus directory")
    return result


def observe_case(case, trace, assets):
    require(integers(case.get("target"), 4), "Invalid case target")
    tx, ty, width, height = case["target"]
    require(0 < width <= 1024 and 0 < height <= 1024, "Unsupported target extent")
    require(trace.get("oracleOnly") is True and trace.get("gpuInput") is False,
            "Expected oracle-only MixedFixtureWorld trace")
    require(trace.get("rotation") == case["rotation"], "Camera rotation differs")
    ids = {}
    components = []
    counts = Counter()
    examples = {}
    covered_columns = set()
    ambiguous_columns = []
    unknown_images = set()
    for column in trace["columns"]:
        cx = column["column"]
        require(type(cx) is int and cx % 32 == 0 and cx not in covered_columns, "Invalid/duplicate column")
        covered_columns.add(cx)
        left, right = max(cx, tx), min(cx + 32, tx + width)
        require(left < right, "Column outside target")
        parts = [part for parent in column["orderedParents"] for part in parent]
        require(len(parts) <= 65536, "Oversized paint column")
        keys = [component_key(part) for part in parts]
        # Screen/image/bounds are insufficient to distinguish identical twins.
        # Do not manufacture cross-column object identities from list position.
        if len(set(keys)) != len(keys):
            ambiguous_columns.append(cx)
            continue
        stacks = [[] for _ in range((right - left) * height)]
        for part, key in zip(parts, keys):
            if key not in ids:
                ids[key] = len(components)
                components.append({"id": ids[key], "image": part["image"],
                                   "screen": part["screen"], "bounds": part["bounds"]})
            identity = ids[key]
            asset = assets.get(part["image"])
            if asset is None:
                unknown_images.add(part["image"])
                continue
            x = part["screen"][0] + asset["xOffset"]
            y = part["screen"][1] + asset["yOffset"]
            for py in range(max(y, ty), min(y + asset["height"], ty + height)):
                for px in range(max(x, left), min(x + asset["width"], right)):
                    if asset["pixels"][(py - y) * asset["width"] + px - x]:
                        stacks[(py - ty) * (right - left) + px - left].append(identity)
        for offset, stack in enumerate(stacks):
            if len(stack) < 2:
                continue
            front = stack[-1]
            for back in stack[:-1]:
                pair = (back, front)
                counts[pair] += 1
                examples.setdefault(pair, [left + offset % (right - left), ty + offset // (right - left)])
    require(covered_columns == set(range(tx // 32 * 32, tx + width, 32)), "Missing paint columns")
    contradictions = sorted({tuple(sorted(pair)) for pair in counts if pair[::-1] in counts})
    return {"name": case["name"], "rotation": case["rotation"], "stableSort": trace["stableSort"],
            "target": case["target"], "components": components,
            "observations": [{"behind": a, "inFront": b, "maskPixels": count,
                              "exampleScreenPixel": examples[(a, b)], "review": "pending"}
                             for (a, b), count in sorted(counts.items())],
            "contradictoryPairs": [list(pair) for pair in contradictions],
            "ambiguousColumns": ambiguous_columns, "unknownImages": sorted(unknown_images),
            "completeMaskCoverage": not ambiguous_columns and not unknown_images}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.corpus.resolve(strict=True)
    manifest_path = local_file(root, "corpus.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("schema") == 1 and manifest.get("recipe") == "finite-mixed-v1"
            and manifest.get("zoom") == 0, "Only the actual finite-mixed-v1 zero-zoom corpus is supported")
    assets = {}
    for original in manifest["assets"]:
        asset = dict(original)
        require(asset["image"] not in assets and asset.get("coveredZeroPixels") == 0,
                "Duplicate asset or unsupported covered-zero mask")
        require(integers([asset[k] for k in ("width", "height", "xOffset", "yOffset")], 4)
                and 0 < asset["width"] <= 2048 and 0 < asset["height"] <= 2048, "Invalid asset dimensions")
        asset["pixels"] = bytes.fromhex(asset["decodedIndexedHex"])
        require(len(asset["pixels"]) == asset["width"] * asset["height"], "Invalid decoded asset length")
        assets[asset["image"]] = asset
    inputs = [manifest_path]
    results = []
    names = set()
    for case in manifest["cases"]:
        require(case["name"] not in names, "Duplicate case")
        names.add(case["name"])
        trace_path = local_file(root, case["name"] + ".paint.json")
        inputs.append(trace_path)
        results.append(observe_case(case, json.loads(trace_path.read_text(encoding="utf-8")), assets))
    report = {"schema": 1,
              "scope": "Frozen paint-order plus decoded-mask observations; no current GPU comparison or fit",
              "limitations": ["Trace omits recolour/blend state and stable semantic owner IDs",
                              "Same-colour opaque intersections are retained from trace order, not inferred from RGB",
                              "Only pairs with a topmost decoded-mask member are reported",
                              "Review, exact replay and current GPU identity matching are required before fitting"],
              "inputSha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
              "toolSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "cases": results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps({"cases": len(results), "observations": sum(len(c["observations"]) for c in results),
                      "incompleteCases": sum(not c["completeMaskCoverage"] for c in results),
                      "contradictoryPairs": sum(len(c["contradictoryPairs"]) for c in results)}))


if __name__ == "__main__":
    main()
