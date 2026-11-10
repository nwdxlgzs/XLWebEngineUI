"""Reconstruction of the actual locked Dawn compatibility changes."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from dependency_patches import apply_patch_file


class DawnRebuildTests(unittest.TestCase):
    def replay(self, source, filename):
        if not source.is_dir():
            self.skipTest('locked dependency source unavailable')
        patch = json.loads((ROOT / filename).read_text(encoding='utf-8'))
        files = [record['file'] for record in patch.get('files', [patch])]
        before = {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in files}
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            overlay = Path(temporary)
            outputs = apply_patch_file(source, overlay, filename)
            expected = {p.relative_to(overlay): p.read_bytes() for p in outputs}
            for p in outputs:
                p.unlink()
            restored = apply_patch_file(source, overlay, filename)
            self.assertEqual({p.relative_to(overlay): p.read_bytes() for p in restored}, expected)
        self.assertEqual({name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in files}, before)

    def test_private_graphics_patch_rebuilds_after_overlay_deletion(self):
        spec = json.loads((ROOT / 'third_party/dawn-sources.lock.json').read_text())['dependencies']['dawn']
        self.replay(ROOT / 'build/deps/src/dawn' / spec['sha256'][:12],
                    'third_party/patches/dawn-private-static-graphics.json')

    def test_skia_prebuilt_adapter_rebuilds_after_overlay_deletion(self):
        spec = json.loads((ROOT / 'third_party/dependencies.lock.json').read_text())['dependencies']['skia']
        self.replay(ROOT / 'build/deps/src/skia' / spec['sha256'][:12],
                    'third_party/patches/skia-dawn-prebuilt.json')


if __name__ == '__main__':
    unittest.main()
