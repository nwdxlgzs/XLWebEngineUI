#!/usr/bin/env python3
"""Compare focused CSS text geometry with an explicitly selected local Chromium.

Development-only Windows oracle. No browser or reference font is distributed
with the SDK. All generated pages, profiles and results stay inside build/.
"""
import argparse
import contextlib
import ctypes as c
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid
import sys

sys.dont_write_bytecode = True
import artifacts
from ci_support import checked_child, source_manifest, manifest_digest, read_json, write_json

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FAMILY = 'reports/text-reference/windows-x64'


def adopt_completed_reports(build):
    """Only migrate recognizable finished reports produced by the older tool."""
    folder = Path(build) / 'reports/text-reference'
    if not folder.exists():
        return
    checked_child(build, folder)
    for path in folder.iterdir():
        if not re.fullmatch(r'\d{8}T\d{6}-[0-9a-f]{8}', path.name):
            continue
        checked_child(build, path)
        if not path.is_dir():
            continue
        report = path / 'report.json'
        checked_child(build, report)
        if not report.is_file():
            continue
        _, _, manifest, _ = artifacts.locations(build, path)
        if manifest.exists():
            continue
        try:
            data = read_json(report)
        except (ValueError, OSError):
            continue
        if data.get('state') not in ('passed', 'failed') or not all(key in data for key in
                ('browser', 'library', 'font_sha256', 'cases')):
            continue
        artifacts.register(build, path, FAMILY, last_used=report.stat().st_mtime)


@contextlib.contextmanager
def report_run(build=BUILD, retain=3):
    adopt_completed_reports(build)
    output = Path(build) / 'reports/text-reference' / (time.strftime('%Y%m%dT%H%M%S') + '-' + uuid.uuid4().hex[:8])
    try:
        with artifacts.registered_directory(build, output, FAMILY):
            write_json(output / 'report.json', {'state': 'running', 'started_at': time.time()})
            try:
                yield output
            except BaseException as error:
                write_json(output / 'report.json', {'state': 'interrupted' if isinstance(error, KeyboardInterrupt) else 'failed',
                                                  'error': str(error), 'finished_at': time.time()})
                raise
    finally:
        artifacts.collect(build, retain, artifacts.pinned_content_keys(ROOT), families={FAMILY})


def process_output(args, output, build=BUILD, timeout=40):
    """Lease the real child PID as well as its producing Python process."""
    child = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             creationflags=0x08000000 if os.name == 'nt' else 0)
    with contextlib.ExitStack() as stack:
        try:
            stack.enter_context(artifacts.lease(build, output, owner_pid=child.pid))
            try:
                stdout, stderr = child.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                raise RuntimeError('reference browser timed out')
            if child.returncode:
                raise RuntimeError('reference browser failed with status ' + str(child.returncode))
            return stdout, stderr
        finally:
            if child.poll() is None:
                try:
                    if os.name == 'nt':
                        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=10)
                finally:
                    if child.poll() is None:
                        child.kill()
                    child.communicate(timeout=10)


class Options(c.Structure):
    _fields_ = [('struct_size', c.c_uint32), ('abi_version', c.c_uint32), ('kind', c.c_uint32),
                ('flags', c.c_uint32), ('width', c.c_int32), ('height', c.c_int32),
                ('background', c.c_uint32), ('native_kind', c.c_uint32),
                ('native_window', c.c_size_t), ('title', c.c_char_p)]


class Bounds(c.Structure):
    _fields_ = [(name, c.c_float) for name in ('x', 'y', 'width', 'height')]


def compare(browser, library, font, output, case_filter=None):
    cases = json.loads((ROOT / 'tests/document/text-reference.json').read_text(encoding='utf-8'))
    if case_filter:
        cases = [case for case in cases if re.search(case_filter, case['name'])]
        if not cases:
            raise ValueError('no reference cases match: ' + case_filter)
    shutil.copyfile(font, output / 'font.ttf')
    # Chrome's headless viewport can differ from the offscreen view. Pin the
    # containing block too, so percentages/auto margins compare equal inputs.
    css = "@font-face{font-family:test;src:url(font.ttf)}*{scrollbar-width:none}body{margin:0;width:360px}p{margin:0;width:100px;font-size:20px;line-height:24px;font-family:test}"
    sections = []
    for case in cases:
        sections.append('<section data-case="' + case['name'] + '" data-scroll="' + html.escape(json.dumps(case.get('scroll', [])), quote=True) + '"><p style="' + html.escape(case['style'], quote=True)
                        + '">' + case['html'] + '</p></section>')
    script = """document.fonts.ready.then(()=>{
      let output={};
      for(let root of document.querySelectorAll('section')){
        for(let [selector,x,y] of JSON.parse(root.dataset.scroll)){
          let el=root.querySelector(selector); el.scrollLeft=x; el.scrollTop=y;
        }
        let result={height:root.querySelector('p').getBoundingClientRect().height};
        for(let el of root.querySelectorAll('[id]')){
          let box=el.getBoundingClientRect(),origin=root.getBoundingClientRect(),key='#'+el.id;
          result[key]=box.x-origin.x;
          for(let field of ['x','y','width','height'])result[key+'.'+field]=box[field]-(field==='x'?origin.x:field==='y'?origin.y:0);
          for(let [field,value] of Object.entries({scroll_x:el.scrollLeft,scroll_y:el.scrollTop,scroll_width:el.scrollWidth,scroll_height:el.scrollHeight,client_width:el.clientWidth,client_height:el.clientHeight}))result[key+'.'+field]=value;
        }
        output[root.dataset.case]=result;
      }
      document.body.textContent=JSON.stringify(output);
    })"""
    page = output / 'reference.html'
    page.write_text('<!doctype html><meta charset=utf-8><style>' + css + '</style>' + ''.join(sections)
                    + '<script>' + script + '</script>', encoding='utf-8')
    args = [str(browser), '--headless', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
            '--user-data-dir=' + str(output / 'profile'), '--virtual-time-budget=3000', '--dump-dom', page.as_uri()]
    stdout, stderr = process_output(args, output)
    (output / 'browser.html').write_bytes(stdout)
    (output / 'browser.log').write_bytes(stderr)
    match = re.search(r'<body>(.*)</body>', stdout.decode('utf-8', errors='replace'), re.S)
    if not match:
        raise RuntimeError('reference browser did not return geometry')
    reference = json.loads(html.unescape(match.group(1)))
    dll = c.CDLL(str(library))

    def bind(name, args):
        value = getattr(dll, name)
        value.argtypes, value.restype = args, c.c_int
        return value

    pointer = c.c_void_p
    runtime_create = bind('xl_runtime_create', [pointer, c.POINTER(pointer)])
    runtime_destroy = bind('xl_runtime_destroy', [pointer])
    document_create = bind('xl_document_create', [pointer, c.POINTER(pointer)])
    document_destroy = bind('xl_document_destroy', [pointer])
    add_font = bind('xl_document_add_font', [pointer, c.c_char_p, pointer, c.c_size_t])
    load = bind('xl_document_load_html', [pointer, c.c_char_p, c.c_size_t])
    query = bind('xl_document_query_selector', [pointer, c.c_char_p, c.c_size_t, c.POINTER(c.c_uint64)])
    options_init = bind('xl_view_options_init', [c.POINTER(Options), c.c_size_t])
    view_create = bind('xl_view_create', [pointer, c.POINTER(Options), c.POINTER(pointer)])
    attach = bind('xl_view_set_document', [pointer, pointer])
    render = bind('xl_view_render', [pointer, c.POINTER(c.c_uint32)])
    set_scroll = bind('xl_view_set_scroll', [pointer, c.c_uint64, c.c_float, c.c_float])
    class ScrollInfo(c.Structure):
        _fields_ = [('struct_size', c.c_uint32), ('flags', c.c_uint32)] + [(name, c.c_float) for name in
                    ('x', 'y', 'client_width', 'client_height', 'scroll_width', 'scroll_height', 'min_x', 'min_y', 'max_x', 'max_y')]
    get_scroll = bind('xl_view_get_scroll', [pointer, c.c_uint64, c.POINTER(ScrollInfo), c.c_size_t])
    bounds = bind('xl_view_node_bounds', [pointer, c.c_uint64, c.POINTER(Bounds)])
    view_destroy = bind('xl_view_destroy', [pointer])

    def check(status):
        if status:
            raise RuntimeError('SDK status ' + str(status))

    runtime, document, view = pointer(), pointer(), pointer()
    result = {'state': 'passed', 'browser': str(browser), 'library': str(library),
              'source_digest': manifest_digest(source_manifest(ROOT)),
              'browser_sha256': hashlib.sha256(browser.read_bytes()).hexdigest(),
              'library_sha256': hashlib.sha256(library.read_bytes()).hexdigest(),
              'font_sha256': hashlib.sha256(font.read_bytes()).hexdigest(), 'cases': {}}
    try:
        check(runtime_create(None, c.byref(runtime)))
        check(document_create(runtime, c.byref(document)))
        data = font.read_bytes()
        packed = c.create_string_buffer(data)
        check(add_font(document, b'test', packed, len(data)))
        options = Options()
        check(options_init(c.byref(options), c.sizeof(options)))
        options.kind, options.width, options.height = 2, 360, 260
        check(view_create(runtime, c.byref(options), c.byref(view)))
        check(attach(view, document))
        for case in cases:
            content = ('<!doctype html><style>' + css + '</style><p style="' + html.escape(case['style'], quote=True)
                       + '">' + case['html'] + '</p>').encode('utf-8')
            check(load(document, content, len(content)))
            deadline = time.monotonic() + 10
            flags = c.c_uint32()
            while True:
                status = render(view, c.byref(flags))
                if status != 5:
                    if status:
                        raise RuntimeError(case['name'] + ': render returned SDK status ' + str(status))
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError('render timeout')
                time.sleep(.001)
            for selector, x, y in case.get('scroll', []):
                node = c.c_uint64()
                encoded = selector.encode('utf-8')
                check(query(document, encoded, len(encoded), c.byref(node)))
                check(set_scroll(view, node, x, y))
            if case.get('scroll'):
                check(render(view, c.byref(flags)))
            actual = {}
            for selector in ['p'] + case.get('positions', []):
                node, rectangle = c.c_uint64(), Bounds()
                encoded = selector.encode('utf-8')
                check(query(document, encoded, len(encoded), c.byref(node)))
                check(bounds(view, node, c.byref(rectangle)))
                actual['height' if selector == 'p' else selector] = rectangle.height if selector == 'p' else rectangle.x
            for selector in case.get('geometry', []):
                node, rectangle = c.c_uint64(), Bounds()
                encoded = selector.encode('utf-8')
                check(query(document, encoded, len(encoded), c.byref(node)))
                check(bounds(view, node, c.byref(rectangle)))
                for field in ('x', 'y', 'width', 'height'):
                    actual[selector+'.'+field] = getattr(rectangle, field)
            for selector in case.get('scroll_metrics', []):
                node, info = c.c_uint64(), ScrollInfo()
                encoded = selector.encode('utf-8')
                check(query(document, encoded, len(encoded), c.byref(node)))
                check(get_scroll(view, node, c.byref(info), c.sizeof(info)))
                for field in ('x', 'y', 'scroll_width', 'scroll_height', 'client_width', 'client_height'):
                    actual[selector+'.'+('scroll_'+field if field in ('x','y') else field)] = getattr(info, field)
            expected = reference[case['name']]
            delta = {name: abs(value - expected[name]) for name, value in actual.items()}
            passed = all(value <= 1.01 for value in delta.values())
            result['cases'][case['name']] = {'state': 'passed' if passed else 'failed', 'engine': actual, 'reference': expected, 'delta': delta}
            if not passed:
                result['state'] = 'failed'
    finally:
        if view:
            check(view_destroy(view))
        if document:
            check(document_destroy(document))
        if runtime:
            check(runtime_destroy(runtime))
    (output / 'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--cases', help='only directly affected case names (regular expression)')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows DLL oracle currently requires a Windows host')
    manifest = json.loads((BUILD / 'generated/sdk-inputs/windows-x64/Debug/inputs.json').read_text(encoding='utf-8'))
    font = BUILD / manifest['graphics']['skia']['source_overlay'] / 'resources/fonts/Roboto-Regular.ttf'
    retain = read_json(ROOT / 'ci/settings.json')['retention']
    with report_run(retain=retain) as output:
        result = compare(args.browser.resolve(strict=True), args.library.resolve(strict=True), font, output, args.cases)
    print(json.dumps({'state': result['state'], 'cases': len(result['cases']), 'report': str(output / 'report.json')}))
    for name, case in result['cases'].items():
        if case['state'] != 'passed':
            print(name, case)
    return 0 if result['state'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
