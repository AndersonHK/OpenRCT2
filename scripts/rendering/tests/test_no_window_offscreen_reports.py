"""Filesystem-only strict offscreen report validation; never starts Vulkan."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

source = Path(__file__).resolve().parents[1] / "run-no-window-contracts.py"
spec = importlib.util.spec_from_file_location("offscreen_contracts", source)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class OffscreenReportInventory(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        self.base = self.output / "samples/offscreen"
        for relative, (width, height, reference, metadata) in runner.offscreen_report_contracts().items():
            fixture, layer, _ = relative.split("/")
            folder = (self.base / relative).parent
            folder.mkdir(parents=True)
            # Preserve native producer field types; synthetic reports previously hid a scalar/array mismatch.
            report = json.loads((Path(__file__).parent / "fixtures/offscreen-equal-report.json").read_text())
            report.update(metadata)
            report.update(fixture=fixture, layer=layer, width=width, height=height)
            (folder / "report.json").write_text(json.dumps(report), encoding="utf-8")
            for label in (reference, "vulkan"):
                with (folder / (label + ".bin")).open("wb") as stream:
                    stream.truncate(width * height * (1 if layer == "indexed" else 4))
                (folder / (label + ".png")).write_bytes(b"artifact presence only; runner does not decode PNG")
            (folder / "diff.png").write_bytes(b"artifact")

    def test_exact_26_reports_and_declared_dimensions(self):
        failures, reports = runner.verify_offscreen_reports(self.output)
        self.assertEqual(failures, [])
        self.assertEqual(len(reports), 26)
        self.assertEqual(sum("water-overlay" in name for name in reports), 8)
        self.assertEqual(sum("exact-peel" in name for name in reports), 2)
        self.assertIn("ordered-remap-oracle.bin", reports["exact-peel-depth-2047x1439/indexed/report.json"])

    def test_missing_or_unexpected_reports_are_rejected(self):
        (self.base / "clear-0/indexed/report.json").unlink()
        unexpected = self.base / "extra/indexed/report.json"
        unexpected.parent.mkdir(parents=True)
        unexpected.write_text("{}")
        failures, _ = runner.verify_offscreen_reports(self.output)
        self.assertEqual(failures[0]["missingReports"], ["clear-0/indexed/report.json"])
        self.assertEqual(failures[0]["unexpectedReports"], ["extra/indexed/report.json"])

    def test_scalar_channel_error_does_not_match_native_report_schema(self):
        path = self.base / "clear-0/indexed/report.json"
        report = json.loads(path.read_text())
        report["maxChannelError"] = 0
        path.write_text(json.dumps(report))
        failures, _ = runner.verify_offscreen_reports(self.output)
        self.assertTrue(failures)

    def test_wrong_water_depth_metadata_is_rejected(self):
        path = self.base / "water-overlay-adjacent-depth-127/indexed/report.json"
        report = json.loads(path.read_text())
        report["opaqueOverlayDepth"] = 129
        path.write_text(json.dumps(report))
        failures, _ = runner.verify_offscreen_reports(self.output)
        self.assertEqual(len(failures), 1)

    def test_equal_but_truncated_raw_outputs_are_rejected(self):
        folder = self.base / "exact-peel-depth-2047x1439/indexed"
        (folder / "ordered-remap-oracle.bin").write_bytes(b"short")
        (folder / "vulkan.bin").write_bytes(b"short")
        failures, _ = runner.verify_offscreen_reports(self.output)
        self.assertEqual(len(failures), 1)

    def test_nonzero_output_or_accepted_exception_is_rejected(self):
        folder = self.base / "clear-1/indexed"
        with (folder / "vulkan.bin").open("r+b") as stream:
            stream.write(b"changed")
        path = self.base / "water-overlay-adjacent-depth-0/indexed/report.json"
        report = json.loads(path.read_text())
        report["acceptedExceptions"] = ["not allowed"]
        path.write_text(json.dumps(report))
        failures, _ = runner.verify_offscreen_reports(self.output)
        self.assertEqual(len(failures), 2)


if __name__ == "__main__":
    unittest.main()
