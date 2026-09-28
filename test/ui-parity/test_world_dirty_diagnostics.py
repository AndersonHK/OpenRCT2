"""Pure checks of the staged dirty-world receipt gate; no engine or GPU execution."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("world_dirty_runner", ROOT / "scripts/rendering/run-ui-parity.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def captures(incremental):
    result = {}
    rectangles = [[], [[240,160,720,192]], [[464,304,496,336]],
                  [[416,272,440,296],[448,296,472,320],[480,320,504,344],[512,344,536,368],[544,368,568,392]],
                  [[240,480,720,512]], []]
    for repeat in range(2):
        for i, phase in enumerate(runner.FIXTURE_STEPS["world-dirty"]):
            full = i in (0,5)
            metadata = {"mainViewportBounds":[0,0,960,640], "simulationTicks":173,
                        "viewPosition":[-479,145], "rotation":0,"zoom":0,"viewportFlags":0,
                        "paletteEffectFrame":0,"paintStableSort":False,"landscapeSmoothing":False,"cameraMode":"explicit",
                        "forcedFullInvalidation":not incremental or (repeat == 0 and i == 0),
                        "cameraContract":{"paintOrdinal":3+repeat*6+i},
                        "worldDirtyDrawCount":{"before":102+repeat*6+i,"after":103+repeat*6+i},
                        "inputState":{"detail":{"worldDirty":{"schema":1,"phase":phase,"mutation":"none",
                            "viewport":[0,0,960,640],"requestedFullInvalidation":full,"requestedRectangles":rectangles[i],
                            "api":"GfxInvalidateScreen" if full else "IDrawingEngine::Invalidate"}}}}
            result[phase+"-"+str(repeat)] = {"metadata":metadata,"indexedSha256":"same-indexed","rgbaSha256":"same-rgba"}
    return result


class WorldDirtyReceiptTests(unittest.TestCase):
    def test_full_and_requested_dirty_sequences_keep_static_output(self):
        for incremental in (False, True):
            self.assertEqual([], runner.validate_world_dirty_samples(captures(incremental), incremental))

    def test_dirty_only_divergence_is_failure_even_when_full_restore_matches(self):
        data = captures(True)
        data["dirty-upper-canopy-0"]["indexedSha256"] = "wrong-order"
        self.assertTrue(any("Static dirty-world repaint changed dirty-upper-canopy-0" in failure
                            for failure in runner.validate_world_dirty_samples(data, True)))

    def test_missing_phase_or_changed_requested_domain_fails(self):
        data = captures(True)
        del data["dirty-diagonal-1"]
        self.assertTrue(runner.validate_world_dirty_samples(data, True))
        data = captures(True)
        data["dirty-interior-0"]["metadata"]["inputState"]["detail"]["worldDirty"]["requestedRectangles"] = [[0,0,960,640]]
        self.assertTrue(any("invalidation contract" in failure for failure in runner.validate_world_dirty_samples(data, True)))

    def test_hidden_tick_or_paint_counter_manipulation_fails(self):
        for key, value in (("simulationTicks",174),("worldDirtyDrawCount",{"before":103,"after":103})):
            data = copy.deepcopy(captures(True))
            data["dirty-interior-0"]["metadata"][key] = value
            self.assertTrue(runner.validate_world_dirty_samples(data, True))


if __name__ == "__main__":
    unittest.main()
