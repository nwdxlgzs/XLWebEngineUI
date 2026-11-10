import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import artifacts
from ci_support import read_json, write_json


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / "build/test-tools"
        directory.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=str(directory))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def artifact(self, index, content_key=None, references=(), family="test"):
        path = self.root / "cache" / str(index)
        path.mkdir(parents=True)
        (path / "sentinel").write_text("disposable", encoding="utf-8")
        artifacts.register(self.root, path, family, content_key, references)
        _, _, metadata, _ = artifacts.locations(self.root, path)
        info = read_json(metadata)
        info["last_used"] = index
        write_json(metadata, info)
        return path

    def test_retention_is_per_family_and_dry_run_does_not_delete(self):
        for index in range(9):
            self.artifact(index, family="a" if index < 7 else "b")
        self.assertEqual(set(artifacts.collect(self.root, 5, dry_run=True)), {"cache/0", "cache/1"})
        self.assertTrue((self.root / "cache/0/sentinel").exists())
        artifacts.collect(self.root, 5)
        self.assertEqual(len(list((self.root / "cache").iterdir())), 7)

    def test_current_lock_pin_and_transitive_dependency_are_kept(self):
        self.artifact(0)
        self.artifact(1, "current-hash", ["cache/0"])
        for index in range(2, 9):
            self.artifact(index)
        artifacts.collect(self.root, 5, pinned={"current-hash"})
        self.assertTrue((self.root / "cache/0").exists())
        self.assertTrue((self.root / "cache/1").exists())
        self.assertFalse((self.root / "cache/2").exists())

    def test_retained_work_reference_keeps_a_dependency(self):
        self.artifact(0)
        for index in range(1, 7):
            self.artifact(index)
        write_json(self.root / "work/test/key/Debug/.ci-work.json", {"artifact_references": ["cache/0"]})
        artifacts.collect(self.root, 5)
        self.assertTrue((self.root / "cache/0").exists())

    def test_inactive_retained_catalog_keeps_old_validation_and_transitive_sources(self):
        source = self.artifact(0, family="sources")
        compiled = self.artifact(1, references=["cache/0"], family="compiled")
        validation = self.artifact(20, references=["cache/1"], family="validation")
        catalog = self.artifact(21, references=["cache/20"], family="catalog")
        for index in range(2, 9):
            self.artifact(index, family="compiled")
        for index in range(9, 16):
            self.artifact(index, family="sources")
        for index in range(22, 28):
            self.artifact(index, family="validation")
        planned = set(artifacts.collect(self.root, 5, dry_run=True))
        self.assertFalse({"cache/0", "cache/1", "cache/20", "cache/21"} & planned)
        self.assertIn("cache/2", planned)
        artifacts.collect(self.root, 5)
        self.assertTrue(all(path.exists() for path in (source, compiled, validation, catalog)))
        for index in range(28, 34):
            self.artifact(index, family="catalog")
        artifacts.collect(self.root, 5)
        self.assertFalse(any(path.exists() for path in (source, compiled, validation, catalog)))

    def test_active_lease_survives_gc_and_released_resource_can_be_collected(self):
        old = self.artifact(0)
        with artifacts.lease(self.root, old):
            for index in range(1, 8):
                self.artifact(index)
            _, _, metadata, _ = artifacts.locations(self.root, old)
            info = read_json(metadata)
            info["last_used"] = 0
            write_json(metadata, info)
            artifacts.collect(self.root, 5)
            self.assertTrue(old.exists())
        artifacts.collect(self.root, 5)
        self.assertFalse(old.exists())

    def test_missing_registration_cannot_be_leased_or_deleted(self):
        path = self.root / "cache/unowned"
        path.mkdir(parents=True)
        with self.assertRaises(ValueError):
            with artifacts.lease(self.root, path):
                self.fail("unregistered artifact was leased")
        artifacts.collect(self.root, 1)
        self.assertTrue(path.exists())

    def test_registry_cannot_authorize_root_or_outside_deletion(self):
        for relative in (".", "../outside"):
            write_json(self.root / "state/artifacts/bogus.json", {"path": relative})
            with self.assertRaises(ValueError):
                artifacts.collect(self.root)


if __name__ == "__main__":
    unittest.main()
