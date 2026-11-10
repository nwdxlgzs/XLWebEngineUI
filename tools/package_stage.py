"""Package a verified incremental Debug SDK without running the formal CI matrix."""
import datetime
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import uuid
import zipfile

import artifacts
from ci_support import checked_child, manifest_digest, source_manifest, write_json
from dependency_support import ROOT, BUILD, sha256


def require_verified_source(expected):
    if manifest_digest(source_manifest(ROOT)) != expected:
        raise RuntimeError('source changed since focused verification; do not package stale binaries')


def inventory(stage):
    result = {}
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            # Installed Linux .so links are allowed only within this SDK.
            path.resolve().relative_to(stage.resolve())
            result[path.relative_to(stage).as_posix()] = dict(bytes=path.stat().st_size, sha256=sha256(path))
    return result


def require_inputs(files, target, symbols=False):
    if symbols:
        names = ['symbols/XLWebEngineUI.pdb', 'symbols/WebTest.pdb'] if target == 'windows-x64' else [
            'symbols/libXLWebEngineUI.so.debug', 'symbols/WebTest.debug']
    else:
        names = ['include/XLWebEngineUI/xl.h', 'lib/cmake/XLWebEngineUI/XLWebEngineUIConfig.cmake',
                 'share/doc/XLWebEngineUI/RESOURCE-BUILD-INFO.json',
                 'share/doc/XLWebEngineUI/STATIC-ARCHIVE-MERGE.json',
                 'share/doc/XLWebEngineUI/docs/webtest.md',
                  'share/doc/XLWebEngineUI/examples/webtest/index.html']
        names += ['bin/WebTest.exe', 'bin/XLWebEngineUI.dll', 'lib/XLWebEngineUI_static.lib',
                  'lib/XLWebEngineUI.lib', 'bin/d3dcompiler_47.dll'] if target == 'windows-x64' else [
            'bin/WebTest', 'bin/WebTest.bin', 'lib/libXLWebEngineUI_static.a',
            'lib/libXLWebEngineUI.so.0.1.0', 'lib/ld-musl-x86_64.so.1']
        if not any(name.startswith('share/doc/XLWebEngineUI/licenses/') for name in files):
            raise RuntimeError('stage SDK has no license files')
    for name in names:
        if not files.get(name, {}).get('bytes'):
            raise RuntimeError('stage package input missing or empty: ' + name)


def duplicate_payloads(stage, files):
    """Aliases in a tar archive have no payload; distinguish them from copies."""
    groups = {}
    for name, record in files.items():
        if record['bytes'] and not (stage / name).is_symlink():
            groups.setdefault((record['bytes'], record['sha256']), []).append(name)
    duplicates = [dict(bytes=size, sha256=digest, paths=names,
                       redundant_bytes=size * (len(names) - 1))
                  for (size, digest), names in groups.items() if len(names) > 1]
    libraries = [item for item in duplicates if any(
        name.startswith(('bin/', 'lib/')) and (name.endswith(('.dll', '.lib', '.a')) or '.so' in name)
        for name in item['paths'])]
    if libraries:
        raise RuntimeError('SDK contains duplicate physical library payloads: ' + repr(libraries))
    return dict(duplicate_libraries=0, duplicate_files=duplicates,
                redundant_bytes=sum(item['redundant_bytes'] for item in duplicates),
                note='relative library symlinks are archive aliases, not repeated library bytes; static and dynamic SDK variants intentionally contain independently usable implementations')


def archive_component(stage, name, packing, output, command, windows):
    # Stream the installed tree directly. CPack's extra expanded SDK copy used
    # multiple GiB and dominated incremental packaging on WSL/NTFS.
    archive = output / (name + ('.zip' if windows else '.tar.gz'))
    partial = packing / archive.name
    paths = sorted(stage.rglob('*'))
    for path in paths:
        path.resolve().relative_to(stage.resolve())
        if path.is_symlink() and (windows or os.path.isabs(os.readlink(path))):
            raise RuntimeError('stage archive requires portable relative symlinks: ' + str(path))
    try:
        if windows:
            with zipfile.ZipFile(partial, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as writer:
                for path in paths:
                    writer.write(path, name + '/' + path.relative_to(stage).as_posix())
        else:
            with tarfile.open(partial, 'w:gz', compresslevel=6, format=tarfile.PAX_FORMAT, dereference=False) as writer:
                writer.add(stage, arcname=name, recursive=False)
                for path in paths:
                    writer.add(path, arcname=name + '/' + path.relative_to(stage).as_posix(), recursive=False)
        partial.replace(archive)
    finally:
        partial.unlink(missing_ok=True)
    digest = sha256(archive)
    archive.with_name(archive.name + '.sha256').write_text(digest + '  ' + archive.name + '\n', encoding='ascii')
    return dict(path=archive.relative_to(BUILD).as_posix(), bytes=archive.stat().st_size, sha256=digest)


def package(work, target, verification, command):
    """Caller holds the build-tree lock and has passed the selected CTest checks."""
    if target not in ('windows-x64', 'linux-x64-musl'):
        raise RuntimeError('stage packages require an official Windows or musl target')
    require_verified_source(verification['source_digest'])
    windows = target == 'windows-x64'
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    output = BUILD / 'packages' / target / run_id
    name = 'XLWebEngineUI-0.1.0-' + target + '-Debug-preview'
    identity = dict(schema=1, target=target, configuration='Debug', production_ready=False,
                    scope='verified functional stage; selected Debug checks and extracted WebTest smoke; no full CI or SDK consumer matrix',
                    git_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    git_status=subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).splitlines(),
                    source_digest=verification['source_digest'], verification=verification)
    from ci_support import read_json
    static_name = 'XLWebEngineUI_static.lib' if windows else 'libXLWebEngineUI_static.a'
    identity['static_archive_merge'] = read_json(work / (static_name + '.merge.json'))
    if not identity['static_archive_merge']:
        raise RuntimeError('static archive merge evidence is missing')
    family = 'stage-packages/' + target
    try:
        with artifacts.registered_directory(BUILD, output, family):
            report = dict(identity, state='running', archives=[])
            write_json(output / 'report.json', report)
            try:
                with tempfile.TemporaryDirectory(prefix='_scratch-', dir=output) as temporary:
                    scratch = checked_child(BUILD, Path(temporary))
                    sdk, symbols, packing = scratch / 'sdk', scratch / 'symbols', scratch / 'packing'
                    packing.mkdir()
                    for stage, component in ((sdk, 'Runtime'), (symbols, 'Symbols')):
                        if component == 'Runtime':
                            # Installing this immutable archive never strips or edits it.
                            # Avoid a large WSL/NTFS copy when hard links are available.
                            static_name = 'XLWebEngineUI_static.lib' if windows else 'libXLWebEngineUI_static.a'
                            (stage / 'lib').mkdir(parents=True)
                            try:
                                os.link(work / static_name, stage / 'lib' / static_name)
                            except OSError:
                                pass  # CMake performs the normal copy on other filesystems.
                        command.run(['cmake', '--install', work, '--prefix', stage, '--component', component])
                        files = inventory(stage)
                        require_inputs(files, target, component == 'Symbols')
                        if component == 'Runtime':
                            report['payload_audit'] = duplicate_payloads(stage, files)
                        write_json(stage / 'STAGE-BUILD-INFO.json', dict(identity, component=component, files=files))
                        report['archives'].append(archive_component(stage, name + ('-symbols' if component == 'Symbols' else ''),
                                                                    packing, output, command, windows))
                    # Remove installed inputs before extracting the deliverable.
                    for stage in (sdk, symbols, packing):
                        shutil.rmtree(checked_child(BUILD, stage))
                    extracted = scratch / 'extracted'
                    archive = BUILD / report['archives'][0]['path']
                    if windows:
                        with zipfile.ZipFile(archive) as reader:
                            reader.extractall(extracted)
                    else:
                        # Generated above from our validated, contained install tree.
                        with tarfile.open(archive) as reader:
                            reader.extractall(extracted, filter='fully_trusted') if hasattr(tarfile, 'data_filter') else reader.extractall(extracted)
                    portable = extracted / name
                    from ci_support import read_json
                    expected = read_json(portable / 'STAGE-BUILD-INFO.json')['files']
                    actual = inventory(portable)
                    actual.pop('STAGE-BUILD-INFO.json')
                    if actual != expected:
                        raise RuntimeError('extracted SDK file hashes do not match installed inputs')
                    smoke = subprocess.run([portable / 'bin' / ('WebTest.exe' if windows else 'WebTest'), '--check'],
                                           cwd=portable / 'share/doc/XLWebEngineUI/examples/webtest', env=command.env,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8', errors='replace', timeout=30)
                    (output / 'extracted-webtest.log').write_text(smoke.stdout, encoding='utf-8')
                    if smoke.returncode:
                        raise RuntimeError('extracted WebTest failed; see ' + str(output / 'extracted-webtest.log'))
                    shutil.copyfile(work / 'Testing/Temporary/LastTest.log', output / 'focused-tests.log')
                    require_verified_source(verification['source_digest'])
                    report.update(state='passed', extracted_webtest='passed', extracted_hashes='passed')
            except BaseException as error:
                report.update(state='failed', error=str(error))
                raise
            finally:
                write_json(output / 'report.json', report)
    finally:
        artifacts.collect(BUILD, 3, artifacts.pinned_content_keys(ROOT), families={family})
    print('Stage package report: ' + str(output / 'report.json'), flush=True)
    return report
