from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from graphics_deps import discard_intermediates, reusable_cmake_work
from ci_support import write_json


class GraphicsPrefixTests(unittest.TestCase):
    def test_only_redundant_archives_and_symbol_maps_are_discarded(self):
        root = ROOT / 'build/test-tools'
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            prefix = Path(directory)
            (prefix / 'lib/raw').mkdir(parents=True)
            (prefix / 'include').mkdir()
            files = {'lib/final.lib': b'isolated closure', 'lib/raw/input.lib': b'duplicate',
                     'lib/final.lib.symbols': b'mapping', 'lib/final.lib.merge.json': b'{}',
                     'include/api.h': b'public declarations'}
            for name, data in files.items():
                (prefix / name).write_bytes(data)
            self.assertEqual(discard_intermediates(prefix, ['lib/final.lib']), 16)
            for name in ('lib/final.lib', 'lib/final.lib.merge.json', 'include/api.h'):
                self.assertEqual((prefix / name).read_bytes(), files[name])
            self.assertFalse((prefix / 'lib/raw/input.lib').exists())
            self.assertEqual(discard_intermediates(prefix, ['lib/final.lib']), 0)

    def test_missing_final_archive_cannot_trigger_cleanup(self):
        root = ROOT / 'build/test-tools'
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            prefix = Path(directory)
            (prefix / 'lib').mkdir()
            (prefix / 'lib/input.lib').write_bytes(b'only available implementation')
            with self.assertRaises(RuntimeError):
                discard_intermediates(prefix, ['lib/final.lib'])
            self.assertTrue((prefix / 'lib/input.lib').is_file())


class GraphicsWorkTests(unittest.TestCase):
    def test_failed_minimum_version_configuration_reuses_the_source_copy(self):
        root = ROOT / 'build/test-tools'
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as directory:
            base = Path(directory)
            work = base / 'old-key/Debug'
            write_json(work / 'source/.source.json', {'sha256': 'locked'})
            (work / 'out').mkdir()
            (work / 'out/CMakeCache.txt').write_text('CMAKE_CACHE_MAJOR_VERSION:INTERNAL=3\n')
            self.assertEqual(reusable_cmake_work(base, 'Debug', 'locked', '/clang'), work)
            self.assertIsNone(reusable_cmake_work(base, 'Release', 'locked', '/clang'))
            self.assertIsNone(reusable_cmake_work(base, 'Debug', 'other-source', '/clang'))
            # Once configured, incompatible compiler work must not be adopted.
            (work / 'out/build.ninja').write_text('generated')
            self.assertIsNone(reusable_cmake_work(base, 'Debug', 'locked', '/clang'))
            (work / 'out/CMakeCache.txt').write_text('CMAKE_CXX_COMPILER:FILEPATH=/clang\n')
            self.assertEqual(reusable_cmake_work(base, 'Debug', 'locked', '/clang'), work)


if __name__ == '__main__':
    unittest.main()
