#!/usr/bin/env python3
"""Measure retained SDK packages and their object sections without rebuilding."""
import argparse
import collections
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import uuid
import zipfile
import zlib

sys.dont_write_bytecode = True
import artifacts
from dependency_support import BUILD, ROOT, sha256
from merge_archive import windows_resources


def ar_members(path, payload=True):
    """Read regular GNU/COFF/BSD archives, retaining exact member lengths."""
    with path.open('rb') as stream:
        if stream.read(8) != b'!<arch>\n':
            raise ValueError('not a regular archive: ' + str(path))
        names = b''
        while True:
            header = stream.read(60)
            if not header:
                break
            if len(header) != 60 or header[58:] != b'`\n':
                raise ValueError('bad archive member: ' + str(path))
            length = int(header[48:58])
            position = stream.tell()
            name = header[:16].decode('ascii').strip()
            if name == '//':
                names = stream.read(length)
            elif name not in ('/', '/SYM64/'):
                prefix = 0
                if name.startswith('/') and name[1:].isdigit():
                    name = names[int(name[1:]):].split(b'\0', 1)[0].split(b'\n', 1)[0].rstrip(b'/').decode('utf-8')
                elif name.startswith('#1/'):
                    prefix = int(name[3:])
                    name = stream.read(prefix).rstrip(b'\0').decode('utf-8')
                else:
                    name = name.rstrip('/')
                size = length - prefix
                if name not in ('__.SYMDEF', '__.SYMDEF SORTED', '__.SYMDEF_64', '__.SYMDEF_64 SORTED'):
                    yield name.replace('\\', '/'), size, stream.read(size) if payload else None
            stream.seek(position + length + length % 2)


def section_kind(name, executable=False):
    if name.startswith(('.debug', '.zdebug')) or name in ('.gdb_index',):
        return 'debug'
    if executable or name.startswith(('.text', '.init', '.fini', '.plt')):
        return 'code'
    if name in ('.symtab', '.strtab', '.shstrtab'):
        return 'symbols'
    if name.startswith(('.rel', '.rela')):
        return 'relocations'
    return 'data_and_other_sections'


def object_sections(data):
    """Count file-backed bytes, never virtual .bss size or address space."""
    sections = []
    if data[:4] == b'\x7fELF':
        if data[4:6] != b'\x02\x01':
            raise ValueError('expected little endian ELF64')
        table = struct.unpack_from('<Q', data, 40)[0]
        entry, count, names_index = struct.unpack_from('<HHH', data, 58)
        if not count:
            count = struct.unpack_from('<Q', data, table + 32)[0]
        if names_index == 0xffff:
            names_index = struct.unpack_from('<I', data, table + 40)[0]
        rows = [struct.unpack_from('<IIQQQQIIQQ', data, table + entry * i) for i in range(count)]
        names_row = rows[names_index]
        names = data[names_row[4]:names_row[4] + names_row[5]]
        for row in rows:
            name = names[row[0]:].split(b'\0', 1)[0].decode('utf-8')
            if row[1] != 8 and row[5]:  # SHT_NOBITS occupies no file bytes.
                sections.append((name, row[5], section_kind(name, bool(row[2] & 4))))
    elif data[:2] == b'MZ':
        pe = struct.unpack_from('<I', data, 60)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optional = struct.unpack_from('<H', data, pe + 20)[0]
        for i in range(count):
            row = pe + 24 + optional + 40 * i
            name = data[row:row + 8].split(b'\0')[0].decode('ascii')
            size, offset = struct.unpack_from('<II', data, row + 16)
            flags = struct.unpack_from('<I', data, row + 36)[0]
            if size and offset:
                sections.append((name, size, section_kind(name, bool(flags & 0x20000000))))
    elif len(data) >= 20 and (data[:2] in (b'\x64\x86', b'\x4c\x01') or
                             (data[:4] == b'\x00\x00\xff\xff' and len(data) >= 56 and struct.unpack_from('<H', data, 4)[0] >= 2)):
        big = data[:4] == b'\x00\x00\xff\xff' and len(data) >= 56 and struct.unpack_from('<H', data, 4)[0] >= 2
        if big:
            count, symbols, symbol_count = struct.unpack_from('<III', data, 44)
            table, symbol_size = 56, 20
        else:
            count = struct.unpack_from('<H', data, 2)[0]
            symbols, symbol_count = struct.unpack_from('<II', data, 8)
            table = 20 + struct.unpack_from('<H', data, 16)[0]
            symbol_size = 18
        string_start = symbols + symbol_count * symbol_size
        strings = data[string_start:] if symbols else b''
        for i in range(count):
            row = table + i * 40
            name = data[row:row + 8].split(b'\0')[0].decode('ascii')
            if name.startswith('/'):
                name = strings[int(name[1:]):].split(b'\0', 1)[0].decode('utf-8')
            size, offset, relocations = struct.unpack_from('<III', data, row + 16)
            relocation_count = struct.unpack_from('<H', data, row + 32)[0]
            flags = struct.unpack_from('<I', data, row + 36)[0]
            if size and offset:
                sections.append((name, size, section_kind(name, bool(flags & 0x20000000))))
            if relocations and relocation_count:
                real_count = struct.unpack_from('<I', data, relocations)[0] if flags & 0x1000000 else relocation_count
                sections.append(('[COFF relocations]', real_count * 10, 'relocations'))
        if symbols:
            sections.append(('[COFF symbols]', symbol_count * symbol_size + len(strings), 'symbols'))
    return sections


def stable_section_digest(data):
    """Identify objects across symbol renaming using unchanged section bytes.

    Skip symbol/string tables and COFF linker directives, which the production
    isolator rewrites. Used only to disambiguate equal member names.
    """
    digest = hashlib.sha256()
    if data[:4] == b'\x7fELF':
        table = struct.unpack_from('<Q', data, 40)[0]
        entry, count, names_index = struct.unpack_from('<HHH', data, 58)
        rows = [struct.unpack_from('<IIQQQQIIQQ', data, table + entry * i) for i in range(count)]
        names_row = rows[names_index]
        names = data[names_row[4]:names_row[4] + names_row[5]]
        for row in rows:
            name = names[row[0]:].split(b'\0', 1)[0]
            if row[1] in (0, 8) or name in (b'.symtab', b'.strtab', b'.shstrtab'):
                continue
            digest.update(name);digest.update(data[row[4]:row[4] + row[5]])
    elif len(data) >= 20 and data[:2] in (b'\x64\x86', b'\x4c\x01'):
        count = struct.unpack_from('<H', data, 2)[0]
        table = 20 + struct.unpack_from('<H', data, 16)[0]
        for i in range(count):
            row = table + 40 * i
            name = data[row:row + 8].split(b'\0', 1)[0]
            size, offset = struct.unpack_from('<II', data, row + 16)
            if name != b'.drectve' and size and offset:
                digest.update(name);digest.update(data[offset:offset + size])
    elif data[:4] == b'\0\0\xff\xff' and len(data) >= 56:
        count = struct.unpack_from('<I', data, 44)[0]
        for i in range(count):
            row = 56 + 40 * i
            name = data[row:row + 8].split(b'\0', 1)[0]
            size, offset = struct.unpack_from('<II', data, row + 16)
            if name != b'.drectve' and size and offset:
                digest.update(name);digest.update(data[offset:offset + size])
    else:
        digest.update(data)
    return digest.digest()


def analyze_archive(path, owners=None, duplicates=False):
    total = collections.Counter()
    groups = collections.defaultdict(collections.Counter)
    digests = {}
    duplicate_groups = []
    members = []
    engine_names = {p.name + '.o' for p in (ROOT / 'src').rglob('*.cpp')} | {'catalog.cpp.o', 'private_sdk_inputs.cpp.o'}
    pathops_names = set()
    for manifest in (BUILD / 'generated/sdk-inputs').glob('*/Debug/inputs.json'):
        record = json.loads(manifest.read_text('utf-8'))
        listing = BUILD / record['graphics']['skia']['source_overlay'] / 'gn/pathops.gni'
        if listing.is_file():
            for value in re.findall(r'pathops/([^"\s]+\.cpp)', listing.read_text('utf-8')):
                pathops_names.add(Path(value).name + '.o')
    for name, size, data in ar_members(path):
        categories = collections.Counter()
        for _, length, kind in object_sections(data):
            categories[kind] += length
        categories['headers_and_other'] = size - sum(categories.values())
        if categories['headers_and_other'] < 0:
            raise ValueError('overlapping section accounting: ' + name)
        total.update(categories)
        total['member_payload_bytes'] += size
        total['members'] += 1
        owner = (owners or {}).get(name)
        if owner and len(owner) > 1:
            owner = owners.get((name, stable_section_digest(data)), owner)
        group = ('engine' if name.startswith('CMakeFiles/xl_static.dir/') or not owner and name in engine_names else
                 'zstd-runtime' if 'xl_zstd_decoder.dir/' in name or not owner and name.endswith('.c.o') else
                 'skia-pathops' if 'xl_skia_pathops.dir/' in name or not owner and name in pathops_names else ' + '.join(sorted(owner or ['unattributed'])))
        groups[group].update(categories)
        groups[group]['member_payload_bytes'] += size
        groups[group]['members'] += 1
        members.append(dict(name=name, bytes=size, group=group, categories=dict(categories)))
        if duplicates:
            digest = hashlib.sha256(data).hexdigest()
            if digest in digests:
                index = digests[digest]
                if not isinstance(index, list):
                    index = [index]
                    digests[digest] = index
                    duplicate_groups.append(index)
                index.append(dict(name=name, bytes=size, group=group))
            else:
                digests[digest] = dict(name=name, bytes=size, group=group)
    return dict(bytes=path.stat().st_size, categories=dict(total),
                archive_structure_bytes=path.stat().st_size - total['member_payload_bytes'],
                groups=dict(groups), largest_members=sorted(members, key=lambda v:v['bytes'], reverse=True)[:25],
                duplicate_extra_bytes=sum(sum(m['bytes'] for m in g[1:]) for g in duplicate_groups),
                duplicate_extra_members=sum(len(g) - 1 for g in duplicate_groups), duplicate_groups=duplicate_groups)


def deflate_bytes(path):
    encoder = zlib.compressobj(6, zlib.DEFLATED, -15)
    size = 0
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            size += len(encoder.compress(block))
    return size + len(encoder.flush())


def dependency_inventory(manifest):
    owners = collections.defaultdict(set)
    rows = []
    for record in manifest['private_archives']:
        path = BUILD / record['path']
        group = record['path'].split('/')[3] if record['path'].startswith('deps/install/') else 'x11-platform'
        label = group + '/' + path.name
        if not path.is_file():
            rows.append(dict(path=record['path'], missing=True))
            continue
        digest = sha256(path)
        if digest != record['sha256']:
            raise ValueError('dependency differs from package manifest: ' + str(path))
        result = analyze_archive(path)
        rows.append(dict(path=record['path'], label=label, sha256=digest, **result))
        for name, _, _ in ar_members(path, payload=False):
            owners[name].add(label)
    ambiguous = {name for name, labels in owners.items() if len(labels) > 1}
    if ambiguous:
        for record in manifest['private_archives']:
            path = BUILD / record['path']
            if not path.is_file():
                continue
            group = record['path'].split('/')[3] if record['path'].startswith('deps/install/') else 'x11-platform'
            label = group + '/' + path.name
            for name, _, data in ar_members(path):
                if name in ambiguous:
                    owners[(name, stable_section_digest(data))].add(label)
    return owners, rows


def windows_pdb_text(pdb, pdbutil, owners):
    modules = subprocess.check_output([str(pdbutil), 'dump', '--modules', str(pdb)], text=True)
    names = {int(i): name.replace('\\', '/') for i, name in re.findall(r'Mod\s+(\d+)\s+\| `([^`]+)`', modules)}
    contributions = subprocess.check_output([str(pdbutil), 'dump', '--section-contribs', str(pdb)], text=True)
    groups = collections.Counter()
    subgroups = collections.Counter()
    for module, size in re.findall(r'SC\[\.text\]\s+\| mod = (\d+), [^\n]*?size = (\d+)', contributions):
        name = names[int(module)]
        owner = owners.get(name, set())
        group = ('engine' if '/xl_objects.dir/' in name else 'zstd-runtime' if '/xl_zstd_decoder.dir/' in name else
                 'skia-pathops' if '/xl_skia_pathops.dir/' in name else
                 'skia' if any(v.startswith('skia/') for v in owner) else
                 next(iter(owner)).split('/')[0] if len(owner) == 1 else 'CRT_system_and_other')
        groups[group] += int(size)
        if group == 'skia':
            subgroup = ('Ganesh' if '/ganesh/' in name else 'SPIR-V' if 'spirv-tools' in name else
                        'SkSL' if '/sksl/' in name else 'other')
            subgroups[subgroup] += int(size)
    return dict(method='PDB .text section contributions; excludes alignment gaps; exact object names matched to input manifests',
                total_bytes=sum(groups.values()), groups=dict(groups), skia_subgroups=dict(subgroups), modules=len(names))


def analyze_package(report_path, scratch, objcopy, strip_experiment=True):
    report = json.loads(report_path.read_text('utf-8'))
    archives = report.get('archives', report.get('packages'))
    sdk = next(item for item in archives if 'symbols' not in Path(item['path']).name)
    package = BUILD / sdk['path']
    digest = sha256(package)
    if digest != sdk['sha256']:
        raise ValueError('package hash mismatch')
    is_zip = package.suffix == '.zip'
    manifest = None
    files = []
    binaries = {}
    with zipfile.ZipFile(package) if is_zip else tarfile.open(package) as archive:
        entries = archive.infolist() if is_zip else archive.getmembers()
        for item in entries:
            name = item.filename if is_zip else item.name
            regular = not item.is_dir() if is_zip else item.isfile()
            if not regular:
                continue
            size = item.file_size if is_zip else item.size
            files.append(dict(path=name, bytes=size, compressed_bytes=item.compress_size if is_zip else None))
            kind = 'static' if name.endswith(('XLWebEngineUI_static.lib', 'libXLWebEngineUI_static.a')) else 'shared' if name.endswith(('XLWebEngineUI.dll', 'libXLWebEngineUI.so.0.1.0')) else 'webtest' if name.endswith(('WebTest.exe', 'WebTest.bin')) else None
            if kind or name.endswith('RESOURCE-BUILD-INFO.json'):
                with archive.open(item) if is_zip else archive.extractfile(item) as source:
                    if kind:
                        destination = scratch / Path(name).name
                        with destination.open('wb') as out:
                            shutil.copyfileobj(source, out, 1024 * 1024)
                        binaries[kind] = destination
                    else:
                        manifest = json.load(source)
    if manifest is None:
        raise ValueError('missing input manifest')
    owners, dependencies = dependency_inventory(manifest)
    pdb_contributions = None
    if is_zip:
        symbols = next(item for item in archives if 'symbols' in Path(item['path']).name)
        symbol_package = BUILD / symbols['path']
        if sha256(symbol_package) != symbols['sha256']:
            raise ValueError('symbols package hash mismatch')
        with zipfile.ZipFile(symbol_package) as symbols_zip:
            pdb_name = next(n for n in symbols_zip.namelist() if n.endswith('/XLWebEngineUI.pdb') or n == 'symbols/XLWebEngineUI.pdb')
            pdb = scratch / 'XLWebEngineUI.pdb'
            with symbols_zip.open(pdb_name) as source, pdb.open('wb') as destination:
                shutil.copyfileobj(source, destination)
        pdb_contributions = windows_pdb_text(pdb, objcopy.with_name('llvm-pdbutil.exe'), owners)
    static = analyze_archive(binaries['static'], owners, duplicates=True)
    stripped = scratch / ('stripped-' + binaries['static'].name)
    resources = windows_resources(binaries['static']) if strip_experiment else []
    strip_input = binaries['static']
    if strip_experiment and resources:
        # COFF archives may contain native .res records; objcopy cannot read
        # these. Keep their bytes exactly as the production merger does.
        strip_input = scratch / ('without-resources-' + binaries['static'].name)
        shutil.copyfile(binaries['static'], strip_input)
        subprocess.run([str(objcopy.with_name('llvm-ar.exe')), 'd', str(strip_input)] + [name for name, _ in resources], check=True)
    if strip_experiment:
        subprocess.run([str(objcopy), '--strip-debug', str(strip_input), str(stripped)], check=True)
    if strip_experiment and resources:
        paths = []
        for name, data in resources:
            path = scratch / Path(name).name
            path.write_bytes(data)
            paths.append(str(path))
        subprocess.run([str(objcopy.with_name('llvm-ar.exe')), 'r', str(stripped)] + paths, check=True)
    if strip_experiment:
        stripped_result = analyze_archive(stripped)
        static['strip_debug_experiment'] = dict(bytes=stripped.stat().st_size, removed_bytes=static['bytes'] - stripped.stat().st_size,
                                              deflate_bytes=deflate_bytes(stripped), categories=stripped_result['categories'])
    else:
        static['strip_debug_experiment'] = None
    binary_sections = {}
    for kind in ('shared', 'webtest'):
        if kind not in binaries:
            continue
        data = binaries[kind].read_bytes()
        sections = object_sections(data)
        binary_sections[kind] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), sections=[dict(name=n, bytes=s, kind=k) for n,s,k in sections])
        if strip_experiment and data[:4] == b'\x7fELF':
            destination = scratch / ('stripped-all-' + binaries[kind].name)
            subprocess.run([str(objcopy), '--strip-unneeded', str(binaries[kind]), str(destination)], check=True)
            binary_sections[kind]['strip_unneeded_experiment_bytes'] = destination.stat().st_size
    return dict(target=report['target'], configuration=manifest['configuration'], source_digest=report['source_digest'],
                package_path=sdk['path'], package_bytes=package.stat().st_size, sha256=digest,
                files=files, unpacked_regular_bytes=sum(item['bytes'] for item in files),
                symbols_archive=next(item for item in archives if 'symbols' in Path(item['path']).name),
                manifest=manifest, dependencies=dependencies, static=static, binaries=binary_sections,
                windows_pdb_text_contributions=pdb_contributions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, action='append', required=True, help='retained stage package report.json')
    parser.add_argument('--skip-strip', action='store_true', help='measure object composition without generating stripped multi-GB copies')
    args = parser.parse_args()
    objcopy = next((BUILD / 'toolchains').glob('clang+llvm-*-pc-windows-msvc/bin/llvm-objcopy.exe'), None)
    if objcopy is None:
        raise RuntimeError('existing Windows LLVM objcopy is required for copy experiments')
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    output = BUILD / 'reports/library-size' / stamp
    with artifacts.registered_directory(BUILD, output, 'reports/library-size/common'):
        result = dict(schema=1, generated_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      methodology='retained package hashes; physical section sizes; exact SHA256 duplicate member payloads; scratch-only stripping',
                      objcopy=str(objcopy.relative_to(ROOT)), targets=[])
        try:
            for report in args.report:
                print('Analyzing ' + str(report), flush=True)
                with tempfile.TemporaryDirectory(prefix='scratch-', dir=output) as temporary:
                    result['targets'].append(analyze_package(report, Path(temporary), objcopy, not args.skip_strip))
                (output / 'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            result['state'] = 'passed'
        except BaseException as error:
            result.update(state='failed', error=str(error))
            raise
        finally:
            (output / 'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    artifacts.collect(BUILD, retain=3, families={'reports/library-size/common'})
    print(output / 'report.json')


if __name__ == '__main__':
    main()
