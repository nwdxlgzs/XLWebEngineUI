#!/usr/bin/env python3
"""Fetch and inspect the complete locked source dependency set."""
import sys
sys.dont_write_bytecode = True
import argparse
import concurrent.futures
import contextlib
import hashlib
import json
import os
import subprocess
import time
import shutil
from pathlib import Path

from ci_support import read_json, write_json, WorkLock, checked_child
from dependency_support import ROOT, BUILD, extract_source, restore_overlay_files, tree_files
import artifacts


def dependency_compiler_identity(target, env, flags):
    from ci import toolchain_key
    _, versions = toolchain_key(target, env, flags)
    scripts = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
               for path in sorted((ROOT / "cmake/toolchains").glob("*.cmake"))}
    identity = {"target": target, "versions": versions, "scripts": scripts}
    return hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()[:12], versions


SKIA_REQUIREMENTS = ["zlib", "libpng", "libjpeg-turbo", "libwebp", "freetype", "harfbuzz", "icu"]


def ninja_archive_inputs(output, targets, extension, env):
    """Collect only reachable archives, including with Ninja 1.10 hosts."""
    output = Path(output).resolve()
    command = ['ninja', '-C', str(output), '-t', 'inputs'] + list(targets)
    result = subprocess.run(command, text=True, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode == 0:
        values = result.stdout.splitlines()
    elif "unknown tool 'inputs'" in result.stderr:
        # Older Ninja still exposes the same target closure as a DOT graph.
        # Node labels are quoted file names; parse escaping instead of splitting
        # on spaces, and never include obsolete, unreachable work-tree outputs.
        import re
        graph = subprocess.check_output(['ninja', '-C', str(output), '-t', 'graph'] + list(targets), text=True, env=env)
        values = [json.loads('"' + match.group(1) + '"') for match in re.finditer(
            r'^\s*"[^"\n]+"\s+\[label="((?:[^"\\]|\\.)*)"', graph, re.M)]
    else:
        result.check_returncode()
    archives = set()
    for value in values:
        candidate = (output / value).resolve()
        try:
            candidate.relative_to(output)
        except ValueError:
            continue
        if candidate.suffix == extension:
            archives.add(candidate)
    return archives


def platform_stack(configuration, compiler_key):
    sources = read_json(ROOT / "third_party/platform-sources.lock.json")["dependencies"]
    recipes = read_json(ROOT / "third_party/recipes/x11.json")["components"]
    identity = {"sources": sources, "recipes": recipes, "compiler_key": compiler_key,
                "configuration": configuration, "recipe_driver_revision": 1}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    info = read_json(BUILD / "reports/platform-x11" / (configuration + ".json"), {})
    prefix = checked_child(BUILD, BUILD / info["prefix"]) if info.get("prefix") else None
    archives = ["X11", "xcb", "Xau", "Xdmcp", "Xext", "Xrender", "Xfixes", "Xcursor", "Xrandr", "Xi", "Xss"]
    if (info.get("state") != "passed" or info.get("key") != key or not prefix
            or not (prefix / "include/X11/Xlib.h").is_file()
            or not all((prefix / "lib" / ("lib" + name + ".a")).is_file() for name in archives)):
        raise RuntimeError("musl SDL needs the current native X11 stack: run tools/platform_deps.py x11 --configuration " + configuration)
    from platform_deps import extension_key
    extra_key = extension_key(key, compiler_key, configuration)
    extra = read_json(BUILD / "reports/platform-x11-extensions" / (configuration + ".json"), {})
    extra_prefix = checked_child(BUILD, BUILD / extra["prefix"]) if extra.get("prefix") else None
    if (extra.get("state") != "passed" or extra.get("key") != extra_key or not extra_prefix
            or not (extra_prefix / "lib/libXtst.a").is_file()):
        raise RuntimeError("musl SDL needs the current X11 extensions: run tools/platform_deps.py x11 --configuration " + configuration)
    return dict(info, key=extra_key, base_key=key, prefixes=[extra["prefix"], info["prefix"]], extensions=extra)


def platform_pkg_config(prefixes):
    return os.pathsep.join(str(p / relative) for p in prefixes for relative in ("lib/pkgconfig", "share/pkgconfig"))


def private_platform_flags(prefixes):
    runtime = read_json(BUILD / "toolchains/wrappers/linux-musl/toolchain.json")["runtime"]
    roots = ";".join(p.as_posix() for p in prefixes)
    libraries = [p.as_posix() for prefix in prefixes for p in sorted((prefix / "lib").glob("*.a"))]
    return ["-DCMAKE_PREFIX_PATH=" + roots,
            "-DCMAKE_FIND_ROOT_PATH=" + roots + ";" + (Path(runtime) / "sysroot").as_posix(),
            "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
            "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER",
            "-DCMAKE_REQUIRED_LIBRARIES=" + ";".join(["-Wl,--start-group"] + libraries + ["-Wl,--end-group"])]


def cmake_build_identity(spec, recipe, compiler_key, configuration, dependencies, platform_key=None):
    identity = {"source": spec["sha256"], "recipe": recipe, "toolchain": compiler_key,
                "dependency_keys": [item["key"] for item in dependencies], "configuration": configuration,
                "recipe_driver_revision": 2}
    if spec.get('patches'):
        from dependency_patches import patch_inputs
        active_patches = patch_inputs(spec, stage='runtime')
        if active_patches: identity['patches'] = active_patches
    if platform_key:
        identity["platform_key"] = platform_key
        identity["platform_find_policy"] = "private-roots-static-probes-v2"
    if recipe.get("recipe_directory"):
        directory = ROOT / recipe["recipe_directory"]
        identity["recipe_files"] = {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted(directory.rglob("*")) if p.is_file()}
    return identity


def skia_build_identity(spec, compiler_key, configuration, dependencies, target):
    identity = {"source": spec["sha256"], "toolchain": compiler_key, "configuration": configuration,
            "dependencies": {name: dependencies[name]["key"] for name in SKIA_REQUIREMENTS},
            "patch": read_json(ROOT / "third_party/patches/skia-private-dependencies.json"),
            "compatibility_patches": [read_json(ROOT / ("third_party/patches/" + filename))
                                      for filename in ("skia-win7-dwrite.json", "skia-win7-wic.json", "skia-icu-lifetime.json")],
            "recipe_version": 3,
            "backend_recipe": read_json(ROOT / 'third_party/recipes/skia.json'),
            "patch_driver": hashlib.sha256((ROOT / 'tools/dependency_patches.py').read_bytes()).hexdigest()}
    from dependency_patches import patch_inputs
    identity['patches'] = patch_inputs(spec)
    if identity['backend_recipe']['common'].get('skia_use_dawn'):
        from dawn_deps import declared_inputs
        identity['dawn_inputs'] = declared_inputs(target)
        identity['dawn_adapter'] = read_json(ROOT / 'third_party/patches/skia-dawn-prebuilt.json')
    identity['auxiliary'] = {name: record for name, record in read_json(ROOT / 'third_party/skia-sources.lock.json')['dependencies'].items()
                             if name != 'd3d12allocator' or target == 'windows-x64'}
    return identity


def installed_library_exists(prefix, relative):
    path = prefix / relative
    if path.is_symlink():
        # CMake installs libpng.a as an alias of libpng16.a. Allow a final
        # file alias only when both its parent and resolved target stay in
        # this private installation; directory links remain forbidden.
        if path.parent != prefix:
            checked_child(prefix, path.parent)
        path = path.resolve()
    return checked_child(prefix, path).is_file()


def select_compiled_components(target, configuration, compiler_key, names=None):
    """Resolve exact current recipes recursively; never select by newest mtime."""
    lock = read_json(ROOT / "third_party/dependencies.lock.json")["dependencies"]
    recipes = read_json(ROOT / "third_party/recipes/cmake.json")["components"]
    compiled = {}

    def select(name, ancestors=()):
        if name in compiled:
            return compiled[name]
        if name in ancestors:
            raise RuntimeError("dependency cycle: " + " -> ".join(ancestors + (name,)))
        requirements = SKIA_REQUIREMENTS if name == "skia" else recipes[name]["depends"]
        dependencies = [select(other, ancestors + (name,)) for other in requirements]
        if name == "skia":
            identity = skia_build_identity(lock[name], compiler_key, configuration, compiled, target)
        else:
            platform_key = platform_stack(configuration, compiler_key)["key"] if name == "sdl3" and target == "linux-x64-musl" else None
            identity = cmake_build_identity(lock[name], recipes[name], compiler_key, configuration, dependencies, platform_key)
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
        prefix = BUILD / "deps/install" / target / name / key / configuration
        stamp = read_json(prefix / ".build.json", {})
        if (stamp.get("key") != key or stamp.get("source_sha256") != lock[name]["sha256"]
                or stamp.get("prefix") != prefix.relative_to(BUILD).as_posix()
                or stamp.get("state") != "compiled-and-installed" or not stamp.get("libraries")
                or not all(installed_library_exists(prefix, item) for item in stamp["libraries"])):
            raise RuntimeError("current recipe needs compiled {}: run tools/deps.py build {} --target {} --configuration {}".format(name, name, target, configuration))
        compiled[name] = stamp
        return stamp

    for name in names or lock:
        select(name)
    return compiled


def verify_components(target, configuration, jobs, native=False, embedded_catalog=None):
    from ci import target_environment, Commands
    env, flags = target_environment(target)
    lock = read_json(ROOT / "third_party/dependencies.lock.json")["dependencies"]
    compiler_key, _ = dependency_compiler_identity(target, env, flags)
    compiled = select_compiled_components(target, configuration, compiler_key)
    platform = platform_stack(configuration, compiler_key) if target == "linux-x64-musl" else None
    test_inputs = [p for directory in ("tests/dependencies", "tests/resources", "src/resources", "src/base", "third_party/recipes/zstd-runtime")
                   for p in (ROOT / directory).rglob("*") if p.is_file()]
    test_inputs.append(ROOT / "tools/resource_pack.cpp")
    identity = {"components": {name: item["key"] for name, item in compiled.items()},
                "platform": platform["key"] if platform else None, "native": native,
                "verification_driver_revision": 2,
                "tests": {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(test_inputs)}}
    if embedded_catalog:
        embedded_catalog = checked_child(BUILD, Path(embedded_catalog).resolve())
        identity["embedded_catalog"] = read_json(embedded_catalog / "manifest.json")["key"]
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    latest = BUILD / "deps/integration" / target / configuration
    root = latest / key
    root.mkdir(parents=True, exist_ok=True)
    includes, libraries = [], []
    for name in lock:
        item = compiled[name]
        prefix = BUILD / item["prefix"]
        include = prefix / "include"
        if include.is_dir():
            includes.append(include.as_posix())
            for relative in ("freetype2", "harfbuzz", "libpng16", "webm"):
                if (include / relative).is_dir():
                    includes.append((include / relative).as_posix())
        libraries.extend((prefix / relative).as_posix() for relative in item["libraries"] if name != "sdl3")
    platform_libraries = []
    if platform:
        platform_prefixes = [BUILD / item for item in platform["prefixes"]]
        for platform_prefix in platform_prefixes:
            includes.append((platform_prefix / "include").as_posix())
            platform_libraries.extend(p.as_posix() for p in sorted((platform_prefix / "lib").glob("*.a")))
    def cmake_list(name,values):
        return "set(" + name + "\n" + "".join('  "' + value + '"\n' for value in values) + ")\n"
    manifest = root / "dependencies.cmake"
    manifest.write_text(cmake_list("XL_DEP_INCLUDE_DIRS",includes) + cmake_list("XL_DEP_LIBRARIES",libraries)
                        + cmake_list("XL_PLATFORM_LIBRARIES",platform_libraries)
                        + cmake_list("XL_SKIA_ROOT",[(BUILD / compiled["skia"]["source_overlay"]).as_posix()])
                        + cmake_list("XL_ZSTD_SOURCE",[(BUILD / "deps/src/zstd" / lock["zstd"]["sha256"][:12]).as_posix()])
                        + cmake_list("XL_SDL_PREFIX",[(BUILD / compiled["sdl3"]["prefix"]).as_posix()]),encoding="utf-8")
    command = Commands(root / "commands.log", env, verbose=False)
    if platform:
        command.env["PKG_CONFIG_LIBDIR"] = platform_pkg_config(platform_prefixes)
        command.env["PKG_CONFIG_PATH"] = command.env["PKG_CONFIG_LIBDIR"]
        flags = private_platform_flags(platform_prefixes) + flags
    report = {"target": target, "configuration": configuration, "components": compiled, "state": "running",
              "native_window_required": native, "platform": platform, "key": key,
              "work": (root / "work").relative_to(BUILD).as_posix()}
    references = [item["prefix"] for item in compiled.values()]
    if platform:
        references.extend(platform["prefixes"])
    if embedded_catalog:
        references.append(embedded_catalog.relative_to(BUILD).as_posix())
        flags.append("-DXL_EMBEDDED_CATALOG=" + embedded_catalog.as_posix())
    else:
        flags.append("-DXL_EMBEDDED_CATALOG=")
    artifacts.register(BUILD, root, "dependency-integration/{}/{}".format(target, configuration), key, references)
    result = 0
    try:
        with WorkLock(BUILD / "state" / ("dependency-verify-" + target + "-" + configuration + ".lock")), contextlib.ExitStack() as leases:
            leases.enter_context(artifacts.lease(BUILD, root))
            for item in compiled.values():
                leases.enter_context(artifacts.lease(BUILD, BUILD / item["prefix"]))
            if platform:
                for platform_prefix in platform_prefixes:
                    leases.enter_context(artifacts.lease(BUILD, platform_prefix))
            if embedded_catalog:
                leases.enter_context(artifacts.lease(BUILD, embedded_catalog))
            command.run(["cmake", "-S", ROOT / "tests/dependencies", "-B", root / "work", "-G", "Ninja",
                         "-DCMAKE_BUILD_TYPE=" + configuration, "-DXL_DEPENDENCY_MANIFEST=" + manifest.as_posix(),
                         "-DXL_TEST_NATIVE_WINDOW=" + ("ON" if native else "OFF")] + flags)
            command.run(["cmake", "--build", root / "work", "--parallel", str(jobs)])
            import bootstrap
            decoder = root / "work/zstd-runtime" / ("xl_zstd_decoder.lib" if target == "windows-x64" else "libxl_zstd_decoder.a")
            symbols = subprocess.check_output([str(bootstrap.llvm_bin() / ("llvm-nm.exe" if os.name == "nt" else "llvm-nm")),
                                               "--defined-only", "--extern-only", str(decoder)], env=env, text=True)
            if any(name in symbols for name in (" ZSTD_compress", " ZSTD_createCCtx", " ZDICT_")):
                raise RuntimeError("runtime decompressor unexpectedly contains compression or dictionary training")
            (root / "decoder-symbols.txt").write_text(symbols, encoding="utf-8")
            report["decoder_compression_symbols"] = False
            if target == "linux-x64-musl":
                import re
                abi = {}
                executables = ["xl_dependency_integration", "xl_resource_service_tests", "xl_resource_pack"]
                if native:
                    executables.append("xl_native_window")
                if embedded_catalog:
                    executables.append("xl_builtin_resources")
                for name in executables:
                    executable = root / "work" / name
                    dynamic = subprocess.check_output(["readelf", "-dW", executable], env=env, text=True)
                    versions = subprocess.check_output(["readelf", "--version-info", executable], env=env, text=True)
                    program = subprocess.check_output(["readelf", "-lW", executable], env=env, text=True)
                    needed = re.findall(r"\(NEEDED\).*?\[([^]]+)\]", dynamic)
                    if (set(needed) - {"libc.so"} or any(token in versions for token in ("GLIBC_", "GLIBCXX_", "CXXABI_"))
                            or "ld-musl-x86_64.so.1" not in program):
                        raise RuntimeError("unexpected target ABI dependency in " + name + ": " + ", ".join(needed))
                    (root / (name + "-elf.txt")).write_text(dynamic + versions + program, encoding="utf-8")
                    abi[name] = {"needed": needed, "interpreter": "ld-musl-x86_64.so.1", "gnu_versions": False}
                report["musl_abi"] = abi
            command.run(["ctest", "--test-dir", root / "work", "--output-on-failure", "--no-tests=error"])
            report["state"] = "passed"
            print("All compiled dependencies passed real function calls",flush=True)
    except Exception as error:
        report.update(state="failed",error=str(error))
        print(str(error),file=sys.stderr,flush=True)
        result = 1
    write_json(root / "report.json",report)
    write_json(latest / "report.json", report)
    return result


def build_skia(target, configuration, jobs):
    import bootstrap
    from ci import target_environment, Commands
    env, flags = target_environment(target)
    compiler_key, versions = dependency_compiler_identity(target, env, flags)
    lock = read_json(ROOT / "third_party/dependencies.lock.json")["dependencies"]
    spec = lock["skia"]
    source = extract_source(spec, "skia")
    requirements = SKIA_REQUIREMENTS
    dependencies = select_compiled_components(target, configuration, compiler_key, requirements)
    dawn = None
    if read_json(ROOT / 'third_party/recipes/skia.json')['common'].get('skia_use_dawn'):
        from dawn_deps import build as build_dawn
        dawn = build_dawn(target, configuration, jobs)
    patch = read_json(ROOT / "third_party/patches/skia-private-dependencies.json")
    identity = skia_build_identity(spec, compiler_key, configuration, dependencies, target)
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    work = BUILD / "deps/work" / target / "skia" / key / configuration
    # Publish immutable prefixes by their complete input key, while allowing
    # Ninja to reuse unchanged objects at a stable source path for this compiler
    # and upstream revision. A work tree is never evidence of success by itself.
    for marker in sorted((BUILD / 'deps/work' / target / 'skia').glob('*/' + configuration + '/source/.xl-overlay.json')):
        previous = read_json(marker, {})
        if all(previous.get(field) == identity[field] for field in ('source', 'toolchain', 'configuration')):
            work = marker.parent.parent
            break
    overlay = work / "source"
    output = work / "out"
    prefix = BUILD / "deps/install" / target / "skia" / key / configuration
    references = [source.relative_to(BUILD).as_posix()] + [dependencies[name]["prefix"] for name in requirements]
    if dawn:
        references.append(dawn['prefix'])
    auxiliary = {}
    for name, record in identity['auxiliary'].items():
        auxiliary[name] = extract_source(record, name)
        references.append(auxiliary[name].relative_to(BUILD).as_posix())
    work.mkdir(parents=True, exist_ok=True)
    artifacts.register(BUILD, work, "dependency-work/{}/skia/{}".format(target,configuration), key, references)
    with WorkLock(BUILD / "state" / ("skia-" + target + "-" + configuration + ".lock")), contextlib.ExitStack() as leases:
        for path in [source, work] + list(auxiliary.values()) + [BUILD / dependencies[name]["prefix"] for name in requirements]:
            leases.enter_context(artifacts.lease(BUILD, path))
        if dawn:
            leases.enter_context(artifacts.lease(BUILD, BUILD / dawn['prefix']))
        if overlay.is_dir(): restore_overlay_files(source, overlay)
        stamp = read_json(prefix / ".build.json", {})
        reusable = (stamp.get("key") == key and stamp.get("state") == "compiled-and-installed"
                and stamp.get("source_sha256") == spec["sha256"] and stamp.get("libraries")
                and (not auxiliary or stamp.get('library_capture') == ('ninja-target-closure-v1' if dawn else 'recursive-closure-v1'))
                and all(installed_library_exists(prefix, name) for name in stamp["libraries"]))
        if not (overlay / ".xl-overlay.json").is_file():
            print("Preparing Skia per-target source overlay", flush=True)
            if overlay.exists():
                shutil.rmtree(checked_child(BUILD, overlay))
            shutil.copytree(source, overlay, symlinks=True, copy_function=shutil.copyfile)
        include_directories = []
        for name in requirements:
            directory = BUILD / dependencies[name]["prefix"] / "include"
            include_directories.append(directory.as_posix())
            for relative in ("freetype2", "harfbuzz", "libpng16"):
                if (directory / relative).is_dir():
                    include_directories.append((directory / relative).as_posix())
        mapping = patch['targets']
        generated = []
        for target_name, names in mapping.items():
            libraries = []
            for name in names:
                libraries.extend((BUILD / dependencies[name]["prefix"] / relative).as_posix()
                                 for relative in dependencies[name]["libraries"])
            generated.append('    if (target_name == ' + json.dumps(target_name) + ') {\n')
            generated.append('      include_dirs = []\n      libs = []\n')
            generated.append('      include_dirs = ' + json.dumps(include_directories) + '\n')
            generated.append('      libs = ' + json.dumps(libraries) + '\n')
            generated.append('    }\n')
        patch_file = overlay / patch["file"]
        original = (source / patch["file"]).read_text(encoding="utf-8")
        if original.count(patch["anchor"]) != patch["occurrences"]:
            raise RuntimeError("Skia private-dependency patch anchor differs from locked source")
        patched = original.replace(patch["anchor"], patch["anchor"] + "".join(generated))
        if not patch_file.is_file() or patch_file.read_text(encoding="utf-8") != patched:
            patch_file.write_text(patched, encoding="utf-8")
        write_json(overlay / ".xl-overlay.json", identity)
        for name, directory in auxiliary.items():
            destination = overlay / 'third_party/externals' / name
            marker = destination / '.xl-source-key'
            expected = identity['auxiliary'][name]['sha256']
            if not marker.is_file() or marker.read_text(encoding='utf-8') != expected:
                shutil.copytree(directory, destination, dirs_exist_ok=True, symlinks=True)
                marker.write_text(expected, encoding='utf-8')
            else:
                restore_overlay_files(directory, destination)
        from dependency_patches import apply_patch_file
        apply_patch_file(source, overlay, 'third_party/patches/skia-icu-lifetime.json')
        recipe = identity['backend_recipe']
        args = dict(recipe['common'], **recipe.get(target, {}))
        args.update(is_debug=configuration == 'Debug', is_official_build=configuration == 'Release',
                    extra_cflags=['-DU_STATIC_IMPLEMENTATION', '-DU_LIB_SUFFIX_C_NAME=_xl'])
        if dawn:
            apply_patch_file(source, overlay, 'third_party/patches/skia-dawn-prebuilt.json')
            dawn_prefix = BUILD / dawn['prefix']
            dawn_headers = BUILD / dawn['header_root']
            args.update(xl_dawn_include_dirs=[(dawn_headers / p).as_posix() for p in ('include', 'private')],
                        xl_dawn_static_library=(dawn_prefix / dawn['libraries'][0]).as_posix(),
                        dawn_enable_d3d11=target == 'windows-x64', dawn_enable_d3d12=target == 'windows-x64',
                        dawn_enable_vulkan=True, dawn_enable_opengles=True)
        if target == "windows-x64":
            args.update(target_os="win", clang_win=bootstrap.llvm_directory().as_posix(), clang_win_version="23",
                        win_vc=Path(env["VCINSTALLDIR"]).as_posix().rstrip("/"),
                        win_toolchain_version=env["VCTOOLSVERSION"].rstrip("\\/"),
                        win_sdk=Path(env["WINDOWSSDKDIR"]).as_posix().rstrip("/"),
                        win_sdk_version=env["WINDOWSSDKVERSION"].rstrip("\\/"))
            args["extra_cflags"].extend(["/MTd" if configuration == "Debug" else "/MT", "/Z7", "-DWINVER=0x0601", "-D_WIN32_WINNT=0x0601"])
            # Upstream SkDWriteNTDDI_VERSION.h deliberately unsets these
            # macros when NTDDI_VERSION is supplied so modern COM interface
            # declarations stay available while runtime interface probing
            # still supports older systems.
            args["extra_cflags"].append("-DNTDDI_VERSION=0x06010000")
            for filename in ('skia-win7-dwrite.json', 'skia-win7-wic.json', 'skia-optional-d3dcompiler.json', 'skia-win7-d3d-runtime.json'):
                apply_patch_file(source, overlay, 'third_party/patches/' + filename)
        else:
            wrappers = BUILD / "toolchains/wrappers/linux-musl" if target == "linux-x64-musl" else bootstrap.llvm_bin()
            args.update(target_os="linux", cc=str(wrappers / ("cc" if target == "linux-x64-musl" else "clang")),
                        cxx=str(wrappers / ("cxx" if target == "linux-x64-musl" else "clang++")),
                        ar=str(wrappers / ("ar" if target == "linux-x64-musl" else "llvm-ar")),
                        skia_use_system_freetype2=True, skia_use_x11=False,
                        skia_enable_fontmgr_custom_directory=True, skia_enable_fontmgr_custom_embedded=True)
        if reusable:
            print("Skia current locked build reused: " + key, flush=True)
            return 0
        output.mkdir(exist_ok=True)
        (output / "args.gn").write_text("\n".join(name + " = " + json.dumps(value) for name,value in args.items()) + "\n",encoding="utf-8")
        command = Commands(work / "commands.log", env, verbose=False)
        command.run([bootstrap.prepare_gn(), "gen", output], cwd=overlay)
        command.run(["ninja", "-C", output, "-j", str(jobs)] + recipe['build_targets'], cwd=overlay)
        prefix.mkdir(parents=True, exist_ok=True)
        (prefix / "lib").mkdir(exist_ok=True)
        libraries = []
        extension = ".lib" if target == "windows-x64" else ".a"
        if dawn:
            # A reusable Ninja tree can contain obsolete archives from disabled
            # edges. Capture only the current requested targets' real inputs and
            # products, excluding Dawn's separately published closure.
            archive_outputs = ninja_archive_inputs(output, recipe['build_targets'], extension, env)
            for name in recipe['build_targets']:
                candidate = output / ((name if target == 'windows-x64' else 'lib' + name) + extension)
                if not candidate.is_file():
                    raise RuntimeError('Skia requested archive missing: ' + name)
                archive_outputs.add(candidate)
        else:
            archive_outputs = output.rglob('*' + extension) if auxiliary else output.glob('*' + extension)
        for path in sorted(archive_outputs):
            path = path.resolve()
            relative = path.relative_to(output.resolve())
            destination = prefix / 'lib' / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
            libraries.append((Path('lib') / relative).as_posix())
        if not libraries:
            raise RuntimeError("Skia build produced no static libraries")
        stamp = {"name": "skia", "key": key, "state": "compiled-and-installed", "libraries": libraries,
                 "prefix": prefix.relative_to(BUILD).as_posix(), "source_sha256": spec["sha256"],
                 "source_overlay": overlay.relative_to(BUILD).as_posix(), "compiler": versions,
                 "dependencies": requirements, "gn_args": args, "integration_status": "not-verified", "finished_at": time.time()}
        stamp['auxiliary_sources'] = {name: dict(identity['auxiliary'][name], source=directory.relative_to(BUILD).as_posix())
                                      for name, directory in auxiliary.items()}
        stamp['library_capture'] = 'ninja-target-closure-v1' if dawn else 'recursive-closure-v1' if auxiliary else 'root-outputs-v1'
        if dawn:
            stamp['dawn'] = dawn
        write_json(prefix / ".build.json", stamp)
        artifacts.register(BUILD, prefix, "dependency-install/{}/skia/{}".format(target,configuration), key,
                           references + [work.relative_to(BUILD).as_posix()])
        write_json(BUILD / "reports/skia-builds" / target / (configuration + ".json"), stamp)
        print("Skia compiled; SDK integration and disabled auxiliary Skia modules remain to be completed", flush=True)
        return 0


def build_components(names, lock, recipes, target, configuration, jobs):
    from ci import target_environment, toolchain_key, Commands
    env, flags = target_environment(target)
    compiler_key, versions = dependency_compiler_identity(target, env, flags)
    report_path = BUILD / "reports/dependency-builds" / target / (configuration + ".json")
    previous = read_json(report_path, {}).get("components", {})
    retained = {name: item for name,item in previous.items() if name in lock
                and item.get("source_sha256") == lock[name]["sha256"]
                and (BUILD / item["prefix"] / ".build.json").is_file()}
    report = {"target": target, "configuration": configuration, "compiler": versions, "components": retained,
              "requested_components": names, "errors": [], "state": "running"}
    completed = {}
    failures = {}

    def attempt_build(name, ancestors=()):
        if name in completed:
            return completed[name]
        if name in ancestors:
            raise RuntimeError("dependency cycle: " + " -> ".join(ancestors + (name,)))
        if name not in recipes:
            raise RuntimeError(name + ": dedicated build recipe not implemented yet")
        recipe, spec = recipes[name], lock[name]
        dependencies = [build(other, ancestors + (name,)) for other in recipe["depends"]]
        platform_prefixes = []
        platform_key = None
        if name == "sdl3" and target == "linux-x64-musl":
            platform_info = platform_stack(configuration, compiler_key)
            platform_prefixes = [BUILD / item for item in platform_info["prefixes"]]
            platform_key = platform_info["key"]
        source_record = fetch_component(name, spec)
        source = BUILD / source_record["source"]
        identity = cmake_build_identity(spec, recipe, compiler_key, configuration, dependencies, platform_key)
        recipe_source = ROOT / recipe["recipe_directory"] if recipe.get("recipe_directory") else None
        key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
        work = BUILD / "deps/work" / target / name / key / configuration
        cached = previous.get(name, {})
        if cached.get('source_sha256') == spec['sha256'] and cached.get('compiler') == versions and cached.get('key'):
            candidate = BUILD / cached['work'] if cached.get('work') else BUILD / 'deps/work' / target / name / cached['key'] / configuration
            checked_child(BUILD / 'deps/work' / target / name, candidate)
            if (candidate / 'CMakeCache.txt').is_file(): work = candidate
        prefix = BUILD / "deps/install" / target / name / key / configuration
        work.mkdir(parents=True, exist_ok=True)
        prefix.mkdir(parents=True, exist_ok=True)
        references = [source.relative_to(BUILD).as_posix()] + [item["prefix"] for item in dependencies]
        references.extend(p.relative_to(BUILD).as_posix() for p in platform_prefixes)
        artifacts.register(BUILD, work, "dependency-work/{}/{}/{}".format(target,name,configuration), key, references)
        artifacts.register(BUILD, prefix, "dependency-install/{}/{}/{}".format(target,name,configuration), key,
                           references + [work.relative_to(BUILD).as_posix()])
        with WorkLock(BUILD / "state" / ("dep-" + target + "-" + name + "-" + configuration + ".lock")), contextlib.ExitStack() as leases:
            for path in (source, work, prefix):
                leases.enter_context(artifacts.lease(BUILD, path))
            for dependency in dependencies:
                leases.enter_context(artifacts.lease(BUILD, BUILD / dependency["prefix"]))
            for platform_prefix in platform_prefixes:
                leases.enter_context(artifacts.lease(BUILD,platform_prefix))
            stamp = read_json(prefix / ".build.json", {})
            valid = (stamp.get("key") == key and stamp.get("state") == "compiled-and-installed"
                     and stamp.get("libraries") and all((prefix / item).is_file() for item in stamp["libraries"]))
            installed = stamp.get('installed_files', [])
            if not installed and stamp.get('key') == key and (work / 'install_manifest.txt').is_file():
                try:
                    installed = [Path(line).relative_to(prefix).as_posix() for line in
                                 (work / 'install_manifest.txt').read_text(encoding='utf-8').splitlines()]
                except ValueError:
                    installed = []  # A reused work tree last installed another immutable prefix.
            if not installed or set(installed) - tree_files(prefix): valid = False
            if not valid:
                work_identity = work / '.build-identity.json'
                if read_json(work_identity, {}) != identity:
                    # Reconfigure from fresh cache values while retaining Ninja
                    # objects. Removed options and changed toolchain settings
                    # must not survive solely because a tree was reused.
                    (work / 'CMakeCache.txt').unlink(missing_ok=True)
                    for directory in (work / 'CMakeFiles').glob('*'):
                        import re
                        if directory.is_dir() and re.fullmatch(r'\d+\.\d+(?:\.\d+)?', directory.name):
                            shutil.rmtree(checked_child(BUILD, directory))
                    write_json(work_identity, identity)
                command = Commands(work / "commands.log", env, verbose=False)
                if platform_prefixes:
                    command.env["PKG_CONFIG_LIBDIR"] = platform_pkg_config(platform_prefixes)
                    command.env["PKG_CONFIG_PATH"] = command.env["PKG_CONFIG_LIBDIR"]
                if recipe.get("generator"):
                    command.run([sys.executable, "-B", recipe_source / recipe["generator"], "--source", source,
                                 "--build", work / "generated"])
                configure = ["cmake", "-S", recipe_source or (source / recipe.get("subdirectory", ".")),
                             "-B", work, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + configuration,
                             "-DCMAKE_INSTALL_PREFIX=" + prefix.as_posix(), "-DCMAKE_INSTALL_LIBDIR=lib",
                             "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_TESTING=OFF",
                             "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
                             "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF", "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
                             "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded" + ("Debug" if configuration == "Debug" else "")]
                if recipe_source:
                    configure.append("-DXL_DEPENDENCY_SOURCE=" + source.as_posix())
                closure = []
                def visit(item):
                    if item["prefix"] not in closure:
                        closure.append(item["prefix"])
                    for child in item.get("dependencies", []):
                        visit(completed[child])
                for item in dependencies:
                    visit(item)
                if closure:
                    configure.append("-DCMAKE_PREFIX_PATH=" + ";".join((BUILD / p).as_posix() for p in closure))
                if "zlib" in completed and completed["zlib"]["prefix"] in closure:
                    zlib = completed["zlib"]
                    configure.extend(["-DZLIB_LIBRARY=" + (BUILD / zlib["prefix"] / zlib["libraries"][0]).as_posix(),
                                      "-DZLIB_INCLUDE_DIR=" + (BUILD / zlib["prefix"] / "include").as_posix()])
                for option, value in recipe["options"].items():
                    configure.append("-D{}={}".format(option, "ON" if value is True else "OFF" if value is False else value))
                if platform_prefixes:
                    configure.extend(private_platform_flags(platform_prefixes) + ["-DSDL_X11=ON", "-DSDL_X11_SHARED=OFF"])
                command.run(configure + flags)
                command.run(["cmake", "--build", work, "--parallel", str(jobs)])
                command.run(["cmake", "--install", work])
                libraries = sorted(p.relative_to(prefix).as_posix() for p in (prefix / "lib").rglob("*") if p.suffix.lower() in (".a", ".lib"))
                if not libraries:
                    raise RuntimeError("no static libraries installed for " + name)
                stamp = {"name": name, "key": key, "state": "compiled-and-installed", "libraries": libraries,
                         "prefix": prefix.relative_to(BUILD).as_posix(), "source_sha256": spec["sha256"],
                         'work': work.relative_to(BUILD).as_posix(),
                         "recipe": recipe, "compiler": versions, "dependencies": recipe["depends"],
                         "integration_status": "not-verified", "finished_at": time.time()}
                stamp['installed_files'] = sorted(tree_files(prefix) - {'.build.json'})
                write_json(prefix / ".build.json", stamp)
            completed[name] = stamp
            report["components"][name] = stamp
            write_json(report_path, report)
            print(name + ": compiled and installed; final SDK integration still pending", flush=True)
        return stamp

    def build(name, ancestors=()):
        if name in failures:
            raise RuntimeError("{}: blocked by its earlier build failure: {}".format(name, failures[name]))
        try:
            return attempt_build(name, ancestors)
        except Exception as error:
            failures[name] = str(error)
            raise

    for name in names:
        try:
            build(name)
        except Exception as error:
            report["errors"].append({"component": name, "error": str(error)})
            print(str(error), file=sys.stderr, flush=True)
    report["state"] = "failed" if report["errors"] else "passed"
    write_json(report_path, report)
    return 1 if report["errors"] else 0


def fetch_component(name, spec):
    source = extract_source(spec, name)
    notices = []
    for relative in spec["license_files"]:
        path = source / relative
        if not path.is_file():
            raise RuntimeError("{}: declared license file missing: {}".format(name, relative))
        notices.append(str(path.relative_to(BUILD)))
    return {"component": name, "source": str(source.relative_to(BUILD)), "sha256": spec["sha256"],
            "revision": spec["revision"], "license": spec["selected_license"], "license_files": notices,
            "source_status": "verified-and-extracted", "build_status": "not-verified", "integration_status": "not-verified"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("fetch", "status", "build", "skia", "verify"))
    parser.add_argument("components", nargs="*")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--target", choices=("windows-x64", "linux-x64-musl", "linux-x64-glibc-dev"),
                        default="windows-x64" if os.name == "nt" else "linux-x64-musl")
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--native", action="store_true", help="Require real native window presentation and embedding during verify")
    parser.add_argument("--embedded-catalog", type=Path, help="Also verify a generated compressed ICU catalog")
    args = parser.parse_args()
    lock = read_json(ROOT / "third_party/dependencies.lock.json")["dependencies"]
    names = args.components or sorted(lock)
    unknown = set(names) - set(lock)
    if unknown or args.jobs < 1:
        parser.error("unknown components or invalid job count: " + ", ".join(sorted(unknown)))
    if args.command in ("build", "skia", "verify", "status"):
        if os.name == "nt" and args.target.startswith("linux-"):
            script = subprocess.check_output(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-a", "-u", str(Path(__file__).resolve())], text=True).strip()
            forwarded = list(sys.argv[1:])
            if args.embedded_catalog:
                index = forwarded.index("--embedded-catalog") + 1
                forwarded[index] = subprocess.check_output(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-a", "-u", str(args.embedded_catalog.resolve())], text=True).strip()
            return subprocess.call(["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B", script] + forwarded)
        if args.command == "status":
            from ci import target_environment
            env, flags = target_environment(args.target)
            compiler_key, _ = dependency_compiler_identity(args.target, env, flags)
            for name in names:
                record = read_json(BUILD / "reports/dependencies" / (name + ".json"), {})
                try:
                    compiled = select_compiled_components(args.target, args.configuration, compiler_key, [name])[name]
                    state = compiled["state"] + " " + compiled["key"]
                except RuntimeError as error:
                    state = "current-recipe-not-built (" + str(error) + ")"
                print("{} {}: source={}, {} {} build={}".format(name, lock[name]["revision"], record.get("source_status", "not-fetched"),
                      args.target, args.configuration, state))
            return 0
        if args.command in ("skia", "verify"):
            try:
                if args.command == "verify":
                    return verify_components(args.target, args.configuration, min(args.jobs, 8), args.native, args.embedded_catalog)
                return build_skia(args.target, args.configuration, min(args.jobs, 8))
            except Exception as error:
                print(args.command + ": " + str(error), file=sys.stderr)
                return 1
        recipes = read_json(ROOT / "third_party/recipes/cmake.json")["components"]
        ordinary = [name for name in names if name != "skia"]
        if "skia" in names:
            ordinary = list(dict.fromkeys(ordinary + ["zlib", "libpng", "libjpeg-turbo", "libwebp", "freetype", "harfbuzz", "icu"]))
        result = build_components(ordinary, lock, recipes, args.target, args.configuration, min(args.jobs, 8))
        if not result and "skia" in names:
            try:
                result = build_skia(args.target, args.configuration, min(args.jobs, 8))
            except Exception as error:
                print("Skia: " + str(error), file=sys.stderr)
                result = 1
        return result
    errors = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(args.jobs, 6)) as pool:
        futures = {pool.submit(fetch_component, name, lock[name]): name for name in names}
        for future in concurrent.futures.as_completed(futures):
            name = futures[future]
            try:
                record = future.result()
                write_json(BUILD / "reports/dependencies" / (name + ".json"), record)
                print(name + ": verified source and license files", flush=True)
            except Exception as error:
                errors.append({"component": name, "error": str(error)})
                print(name + ": " + str(error), file=sys.stderr, flush=True)
    write_json(BUILD / "reports/dependency-fetch.json", {"state": "failed" if errors else "passed", "components": names, "errors": errors})
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
