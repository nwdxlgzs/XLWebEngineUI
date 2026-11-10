#!/usr/bin/env python3
"""Local, inspectable CI. All disposable state is rooted in build/."""
import sys
sys.dont_write_bytecode = True
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")

import argparse
import contextlib
import datetime
import hashlib
import json
import os
import platform
import shutil
import subprocess
import time
import uuid
from collections import deque
from pathlib import Path

from ci_support import (WorkLock, checked_child, cleanup_runs, cleanup_work, cleanup_package_staging, cleanup_validation_scratch, host_identity, manifest_digest,
                        read_json, record_active, relocate_directory, select_impact, source_manifest, work_lock_path, write_json)
import artifacts
import bootstrap

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SETTINGS = read_json(ROOT / "ci/settings.json")
TARGETS = ("windows-x64", "linux-x64-musl", "linux-x64-glibc-dev")


def capture(arguments, cwd=ROOT, env=None):
    command_line = arguments if isinstance(arguments, str) else [str(arg) for arg in arguments]
    completed = subprocess.run(command_line, cwd=str(cwd), env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               encoding="utf-8", errors="replace")
    if completed.returncode:
        display = arguments if isinstance(arguments, str) else " ".join(map(str, arguments))
        raise RuntimeError("{}\n{}".format(display, completed.stdout))
    return completed.stdout.strip()


def windows_environment():
    env = dict(os.environ)
    env["VSLANG"] = "1033"
    if shutil.which("cl", path=env.get("PATH")):
        return env
    vswhere = Path(env.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if not vswhere.exists():
        raise RuntimeError("Visual Studio Build Tools not found")
    installation = capture([vswhere, "-latest", "-products", "*", "-requires",
                            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"])
    vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
    if not vcvars.is_file():
        raise RuntimeError("vcvars64.bat not found")
    # cmd.exe needs its own outer quoting; list2cmdline would backslash-escape it.
    output = capture('cmd.exe /d /s /c ""{}" >nul && set"'.format(vcvars))
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key and not key.startswith("="):
            env[key.upper()] = value
    env["VSLANG"] = "1033"
    return env


def linux_path(path):
    if os.name != "nt":
        return str(path)
    return capture(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-a", "-u", str(path)])


def validate_documents():
    required = ("RAW-PLAN.MD", "AGENTS.md", "WORKMEM.MD", "PLAN.MD", "TODO.MD")
    for name in required:
        raw = (ROOT / name).read_bytes()
        raw.decode("utf-8")
        if not raw.strip():
            raise RuntimeError("empty management document: " + name)
    size = (ROOT / "WORKMEM.MD").stat().st_size
    if size > 40000:
        raise RuntimeError("WORKMEM.MD exceeds 40,000 bytes")
    if size >= 32768:
        print("warning: WORKMEM.MD reached its 32 KiB consolidation threshold", flush=True)
    return {"workmem_bytes": size, "raw_sha256": hashlib.sha256((ROOT / "RAW-PLAN.MD").read_bytes()).hexdigest()}


class Commands:
    def __init__(self, log, env=None, verbose=True):
        self.log = Path(log)
        self.verbose = verbose
        self.env = env or dict(os.environ)
        self.env["PYTHONDONTWRITEBYTECODE"] = "1"
        self.env["PYTHONIOENCODING"] = "utf-8"
        if os.name != "nt":
            self.env["LC_ALL"] = "C"

    def run(self, arguments, cwd=ROOT, env=None):
        args = [str(arg) for arg in arguments]
        line = "[exec] " + subprocess.list2cmdline(args)
        print(line, flush=True)
        self.log.parent.mkdir(parents=True, exist_ok=True)
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(line + "\n")
            stream.flush()
            process = subprocess.Popen(args, cwd=str(cwd), env=env or self.env,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       encoding="utf-8", errors="replace")
            try:
                tail = deque(maxlen=30)
                for text in process.stdout:
                    tail.append(text)
                    if self.verbose:
                        print(text, end="", flush=True)
                    stream.write(text)
                result = process.wait()
            except BaseException:
                process.terminate()
                process.wait()
                raise
        if result:
            if not self.verbose:
                print("".join(tail), end="", flush=True)
            raise RuntimeError("command failed ({}): {}; log: {}".format(result, subprocess.list2cmdline(args), self.log))


def target_environment(target):
    if target == "windows-x64":
        if os.name != "nt":
            raise RuntimeError("Windows target needs a Windows host")
        bootstrap.prepare_llvm()
        return bootstrap.llvm_environment(windows_environment()), ["-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/toolchains/windows-x64-clang.cmake")]
    if platform.system() != "Linux":
        raise RuntimeError("Linux target needs Linux or WSL dispatch")
    if target == "linux-x64-musl":
        toolchain = ROOT / "cmake/toolchains/linux-x64-musl.cmake"
        if not toolchain.is_file():
            raise RuntimeError("musl C++ toolchain is not provisioned; glibc development results cannot replace it")
        bootstrap.prepare_musl()
        return bootstrap.llvm_environment(), ["-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain)]
    bootstrap.prepare_llvm()
    return bootstrap.llvm_environment(), ["-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/toolchains/linux-x64-clang.cmake")]


def toolchain_key(target, env, flags):
    versions = {}
    for command in ("cmake", "ninja"):
        versions[command] = capture([command, "--version"], env=env).splitlines()[0]
    compiler = "clang-cl" if target == "windows-x64" else "clang++"
    versions["compiler_path"] = str(bootstrap.llvm_bin() / (compiler + (".exe" if os.name == "nt" else "")))
    if target == "linux-x64-musl":
        metadata = read_json(BUILD / "toolchains/wrappers/linux-musl/toolchain.json")
        versions["compiler_path"] = str(BUILD / "toolchains/wrappers/linux-musl/cxx")
        versions["compiler_version"] = capture([versions["compiler_path"], "--version"], env=env).splitlines()[0]
        versions["runtime_key"] = metadata["key"]
        versions["musl_runtime_sha256"] = hashlib.sha256(Path("/usr/lib/x86_64-linux-musl/libc.so").read_bytes()).hexdigest()
    else:
        versions["compiler_version"] = capture([versions["compiler_path"], "--version"], env=env).splitlines()[0]
    linker = bootstrap.llvm_bin() / ("lld-link.exe" if target == "windows-x64" else "ld.lld")
    versions["linker"] = capture([linker, "--version"], env=env).splitlines()[0]
    if target == "windows-x64":
        versions["msvc_runtime_headers"] = env.get("VCTOOLSVERSION", "unknown")
        versions["windows_sdk"] = env.get("WINDOWSSDKVERSION", "unknown")
    inputs = {}
    for path in sorted(list((ROOT / "cmake").rglob("*")) + [ROOT / "CMakeLists.txt"]):
        if path.is_file():
            inputs[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for path in sorted((ROOT / "third_party").rglob("*.json")):
        inputs[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    digest = manifest_digest({"target": target, "versions": versions, "inputs": inputs, "flags": flags})[:12]
    return digest, versions


def run_local_target(target, mode, run_dir, configurations, make_package, source_digest, dependency_check=False):
    destination = Path(run_dir) / target
    destination.mkdir(parents=True, exist_ok=True)
    record = {"target": target, "state": "running", "pid": os.getpid(), "host": host_identity(),
              "source_digest": source_digest, "phase": "web-core", "configurations": [],
              "legacy_validation": {"win7_native": "not-verified", "uos_native": "not-verified"}}
    result_path = destination / "result.json"
    write_json(result_path, record)
    leases = contextlib.ExitStack()
    try:
        env, flags = target_environment(target)
        command = Commands(destination / "commands.log", env)
        if target.startswith("linux-"):
            command.run([sys.executable, "-B", "-m", "unittest", "discover", "-s", ROOT / "tests/tools", "-v"])
        key, tools = toolchain_key(target, env, flags)
        artifact_references = []
        toolchain_artifacts = [bootstrap.llvm_directory()]
        if target == "linux-x64-musl":
            metadata = read_json(BUILD / "toolchains/wrappers/linux-musl/toolchain.json")
            toolchain_artifacts.append(Path(metadata["runtime"]))
        for artifact in toolchain_artifacts:
            leases.enter_context(artifacts.lease(BUILD, artifact))
            artifact_references.append(artifact.relative_to(BUILD).as_posix())
        record["toolchain"] = tools
        build_info = destination / "BUILD-INFO.json"
        record["phase"] = "web-core"
        record["sdk_components"] = ["runtime", "lazy-resources", "icu-nfc", "retained-raster",
                                    "standalone-window", "borrowed-native-window", "offscreen-view", "html-dom",
                                    "css-block-layout", "css-inline-layout", "css-whitespace-tabs-line-height", "dom-text-mapping", "text-hit-testing",
                                    "quickjs-dom", "shaped-text", "ax-semantic-snapshot", "ui-content-uri", "webtest-preview"]
        write_json(build_info, dict(record, abi=target, version="0.1.0-web-core", production_ready=False))
        for config in configurations:
            import sdk_inputs
            inputs = sdk_inputs.prepare(target, config, min(os.cpu_count() or 2, 6))
            private_inputs = read_json(inputs.with_suffix(".json"))
            for relative in private_inputs["references"]:
                leases.enter_context(artifacts.lease(BUILD, BUILD / relative))
                if relative not in artifact_references:
                    artifact_references.append(relative)
            work = BUILD / "work" / target / key / config
            with WorkLock(work_lock_path(BUILD, work)):
                work_info = {"target": target, "configuration": config, "state": "running",
                             "host": host_identity(), "pid": os.getpid(), "last_used": time.time(),
                             "artifact_references": artifact_references}
                write_json(work / ".ci-work.json", work_info)
                command.run(["cmake", "-S", ROOT, "-B", work, "-G", "Ninja",
                             "-DCMAKE_BUILD_TYPE=" + config, "-DXL_BUILD_ABI=" + target,
                             "-DXL_SDK_INPUT_MANIFEST=" + inputs.as_posix(),
                             "-DXL_BUILD_INFO_FILE=" + build_info.as_posix()] + flags)
                command.run(["cmake", "--build", work, "--parallel", str(min(os.cpu_count() or 2, 8))])
                test_env = dict(command.env or os.environ)
                test_env["XL_REQUIRE_NATIVE_INPUT"] = "1"
                command.run(["ctest", "--test-dir", work, "--output-on-failure", "--no-tests=error"], env=test_env)
                llvm = bootstrap.llvm_bin()
                static_library = work / ("XLWebEngineUI_static.lib" if target == "windows-x64" else "libXLWebEngineUI_static.a")
                symbols = capture([llvm / ("llvm-nm.exe" if os.name == "nt" else "llvm-nm"),
                                   "--defined-only", "--extern-only", "--format=posix", static_library], env=command.env)
                import re
                decoder = work / "third_party/recipes/zstd-runtime" / ("xl_zstd_decoder.lib" if target == "windows-x64" else "libxl_zstd_decoder.a")
                private_archives = [BUILD / item["path"] for item in private_inputs["private_archives"]]
                for item, archive in zip(private_inputs["private_archives"], private_archives):
                    if hashlib.sha256(archive.read_bytes()).hexdigest() != item["sha256"]:
                        raise RuntimeError("SDK private input archive changed: " + item["path"])
                input_symbols = capture([llvm / ("llvm-nm.exe" if os.name == "nt" else "llvm-nm"),
                                         "--defined-only", "--extern-only", "--format=posix", decoder] + private_archives,
                                        env=command.env)
                from merge_archive import private_symbol
                private_names = {name for name, kind in re.findall(r"^(\S+)\s+([A-Za-z])\s", input_symbols, re.MULTILINE)
                                 if private_symbol(name, kind)}
                static_names = set(re.findall(r"^(\S+)\s+[A-Za-z]\s", symbols, re.MULTILINE))
                if not {"ZSTD_decompress", "SDL_Init"}.issubset(private_names) or not any("drawRect" in name and "SkCanvas" in name for name in private_names):
                    raise RuntimeError("static SDK private closure is missing the decoder, SDL or Skia C++ implementation")
                if private_names & static_names:
                    raise RuntimeError("static SDK exposes unisolated private symbols: " + ", ".join(sorted(private_names & static_names)[:20]))
                if not {"xl_private_" + name for name in private_names}.issubset(static_names):
                    raise RuntimeError("static SDK is missing renamed private C/C++ definitions")
                if any(name in symbols for name in ("xl_private_ZSTD_compress ", "xl_private_ZSTD_createCCtx ", "ZDICT_trainFromBuffer ")):
                    raise RuntimeError("SDK contains a runtime compression encoder")
                if "xl_private_ZSTD_decompress " not in symbols or "xl_runtime_request_resource " not in symbols:
                    raise RuntimeError("static SDK did not merge the real resource and decompressor path")
                write_json(destination / (config + "-private-symbol-audit.json"),
                           {"state": "passed", "input_archives": len(private_archives) + 1,
                            "isolated_definitions": len(private_names), "c_and_cpp": True})
                (destination / (config + "-static-symbols.txt")).write_text(symbols, encoding="utf-8")
                shared_library = work / ("XLWebEngineUI.dll" if target == "windows-x64" else "libXLWebEngineUI.so.0.1.0")
                if target == "windows-x64":
                    exported = capture([llvm / "llvm-readobj.exe", "--coff-exports", shared_library], env=command.env)
                    names = re.findall(r"^  Name: (.+)$", exported, re.MULTILINE)
                else:
                    exported = capture([llvm / "llvm-nm", "--dynamic", "--defined-only", "--format=posix", shared_library], env=command.env)
                    names = [line.split()[0].split("@")[0] for line in exported.splitlines() if line.split() and not line.startswith("XLWEBENGINEUI_")]
                public_names = set(re.findall(r"XL_API\s+[^;]*?\b(xl_\w+)\s*\(",
                                              (ROOT / "include/XLWebEngineUI/xl.h").read_text(encoding="utf-8")))
                if set(names) != public_names:
                    raise RuntimeError("shared SDK exports do not match the public C ABI: " + str(set(names) ^ public_names))
                (destination / (config + "-shared-exports.txt")).write_text(exported, encoding="utf-8")
                if target == "linux-x64-musl":
                    library = work / "libXLWebEngineUI.so.0.1.0"
                    dynamic = capture(["readelf", "-d", library], env=command.env)
                    versions_info = capture(["readelf", "--version-info", library], env=command.env)
                    if "libc.so.6" in dynamic or "GLIBC_" in versions_info or "GLIBCXX_" in versions_info:
                        raise RuntimeError("musl SDK accidentally depends on glibc or GNU libstdc++")
                    if "libc.so" not in dynamic:
                        raise RuntimeError("musl shared SDK did not retain its explicit libc runtime boundary")
                    (destination / (config + "-elf.txt")).write_text(dynamic + "\n" + versions_info, encoding="utf-8")
                install_stage = destination / config / "sdk-install"
                stage = destination / config / "sdk-relocated"
                command.run(["cmake", "--install", work, "--prefix", install_stage, "--component", "Runtime"])
                relocate_directory(install_stage, stage)
                webtest = stage / 'bin' / ('WebTest.exe' if target == 'windows-x64' else 'WebTest')
                command.run([webtest, '--check'], cwd=stage / 'share/doc/XLWebEngineUI/examples/webtest')
                symbols_stage = destination / config / "symbol-files"
                command.run(["cmake", "--install", work, "--prefix", symbols_stage, "--component", "Symbols"])
                symbol_files = list((symbols_stage / "symbols").iterdir())
                if not symbol_files or any(path.stat().st_size == 0 for path in symbol_files):
                    raise RuntimeError("separate debug symbol files missing or empty")
                if target.startswith("linux-"):
                    sections = capture(["readelf", "-S", "--wide", stage / "lib/libXLWebEngineUI.so.0.1.0"], env=command.env)
                    debug_sections = capture(["readelf", "-S", "--wide", symbols_stage / "symbols/libXLWebEngineUI.so.debug"], env=command.env)
                    if ".debug_info" in sections or ".gnu_debuglink" not in sections or ".debug_info" not in debug_sections:
                        raise RuntimeError("shared library debug information was not correctly separated")
                consumer_source = destination / config / "consumer-source"
                shutil.copytree(ROOT / "tests/sdk", consumer_source)
                # Test inputs travel with the independent consumer source;
                # the relocated SDK never references a build-tree font path.
                test_font = BUILD / private_inputs['graphics']['skia']['source_overlay'] / 'resources/fonts/Roboto-Regular.ttf'
                shutil.copyfile(test_font, consumer_source / 'test-font.ttf')
                consumer = destination / config / "consumer-build"
                command.run(["cmake", "-S", consumer_source, "-B", consumer, "-G", "Ninja",
                             "-DCMAKE_BUILD_TYPE=" + config, "-DCMAKE_PREFIX_PATH=" + str(stage)] + flags)
                command.run(["cmake", "--build", consumer, "--parallel", "4"])
                run_env = dict(command.env)
                run_env["PATH"] = str(stage / "bin") + os.pathsep + run_env.get("PATH", "")
                command.run(["ctest", "--test-dir", consumer, "--output-on-failure", "--no-tests=error"], env=run_env)
                if target == "windows-x64":
                    compat_env = dict(run_env, __COMPAT_LAYER="WIN7RTM")
                    command.run([str(consumer / "consume_shared.exe")], env=compat_env)
                    record["win7_compatibility_mode"] = "smoke-passed-on-modern-windows; not-native-win7-validation"
                record["configurations"].append({"name": config, "state": "passed", "sdk_static": "passed", "sdk_shared": "passed",
                                                 "resource_sdk": "passed", "view_sdk": "passed", "document_sdk": "passed",
                                                 "inline_text_sdk": "passed", "css_whitespace_sdk": "passed", "css_font_sdk": "passed", "css_box_sdk": "passed", "native_view": "passed",
                                                 "sdk_c_cpp_static_shared": "passed", "private_symbol_isolation": "passed"})
                write_json(build_info, dict(record, state="configuration-validation-passed", abi=target,
                                            version="0.1.0-web-core", production_ready=False))
                if make_package and config == "Release":
                    package_dir = destination / "packages"
                    command.run(["cpack", "--config", work / "CPackConfig.cmake", "-B", package_dir])
                    packages = []
                    for package in package_dir.iterdir():
                        if package.is_file() and package.suffix in (".zip", ".gz"):
                            checksum = hashlib.sha256(package.read_bytes()).hexdigest()
                            package.with_name(package.name + ".sha256").write_text(checksum + "  " + package.name + "\n", encoding="utf-8")
                            packages.append({"path": str(package.relative_to(BUILD)), "sha256": checksum})
                    if len(packages) != 2 or sum("-symbols." in item["path"] for item in packages) != 1:
                        raise RuntimeError("CPack must produce an SDK archive and a separate symbols archive")
                    record["packages"] = packages
                write_json(work / ".ci-work.json", dict(work_info, state="passed", last_used=time.time()))
        if dependency_check and target in ("windows-x64", "linux-x64-musl"):
            command.run([sys.executable, "-B", ROOT / "tools/validate_dependencies.py", "--target", target,
                         "--jobs", str(min(os.cpu_count() or 2, 6))])
            dependency_record = read_json(BUILD / "reports/dependency-validation" / (target + ".json"))
            if dependency_record.get("state") != "passed":
                raise RuntimeError("real dependency and resource validation did not pass")
            record["dependency_validation"] = dependency_record
            write_json(destination / "dependencies.json", dependency_record)
        else:
            record["dependency_validation"] = {"state": "not-selected", "reason": "outside target or changed module scope"}
        record["state"] = "passed"
        return 0
    except Exception as error:
        record.update(state="failed", error=str(error))
        print("ERROR: " + str(error), flush=True)
        return 1
    finally:
        record["finished_at"] = time.time()
        write_json(result_path, record)
        leases.close()
        cleanup_completed_work()


def refresh_work_references(work, info):
    """Keep GC references aligned with the manifest of a reused CMake tree."""
    cache = work / 'CMakeCache.txt'
    if not cache.is_file():
        return
    for line in cache.read_text(encoding='utf-8').splitlines():
        if line.startswith('XL_SDK_INPUT_MANIFEST:FILEPATH='):
            manifest = Path(line.split('=', 1)[1]).with_suffix('.json')
            current = read_json(manifest)['references']
            toolchains = [p for p in info.get('artifact_references', []) if p.startswith('toolchains/')]
            info['artifact_references'] = list(dict.fromkeys(toolchains + current))
            return


def focused_quick(args):
    """Reuse a Debug tree; optionally package the verified functional stage."""
    validate_documents()
    targets = args.targets or (['windows-x64'] if os.name == 'nt' else ['linux-x64-musl'])
    if args.package and any(target not in ('windows-x64', 'linux-x64-musl') for target in targets):
        raise ValueError('quick --package supports official Windows and musl targets only')
    before = source_manifest(ROOT)
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    run = BUILD / 'runs' / ('quick--' + '+'.join(sorted(targets))) / run_id
    record = dict(mode='quick', state='running', host=host_identity(), pid=os.getpid(),
                  source_digest=manifest_digest(before), started_at=time.time(), targets=[],
                  scope='focused incremental Debug; ' + ('stage SDK packages; no full audit' if args.package else 'no SDK install, packages or full audit'))
    write_json(run / 'report.json', record)
    try:
        for target in targets:
            if os.name == 'nt' and target.startswith('linux-'):
                child = ['wsl.exe', '-d', 'Ubuntu', '--exec', 'python3', '-B', linux_path(ROOT / 'tools/ci.py'),
                         'quick', '--targets', target]
                if args.tests:
                    child += ['--tests', args.tests]
                if args.build_targets:
                    child += ['--build-targets'] + args.build_targets
                if args.package:
                    child.append('--package')
                Commands(run / 'commands.log').run(child)
                record['targets'].append(dict(target=target, state='passed', scope='WSL focused quick'))
                continue
            baseline = read_json(BUILD / 'state' / target / 'baseline.json')
            impact = select_impact(before, baseline, SETTINGS)
            if impact['scope'] == 'docs' and not args.tests and not args.build_targets and not args.package:
                record['targets'].append(dict(target=target, state='passed', scope='documents'))
                continue
            candidates = []
            for metadata in (BUILD / 'work' / target).glob('*/Debug/.ci-work.json'):
                info = read_json(metadata, {})
                if info.get('host') == host_identity() and not record_active(info):
                    candidates.append((info.get('last_used', 0), metadata.parent))
            env, flags = target_environment(target)
            command = Commands(run / (target + '.log'), env)
            if candidates:
                work = max(candidates)[1]
            else:
                key, _ = toolchain_key(target, env, flags)
                work = BUILD / 'work' / target / key / 'Debug'
            with WorkLock(work_lock_path(BUILD, work)):
                info = read_json(work / '.ci-work.json', {})
                info.update(target=target, configuration='Debug')
                info.update(state='running', pid=os.getpid(), host=host_identity(), last_used=time.time())
                write_json(work / '.ci-work.json', info)
                try:
                    # A reused tree may outlive a deleted dependency prefix or a
                    # recipe/patch change. Resolve the exact closure on every
                    # invocation; successful dependency builds are reused.
                    import sdk_inputs
                    inputs = sdk_inputs.prepare(target, 'Debug', min(os.cpu_count() or 2, 6))
                    info['artifact_references'] = read_json(inputs.with_suffix('.json'))['references']
                    write_json(work / '.ci-work.json', info)
                    if not (work / 'CMakeCache.txt').is_file():
                        command.run(['cmake', '-S', ROOT, '-B', work, '-G', 'Ninja',
                                     '-DCMAKE_BUILD_TYPE=Debug', '-DXL_BUILD_ABI=' + target,
                                     '-DXL_SDK_INPUT_MANIFEST=' + inputs.as_posix()] + flags)
                    refresh_work_references(work, info)
                    write_json(work / '.ci-work.json', info)
                    build = ['cmake', '--build', work, '--parallel', '6']
                    if args.build_targets:
                        required = ['WebTest', 'xl_static'] if args.package else []
                        build += ['--target'] + list(dict.fromkeys(args.build_targets + required))
                    command.run(build)
                    tests = ['ctest', '--test-dir', work, '--output-on-failure', '--no-tests=error']
                    if args.tests:
                        tests += ['-R', args.tests]
                    command.run(tests)
                    info.update(state='passed', focused_source_digest=record['source_digest'])
                    if args.package:
                        from package_stage import package
                        verification = dict(source_digest=record['source_digest'], tests=args.tests or 'root tests',
                                            report=(run / 'report.json').relative_to(BUILD).as_posix(),
                                            work=work.relative_to(BUILD).as_posix())
                        packaged = package(work, target, verification, command)
                        record.setdefault('packages', []).append(packaged)
                except BaseException:
                    info['state'] = 'failed'
                    raise
                finally:
                    info['last_used'] = time.time()
                    write_json(work / '.ci-work.json', info)
            record['targets'].append(dict(target=target, state='passed', work=str(work.relative_to(BUILD)),
                                          tests=args.tests or 'root tests', build_targets=args.build_targets))
        if source_manifest(ROOT) != before:
            raise RuntimeError('source changed during focused verification')
        record['state'] = 'passed'
        return 0
    except (Exception, KeyboardInterrupt) as error:
        record.update(state='interrupted' if isinstance(error, KeyboardInterrupt) else 'failed', error=str(error))
        print('ERROR: ' + str(error), flush=True)
        return 1
    finally:
        record['finished_at'] = time.time()
        write_json(run / 'report.json', record)
        print('Report: ' + str(run / 'report.json'), flush=True)
        cleanup_runs(BUILD, SETTINGS['retention'])
        cleanup_completed_work()


def doctor():
    report = {"host": platform.platform(), "machine": platform.machine(), "python": sys.version,
              "phase": "web-core", "legacy_native": "not-verified", "tools": {}}
    for name in ("git", "cmake", "ninja", "wsl", "clang-cl", "clang++", "lld-link", "ld.lld", "musl-gcc"):
        report["tools"][name] = shutil.which(name)
    if os.name == "nt":
        try:
            env = windows_environment()
            report["msvc"] = {"available": bool(shutil.which("cl", path=env.get("PATH"))), "version": env.get("VCTOOLSVERSION")}
        except RuntimeError as error:
            report["msvc"] = {"available": False, "error": str(error)}
        try:
            report["wsl"] = json.loads(capture(["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B", linux_path(ROOT / "tools/ci.py"), "doctor", "--json"]))
        except (RuntimeError, ValueError) as error:
            report["wsl"] = {"available": False, "error": str(error)}
    report["documents"] = validate_documents()
    report["musl_cxx_toolchain_configured"] = (ROOT / "cmake/toolchains/linux-x64-musl.cmake").is_file()
    report["musl_wrappers_present"] = (BUILD / "toolchains/wrappers/linux-musl/cxx").is_file()
    report["llvm_host"] = read_json(bootstrap.llvm_directory() / ".complete.json", {"state": "not-prepared"})
    report["musl_clang_metadata"] = read_json(BUILD / "toolchains/wrappers/linux-musl/toolchain.json", {"state": "not-prepared"})
    write_json(BUILD / "reports" / ("doctor-" + platform.system().lower() + ".json"), report)
    return report


def execute(args):
    validate_documents()
    before = source_manifest(ROOT)
    digest = manifest_digest(before)
    targets = args.targets or (["windows-x64"] if os.name == "nt" else ["linux-x64-musl"])
    if args.command in ("full", "release") and not args.targets:
        targets = list(SETTINGS["release_targets"])
    if args.command == "release":
        if capture(["git", "status", "--porcelain"]):
            raise RuntimeError("release requires a clean committed Git tree")
        readiness = read_json(ROOT / "ci/release-readiness.json")
        missing = [name for name, ready in readiness["requirements"].items() if not ready]
        if not readiness["production_ready"] or missing:
            raise RuntimeError("formal release is not ready: " + ", ".join(missing))
        if set(targets) != set(SETTINGS["release_targets"]):
            raise RuntimeError("release cannot omit required platforms")
    impacts = {}
    for target in targets:
        baseline = read_json(BUILD / "state" / target / "baseline.json")
        impacts[target] = select_impact(before, baseline, SETTINGS)
    if args.plan:
        print(json.dumps(impacts, ensure_ascii=False, indent=2))
        return 0
    run_id = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    bucket = args.command + "--" + "+".join(sorted(targets))
    run_dir = BUILD / "runs" / bucket / run_id
    run_dir.mkdir(parents=True)
    record = {"id": run_id, "mode": args.command, "state": "running", "pid": os.getpid(), "host": host_identity(),
              "source_digest": digest, "started_at": time.time(), "phase": "web-core", "impact": impacts, "targets": [],
              "requested_targets": targets, "required_targets": SETTINGS["release_targets"],
              "official_matrix_selected": set(targets) == set(SETTINGS["release_targets"])}
    report_path = run_dir / "report.json"
    write_json(report_path, record)
    command = Commands(run_dir / "commands.log")
    try:
        command.run([sys.executable, "-B", "-m", "unittest", "discover", "-s", str(ROOT / "tests/tools"), "-v"])
        for target in targets:
            impact = impacts[target]
            print("{}: {} ({})".format(target, impact["scope"], impact["reason"]), flush=True)
            if args.command == "quick" and impact["scope"] == "docs":
                record["targets"].append({"target": target, "state": "passed", "scope": "docs", "skipped": "native build: no affected source"})
                continue
            configs = ["Debug", "Release"] if args.command != "quick" or impact["scope"] == "global" else ["Debug"]
            package = args.command in ("full", "release")
            dependency_check = (args.command in ("full", "release") or impact["scope"] == "global"
                                or bool({"resources", "graphics", "platform"} & set(impact["modules"])))
            if target.startswith("linux-") and os.name == "nt":
                child = ["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B", linux_path(ROOT / "tools/ci.py"),
                         "_target", "--target", target, "--run-dir", linux_path(run_dir), "--mode", args.command,
                         "--source-digest", digest, "--configs"] + configs
                if package:
                    child.append("--package")
                if dependency_check:
                    child.append("--dependency-check")
                try:
                    command.run(child)
                finally:
                    result = read_json(run_dir / target / "result.json")
                    if result:
                        record["targets"].append(result)
            else:
                code = run_local_target(target, args.command, run_dir, configs, package, digest, dependency_check)
                result = read_json(run_dir / target / "result.json")
                record["targets"].append(result)
                if code:
                    raise RuntimeError(target + " validation failed")
        if source_manifest(ROOT) != before:
            raise RuntimeError("source changed during CI; results cannot establish a baseline")
        record["state"] = "passed"
        if args.command in ("full", "release") and record["official_matrix_selected"]:
            write_json(report_path, record)
            from audit_sdk import audit
            audit(report_path)
            record["sdk_audit"] = {"state": "passed", "report": "sdk-audit.json"}
        for target in targets:
            write_json(BUILD / "state" / target / "baseline.json", before)
        return 0
    except (Exception, KeyboardInterrupt) as error:
        record.update(state="interrupted" if isinstance(error, KeyboardInterrupt) else "failed", error=str(error))
        print("ERROR: " + str(error), flush=True)
        return 1
    finally:
        record["finished_at"] = time.time()
        write_json(report_path, record)
        print("Report: " + str(report_path), flush=True)
        cleanup_runs(BUILD, SETTINGS["retention"])
        cleanup_completed_work()


def cleanup_completed_work(dry_run=False):
    stamp_path = BUILD / "state" / ("cleanup-" + platform.system().lower() + ".json")
    from compare_text_reference import adopt_completed_reports
    if not dry_run:
        adopt_completed_reports(BUILD)
        artifacts.register_report_files(BUILD)
    removed = {"leases": artifacts.prune_local_leases(BUILD, dry_run),
               "work": cleanup_work(BUILD, SETTINGS["retention"], dry_run),
               "package_staging": cleanup_package_staging(BUILD, dry_run),
               "validation_scratch": cleanup_validation_scratch(BUILD, dry_run),
               "artifacts": artifacts.collect(BUILD, SETTINGS["retention"],
                                              artifacts.pinned_content_keys(ROOT), dry_run)}
    if not dry_run:
        write_json(stamp_path, {"last_run": time.time(), "removed": removed})
    return removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("doctor", "quick", "full", "release", "gc", "_target"))
    parser.add_argument("--targets", choices=TARGETS, nargs="+")
    parser.add_argument("--plan", action="store_true", help="show impact selection without building")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument('--tests', help='quick: directly affected CTest name expression')
    parser.add_argument('--build-targets', nargs='+', help='quick: only these CMake targets')
    parser.add_argument("--local-only", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--target", choices=TARGETS, help=argparse.SUPPRESS)
    parser.add_argument("--run-dir", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--mode", help=argparse.SUPPRESS)
    parser.add_argument("--source-digest", help=argparse.SUPPRESS)
    parser.add_argument("--configs", nargs="+", choices=("Debug", "Release"), help=argparse.SUPPRESS)
    parser.add_argument("--package", action="store_true", help='quick: package a verified functional stage, including WebTest, SDK and separate symbols')
    parser.add_argument("--dependency-check", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    try:
        if args.command == "doctor":
            print(json.dumps(doctor(), ensure_ascii=False, indent=2))
            return 0
        if args.command == "gc":
            with WorkLock(BUILD / "state/gc.lock"):
                result = {"runs": cleanup_runs(BUILD, SETTINGS["retention"], args.dry_run),
                          "work": cleanup_completed_work(dry_run=args.dry_run)}
            if os.name == 'nt' and not args.local_only and shutil.which('wsl.exe'):
                # The two OSes must verify their own PIDs and retire their own
                # dead work/leases; a foreign-host guess cannot release them.
                child = ['wsl.exe', '-d', 'Ubuntu', '--exec', 'python3', '-B', linux_path(ROOT / 'tools/ci.py'), 'gc', '--local-only']
                if args.dry_run:
                    child.append('--dry-run')
                result['wsl'] = json.loads(capture(child))
            print(json.dumps(result, ensure_ascii=False, indent=2))
            return 0
        if args.command == "_target":
            if not args.target or not args.run_dir or not args.configs:
                parser.error("internal target invocation is incomplete")
            checked_child(BUILD, args.run_dir)
            return run_local_target(args.target, args.mode, args.run_dir, args.configs, args.package, args.source_digest, args.dependency_check)
        if args.command == 'quick' and not args.plan:
            return focused_quick(args)
        return execute(args)
    except (RuntimeError, OSError, ValueError, subprocess.CalledProcessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
