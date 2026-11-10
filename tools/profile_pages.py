#!/usr/bin/env python3
"""Profile immutable Windows SDK DLLs against unmodified user HTML pages."""
import argparse
import ctypes as C
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time
import uuid
from html.parser import HTMLParser

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
BACKENDS = {'raster': 0, 'auto': 1, 'd3d12': 2, 'angle-d3d11': 3, 'vulkan': 4, 'angle-gl': 5, 'swiftshader': 6, 'opengl': 7}


class ViewOptions(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('struct_size', 'abi_version', 'kind', 'flags')] + [
        ('width', C.c_int32), ('height', C.c_int32), ('background_argb', C.c_uint32),
        ('native_kind', C.c_uint32), ('native_window', C.c_size_t), ('title_utf8', C.c_char_p)]


class ViewStats(C.Structure):
    _fields_ = [('struct_size', C.c_uint32), ('needs_frame', C.c_uint32), ('width', C.c_int32), ('height', C.c_int32),
                ('close_requested', C.c_uint32), ('reserved', C.c_uint32)] + [
                    (n, C.c_uint64) for n in ('raster_frames', 'presented_frames', 'skipped_frames', 'rasterized_pixels', 'uploaded_pixels')]


class DocStats(C.Structure):
    _fields_ = [('struct_size', C.c_uint32), ('reserved', C.c_uint32)] + [
        (n, C.c_uint64) for n in ('revision', 'nodes', 'layout_frames', 'unsupported_layout_nodes')]


class Graphics(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('struct_size', 'requested', 'active', 'attempted_mask', 'failed_mask', 'shader_compiler')] + [
        (n, C.c_uint64) for n in ('generation', 'recoveries')]


class Scroll(C.Structure):
    _fields_ = [('struct_size', C.c_uint32), ('flags', C.c_uint32)] + [(n, C.c_float) for n in (
        'x', 'y', 'client_width', 'client_height', 'scroll_width', 'scroll_height', 'min_x', 'min_y', 'max_x', 'max_y')]


def as_dict(value):
    return {n: getattr(value, n) for n, _ in value._fields_}


def summarize(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(samples=len(values), min_ms=min(values), median_ms=statistics.median(values),
                p95_ms=ordered[min(len(ordered)-1, math.ceil(len(ordered)*.95)-1)], max_ms=max(values),
                mean_ms=statistics.mean(values), above_16_67_ms=sum(v>1000/60 for v in values),
                above_33_33_ms=sum(v>1000/30 for v in values))


class API:
    def __init__(self, path):
        self.dll = C.CDLL(str(path))
        def bind(name, args, result=C.c_int):
            f = getattr(self.dll, name); f.argtypes=args; f.restype=result
            setattr(self, name.removeprefix('xl_') if hasattr(str, 'removeprefix') else name[3:], f)
        P=C.c_void_p; Z=C.c_size_t; U=C.c_uint32; F=C.c_float; N=C.c_uint64
        for name,args in [
            ('runtime_create',[P,C.POINTER(P)]),('runtime_destroy',[P]),
            ('document_create',[P,C.POINTER(P)]),('document_destroy',[P]),
            ('document_load_html',[P,C.c_char_p,Z]),('document_add_font',[P,C.c_char_p,P,Z]),
            ('document_get_stats',[P,C.POINTER(DocStats),Z]),
            ('document_query_selector',[P,C.c_char_p,Z,C.POINTER(N)]),
            ('view_options_init',[C.POINTER(ViewOptions),Z]),('view_create',[P,C.POINTER(ViewOptions),C.POINTER(P)]),
            ('view_destroy',[P]),('view_set_document',[P,P]),('view_set_graphics_backend',[P,U]),
            ('view_get_graphics_info',[P,C.POINTER(Graphics),Z]),('view_render',[P,C.POINTER(U)]),
            ('view_get_stats',[P,C.POINTER(ViewStats),Z]),('view_get_scroll',[P,N,C.POINTER(Scroll),Z]),
            ('view_set_scroll',[P,N,F,F]),('view_scroll_at',[P,F,F,F,F]),
            ('view_copy_rgba',[P,P,Z,Z]),('runtime_pump_events',[P,U]),
            ('view_set_scroll_input',[P,U]),('view_native_handle',[P,C.POINTER(U),C.POINTER(C.c_size_t)])]:
            bind('xl_'+name,args)
        self.instrumented = hasattr(self.dll,'xl_perf_reset')
        bind('xl_view_set_native_accessibility',[P,U])
        bind('xl_document_eval',[P,C.c_char_p,Z,U])
        bind('xl_document_script_result',[P,P,Z,C.POINTER(Z)])
        if self.instrumented:
            self.dll.xl_perf_reset.argtypes=[];self.dll.xl_perf_reset.restype=None
            self.dll.xl_perf_name.argtypes=[U];self.dll.xl_perf_name.restype=C.c_char_p
            self.dll.xl_perf_ns.argtypes=[U];self.dll.xl_perf_ns.restype=C.c_uint64
            self.dll.xl_perf_count.argtypes=[U];self.dll.xl_perf_count.restype=C.c_uint64
    def reset(self):
        if self.instrumented:self.dll.xl_perf_reset()
    def counters(self):
        result={}
        if self.instrumented:
            for i in range(64):
                name=self.dll.xl_perf_name(i)
                if not name:break
                result[name.decode()]=dict(ms=self.dll.xl_perf_ns(i)/1e6,count=self.dll.xl_perf_count(i))
        return result
    @staticmethod
    def check(status):
        if status:raise RuntimeError('SDK status='+str(status))


def worker(args):
    result=dict(page=str(args.page), page_sha256=hashlib.sha256(args.page.read_bytes()).hexdigest(),
                backend=args.backend, native=args.native, dimensions=[args.width,args.height],
                scripts='inline classic scripts evaluated' if args.scripts else 'not evaluated; source HTML unchanged; matching CSS-only diagnosis')
    start=time.perf_counter();api=API(args.dll);result['dll_load_ms']=(time.perf_counter()-start)*1000
    runtime=C.c_void_p();doc=C.c_void_p();view=C.c_void_p()
    try:
        start=time.perf_counter();api.check(api.runtime_create(None,C.byref(runtime)))
        api.check(api.document_create(runtime,C.byref(doc)))
        opts=ViewOptions();api.check(api.view_options_init(C.byref(opts),C.sizeof(opts)))
        opts.kind=0 if args.native else 2;opts.flags=1 if args.native else 0
        opts.width=args.width;opts.height=args.height;opts.title_utf8=b'XL rendering performance probe'
        api.check(api.view_create(runtime,C.byref(opts),C.byref(view)))
        result['runtime_document_view_ms']=(time.perf_counter()-start)*1000
        api.reset();start=time.perf_counter();api.check(api.view_set_graphics_backend(view,BACKENDS[args.backend]))
        result['backend_init_ms']=(time.perf_counter()-start)*1000;result['backend_init_counters']=api.counters()
        if args.font:
            data=args.font.read_bytes();api.check(api.document_add_font(doc,b'probe',data,len(data)))
        data=args.page.read_bytes();api.reset();start=time.perf_counter()
        api.check(api.document_load_html(doc,data,len(data)));api.check(api.view_set_document(view,doc))
        result['html_load_ms']=(time.perf_counter()-start)*1000;result['html_load_counters']=api.counters()
        if args.scripts:
            class Scripts(HTMLParser):
                def __init__(self):super().__init__(convert_charrefs=False);self.current=None;self.blocks=[]
                def handle_starttag(self,tag,attrs):
                    if tag=='script':self.current=[dict(attrs),[]]
                def handle_data(self,value):
                    if self.current is not None:self.current[1].append(value)
                def handle_endtag(self,tag):
                    if tag=='script' and self.current is not None:self.blocks.append(self.current);self.current=None
            parsed=Scripts();parsed.feed(data.decode('utf-8-sig'));script_rows=[]
            ready=b"globalThis.console=Object.freeze(Object.fromEntries(['log','info','warn','error','debug'].map(level=>[level,(...args)=>{}])));'console ready'"
            api.check(api.document_eval(doc,ready,len(ready),1000))
            for attrs,parts in parsed.blocks:
                if attrs.get('type','').lower() not in ('','text/javascript','application/javascript'):continue
                if 'src' in attrs:raise RuntimeError('external scripts need explicit host loader')
                code=''.join(parts).encode();start=time.perf_counter();status=api.document_eval(doc,code,len(code),1000)
                needed=C.c_size_t();api.document_script_result(doc,None,0,C.byref(needed));buffer=C.create_string_buffer(needed.value+1)
                api.document_script_result(doc,buffer,needed.value,C.byref(needed))
                script_rows.append(dict(status=status,ms=(time.perf_counter()-start)*1000,result=buffer.value.decode('utf-8','replace')[:2000]))
            result['script_results']=script_rows
        if args.native and args.native_ax:api.check(api.view_set_native_accessibility(view,1))
        flags=C.c_uint32()
        def render():
            calls=0;start=time.perf_counter()
            while True:
                status=api.view_render(view,C.byref(flags));calls+=1
                if status!=5:break
                if time.perf_counter()-start>30:raise TimeoutError('render busy for 30 seconds')
                time.sleep(.001)
            api.check(status)
            return (time.perf_counter()-start)*1000,calls
        api.reset();result['first_render_ms'],result['first_render_calls']=render();result['first_render_counters']=api.counters()
        if args.cancel_timers:
            code=b'for(let i=1;i<4096;i++)clearTimeout(i);"timers cancelled for diagnostic control"'
            api.check(api.document_eval(doc,code,len(code),1000));result['diagnostic_cancelled_timers']=True
        ds=DocStats();vs=ViewStats();gi=Graphics()
        api.check(api.document_get_stats(doc,C.byref(ds),C.sizeof(ds)));result['document']=as_dict(ds)
        api.check(api.view_get_graphics_info(view,C.byref(gi),C.sizeof(gi)));result['graphics']=as_dict(gi)
        if args.native:api.check(api.view_set_scroll_input(view,1))
        # Find an actual scrollable port without modifying the HTML or styles.
        candidates=[('viewport',0)]
        for selector in ['main','.main','.content','.page','.app','article','body','html']:
            node=C.c_uint64();b=selector.encode()
            if api.document_query_selector(doc,b,len(b),C.byref(node))==0:candidates.append((selector,node.value))
        ports=[]
        for selector,node in candidates:
            info=Scroll()
            if api.view_get_scroll(view,node,C.byref(info),C.sizeof(info))==0:
                ports.append(dict(selector=selector,node=node,**as_dict(info)))
        result['scrollports']=ports
        usable=[p for p in ports if p['max_y']>p['min_y']+10]
        result['scrollable']=bool(usable)
        target=max(usable,key=lambda p:p['max_y']-p['min_y']) if usable else None
        steady=[];api.reset()
        for _ in range(12):steady.append(render()[0])
        result['idle']=summarize(steady);result['idle_counters']=api.counters()
        if target:
            before=DocStats();api.check(api.document_get_stats(doc,C.byref(before),C.sizeof(before)))
            api.check(api.view_get_stats(view,C.byref(vs),C.sizeof(vs)));vs_before=as_dict(vs)
            rows=[];api.reset()
            for i in range(args.frames):
                period=max(2,min(24,int((target['max_y']-target['min_y'])/24)))
                y=target['min_y']+24*(1+(i%period))
                start=time.perf_counter();api.check(api.view_set_scroll(view,target['node'],target['x'],y))
                setter=(time.perf_counter()-start)*1000;elapsed,calls=render()
                rows.append(dict(set_ms=setter,render_ms=elapsed,calls=calls,y=y))
            result['scroll_counters']=api.counters();result['scroll_samples']=rows
            result['scroll']=summarize([r['set_ms']+r['render_ms'] for r in rows])
            after=DocStats();api.check(api.document_get_stats(doc,C.byref(after),C.sizeof(after)))
            result['scroll_layout_frames']=after.layout_frames-before.layout_frames
            result['scroll_document_revision_change']=after.revision-before.revision
            api.check(api.view_get_stats(view,C.byref(vs),C.sizeof(vs)))
            result['scroll_rasterized_pixels']=vs.rasterized_pixels-vs_before['rasterized_pixels']
            result['scroll_uploaded_pixels']=vs.uploaded_pixels-vs_before['uploaded_pixels']
            info=Scroll();api.check(api.view_get_scroll(view,target['node'],C.byref(info),C.sizeof(info)))
            result['final_scroll']=as_dict(info)
            if args.native:
                api.check(api.view_set_scroll(view,target['node'],target['x'],0));render()
                kind=C.c_uint32();hwnd=C.c_size_t();api.check(api.view_native_handle(view,C.byref(kind),C.byref(hwnd)))
                user=C.WinDLL('user32');user.PostMessageW.argtypes=[C.c_void_p,C.c_uint,C.c_size_t,C.c_ssize_t];user.PostMessageW.restype=C.c_int
                point=(C.c_long*2)(args.width//2,args.height//2)
                user.ClientToScreen.argtypes=[C.c_void_p,C.c_void_p];user.ClientToScreen(C.c_void_p(hwnd.value),point)
                lparam=(point[0]&0xffff)|((point[1]&0xffff)<<16);wheel=[];api.reset()
                for _ in range(args.frames):
                    start=time.perf_counter()
                    if not user.PostMessageW(C.c_void_p(hwnd.value),0x020A,((-120)&0xffff)<<16,lparam):raise RuntimeError('PostMessage wheel failed')
                    api.check(api.runtime_pump_events(runtime,1));render();wheel.append((time.perf_counter()-start)*1000)
                result['native_wheel']=summarize(wheel);result['native_wheel_counters']=api.counters()
                api.check(api.view_get_scroll(view,target['node'],C.byref(info),C.sizeof(info)))
                result['native_wheel_final_scroll']=as_dict(info)
        # Isolate redraw/readback cost with layout already valid.
        api.dll.xl_view_invalidate.argtypes=[C.c_void_p,C.c_int32,C.c_int32,C.c_int32,C.c_int32]
        api.dll.xl_view_invalidate.restype=C.c_int
        redraw=[];api.reset()
        for _ in range(8):
            api.check(api.dll.xl_view_invalidate(view,0,0,args.width,args.height));redraw.append(render()[0])
        result['redraw']=summarize(redraw);result['redraw_counters']=api.counters()
        pixels=(C.c_ubyte*(args.width*args.height*4))();copies=[];api.reset()
        for _ in range(5):
            start=time.perf_counter();api.check(api.view_copy_rgba(view,pixels,len(pixels),args.width*4))
            copies.append((time.perf_counter()-start)*1000)
        result['copy_rgba']=summarize(copies);result['copy_counters']=api.counters()
        result['rgba_sha256']=hashlib.sha256(pixels).hexdigest()
        result['state']='passed'
    except BaseException as error:
        result.update(state='failed',error=repr(error))
    finally:
        if view:api.view_destroy(view)
        if doc:api.document_destroy(doc)
        if runtime:api.runtime_destroy(runtime)
    print(json.dumps(result,ensure_ascii=False),flush=True)
    return 0 if result.get('state')=='passed' else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--page',type=Path)
    parser.add_argument('--backend',choices=BACKENDS,default='raster')
    parser.add_argument('--backends',nargs='+',choices=BACKENDS,default=['raster','auto','angle-gl'])
    parser.add_argument('--frames',type=int,default=30)
    parser.add_argument('--width',type=int,default=960);parser.add_argument('--height',type=int,default=640)
    parser.add_argument('--font',type=Path);parser.add_argument('--native',action='store_true')
    parser.add_argument('--native-ax',action='store_true');parser.add_argument('--scripts',action='store_true')
    parser.add_argument('--cancel-timers',action='store_true',help='diagnostic control only: cancel page timers after initial render')
    parser.add_argument('--worker',action='store_true');parser.add_argument('--output',type=Path)
    args=parser.parse_args();args.dll=args.dll.resolve()
    if args.worker:return worker(args)
    import artifacts
    from ci_support import source_manifest,manifest_digest
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    output=args.output or BUILD/'reports/render-performance'/stamp
    result=dict(schema=1,source_digest=manifest_digest(source_manifest(ROOT)),dll=str(args.dll),
                dll_sha256=hashlib.sha256(args.dll.read_bytes()).hexdigest(),kind='hidden native window' if args.native else 'offscreen',
                samples=[],note=('timing includes real SDK calls; original inline scripts evaluated' if args.scripts else
                                 'timing includes real SDK calls; no script evaluation') + '; each page/backend uses a fresh process')
    with artifacts.registered_directory(BUILD,output,'reports/render-performance/windows-x64'):
        pages=[args.page] if args.page else sorted((ROOT/'tests/user-dev-tests').glob('*.html'))
        for page in pages:
            for backend in args.backends:
                command=[sys.executable,'-B',str(Path(__file__).resolve()),'--worker','--dll',str(args.dll),
                         '--page',str(page.resolve()),'--backend',backend,'--frames',str(args.frames),
                         '--width',str(args.width),'--height',str(args.height)]
                if args.native:command.append('--native')
                if args.native_ax:command.append('--native-ax')
                if args.scripts:command.append('--scripts')
                if args.cancel_timers:command.append('--cancel-timers')
                if args.font:command+=['--font',str(args.font.resolve())]
                started=time.perf_counter()
                child=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,encoding='utf-8',errors='replace',timeout=180,
                                     env=dict(os.environ,PYTHONIOENCODING='utf-8'))
                raw=child.stdout.strip().splitlines()
                try:sample=json.loads(raw[-1])
                except (ValueError,IndexError):sample=dict(state='failed',page=str(page),backend=backend,stdout=child.stdout,error=child.stderr)
                sample.update(exit_code=child.returncode,process_wall_ms=(time.perf_counter()-started)*1000)
                if child.stderr:sample['stderr']=child.stderr
                result['samples'].append(sample)
                (output/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                print(page.name,backend,sample.get('state'),'first_ms=',round(sample.get('first_render_ms',0),2),
                      'scroll_ms=',round((sample.get('scroll') or {}).get('median_ms',0),2),'active=',sample.get('graphics',{}).get('active'),flush=True)
        result['state']='passed' if all(s['state']=='passed' for s in result['samples']) else 'failed'
        (output/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    artifacts.collect(BUILD,retain=3,families={'reports/render-performance/windows-x64'})
    print(output/'report.json')
    return 0 if result['state']=='passed' else 1


if __name__=='__main__':
    raise SystemExit(main())
