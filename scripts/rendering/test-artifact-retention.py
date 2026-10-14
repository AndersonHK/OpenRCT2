"""Check failure retention and containment before any destructive helper use."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("retention", Path(__file__).with_name("artifact-retention.py"))
retention = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retention)


class RetentionTests(unittest.TestCase):
    def setUp(self):
        namespace = Path(__file__).resolve().parents[2] / "obj/vulkan-parity"
        self.temporary = tempfile.TemporaryDirectory(prefix="retention-test-", dir=namespace)
        self.output = Path(self.temporary.name)
        self.samples = self.output / "samples/guards/0"
        self.samples.mkdir(parents=True)
        for name in ("before.bin", "parents.bin", "commands.bin", "columns.bin", "report.json", "vulkan.png"):
            (self.samples / name).write_bytes(b"guard evidence")

    def tearDown(self):
        self.temporary.cleanup()

    def test_success_keeps_images_status_and_hashed_journal(self):
        result = retention.compact(self.output, passed=True, directories=("samples",))
        self.assertEqual(result["removedBytes"], 42)
        self.assertEqual({p.name for p in self.samples.iterdir()}, {"columns.bin", "report.json", "vulkan.png"})
        receipt = json.loads((self.output / "scratch-retention-terrain.json").read_text())
        self.assertEqual(receipt["status"], "complete")
        self.assertTrue(all(len(entry["sha256"]) == 64 for entry in receipt["discarded"]))

    def test_failure_and_explicit_reference_preserve_all_outputs(self):
        for passed, retain in ((False, False), (True, True)):
            result = retention.compact(self.output, passed=passed, retain=retain, directories=("samples",))
            self.assertEqual(result["removedBytes"], 0)
            self.assertEqual(len(list(self.samples.iterdir())), 6)

    def test_namespace_root_and_outside_paths_rejected(self):
        for path in (self.output.parent, self.output.parents[2]):
            with self.assertRaises(ValueError):
                retention.compact(path, passed=True, directories=("samples",))

    def test_traversal_rejected(self):
        with self.assertRaises(ValueError):
            retention.compact(self.output, passed=True, directories=("../outside",))
        self.assertEqual(len(list(self.samples.iterdir())), 6)

    def test_build_removes_int_only_and_keeps_runnable_reuse(self):
        (self.output / "int").mkdir()
        (self.output / "int/test.obj").write_bytes(b"compiler")
        (self.output / "bin").mkdir()
        for name in ("test.exe", "test.lib"):
            (self.output / "bin" / name).write_bytes(b"runtime")
        retention.compact(self.output, passed=True, kind="build", directories=("int",))
        self.assertTrue((self.output / "bin/test.exe").is_file())
        self.assertTrue((self.output / "bin/test.lib").is_file())
        self.assertFalse((self.output / "int/test.obj").exists())

    def test_cache_journal_does_not_break_subsequent_snapshot_reuse(self):
        builder_spec = importlib.util.spec_from_file_location("incremental", Path(__file__).with_name("build-incremental-ui-parity.py"))
        builder = importlib.util.module_from_spec(builder_spec)
        builder_spec.loader.exec_module(builder)
        cache = self.output / "cache"
        active = cache / ("generation-" + "a" * 32)
        old = cache / ("generation-" + "b" * 32)
        active.mkdir(parents=True)
        old.mkdir()
        (active / "test.obj").write_bytes(b"current")
        (old / "test.obj").write_bytes(b"obsolete")
        snapshot = self.output / "snapshot"
        snapshot.mkdir()
        state = {"usable": True, "privateWorkspace": str(active), "snapshotDirectory": str(snapshot),
                 "workspaceSha256": builder.manifest(active), "signature": {}, "sourceSha256": {},
                 "newestWorkspaceMtimeNs": 0, "inputMtimeNs": {}}
        builder.write_json(snapshot / "cache-output-state.json", state)
        builder.write_json(snapshot / "receipt.json", {"status": "pass", "incrementalCache": {
            "outputStateSha256": builder.build.sha256(snapshot / "cache-output-state.json")}})
        retention.compact(cache, passed=True, kind="cache", directories=(old.name,), journal_root=snapshot)
        state["publishedSha256"] = builder.manifest(snapshot)
        chosen, _, _ = builder.choose_generation(cache, state, {}, {}, Path(__file__).resolve().parents[2], 0)
        self.assertEqual(chosen, active)
        self.assertFalse((old / "test.obj").exists())

    @unittest.skipUnless(os.name == "nt", "Windows junction containment")
    def test_in_tree_junction_refused_without_touching_target(self):
        link = self.output / "redirect"
        command = "New-Item -ItemType Junction -Path '" + str(link).replace("'", "''") + "' -Target '" + str(self.samples).replace("'", "''") + "' | Out-Null"
        subprocess.run(["powershell", "-NoProfile", "-Command", command], check=True, capture_output=True)
        try:
            with self.assertRaises(ValueError):
                retention.compact(self.output, passed=True, directories=("redirect",))
            self.assertTrue((self.samples / "before.bin").exists())
        finally:
            os.rmdir(link)


if __name__ == "__main__":
    unittest.main()
