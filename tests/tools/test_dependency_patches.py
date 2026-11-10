from pathlib import Path
import hashlib
import json
import sys
import tempfile
import io
import tarfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import dependency_patches as patches
import deps
import dependency_support as support


class PatchRebuildTests(unittest.TestCase):
    def test_lexbor_empty_inline_patch_replays_without_changing_source(self):
        locked = json.loads((ROOT / 'third_party/dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['lexbor']
        source = ROOT / 'build/deps/src/lexbor' / locked['sha256'][:12]
        original = source / 'source/lexbor/style/html/interfaces/element.c'
        if not original.is_file():
            self.skipTest('locked Lexbor source unavailable')
        digest = hashlib.sha256(original.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            filename = 'third_party/patches/lexbor-empty-inline-style.json'
            generated = patches.apply_patch_file(source, Path(temporary), filename)[0]
            expected = generated.read_bytes()
            self.assertIn(b'if (element->style != NULL)', expected)
            generated.unlink()
            self.assertEqual(patches.apply_patch_file(source, Path(temporary), filename)[0].read_bytes(), expected)
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), digest)

    def test_lexbor_relational_retry_patch_replays_without_changing_source(self):
        locked = json.loads((ROOT / 'third_party/dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['lexbor']
        source = ROOT / 'build/deps/src/lexbor' / locked['sha256'][:12]
        original = source / 'source/lexbor/selectors/selectors.c'
        if not original.is_file():
            self.skipTest('locked Lexbor source unavailable')
        digest = hashlib.sha256(original.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            filename = 'third_party/patches/lexbor-selector-retry.json'
            generated = patches.apply_patch_file(source, Path(temporary), filename)[0]
            expected = generated.read_bytes()
            self.assertIn(b'lxb_selectors_retry_compound(selectors, current)', expected)
            self.assertIn(b'first = current->forward ? first->prev : first->next;', expected)
            generated.unlink()
            self.assertEqual(patches.apply_patch_file(source, Path(temporary), filename)[0].read_bytes(), expected)
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), digest)

    def test_lexbor_generated_syntax_patch_replays_after_overlay_deletion(self):
        locked = json.loads((ROOT / 'third_party/dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['lexbor']
        source = ROOT / 'build/deps/src/lexbor' / locked['sha256'][:12]
        original = source / 'source/lexbor/css/selectors/state.c'
        if not original.is_file():
            self.skipTest('locked Lexbor source unavailable')
        digest = hashlib.sha256(original.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            overlay = Path(temporary)
            filename = 'third_party/patches/lexbor-focus-parser.json'
            generated = patches.apply_patch_file(source, overlay, filename)[0]
            expected = generated.read_bytes()
            self.assertIn(b'lxb_css_selectors_state_specificity_set_c(selectors);', expected)
            self.assertNotIn(b'case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE:', expected)
            self.assertNotIn(b'case LXB_CSS_SELECTOR_PSEUDO_ELEMENT_AFTER:', expected)
            generated.unlink()
            self.assertEqual(patches.apply_patch_file(source, overlay, filename)[0].read_bytes(), expected)
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), digest)

    def test_overlay_repair_does_not_scan_unrelated_nested_dependencies(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            source, overlay = root / 'source', root / 'overlay'
            (source / 'include').mkdir(parents=True)
            (source / 'include/api.h').write_bytes(b'locked header')
            (source / '.source.json').write_text(json.dumps({'files': ['include/api.h']}))
            foreign = overlay / 'third_party/large-unrelated-source'
            foreign.mkdir(parents=True)
            (foreign / 'keep').write_bytes(b'unrelated')
            original_scandir = support.os.scandir
            def scan(path):
                if Path(path) == overlay / 'third_party':
                    raise AssertionError('unrelated dependency tree was scanned')
                return original_scandir(path)
            with patch.object(support.os, 'scandir', side_effect=scan):
                support.restore_overlay_files(source, overlay)
            self.assertEqual((overlay / 'include/api.h').read_bytes(), b'locked header')
            self.assertEqual((foreign / 'keep').read_bytes(), b'unrelated')

    def test_deleted_upstream_header_and_overlay_file_are_reconstructed(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            archive = root / 'source.tar.gz'
            with tarfile.open(archive, 'w:gz') as package:
                for name, data in [('locked/include/api.h', b'original header'), ('locked/source.c', b'original code')]:
                    item = tarfile.TarInfo(name); item.size = len(data)
                    package.addfile(item, io.BytesIO(data))
            spec = dict(archive_root='locked', sha256=hashlib.sha256(archive.read_bytes()).hexdigest(), revision='test')
            support.artifacts.register(root, archive, 'downloads/fixture', spec['sha256'])
            with patch.object(support, 'BUILD', root), patch.object(support, 'download', return_value=archive):
                source = support.extract_source(spec, 'fixture')
                (source / 'include/api.h').unlink()
                recovered = support.extract_source(spec, 'fixture')
                self.assertEqual((recovered / 'include/api.h').read_bytes(), b'original header')
                overlay = root / 'overlay'
                support.restore_overlay_files(source, overlay)
                (overlay / 'source.c').unlink()
                support.restore_overlay_files(source, overlay)
                self.assertEqual((overlay / 'source.c').read_bytes(), b'original code')

    def test_deleted_overlay_recovers_from_tracked_patch_without_changing_upstream(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            root = Path(temporary)
            source, overlay = root / 'upstream', root / 'overlay'
            original = source / 'src/dynapi/SDL_dynapi.h'
            original.parent.mkdir(parents=True)
            original.write_text('#define SDL_DYNAMIC_API 1\n', encoding='utf-8')
            digest = hashlib.sha256(original.read_bytes()).hexdigest()
            filename = 'third_party/patches/sdl3-private-dynapi.json'
            generated = patches.apply_patch_file(source, overlay, filename)[0]
            expected = generated.read_bytes()
            generated.unlink()
            self.assertEqual(patches.apply_patch_file(source, overlay, filename)[0].read_bytes(), expected)
            generated.write_text('untracked stale edit', encoding='utf-8')
            patches.apply_patch_file(source, overlay, filename)
            self.assertEqual(generated.read_bytes(), expected)
            self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), digest)
            with self.assertRaises(RuntimeError): patches.apply_patch_file(source, source, filename)

    def test_stale_or_ambiguous_anchor_is_rejected(self):
        for original in ('other source', 'anchor anchor'):
            with self.assertRaises(RuntimeError):
                patches.patched_text(original, {'before': 'anchor', 'after': 'replacement'})

    def test_every_declared_edit_is_inside_patch_directory_and_affects_build_identity(self):
        locked = json.loads((ROOT / 'third_party/dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
        for name in ('skia', 'sdl3', 'lexbor', 'icu'):
            self.assertTrue(patches.patch_inputs(locked[name]), name)
        spec = locked['lexbor']
        recipe = {'depends': [], 'options': {}}
        original = deps.cmake_build_identity(spec, recipe, 'clang', 'Debug', [])
        with patch.object(patches, 'patch_inputs', return_value={'changed': 'hash'}):
            self.assertNotEqual(original, deps.cmake_build_identity(spec, recipe, 'clang', 'Debug', []))

    def test_data_only_patch_does_not_rebuild_icu_code(self):
        spec = json.loads((ROOT / 'third_party/dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['icu']
        before = deps.cmake_build_identity(spec, {'options': {}}, 'clang', 'Debug', [])
        data_hashes = patches.patch_inputs(spec)
        with patch.object(patches, 'sha256', return_value='changed-data-patch'):
            self.assertNotEqual(data_hashes, patches.patch_inputs(spec))
            self.assertEqual(before, deps.cmake_build_identity(spec, {'options': {}}, 'clang', 'Debug', []))


if __name__ == '__main__': unittest.main()
