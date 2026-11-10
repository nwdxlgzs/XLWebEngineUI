#!/usr/bin/env python3
"""Build a private timing DLL from copied source and existing Debug objects.

Never modifies the normal work tree, original objects, or production sources.
"""
import argparse
import ctypes
import datetime
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

sys.dont_write_bytecode=True
from dependency_support import ROOT,BUILD
import artifacts

LABELS=['view.render','document.layout','document.styles','document.has_text','layout.block',
        'accessibility.build','text.shape','text.cache_hit','text.cache_miss','style.computed',
        'raster.paint','gpu.finish','raster.readback','desktop.present','text.paint',
        'paint.box','paint.clip','accessibility.publish']
PATCHES={
 'src/runtime/view_api.cpp':[(r'xl_status xl_view_render\([^\{]+\{','view.render'),
                            (r'void publish_accessibility\([^\{]+\{','accessibility.publish')],
 'src/document/document.cpp':[(r'DocumentFrame Document::layout\([^\{]+\{','document.layout'),
                             (r'void Document::Impl::refresh_styles\([^\{]+\{','document.styles'),
                             (r'bool Document::has_text\([^\{]+\{','document.has_text')],
 'src/layout/block_layout.cpp':[(r'DocumentFrame block_layout\([^\{]+\{','layout.block')],
 'src/accessibility/semantics.cpp':[(r'void build_accessibility\([^\{]+\{','accessibility.build')],
 'src/text/paragraph.cpp':[(r'std::shared_ptr<TextParagraph> TextEngine::shape\([^\{]+\{','text.shape'),
                           (r'void TextParagraph::paint\([^\{]+\{','text.paint')],
 'src/style/computed.cpp':[(r'BoxStyle box_style\([^\{]+\{','style.computed')],
 'src/graphics/raster_view.cpp':[(r'Damage RasterView::paint\([^\{]+\{','raster.paint'),
                               (r'bool RasterView::read_region\([^\{]+\{','raster.readback')],
 'src/graphics/gpu_device.cpp':[(r'bool GpuDevice::finish\([^\{]+\{','gpu.finish')],
 'src/platform/desktop.cpp':[(r'bool DesktopWindow::present\([^\{]+\{','desktop.present')],
 'src/graphics/box_paint.cpp':[(r'void paint_box\([^\{]+\{','paint.box'),
                             (r'void apply_clip\([^\{]+\{','paint.clip')],
}


def argv(command):
    shell=ctypes.WinDLL('shell32');kernel=ctypes.WinDLL('kernel32')
    shell.CommandLineToArgvW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype=ctypes.POINTER(ctypes.c_wchar_p)
    kernel.LocalFree.argtypes=[ctypes.c_void_p];kernel.LocalFree.restype=ctypes.c_void_p
    count=ctypes.c_int();ptr=shell.CommandLineToArgvW(command,ctypes.byref(count))
    try:return [ptr[i] for i in range(count.value)]
    finally:kernel.LocalFree(ptr)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();work=args.work.resolve()
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    out=args.output or BUILD/'reports/render-instrumentation'/stamp
    from ci import windows_environment
    env=windows_environment()
    report=dict(schema=1,state='running',work=str(work),sources={},labels=LABELS,
                source_dll_sha256=hashlib.sha256((work/'XLWebEngineUI.dll').read_bytes()).hexdigest(),
                note='copied source with RAII timers; original Debug compile flags, inputs and object graph; no product source modifications')
    with artifacts.registered_directory(BUILD,out,'reports/render-instrumentation/windows-x64'):
        scratch=out/'scratch';scratch.mkdir()
        header=scratch/'xl_probe.h'
        header.write_text('''#pragma once
#include <atomic>
#include <chrono>
namespace xl_probe {
extern std::atomic<unsigned long long> ns[64], counts[64];
struct Scope {
 unsigned id; std::chrono::steady_clock::time_point start;
 explicit Scope(unsigned value):id(value),start(std::chrono::steady_clock::now()){}
 ~Scope(){ns[id].fetch_add(static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()),std::memory_order_relaxed);counts[id].fetch_add(1,std::memory_order_relaxed);}
};
inline void count(unsigned id){counts[id].fetch_add(1,std::memory_order_relaxed);}
}
''',encoding='utf-8')
        support=scratch/'xl_probe.cpp'
        support.write_text('#include "xl_probe.h"\nnamespace xl_probe { std::atomic<unsigned long long> ns[64]{},counts[64]{}; }\n'
                           'static const char* labels[] = {'+','.join(json.dumps(n) for n in LABELS)+'};\n'
                           'extern "C" __declspec(dllexport) void xl_perf_reset(){for(unsigned i=0;i<64;++i){xl_probe::ns[i]=0;xl_probe::counts[i]=0;}}\n'
                           'extern "C" __declspec(dllexport) const char* xl_perf_name(unsigned i){return i<sizeof(labels)/sizeof(labels[0])?labels[i]:nullptr;}\n'
                           'extern "C" __declspec(dllexport) unsigned long long xl_perf_ns(unsigned i){return i<64?xl_probe::ns[i].load():0;}\n'
                           'extern "C" __declspec(dllexport) unsigned long long xl_perf_count(unsigned i){return i<64?xl_probe::counts[i].load():0;}\n',encoding='utf-8')
        commands=json.loads((work/'compile_commands.json').read_text('utf-8'))
        replacements={};compiler=None
        log=(out/'build.log').open('w',encoding='utf-8')
        try:
            for name,patches in PATCHES.items():
                original=ROOT/name;text=original.read_text('utf-8');report['sources'][name]=hashlib.sha256(original.read_bytes()).hexdigest()
                for pattern,label in patches:
                    text,count=re.subn(pattern,lambda m:m.group(0)+'\n xl_probe::Scope xl_probe_scope('+str(LABELS.index(label))+');',text,count=1)
                    if count!=1:raise RuntimeError('timing anchor missing: '+name+' '+label)
                if name.endswith('paragraph.cpp'):
                    before='if (auto paragraph = entry.paragraph.lock()) return paragraph;'
                    if text.count(before)!=1:raise RuntimeError('cache hit anchor changed')
                    text=text.replace(before,'if (auto paragraph = entry.paragraph.lock()) {xl_probe::count(7);return paragraph;}\n    xl_probe::count(8);')
                copied=scratch/(original.parent.name+'_'+original.name)
                copied.write_text('#include "xl_probe.h"\n'+text,encoding='utf-8')
                row=next(r for r in commands if Path(r['file']).resolve()==original.resolve() and 'xl_objects.dir' in r['command'])
                command=argv(row['command']);compiler=command[0];destination=scratch/(copied.stem+'.obj')
                command=[a for a in command if not a.startswith(('/Fo','/Fd'))]
                command[-1]=str(copied);command.insert(1,'/I'+str(original.parent));command.insert(1,'/Fo'+str(destination))
                subprocess.run(command,cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
                replacements[row['output'].replace('\\','/')]=str(destination)
                print('Instrumented '+name,flush=True)
            support_object=scratch/'xl_probe.obj'
            subprocess.run([compiler,'/nologo','/c','/std:c++20','/MTd','/Z7','/Od','/EHsc',str(support),'/Fo'+str(support_object)],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
            ninja=(work/'build.ninja').read_text('utf-8')
            line=next(l for l in ninja.splitlines() if l.startswith('build XLWebEngineUI.dll '))
            variables=ninja[ninja.index(line)+len(line):].split('\n\n')[0]
            inputs=line.split(': ',1)[1].split(' |',1)[0].split()[1:]
            objects=[replacements.get(p.replace('\\','/'),str(work/p)) for p in inputs]
            libraries=argv(re.search(r'^  LINK_LIBRARIES = (.+)$',variables,re.M)[1])
            libraries=[str(work/p) if '\\' in p and not Path(p).is_absolute() else p for p in libraries]
            linker=Path(compiler).with_name('lld-link.exe');dll=scratch/'XLWebEngineUI-probe.dll'
            arguments=['/nologo','/dll','/machine:x64','/debug','/DEBUG:FULL','/version:0.1',
                       '/out:'+str(dll),'/implib:'+str(scratch/'probe.lib'),'/pdb:'+str(scratch/'probe.pdb')]+objects+[str(support_object)]+libraries
            response=scratch/'link.rsp';response.write_text(subprocess.list2cmdline(arguments),encoding='utf-8')
            subprocess.run([str(linker),'@'+str(response)],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
            for name,digest in report['sources'].items():
                if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('source changed during probe build: '+name)
            report.update(state='passed',probe_dll=str(dll),probe_dll_sha256=hashlib.sha256(dll.read_bytes()).hexdigest())
        except BaseException as error:
            report.update(state='failed',error=repr(error));raise
        finally:
            log.close();(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(out/'report.json')


if __name__=='__main__':main()
