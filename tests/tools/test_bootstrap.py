import sys
import tarfile
import os
import tempfile
import unittest
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import dependency_support
import bootstrap
from ci_support import write_json


class WrapperTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "build/test-tools"
        parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=str(parent))
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def test_reusing_a_wrapper_never_opens_it_for_writing(self):
        path = self.root / "clang"
        script = '#!/bin/sh\nexec compiler "$@"\n'
        bootstrap.install_wrapper(path, script)
        before = path.stat()
        with mock.patch.object(Path, "write_bytes", side_effect=AssertionError("in-use wrapper rewritten")), \
                mock.patch.object(bootstrap.os, "replace", side_effect=AssertionError("unchanged wrapper replaced")):
            bootstrap.install_wrapper(path, script)
        self.assertEqual(path.stat().st_mtime_ns, before.st_mtime_ns)
        self.assertEqual(path.read_bytes(), script.encode())

    @unittest.skipIf(os.name == "nt", "POSIX permits replacing an open executable")
    def test_replacement_preserves_the_script_already_open_by_a_reader(self):
        path = self.root / "clang"
        bootstrap.install_wrapper(path, "old complete script\n")
        with path.open("rb") as executing:
            bootstrap.install_wrapper(path, "new complete script\n")
            self.assertEqual(executing.read(), b"old complete script\n")
        self.assertEqual(path.read_bytes(), b"new complete script\n")
        self.assertTrue(os.access(str(path), os.X_OK))

    def test_failed_publication_preserves_existing_wrapper_and_cleans_temporary(self):
        path = self.root / "clang"
        bootstrap.install_wrapper(path, "working script\n")
        with mock.patch.object(bootstrap.os, "replace", side_effect=OSError("publication failed")):
            with self.assertRaises(OSError):
                bootstrap.install_wrapper(path, "replacement script\n")
        self.assertEqual(path.read_bytes(), b"working script\n")
        self.assertEqual(list(self.root.iterdir()), [path])


class MuslPreparationTests(unittest.TestCase):
    @unittest.skipIf(os.name == 'nt', 'musl preparation runs in the Linux interpreter')
    def test_nested_preparation_reuses_but_a_missing_runtime_triggers_recovery(self):
        parent = ROOT / 'build/test-tools'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temporary:
            root = Path(temporary)
            runtime = root / 'runtime'
            binary = root / 'llvm-bin'
            def provision():
                for relative in bootstrap.MUSL_RUNTIME_ENTRIES + ('licenses/musl.txt',):
                    path = runtime / relative; path.parent.mkdir(parents=True, exist_ok=True); path.touch()
                for name in ('cc', 'cxx', 'ar', 'ranlib'):
                    path = root / 'toolchains/wrappers/linux-musl' / name
                    path.parent.mkdir(parents=True, exist_ok=True); path.touch()
                binary.mkdir(exist_ok=True)
                for name in ('clang', 'clang++', 'ld.lld', 'llvm-ar', 'llvm-ranlib'):
                    (binary / name).touch()
                write_json(root / 'toolchains/wrappers/linux-musl/toolchain.json', {'runtime': str(runtime)})
            with mock.patch.object(bootstrap, 'BUILD', root), mock.patch.object(bootstrap, '_prepared_musl', None), \
                    mock.patch.object(bootstrap, 'llvm_bin', return_value=binary), \
                    mock.patch.object(bootstrap, '_prepare_musl', side_effect=provision) as prepare:
                bootstrap.prepare_musl(); bootstrap.prepare_musl()
                self.assertEqual(prepare.call_count, 1)
                (runtime / 'lib/libc++.a').unlink()
                bootstrap.prepare_musl()
                self.assertEqual(prepare.call_count, 2)
                self.assertTrue((runtime / 'lib/libc++.a').is_file())


class ArchiveTests(unittest.TestCase):
    def member(self, name, kind=tarfile.REGTYPE, target=""):
        item = tarfile.TarInfo(name)
        item.type = kind
        item.linkname = target
        return item

    def test_archive_paths_cannot_escape_or_replace_external_files(self):
        for name in ("../escape", "/absolute", "another-package/tool", "locked-source/../escape", "locked-source/C:/escape", "locked-source\\escape"):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                dependency_support.validate_members([self.member(name)], "locked-source")

    def test_links_cannot_escape_the_toolchain(self):
        root = "locked-source"
        for kind, target in ((tarfile.SYMTYPE, "../../escape"), (tarfile.SYMTYPE, "/absolute"),
                             (tarfile.LNKTYPE, "other-package/file")):
            with self.subTest(target=target), self.assertRaises(RuntimeError):
                dependency_support.validate_members([self.member(root + "/link", kind, target)], root)

    def test_device_entries_are_rejected(self):
        with self.assertRaises(RuntimeError):
            dependency_support.validate_members([self.member("locked-source/device", tarfile.CHRTYPE)], "locked-source")

    def test_internal_relative_symlink_is_allowed(self):
        root = "locked-source"
        dependency_support.validate_members([self.member(root + "/lib/sub/link", tarfile.SYMTYPE, "../file")], root)

    def test_partial_bytes_require_the_exact_server_range(self):
        self.assertTrue(dependency_support.range_request_valid("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 4-9/12\r\n", 4, 9, 12, 3))
        self.assertFalse(dependency_support.range_request_valid("HTTP/1.1 200 OK\r\n", 4, 9, 12, 6))
        self.assertFalse(dependency_support.range_request_valid("Content-Range: bytes 0-9/12\n", 4, 9, 12, 6))
        self.assertFalse(dependency_support.range_request_valid("Content-Range: bytes 4-9/20\n", 4, 9, 12, 6))
        self.assertFalse(dependency_support.range_request_valid("Content-Range: bytes 4-9/12\n", 4, 9, 12, 7))


if __name__ == "__main__":
    unittest.main()
