from pathlib import Path
import os
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from package_stage import archive_component, inventory, duplicate_payloads
from dependency_support import sha256


class NoCopyCommand:
    def run(self, *args, **kwargs):
        raise AssertionError('stage archiving must not invoke CPack or copy the SDK')


class StageArchiveTests(unittest.TestCase):
    def setUp(self):
        directory = ROOT / 'build/test-tools'
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage, self.packing, self.output = [self.root / name for name in ('sdk', 'packing', 'output')]
        for directory in (self.stage, self.packing, self.output):
            directory.mkdir()
        (self.stage / 'bin').mkdir()
        (self.stage / 'empty').mkdir()
        (self.stage / 'bin/WebTest').write_bytes(b'payload\x00\nwith literal \\n bytes')
        (self.stage / 'bin/WebTest').chmod(0o755)
        (self.stage / 'name with space.txt').write_text('sample')

    def pack(self, windows):
        report = archive_component(self.stage, 'preview', self.packing, self.output, NoCopyCommand(), windows)
        archive = ROOT / 'build' / report['path']
        self.assertEqual(sha256(archive), report['sha256'])
        self.assertEqual(list(self.packing.iterdir()), [])
        self.assertTrue(archive.with_name(archive.name + '.sha256').is_file())
        return archive

    def test_zip_streams_and_preserves_bytes_and_empty_directories(self):
        archive = self.pack(True)
        with zipfile.ZipFile(archive) as reader:
            self.assertIn('preview/empty/', reader.namelist())
            self.assertEqual(reader.read('preview/bin/WebTest'), (self.stage / 'bin/WebTest').read_bytes())
            self.assertIsNone(reader.testzip())

    def test_tar_streams_and_preserves_executable(self):
        archive = self.pack(False)
        with tarfile.open(archive) as reader:
            entry = reader.getmember('preview/bin/WebTest')
            self.assertEqual(entry.mode & 0o777, (self.stage / 'bin/WebTest').stat().st_mode & 0o777)
            if os.name != 'nt':
                self.assertTrue(entry.mode & 0o111)
            self.assertEqual(reader.extractfile(entry).read(), (self.stage / 'bin/WebTest').read_bytes())
            self.assertTrue(reader.getmember('preview/empty').isdir())

    def symlink(self, target):
        try:
            os.symlink(target, self.stage / 'link')
        except OSError as error:
            self.skipTest('symlink creation unavailable: ' + str(error))

    def test_tar_retains_relative_symlink(self):
        self.symlink('bin/WebTest')
        with tarfile.open(self.pack(False)) as reader:
            entry = reader.getmember('preview/link')
            self.assertTrue(entry.issym())
            self.assertEqual(entry.linkname, 'bin/WebTest')

    def test_symlink_escape_rejected_before_archive_creation(self):
        self.symlink('../outside')
        with self.assertRaises(ValueError):
            self.pack(False)
        self.assertEqual(list(self.output.iterdir()), [])

    def test_physical_duplicate_library_is_rejected(self):
        (self.stage / 'bin/a.dll').write_bytes(b'library bytes')
        (self.stage / 'bin/b.dll').write_bytes(b'library bytes')
        with self.assertRaisesRegex(RuntimeError, 'duplicate physical library'):
            duplicate_payloads(self.stage, inventory(self.stage))

    def test_tar_library_alias_is_not_duplicate_payload(self):
        (self.stage / 'bin/a.so.1').write_bytes(b'library bytes')
        try:
            os.symlink('a.so.1', self.stage / 'bin/a.so')
        except OSError as error:
            self.skipTest(str(error))
        self.assertEqual(duplicate_payloads(self.stage, inventory(self.stage))['redundant_bytes'], 0)


if __name__ == '__main__':
    unittest.main()
