#!/usr/bin/env python3
"""Independently inspect relocated view SDKs and their candidate archives."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import zipfile

sys.dont_write_bytecode = True
from ci_support import source_manifest, manifest_digest, read_json, write_json
from dependency_support import ROOT, BUILD
import bootstrap


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def consumer_test_count(path):
    # CTest may use the host locale for timestamps. Count its ASCII result
    # markers without requiring the surrounding localized log to be UTF-8.
    return path.read_bytes().count(b'Test Passed.')


def archive_files(path):
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as archive:
            return {item.filename: archive.read(item) for item in archive.infolist() if not item.is_dir()}
    with tarfile.open(path) as archive:
        return {item.name: archive.extractfile(item).read() for item in archive.getmembers() if item.isfile()}


def audit(report_path, output=None):
    report_path = Path(report_path).resolve()
    report = read_json(report_path)
    require(report['state'] == 'passed' and report['phase'] == 'web-core', 'document CI did not pass')
    require(report['official_matrix_selected'], 'audit requires both official targets')
    require(report['source_digest'] == manifest_digest(source_manifest(ROOT)), 'CI source fingerprint is stale')
    require(not any(read_json(report_path.parent / item['target'] / 'result.json', {}).get('validation_scratch')
                    for item in report['targets']),
            'expanded validation files were released after verification; consult sdk-audit.json and retained package hashes')
    require((ROOT / 'WORKMEM.MD').stat().st_size <= 40000, 'WORKMEM exceeds its limit')
    llvm = bootstrap.llvm_bin()

    def command(name, *args):
        tool = llvm / (name + ('.exe' if os.name == 'nt' else ''))
        return subprocess.check_output([str(tool)] + [str(arg) for arg in args], text=True, encoding='utf-8')

    def in_build(relative):
        path = (BUILD / str(relative).replace('\\', '/')).resolve()
        require(BUILD.resolve() in path.parents, 'audit input is outside build: ' + str(path))
        return path

    lock = read_json(ROOT / 'third_party/dependencies.lock.json')['dependencies']
    public = set(re.findall(r'XL_API\s+[^;]*?\b(xl_\w+)\s*\(',
                            (ROOT / 'include/XLWebEngineUI/xl.h').read_text(encoding='utf-8')))
    result = {'state': 'passed', 'source_digest': report['source_digest'], 'ci': str(report_path.relative_to(BUILD)),
              'raw_sha256': digest(ROOT / 'RAW-PLAN.MD'), 'targets': {}, 'packages': []}
    for target in report['targets']:
        name = target['target']
        windows = name == 'windows-x64'
        require(target['state'] == 'passed' and target['dependency_validation']['state'] == 'passed', name + ': CI failure')
        require({item['name'] for item in target['configurations']} == {'Debug', 'Release'}, name + ': incomplete matrix')
        packed = BUILD / 'generated/sdk-resources' / name / 'resource-1.zst'
        packed_bytes = packed.read_bytes()
        catalog = read_json(packed.parent / 'manifest.json')
        require(digest(packed) == catalog['resources'][0]['compressed_sha256'], name + ': corrupt resource catalog')
        details = {}
        release_licenses = {}
        for configuration in target['configurations']:
            config = configuration['name']
            prefix = name + '/' + config + ': '
            for gate in ('resource_sdk', 'view_sdk', 'document_sdk', 'inline_text_sdk', 'css_whitespace_sdk', 'css_font_sdk', 'css_box_sdk', 'native_view', 'sdk_c_cpp_static_shared', 'private_symbol_isolation'):
                require(configuration.get(gate) == 'passed', prefix + 'missing gate ' + gate)
            stage = report_path.parent / name / config / 'sdk-relocated'
            static = stage / 'lib' / ('XLWebEngineUI_static.lib' if windows else 'libXLWebEngineUI_static.a')
            shared = stage / ('bin/XLWebEngineUI.dll' if windows else 'lib/libXLWebEngineUI.so.0.1.0')
            require(packed_bytes in static.read_bytes() and packed_bytes in shared.read_bytes(), prefix + 'embedded ICU missing')
            definitions = set(re.findall(r'^(\S+)\s+[A-Za-z]\s',
                                         command('llvm-nm', '--defined-only', '--extern-only', '--format=posix', static), re.MULTILINE))
            require({'xl_private_ZSTD_decompress', 'xl_private_SDL_Init', 'xl_view_render', 'xl_document_eval',
                     'xl_view_dom_text_rects', 'xl_view_hit_test_text',
                     'xl_private_JS_NewRuntime', 'xl_private_lxb_html_document_create'}.issubset(definitions), prefix + 'missing private renderer/document implementation')
            require(any(symbol.startswith('xl_private_') and 'drawRect' in symbol and 'SkCanvas' in symbol
                        for symbol in definitions), prefix + 'Skia C++ implementation missing')
            require(any(re.fullmatch(r'xl_private_u_init_\d+_xl', symbol) for symbol in definitions), prefix + 'private ICU missing')
            forbidden = {'ZSTD_decompress', 'POOL_create', 'g_debuglevel', 'SDL_Init', 'JS_NewRuntime', 'lxb_html_document_create', 'malloc', 'free',
                         '__libc_start_main', 'pthread_create', 'xl_private_ZSTD_compress', 'xl_private_ZSTD_createCCtx'}
            require(not definitions & forbidden, prefix + 'unisolated dependency, libc or encoder in static SDK')
            if windows:
                exports = command('llvm-readobj', '--coff-exports', shared)
                exported = set(re.findall(r'^  Name: (.+)$', exports, re.MULTILINE))
                imports = command('llvm-readobj', '--coff-imports', shared)
                needed = re.findall(r'^  Name: (.+)$', imports, re.MULTILINE)
                system = {'KERNEL32', 'USER32', 'GDI32', 'WINMM', 'IMM32', 'OLE32', 'OLEAUT32', 'VERSION', 'ADVAPI32',
                          'SETUPAPI', 'SHELL32', 'DINPUT8', 'DWRITE', 'USP10', 'OPENGL32', 'FONTSUB', 'COMDLG32', 'WINSPOOL',
                          'COMCTL32', 'DXGI'}
                require(all(item.upper()[:-4] in system and item.upper().endswith('.DLL') for item in needed),
                        prefix + 'unexpected DLL imports: ' + str(needed))
            else:
                exports = command('llvm-nm', '--dynamic', '--defined-only', '--format=posix', shared)
                exported = {line.split()[0].split('@')[0] for line in exports.splitlines()
                            if line.strip() and not line.startswith('XLWEBENGINEUI_')}
                needed = [line.strip() for line in command('llvm-readobj', '--needed-libs', shared).splitlines() if line.startswith('  ')]
                require(needed == ['libc.so'], prefix + 'incorrect runtime boundary: ' + str(needed))
                dynamic = command('llvm-nm', '--dynamic', shared)
                require(not any(item in dynamic for item in ('GLIBC_', 'GLIBCXX_', 'CXXABI_')), prefix + 'GNU runtime dependency')
            require(exported == public, prefix + 'public ABI differs from header')
            for cmake in stage.rglob('*.cmake'):
                content = cmake.read_text(encoding='utf-8')
                require(not any(item in content for item in ('build/deps/', 'D:/project/', '/mnt/d/project/')),
                        prefix + 'CMake contains build paths: ' + str(cmake))
            inputs = read_json(stage / 'share/doc/XLWebEngineUI/RESOURCE-BUILD-INFO.json')
            licenses = {}
            for component, stamp in inputs['graphics'].items():
                spec = lock[component]
                require(stamp['source_sha256'] == spec['sha256'], prefix + component + ' source mismatch')
                source = BUILD / 'deps/src' / component / spec['sha256'][:12]
                for relative in spec['license_files']:
                    licenses[component + '/' + relative] = (source / relative).read_bytes()
            for component, spec in inputs['graphics']['skia'].get('auxiliary_sources', {}).items():
                for relative in spec['license_files']:
                    licenses[component + '/' + relative] = (in_build(spec['source']) / relative).read_bytes()
            generated = BUILD / 'generated/sdk-inputs' / name / config / 'licenses'
            for source in generated.rglob('*'):
                if source.is_file():
                    licenses.setdefault(source.relative_to(generated).as_posix(), source.read_bytes())
            licenses['zstd-BSD.txt'] = (BUILD / 'deps/src/zstd' / lock['zstd']['sha256'][:12] / 'LICENSE').read_bytes()
            licenses['icu/LICENSE'] = (BUILD / 'deps/src/icu' / lock['icu']['sha256'][:12] / 'LICENSE').read_bytes()
            if not windows:
                runtime = read_json(BUILD / 'toolchains/wrappers/linux-musl/toolchain.json')
                runtime_licenses = BUILD / 'toolchains' / Path(runtime['runtime']).name / 'licenses'
                for component in ('libcxx', 'libcxxabi', 'libunwind', 'compiler-rt', 'llvm-libc-headers', 'musl'):
                    licenses[component + '.txt'] = (runtime_licenses / (component + '.txt')).read_bytes()
            installed_licenses = stage / 'share/doc/XLWebEngineUI/licenses'
            licenses['WebTest-Roboto-OFL.txt'] = (ROOT / 'assets/webtest/OFL.txt').read_bytes()
            for relative, expected in licenses.items():
                require((installed_licenses / relative).read_bytes() == expected, prefix + 'license differs: ' + relative)
            consumer_log = report_path.parent / name / config / 'consumer-build/Testing/Temporary/LastTest.log'
            require(consumer_test_count(consumer_log) == 4, prefix + 'C/C++ static/shared consumers did not all pass')
            test_font = report_path.parent / name / config / 'consumer-source/test-font.ttf'
            original_font = in_build(inputs['graphics']['skia']['source_overlay']) / 'resources/fonts/Roboto-Regular.ttf'
            require(test_font.read_bytes() == original_font.read_bytes(), prefix + 'consumer font is not the locked test input')
            details[config] = {'embedded_icu': True, 'private_c_cpp': True, 'public_exports': len(exported),
                               'needed': needed, 'licenses': len(licenses), 'relocated_consumers': 4}
            require((stage / 'bin' / ('WebTest.exe' if windows else 'WebTest')).is_file(), prefix + 'WebTest missing')
            if name == 'linux-x64-musl':
                require(digest(stage / 'lib/ld-musl-x86_64.so.1') == inputs['compiler']['musl_runtime_sha256'],
                        prefix + 'WebTest musl loader mismatch')
                require((stage / 'bin/WebTest.bin').is_file(), prefix + 'WebTest executable missing')
            if config == 'Release':
                release_licenses = licenses
        require(len(target['packages']) == 2, name + ': expected SDK and symbols packages')
        for package in target['packages']:
            path = in_build(package['path'])
            require(digest(path) == package['sha256'], name + ': package checksum mismatch')
            require(path.with_name(path.name + '.sha256').read_text().split()[0] == package['sha256'], name + ': checksum file mismatch')
            files = archive_files(path)
            symbols = '-symbols.' in path.name
            if symbols:
                require(files and all(member.endswith(('.pdb', '.debug')) for member in files), name + ': mixed symbol package')
                for member, content in files.items():
                    require(content.startswith(b'Microsoft C/C++ MSF 7.00' if member.endswith('.pdb') else b'\x7fELF'), name + ': invalid debug symbols')
            else:
                require(not any(member.endswith(('.pdb', '.debug', '.dat', '.zst')) for member in files), name + ': loose runtime data or symbols')
                require(any(member.endswith('/bin/' + ('WebTest.exe' if windows else 'WebTest')) for member in files), name + ': package missing WebTest')
                require(any(member.endswith('/docs/webtest.md') for member in files), name + ': WebTest documentation missing')
                for relative, expected in release_licenses.items():
                    matches = [content for member, content in files.items() if member.endswith('/licenses/' + relative)]
                    require(matches == [expected], name + ': package license differs: ' + relative)
                info = [json.loads(content) for member, content in files.items() if member.endswith('/BUILD-INFO.json')]
                require(len(info) == 1 and info[0]['phase'] == 'web-core' and not info[0]['production_ready']
                        and info[0]['source_digest'] == report['source_digest'], name + ': incorrect package identity')
                for suffix in ('XLWebEngineUI_static.lib' if windows else 'libXLWebEngineUI_static.a',
                               'XLWebEngineUI.dll' if windows else 'libXLWebEngineUI.so.0.1.0'):
                    libraries = [content for member, content in files.items() if member.endswith('/' + suffix)]
                    require(len(libraries) == 1 and packed_bytes in libraries[0], name + ': packaged library missing embedded ICU')
                require(any(member.endswith('/docs/views.md') for member in files), name + ': view contract missing')
                require(any(member.endswith('/docs/web-core.md') for member in files), name + ': document contract missing')
            result['packages'].append({'path': str(path.relative_to(BUILD)), 'sha256': package['sha256'], 'symbols': symbols})
        result['targets'][name] = details
    require(set(result['targets']) == {'windows-x64', 'linux-x64-musl'} and len(result['packages']) == 4, 'incomplete official delivery')
    output = Path(output) if output else report_path.parent / 'sdk-audit.json'
    write_json(output, result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.report, args.output)
    print(json.dumps({'state': result['state'], 'source_digest': result['source_digest'], 'packages': len(result['packages'])}))
