"""Preserve exact accepted giant fixture producer code, never a future live tree.

Writes a new complete archive only after all selected original bytes are proven
by both immutable fixture and producer receipts. Never edits historical evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil

ROOT_BUILD_METADATA = frozenset((
    "CMakeLists.txt", "CMakeLists_mingw.txt", "CMakeSettings.json",
    "openrct2.common.props", "openrct2.proj", "openrct2.vulkan.props",
))


def eligible(relative):
    if not isinstance(relative, str) or "\\" in relative or ":" in relative:
        return False
    parts = relative.split("/")
    if any(part in ("", ".", "..") for part in parts):
        return False
    return (relative.startswith(("src/", "data/shaders/")) or relative in ROOT_BUILD_METADATA)


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inside(root, relative, *, exists=True):
    require(isinstance(relative, str) and relative and "\\" not in relative and ":" not in relative,
            "Invalid archive relative path")
    require(all(part not in ("", ".", "..") for part in relative.split("/")), "Noncanonical archive path")
    path = (root / PurePosixPath(relative)).resolve(strict=exists)
    require(root.resolve() in path.parents, "Archive path escapes its root")
    return path


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def selected_sources(root, fixture, producer):
    selected = {}
    for key, digest in producer["sourceSha256"].items():
        if not key.startswith("source/"):
            continue
        relative = key.removeprefix("source/")
        if not eligible(relative):
            continue
        source = inside(root, relative, exists=False)
        require(fixture["inputSha256"].get(str(source)) == digest,
                "Producer source is not identically attested by fixture: " + relative)
        require(relative not in selected, "Duplicate producer source")
        selected[relative] = digest
    require(selected, "Producer has no eligible original source inputs")
    return selected


HISTORICAL_BUILDER = "scripts/rendering/build-current-ui-parity.py"
HISTORICAL_FIXTURE_HELPER = "scripts/rendering/run-screenshot-parity.py"
GIANT_CASE_NAMES = tuple(f"{background}-r{rotation}z{zoom}" for background in ("ordinary", "transparent")
                        for rotation, zoom in [(r,z) for z in (0,1) for r in range(4)] + [(0,2),(0,3)])


def historical_tooling(root, fixture_path, fixture, producer, summary_pins, pin):
    """Exactly two historical scripts, with distinct independent attestation rules."""
    builder = str(inside(root, HISTORICAL_BUILDER, exists=False))
    helper = str(inside(root, HISTORICAL_FIXTURE_HELPER, exists=False))
    builder_digest = fixture["inputSha256"].get(builder)
    helper_digest = fixture["inputSha256"].get(helper)
    require(builder_digest is not None and producer.get("builderSha256", {}).get(builder) == builder_digest,
            "Historical builder needs matching fixture and successful producer.builderSha256")
    require(helper_digest is not None, "Historical fixture helper must be an attested fixture input")
    fixture_hash = sha(fixture_path)
    evidence_root = root / "obj/vulkan-parity"
    def summary(path, expected, *, fresh):
        path = Path(path).resolve(strict=True)
        require(evidence_root in path.parents, "Historical summary must be workspace evidence")
        pin(path, expected)
        value = read(path)
        require(value.get("schema") == 3 and value.get("kind") == "giant-cli-parity"
                and value.get("status") == "pass" and value.get("failures") == []
                and value.get("inputAuditFailures") == [] and value.get("renderer") in ("frozen", "software"),
                "Historical helper requires successful giant frozen/software summary")
        require(not fresh or value.get("freshRepeat") is True, "Historical helper needs fresh-repeat proof")
        identity = value.get("fixture", {})
        require(identity.get("kind") == "giant-seams-v1" and identity.get("version") == 1
                and identity.get("caseNames") == list(GIANT_CASE_NAMES)
                and identity.get("cameras") == fixture.get("giantCameras")
                and identity.get("backgroundPolicy") == "config-false-explicit-cli-switch"
                and Path(identity["inputManifest"]["path"]).resolve() == fixture_path
                and identity["inputManifest"]["sha256"] == fixture_hash,
                "Historical summary fixture identity differs")
        require(value.get("park") == {"path": fixture["park"]["path"], "sha256": fixture["park"]["sha256"]},
                "Historical summary uses another park")
        cases = value.get("cases", [])
        require([case.get("name") for case in cases] == list(GIANT_CASE_NAMES)
                and all(case.get("exitCode") == 0 and case.get("failures") == [] for case in cases),
                "Historical summary case inventory did not pass")
        require(value.get("inputSha256", {}).get(helper) == helper_digest,
                "Historical helper differs from independently successful summary input")
        return path, value
    observed, proofs = {}, []
    identity_fields = ("renderer", "buildReceipt", "executable", "runtimeDllSha256", "park", "assets",
                       "referenceReceipt", "dataSha256", "fixture", "shaderSha256", "factory")
    for proof in summary_pins:
        path, value = summary(proof["path"], proof["sha256"], fresh=True)
        renderer = value["renderer"]
        require(renderer not in observed, "Duplicate historical helper renderer proof")
        same_identity = False
        for reference in value.get("comparisonReceipts", []):
            _, previous = summary(reference["path"], reference["sha256"], fresh=False)
            if all(previous.get(key) == value.get(key) for key in identity_fields):
                same_identity = True
        require(same_identity, "Fresh summary lacks a successful reference with identical build/fixture identity")
        require(all(case.get("comparisons") and all(
            comparison.get("differingPixelsOrEntries")
            and all(count == 0 for count in comparison["differingPixelsOrEntries"].values())
            for comparison in case["comparisons"]) for case in value["cases"]),
            "Fresh historical summary does not report exact comparisons")
        observed[renderer] = (path, value)
        proofs.append({"path": str(path), "sha256": proof["sha256"]})
    require(set(observed) == {"frozen", "software"}, "Both frozen and current-software fresh summaries required")
    frozen_path, _ = observed["frozen"]
    require(any(Path(reference["path"]).resolve() == frozen_path and reference["sha256"] == sha(frozen_path)
                for reference in observed["software"][1]["comparisonReceipts"]),
            "Current-software summary does not reference the pinned frozen fresh result")
    return {HISTORICAL_BUILDER: (builder_digest, "producer-builder"),
            HISTORICAL_FIXTURE_HELPER: (helper_digest, "fixture-helper")}, proofs

def archive(root, fixture_path, output, prior_archives=(), helper_path=None, summary_pins=()):
    root = root.resolve(strict=True)
    fixture_path = fixture_path.resolve(strict=True)
    output = output.resolve()
    evidence_root = root / "obj/vulkan-parity"
    require(evidence_root in fixture_path.parents, "Fixture receipt must be workspace evidence")
    require(evidence_root in output.parents and not output.exists(), "Output must be new workspace evidence")
    pins = {}
    def pin(path, expected=None):
        path = path.resolve(strict=True)
        actual = sha(path)
        require(expected is None or actual == expected, "Pinned input changed: " + str(path))
        require(path not in pins or pins[path] == actual, "Conflicting pinned input")
        pins[path] = actual
        return actual
    fixture_hash = pin(fixture_path)
    fixture = read(fixture_path)
    require(fixture.get("schema") == 1 and fixture.get("fixture") == "giant-seams-v1"
            and fixture.get("accepted") is True and not fixture.get("failure")
            and fixture.get("inputAuditFailures") == [], "Accepted unchanged giant fixture required")
    producer_pin = fixture["builds"]["current"]
    producer_path = Path(producer_pin["receipt"]).resolve(strict=True)
    require(evidence_root in producer_path.parents, "Producer receipt must be workspace evidence")
    pin(producer_path, producer_pin["sha256"])
    producer = read(producer_path)
    require(producer.get("status") == "pass" and Path(producer["sourceRoot"]).resolve() == root
            and not any(producer.get(key) for key in
                        ("sourceChangesDuringBuild", "dependencyChangesDuringBuild", "missingArtifacts")),
            "Successful source-stable original current producer required")
    selected = selected_sources(root, fixture, producer)
    tooling, tooling_proofs = historical_tooling(root, fixture_path, fixture, producer, summary_pins, pin)
    selected.update({name: digest for name, (digest, _) in tooling.items()})
    fallback = {}
    prior_proofs = []
    for path, digest in prior_archives:
        path = Path(path).resolve(strict=True)
        require(evidence_root in path.parents, "Prior archive must be workspace evidence")
        pin(path, digest)
        previous = read(path)
        require(previous.get("schema") == 1 and previous.get("fixtureManifestSha256") == fixture_hash,
                "Prior archive belongs to a different fixture")
        require(isinstance(previous.get("files"), list) and previous["files"], "Empty prior archive")
        for entry in previous["files"]:
            relative = entry["source"]
            require((eligible(relative) or relative in tooling) and relative in selected and entry.get("sha256") == selected[relative],
                    "Prior archive input is not an eligible attested producer source")
            require(relative not in fallback, "Duplicate source in prior archives")
            preserved = inside(path.parent, entry["archive"])
            require(preserved != inside(root, relative, exists=False), "Prior archive does not preserve independent bytes")
            pin(preserved, selected[relative])
            fallback[relative] = preserved
        prior_proofs.append({"path": str(path), "sha256": digest})
    if helper_path is not None:
        pin(Path(helper_path))
    output.mkdir(parents=True)
    entries, origins = [], {}
    try:
        for relative, expected in sorted(selected.items()):
            current = inside(root, relative, exists=False)
            # Prefer the explicitly pinned original archive, even if live happens
            # to match today. Never reconstruct historical bytes from a patch.
            source = fallback.get(relative, current)
            require(source.is_file(), "Original producer bytes unavailable: " + relative)
            pin(source, expected)
            destination = inside(output, "source/" + relative, exists=False)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            require(sha(destination) == expected and sha(source) == expected, "Source changed during archive copy")
            entries.append({"source": relative, "archive": "source/" + relative, "sha256": expected,
                            "kind": tooling[relative][1] if relative in tooling else "producer-source"})
            origins[relative] = {"path": str(source), "kind": "pinned-prior-archive" if relative in fallback else "matching-live-original"}
        changes = [str(path) for path, expected in pins.items() if not path.is_file() or sha(path) != expected]
        require(not changes, "Pinned archive inputs changed: " + repr(changes))
        require(len(entries) == len(selected), "Complete selected source inventory was not archived")
        result = {"schema": 1, "status": "pass", "fixtureManifestSha256": fixture_hash,
                  "producerReceipt": {"path": str(producer_path), "sha256": pins[producer_path]},
                  "completeProducerSourceArchive": True, "files": entries,
                  "rootBuildMetadataAllowlist": sorted(ROOT_BUILD_METADATA), "priorArchives": prior_proofs,
                  "toolingSummaryPins": tooling_proofs,
                  "origins": origins, "inputSha256": {str(path): digest for path, digest in pins.items()},
                  "inputChangesDuringArchive": changes,
                  "scope": "Original attested source/build metadata plus exactly two independently attested historical scripts; no assets, fixture validators, recipes, parks or reference pixels"}
        (output / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        return result
    except Exception as error:
        (output / "failure-receipt.json").write_text(json.dumps({"status": "fail", "error": str(error),
            "fixtureManifestSha256": fixture_hash, "selectedCount": len(selected), "copiedCount": len(entries),
            "inputSha256": {str(path): digest for path, digest in pins.items()}}, indent=2) + "\n", encoding="utf-8")
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prior-archive", nargs=2, action="append", default=[], metavar=("MANIFEST", "SHA256"))
    parser.add_argument("--reference-summary", nargs=2, action="append", required=True, metavar=("SUMMARY", "SHA256"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    result = archive(root, args.fixture_manifest, args.output, args.prior_archive, Path(__file__),
                     [{"path": path, "sha256": digest} for path, digest in args.reference_summary])
    print(json.dumps({"status": result["status"], "files": len(result["files"]), "manifest": str(args.output / "manifest.json")}))


if __name__ == "__main__":
    main()
