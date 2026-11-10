import json
import os
from pathlib import Path
import platform
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from ci_support import (WorkLock, checked_child, cleanup_runs, cleanup_work, host_identity, process_alive,
                        read_json, select_impact, work_lock_path, write_json)


class Fixture(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/test-tools"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=str(parent))
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)


class RetentionTests(Fixture):
    def make_run(self, name, finished, active=False):
        path = self.root / "runs/quick--test" / name
        write_json(path / "report.json", {
            "state": "running" if active else "passed", "finished_at": finished,
            "pid": os.getpid(), "host": host_identity()})
        return path

    def test_keep_five_finished_and_protect_live_run(self):
        for index in range(8):
            self.make_run(str(index), index)
        active = self.make_run("active", -1, True)
        preview = cleanup_runs(self.root, 5, dry_run=True)
        self.assertEqual({Path(path).name for path in preview}, {"0", "1", "2"})
        self.assertTrue((self.root / "runs/quick--test/0").exists())
        cleanup_runs(self.root, 5)
        self.assertTrue(active.exists())
        self.assertEqual(len(list((self.root / "runs/quick--test").iterdir())), 6)

    def test_foreign_active_job_is_protected(self):
        path = self.make_run("foreign", 0)
        write_json(path / "report.json", {"state": "running", "pid": 1, "host": "another-host"})
        for index in range(7):
            self.make_run(str(index), index)
        cleanup_runs(self.root, 5)
        self.assertTrue(path.exists())

    def test_each_bucket_has_its_own_retention(self):
        for bucket in ("debug", "release"):
            for index in range(7):
                write_json(self.root / "runs" / bucket / str(index) / "report.json",
                           {"state": "failed", "finished_at": index})
        cleanup_runs(self.root, 5)
        for bucket in ("debug", "release"):
            self.assertEqual(len(list((self.root / "runs" / bucket).iterdir())), 5)

    def test_root_escape_and_parent_traversal_are_refused(self):
        for target in (self.root, self.root.parent / "outside", self.root / "child/../escape"):
            with self.assertRaises(ValueError):
                checked_child(self.root, target)

    def test_symlink_is_not_followed(self):
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "sentinel").write_text("keep", encoding="utf-8")
        link = self.root / "link"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest("symlink permission unavailable: " + str(error))
        with self.assertRaises(ValueError):
            checked_child(self.root, link / "sentinel")
        self.assertEqual((outside / "sentinel").read_text(encoding="utf-8"), "keep")

    def test_invalid_retention_cannot_delete_everything(self):
        with self.assertRaises(ValueError):
            cleanup_runs(self.root, 0)


class LockTests(Fixture):
    def test_live_lock_cannot_be_taken(self):
        path = self.root / "active.lock"
        with WorkLock(path):
            with self.assertRaises(RuntimeError):
                with WorkLock(path):
                    self.fail("concurrent owner entered")
        with WorkLock(path):
            self.assertTrue(path.exists())

    def test_dead_local_owner_can_be_recovered(self):
        path = self.root / "active.lock"
        write_json(path, {"state": "running", "host": host_identity(), "pid": 2147483647})
        with WorkLock(path) as lock:
            lock.stream.seek(0)
            self.assertEqual(json.loads(lock.stream.read())["pid"], os.getpid())
        with WorkLock(path) as lock:
            lock.stream.seek(0)
            self.assertEqual(json.loads(lock.stream.read())["pid"], os.getpid())

    def test_process_probe_does_not_terminate_the_owner(self):
        self.assertTrue(process_alive(os.getpid()))
        self.assertTrue(process_alive(os.getpid()))
        self.assertFalse(process_alive(-1))

    def test_work_cleanup_keeps_five_and_skips_locked_candidate(self):
        entries = []
        for index in range(8):
            work = self.root / "work/test-target" / str(index) / "Debug"
            write_json(work / ".ci-work.json", {"target": "test-target", "configuration": "Debug",
                "state": "passed", "host": host_identity(), "last_used": index})
            entries.append(work)
        with WorkLock(work_lock_path(self.root, entries[0])):
            removed = cleanup_work(self.root, 5)
        self.assertEqual({Path(path).parent.name for path in removed}, {"1", "2"})
        self.assertTrue(entries[0].exists())
        cleanup_work(self.root, 5)
        self.assertEqual(sum(path.exists() for path in entries), 5)


class ImpactTests(unittest.TestCase):
    def setUp(self):
        self.settings = json.loads((ROOT / "ci/settings.json").read_text(encoding="utf-8"))

    def test_runtime_change_includes_transitive_sdk_consumers(self):
        result = select_impact({"src/runtime/a.cpp": "new"}, {"src/runtime/a.cpp": "old"}, self.settings)
        self.assertEqual(result["scope"], "modules")
        self.assertIn("sdk", result["modules"])

    def test_deleted_file_is_not_ignored(self):
        result = select_impact({}, {"src/runtime/deleted.cpp": "old"}, self.settings)
        self.assertEqual(result["scope"], "modules")
        self.assertIn("src/runtime/deleted.cpp", result["changed"])

    def test_unknown_and_public_changes_widen_validation(self):
        for path in ("unknown.file", "include/XLWebEngineUI/xl.h", "tools/ci.py"):
            result = select_impact({path: "new"}, {}, self.settings)
            self.assertEqual(result["scope"], "global")

    def test_docs_only_does_not_require_a_compiler(self):
        for path in ("PLAN.MD", "third_party/README.md", "tools/README.md"):
            result = select_impact({path: "new"}, {path: "old"}, self.settings)
            self.assertEqual(result["scope"], "docs")

    def test_text_service_and_public_resource_tests_select_resources(self):
        for path in ("src/text/icu_session.cpp", "tests/runtime_resource_tests.cpp"):
            result = select_impact({path: "new"}, {path: "old"}, self.settings)
            self.assertEqual(result["scope"], "modules")
            self.assertIn("resources", result["modules"])
            self.assertIn("sdk", result["modules"])

    def test_native_platform_and_drawing_changes_reach_view_and_sdk_checks(self):
        for path in ("src/platform/desktop.cpp", "src/graphics/raster_view.cpp", "tests/views/native_tests.cpp"):
            result = select_impact({path: "new"}, {path: "old"}, self.settings)
            self.assertEqual(result["scope"], "modules")
            self.assertTrue({"graphics", "runtime", "sdk"}.issubset(result["modules"]))

    def test_web_pipeline_changes_include_script_semantics_and_sdk(self):
        for path in ("src/document/document.cpp", "src/style/computed.cpp", "src/script/dom_bindings.cpp"):
            result = select_impact({path: "new"}, {path: "old"}, self.settings)
            self.assertEqual(result["scope"], "modules")
            self.assertTrue({"document", "layout", "accessibility", "sdk"}.issubset(result["modules"]))

    def test_missing_baseline_runs_full_current_target(self):
        self.assertEqual(select_impact({}, None, self.settings)["scope"], "global")


if __name__ == "__main__":
    unittest.main()
