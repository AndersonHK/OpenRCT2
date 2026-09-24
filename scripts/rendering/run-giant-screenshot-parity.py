"""Versioned giant CLI qualification: external frozen software/configured Vulkan.
Sequential tile evidence, exact full indexed/palette/alpha/RGBA PNGs, fresh-process
repeats, seam samples and durable failure receipts. Does not qualify interactive performance.
"""
import argparse
import ast
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import traceback
from PIL import Image, ImageChops

HELPER = Path(__file__).with_name("run-screenshot-parity.py")
_spec = importlib.util.spec_from_file_location("giant_base", HELPER)
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)
sha, read, require, pin, ini, child = base.sha, base.read, base.require, base.pin, base.ini, base.child
Evidence, SHADER_NAMES = base.Evidence, base.SHADER_NAMES
verify_configured_build = base.verify_configured_build
configured_data_overlay, install_shader, pin_installed_data = (base.configured_data_overlay, base.install_shader, base.pin_installed_data)
write_summary = base.write_summary
CASE_NAMES = tuple(f"{background}-r{rotation}z{zoom}" for background in ("ordinary", "transparent")
                   for rotation, zoom in [(r,z) for z in (0,1) for r in range(4)] + [(0,2),(0,3)])
BUFFER_NAMES = ("indexed.bin", "palette.bin", "alpha.bin", "rgba.bin")


def png_buffers(path, expected_extent):
    with Image.open(path) as image:
        require(image.mode == "P" and list(image.size) == expected_extent, "Giant PNG mode/extent differs")
        transparency = image.info.get("transparency")
        if isinstance(transparency, int):
            alpha = bytes(0 if i == transparency else 255 for i in range(256))
        else:
            require(isinstance(transparency, bytes) and len(transparency) <= 256, "Missing/invalid indexed PNG tRNS")
            alpha = transparency + bytes([255]) * (256 - len(transparency))
        require(alpha == bytes([0]) + bytes([255])*255, "Only palette index zero must be transparent")
        buffers = {"indexed.bin": image.tobytes(), "palette.bin": bytes(image.getpalette()),
                   "alpha.bin": alpha, "rgba.bin": image.convert("RGBA").tobytes()}
        count = expected_extent[0] * expected_extent[1]
        require([len(buffers[n]) for n in BUFFER_NAMES] == [count,768,256,count*4], "Giant PNG buffer lengths differ")
        return buffers


# Only these producer-attested root build files may outlive the original build.
# Fixture/census helpers, recipes, assets and arbitrary root metadata stay live-pinned.
ROOT_PRODUCER_BUILD_METADATA = frozenset((
    "CMakeLists.txt", "CMakeLists_mingw.txt", "CMakeSettings.json",
    "openrct2.common.props", "openrct2.proj", "openrct2.vulkan.props",
))

def fixture_source_archive(archive_path, fixture_path, manifest, root, evidence):
    """Preserve historical producer bytes without requiring the live renderer to stay frozen."""
    if archive_path is None:
        return {}
    archive_path = archive_path.resolve(strict=True)
    require(root / "obj/vulkan-parity" in archive_path.parents, "Fixture source archive must be isolated evidence")
    evidence.track(archive_path)
    archive = read(archive_path)
    require(archive.get("schema") == 1 and archive.get("fixtureManifestSha256") == sha(fixture_path),
            "Source archive belongs to another fixture receipt")
    producer_pin = manifest["builds"]["current"]
    producer_path = Path(producer_pin["receipt"]).resolve(strict=True)
    evidence.track(producer_path, producer_pin["sha256"])
    producer = read(producer_path)
    require(producer.get("status") == "pass" and Path(producer["sourceRoot"]).resolve() == root
            and not any(producer.get(k) for k in ("sourceChangesDuringBuild", "dependencyChangesDuringBuild", "missingArtifacts")),
            "Archive requires the successful current fixture producer")
    tooling = {}
    if any(entry.get("source") in ("scripts/rendering/build-current-ui-parity.py",
                                   "scripts/rendering/run-screenshot-parity.py") for entry in archive.get("files", [])):
        # Current verifier code is independently pinned; archived scripts are evidence only, never imported.
        archive_helper = root / "scripts/rendering/archive-giant-fixture-producer.py"
        evidence.track(archive_helper)
        spec = importlib.util.spec_from_file_location("giant_historical_tooling", archive_helper)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        tooling, _ = helper.historical_tooling(root, fixture_path, manifest, producer,
                                               archive.get("toolingSummaryPins", []), evidence.track)
    replacements = {}
    entries = archive.get("files", [])
    require(isinstance(entries, list) and entries, "Source archive has no files")
    for entry in entries:
        relative = entry["source"]
        require(isinstance(relative, str) and "\\" not in relative and ":" not in relative
                and all(part not in ("", ".", "..") for part in relative.split("/")),
                "Archived source path must be canonical")
        source = (root / relative).resolve()
        require(root in source.parents, "Archived source path escapes producer root")
        require(relative.startswith(("src/", "data/shaders/")) or relative in ROOT_PRODUCER_BUILD_METADATA or relative in tooling,
                "Only historical producer sources/build metadata or explicitly attested tooling may be archived, not fixtures, recipes or assets")
        expected = producer["sourceSha256"].get("source/" + relative)
        if relative in tooling:
            expected, kind = tooling[relative]
            require(entry.get("kind") == kind, "Historical tooling kind differs from its attestation rule")
        require(expected is not None and manifest["inputSha256"].get(str(source)) == expected
                and entry.get("sha256") == expected and str(source) not in replacements,
                "Archived input is not uniquely attested by the fixture and producer")
        preserved = child(archive_path.parent, entry["archive"])
        require(preserved != source, "Source archive must preserve independent bytes")
        evidence.track(preserved, expected)
        replacements[str(source)] = preserved
    return replacements


def fixture_language_sources(manifest, reference, root, evidence, proof):
    """Historical producer language sources, never replacement runtime assets."""
    receipts = {}
    for lane in ("current", "frozen"):
        receipt_pin = manifest["builds"][lane]
        receipt_path = Path(receipt_pin["receipt"]).resolve(strict=True)
        require(root / "obj/vulkan-parity" in receipt_path.parents, "Language producer receipt escapes evidence")
        evidence.track(receipt_path, receipt_pin["sha256"])
        receipt = read(receipt_path)
        require(receipt.get("status") == "pass" and receipt.get("exitCode") == 0
                and all(receipt.get(key) == [] for key in
                        ("sourceChangesDuringBuild", "dependencyChangesDuringBuild", "missingArtifacts")),
                "Historical language sources require successful stable producer receipts")
        receipts[lane] = receipt
    require(Path(receipts["current"]["sourceRoot"]).resolve() == root, "Current producer source root differs")
    frozen_root = Path(receipts["frozen"]["sourceRoot"]).resolve(strict=True)
    require(root / "obj/vulkan-parity" in frozen_root.parents, "Frozen language source root escapes evidence")
    archive_receipt = frozen_root / "oracle-ui-source-receipt.json"
    archive_digest = manifest["inputSha256"].get(str(archive_receipt))
    require(archive_digest is not None, "Fixture must pin the independent frozen extraction receipt")
    evidence.track(archive_receipt, archive_digest)
    extraction = read(archive_receipt)
    frozen_identity = receipts["frozen"].get("frozenReference", {})
    require(extraction.get("referenceRevision") == frozen_identity.get("revision") == reference["revision"]
            and extraction.get("sourceArchiveSha256") == frozen_identity.get("sourceArchiveSha256")
                == reference["sourceArchive"]["sha256"], "Frozen language extraction provenance differs")
    evidence.track(root / reference["localReference"] / "source.zip", reference["sourceArchive"]["sha256"])
    replacements = {}
    for name, expected in manifest["inputSha256"].items():
        original = Path(name)
        if original.parent != root / "data/language" or original.suffix != ".txt":
            continue
        relative = original.relative_to(root).as_posix()
        preserved = child(frozen_root, relative)
        # Source versions that differ between the two producers remain live-pinned.
        # They cannot use this narrowly attested archive rule.
        if not all(value == expected for value in (
                receipts["current"]["sourceSha256"].get("source/" + relative),
                receipts["frozen"]["sourceSha256"].get("source/" + relative),
                extraction.get("originalSourceSha256", {}).get(relative),
                manifest["inputSha256"].get(str(preserved)))):
            continue
        evidence.track(preserved, expected)
        replacements[name] = preserved
    proof.update({"kind": "identical-producer-language-sources", "extractionReceipt": pin(archive_receipt),
                  "sourceRoot": str(frozen_root),
                  "replacements": {name: {"path": str(path), "sha256": manifest["inputSha256"][name]}
                                   for name, path in replacements.items()},
                  "scope": "Historical producer source inputs only; runtime asset roots, inventories and exact comparisons are unchanged"})
    return replacements


def verify_fixture(path, root, evidence, source_archive=None, language_proof=None):
    path = path.resolve(strict=True)
    evidence.track(path)
    manifest = read(path)
    require(manifest.get("schema") == 1 and manifest.get("accepted") is True
            and manifest.get("fixture") == "giant-seams-v1" and not manifest.get("failure")
            and manifest.get("inputAuditFailures") == [], "Accepted, stable giant fixture required")
    require(set(manifest.get("censusDifferences", {})) == {"frozen-verify","frozen-verify-repeat","current-verify"}
            and all(v == [] for v in manifest["censusDifferences"].values()), "Giant reload/fresh-repeat census missing")
    reference = read(root / "docs/vulkan-software-reference.json")
    require(manifest["frozenReference"]["revision"] == reference["revision"]
            and manifest["frozenReference"]["sourceArchiveSha256"] == reference["sourceArchive"]["sha256"],
            "Giant preparer frozen revision differs")
    archived_sources = fixture_source_archive(source_archive, path, manifest, root, evidence)
    archived_sources.update(fixture_language_sources(
        manifest, reference, root, evidence, language_proof if language_proof is not None else {}))
    for name, digest in manifest["inputSha256"].items():
        evidence.track(archived_sources.get(name, Path(name)), digest)
    park = Path(manifest["park"]["path"]).resolve(strict=True)
    require(path.parent in park.parents, "Fixture park escapes accepted output")
    evidence.track(park, manifest["park"]["sha256"])
    require([v.get("name") for v in manifest["processes"]] ==
            ["frozen-prepare","frozen-verify","frozen-verify-repeat","current-verify"], "Fixture process inventory differs")
    helper = root / "scripts/rendering/prepare-giant-screenshot-fixture.py"
    evidence.track(helper, manifest["runnerSha256"])
    tree = ast.parse(helper.read_text(encoding="utf-8"))
    selected = [node for node in tree.body if isinstance(node,ast.FunctionDef)
                and node.name in ("validate_nonuniform_census","giant_extents")]
    require(len(selected)==2, "Pinned fixture validator functions missing")
    validators = {}
    exec(compile(ast.Module(body=selected,type_ignores=[]),str(helper),"exec"),validators)
    first_census = None
    for record in manifest["processes"]:
        require(record["exitCode"] == 0, "Fixture process failed")
        evidence.track(path.parent / (record["name"] + ".json"), record["reportSha256"])
        evidence.track(path.parent / (record["name"] + ".log"), record["logSha256"])
        canonical_path = path.parent / (record["name"] + ".canonical.json")
        evidence.track(canonical_path, record["canonicalCensusSha256"])
        report = read(path.parent / (record["name"] + ".json"))
        require(report.get("noSimulationTicks") is True, "Fixture process advanced simulation")
        census = report["census"]
        validators["validate_nonuniform_census"](census)
        canonical = (json.dumps(census,sort_keys=True,ensure_ascii=False,separators=(",",":"))+"\n").encode("utf-8")
        require(canonical_path.read_bytes()==canonical,"Census report/canonical bytes differ")
        if first_census is None: first_census=census
        require(census==first_census,"Giant fresh reload census differs")
        require(validators["giant_extents"](census)==manifest["giantCameras"],"Giant predicted camera receipt differs")
    return park, manifest, manifest["giantCameras"]


def validate_tiles(folder, renderer, buffers, extent, args):
    report_path = folder / "capture/report.json"
    report = read(report_path)
    require(renderer == "vulkan", "Current giant capture supports Vulkan only")
    count = 1
    require(report.get("fixture") == "screenshot-cli-giant" and report.get("fixtureVersion") == 1
            and report.get("schema") == 1 and report.get("mode") == renderer and report.get("exitCode") == 0
            and report.get("error") == "" and report.get("capture") is None
            and report.get("serviceCreations") == count and report.get("deviceCreations") == count,
            "Giant observer identity/lifecycle differs")
    production = report.get("productionFactory") or {}
    require(production.get("kind") == "configured" and production.get("renderer") == "vulkan"
            and "configuredEngine" not in production and "benchmarkOverride" not in production
            and production.get("ownerCreated") is True
            and production.get("deviceObservation") == "persistent-owner-created-state",
            "Current giant capture requires the Vulkan-only production factory and one shared owner")
    checks = production.get("enabledChecks")
    require(isinstance(checks, list) and checks
            and all(check == {"renderer": "vulkan", "enabled": True} for check in checks),
            "Vulkan-only production factory availability was not observed")
    artifacts = {"report": pin(report_path)}
    state = report["assetState"]
    require(state.get("rct1Required") is True and state.get("rct1CsgLoaded") is True
            and state.get("g1RecordCount") == 29294 and type(state.get("g1PayloadCount")) is int
            and 0 < state["g1PayloadCount"] <= 29294, "Loaded CSG/G1 not observed")
    require(Path(state["configuredRct1Path"]).resolve() == args.rct1.resolve()
            and Path(state["configuredRct2Path"]).resolve() == args.rct2.resolve(), "Loaded asset roots differ")
    width,height = extent
    expected = [(x,y,min(2048,width-x),min(2048,height-y))
                for y in range(0,height,2048) for x in range(0,width,2048)]
    tiles,begins = report["tiles"],report["tileBegins"]
    require(len(tiles) == len(begins) == len(expected)
            and report["sessions"] == {"begun":len(expected),"retired":len(expected),"live":0,"peak":1},
            "Tile count/serialized session lifetime differs")
    submissions, generations = set(),set()
    row_evidence=[]
    for i,((x,y,w,h),tile,begin) in enumerate(zip(expected,tiles,begins)):
        name = f"screenshot-cli-giant-tile-{i}"
        require(begin == {"name":name,"extent":[w,h],"tick":0,"liveSessions":1,
                          "completedBeforeBegin":i,"retiredBeforeBegin":i}, "Tile begin/tick/retirement order differs")
        require(tile.get("name") == name and tile.get("ordinal") == i and tile.get("tickAtReadback") == 0
                and tile.get("completionIdentityMatches") is True and tile.get("logicalExtent") == [w,h]
                and tile.get("outputExtent") == [w,h] and w*h <= 4194304, "Tile owned identity/extent/tick differs")
        require(all(type(tile.get(k)) is int and tile[k]>0 for k in ("submissionId","targetId","targetGeneration")),
                "Invalid tile submission identity")
        require(tile["submissionId"] not in submissions and (tile["targetId"],tile["targetGeneration"]) not in generations,
                "Repeated tile submission or target lease generation")
        submissions.add(tile["submissionId"]);generations.add((tile["targetId"],tile["targetGeneration"]))
        require(tile.get("alphaPolicyValue") == 2 and tile.get("alphaPolicy") == "transparentIndexZero"
                and tile.get("scaleQuality") == 0 and tile.get("indexedOutput") is True
                and tile.get("rgbaOutput") is False and tile.get("lightingEnabled") is False
                and tile.get("clearIndex") == 0 and tile.get("indexedBytes") == w*h and tile.get("rgbaBytes") == 0,
                "Tile request/readback contract differs")
        palette=tile["requestPalette"]
        require(tile.get("resultPalette") == palette and len(palette)==256
                and all(isinstance(v,list) and len(v)==4 and all(type(c) is int and 0<=c<=255 for c in v) for v in palette)
                and bytes(c for v in palette for c in v[:3]) == buffers["palette.bin"], "Tile/PNG palette differs")
        artifact=folder/"capture"/(name+".indexed")
        data=artifact.read_bytes()
        require(len(data)==w*h, "Tile raw length differs")
        for row in range(h):
            require(data[row*w:(row+1)*w] == buffers["indexed.bin"][(y+row)*width+x:(y+row)*width+x+w],
                    "Owned tile differs from final PNG at ordinal "+str(i)+" row "+str(row))
        artifacts[name]=pin(artifact)
        row_evidence.append({"ordinal":i,"outputOffset":[x,y],"offsetEvidence":"inferred-pinned-row-major-protocol",
                             "extent":[w,h],"indexed":pin(artifact)})
    require({p.name for p in (folder/"capture").glob("*.indexed")} == {f"screenshot-cli-giant-tile-{i}.indexed" for i in range(len(expected))},
            "Unexpected tile raw artifact membership")
    return report,artifacts,row_evidence


def verify_reference(path, identity, evidence):
    prior=read(path/"summary.json"); receipt_pin=evidence.track(path/"summary.json")
    require(prior.get("schema")==3 and prior.get("kind")=="giant-cli-parity" and prior.get("status")=="pass"
            and prior.get("failures")==[] and prior.get("inputAuditFailures")==[], "Passing giant reference receipt required")
    if prior.get("renderer") == "frozen":
        require(prior.get("frozenBuildProvenance",{}).get("kind") == "frozen-screenshot-cli-build",
                "Frozen giant reference lacks qualified executable provenance")
    for key in ("park","assets","referenceReceipt","dataSha256","fixture"):
        require(prior.get(key)==identity[key], "Giant reference input differs: "+key)
    require(len(prior["cases"])==len(CASE_NAMES) and {v["name"] for v in prior["cases"]}==set(CASE_NAMES), "Reference cases incomplete")
    for case in prior["cases"]:
        require(case.get("exitCode")==0 and case.get("failures")==[], "Reference case failed")
        folder=path/case["name"]
        extent=identity["fixture"]["cameras"][case["name"].split("-")[-1]]["extent"]
        buffers=png_buffers(folder/"screen.png",extent)
        for filename in ("screen.png",*BUFFER_NAMES):
            artifact=folder/filename; expected=case["images"][filename]
            require(Path(expected["path"]).resolve()==artifact.resolve(), "Reference artifact path differs")
            evidence.track(artifact,expected["sha256"])
            if filename in buffers: require(artifact.read_bytes()==buffers[filename], "Reference PNG/raw differs")
    return prior,receipt_pin


def seam_samples(folder, extent):
    width,height=extent; boxes=[]
    for x in range(2048,width,2048): boxes.append((f"x{x}",(max(0,x-32),0,min(width,x+32),height)))
    for y in range(2048,height,2048): boxes.append((f"y{y}",(0,max(0,y-32),width,min(height,y+32))))
    for x in range(2048,width,2048):
        for y in range(2048,height,2048): boxes.append((f"junction-{x}-{y}",(x-32,y-32,min(width,x+32),min(height,y+32))))
    boxes += [("final-row",(0,max(0,height-32),width,height)),("final-column",(max(0,width-32),0,width,height))]
    result=[]
    with Image.open(folder/"screen.png") as image:
        for name,box in boxes:
            path=folder/("seam-"+name+".png");image.crop(box).save(path)
            result.append({"name":name,"box":list(box),"artifact":pin(path),"manualReview":"pending"})
    return result

def execute(args, root, output, summary, evidence):
    verification_log = output / "reference-verification.log"
    with verification_log.open("w", encoding="utf-8") as log:
        result = subprocess.run([sys.executable, str(root / "scripts/rendering/verify-software-reference.py")],
                                stdout=log, stderr=subprocess.STDOUT, timeout=180)
    require(result.returncode == 0, "Frozen reference verification failed")
    summary["referenceVerificationLog"] = pin(verification_log)
    reference_path = root / "docs/vulkan-software-reference.json"
    reference_receipt = read(reference_path)
    frozen = root / reference_receipt["localReference"]
    language_proof = {}
    park, fixture_manifest, cameras = verify_fixture(
        args.fixture_manifest, root, evidence, args.fixture_source_archive, language_proof)
    summary["fixtureProducerLanguageSources"] = language_proof
    if args.fixture_source_archive is not None:
        summary["fixtureProducerSourceArchive"] = evidence.track(args.fixture_source_archive.resolve(strict=True))
    data = frozen / "package/data"
    assets = {name: evidence.track(folder.resolve(strict=True) / "Data" / name)
              for folder, names in ((args.rct1, ("csg1.dat", "csg1i.dat")), (args.rct2, ("g1.dat",))) for name in names}
    data_hashes = evidence.tree(data)
    require(data_hashes == fixture_manifest["assetSha256"]["data"], "Fixture/capture installed source data differs")
    for asset_name, asset_root in (("rct1",args.rct1),("rct2",args.rct2)):
        require(evidence.tree(asset_root) == fixture_manifest["assetSha256"][asset_name], "Fixture/capture asset tree differs")
    receipt = None
    if args.renderer == "frozen":
        require(args.oracle_source is None,
                "Source-only frozen binaries are unqualified; use --build-receipt from build-frozen-screenshot-oracle.py")
        require(args.build_receipt is not None,
                "Frozen lane requires --build-receipt from build-frozen-screenshot-oracle.py")
        provenance = args.build_receipt.resolve(strict=True)
        receipt = read(provenance)
        require(receipt.get("schema") == 1 and receipt.get("kind") == "frozen-screenshot-cli-build"
                and receipt.get("status") == "pass" and receipt.get("exitCode") == 0,
                "Successful qualified frozen CLI build required; extraction receipts are not binary provenance")
        require(all(receipt.get(key) == [] for key in ("sourceChangesDuringBuild", "dependencyChangesDuringBuild",
                    "generatedInputChangesDuringBuild", "fixedBuildInputChangesDuringBuild", "missingArtifacts"))
                and receipt.get("frozenInputsChangedDuringBuild") is False
                and receipt.get("runtimeDllCopyMismatch") is False
                and receipt.get("compilerConcurrency") == "serial",
                "Frozen CLI build lacks complete successful stability/serial-compiler checks")
        require(receipt.get("stageResults") == [{"name": "frozen-cli-driver", "exitCode": 0}],
                "Frozen CLI driver compilation did not pass")
        evidence.track(provenance.parent / "build.log", receipt["buildLogSha256"])
        frozen_inputs = receipt["frozenInputs"]
        require(frozen_inputs.get("referenceRevision") == reference_receipt["revision"]
                and frozen_inputs.get("sourceArchiveSha256") == reference_receipt["sourceArchive"]["sha256"],
                "Frozen build does not reference the accepted frozen archive/revision")
        evidence.track(frozen / "source.zip", frozen_inputs["sourceArchiveSha256"])
        require(frozen_inputs["frozenAssetSha256"] == {"package/data/" + name: value for name, value in data_hashes.items()},
                "Frozen build asset inventory differs from capture data")
        source = Path(receipt["sourceRoot"]).resolve(strict=True)
        require(root in source.parents and frozen_inputs["originalSourceSha256"], "Frozen source root/inventory is invalid")
        for name, expected in frozen_inputs["originalSourceSha256"].items():
            evidence.track(child(source, name), expected)
        for name, expected in frozen_inputs["receiptSha256"].items():
            evidence.track(Path(name), expected)
        for name, expected in receipt["generatedInputSha256"].items():
            generated = Path(name).resolve(strict=True)
            require(provenance.parent in generated.parents, "Frozen generated input escapes build directory")
            evidence.track(generated, expected)
        evidence.track(provenance.parent / "Cli.cpp", frozen_inputs["instrumentedCliSha256"])
        require("bin/openrct2-cli.exe" in receipt["artifactSha256"]
                and "bin/libopenrct2.lib" in receipt["artifactSha256"], "Frozen build artifact inventory is incomplete")
        for name, expected in receipt["artifactSha256"].items():
            evidence.track(child(provenance.parent, name), expected)
        # This builder's DLL keys include bin/, unlike the current diagnostic builder.
        require(receipt["runtimeDllSha256"] == {name: value for name, value in receipt["artifactSha256"].items()
                                               if name.endswith(".dll")}, "Frozen DLL/artifact receipt inventories disagree")
        for name, expected in receipt["runtimeDllSha256"].items():
            require(name.startswith("bin/"), "Frozen runtime DLL receipt uses an invalid relative path")
            evidence.track(child(provenance.parent, name), expected)
        library_path = Path(receipt["libraryReceipt"]).resolve(strict=True)
        evidence.track(library_path, receipt["libraryReceiptSha256"])
        library_receipt = read(library_path)
        require(set(receipt["reusedLibraries"]) == {"core"}, "Frozen CLI must reuse only the qualified software core")
        core_reuse = receipt["reusedLibraries"]["core"]
        core_hash = receipt["artifactSha256"]["bin/libopenrct2.lib"]
        require(library_receipt.get("status") == "pass"
                and not library_receipt.get("sourceChangesDuringBuild")
                and not library_receipt.get("dependencyChangesDuringBuild")
                and Path(core_reuse["sourceReceipt"]).resolve() == library_path
                and core_reuse["sourceReceiptSha256"] == receipt["libraryReceiptSha256"]
                and core_reuse["librarySha256"] == core_hash
                and library_receipt["artifactSha256"]["bin/libopenrct2.lib"] == core_hash,
                "Frozen binary does not retain the receipt-qualified software core")
        executable = provenance.parent / "bin/openrct2-cli.exe"
        summary["frozenBuildProvenance"] = {
            "kind": receipt["kind"], "referenceRevision": frozen_inputs["referenceRevision"],
            "sourceArchiveSha256": frozen_inputs["sourceArchiveSha256"], "coreSha256": core_hash,
            "libraryReceipt": evidence.track(library_path),
            "instrumentedCliSha256": frozen_inputs["instrumentedCliSha256"]}
        summary["executableProvenanceLimit"] = (
            "New receipt-qualified frozen-core CLI build with reviewed startup path plumbing; "
            "not the original accepted package executable. Build provenance does not establish pixel parity "
            "or independently observe loaded CSG in the factory-free frozen lane.")
    else:
        require(args.build_receipt is not None, "Current lanes require --build-receipt")
        provenance = args.build_receipt.resolve(strict=True)
        receipt = read(provenance)
        require(receipt.get("status") == "pass" and receipt.get("exitCode") == 0
                and not receipt.get("sourceChangesDuringBuild") and not receipt.get("dependencyChangesDuringBuild")
                and not receipt.get("missingArtifacts"), "Successful source/dependency-stable build required")
        if args.factory == "configured":
            verify_configured_build(receipt, provenance, root, evidence)
            contract_header = root / "src/openrct2/drawing/IDrawingEngine.h"
            contract_hash = receipt["sourceSha256"].get("source/src/openrct2/drawing/IDrawingEngine.h")
            require(contract_hash is not None, "Current build must pin the Vulkan-only renderer contract")
            evidence.track(contract_header, contract_hash)
            require("#define OPENRCT2_VULKAN_ONLY 1" in contract_header.read_text(encoding="utf-8"),
                    "Current giant execution requires a Vulkan-only build; historical receipts remain comparison-only")
            summary["rendererContract"] = {"vulkanOnly": True, "header": str(contract_header), "sha256": contract_hash}
        require("bin/screenshot-parity.exe" in receipt["artifactSha256"], "Build does not qualify screenshot driver")
        for name, expected in receipt["artifactSha256"].items():
            evidence.track(child(provenance.parent, name), expected)
        for name, expected in receipt["runtimeDllSha256"].items():
            evidence.track(child(provenance.parent / "bin", name), expected)
        executable = provenance.parent / "bin/screenshot-parity.exe"
    runtime_hashes = evidence.tree(executable.parent, "*.dll", require_nonempty=False)
    expected_runtime = {name.removeprefix("bin/") if args.renderer == "frozen" else name: value
                        for name, value in receipt["runtimeDllSha256"].items()}
    require(runtime_hashes == expected_runtime, "Runtime DLL inventory differs from the qualified build")
    identity = {"renderer": args.renderer, "buildReceipt": evidence.track(provenance), "executable": evidence.track(executable),
                "runtimeDllSha256": runtime_hashes, "park": evidence.track(park),
                "assets": assets, "referenceReceipt": evidence.track(reference_path), "dataSha256": data_hashes,
                "fixture": {"kind": "giant-seams-v1", "version": 1, "caseNames": list(CASE_NAMES),
                            "inputManifest": evidence.track(args.fixture_manifest.resolve(strict=True)),
                            "cameras": cameras, "backgroundPolicy": "config-false-explicit-cli-switch"}}
    if args.factory == "configured":
        identity["factory"] = "configured"
        data = configured_data_overlay(data, output, data_hashes, evidence)
    env = {key.upper() if os.name == "nt" else key: value for key, value in os.environ.items()
           if not key.upper().startswith(("OPENRCT2_", "VK_"))}
    env.update({"OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT": "1" if args.renderer == "vulkan" else "0",
                "OPENRCT2_ORACLE_DATA_PATH": str(data), "OPENRCT2_ORACLE_RCT1_PATH": str(args.rct1.resolve()),
                "OPENRCT2_ORACLE_RCT2_PATH": str(args.rct2.resolve())})
    if args.factory == "configured":
        env["OPENRCT2_CLI_CONFIGURED_FACTORY"] = "1"
        env.pop("OPENRCT2_DIAGNOSTIC_OFFSCREEN_SCREENSHOT")
    shader_hashes = {}
    if args.renderer == "vulkan":
        require(args.shader_build_receipt is not None, "Vulkan lane requires --shader-build-receipt")
        shader_receipt_path = args.shader_build_receipt.resolve(strict=True)
        shader_receipt = read(shader_receipt_path)
        require(shader_receipt.get("status") == "pass" and not shader_receipt.get("sourceChangesDuringBuild"),
                "Successful source-stable shader build required")
        identity["shaderBuildReceipt"] = evidence.track(shader_receipt_path)
        for name, digest in shader_receipt["sourceSha256"].items():
            if name.startswith("data/shaders/vulkan/"):
                require(receipt["sourceSha256"].get("source/" + name) == digest, "Driver/shader source receipts differ: " + name)
        shaders = data / "shaders/vulkan" if args.factory == "configured" else output / "shaders"
        shaders.mkdir(parents=True, exist_ok=args.factory == "configured")
        for name, expected in shader_receipt["artifactSha256"].items():
            if name.endswith(".spv"):
                source_shader = child(root, name)
                evidence.track(source_shader, expected)
                require(source_shader.name not in shader_hashes, "Duplicate shader basename")
                target = shaders / source_shader.name
                install_shader(source_shader, target, expected, evidence,
                               data_hashes if args.factory == "configured" else None)
                shader_hashes[target.name] = expected
        require(set(shader_hashes) == SHADER_NAMES, "Shader set does not match the current receipt-qualified renderer")
        layer_settings = output / "vk_layer_settings.txt"
        layer_settings.write_text("khronos_validation.validate_sync = true\n"
                                  "khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n"
                                  "khronos_validation.log_filename = stdout\n"
                                  "khronos_validation.report_flags = error,warn\n"
                                  "khronos_validation.enable_message_limit = false\n", encoding="utf-8")
        summary["validationSettings"] = evidence.track(layer_settings)
        env.pop("VK_LAYER_ENABLES", None)
        env.update({"OPENRCT2_VULKAN_SHADER_DIRECTORY": str(shaders), "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
                    "VK_LAYER_SETTINGS_PATH": str(output), "VK_LOADER_DEBUG": "error,warn,layer"})
        if args.factory == "configured":
            env.pop("OPENRCT2_VULKAN_SHADER_DIRECTORY")
    if args.factory == "configured":
        summary["installedDataSha256"] = pin_installed_data(data, data_hashes, shader_hashes, evidence)
    env["OPENRCT2_CLI_GIANT_PARITY"] = "1"
    identity["shaderSha256"] = shader_hashes
    summary.update(identity)
    comparisons=[]
    for reference in args.compare_run:
        reference=reference.resolve(strict=True)
        previous,receipt_pin=verify_reference(reference,identity,evidence)
        comparisons.append((reference,previous));summary["comparisonReceipts"].append(receipt_pin)
    require(args.renderer=="frozen" or any(p["renderer"]=="frozen" and p.get("freshRepeat") is True for _,p in comparisons),
            "Current giant lane requires an explicit successful frozen fresh-repeat reference")
    if args.fresh_repeat:
        require(any(all(previous.get(k)==v for k,v in identity.items()) for _,previous in comparisons),
                "Fresh repeat requires the identical renderer, executable, libraries, shaders and fixture")
    config="[general]\nrct1_path = "+ini(args.rct1.resolve())+"\ngame_path = "+ini(args.rct2.resolve())+"\ntransparent_screenshot = false\n"
    for name in CASE_NAMES:
        folder=output/name;folder.mkdir();profile=folder/"profile";profile.mkdir()
        (profile/"config.ini").write_text(config,encoding="utf-8")
        (folder/"config-input.ini").write_text(config,encoding="utf-8")
        evidence.track(folder/"config-input.ini")
        env.update({"OPENRCT2_ORACLE_USER_PATH":str(profile),"OPENRCT2_CLI_PARITY_ARTIFACTS":str(folder/"capture")})
        camera=name.split("-")[-1]; rotation,zoom=int(camera[1]),int(camera[3]);extent=cameras[camera]["extent"]
        command=[str(executable),"screenshot",str(park),str(folder/"screen.png"),"giant",str(zoom),str(rotation)]
        if name.startswith("transparent"):command.append("--transparent")
        case={"name":name,"command":command,"expectedCamera":cameras[camera],"exitCode":None,
              "failures":[],"comparisons":[],"configInput":pin(folder/"config-input.ini")}
        summary["cases"].append(case)
        try:
            with (folder/"capture.log").open("w",encoding="utf-8") as log:
                completed=subprocess.run(command,cwd=executable.parent,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=args.timeout)
            case["exitCode"]=completed.returncode
            require(completed.returncode==0,"Giant screenshot command failed")
            buffers=png_buffers(folder/"screen.png",extent)
            for filename,buffer in buffers.items():(folder/filename).write_bytes(buffer)
            case["images"]={name:pin(folder/name) for name in ("screen.png",*BUFFER_NAMES)}
            case["manualReview"]={"status":"pending","fullImage":pin(folder/"screen.png"),"samples":seam_samples(folder,extent)}
            if args.renderer!="frozen":
                case["diagnostics"],case["diagnosticArtifacts"],case["tiles"]=validate_tiles(folder,args.renderer,buffers,extent,args)
            if args.renderer=="vulkan":
                lines=(folder/"capture.log").read_text(encoding="utf-8",errors="replace").splitlines()
                activated=any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
                diagnostics=[line for line in lines if any(v in line.lower() for v in
                             ("vuid-","sync-hazard","validation error","validation warning","validation performance warning"))]
                case.update({"validationActivated":activated,"validationMessages":diagnostics})
                require(activated and not diagnostics,"Validation activation absent or diagnostics emitted")
            for number,(reference,previous) in enumerate(comparisons):
                different={}
                for filename,actual in buffers.items():
                    expected=(reference/name/filename).read_bytes()
                    require(len(expected)==len(actual),"Reference raw extent differs")
                    if actual==expected:different[filename]=0
                    elif filename in ("palette.bin","alpha.bin"):
                        stride=3 if filename=="palette.bin" else 1
                        different[filename]=sum(actual[i:i+stride]!=expected[i:i+stride] for i in range(0,len(actual),stride))
                    else:
                        mode="L" if filename=="indexed.bin" else "RGBA"
                        delta=ImageChops.difference(Image.frombytes(mode,tuple(extent),actual),Image.frombytes(mode,tuple(extent),expected))
                        if mode=="RGBA":
                            channels=delta.split();delta=ImageChops.lighter(ImageChops.lighter(channels[0],channels[1]),ImageChops.lighter(channels[2],channels[3]))
                        different[filename]=extent[0]*extent[1]-delta.histogram()[0]
                        case.setdefault("divergenceRegions",[]).append({"reference":str(reference),"buffer":filename,"bounds":delta.getbbox()})
                        delta.save(folder/f"difference-{number}-{mode}.png")
                case["comparisons"].append({"reference":str(reference),"differingPixelsOrEntries":different})
                if any(different.values()):
                    case["failures"].append("Reference divergence: "+str(reference))
                    case["manualReview"]["divergenceReviewRequired"]=True
        except subprocess.TimeoutExpired:
            case["timedOut"]=True;case["failures"].append(f"Command timed out after {args.timeout} seconds; partial artifacts retained")
        except Exception as error:case["failures"].append(type(error).__name__+": "+str(error))
        finally:
            if (folder/"capture.log").is_file():case["log"]=pin(folder/"capture.log")
            if case["failures"]:
                case["partialArtifacts"]={p.relative_to(folder).as_posix():pin(p) for p in folder.rglob("*")
                                          if p.is_file() and "profile" not in p.relative_to(folder).parts}
                if "manualReview" in case:case["manualReview"]["failureReviewRequired"]=True
            summary["failures"].extend(name+": "+failure for failure in case["failures"])
            write_summary(output,summary,complete=False)
    require(len(summary["cases"])==len(CASE_NAMES),"Missing giant cases")
    summary["backgroundPolicyDistinctions"]=[]
    for camera in [name.removeprefix("ordinary-") for name in CASE_NAMES if name.startswith("ordinary-")]:
        ordinary=(output/("ordinary-"+camera)/"indexed.bin").read_bytes()
        transparent=(output/("transparent-"+camera)/"indexed.bin").read_bytes()
        require(len(ordinary)==len(transparent),"Background policy image lengths differ")
        # Byte-counted in C through Pillow; no Python per-pixel loop on giant images.
        extent=cameras[camera]["extent"]
        delta=ImageChops.difference(Image.frombytes("L",tuple(extent),ordinary),Image.frombytes("L",tuple(extent),transparent))
        changed=len(ordinary)-delta.histogram()[0]
        require(changed>0,"Background policies produced identical outputs")
        summary["backgroundPolicyDistinctions"].append({"camera":camera,"differentIndexedPixels":changed})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer",choices=("frozen","vulkan"),required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--fixture-manifest",type=Path,required=True)
    parser.add_argument("--fixture-source-archive",type=Path,
                        help="Exact historical producer code bytes bound to the unchanged accepted fixture manifest")
    parser.add_argument("--build-receipt",type=Path,required=True)
    parser.add_argument("--shader-build-receipt",type=Path)
    parser.add_argument("--rct1",type=Path,required=True)
    parser.add_argument("--rct2",type=Path,required=True)
    parser.add_argument("--compare-run",type=Path,action="append",default=[])
    parser.add_argument("--fresh-repeat",action="store_true")
    parser.add_argument("--timeout",type=int,default=300)
    args=parser.parse_args();args.factory="diagnostic" if args.renderer=="frozen" else "configured";args.oracle_source=None
    require(0<args.timeout<=900,"Timeout must be bounded between 1 and 900 seconds")
    root=Path(__file__).resolve().parents[2];output=args.output.resolve()
    require(not output.exists() and root in output.parents,"Output must be a new workspace directory")
    output.mkdir(parents=True);evidence=Evidence()
    summary={"schema":3,"kind":"giant-cli-parity","status":"incomplete","renderer":args.renderer,"cases":[],
             "failures":[],"comparisonReceipts":[],"freshRepeat":args.fresh_repeat,"runnerSha256":sha(Path(__file__)),
             "scope":"Exact giant indexed PNG/palette/alpha/RGBA and serialized bounded owned tile readbacks; manual review is separately required.",
             "loadedAssetEvidenceLimit":"Loaded CSG/G1 are observed at Vulkan factory creation. External frozen and historical software captures have no post-load hook; independently qualified fixture reload census does not prove those CLI processes loaded the same CSG.",
             "cameraEvidenceLimit":"Giant camera bounds derive independently from the exact frozen algorithm and accepted surface census; PNG extent is directly checked, while CLI camera coordinates have no post-load observation hook.",
             "deviceEvidenceLimit":"Configured owner IsCreated observes its persistent zero/one device state; no Vulkan API creation interception.",
             "manualReviewStatus":"pending"}
    write_summary(output,summary,False);completed=False
    try:
        require(not args.fresh_repeat or args.compare_run,"Fresh repeat needs a prior identical lane")
        evidence.track(Path(__file__));evidence.track(HELPER)
        evidence.track(root/"scripts/rendering/verify-software-reference.py")
        execute(args,root,output,summary,evidence);completed=True
    except Exception as error:
        summary["failures"].append(type(error).__name__+": "+str(error))
        (output/"runner-error.log").write_text(traceback.format_exc(),encoding="utf-8")
    finally:
        if not completed and not summary["failures"]:summary["failures"].append("Runner interrupted")
        try:
            summary["inputAuditFailures"]=evidence.audit();summary["failures"].extend(summary["inputAuditFailures"])
        except Exception as error:summary["failures"].append("Input audit failed: "+str(error))
        summary["inputSha256"]=evidence.files
        write_summary(output,summary,True)
    print(json.dumps({"status":summary["status"],"cases":len(summary["cases"]),"failures":summary["failures"],"output":str(output)}))
    raise SystemExit(bool(summary["failures"]))

if __name__=="__main__":main()
