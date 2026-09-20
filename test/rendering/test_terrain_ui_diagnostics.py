"""Gate regressions; no game/GPU/compiler invocation. Run manually after source review."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ui_parity_runner", ROOT / "scripts/rendering/run-ui-parity.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TerrainUiDiagnosticGateTests(unittest.TestCase):
    def setUp(self):
        self.tiles = [[16,0,0,0,0,1] for _ in range(1024)]
        catalog = [{"kind":k,"slot":0,"base":100*k,"count":1000,"selectors":[0]*144} for k in (1,2)]
        materials = [{k:v for k,v in m.items() if k != "slot"} | {"flags":0} for m in catalog]
        self.metadata = {"rotation":0,"zoom":0,"viewPosition":[0,0],"mainViewportBounds":[0,0,64,64],
            "balloonPublication":{"entityCount":0},"cpuViewportPaint":{"generate":0,"arrange":0,"draw":0},
            "nativeTerrainFixture":{"schema":1,"viewports":1,"atlasLease":1,
                "uploads":{"tiles":0,"materials":0,"sprites":0,"statusReadback":8,"viewports":1},
                "source":{"epoch":1,"bounded":True,"chunkRevisions":[1,2,3,4],"tiles":self.tiles,"materials":catalog},
                "scenes":[{"epoch":1,"materialRevision":1,"spriteRevision":1,"chunkRevisions":[1,2,3,4],
                    "camera":[0,0,64,64,0,0,0,0,0,0,1],"tiles":[[16,0,0,0,1,1] for _ in range(1024)],
                    "materials":materials,"sprites":[{"image":100}]}]}}

    def test_positive_contract_and_cpu_duplicate_rejection(self):
        self.assertEqual(runner.validate_native_terrain_sample(self.metadata,self.tiles,False),[])
        self.metadata["cpuViewportPaint"]["generate"] = 1
        self.assertIn("Native terrain did not bypass all CPU world column work",
            runner.validate_native_terrain_sample(self.metadata,self.tiles,False))

    def test_changed_camera_stale_revision_and_upload_reject(self):
        for mutate, expected in (
            (lambda n:n["scenes"][0]["camera"].__setitem__(0,1),"Terrain GPU clip/camera differs from the actual main viewport"),
            (lambda n:n["scenes"][0].__setitem__("epoch",2),"GPU terrain command has stale publication identity"),
            (lambda n:n["uploads"].__setitem__("tiles",8192),"Settled terrain frame uploaded state or viewport accounting differs")):
            value=copy.deepcopy(self.metadata);mutate(value["nativeTerrainFixture"])
            self.assertIn(expected,runner.validate_native_terrain_sample(value,self.tiles,False))

    def test_decline_requires_positive_cpu_calls_and_zero_admission(self):
        native=self.metadata["nativeTerrainFixture"];native["scenes"]=[];native["viewports"]=0;native["uploads"]["viewports"]=0
        self.assertIn("Terrain decline did not perform positive CPU world paint",
            runner.validate_native_terrain_sample(self.metadata,self.tiles,True))
        self.metadata["cpuViewportPaint"]={"generate":2,"arrange":2,"draw":2}
        self.assertEqual(runner.validate_native_terrain_sample(self.metadata,self.tiles,True),[])


    def preparation_sequence(self):
        values = [copy.deepcopy(self.metadata) for _ in range(3)]
        for i, value in enumerate(values):
            value["frameNumber"] = 10 + i * 3
            value["nativeTerrainFixture"]["preparation"] = {
                "schema":1, "frameNumber":value["frameNumber"], "epoch":1,
                "materialMapCopies":2, "spriteCatalogBuilds":1, "residencyRebinds":5 + i * 3}
        return values

    def test_preparation_retains_catalog_across_named_packets(self):
        self.assertEqual(runner.validate_terrain_preparation_sequence(self.preparation_sequence()), [])

    def test_preparation_rejects_rebuild_missing_and_nonretained_catalogs(self):
        for key, replacement in (("materialMapCopies",3), ("spriteCatalogBuilds",2),
                ("residencyRebinds",5), ("materialMapCopies",0), ("spriteCatalogBuilds",True)):
            with self.subTest(key=key, replacement=replacement):
                values=self.preparation_sequence()
                values[1]["nativeTerrainFixture"]["preparation"][key]=replacement
                self.assertTrue(runner.validate_terrain_preparation_sequence(values))
        values=self.preparation_sequence()
        del values[1]["nativeTerrainFixture"]["preparation"]
        self.assertTrue(runner.validate_terrain_preparation_sequence(values))
        self.assertTrue(runner.validate_terrain_preparation_sequence(values[:1]))

    def test_preparation_rejects_future_stale_and_changed_publications(self):
        for mutate in (
                lambda v:v["nativeTerrainFixture"]["preparation"].__setitem__("frameNumber",99),
                lambda v:v.__setitem__("frameNumber",10),
                lambda v:v["nativeTerrainFixture"]["preparation"].__setitem__("epoch",2),
                lambda v:v["nativeTerrainFixture"]["source"]["chunkRevisions"].__setitem__(0,8)):
            values=self.preparation_sequence(); mutate(values[1])
            self.assertTrue(runner.validate_terrain_preparation_sequence(values))

if __name__ == "__main__":
    unittest.main()
