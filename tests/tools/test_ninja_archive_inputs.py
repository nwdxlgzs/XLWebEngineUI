"""Archive selection uses the real target graph, including old Ninja hosts."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from deps import ninja_archive_inputs


@unittest.skipUnless(shutil.which('ninja'), 'Ninja is unavailable')
class ArchiveGraphTests(unittest.TestCase):
    def test_real_graph_keeps_reachable_space_name_and_excludes_stale_and_external(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            work = Path(temporary) / 'out'; work.mkdir()
            (work / 'build.ninja').write_text(
                'rule archive\n  command = unused\n'
                'build dep$ space.a: archive input.cc\n'
                'build libskia.a: archive dep$ space.a ../external.a\n'
                'build skia: phony libskia.a\n'
                'build stale.a: archive old.cc\n', encoding='utf-8')
            for name in ('libskia.a', 'dep space.a', 'stale.a'):
                (work / name).write_bytes(b'archive fixture')
            original_run = subprocess.run
            def old_ninja(args, **kwargs):
                if '-t' in args and args[args.index('-t') + 1] == 'inputs':
                    return subprocess.CompletedProcess(args, 1, '', "ninja: fatal: unknown tool 'inputs'\n")
                return original_run(args, **kwargs)
            with mock.patch('deps.subprocess.run', side_effect=old_ninja):
                selected = ninja_archive_inputs(work, ['skia'], '.a', os.environ.copy())
            self.assertEqual(selected, {(work / 'libskia.a').resolve(), (work / 'dep space.a').resolve()})

    def test_missing_target_is_not_hidden_by_compatibility_fallback(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            work = Path(temporary)
            (work / 'build.ninja').write_text('build valid: phony\n', encoding='utf-8')
            with self.assertRaises(subprocess.CalledProcessError):
                ninja_archive_inputs(work, ['missing'], '.a', os.environ.copy())


if __name__ == '__main__':
    unittest.main()
