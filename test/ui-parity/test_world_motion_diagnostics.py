"""Pure receipt checks, no game or GPU. Run only after root integration."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("motion_runner", ROOT / "scripts/rendering/run-ui-parity.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def captures():
    result = {}
    for tick in range(17):
        for pass_index, phase in enumerate(("damage", "full")):
            ordinal = tick * 2 + pass_index + 3
            record = [42, 5, 160 + tick, 320, 16] + [0] * 20
            meta = {"simulationTicks":100 + tick,"paletteEffectFrame":tick,"cameraContract":{"paintOrdinal":ordinal},
                "forcedFullInvalidation":tick == 0 or phase == "full","inputState":{"sameTick":tick},
                "worldMotion":{"schema":1,"initialTick":100,"tickOffset":tick,"totalTicks":16,"pass":phase,
                    "positions":"authoritative; no tween","drawCount":{"before":ordinal + 2,"after":ordinal + 3},
                    "vehicles":{"schema":1,"count":1,"columns":list(range(25)),"records":[record],"scenarioRng":[tick,3]}}}
            meta["worldMotion"]["consumedSourceTick"] = 100 + tick
            meta["worldMotion"]["consumedVehicles"] = {key:copy.deepcopy(value)
                for key,value in meta["worldMotion"]["vehicles"].items() if key != "scenarioRng"}
            result[f"motion-{tick}-{phase}"] = {"metadata":meta,"rgbaSha256":str(tick),"indexedSha256":str(tick)}
    return result


class WorldMotionReceiptTests(unittest.TestCase):
    def test_complete_dynamic_sequence(self):
        self.assertEqual([], runner.validate_world_motion_samples(captures(), 16))

    def test_one_frame_repaint_divergence_is_not_hidden_by_later_match(self):
        data = captures()
        data["motion-7-damage"]["indexedSha256"] = "wrong-order"
        self.assertTrue(any("repaint differs at tick 7" in x for x in runner.validate_world_motion_samples(data, 16)))

    def test_stale_tick_or_palette_metadata_rejected(self):
        for field in ("simulationTicks", "paletteEffectFrame"):
            data = captures()
            data["motion-7-full"]["metadata"][field] = 0
            self.assertTrue(runner.validate_world_motion_samples(data, 16))

    def test_vehicle_or_rng_state_change_between_paints_rejected(self):
        for field in ("records", "scenarioRng"):
            data = captures()
            data["motion-7-full"]["metadata"]["worldMotion"]["vehicles"][field] = []
            self.assertTrue(runner.validate_world_motion_samples(data, 16))

    def test_missing_capture_and_motionless_park_rejected(self):
        data = captures()
        del data["motion-7-damage"]
        self.assertTrue(runner.validate_world_motion_samples(data, 16))
        data = captures()
        fixed = copy.deepcopy(data["motion-0-damage"]["metadata"]["worldMotion"]["vehicles"]["records"])
        for sample in data.values():
            sample["metadata"]["worldMotion"]["vehicles"]["records"] = copy.deepcopy(fixed)
        self.assertTrue(any("did not move" in x for x in runner.validate_world_motion_samples(data, 16)))

    def test_identical_live_state_cannot_hide_different_consumed_publications(self):
        for key in ("consumedSourceTick", "consumedVehicles"):
            data = captures()
            motion = data["motion-7-full"]["metadata"]["worldMotion"]
            motion[key] = copy.deepcopy(data["motion-6-full"]["metadata"]["worldMotion"][key])
            self.assertTrue(any("consumed publication changed at tick 7" in x
                for x in runner.validate_world_motion_samples(data, 16)))

    def test_unstamped_legacy_captures_cannot_qualify_motion(self):
        data = captures()
        del data["motion-7-full"]["metadata"]["worldMotion"]["consumedSourceTick"]
        self.assertTrue(any("Missing or malformed consumed publication" in x
            for x in runner.validate_world_motion_samples(data, 16)))


if __name__ == "__main__":
    unittest.main()
