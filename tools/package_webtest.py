#!/usr/bin/env python3
"""Make a small developer preview from an already installed SDK; no builds/CI."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import uuid
import zipfile

sys.dont_write_bytecode = True
from ci_support import checked_child, manifest_digest, source_manifest, write_json
from dependency_support import ROOT, BUILD, sha256
import artifacts


def package(stage, target, configuration, discard=False):
    stage = checked_child(BUILD, Path(stage).resolve())
    windows = target == 'windows-x64'
    if windows != (os.name == 'nt'):
        raise RuntimeError('package on the installed executable host (use WSL for musl)')
    program = stage / 'bin' / ('WebTest.exe' if windows else 'WebTest')
    binary = program if windows else stage / 'bin/WebTest.bin'
    docs = stage / 'share/doc/XLWebEngineUI'
    names = ['bin/WebTest.exe', 'bin/XLWebEngineUI.dll', 'bin/d3dcompiler_47.dll'] if windows else [
        'bin/WebTest', 'bin/WebTest.bin', 'lib/ld-musl-x86_64.so.1',
        'lib/libXLWebEngineUI.so.0', 'lib/libXLWebEngineUI.so.0.1.0']
    names += [path.relative_to(stage).as_posix() for folder in ('licenses', 'docs', 'examples/webtest')
              for path in (docs / folder).rglob('*') if path.is_file()]
    for name in names:
        path = stage / name
        if not path.is_file():
            raise RuntimeError('installed preview input missing: ' + name)
        if path.is_symlink() and path.resolve() != (stage / 'lib/libXLWebEngineUI.so.0.1.0').resolve():
            raise RuntimeError('unexpected preview symlink: ' + name)
    identity = dict(target=target, configuration=configuration, production_ready=False,
                    scope='developer preview; focused ' + configuration + ' checks, no formal SDK/full verification',
                    source_digest=manifest_digest(source_manifest(ROOT)),
                    executable_sha256=sha256(binary), font_source=json.loads((ROOT / 'assets/webtest/source.json').read_text(encoding='utf-8')))
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    output = BUILD / 'previews' / target / run_id
    name = 'WebTest-0.1.0-{}-{}-preview'.format(target, configuration)
    with artifacts.registered_directory(BUILD, output, 'webtest-previews/' + target):
        result = subprocess.run([str(program), '--check'], cwd=str(docs / 'examples/webtest'),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8', errors='replace', timeout=30)
        (output / 'installed-preview.log').write_text(result.stdout, encoding='utf-8')
        if result.returncode:
            raise RuntimeError('installed WebTest failed; see ' + str(output / 'installed-preview.log'))
        archive = output / (name + ('.zip' if windows else '.tar.gz'))
        payload = json.dumps(identity, ensure_ascii=False, indent=2).encode('utf-8')
        if windows:
            with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as writer:
                for relative in sorted(set(names)):
                    writer.write(stage / relative, name + '/' + relative)
                writer.writestr(name + '/PREVIEW-BUILD-INFO.json', payload)
        else:
            import io
            with tarfile.open(archive, 'w:gz') as writer:
                for relative in sorted(set(names)):
                    writer.add(stage / relative, name + '/' + relative, recursive=False)
                info = tarfile.TarInfo(name + '/PREVIEW-BUILD-INFO.json')
                info.size = len(payload)
                writer.addfile(info, io.BytesIO(payload))
        digest = sha256(archive)
        archive.with_name(archive.name + '.sha256').write_text(digest + '  ' + archive.name + '\n', encoding='ascii')
        write_json(output / 'report.json', dict(identity, state='passed', archive=archive.name,
                   archive_bytes=archive.stat().st_size, archive_sha256=digest, installed_smoke='passed'))
    if discard:
        # Only an explicitly selected, resolved child inside this build root.
        # Never delete a parent of the package or a toolchain/work directory.
        if stage.parent != BUILD / 'webtest-install':
            raise RuntimeError('--discard-stage is restricted to build/webtest-install/<target>')
        checked_child(BUILD, stage)
        shutil.rmtree(stage)
    artifacts.collect(BUILD, 3, artifacts.pinned_content_keys(ROOT), families={'webtest-previews/' + target})
    print(archive, flush=True)
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--target', choices=('windows-x64', 'linux-x64-musl'), required=True)
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Debug')
    parser.add_argument('--discard-stage', action='store_true')
    args = parser.parse_args()
    package(args.stage, args.target, args.configuration, args.discard_stage)
