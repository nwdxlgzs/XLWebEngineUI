import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from ci import refresh_work_references


class FocusedReferences(unittest.TestCase):
    def test_reused_tree_protects_current_inputs_and_retires_old_dependencies(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            work = Path(temporary)
            manifest = work / 'inputs.cmake'
            manifest.with_suffix('.json').write_text(json.dumps({'references': [
                'deps/install/lexbor/new/Debug', 'generated/resources', 'generated/resources']}), encoding='utf-8')
            (work / 'CMakeCache.txt').write_text('XL_SDK_INPUT_MANIFEST:FILEPATH=' + manifest.as_posix() + '\n', encoding='utf-8')
            info = {'artifact_references': ['toolchains/clang', 'deps/install/lexbor/old/Debug']}
            refresh_work_references(work, info)
            self.assertEqual(info['artifact_references'], [
                'toolchains/clang', 'deps/install/lexbor/new/Debug', 'generated/resources'])

    def test_no_manifest_does_not_discard_existing_references(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            work = Path(temporary)
            info = {'artifact_references': ['toolchains/clang']}
            refresh_work_references(work, info)
            (work / 'CMakeCache.txt').write_text('OTHER:FILEPATH=ignored\n', encoding='utf-8')
            refresh_work_references(work, info)
            self.assertEqual(info['artifact_references'], ['toolchains/clang'])


if __name__ == '__main__':
    unittest.main()
