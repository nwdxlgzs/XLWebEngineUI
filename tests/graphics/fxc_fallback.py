"""Run optional compiler failure with private copies; never change the system DLL."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
for name in ('program', 'engine', 'compiler', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--modern', action='store_true')
parser.add_argument('--local', action='store_true')
args = parser.parse_args()
args.work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=args.work) as directory:
    root = Path(directory)
    for source, name in ((args.program, 'probe.exe'), (args.engine, 'XLWebEngineUI.dll'),
                         (args.compiler, 'd3dcompiler_47.dll')):
        shutil.copyfile(source, root / name)
    result = subprocess.run([str(root / 'probe.exe')] + (['--modern'] if args.modern else ['--local'] if args.local else []), cwd=root, timeout=45)
    raise SystemExit(result.returncode)
