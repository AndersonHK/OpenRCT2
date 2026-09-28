"""Reject performance claims based on requested rather than observed output size."""
import importlib.util
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / "run-render-performance.py"
spec = importlib.util.spec_from_file_location("render_performance", source)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class DisplayEvidence(unittest.TestCase):
    def parse(self, extra=""):
        log = (Path(__file__).parent / "fixtures/render-performance-checkpoint52.txt").read_text()
        return runner.parse_log(log.replace("  elapsed:", extra + "  elapsed:"))

    def test_old_report_remains_readable_but_cannot_certify_4k(self):
        result = self.parse()
        self.assertNotIn("displayObservation", result)
        with self.assertRaisesRegex(ValueError, "unavailable"):
            runner.validate_display_observation(result, 3840, 2160)

    def test_observed_4k_and_refresh(self):
        result = self.parse("  drawable pixels: 3840 x 2160 initial, 3840 x 2160 final\n"
                            "  monitor refresh: 144 Hz initial, 144 Hz final\n")
        runner.validate_display_observation(result, 3840, 2160)
        self.assertEqual(result["displayObservation"]["initialExtent"], [3840, 2160])
        self.assertIn("displayed presentation cadence is not measured", result["missingMetrics"])

    def test_wrong_extent_resize_and_refresh_change_are_rejected(self):
        for width, height, hz in ((960, 640, 144), (3840, 2160, 60), (0, 0, 0)):
            with self.subTest(width=width, height=height, hz=hz):
                result = self.parse("  drawable pixels: 3840 x 2160 initial, %d x %d final\n"
                                    "  monitor refresh: 144 Hz initial, %d Hz final\n" % (width, height, hz))
                with self.assertRaises(ValueError):
                    runner.validate_display_observation(result, 3840, 2160)

    def test_partial_or_duplicate_evidence_is_rejected(self):
        extent = "  drawable pixels: 3840 x 2160 initial, 3840 x 2160 final\n"
        for extra in (extent, extent + extent + "  monitor refresh: 144 Hz initial, 144 Hz final\n"):
            with self.assertRaises(ValueError):
                self.parse(extra)


if __name__ == "__main__":
    unittest.main()
