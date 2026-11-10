#!/usr/bin/env python3
"""Create the marked, reproducible private SDL overlay without changing upstream."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
from pathlib import Path
import shutil
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
from dependency_support import restore_overlay_files


def prepare(source, build):
    patch_path = Path(__file__).resolve().parents[2] / 'patches/sdl3-private-dynapi.json'
    patch = json.loads(patch_path.read_text(encoding='utf-8'))
    original = (source / patch['file']).read_text(encoding='utf-8')
    if original.count(patch['before']) != 1:
        raise RuntimeError('SDL dynamic-API patch no longer matches the locked source')
    overlay = build / 'source'
    identity = {'source': str(source.resolve()),
                'upstream_file': hashlib.sha256(original.encode('utf-8')).hexdigest(),
                'patch': hashlib.sha256(patch_path.read_bytes()).hexdigest()}
    stamp = build / '.xl-private-sdl.json'
    if stamp.is_file() and json.loads(stamp.read_text(encoding='utf-8')) == identity:
        restore_overlay_files(source, overlay)
    else:
        shutil.copytree(source, overlay, dirs_exist_ok=True, symlinks=True)
    (overlay / patch['file']).write_text(original.replace(patch['before'], patch['after']), encoding='utf-8')
    stamp.write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    arguments = parser.parse_args()
    prepare(arguments.source, arguments.build)
