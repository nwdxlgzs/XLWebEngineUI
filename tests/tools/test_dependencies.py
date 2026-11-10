import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import deps
import platform_deps
from ci_support import write_json


class CompiledSelectionTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/test-tools"
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=str(parent))
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        for name, value in (("ROOT", self.root), ("BUILD", self.build)):
            replacement = patch.object(deps, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        self.specs = {"leaf": {"sha256": "a" * 64}, "parent": {"sha256": "b" * 64}}
        self.recipes = {"leaf": {"depends": [], "options": {}},
                        "parent": {"depends": ["leaf"], "options": {}}}
        self.save_locks()
        self.leaf = self.install("leaf")
        self.parent = self.install("parent", [self.leaf])

    def save_locks(self):
        write_json(self.root / "third_party/dependencies.lock.json", {"dependencies": self.specs})
        write_json(self.root / "third_party/recipes/cmake.json", {"components": self.recipes})

    def install(self, name, dependencies=()):
        identity = deps.cmake_build_identity(self.specs[name], self.recipes[name], "compiler-a", "Release", dependencies)
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
        prefix = self.build / "deps/install/test-target" / name / key / "Release"
        (prefix / "lib").mkdir(parents=True, exist_ok=True)
        (prefix / "lib/library.a").write_bytes(b"archive fixture")
        stamp = {"key": key, "prefix": prefix.relative_to(self.build).as_posix(),
                 "source_sha256": self.specs[name]["sha256"], "state": "compiled-and-installed",
                 "libraries": ["lib/library.a"]}
        write_json(prefix / ".build.json", stamp)
        return stamp

    def select(self):
        return deps.select_compiled_components("test-target", "Release", "compiler-a")

    def test_exact_current_closure_is_selected(self):
        self.assertEqual(self.select(), {"leaf": self.leaf, "parent": self.parent})

    def test_changed_recipe_rejects_old_success(self):
        self.recipes["leaf"]["options"]["FEATURE"] = True
        self.save_locks()
        with self.assertRaisesRegex(RuntimeError, "needs compiled leaf"):
            self.select()

    def test_rebuilt_dependency_invalidates_parent(self):
        self.recipes["leaf"]["options"]["FEATURE"] = True
        self.save_locks()
        self.install("leaf")
        with self.assertRaisesRegex(RuntimeError, "needs compiled parent"):
            self.select()

    def test_missing_archive_rejects_success_stamp(self):
        (self.build / self.leaf["prefix"] / "lib/library.a").unlink()
        with self.assertRaisesRegex(RuntimeError, "needs compiled leaf"):
            self.select()

    def test_other_compiler_cannot_reuse_success(self):
        with self.assertRaisesRegex(RuntimeError, "needs compiled leaf"):
            deps.select_compiled_components("test-target", "Release", "compiler-b")

    def test_changed_source_rejects_old_success(self):
        self.specs["leaf"]["sha256"] = "c" * 64
        self.save_locks()
        with self.assertRaisesRegex(RuntimeError, "needs compiled leaf"):
            self.select()

    def test_changed_custom_recipe_file_rejects_old_success(self):
        self.recipes["leaf"]["recipe_directory"] = "third_party/recipes/leaf"
        self.save_locks()
        source = self.root / "third_party/recipes/leaf/CMakeLists.txt"
        source.parent.mkdir(parents=True)
        source.write_text("initial recipe", encoding="utf-8")
        self.install("leaf")
        source.write_text("updated recipe", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "needs compiled leaf"):
            self.select()

    def test_library_path_cannot_escape_install_prefix(self):
        self.leaf["libraries"] = ["../foreign.a"]
        write_json(self.build / self.leaf["prefix"] / ".build.json", self.leaf)
        with self.assertRaises(ValueError):
            self.select()

    def make_library_alias(self, target):
        prefix = self.build / self.leaf["prefix"]
        alias = prefix / "lib/library-alias.a"
        try:
            alias.symlink_to(target)
        except OSError as error:
            self.skipTest("symlink permission unavailable: " + str(error))
        self.leaf["libraries"].append("lib/library-alias.a")
        write_json(prefix / ".build.json", self.leaf)

    def test_static_archive_alias_inside_install_is_accepted(self):
        self.make_library_alias("library.a")
        self.assertEqual(self.select()["leaf"], self.leaf)

    def test_static_archive_alias_outside_install_is_refused(self):
        foreign = self.root / "foreign.a"
        foreign.write_bytes(b"outside the installation")
        self.make_library_alias(foreign)
        with self.assertRaises(ValueError):
            self.select()
        self.assertTrue(foreign.is_file())


class PlatformStackSelectionTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/test-tools"
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=str(parent))
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        for module in (deps, platform_deps):
            for name, value in (("ROOT", self.root), ("BUILD", self.build)):
                replacement = patch.object(module, name, value)
                replacement.start()
                self.addCleanup(replacement.stop)
        write_json(self.root / "third_party/platform-sources.lock.json", {"dependencies": {}})
        write_json(self.root / "third_party/recipes/x11.json", {"components": {}})
        write_json(self.root / "third_party/platform-extensions.lock.json", {"dependencies": {"libXtst": {"sha256": "a" * 64}}})
        write_json(self.root / "third_party/recipes/x11-extensions.json", {"components": {}})
        identity = {"sources": {}, "recipes": {}, "compiler_key": "compiler", "configuration": "Release", "recipe_driver_revision": 1}
        self.base_key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
        self.base = self.build / "base"
        (self.base / "include/X11").mkdir(parents=True)
        (self.base / "include/X11/Xlib.h").write_bytes(b"header")
        (self.base / "lib").mkdir()
        for name in ("X11", "xcb", "Xau", "Xdmcp", "Xext", "Xrender", "Xfixes", "Xcursor", "Xrandr", "Xi", "Xss"):
            (self.base / "lib" / ("lib" + name + ".a")).write_bytes(b"archive")
        write_json(self.build / "reports/platform-x11/Release.json", {"state": "passed", "key": self.base_key, "prefix": "base"})
        extra_key = platform_deps.extension_key(self.base_key, "compiler", "Release")
        self.extra = self.build / "extra"
        (self.extra / "lib").mkdir(parents=True)
        (self.extra / "lib/libXtst.a").write_bytes(b"extension")
        write_json(self.build / "reports/platform-x11-extensions/Release.json", {"state": "passed", "key": extra_key, "prefix": "extra"})

    def test_complete_private_stack_includes_both_prefixes(self):
        result = deps.platform_stack("Release", "compiler")
        self.assertEqual(result["prefixes"], ["extra", "base"])
        self.assertEqual(result["base_key"], self.base_key)

    def test_base_only_success_cannot_claim_xtest_available(self):
        (self.build / "reports/platform-x11-extensions/Release.json").unlink()
        with self.assertRaisesRegex(RuntimeError, "current X11 extensions"):
            deps.platform_stack("Release", "compiler")

    def test_missing_base_archive_is_detected(self):
        (self.base / "lib/libXi.a").unlink()
        with self.assertRaisesRegex(RuntimeError, "current native X11 stack"):
            deps.platform_stack("Release", "compiler")

    def test_changed_extension_source_cannot_reuse_success_report(self):
        write_json(self.root / "third_party/platform-extensions.lock.json", {"dependencies": {"libXtst": {"sha256": "b" * 64}}})
        with self.assertRaisesRegex(RuntimeError, "current X11 extensions"):
            deps.platform_stack("Release", "compiler")


if __name__ == "__main__":
    unittest.main()
