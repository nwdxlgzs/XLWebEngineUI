"""Require real DXC text-page rendering where SM6 is available."""
import argparse
import subprocess

parser = argparse.ArgumentParser()
for name in ('probe', 'program', 'root'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
probe = subprocess.run([args.probe, '--modern'], timeout=45)
if probe.returncode:
    raise SystemExit(probe.returncode)
page = subprocess.run([args.program, '--root', args.root, '--check', '--graphics', 'd3d12'],
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                      encoding='utf-8', errors='replace', timeout=45)
print(page.stdout)
if page.returncode:
    raise SystemExit(page.returncode)
if 'Graphics requested=2 active=2 shader_compiler=2' not in page.stdout:
    raise SystemExit('DXC text page silently fell back after the SM6 probe passed')
