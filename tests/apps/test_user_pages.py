"""Run user-supplied HTML files through the shipped executable, without edits."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    pages = sorted(p for p in args.root.rglob('*') if p.suffix.lower() in ('.html', '.htm'))
    for page in pages:
        process = subprocess.run([str(args.program.resolve()), str(page.relative_to(args.root)), '--check'],
                                 cwd=args.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 encoding='utf-8', errors='replace', timeout=30)
        print(str(page.relative_to(args.root)) + '\n' + process.stdout)
        if process.returncode:
            raise RuntimeError('user page failed to load/render: ' + str(page))
    print('{} user pages loaded/rendered; this smoke test does not certify CSS fidelity'.format(len(pages)))


if __name__ == '__main__':
    main()
