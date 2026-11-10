#!/usr/bin/env python3
"""Reproducible private Dawn/Tint closure, using existing static graphics."""
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
import uuid

import artifacts
from ci_support import WorkLock, checked_child, read_json, write_json
from dependency_support import ROOT, BUILD, extract_source, restore_overlay_files, sha256, tree_files
from dependency_patches import apply_patch_file, patch_inputs


def source_specs():
    specs = dict(read_json(ROOT / 'third_party/dawn-sources.lock.json')['dependencies'])
    shared = read_json(ROOT / 'third_party/skia-sources.lock.json')['dependencies']
    specs.update({name: shared[name] for name in ('spirv-tools', 'spirv-headers', 'vulkan-headers')})
    return specs


def declared_inputs(target):
    recipe_dir = ROOT / 'third_party/recipes/dawn'
    specs = source_specs()
    identity = dict(sources=specs, recipe=read_json(ROOT / 'third_party/recipes/dawn.json'),
                    recipe_files={p.name: sha256(p) for p in sorted(recipe_dir.rglob('*')) if p.is_file()},
                    patches=patch_inputs(specs['dawn']), driver=sha256(Path(__file__)))
    if target == 'windows-x64':
        identity['dxc_headers'] = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies']['dxcompiler']['sha256']
    if target == 'linux-x64-musl':
        identity['tls_fallback'] = read_json(ROOT / 'third_party/toolchains.lock.json')['llvm-runtimes-source']
    return identity


def sources():
    specs = source_specs()
    paths = {}
    for name, spec in specs.items():
        paths[name] = extract_source(spec, name)
        for relative in spec['license_files']:
            if not (paths[name] / relative).is_file():
                raise RuntimeError('missing license: ' + name + '/' + relative)
    return specs, paths


def graphics_component(name, target, configuration, compiler_key):
    stamp = read_json(BUILD / 'reports/graphics-builds' / target / (name + '-' + configuration + '.json'))
    spec = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies'][name]
    if (stamp.get('state') != 'compiled-and-installed' or stamp.get('source_sha256') != spec['sha256']
            or stamp.get('identity', {}).get('compiler') != compiler_key):
        raise RuntimeError('build current ' + name + ' before Dawn')
    prefix = checked_child(BUILD, BUILD / stamp['prefix'])
    if not stamp.get('libraries') or not all((prefix / p).is_file() for p in stamp['libraries']):
        raise RuntimeError('incomplete ' + name + ' static closure')
    return stamp


def headers_available(stamp):
    if not stamp.get('header_root'):
        return False
    root = checked_child(BUILD, BUILD / stamp['header_root'])
    inventory = read_json(root / '.headers.json', {})
    return (inventory.get('key') == stamp.get('header_key') and inventory.get('files')
            and set(inventory['files']).issubset(tree_files(root, inventory['files'])))


def publish_headers(source, output, target, references):
    """A C++ implementation patch must not change every Skia include path."""
    origins = [(source / 'include', 'include', False), (output / 'dawn/gen/include', 'include', False),
               (source / 'src/tint', 'private/src/tint', True),
               (source / 'src/utils', 'private/src/utils', True),
               (output / 'dawn/gen/src/tint', 'private/src/tint', True)]
    files = {}
    for origin, destination, private in origins:
        if not origin.is_dir():
            continue
        for path in origin.rglob('*'):
            if path.is_file() and (not private or path.suffix in ('.h', '.inl', '.inc')):
                files[(Path(destination) / path.relative_to(origin)).as_posix()] = path
    inventory = {name: sha256(path) for name, path in sorted(files.items())}
    key = hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()[:20]
    root = BUILD / 'deps/headers' / target / 'dawn' / key
    root.mkdir(parents=True, exist_ok=True)
    artifacts.register(BUILD, root, 'dependency-headers/' + target + '/dawn', key, references)
    with artifacts.lease(BUILD, root):
        previous = read_json(root / '.headers.json', {})
        for name, origin in files.items():
            destination = root / name
            if not destination.is_file() or previous.get('files', {}).get(name) != inventory[name]:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(origin, destination)
        write_json(root / '.headers.json', dict(key=key, files=inventory))
    return root, key


def build(target, configuration, jobs, probe=False, report_dir=None):
    if report_dir is None:
        run_id = time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '-' + uuid.uuid4().hex[:8]
        report_dir = BUILD / 'reports/dawn-build' / target / configuration / run_id
        family = 'dawn-build/' + target + '/' + configuration
        with artifacts.registered_directory(BUILD, report_dir, family):
            status = dict(state='running', target=target, configuration=configuration, probe=probe)
            write_json(report_dir / 'report.json', status)
            try:
                stamp = build(target, configuration, jobs, probe, report_dir)
                status.update(state='passed', key=stamp['key'], integration_status=stamp['integration_status'])
                return stamp
            except Exception as error:
                status.update(state='failed', error=str(error))
                raise
            finally:
                write_json(report_dir / 'report.json', status)
    from ci import target_environment, Commands
    from deps import dependency_compiler_identity, platform_stack
    from graphics_deps import reusable_cmake_work
    env, flags = target_environment(target)
    compiler_key, compiler = dependency_compiler_identity(target, env, flags)
    cmake, cmake_spec = 'cmake', None
    if target.startswith('linux-'):
        import bootstrap
        cmake_spec = bootstrap.LOCK['cmake-linux-x64']
    identity = dict(declared_inputs(target), compiler=compiler_key, target=target, configuration=configuration)
    if cmake_spec:
        identity['host_cmake'] = dict(version=cmake_spec['version'], sha256=cmake_spec['sha256'])
    platform = platform_stack(configuration, compiler_key) if target == 'linux-x64-musl' else None
    if platform:
        identity['platform'] = platform['key']
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    prefix = BUILD / 'deps/install' / target / 'dawn' / key / configuration
    if not probe and (prefix / '.build.json').is_file():
        with WorkLock(BUILD / 'state' / ('dawn-' + target + '.lock')):
            stamp = read_json(prefix / '.build.json', {})
            if (stamp.get('key') == key and stamp.get('state') == 'compiled-and-installed'
                    and stamp.get('libraries') and all((prefix / p).is_file() for p in stamp['libraries'])
                    and headers_available(stamp)):
                artifacts.register(BUILD, prefix, 'dependency-install/' + target + '/dawn/' + configuration,
                                   key, stamp.get('artifact_references', [stamp['header_root']]))
                print('Dawn/Tint private closure reused without scanning source/tool trees: ' + key, flush=True)
                return stamp
    if cmake_spec:
        cmake = bootstrap.prepare_cmake()
        # WSL inherits Windows' PATH. CMake package discovery would otherwise
        # stat every Windows SDK/tool directory through 9p for each probe.
        env = dict(env, PATH=os.pathsep.join((str(bootstrap.llvm_bin()), str(cmake.parent), '/usr/bin', '/bin')))
    specs, paths = sources()
    if target == 'linux-x64-musl':
        # Old musl has no __cxa_thread_atexit_impl. Use the matching, unmodified
        # LLVM pthread fallback in this closure instead of pulling the existing
        # toolchain's incorrectly probed strong-reference object from libc++.a.
        metadata = read_json(BUILD / 'toolchains/wrappers/linux-musl/toolchain.json')
        paths['llvm-tls-fallback'] = checked_child(BUILD, Path(metadata['source']))
        specs['llvm-tls-fallback'] = dict(identity['tls_fallback'], license_files=['libcxxabi/LICENSE.TXT'])
    # The archive contains external entry-point references, not another copy of
    # these libraries. Keeping their build keys out of its identity also avoids
    # a Skia -> Dawn -> ANGLE -> Skia build-identity cycle.
    graphics = ({name: graphics_component(name, target, configuration, compiler_key)
                 for name in ('swiftshader', 'angle') + (('dxcompiler',) if target == 'windows-x64' else ())}
                if probe else {})
    dxc_spec, dxc_source = None, None
    if target == 'windows-x64':
        dxc_spec = read_json(ROOT / 'third_party/graphics-sources.lock.json')['dependencies']['dxcompiler']
        dxc_source = extract_source(dxc_spec, 'dxcompiler')
    recipe_dir = ROOT / 'third_party/recipes/dawn'
    work_root = BUILD / 'deps/work' / target / 'dawn'
    work = reusable_cmake_work(work_root, configuration, specs['dawn']['sha256'], compiler['compiler_path'])
    work = work or work_root / key / configuration
    references = [p.relative_to(BUILD).as_posix() for p in paths.values()]
    if cmake_spec:
        references.append(cmake.parent.parent.relative_to(BUILD).as_posix())
    if dxc_source:
        references.append(dxc_source.relative_to(BUILD).as_posix())
    references += [stamp['prefix'] for stamp in graphics.values()]
    if platform:
        references += platform['prefixes']
    for path, family in ((work, 'dependency-work'), (prefix, 'dependency-install')):
        path.mkdir(parents=True, exist_ok=True)
        artifacts.register(BUILD, path, family + '/' + target + '/dawn/' + configuration, key, references)
    report = BUILD / 'reports/graphics-builds' / target / ('dawn-' + configuration + '.json')
    with WorkLock(BUILD / 'state' / ('dawn-' + target + '.lock')), contextlib.ExitStack() as leases:
        for path in list(paths.values()) + ([dxc_source] if dxc_source else []) + [work, prefix] + [BUILD / s['prefix'] for s in graphics.values()]:
            leases.enter_context(artifacts.lease(BUILD, path))
        stamp = read_json(prefix / '.build.json', {})
        reusable = (stamp.get('key') == key and stamp.get('state') == 'compiled-and-installed'
                    and stamp.get('libraries') and all((prefix / p).is_file() for p in stamp['libraries'])
                    and headers_available(stamp))
        if reusable and not probe:
            references.append(stamp['header_root'])
            artifacts.register(BUILD, prefix, 'dependency-install/' + target + '/dawn/' + configuration, key,
                               references + [work.relative_to(BUILD).as_posix()])
            print('Dawn/Tint private closure reused: ' + key, flush=True)
            return stamp
        source = paths['dawn']
        overlay = work / 'source'
        if overlay.is_dir():
            restore_overlay_files(source, overlay)
        else:
            print('Preparing Dawn source overlay', flush=True)
            shutil.copytree(source, overlay, symlinks=True, copy_function=shutil.copyfile)
        for filename in specs['dawn'].get('patches', []):
            apply_patch_file(source, overlay, filename)
        env = dict(env, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
                   PYTHONPATH=os.pathsep.join(str(paths[n] / 'src') for n in ('jinja2', 'markupsafe')))
        command = Commands(report_dir / 'commands.log', env, verbose=False)
        options = dict(identity['recipe']['options'])
        options.update(DAWN_ENABLE_D3D11=target == 'windows-x64', DAWN_ENABLE_D3D12=target == 'windows-x64',
                       DAWN_ENABLE_METAL=False, DAWN_USE_BUILT_DXC=target == 'windows-x64',
                       DAWN_USE_X11=target.startswith('linux-'), TINT_BUILD_HLSL_WRITER=target == 'windows-x64',
                       TINT_BUILD_MSL_WRITER=False)
        dirs = {'ABSEIL': 'abseil-cpp', 'EGL_REGISTRY': 'egl-registry', 'OPENGL_REGISTRY': 'opengl-registry',
                'SPIRV_TOOLS': 'spirv-tools', 'SPIRV_HEADERS': 'spirv-headers', 'VULKAN_HEADERS': 'vulkan-headers',
                'VULKAN_UTILITY_LIBRARIES': 'vulkan-utility-libraries'}
        options.update({'DAWN_' + name + '_DIR': paths[value].as_posix() for name, value in dirs.items()})
        options.update(DAWN_JINJA2_DIR=(paths['jinja2'] / 'src/jinja2').as_posix(),
                       DAWN_MARKUPSAFE_DIR=(paths['markupsafe'] / 'src/markupsafe').as_posix(),
                       Python3_EXECUTABLE=Path(sys.executable).as_posix(), XL_DAWN_SOURCE=overlay.as_posix())
        if target == 'linux-x64-musl':
            options['XL_DAWN_LLVM_TLS_SOURCE'] = paths['llvm-tls-fallback'].as_posix()
        if dxc_source:
            options['XL_DAWN_DXC_INCLUDE'] = (dxc_source / 'include').as_posix()
        for name, stamp in graphics.items():
            base = BUILD / stamp['prefix']
            options['XL_DAWN_' + name.upper() + '_LIBRARY'] = (base / stamp['libraries'][0]).as_posix()
            if name == 'dxcompiler':
                options['XL_DAWN_DXC_LIBRARY'] = options.pop('XL_DAWN_DXCOMPILER_LIBRARY')
            if name == 'angle':
                options['XL_DAWN_ANGLE_INCLUDE'] = (base / 'include').as_posix()
        options['XL_DAWN_PROBE'] = (ROOT / 'tests/graphics/dawn_native.cpp').as_posix() if probe else ''
        probe_hash = sha256(ROOT / 'tests/graphics/dawn_native.cpp') if probe else None
        if probe:
            from deps import select_compiled_components
            zlib = select_compiled_components(target, configuration, compiler_key, ['zlib'])['zlib']
            extra_libraries = [(BUILD / zlib['prefix'] / p).as_posix() for p in zlib['libraries']]
            if platform:
                extra_libraries += [p.as_posix() for relative in platform['prefixes']
                                    for p in sorted((BUILD / relative / 'lib').glob('*.a'))]
            options['XL_DAWN_PROBE_LIBRARIES'] = ';'.join(extra_libraries)
        output = work / 'out'
        configure = [cmake, '-S', recipe_dir, '-B', output, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=' + configuration,
                     '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded' + ('Debug' if configuration == 'Debug' else '')]
        if platform:
            from deps import private_platform_flags
            prefixes = [BUILD / p for p in platform['prefixes']]
            configure += private_platform_flags(prefixes)
            options['XL_DAWN_X11_INCLUDE_DIRS'] = ';'.join((p / 'include').as_posix() for p in prefixes)
        configure += ['-D{}={}'.format(k, 'ON' if v is True else 'OFF' if v is False else v) for k, v in options.items()]
        command.run(configure + flags)
        command.run([cmake, '--build', output, '--parallel', str(jobs), '--target', 'xl_dawn_probe' if probe else 'xl_dawn'])
        archive = output / ('xl_dawn.lib' if target == 'windows-x64' else 'libxl_dawn.a')
        library = prefix / 'lib' / archive.name
        library.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(archive, library)
        # Header paths depend on generated bytes, rather than unrelated backend
        # object/driver/compatibility changes. These headers are never public SDK types.
        header_root, header_key = publish_headers(source, output, target, references)
        references.append(header_root.relative_to(BUILD).as_posix())
        for name, spec in specs.items():
            for relative in spec['license_files']:
                destination = prefix / 'share/licenses' / name / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(paths[name] / relative, destination)
        stamp = dict(name='dawn', key=key, state='compiled-and-installed', identity=identity, compiler=compiler,
                     prefix=prefix.relative_to(BUILD).as_posix(), work=work.relative_to(BUILD).as_posix(),
                     source_sha256=specs['dawn']['sha256'], libraries=[library.relative_to(prefix).as_posix()],
                     header_root=header_root.relative_to(BUILD).as_posix(), header_key=header_key,
                     integration_status='not-verified', finished_at=time.time())
        if probe:
            executable = output / ('xl_dawn_probe.exe' if target == 'windows-x64' else 'xl_dawn_probe')
            command.run([executable])
            if sha256(ROOT / 'tests/graphics/dawn_native.cpp') != probe_hash:
                raise RuntimeError('Dawn probe source changed during build/verification')
            stamp['integration_status'] = 'native-probe-passed'
            stamp['probe_source_sha256'] = probe_hash
            stamp['probe_graphics'] = {name: s['key'] for name, s in graphics.items()}
        if any(identity[name] != value for name, value in declared_inputs(target).items()):
            raise RuntimeError('Dawn dependency recipe/source lock/patch changed during build; reuse objects in a fresh invocation')
        stamp['artifact_references'] = references + [work.relative_to(BUILD).as_posix()]
        write_json(prefix / '.build.json', stamp)
        write_json(report, stamp)
        artifacts.register(BUILD, prefix, 'dependency-install/' + target + '/dawn/' + configuration, key,
                           references + [work.relative_to(BUILD).as_posix()])
        print('Dawn/Tint private closure built: ' + key, flush=True)
        return stamp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('fetch', 'build', 'probe'))
    parser.add_argument('--target', default='windows-x64' if os.name == 'nt' else 'linux-x64-musl',
                        choices=('windows-x64', 'linux-x64-musl', 'linux-x64-glibc-dev'))
    parser.add_argument('--configuration', choices=('Debug', 'Release'), default='Debug')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    try:
        if args.command == 'fetch':
            sources()
        elif os.name == 'nt' and args.target.startswith('linux-'):
            from ci import linux_path
            import subprocess
            return subprocess.call(['wsl.exe', '-d', 'Ubuntu', '--exec', 'python3', '-B', linux_path(Path(__file__))] + sys.argv[1:])
        else:
            build(args.target, args.configuration, max(1, min(args.jobs, 8)), args.command == 'probe')
        return 0
    finally:
        artifacts.collect(BUILD, 3, artifacts.pinned_content_keys(ROOT))


if __name__ == '__main__':
    sys.exit(main())
