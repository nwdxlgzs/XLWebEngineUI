"""Exercise the shipped executable against a real local UI directory."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def run(program, directory, *args, expected=0):
    process = subprocess.run([str(program)] + list(args), cwd=str(directory),
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             encoding='utf-8', errors='replace', timeout=25)
    if process.returncode != expected:
        raise AssertionError('exit {} != {}\n{}'.format(process.returncode, expected, process.stdout))
    return process.stdout


def verify(program, work):
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=str(work)) as temporary:
        root = Path(temporary)
        (root / 'nested').mkdir()
        (root / 'scripts').mkdir()
        (root / 'index.html').write_text(
            '<!doctype html><link rel=stylesheet href="theme%20test.css">'
            '<div id=box></div><script src="scripts/boot.js"></script>'
            '<script>document.querySelector("#box").textContent=xl.readText("message.txt")</script>', encoding='utf-8')
        (root / 'theme test.css').write_text('body{margin:0}#box{width:40px;height:30px;background-color:red}', encoding='utf-8')
        (root / 'scripts/boot.js').write_text(
            'console.log("classic script loaded");document.querySelector("#box").style.setProperty("background-color","#00ff00");', encoding='utf-8')
        (root / 'message.txt').write_text('Hello UI', encoding='utf-8')
        (root / 'unused.bin').write_bytes(b'This must never be read or mounted')
        output = run(program, root, '--check', '--width', '80', '--height', '60', '--snapshot', 'frame.ppm')
        assert 'Rendered 80x60' in output and 'resources=4' in output, output
        assert 'unused.bin' not in output, output
        assert 'log: classic script loaded' in output, output
        image = (root / 'frame.ppm').read_bytes()
        header = b'P6\n80 60\n255\n'
        assert image.startswith(header) and len(image) == len(header) + 80 * 60 * 3
        offset = len(header) + (25 * 80 + 30) * 3
        assert image[offset:offset+3] == b'\x00\xff\x00', image[offset:offset+3]
        assert not list(root.glob('*.dat')) and not list(root.glob('*.ttf'))
        (root / 'vector.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="30" height="20"><rect width="30" height="20" fill="red"/></svg>',encoding='utf-8')
        (root / 'images.html').write_text('<style>body{margin:0}img{display:block}</style><img src="vector.svg" alt="Vector">',encoding='utf-8')
        output=run(program,root,'images.html','--check','--width','80','--height','60','--snapshot','image.ppm')
        assert 'vector.svg' in output and 'resources=2' in output,output
        image=(root/'image.ppm').read_bytes();offset=len(header)+(10*80+10)*3
        assert image[offset:offset+3]==b'\xff\x00\x00',image[offset:offset+3]
        (root/'canvas.html').write_text('<style>body{margin:0}canvas{display:block}</style><canvas id="c" width="30" height="20"></canvas><script>var x=document.getElementById("c").getContext("2d");x.fillStyle="lime";x.fillRect(0,0,30,20)</script>',encoding='utf-8')
        output=run(program,root,'canvas.html','--check','--width','80','--height','60','--snapshot','canvas.ppm')
        assert 'resources=1' in output,output
        image=(root/'canvas.ppm').read_bytes();offset=len(header)+(10*80+10)*3
        assert image[offset:offset+3]==b'\x00\xff\x00',image[offset:offset+3]
        output = run(program, root, '--check', '--no-scripts')
        assert 'resources=2' in output, output
        # A UTF-8 page location keeps CSS relative resolution inside the root.
        (root / 'nested/页面.html').write_text('<link rel=stylesheet href="../theme%20test.css"><div id=box>Nested</div>', encoding='utf-8')
        output = run(program, root, 'nested/页面.html', '--check')
        assert '%E9%A1%B5%E9%9D%A2.html' in output and 'resources=2' in output, output
        # Logical source location is preserved even for a shared physical file.
        (root / 'nested/shared.html').write_text('<link rel=stylesheet href="theme%20test.css"><div id=box>Alias</div>', encoding='utf-8')
        try:
            (root / 'alias.html').symlink_to(root / 'nested/shared.html')
        except OSError:
            pass
        else:
            output = run(program, root, 'alias.html', '--check')
            assert 'xl-webengine-ui://app/alias.html' in output and 'resources=2' in output, output
        (root / 'index.html').write_text('<link rel=stylesheet href="missing.css"><p>Missing dependency</p>', encoding='utf-8')
        output = run(program, root, '--check', expected=2)
        assert 'missing.css' in output, output
        outside = root.parent / (root.name + '-outside.css')
        outside.write_text('#box{background-color:blue}', encoding='utf-8')
        try:
            try:
                (root / 'escape.css').symlink_to(outside)
            except OSError:
                pass  # Windows can forbid symlink creation for this user.
            else:
                (root / 'index.html').write_text('<link rel=stylesheet href="escape.css"><p>Root boundary</p>', encoding='utf-8')
                assert 'outside the UI root' in run(program, root, '--check', expected=2)
        finally:
            outside.unlink()
        (root / 'index.html').write_text('<script>throw new Error("broken preview")</script>', encoding='utf-8')
        output = run(program, root, '--check', expected=1)
        assert 'broken preview' in output, output
        (root / 'index.html').unlink()
        assert 'WebTest:' in run(program, root, '--check', expected=1)
        assert 'Usage:' in run(program, root, '--help')
        assert not list(root.glob('*.dat'))
    print('WebTest local root, lazy dependencies, scripts, Unicode paths, rendered pixels and failures passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    verify(args.program.resolve(), args.work.resolve())
