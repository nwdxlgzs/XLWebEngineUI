"""Build ICU data from Chromium's pinned common profile, using our locked ICU."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import zipfile

import artifacts
from ci_support import WorkLock, checked_child, read_json, write_json
from dependency_support import ROOT, BUILD, download, sha256

PROFILE = ROOT / 'third_party/icu-chromium'
RECIPE = ROOT / 'third_party/recipes/icu-data-tools'


def adapt_break_resources(data):
    from dependency_patches import apply_patch_file
    filename = 'third_party/patches/icu-chromium-break-data.json'
    # The release sources contain the binary .dat; these text inputs come
    # from the separately locked ICU data archive overlaid immediately above.
    # Preserve just the five pristine inputs before applying the tracked patch.
    with tempfile.TemporaryDirectory(prefix='break-inputs-', dir=data.parent) as temporary:
        original = Path(temporary)
        for record in read_json(ROOT / filename)['files']:
            path = checked_child(data, data / record['file'])
            destination = original / record['file']
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
        apply_patch_file(original, data, filename)


def prepare(source, prefix, target, configuration, env, flags, jobs=6):
    from ci import Commands
    locked = read_json(PROFILE / 'source.json')
    if sha256(PROFILE / 'common.json') != locked['filter_sha256']:
        raise RuntimeError('Chromium ICU common filter differs from its locked hash')
    for item in locked['mappings']:
        if sha256(PROFILE / 'mappings' / item['name']) != item['sha256']:
            raise RuntimeError('Chromium mapping differs from its locked hash: ' + item['name'])
    files = [p for directory in (PROFILE, RECIPE) for p in sorted(directory.rglob('*')) if p.is_file()]
    identity = dict(icu_source=read_json(source / '.source.json')['sha256'],
                    data_source=locked['data_source']['sha256'],
                    recipes={p.relative_to(ROOT).as_posix(): sha256(p) for p in files},
                    generator=sha256(Path(__file__)))
    identity['data_patch'] = sha256(ROOT / 'third_party/patches/icu-chromium-break-data.json')
    identity['patch_driver'] = sha256(ROOT / 'tools/dependency_patches.py')
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:16]
    output = BUILD / 'generated/icu-data' / key
    data_file = output / 'icudt79l.dat'
    with WorkLock(BUILD / 'state/icu-data-common.lock'):
        old = read_json(output / 'manifest.json', {})
        if old.get('identity') == identity and data_file.is_file() and sha256(data_file) == old.get('sha256'):
            artifacts.register(BUILD, output, 'icu-data/common', key, old['references'])
            print('Chromium common ICU data reused: ' + str(old['bytes']) + ' bytes', flush=True)
            return data_file
        archive = download(locked['data_source'], 'icu-data-source')
        # Locale filters and data redirects do not compile the host tools.
        # Keep their cache keyed to code, compiler/runtime prefix and recipes.
        host_identity = dict(source=identity['icu_source'], target=target, prefix=prefix.relative_to(BUILD).as_posix(),
                             configuration=configuration, recipes={p.relative_to(RECIPE).as_posix(): sha256(p)
                                for p in sorted(RECIPE.rglob('*')) if p.is_file()})
        host_key = hashlib.sha256(json.dumps(host_identity, sort_keys=True).encode()).hexdigest()[:12]
        host = BUILD / 'host-tools/icu-data-tools' / target / host_key
        host.mkdir(parents=True, exist_ok=True)
        references = [p.relative_to(BUILD).as_posix() for p in (source, prefix, archive)]
        artifacts.register(BUILD, host, 'icu-data-tools/' + target, host_key, references)
        output.mkdir(parents=True, exist_ok=True)
        references.append(host.relative_to(BUILD).as_posix())
        artifacts.register(BUILD, output, 'icu-data/common', key, references)
        with artifacts.lease(BUILD, output), artifacts.lease(BUILD, host), contextlib.ExitStack() as leases:
            for item in (source, prefix, archive):
                leases.enter_context(artifacts.lease(BUILD, item))
            command = Commands(output / 'commands.log', env, verbose=False)
            command.run(['cmake', '-S', RECIPE, '-B', host, '-G', 'Ninja',
                         '-DCMAKE_BUILD_TYPE=' + configuration, '-DXL_ICU_SOURCE=' + source.as_posix(),
                         '-DXL_ICU_PREFIX=' + prefix.as_posix()] + flags)
            command.run(['cmake', '--build', host, '--parallel', str(jobs)])
            with tempfile.TemporaryDirectory(prefix='_scratch-', dir=output) as temporary:
                scratch = checked_child(BUILD, Path(temporary))
                data = scratch / 'data'
                shutil.copytree(source / 'source/data', data)
                with zipfile.ZipFile(archive) as package:
                    for entry in package.infolist():
                        path = scratch / entry.filename
                        checked_child(scratch, path)
                    package.extractall(scratch)
                for entry in locked['mappings']:
                    shutil.copyfile(PROFILE / 'mappings' / entry['name'], data / 'mappings' / entry['name'])
                adapt_break_resources(data)
                # Chromium excludes pseudo locales before applying the common filter.
                for path in data.rglob('*'):
                    if path.is_file() and (path.name.startswith('en_XA') or path.name.startswith('ar_XB')):
                        path.unlink()
                tools = host / 'bin'
                if os.name == 'nt':
                    tools = scratch / 'tools'
                    for executable in (host / 'bin').glob('*.exe'):
                        link = tools / executable.stem / configuration / executable.name
                        link.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(executable, link)
                tool_env = dict(env, PYTHONPATH=str(source / 'source/python'),
                                XL_ICU_TOOL_DATA=str(source / 'source/data/in/icudt79l.dat'),
                                PYTHONDONTWRITEBYTECODE='1')
                command.run([sys.executable, '-B', '-m', 'icutools.databuilder',
                             '--mode', 'windows-exec' if os.name == 'nt' else 'unix-exec',
                             '--src_dir', data, '--filter_file', PROFILE / 'common.json',
                             '--out_dir', scratch / 'out', '--tmp_dir', scratch / 'tmp',
                             '--tool_dir', tools, '--tool_cfg', configuration], cwd=scratch, env=tool_env)
                names = sorted(p.relative_to(scratch / 'out').as_posix() for p in (scratch / 'out').rglob('*') if p.is_file())
                items = scratch / 'icudata.lst'
                items.write_text('\n'.join(names) + '\n', encoding='ascii')
                tool = host / 'bin' / ('icupkg.exe' if os.name == 'nt' else 'icupkg')
                # Chromium's common build intentionally excludes some alias/title
                # dependencies; its copy_data.sh also uses --ignore-deps.
                command.run([tool, '-tl', '--ignore-deps', '-a', items, '-s', scratch / 'out', 'new', data_file], env=tool_env)
                original = (source / 'source/data/in/icudt79l.dat').stat().st_size
                if not 0 < data_file.stat().st_size < original:
                    raise RuntimeError('Chromium filtering did not reduce ICU data')
                write_json(output / 'manifest.json', dict(identity=identity, profile_revision=locked['revision'],
                           profile='Chromium common', bytes=data_file.stat().st_size, original_bytes=original,
                           sha256=sha256(data_file), items=names, references=references))
        print('Chromium common ICU data built: {} -> {} bytes'.format(original, data_file.stat().st_size), flush=True)
        return data_file
