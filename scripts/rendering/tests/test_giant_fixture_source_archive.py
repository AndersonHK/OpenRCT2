"""Filesystem-only historical archive guards; no builds, game, or GPU processes."""
import ast
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

helper_path = Path(__file__).resolve().parents[1] / "archive-giant-fixture-producer.py"
spec = importlib.util.spec_from_file_location("giant_archive", helper_path)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
runner_path = helper_path.with_name("run-giant-screenshot-parity.py")
tree = ast.parse(runner_path.read_text(encoding="utf-8"))
nodes = [node for node in tree.body if
         isinstance(node, ast.FunctionDef) and node.name == "fixture_source_archive" or
         isinstance(node, ast.Assign) and any(isinstance(target, ast.Name)
             and target.id == "ROOT_PRODUCER_BUILD_METADATA" for target in node.targets)]
namespace = {"Path": Path, "sha": helper.sha, "read": helper.read, "require": helper.require, "child": helper.inside, "importlib": importlib}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(runner_path), "exec"), namespace)
verify = namespace["fixture_source_archive"]


class Evidence:
    def __init__(self):
        self.files = {}
    def track(self, path, expected=None):
        digest = helper.sha(path)
        helper.require(expected is None or digest == expected, "Evidence hash changed")
        self.files[str(path)] = digest


class GiantProducerArchive(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.evidence = self.root / "obj/vulkan-parity"
        self.evidence.mkdir(parents=True)
        self.contents = {"src/core.cpp": b"original core", "data/shaders/vulkan/test.vert": b"original shader",
                         "openrct2.vulkan.props": b"original props", "assets.json": b"asset metadata",
                         "data/object.dat": b"asset", "test/terrain-parity/GiantScreenshotRecipe.h": b"recipe",
                         "test/ui-parity/BalloonFixtureState.h": b"census",
                         helper.HISTORICAL_BUILDER: b"original builder", helper.HISTORICAL_FIXTURE_HELPER: b"original helper"}
        for name, data in self.contents.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        sources = {("harness/" if name.startswith("test/") else "source/") + name: helper.sha(self.root / name)
                   for name in self.contents}
        self.producer = {"status": "pass", "sourceRoot": str(self.root), "sourceSha256": sources,
                         "sourceChangesDuringBuild": [], "dependencyChangesDuringBuild": [], "missingArtifacts": []}
        self.producer["builderSha256"] = {str(self.root / helper.HISTORICAL_BUILDER):
                                          helper.sha(self.root / helper.HISTORICAL_BUILDER)}
        self.producer_path = self.evidence / "producer.json"
        self.write(self.producer_path, self.producer)
        self.fixture = {"schema": 1, "fixture": "giant-seams-v1", "accepted": True, "inputAuditFailures": [],
                        "builds": {"current": {"receipt": str(self.producer_path), "sha256": helper.sha(self.producer_path)}},
                        "inputSha256": {str(self.root / name): helper.sha(self.root / name) for name in self.contents}}
        self.fixture["giantCameras"] = {"camera": "fixed"}
        self.fixture["park"] = {"path": str(self.root / "data/object.dat"), "sha256": helper.sha(self.root / "data/object.dat")}
        self.fixture_path = self.evidence / "fixture.json"
        self.write(self.fixture_path, self.fixture)
        self.output = self.evidence / "complete"
        current_verifier_helper = self.root / "scripts/rendering/archive-giant-fixture-producer.py"
        current_verifier_helper.write_bytes(helper_path.read_bytes())
        def summary(renderer, fresh, references, name):
            value = {"schema": 3, "kind": "giant-cli-parity", "status": "pass", "renderer": renderer,
                     "freshRepeat": fresh, "failures": [], "inputAuditFailures": [],
                     "park": self.fixture["park"], "buildReceipt": renderer, "executable": renderer,
                     "fixture": {"kind": "giant-seams-v1", "version": 1, "caseNames": list(helper.GIANT_CASE_NAMES),
                         "cameras": self.fixture["giantCameras"], "backgroundPolicy": "config-false-explicit-cli-switch",
                         "inputManifest": {"path": str(self.fixture_path), "sha256": helper.sha(self.fixture_path)}},
                     "inputSha256": {str(self.root / helper.HISTORICAL_FIXTURE_HELPER):
                                     helper.sha(self.root / helper.HISTORICAL_FIXTURE_HELPER)},
                     "comparisonReceipts": references,
                     "cases": [{"name": case, "exitCode": 0, "failures": [],
                                "comparisons": [{"differingPixelsOrEntries": {"indexed.bin": 0}}]} for case in helper.GIANT_CASE_NAMES]}
            path = self.evidence / name
            self.write(path, value)
            return {"path": str(path), "sha256": helper.sha(path)}
        frozen_first = summary("frozen", False, [], "frozen01.json")
        frozen_repeat = summary("frozen", True, [frozen_first], "frozen02.json")
        software_first = summary("software", False, [frozen_repeat], "software01.json")
        software_repeat = summary("software", True, [frozen_repeat, software_first], "software02.json")
        self.summaries = [frozen_repeat, software_repeat]

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def prior(self, source="src/core.cpp", artifact="original.cpp", *, fixture_hash=None):
        directory = self.evidence / "prior"
        directory.mkdir(exist_ok=True)
        (directory / "original.cpp").write_bytes(self.contents["src/core.cpp"])
        path = directory / "manifest.json"
        self.write(path, {"schema": 1, "fixtureManifestSha256": fixture_hash or helper.sha(self.fixture_path),
                         "files": [{"source": source, "archive": artifact,
                                    "sha256": self.fixture["inputSha256"].get(str(self.root / source))}]})
        return path, helper.sha(path)

    def make(self, prior=()):
        return helper.archive(self.root, self.fixture_path, self.output, prior, helper_path, self.summaries)

    def test_complete_archive_preserves_old_bytes_and_excludes_fixture_inputs(self):
        prior = self.prior()
        (self.root / "src/core.cpp").write_bytes(b"future source")
        result = self.make([prior])
        self.assertEqual({entry["source"] for entry in result["files"]},
                         {"src/core.cpp", "data/shaders/vulkan/test.vert", "openrct2.vulkan.props",
                          helper.HISTORICAL_BUILDER, helper.HISTORICAL_FIXTURE_HELPER})
        self.assertEqual((self.output / "source/src/core.cpp").read_bytes(), b"original core")
        self.assertEqual(result["origins"]["src/core.cpp"]["kind"], "pinned-prior-archive")
        kinds = {entry["source"]: entry["kind"] for entry in result["files"]}
        self.assertEqual(kinds[helper.HISTORICAL_BUILDER], "producer-builder")
        self.assertEqual(kinds[helper.HISTORICAL_FIXTURE_HELPER], "fixture-helper")
        replacements = verify(self.output / "manifest.json", self.fixture_path, self.fixture, self.root, Evidence())
        self.assertEqual(len(replacements), 5)
        self.assertNotIn(str(self.root / "test/ui-parity/BalloonFixtureState.h"), replacements)

    def test_missing_original_never_archives_future_live_bytes(self):
        (self.root / "src/core.cpp").write_bytes(b"future source")
        with self.assertRaisesRegex(ValueError, "Pinned input changed"):
            self.make()
        self.assertFalse((self.output / "manifest.json").exists())
        self.assertTrue((self.output / "failure-receipt.json").is_file())

    def test_fixture_and_producer_hash_must_agree(self):
        self.fixture["inputSha256"][str(self.root / "src/core.cpp")] = "0" * 64
        self.write(self.fixture_path, self.fixture)
        with self.assertRaisesRegex(ValueError, "identically attested"):
            self.make()

    def test_failed_producer_is_rejected(self):
        self.producer["status"] = "fail"
        self.write(self.producer_path, self.producer)
        self.fixture["builds"]["current"]["sha256"] = helper.sha(self.producer_path)
        self.write(self.fixture_path, self.fixture)
        with self.assertRaisesRegex(ValueError, "Successful source-stable"):
            self.make()

    def test_prior_receipt_hash_and_fixture_identity_are_required(self):
        prior = self.prior()
        with self.assertRaisesRegex(ValueError, "Pinned input changed"):
            self.make([(prior[0], "0" * 64)])
        prior = self.prior(fixture_hash="0" * 64)
        with self.assertRaisesRegex(ValueError, "different fixture"):
            self.make([prior])

    def test_duplicate_prior_source_is_rejected(self):
        prior = self.prior()
        with self.assertRaisesRegex(ValueError, "Duplicate source"):
            self.make([prior, prior])

    def test_prior_artifact_cannot_escape(self):
        prior = self.prior(artifact="../producer.json")
        with self.assertRaisesRegex(ValueError, "Noncanonical"):
            self.make([prior])

    def test_assets_recipes_census_and_unknown_root_metadata_are_ineligible(self):
        for name in ("assets.json", "licence.txt", ".commitlint.json", "arbitrary.props", "data/object.dat",
                     "test/terrain-parity/GiantScreenshotRecipe.h", "test/ui-parity/BalloonFixtureState.h",
                     "src/../assets.json", "src//core.cpp", "src\\core.cpp"):
            with self.subTest(name=name):
                self.assertFalse(helper.eligible(name))
        self.assertEqual(helper.ROOT_BUILD_METADATA, namespace["ROOT_PRODUCER_BUILD_METADATA"])

    def test_mutation_during_copy_has_no_passing_manifest(self):
        real_copy = helper.shutil.copyfile
        def corrupt(source, destination):
            real_copy(source, destination)
            source.write_bytes(b"changed during capture")
        with patch.object(helper.shutil, "copyfile", side_effect=corrupt):
            with self.assertRaisesRegex(ValueError, "changed during archive copy"):
                self.make()
        self.assertFalse((self.output / "manifest.json").exists())

    def test_existing_output_cannot_be_overwritten(self):
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, "Output must be new"):
            self.make()

    def test_verifier_can_use_deleted_original_source_but_rejects_asset_substitution(self):
        result = self.make()
        (self.root / "src/core.cpp").unlink()
        replacements = verify(self.output / "manifest.json", self.fixture_path, self.fixture, self.root, Evidence())
        self.assertEqual(len(replacements), 5)
        result["files"].append({"source": "assets.json", "archive": "source/openrct2.vulkan.props",
                                "sha256": self.fixture["inputSha256"][str(self.root / "assets.json")]})
        self.write(self.output / "manifest.json", result)
        with self.assertRaisesRegex(ValueError, "Only historical producer"):
            verify(self.output / "manifest.json", self.fixture_path, self.fixture, self.root, Evidence())

    def test_historical_builder_needs_successful_producer_attestation(self):
        self.producer["builderSha256"] = {}
        self.write(self.producer_path, self.producer)
        self.fixture["builds"]["current"]["sha256"] = helper.sha(self.producer_path)
        self.write(self.fixture_path, self.fixture)
        with self.assertRaisesRegex(ValueError, "producer.builderSha256"):
            self.make()

    def test_historical_helper_needs_both_fresh_lanes(self):
        self.summaries = self.summaries[:1]
        with self.assertRaisesRegex(ValueError, "Both frozen and current-software"):
            self.make()

    def test_fresh_summary_must_match_prior_build_identity(self):
        path = Path(self.summaries[0]["path"])
        data = helper.read(path)
        data["buildReceipt"] = "different build"
        self.write(path, data)
        self.summaries[0]["sha256"] = helper.sha(path)
        with self.assertRaisesRegex(ValueError, "identical build/fixture identity"):
            self.make()

    def test_current_summary_helper_digest_must_match_fixture(self):
        path = Path(self.summaries[0]["path"])
        data = helper.read(path)
        data["inputSha256"][str(self.root / helper.HISTORICAL_FIXTURE_HELPER)] = "0" * 64
        self.write(path, data)
        self.summaries[0]["sha256"] = helper.sha(path)
        with self.assertRaisesRegex(ValueError, "independently successful summary"):
            self.make()

    def test_tooling_kind_cannot_substitute_attestation_rule(self):
        result = self.make()
        next(entry for entry in result["files"] if entry["source"] == helper.HISTORICAL_FIXTURE_HELPER)["kind"] = "producer-builder"
        self.write(self.output / "manifest.json", result)
        with self.assertRaisesRegex(ValueError, "tooling kind"):
            verify(self.output / "manifest.json", self.fixture_path, self.fixture, self.root, Evidence())
    def test_verifier_rejects_duplicate_and_wrong_hash_archive_entries(self):
        result = self.make()
        result["files"].append(result["files"][0])
        self.write(self.output / "manifest.json", result)
        with self.assertRaisesRegex(ValueError, "uniquely attested"):
            verify(self.output / "manifest.json", self.fixture_path, self.fixture, self.root, Evidence())
        result["files"].pop()
        result["files"][0]["sha256"] = "0" * 64
        self.write(self.output / "manifest.json", result)
        with self.assertRaisesRegex(ValueError, "uniquely attested"):
            verify(self.output / "manifest.json", self.fixture_path, self.fixture, self.root, Evidence())


if __name__ == "__main__":
    unittest.main()
