#!/usr/bin/env python3
"""Prepare the locked private libraries and embedded data used by the SDK."""
import sys
sys.dont_write_bytecode = True
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil

from ci_support import WorkLock, read_json, write_json
from dependency_support import ROOT, BUILD, extract_source
import artifacts


def prepare(target, configuration, jobs):
    from ci import target_environment, Commands
    from deps import (build_components, build_skia, SKIA_REQUIREMENTS, dependency_compiler_identity,
                      select_compiled_components, platform_stack)
    from embed_resources import generate
    env, flags = target_environment(target)
    compiler_key, compiler = dependency_compiler_identity(target, env, flags)
    lock = read_json(ROOT / "third_party/dependencies.lock.json")["dependencies"]
    recipes = read_json(ROOT / "third_party/recipes/cmake.json")["components"]
    zstd = extract_source(lock["zstd"], "zstd")
    inputs = [ROOT / "tools/resource_pack.cpp", ROOT / "tools/resource-pack/CMakeLists.txt"]
    identity = {"compiler": compiler_key, "source": lock["zstd"]["sha256"],
                "recipes": {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    encoder_key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]
    work = BUILD / "host-tools/resource-pack" / target / encoder_key
    work.mkdir(parents=True, exist_ok=True)
    artifacts.register(BUILD, work, "resource-encoder/" + target, encoder_key, [zstd.relative_to(BUILD).as_posix()])
    encoder = work / ("xl_resource_pack.exe" if os.name == "nt" else "xl_resource_pack")
    with WorkLock(BUILD / "state" / ("sdk-inputs-" + target + ".lock")), artifacts.lease(BUILD, work):
        command = Commands(work / "commands.log", env, verbose=False)
        command.run(["cmake", "-S", ROOT / "tools/resource-pack", "-B", work, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                     "-DXL_ZSTD_SOURCE=" + zstd.as_posix()] + flags)
        command.run(["cmake", "--build", work, "--parallel", str(jobs)])
        generated = BUILD / "generated/sdk-resources" / target
        if target == 'linux-x64-musl':
            try:
                platform_stack(configuration, compiler_key)
            except RuntimeError:
                from platform_deps import build_x11, build_extensions
                if build_x11(configuration, jobs) or build_extensions(configuration, jobs):
                    raise RuntimeError('SDK private X11 build failed')
                platform_stack(configuration, compiler_key)
        if build_components(['sdl3', 'lexbor', 'quickjs-ng'] + SKIA_REQUIREMENTS, lock, recipes, target, configuration, jobs):
            raise RuntimeError("SDK graphics dependency build failed")
        if build_skia(target, configuration, jobs):
            raise RuntimeError('SDK Skia build failed')
        compiled = select_compiled_components(target, configuration, compiler_key, ['sdl3', 'skia', 'lexbor', 'quickjs-ng'])
        icu = BUILD / compiled["icu"]["prefix"]
        from icu_data import prepare as prepare_icu_data
        filtered = prepare_icu_data(extract_source(lock['icu'], 'icu'), icu, target, configuration, env, flags, jobs)
        resource_manifest = generate(ROOT / "assets/resources.json", encoder, generated, {'chromium-common': filtered})
        manifest = BUILD / "generated/sdk-inputs" / target / configuration / "inputs.cmake"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        include_dirs = [BUILD / compiled['skia']['source_overlay']]
        graphics, c_archives, platform_archives, platform_prefixes = [], [], [], []
        static_graphics = {}
        dawn = compiled['skia'].get('dawn')
        if dawn:
            dawn_prefix = BUILD / dawn['prefix']
            dawn_headers = BUILD / dawn['header_root']
            include_dirs += [dawn_headers / 'include', dawn_headers / 'private']
            graphics += [dawn_prefix / relative for relative in dawn['libraries']]
        if target == 'windows-x64' or target.startswith('linux-'):
            from graphics_deps import build_angle, build_cmake_graphics
            static_graphics['angle'] = build_angle(target,configuration,jobs)
            static_graphics['swiftshader'] = build_cmake_graphics('swiftshader',target,configuration,jobs)
            if target == 'windows-x64':
                static_graphics['dxcompiler'] = build_cmake_graphics('dxcompiler',target,configuration,jobs)
            for component in static_graphics.values():
                prefix = BUILD / component['prefix']
                include_dirs.append(prefix / 'include')
                graphics += [prefix / relative for relative in component['libraries']]
        references = [work.relative_to(BUILD).as_posix(), generated.relative_to(BUILD).as_posix(), zstd.relative_to(BUILD).as_posix()]
        references.append(filtered.parent.relative_to(BUILD).as_posix())
        references.extend(component['prefix'] for component in static_graphics.values())
        if dawn:
            references.append(dawn['prefix'])
        for name, component in compiled.items():
            prefix = BUILD / component['prefix']
            references.append(component['prefix'])
            if (prefix / 'include').is_dir():
                include_dirs.append(prefix / 'include')
            if name == 'icu':
                continue
            for relative in component['libraries']:
                filename = Path(relative).name
                # Select the actual Skia closure, avoiding duplicate decoder/alias archives.
                if name == 'brotli' and 'enc' in filename:
                    continue
                if name == 'harfbuzz' and filename not in ('harfbuzz.lib', 'libharfbuzz.a'):
                    continue
                if name == 'libwebp' and 'webpdecoder' in filename:
                    continue
                path = prefix / relative
                if path.is_symlink():
                    continue
                graphics.append(path)
                if name != 'skia':
                    c_archives.append(path)
        if target == 'linux-x64-musl':
            platform = platform_stack(configuration, compiler_key)
            platform_prefixes = [BUILD / item for item in platform['prefixes']]
            references.extend(platform['prefixes'])
            platform_archives = [p for prefix in platform_prefixes for p in sorted((prefix / 'lib').glob('*.a')) if not p.is_symlink()]
            c_archives += platform_archives
        license_root = manifest.parent / 'licenses'
        for component in static_graphics.values():
            shutil.copytree(BUILD / component['prefix'] / 'share/licenses',license_root,dirs_exist_ok=True)
        if dawn:
            shutil.copytree(BUILD / dawn['prefix'] / 'share/licenses', license_root, dirs_exist_ok=True)
        for filename in ('CHROMIUM-LICENSE', 'ICU-LICENSE'):
            destination = license_root / 'icu-chromium' / filename
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / 'third_party/icu-chromium' / filename, destination)
        for name in compiled:
            source = extract_source(lock[name], name)
            references.append(source.relative_to(BUILD).as_posix())
            for relative in lock[name]['license_files']:
                destination = license_root / name / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source / relative, destination)
        skcms_license = BUILD / compiled['skia']['source_overlay'] / 'modules/skcms/LICENSE'
        if skcms_license.is_file():
            (license_root / 'skcms').mkdir(parents=True, exist_ok=True)
            shutil.copyfile(skcms_license, license_root / 'skcms/LICENSE')
        for name, record in compiled['skia'].get('auxiliary_sources', {}).items():
            source = BUILD / record['source']
            references.append(record['source'])
            for relative in record['license_files']:
                destination = license_root / name / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source / relative, destination)
        for prefix in platform_prefixes:
            shutil.copytree(prefix / 'share/licenses', license_root / 'x11-platform', dirs_exist_ok=True)
        system_libraries = (['kernel32','user32','gdi32','winmm','imm32','ole32','oleaut32','version',
                             'uuid','advapi32','setupapi','shell32','dinput8','dwrite','usp10','opengl32','fontsub','d3d12','dxgi','dxguid']
                            if target == 'windows-x64' else ['m', 'dl'])
        text = ""
        for name, paths in {"XL_ICU_INCLUDE": [icu / "include"],
                            "XL_ICU_LIBRARIES": [icu / item for item in compiled["icu"]["libraries"]],
                            "XL_ZSTD_SOURCE": [zstd], "XL_EMBEDDED_CATALOG": [generated],
                            'XL_GRAPHICS_INCLUDE_DIRS': include_dirs,
                            'XL_SKIA_SOURCE': [BUILD / compiled['skia']['source_overlay']],
                            'XL_GRAPHICS_LIBRARIES': graphics + platform_archives,
                            'XL_GRAPHICS_HOST_PLATFORM_LIBRARIES': platform_archives,
                            'XL_GRAPHICS_PLATFORM_INCLUDE_DIRS': [prefix / 'include' for prefix in platform_prefixes],
                            'XL_GRAPHICS_C_ARCHIVES': c_archives,
                            'XL_GRAPHICS_LICENSES': [license_root]}.items():
            text += "set(" + name + "\n" + "".join('  "' + path.as_posix() + '"\n' for path in paths) + ")\n"
        text += 'set(XL_GRAPHICS_SYSTEM_LIBRARIES ' + ' '.join(system_libraries) + ')\n'
        text += 'set(XL_STATIC_GRAPHICS ' + ('ON' if static_graphics else 'OFF') + ')\n'
        text += 'set(XL_DAWN_GRAPHICS ' + ('ON' if dawn else 'OFF') + ')\n'
        if not manifest.exists() or manifest.read_text(encoding="utf-8") != text:
            manifest.write_text(text, encoding="utf-8")
        record = {"target": target, "configuration": configuration, "compiler": compiler,
                  "icu": compiled["icu"], "zstd": lock["zstd"], "icu_source": lock["icu"],
                  "resource_catalog": resource_manifest["key"], "resources": resource_manifest["resources"],
                  'graphics': compiled, 'system_libraries': system_libraries,
                  'static_graphics': static_graphics,
                  'references': list(dict.fromkeys(references))}
        private_archives = [icu / item for item in compiled['icu']['libraries']] + graphics + platform_archives
        record['private_archives'] = [
            {'path': path.relative_to(BUILD).as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            for path in dict.fromkeys(private_archives)]
        write_json(manifest.with_suffix(".json"), record)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("windows-x64", "linux-x64-musl", "linux-x64-glibc-dev"), required=True)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    if os.name == "nt" and args.target.startswith("linux-"):
        script = subprocess.check_output(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-a", "-u", str(Path(__file__).resolve())], text=True).strip()
        return subprocess.call(["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B", script] + sys.argv[1:])
    print(prepare(args.target, args.configuration, min(args.jobs, 8)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
