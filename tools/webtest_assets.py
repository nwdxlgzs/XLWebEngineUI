#!/usr/bin/env python3
"""Embed WebTest's hash-locked fallback font using the existing host encoder."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from ci_support import checked_child

ROOT = Path(__file__).resolve().parents[1]


def generate(encoder, output):
    source = ROOT / 'assets/webtest'
    lock = json.loads((source / 'source.json').read_text(encoding='utf-8'))
    for name, expected in lock['files'].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError('WebTest font source hash mismatch: ' + name)
    output = checked_child(ROOT / 'build', Path(output).resolve())
    output.mkdir(parents=True, exist_ok=True)
    compressed = output / 'font.zst'
    subprocess.run([str(encoder), str(source / 'Roboto.ttf'), str(compressed)], check=True)
    raw = compressed.read_bytes()
    lines = [','.join('0x{:02x}'.format(value) for value in raw[i:i+24]) + ',' for i in range(0, len(raw), 24)]
    (output / 'webtest_font.h').write_text(
        '#pragma once\n#include <cstddef>\nnamespace webtest {\n'
        'inline constexpr unsigned char font_data[] = {\n' + '\n'.join(lines) + '\n};\n'
        'inline constexpr std::size_t font_size = ' + str((source / 'Roboto.ttf').stat().st_size) + ';\n}\n',
        encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--encoder', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.encoder.resolve(), args.output)
