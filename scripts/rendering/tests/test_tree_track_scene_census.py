"""No game/GPU: validate scene census ownership and absence-vs-missing distinction."""
import ast
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / "run-ui-parity.py"
node = next(n for n in ast.parse(source.read_text(encoding="utf-8")).body
            if isinstance(n, ast.FunctionDef) and n.name == "validate_tree_track_census")
namespace = {}
exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), "exec"), namespace)
validate = namespace["validate_tree_track_census"]


class TreeTrackCensusTests(unittest.TestCase):
    def setUp(self):
        self.census = {"schemaVersion":1,"kind":"loaded-tree-wooden-track-scene-locator","mutatedWorld":False,
            "paused":True,"simulationTicks":123,"simulationTicksAfter":123,"unresolvedScenery":0,
            "searchRadiusTiles":8,"candidateLimit":64,"viewportExtent":[960,640],"mapSize":[128,128],
            "treeCounts":{},"woodenStyleCounts":[],"matchedTrees":0,"woodenTrackElements":0,"candidates":[]}
        self.samples = {"frame":{"metadata":{"simulationTicks":123,
            "sceneLocator":{"simulationTicks":123,"viewportExtent":[960,640]}}}}

    def test_explicit_absence_is_valid_but_missing_evidence_is_not(self):
        self.assertEqual(validate(self.census,self.samples,False),[])
        self.assertTrue(validate({},self.samples,False))

    def test_unresolved_loaded_object_and_unpaused_census_fail(self):
        self.census["unresolvedScenery"] = 1
        self.assertTrue(validate(self.census,self.samples,False))
        self.census["unresolvedScenery"] = 0
        self.census["paused"] = False
        self.assertTrue(validate(self.census,self.samples,False))

    def test_motion_uses_initial_tick_not_later_simulated_tick(self):
        self.samples["frame"]["metadata"].update(simulationTicks=139,worldMotion={"initialTick":123})
        self.assertEqual(validate(self.census,self.samples,True),[])
        self.samples["frame"]["metadata"]["worldMotion"]["initialTick"] = 124
        self.assertTrue(validate(self.census,self.samples,True))

    def test_census_must_not_advance_ticks_or_invent_matches(self):
        self.census["simulationTicksAfter"] = 124
        self.assertTrue(validate(self.census,self.samples,False))
        self.census["simulationTicksAfter"] = 123
        self.census["matchedTrees"] = 1
        self.assertTrue(validate(self.census,self.samples,False))


if __name__ == "__main__":
    unittest.main()
