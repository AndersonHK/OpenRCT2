"""Verify byte preservation/deduplication and cleanup classification."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("curation", Path(__file__).with_name("archive-vulkan-artifacts.py"))
curation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(curation)


class ArchiveTests(unittest.TestCase):
    def test_classification_preserves_sources_inputs_and_live_evidence(self):
        for path in ["reference-9a092745f3/data/g1.dat", "ordering187-fixtures/user-images/a.png",
                     "guest-shop207-control/raw.bin", "old/source/data/unique.json", "old/bin/owner.park",
                     "old/int/candidate.patch", "old/.git/objects/pack/a.idx"]:
            self.assertIsNone(curation.classify(path), path)
        self.assertEqual(curation.classify("old/int/core/test.obj"), "discard-compiler")
        self.assertEqual(curation.classify("old/bin/test.exe"), "binaries")

    def test_archive_deduplicates_and_restores_original_bytes(self):
        with tempfile.TemporaryDirectory(prefix="archive-test-", dir=curation.NAMESPACE) as folder:
            root = Path(folder)
            scope = root / "case"
            scope.mkdir()
            payload = b"guard" * 1000
            for name in ("one.bin", "two.bin"):
                (scope / name).write_bytes(payload)
            (scope / "notes.json").write_text('{"failure":"preserve"}')
            output = root / "archive"
            curation.prepare(output, scope)
            state = json.loads((output / "archive-plan.json").read_text())
            self.assertEqual(state["selectedFiles"], 2)
            self.assertEqual(next(a for a in state["archives"] if a["path"].endswith("raw.zip"))["verifiedUniquePayloads"], 1)
            relative = (scope / "two.bin").relative_to(curation.ROOT).as_posix()
            restored = root / "restored.bin"
            curation.restore(output, relative, restored)
            self.assertEqual(restored.read_bytes(), payload)
            self.assertEqual(restored.stat().st_mtime_ns, (scope / "two.bin").stat().st_mtime_ns)
            self.assertTrue((scope / "one.bin").exists())
            self.assertTrue((scope / "notes.json").exists())

    def test_corrupt_payload_and_external_restore_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="archive-test-", dir=curation.NAMESPACE) as folder:
            root = Path(folder)
            archive = root / "bad.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("a" * 64, b"not the addressed payload")
            with self.assertRaises(ValueError):
                curation.verify_archive(archive)
            with self.assertRaises(ValueError):
                curation.contained(curation.ROOT / "src/wrong.bin")

    @unittest.skipUnless(os.name == "nt", "Native Windows archive apply")
    def test_native_apply_preserves_kept_bin_and_metadata(self):
        with tempfile.TemporaryDirectory(prefix="archive-test-", dir=curation.NAMESPACE) as folder:
            root = Path(folder)
            scope = root / "case"
            live = scope / "bin"
            live.mkdir(parents=True)
            (live / "libopenrct2.lib").write_bytes(b"selected reusable library")
            (scope / "result.json").write_text('{"failure":"keep"}')
            (scope / "readback.bin").write_bytes(b"actual payload")
            output = root / "archive"
            curation.prepare(output, scope, keep=(live,))
            command = ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(Path(__file__).with_name("apply-vulkan-archive.ps1")),
                       "-Plan", str(output / "archive-plan.json")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((scope / "readback.bin").exists())
            self.assertTrue((scope / "result.json").exists())
            self.assertTrue((live / "libopenrct2.lib").exists())
            self.assertEqual(json.loads((output / "archive-result.json").read_text(encoding="utf-8-sig"))["removedFiles"], 1)


if __name__ == "__main__":
    unittest.main()
