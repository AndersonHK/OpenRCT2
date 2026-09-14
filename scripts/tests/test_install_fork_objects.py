"""Non-destructive migration checks for existing packed object installations."""
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

spec = importlib.util.spec_from_file_location("installer", Path(__file__).parents[1] / "install-fork-objects.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class ObjectsInstallTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.source = root / "source"
        self.destination = root / "install"
        self.destination.mkdir()
        self.name = "objects/rct2/ride/kart.json"
        path = self.source / self.name
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"id": "rct2.ride.kart1", "properties": {"frictionSoundGainDb": -6}}))

    def check(self):
        return installer.check_destination(self.source, self.destination, [self.name])

    def test_old_upstream_pack_is_rejected_without_modification(self):
        packed = self.destination / "old-kart.parkobj"
        with zipfile.ZipFile(packed, "w") as archive:
            archive.writestr("object.json", json.dumps({"id": "rct2.ride.kart1", "properties": {}}))
        before = packed.read_bytes()
        with self.assertRaisesRegex(ValueError, "Competing object rct2.ride.kart1"):
            self.check()
        self.assertEqual(before, packed.read_bytes())
        self.assertEqual([packed], list(self.destination.iterdir()))

    def test_unrelated_music_pack_is_preserved(self):
        packed = self.destination / "music.parkobj"
        with zipfile.ZipFile(packed, "w") as archive:
            archive.writestr("object.json", json.dumps({"id": "openrct2.music.fairground2"}))
        before = packed.read_bytes()
        self.assertEqual(["rct2/ride/kart.json"], self.check())
        self.assertEqual(before, packed.read_bytes())

    def test_stale_previously_managed_asset_requires_review(self):
        stale = self.destination / "removed-object.json"
        stale.write_text(json.dumps({"id": "rct2.ride.removed"}))
        marker = self.destination / installer.PROVENANCE_FILE
        marker.write_text(json.dumps({"revision": "old", "files": [stale.name]}))
        with self.assertRaisesRegex(ValueError, "Stale previously installed fork asset"):
            self.check()
        self.assertTrue(stale.exists())

    def test_existing_definition_at_expected_path_can_be_updated(self):
        path = self.destination / "rct2/ride/kart.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"id": "rct2.ride.kart1", "properties": {}}))
        self.assertEqual(["rct2/ride/kart.json"], self.check())


if __name__ == "__main__":
    unittest.main()
