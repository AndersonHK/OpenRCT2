"""Filesystem-only cache-admission regressions; never invokes MSBuild or a game."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import time
import unittest

path = Path(__file__).resolve().parents[1] / "build-incremental-ui-parity.py"
spec = importlib.util.spec_from_file_location("incremental_ui", path)
cache_build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cache_build)


class CacheAdmission(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name).resolve()
        self.cache = self.source / "cache"
        self.work = self.cache / ("generation-" + "a" * 32)
        self.work.mkdir(parents=True)
        self.snapshot = self.source / "qualified-output"
        self.snapshot.mkdir()
        (self.snapshot / "receipt.json").write_text(json.dumps({"status": "pass"}))
        self.object = self.work / "example.obj"
        self.object.write_bytes(b"previous qualified object")
        self.input = self.source / "src/example.cpp"
        self.input.parent.mkdir()
        self.input.write_bytes(b"int example() { return 1; }\n")
        self.key = "source/src/example.cpp"
        self.now = time.time_ns()
        self.old_time = self.now - 20_000_000_000
        os.utime(self.object, ns=(self.old_time, self.old_time))
        os.utime(self.input, ns=(self.old_time, self.old_time))
        self.old_time = self.input.stat().st_mtime_ns
        self.before = {self.key: cache_build.build.sha256(self.input)}
        self.state = {"usable": True, "snapshotDirectory": str(self.snapshot), "privateWorkspace": str(self.work),
                      "workspaceSha256": cache_build.manifest(self.work), "signature": "same",
                      "sourceSha256": self.before, "inputMtimeNs": {self.key: self.old_time},
                      "newestWorkspaceMtimeNs": self.old_time}
        cache_build.write_json(self.snapshot / "cache-output-state.json", self.state)
        cache_build.write_json(self.snapshot / "receipt.json", {"status": "pass", "incrementalCache": {
            "outputStateSha256": cache_build.build.sha256(self.snapshot / "cache-output-state.json")}})
        self.state["publishedSha256"] = cache_build.manifest(self.snapshot)

    def changed_input(self, mtime):
        self.input.write_bytes(b"int example() { return 2; }\n")
        os.utime(self.input, ns=(mtime, mtime))
        return {self.key: cache_build.build.sha256(self.input)}

    def choose(self, inputs, signature="same"):
        return cache_build.choose_generation(self.cache, self.state, signature, inputs, self.source, self.now)

    def test_preserved_timestamp_content_edit_cannot_skip_compilation(self):
        inputs = self.changed_input(self.old_time)
        work, reason, changes = self.choose(inputs)
        self.assertIsNone(work)
        self.assertIn("mtime", reason)
        self.assertEqual(changes, [self.key])
        self.assertEqual(self.input.stat().st_mtime_ns, self.old_time)
        self.assertEqual(cache_build.manifest(self.snapshot), self.state["publishedSha256"])

    def test_newer_cpp_edit_can_use_verified_tracking(self):
        work, _, changes = self.choose(self.changed_input(self.now - 1_000_000_000))
        self.assertEqual(work, self.work)
        self.assertEqual(changes, [self.key])
        self.assertEqual(self.object.read_bytes(), b"previous qualified object")

    def test_compiler_or_dependency_signature_change_is_cold(self):
        work, _, _ = self.choose(self.before, "different")
        self.assertIsNone(work)

    def test_changed_private_object_is_rejected(self):
        self.object.write_bytes(b"unqualified replacement")
        with self.assertRaisesRegex(RuntimeError, "outside the builder"):
            self.choose(self.before)

    def test_changed_prior_evidence_is_rejected(self):
        (self.snapshot / "receipt.json").write_text('{"status":"altered"}')
        with self.assertRaisesRegex(RuntimeError, "snapshot changed"):
            self.choose(self.before)

    def test_mutable_timestamp_proof_cannot_drift_from_archived_state(self):
        self.state["inputMtimeNs"][self.key] -= 10_000_000_000
        with self.assertRaisesRegex(RuntimeError, "metadata differs"):
            self.choose(self.before)


class ObjectDirectories(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.work = self.root / "private-generation"
        self.work.mkdir()
        self.project = self.work / "core.vcxproj"

    def write_project(self, values):
        project = cache_build.build.ET.Element(cache_build.build.tag("Project"))
        group = cache_build.build.child(project, "ItemGroup")
        item = cache_build.build.child(group, "ClCompile", Include="Vehicle.MiniGolf.cpp")
        for value in values:
            cache_build.build.child(item, "ObjectFileName", value)
        cache_build.build.write_xml(self.project, project)

    def test_original_macro_and_generated_override_preserve_project_bytes(self):
        absolute = self.work / "int/core/objects/src/openrct2/ride"
        self.write_project(["$(intDIR)ride\\", str(absolute) + os.sep])
        original = self.project.read_bytes()
        cache_build.prepare_object_directories(self.project, self.work, "core")
        self.assertTrue((self.work / "int/core/ride").is_dir())
        self.assertTrue(absolute.is_dir())
        self.assertEqual(self.project.read_bytes(), original)

    def test_unknown_macro_is_rejected_without_creating_a_directory(self):
        self.write_project(["$(UncontrolledRoot)ride\\"])
        with self.assertRaisesRegex(RuntimeError, "Unsupported MSBuild expression"):
            cache_build.prepare_object_directories(self.project, self.work, "core")
        self.assertEqual(list(self.work.iterdir()), [self.project])

    def test_macro_traversal_outside_private_workspace_is_rejected(self):
        self.write_project(["$(IntDir)..\\..\\..\\escape\\"])
        with self.assertRaisesRegex(RuntimeError, "escapes private workspace"):
            cache_build.prepare_object_directories(self.project, self.work, "core")
        self.assertFalse((self.root / "escape").exists())
        self.assertEqual(list(self.work.iterdir()), [self.project])


if __name__ == "__main__":
    unittest.main()
