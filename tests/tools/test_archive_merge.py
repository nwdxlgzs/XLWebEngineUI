from pathlib import Path
import sys
import tempfile
import struct
import unittest
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from merge_archive import archive_members, merge_unique, windows_resources, private_symbol, rewrite_coff_directives


def member(name, body):
    header = '{:<16}{:<12}{:<6}{:<6}{:<8}{:<10}`\n'.format(name, 0, 0, 0, 0, len(body)).encode('ascii')
    assert len(header) == 60
    return header + body + (b'\n' if len(body) % 2 else b'')


class ResourceMemberTests(unittest.TestCase):
    def test_dawn_private_template_ownership_keeps_standard_host_abi(self):
        # A real defined COFF name from Dawn's compiled Instance.cpp object.
        self.assertTrue(private_symbol('??$?0$$V@?$_Compressed_pair@U?$default_delete@VBackendConnection@native@dawn@@@std@@PEAVBackendConnection@native@dawn@@$00@std@@QEAA@U_Zero_then_variadic_args_t@1@@Z', 'T'))
        self.assertTrue(private_symbol('_ZNSt10unique_ptrIN4dawn6native17BackendConnectionESt14default_deleteIS2_EED2Ev', 'W'))
        self.assertFalse(private_symbol('??0exception@std@@QEAA@XZ', 'T'))
        # Actual Dawn + static SwiftShader link failure: a private global
        # SPIR-V function returning std::string is not part of the host STL ABI.
        self.assertTrue(private_symbol('?spvTargetEnvList@@YA?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@HH@Z', 'T'))
    def test_real_elf_with_odd_member_names_can_be_indexed_by_llvm(self):
        from bootstrap import llvm_bin
        extension = '.exe' if os.name == 'nt' else ''
        compiler, ar, nm = [llvm_bin() / (name + extension) for name in ('clang', 'llvm-ar', 'llvm-nm')]
        if not all(path.is_file() for path in (compiler, ar, nm)):
            self.skipTest('prepared LLVM tools unavailable')
        directory = ROOT / 'build/test-tools'
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as temporary:
            root = Path(temporary)
            source, obj, original, merged = [root / name for name in ('sample.c', 'sample.o', 'original.a', 'merged.a')]
            source.write_text('int archive_alignment_probe(void) { return 17; }\n')
            subprocess.run([str(compiler), '--target=x86_64-linux-gnu', '-c', str(source), '-o', str(obj)], check=True, capture_output=True)
            body = obj.read_bytes()
            original.write_bytes(b'!<arch>\n' + member('Blitter.cpp.o/', body))
            merge_unique([original], merged)
            subprocess.run([str(ar), 's', str(merged)], check=True, capture_output=True)
            output = subprocess.check_output([str(nm), '--defined-only', str(merged)], text=True)
            self.assertIn('archive_alignment_probe', output)
            self.assertEqual(list(archive_members(merged)), [('Blitter.cpp.o', body)])

    def test_coff_linker_roots_aliases_and_exports_follow_isolation(self):
        directives=b' /DEFAULTLIB:libcmt /include:"?dump@llvm@@QEAAXXZ" /alternatename:old=target /EXPORT:old,DATA /include:host '
        names={b'?dump@llvm@@QEAAXXZ':b'xl_?dump@llvm@@QEAAXXZ',b'old':b'xl_old',b'target':b'xl_target'}
        for big in (False,True):
            header=bytearray(56 if big else 20)
            if big:
                struct.pack_into('<HHH',header,0,0,0xffff,2)
                struct.pack_into('<I',header,44,1)
            else:
                struct.pack_into('<HH',header,0,0x8664,1)
            section=bytearray(40);section[:8]=b'.drectve'
            struct.pack_into('<II',section,16,len(directives),len(header)+40)
            original=bytes(header+section)+directives+b'unrelated-debug-and-string-data'
            updated=rewrite_coff_directives(original,names)
            size,offset=struct.unpack_from('<II',updated,len(header)+16)
            actual=updated[offset:offset+size]
            self.assertIn(b'/include:"xl_?dump@llvm@@QEAAXXZ"',actual)
            self.assertIn(b'/alternatename:xl_old=xl_target',actual)
            self.assertIn(b'/EXPORT:xl_old,DATA',actual)
            self.assertIn(b'/DEFAULTLIB:libcmt',actual)
            self.assertIn(b'/include:host',actual)
            self.assertEqual(updated[len(header)+40:len(original)],original[len(header)+40:])
            self.assertEqual(rewrite_coff_directives(updated,names),updated)
        self.assertFalse(private_symbol('?_AtlInitializeCriticalSectionEx@ATL@@YAHPEAU_RTL_CRITICAL_SECTION@@KK@Z','T'))

    def test_invalid_coff_directive_payload_is_rejected(self):
        original=bytearray(60);struct.pack_into('<HH',original,0,0x8664,1)
        original[20:28]=b'.drectve';struct.pack_into('<II',original,36,100,60)
        with self.assertRaises(RuntimeError):rewrite_coff_directives(bytes(original),{b'x':b'xl_x'})
    def test_engine_cpp_symbols_are_private_and_standard_exception_rtti_is_preserved(self):
        for symbol in ('SDL_Init', 'g_debuglevel', '_ZN8SkCanvas8drawRectERK6SkRectRK7SkPaint',
                       '?drawRect@SkCanvas@@QEAAXAEBUSkRect@@AEBVSkPaint@@@Z',
                       '_ZNSt3__110unique_ptrIN4skia10textlayout9ParagraphENS_14default_deleteIS3_EEED2Ev',
                       '_ZNSt3__16vectorIPN2gl7TextureENS_9allocatorIS3_EEED2Ev',
                       '_ZNSt3__110shared_ptrIN2rx9ContextGLEED2Ev',
                       '??1?$vector@PEAVTexture@gl@@V?$allocator@PEAVTexture@gl@@@std@@@std@@QEAA@XZ',
                       '??1?$vector@PEAVTSymbol@sh@@V?$allocator@PEAVTSymbol@sh@@@std@@@std@@QEAA@XZ',
                       '??1?$unique_ptr@VAllocation@D3D12MA@@U?$default_delete@VAllocation@D3D12MA@@@std@@@std@@QEAA@XZ',
                       '??1?$unique_ptr@VContext@sw@@U?$default_delete@VContext@sw@@@std@@@std@@QEAA@XZ',
                       '??1?$unique_ptr@VParagraph@textlayout@skia@@U?$default_delete@VParagraph@textlayout@skia@@@std@@@std@@QEAA@XZ'):
            self.assertTrue(private_symbol(symbol, 'T'), symbol)
        for symbol in ('_ZTISt9bad_alloc', '_ZTINSt3__112length_errorE', '_ZGVNSt3__112basic_stringE', '??_R0?AVbad_alloc@std@@@8', '_Znwm'):
            self.assertFalse(private_symbol(symbol, 'T'), symbol)

    def test_microsoft_and_gnu_long_names_preserve_resource_bytes(self):
        directory = ROOT / 'build/test-tools'
        directory.mkdir(parents=True, exist_ok=True)
        resource = bytes.fromhex('0000000020000000ffff0000ffff0000') + b'original-version-data'
        filename = b'CMakeFiles/font.dir/ftver.rc.res'
        for terminator in (b'\0', b'/\n'):
            with tempfile.TemporaryDirectory(dir=directory) as temporary:
                path = Path(temporary) / 'mixed.lib'
                names = filename + terminator + b'normal.obj' + terminator
                path.write_bytes(b'!<arch>\n' + member('//', names) + member('/0', resource)
                                 + member('/' + str(len(filename) + len(terminator)), b'\x64\x86ordinary-code'))
                self.assertEqual(windows_resources(path), [(filename.decode(), resource)])

    def test_truncated_archive_is_rejected(self):
        directory = ROOT / 'build/test-tools'
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as temporary:
            path = Path(temporary) / 'partial.lib'
            path.write_bytes(b'!<arch>\npartial-header')
            with self.assertRaises(RuntimeError):
                windows_resources(path)

    def test_exact_payload_dedup_keeps_distinct_same_name_objects_and_resources(self):
        directory = ROOT / 'build/test-tools'
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as temporary:
            a, b, merged = [Path(temporary) / name for name in ('a.lib', 'b.lib', 'merged.lib')]
            resource = bytes.fromhex('0000000020000000ffff0000ffff0000') + b'version'
            a.write_bytes(b'!<arch>\n' + member('same.obj/', b'code-a') + member('v.res/', resource))
            b.write_bytes(b'!<arch>\n' + member('alias.obj/', b'code-a') + member('same.obj/', b'code-b') + member('v.res/', resource))
            report = merge_unique([a, b], merged)
            self.assertEqual(list(archive_members(merged)), [('same.obj', b'code-a'), ('v.res', resource), ('same.obj', b'code-b')])
            self.assertEqual(report['removed_members'], 2)
            self.assertEqual(report['removed_payload_bytes'], 6 + len(resource))
            self.assertEqual(windows_resources(merged), [('v.res', resource)])

    def test_truncated_payload_is_rejected(self):
        directory = ROOT / 'build/test-tools'
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as temporary:
            path = Path(temporary) / 'bad.lib'
            path.write_bytes((b'!<arch>\n' + member('x.obj/', b'12345678'))[:-4])
            with self.assertRaises(RuntimeError):
                list(archive_members(path))


if __name__ == '__main__':
    unittest.main()
