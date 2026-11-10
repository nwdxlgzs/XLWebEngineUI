#!/usr/bin/env python3
"""Fetch locked graphics sources and build ANGLE's real private static closure."""
import sys
sys.dont_write_bytecode = True
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
import tempfile

import artifacts
from ci_support import WorkLock, read_json, write_json
from dependency_support import ROOT, BUILD, extract_source, restore_overlay_files, sha256


def discard_intermediates(prefix, libraries):
    """Published prefixes need the isolated closure, not duplicate input archives."""
    from ci_support import checked_child
    prefix=checked_child(BUILD,Path(prefix))
    keep={checked_child(prefix,prefix / name) for name in libraries}
    if not keep or not all(path.is_file() for path in keep):
        raise RuntimeError('cannot compact an incomplete graphics closure')
    released=0
    for path in (prefix / 'lib').rglob('*'):
        if path.suffix not in ('.a','.lib','.symbols','.isolated','.merged') or not path.is_file():continue
        resolved=checked_child(prefix,path)
        if resolved in keep:continue
        released+=resolved.stat().st_size;path.unlink()
    return released


def isolate_graphics(prefix, libraries, name, required, target):
    """Separate graphics components' incompatible private dependency versions."""
    import bootstrap
    from merge_archive import merge
    extension = '.lib' if target == 'windows-x64' else '.a'
    output = prefix / 'lib' / ('xl_' + name + extension)
    ar, nm, objcopy = [bootstrap.llvm_bin() / (tool + ('.exe' if os.name == 'nt' else ''))
                       for tool in ('llvm-ar', 'llvm-nm', 'llvm-objcopy')]
    inputs = [prefix / p for p in libraries]
    # Prefix is not published until all isolation and inventory checks succeed.
    # A fresh empty archive cannot silently incorporate a previous merged output.
    output.write_bytes(b'!<arch>\n')
    merge(output, inputs, inputs, ar, nm, objcopy, 'xl_' + name + '_', required)
    return [output.relative_to(prefix).as_posix()]


def fetch(names):
    locked = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies']
    for name in names or locked:
        spec = locked[name]
        source = extract_source(spec, name)
        for relative in spec['license_files']:
            if not (source / relative).is_file(): raise RuntimeError('missing license: ' + name + '/' + relative)
        print(name + ': locked source and license verified: ' + str(source), flush=True)


def build_angle(target, configuration, jobs):
    import bootstrap
    from ci import target_environment, Commands
    from deps import select_compiled_components, dependency_compiler_identity, platform_stack
    env, flags = target_environment(target)
    compiler_key, compiler = dependency_compiler_identity(target, env, flags)
    spec = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies']['angle']
    auxiliary_spec = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies']['chromium-zlib-utils']
    auxiliary = extract_source(auxiliary_spec, 'chromium-zlib-utils')
    recipe = read_json(ROOT / 'third_party/recipes/angle.json')
    try:
        skia = select_compiled_components(target, configuration, compiler_key, ['skia'])['skia']
    except RuntimeError:
        from deps import build_skia
        if build_skia(target, configuration, jobs): raise RuntimeError('Skia scaffold build failed')
        skia = select_compiled_components(target, configuration, compiler_key, ['skia'])['skia']
    source = extract_source(spec, 'angle')
    gl_headers, gl_spec = None, None
    if target.startswith('linux-'):
        gl_spec = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies']['libglvnd-headers']
        gl_headers = extract_source(gl_spec, 'libglvnd-headers')
    identity = dict(source=spec['sha256'], skia_key=skia['key'], recipe=recipe,
                    target=target, configuration=configuration, compiler=compiler_key,
                    driver=sha256(Path(__file__)), isolation_driver=sha256(ROOT / 'tools/merge_archive.py'), auxiliary=auxiliary_spec['sha256'],
                    patches={name: sha256(ROOT / name) for name in
                             spec['patches'] + ['third_party/patches/skia-angle-static.json']})
    if gl_spec:
        identity['glx_headers'] = dict(sha256=gl_spec['sha256'], revision=gl_spec['revision'], license=gl_spec['license'])
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    work = BUILD / 'deps/work' / target / 'angle' / key / configuration
    previous = read_json(BUILD / 'reports/graphics-builds' / target / ('angle-' + configuration + '.json'), {})
    if previous.get('source_sha256') == spec['sha256'] and previous.get('work'):
        from ci_support import checked_child
        work = checked_child(BUILD / 'deps/work' / target / 'angle', BUILD / previous['work'])
    else:
        # These are untrusted compiler caches, never successful build evidence.
        # Prefer the same scaffold/compiler path and most reusable Ninja objects.
        candidates = []
        for ninja_log in (BUILD / 'deps/work' / target / 'angle').glob('*/' + configuration + '/out/.ninja_log'):
            args_file = ninja_log.parent / 'args.gn'
            text = args_file.read_text(encoding='utf-8')
            tool_field = 'clang_win' if target == 'windows-x64' else 'cxx'
            if tool_field + ' = ' + json.dumps(skia['gn_args'][tool_field]) in text:
                candidates.append((ninja_log.stat().st_size, ninja_log.parent.parent))
        if candidates: work = max(candidates)[1]
    prefix = BUILD / 'deps/install' / target / 'angle' / key / configuration
    overlay = BUILD / skia['source_overlay']
    # Share Skia's reproducible GN scaffold; avoid another full source-tree copy.
    destination = overlay / 'third_party/externals/angle2'
    references = [source.relative_to(BUILD).as_posix(), auxiliary.relative_to(BUILD).as_posix(), skia['prefix']]
    if gl_headers: references.append(gl_headers.relative_to(BUILD).as_posix())
    work.mkdir(parents=True, exist_ok=True)
    artifacts.register(BUILD, work, 'dependency-work/{}/angle/{}'.format(target, configuration), key, references)
    prefix.mkdir(parents=True,exist_ok=True)
    artifacts.register(BUILD,prefix,'dependency-install/{}/angle/{}'.format(target,configuration),key,
                       references + [work.relative_to(BUILD).as_posix()])
    with WorkLock(BUILD / 'state' / ('angle-' + target + '.lock')), contextlib.ExitStack() as leases:
        for item in (source, work, prefix, BUILD / skia['prefix'], overlay.parent): leases.enter_context(artifacts.lease(BUILD, item))
        if gl_headers: leases.enter_context(artifacts.lease(BUILD, gl_headers))
        marker = destination / '.xl-angle-source'
        if not marker.is_file() or marker.read_text(encoding='ascii') != spec['sha256']:
            if destination.exists():
                from ci_support import checked_child
                shutil.rmtree(checked_child(BUILD, destination))
            shutil.copytree(source, destination, symlinks=True)
            marker.write_text(spec['sha256'], encoding='ascii')
        else:
            restore_overlay_files(source, destination)
        from dependency_patches import apply_patch_file
        if target == 'windows-x64':
            for filename in spec['patches']: apply_patch_file(source, destination, filename)
        stamp = read_json(prefix / '.build.json', {})
        if stamp.get('key') == key and stamp.get('state') == 'compiled-and-installed' and stamp.get('libraries') and all(
                (prefix / relative).is_file() for relative in stamp['libraries']):
            discard_intermediates(prefix,stamp['libraries'])
            print('ANGLE static closure reused: ' + key, flush=True)
            return stamp
        args = dict(skia['gn_args'], **recipe['gn_args'])
        from dependency_patches import apply_patch_file
        upstream = extract_source(read_json(ROOT / 'third_party/dependencies.lock.json')['dependencies']['skia'], 'skia')
        apply_patch_file(upstream, overlay, 'third_party/patches/skia-angle-static.json')
        args['xl_enable_angle_static'] = True
        zlib_overlay = overlay / 'third_party/externals/zlib'
        shutil.copytree(auxiliary / 'google', zlib_overlay / 'google', dirs_exist_ok=True)
        args['angle_enable_d3d11'] = target == 'windows-x64'
        if gl_headers:
            args['extra_cflags'] = list(args['extra_cflags']) + ['-I' + (gl_headers / 'include').as_posix()]
        if target == 'linux-x64-musl':
            platform = platform_stack(configuration, compiler_key)
            references.extend(platform['prefixes'])
            args['extra_cflags'] = list(args['extra_cflags']) + ['-I' + (BUILD / p / 'include').as_posix() for p in platform['prefixes']]
        output = work / 'out'
        output.mkdir(exist_ok=True)
        (output / 'args.gn').write_text(''.join(k + ' = ' + json.dumps(v) + '\n' for k, v in args.items()), encoding='utf-8')
        command = Commands(work / 'commands.log', env, verbose=False)
        command.run([bootstrap.prepare_gn(), 'gen', output], cwd=overlay)
        command.run(['ninja', '-C', output, '-j', str(jobs), 'xl_angle_static'], cwd=overlay)
        extension = '*.lib' if target == 'windows-x64' else '*.a'
        libraries = []
        for archive in sorted(output.rglob(extension)):
            relative = Path('lib') / archive.relative_to(output)
            installed = prefix / relative
            installed.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(archive, installed)
            libraries.append(relative.as_posix())
        if not any('libEGL_static' in p for p in libraries) or not any('libGLESv2_static' in p for p in libraries):
            raise RuntimeError('ANGLE did not produce both real static EGL/GLES libraries')
        products = libraries
        libraries = [relative for relative in products if Path(relative).stem in ('xl_angle_static', 'libxl_angle_static')]
        if len(libraries) != 1: raise RuntimeError('ANGLE complete static closure is missing')
        raw_libraries = libraries
        libraries = isolate_graphics(prefix, libraries, 'angle', 'eglGetProcAddress', target)
        discarded=discard_intermediates(prefix,libraries)
        shutil.copytree(source / 'include', prefix / 'include', dirs_exist_ok=True)
        notices = prefix / 'share/licenses/angle'
        notices.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / 'LICENSE', notices / 'LICENSE')
        for relative in auxiliary_spec['license_files']:
            shutil.copyfile(auxiliary / relative, notices / ('zlib-utils-' + Path(relative).name))
        if gl_headers:
            for relative in gl_spec['license_files']:
                notice = prefix / 'share/licenses/libglvnd-headers' / relative
                notice.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(gl_headers / relative, notice)
        stamp = dict(name='angle', key=key, source_sha256=spec['sha256'], identity=identity,
                     state='compiled-and-installed', integration_status='not-verified',
                     prefix=prefix.relative_to(BUILD).as_posix(), libraries=libraries,
                     build_products=products, intermediate_archives_retained=False, discarded_intermediate_bytes=discarded,
                     required_components=['zlib'],
                     raw_libraries=raw_libraries, symbol_prefix='xl_angle_',
                     work=work.relative_to(BUILD).as_posix(),
                     source_overlay=destination.relative_to(BUILD).as_posix(), gn_args=args,
                     finished_at=time.time())
        write_json(prefix / '.build.json', stamp)
        artifacts.register(BUILD, prefix, 'dependency-install/{}/angle/{}'.format(target, configuration), key,
                           references + [work.relative_to(BUILD).as_posix()])
        write_json(BUILD / 'reports/graphics-builds' / target / ('angle-' + configuration + '.json'), stamp)
        return stamp


def reusable_cmake_work(root, configuration, source_sha, compiler_path):
    """Reuse matching compiled work, or a source copy before compiler detection."""
    candidates = []
    for marker in Path(root).glob('*/' + configuration + '/source/.source.json'):
        if read_json(marker, {}).get('sha256') != source_sha:
            continue
        work = marker.parent.parent
        cache = work / 'out/CMakeCache.txt'
        compiler_files = list(cache.parent.glob('CMakeFiles/*/CMakeCXXCompiler.cmake'))
        files = ([cache] if cache.is_file() else []) + compiler_files
        text = '\n'.join(p.read_text(encoding='utf-8') for p in files).replace('\\', '/').lower()
        compatible = str(compiler_path).replace('\\', '/').lower() in text
        source_only = not compiler_files and not (work / 'out/build.ninja').exists()
        if compatible or source_only:
            log = cache.parent / '.ninja_log'
            candidates.append((log.stat().st_size if log.is_file() else 0, str(work), work))
    return max(candidates)[2] if candidates else None


def build_cmake_graphics(name, target, configuration, jobs):
    from ci import target_environment, Commands
    from deps import dependency_compiler_identity
    from dependency_patches import apply_patch_file, patch_inputs
    env, flags = target_environment(target)
    compiler_key, compiler = dependency_compiler_identity(target, env, flags)
    spec = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies'][name]
    recipe = read_json(ROOT / ('third_party/recipes/' + name + '.json'))
    cmake, host_tool = 'cmake', None
    if name == 'swiftshader' and target.startswith('linux-'):
        import bootstrap
        cmake = bootstrap.prepare_cmake()
        host_tool = bootstrap.LOCK['cmake-linux-x64']
    source = extract_source(spec, name)
    identity = dict(source=spec['sha256'], compiler=compiler_key, configuration=configuration,
                    target=target, recipe=recipe, patches=patch_inputs(spec), driver=sha256(Path(__file__)),
                    isolation_driver=sha256(ROOT / 'tools/merge_archive.py'))
    if host_tool:
        identity['host_cmake'] = dict(version=host_tool['version'], sha256=host_tool['sha256'])
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    work = BUILD / 'deps/work' / target / name / key / configuration
    previous = read_json(BUILD / 'reports/graphics-builds' / target / (name + '-' + configuration + '.json'), {})
    if (previous.get('source_sha256') == spec['sha256'] and previous.get('compiler') == compiler and previous.get('work')):
        from ci_support import checked_child
        candidate = checked_child(BUILD / 'deps/work' / target / name, BUILD / previous['work'])
        if (candidate / 'out/CMakeCache.txt').is_file(): work = candidate
    if not work.exists():
        candidate = reusable_cmake_work(BUILD / 'deps/work' / target / name, configuration,
                                        spec['sha256'], compiler['compiler_path'])
        if candidate: work = candidate
    prefix = BUILD / 'deps/install' / target / name / key / configuration
    work.mkdir(parents=True, exist_ok=True)
    references = [source.relative_to(BUILD).as_posix()]
    if host_tool:
        references.append(cmake.parent.parent.relative_to(BUILD).as_posix())
    artifacts.register(BUILD, work, 'dependency-work/{}/{}/{}'.format(target, name, configuration), key, references)
    prefix.mkdir(parents=True,exist_ok=True)
    artifacts.register(BUILD,prefix,'dependency-install/{}/{}/{}'.format(target,name,configuration),key,
                       references + [work.relative_to(BUILD).as_posix()])
    with WorkLock(BUILD / 'state' / (name + '-' + target + '.lock')), artifacts.lease(BUILD, source), artifacts.lease(BUILD, work), artifacts.lease(BUILD,prefix):
        stamp = read_json(prefix / '.build.json', {})
        if stamp.get('key') == key and stamp.get('state') == 'compiled-and-installed' and stamp.get('libraries') and all(
                (prefix / relative).is_file() for relative in stamp['libraries']):
            discard_intermediates(prefix,stamp['libraries']);return stamp
        overlay = work / 'source'
        if not overlay.is_dir(): shutil.copytree(source, overlay, symlinks=True,copy_function=shutil.copyfile)
        else: restore_overlay_files(source, overlay)
        for filename in spec['patches']: apply_patch_file(source, overlay, filename)
        command = Commands(work / 'commands.log', env, verbose=False)
        output = work / 'out'
        configure = [cmake, '-S', overlay, '-B', output, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=' + configuration,
                     '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',
                     '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
                     '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded' + ('Debug' if configuration == 'Debug' else '')]
        if name == 'dxcompiler':
            configure += ['-C', overlay / 'cmake/caches/PredefinedParams.cmake',
                          '-DLLVM_USE_CRT_DEBUG=MTd', '-DLLVM_USE_CRT_RELEASE=MT',
                          '-DLLVM_USE_CRT_RELWITHDEBINFO=MT', '-DLLVM_USE_CRT_MINSIZEREL=MT']
        configure += ['-D{}={}'.format(k, 'ON' if v is True else 'OFF' if v is False else v) for k, v in recipe['options'].items()]
        command.run(configure + flags)
        command.run([cmake, '--build', output, '--parallel', str(jobs), '--target', recipe.get('build_target',recipe['target'])])
        libraries = []
        if name == 'dxcompiler':
            from ci_support import checked_child
            inventory=output / 'xl-runtime-libraries.txt'
            if not inventory.is_file(): raise RuntimeError('DXC runtime link graph inventory missing')
            archives=[checked_child(output,Path(item)) for item in inventory.read_text(encoding='utf-8').splitlines() if item]
        else:
            archives=sorted(output.rglob('*.lib' if target == 'windows-x64' else '*.a'))
        for archive in archives:
            relative = Path('lib') / archive.relative_to(output)
            installed = prefix / relative
            installed.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(archive, installed); libraries.append(relative.as_posix())
        if not any(Path(p).stem in (recipe['target'], 'lib' + recipe['target']) for p in libraries): raise RuntimeError(name + ' static implementation missing')
        products = libraries
        libraries = isolate_graphics(prefix, libraries, name, 'vkGetInstanceProcAddr' if name == 'swiftshader' else 'DxcCreateInstance', target)
        discarded=discard_intermediates(prefix,libraries)
        shutil.copytree(source / 'include', prefix / 'include', dirs_exist_ok=True)
        for relative in spec['license_files']:
            notice = prefix / 'share/licenses' / name / relative
            notice.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(source / relative, notice)
        stamp = dict(name=name, key=key, prefix=prefix.relative_to(BUILD).as_posix(),
                     source_sha256=spec['sha256'], identity=identity, compiler=compiler,
                     work=work.relative_to(BUILD).as_posix(), libraries=libraries,
                     build_products=products, intermediate_archives_retained=False, discarded_intermediate_bytes=discarded,
                     symbol_prefix='xl_' + name + '_',
                     state='compiled-and-installed', integration_status='not-verified', finished_at=time.time())
        write_json(prefix / '.build.json', stamp)
        artifacts.register(BUILD, prefix, 'dependency-install/{}/{}/{}'.format(target, name, configuration), key,
                           references + [work.relative_to(BUILD).as_posix()])
        write_json(BUILD / 'reports/graphics-builds' / target / (name + '-' + configuration + '.json'), stamp)
        return stamp


def verify_dxcompiler(target, configuration, jobs):
    from ci import target_environment, Commands
    stamp=build_cmake_graphics('dxcompiler',target,configuration,jobs)
    prefix=BUILD / stamp['prefix']
    work=BUILD / 'reports/graphics-smoke' / target / 'dxcompiler-static' / stamp['key']
    work.mkdir(parents=True,exist_ok=True)
    artifacts.register(BUILD,work,'graphics-probe/' + target + '/dxcompiler',stamp['key'],[stamp['prefix']])
    env,flags=target_environment(target)
    command=Commands(work / 'commands.log',env,verbose=False)
    report=dict(state='running',dependency_key=stamp['key'],
                test_sha256=sha256(ROOT / 'tests/graphics/dxcompiler_static.cpp'))
    with artifacts.lease(BUILD,work),artifacts.lease(BUILD,prefix):
        try:
            command.run(['cmake','-S',ROOT / 'tests/graphics/dxcompiler','-B',work / 'out','-G','Ninja',
                         '-DCMAKE_BUILD_TYPE=' + configuration,'-DXL_DXC_PREFIX=' + prefix.as_posix(),
                         '-DXL_DXC_LIBRARY=' + (prefix / stamp['libraries'][0]).as_posix()] + flags)
            command.run(['cmake','--build',work / 'out','--parallel',str(jobs)])
            command.run(['ctest','--test-dir',work / 'out','--output-on-failure','--no-tests=error'])
            report['state']='passed'
        except Exception as error:
            report.update(state='failed',error=str(error));raise
        finally:
            log=work / 'out/Testing/Temporary/LastTest.log'
            if log.is_file():shutil.copyfile(log,work / 'LastTest.log')
            # Consumer binaries/PDBs are disposable validation scratch.
            output=work / 'out'
            if output.exists():
                from ci_support import checked_child
                shutil.rmtree(checked_child(work,output))
            write_json(work / 'report.json',report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('fetch', 'angle', 'swiftshader', 'dxcompiler', 'verify-dxcompiler'))
    parser.add_argument('names', nargs='*')
    parser.add_argument('--target', default='windows-x64' if os.name == 'nt' else 'linux-x64-musl',
                        choices=('windows-x64', 'linux-x64-musl', 'linux-x64-glibc-dev'))
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Debug')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    try:
        if args.command == 'fetch': fetch(args.names)
        else:
            if os.name == 'nt' and args.target.startswith('linux-'):
                from ci import linux_path
                import subprocess
                return subprocess.call(['wsl.exe', '-d', 'Ubuntu', '--exec', 'python3', '-B', linux_path(Path(__file__))] + sys.argv[1:])
            jobs = max(1, min(args.jobs, 8))
            if args.command == 'verify-dxcompiler': verify_dxcompiler(args.target,args.configuration,jobs)
            elif args.command == 'angle': build_angle(args.target, args.configuration, jobs)
            else: build_cmake_graphics(args.command, args.target, args.configuration, jobs)
        return 0
    finally:
        artifacts.collect(BUILD, 3, artifacts.pinned_content_keys(ROOT))


if __name__ == '__main__': sys.exit(main())
