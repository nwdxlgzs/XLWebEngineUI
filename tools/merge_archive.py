#!/usr/bin/env python3
"""Merge SDK private archives and isolate their private C/C++ symbols."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import sys
import tempfile
sys.dont_write_bytecode = True


def private_symbol(name, kind):
    if kind in ('A', 'a') or name in ('__ImageBase', '_tls_used', '_tls_index', '__security_cookie', '_fltused', '__fltused',
                                    '_Avx2WmemEnabled', '_Avx2WmemEnabledWeakValue'):
        return False
    if name.startswith(('__cxa_', '__gxx_', '_Unwind_', '__std_', 'DW.ref.__gxx_', '.refptr.__')):
        return False
    if re.match(r'^_Z(?:nw|na|dl|da)', name) or name.startswith(('??2@', '??3@', '??_U@', '??_V@')):
        return False
    # Platform ATL support library calls this SDK inline helper by its ABI name.
    if name.startswith('?_AtlInitializeCriticalSectionEx@ATL@@'):
        return False
    standard = '@std@@' in name or re.search(r'_Z[A-Z]*N?K?St', name) or '__cxxabiv1' in name
    owned_type = re.search(r'Sk[A-Z]|Gr[A-Z]|skia|skgpu|sktext|skvx|skvm|SkSL|icu_\d+_xl|hb_|SDL_|spvtools|spv_|spv[A-Z]|spirv_cross|D3D12MA|Vma|angle|marl|llvm|clang|hlsl|Dxc|@(?:egl|gl|rx|sh|sw|rr|vk|Ice|spv|dawn|tint|wgpu|absl)@@|N[0-9]+(?:egl|gl|rx|sh|sw|rr|vk|Ice|spv|dawn|tint|wgpu|absl)', name)
    # Keep standard-library ABI/RTTI compatible with the C++ runtime.
    # Templates containing private engine types belong to this SDK image.
    return not standard or bool(owned_type)


def rewrite_coff_directives(data, names):
    """Keep COFF linker roots/aliases consistent with renamed symbol tables."""
    if len(data) < 20 or not names:
        return data
    machine, sections = struct.unpack_from('<HH', data)
    if machine in (0x8664, 0x14c, 0xaa64):
        table = 20 + struct.unpack_from('<H', data, 16)[0]
    elif data[:4] == b'\0\0\xff\xff' and len(data) >= 56 and struct.unpack_from('<H', data, 4)[0] >= 2:
        sections = struct.unpack_from('<I', data, 44)[0]
        table = 56
    else:
        return data
    if table + 40 * sections > len(data):
        return data
    for index in range(sections):
        header = table + 40 * index
        if data[header:header + 8] != b'.drectve':
            continue
        size, offset = struct.unpack_from('<II', data, header + 16)
        if offset + size > len(data):
            raise RuntimeError('invalid COFF directive section')
        original = data[offset:offset + size]

        def replace(match):
            value = match[2]
            quoted = value.startswith(b'"')
            symbol = value[1:-1] if quoted else value
            if b'alternatename' in match[1].lower():
                parts = symbol.split(b'=')
                value = b'='.join(names.get(part, part) for part in parts)
            else:
                parts = symbol.split(b',', 1)
                value = names.get(parts[0], parts[0])
                if len(parts) == 2:
                    value += b',' + parts[1]
            return match[1] + (b'"' + value + b'"' if quoted else value)

        updated = re.sub(rb'(?i)((?:/|-)(?:include|export|alternatename):)("[^"\r\n]+"|[^\s\x00]+)', replace, original)
        if updated == original:
            return data
        # Raw section offsets need not follow symbol/string tables in COFF.
        # Appending preserves every other section, relocation, and debug record.
        result = bytearray(data)
        struct.pack_into('<II', result, header + 16, len(updated), len(result))
        result.extend(updated)
        return bytes(result)
    return data


def archive_members(archive):
    """Read GNU, Microsoft and BSD regular archives, excluding their indexes."""
    names = b''
    with archive.open('rb') as stream:
        if stream.read(8) != b'!<arch>\n':
            raise RuntimeError('expected a regular LLVM archive')
        while True:
            header = stream.read(60)
            if not header: break
            if len(header) != 60 or header[58:] != b'`\n':
                raise RuntimeError('truncated or invalid archive header')
            try:
                length = int(header[48:58])
            except ValueError as error:
                raise RuntimeError('invalid archive member size') from error
            if length < 0:
                raise RuntimeError('negative archive member size')
            position = stream.tell()
            name = header[:16].decode('ascii').strip()
            if name == '//':
                names = stream.read(length)
            elif name not in ('/', '/SYM64/'):
                name_length = 0
                if name.startswith('/') and name[1:].isdigit():
                    name = names[int(name[1:]):].split(b'\0', 1)[0].split(b'\n', 1)[0].rstrip(b'/').decode('utf-8')
                elif name.startswith('#1/'):
                    name_length = int(name[3:])
                    name = stream.read(name_length).rstrip(b'\0').decode('utf-8')
                else:
                    name = name.rstrip('/')
                if name_length > length:
                    raise RuntimeError('invalid BSD archive name length')
                body = stream.read(length - name_length)
                if len(body) != length - name_length:
                    raise RuntimeError('truncated archive member')
                if name not in ('__.SYMDEF', '__.SYMDEF SORTED', '__.SYMDEF_64', '__.SYMDEF_64 SORTED'):
                    yield name, body
            if position + length > archive.stat().st_size:
                raise RuntimeError('truncated archive member')
            stream.seek(position + length + length % 2)


def windows_resources(archive):
    """Recognize native .res members, which have no object symbol relocations."""
    signature = bytes.fromhex('0000000020000000ffff0000ffff0000')
    return [(name, body) for name, body in archive_members(archive) if body.startswith(signature)]


def merge_unique(archives, destination, symbol_names=None):
    """Deduplicate exact payloads, never similarly named or similarly defined code."""
    seen = set()
    report = dict(schema=1, members=0, removed_members=0, removed_payload_bytes=0, inputs=[])
    with destination.open('wb') as output:
        output.write(b'!<arch>\n')
        for archive in archives:
            item = dict(path=str(archive), members=0, removed_members=0, removed_payload_bytes=0)
            for name, body in archive_members(archive):
                item['members'] += 1
                key = (len(body), hashlib.sha256(body).digest())
                if key in seen:
                    item['removed_members'] += 1
                    item['removed_payload_bytes'] += len(body)
                    continue
                seen.add(key)
                body = rewrite_coff_directives(body, symbol_names)
                # BSD extended names preserve duplicate basenames with different
                # contents and work with LLVM on both COFF and ELF targets.
                encoded = name.encode('utf-8')
                # BSD extended names include NUL padding so the object begins
                # on an eight-byte boundary (LLVM ArchiveWriter uses this too).
                # An odd name length otherwise makes LLVM reject ELF members.
                encoded += b'\0' * (-(output.tell() + 60 + len(encoded)) % 8)
                size = len(encoded) + len(body)
                header = '{:<16}{:<12}{:<6}{:<6}{:<8}{:<10}`\n'.format(
                    '#1/' + str(len(encoded)), 0, 0, 0, '100644', size).encode('ascii')
                if len(header) != 60:
                    raise RuntimeError('archive member header overflow')
                output.write(header)
                output.write(encoded)
                output.write(body)
                if size % 2:
                    output.write(b'\n')
                report['members'] += 1
            report['inputs'].append(item)
            report['removed_members'] += item['removed_members']
            report['removed_payload_bytes'] += item['removed_payload_bytes']
    return report


def merge(output, libraries, isolate, ar, nm, objcopy, symbol_prefix='xl_private_', require_symbol='ZSTD_decompress'):
    symbols = subprocess.check_output([str(nm), "--defined-only", "--extern-only", "--format=posix"] + [str(path) for path in isolate], text=True)
    names = set()
    for line in symbols.splitlines():
        parts = line.split()
        if len(parts) >= 2 and re.fullmatch(r"[A-Za-z]", parts[1]) and private_symbol(parts[0], parts[1]):
            names.add(parts[0])
    if require_symbol and require_symbol not in names:
        raise RuntimeError('private archive has no required implementation: ' + require_symbol)
    temporary = output.with_suffix(output.suffix + ".merged")
    report = merge_unique([output] + libraries, temporary,
                          {name.encode(): (symbol_prefix + name).encode() for name in names})
    subprocess.run([str(ar), 's', str(temporary)], check=True)
    mapping = output.with_suffix(output.suffix + ".symbols")
    mapping.write_text("".join(name + ' ' + symbol_prefix + name + "\n" for name in sorted(names)), encoding="utf-8")
    isolated = output.with_suffix(output.suffix + ".isolated")
    resources = windows_resources(temporary)
    if resources:
        subprocess.run([str(ar), 'd', str(temporary)] + [name for name, _ in resources], check=True)
    subprocess.run([str(objcopy), "--redefine-syms=" + str(mapping), str(temporary), str(isolated)], check=True)
    if resources:
        with tempfile.TemporaryDirectory(prefix='xl-resource-members-', dir=output.parent) as directory:
            paths = []
            for index, (_, content) in enumerate(resources):
                path = Path(directory) / ('version-' + str(index) + '.res')
                path.write_bytes(content)
                paths.append(str(path))
            subprocess.run([str(ar), 'r', str(isolated)] + paths, check=True)
    isolated.replace(output)
    temporary.unlink()
    report['output_bytes'] = output.stat().st_size
    report['symbol_prefix'] = symbol_prefix
    output.with_suffix(output.suffix + '.merge.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Private archive: removed {} exact duplicate members ({} payload bytes)'.format(
        report['removed_members'], report['removed_payload_bytes']), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--libraries", nargs="+", type=Path, required=True)
    parser.add_argument("--isolate", nargs='+', type=Path, required=True)
    for name in ("ar", "nm", "objcopy"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    merge(args.output, args.libraries, args.isolate, args.ar, args.nm, args.objcopy)


if __name__ == "__main__":
    main()
