"""Tracked dependency edits; the extracted upstream source is always read-only."""
from pathlib import Path, PurePosixPath

from ci_support import read_json
from dependency_support import ROOT, sha256

PATCH_ROOT = ROOT / 'third_party/patches'


def patch_inputs(spec, stage=None):
    result = {}
    for name in spec.get('patches', []):
        path = (ROOT / name).resolve()
        path.relative_to(PATCH_ROOT.resolve())
        if stage and path.suffix == '.json' and read_json(path).get('stage', 'runtime') != stage:
            continue
        result[name] = sha256(path)
    return result


def patched_text(original, patch, label='dependency'):
    changes = patch.get('replacements', [patch] if 'before' in patch else [])
    for index, change in enumerate(changes):
        before = change['before']
        if not before or original.count(before) != change.get('occurrences', 1):
            raise RuntimeError('{} patch {} does not match locked upstream bytes'.format(label, index))
        original = original.replace(before, change['after'])
    return original


def apply_patch_file(source, overlay, filename):
    """Regenerate each changed file from upstream; never patch a patched copy."""
    source, overlay = Path(source).resolve(), Path(overlay).resolve()
    if source == overlay or source in overlay.parents or overlay in source.parents:
        raise RuntimeError('dependency patch output must be separate from upstream')
    path = (ROOT / filename).resolve()
    path.relative_to(PATCH_ROOT.resolve())
    patch = read_json(path)
    records = patch.get('files', [patch])
    written = []
    for record in records:
        relative = PurePosixPath(record['file'])
        if relative.is_absolute() or '..' in relative.parts or '\\' in str(relative) or ':' in str(relative):
            raise RuntimeError('unsafe dependency patch file path')
        original = source.joinpath(*relative.parts).resolve()
        original.relative_to(source)
        destination = overlay.joinpath(*relative.parts).resolve()
        destination.relative_to(overlay)
        text = patched_text(original.read_text(encoding='utf-8'), record, filename)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_text(encoding='utf-8') != text:
            destination.write_text(text, encoding='utf-8')
        written.append(destination)
    return written
