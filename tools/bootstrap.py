#!/usr/bin/env python3
"""Prepare pinned LLVM tools and musl runtimes inside build/, without Zig."""
import sys
sys.dont_write_bytecode = True
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tarfile
import uuid
import zipfile

from ci_support import WorkLock, checked_child, read_json, write_json
from dependency_support import ROOT, BUILD, download, extract_source, sha256, decompress_zstd_with_git, validate_members, publish_directory
import artifacts

LOCK = read_json(ROOT / "third_party/toolchains.lock.json")


def llvm_directory():
    key = "llvm-windows-x64" if os.name == "nt" else "llvm-linux-x64"
    return BUILD / "toolchains" / LOCK[key]["archive_root"]


def llvm_bin():
    return llvm_directory() / "bin"


def llvm_environment(base=None):
    env = dict(os.environ if base is None else base)
    if os.name == "nt":
        for key in list(env):
            if key.lower() == "path" and key != "PATH":
                env.setdefault("PATH", env[key])
                del env[key]
    env["PATH"] = str(llvm_bin()) + os.pathsep + env.get("PATH", "")
    if os.name != "nt":
        libraries = llvm_directory() / "usr/lib/x86_64-linux-gnu"
        env["LD_LIBRARY_PATH"] = str(libraries) + (os.pathsep + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    return env


def install_wrapper(path, script):
    """Publish a complete executable without rewriting an in-use script."""
    path = Path(path)
    content = script.encode("utf-8")
    if path.is_file() and path.read_bytes() == content:
        path.chmod(0o755)
        return
    temporary = path.with_name("." + path.name + "-" + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_bytes(content)
        temporary.chmod(0o755)
        os.replace(str(temporary), str(path))
    finally:
        if temporary.exists():
            temporary.unlink()


def prepare_gn():
    key = "gn-windows-x64" if os.name == "nt" else "gn-linux-x64"
    if key not in LOCK:
        raise RuntimeError("GN package not locked for this host: " + key)
    spec = LOCK[key]
    source = download(spec, key)
    destination = BUILD / "toolchains" / spec["archive_root"]
    filename = "gn.exe" if os.name == "nt" else "gn"
    with WorkLock(BUILD / "state" / ("bootstrap-" + key + ".lock")), artifacts.lease(BUILD, source):
        destination.mkdir(parents=True, exist_ok=True)
        binary = destination / filename
        if not binary.is_file():
            with zipfile.ZipFile(source) as package:
                # Do not unpack the package's metadata or arbitrary paths.
                binary.write_bytes(package.read(filename))
            binary.chmod(0o755)
        version = subprocess.check_output([str(binary), "--version"], text=True).strip()
        if spec["version"][:12] not in version:
            raise RuntimeError("GN revision differs from locked package: " + version)
        artifacts.register(BUILD, destination, "toolchains/" + key, spec["sha256"], [source.relative_to(BUILD).as_posix()])
        return binary


def prepare_cmake():
    """Prepare the locked Linux host tool without changing target compilers."""
    if platform.system() != "Linux" or platform.machine().lower() not in ("amd64", "x86_64"):
        raise RuntimeError("locked CMake host package requires Linux x64")
    spec = LOCK["cmake-linux-x64"]
    # The common extractor checks the archive and restores deleted tool/share
    # files from its inventory. This package is never part of the target SDK.
    root = extract_source(spec, "host-cmake-linux-x64")
    binary = root / "bin/cmake"
    with artifacts.lease(BUILD, root):
        version = subprocess.check_output([str(binary), "--version"], text=True).splitlines()[0]
        if version != "cmake version " + spec["version"]:
            raise RuntimeError("CMake version differs from locked package: " + version)
        if not (root / spec["license_files"][0]).is_file():
            raise RuntimeError("CMake host package license missing")
    return binary


def prepare_llvm():
    if platform.machine().lower() not in ("amd64", "x86_64"):
        raise RuntimeError("locked LLVM host packages require x64")
    key = "llvm-windows-x64" if os.name == "nt" else "llvm-linux-x64"
    spec = LOCK[key]
    destination = llvm_directory()
    identity = hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest()
    with WorkLock(BUILD / "state" / ("bootstrap-" + key + ".lock")):
        stamp = read_json(destination / ".complete.json", {})
        sources = []
        packages = [spec] if os.name == "nt" else spec["packages"]
        for package in packages:
            sources.append(download(package, key))
        if stamp.get("identity") != identity:
            temporary = BUILD / "toolchains" / ("unpack-" + uuid.uuid4().hex[:12])
            temporary.mkdir(parents=True)
            try:
                if os.name == "nt":
                    with artifacts.lease(BUILD, sources[0]):
                        expanded = temporary / "llvm.tar"
                        print("Streaming LLVM archive with its large Zstd window", flush=True)
                        decompress_zstd_with_git(sources[0], expanded)
                        with tarfile.open(str(expanded), "r:") as archive:
                            members = archive.getmembers()
                            validate_members(members, spec["archive_root"])
                            archive.extractall(str(temporary), members=members)
                        expanded.unlink()
                    unpacked = temporary / spec["archive_root"]
                    if not (unpacked / "bin/clang-cl.exe").is_file():
                        raise RuntimeError("LLVM archive does not contain the declared compiler root")
                else:
                    if platform.system() != "Linux" or not shutil.which("dpkg-deb"):
                        raise RuntimeError("Linux build host with dpkg-deb required to unpack LLVM")
                    for source in sources:
                        with artifacts.lease(BUILD, source):
                            subprocess.run(["dpkg-deb", "-x", str(source), str(temporary)], check=True)
                    unpacked = temporary
                if destination.exists():
                    shutil.rmtree(checked_child(BUILD, destination))
                publish_directory(unpacked, destination)
            finally:
                if temporary.exists():
                    shutil.rmtree(checked_child(BUILD, temporary))
        if os.name != "nt":
            wrappers = destination / "bin"
            wrappers.mkdir(exist_ok=True)
            for name in ("clang", "clang++", "ld.lld", "llvm-ar", "llvm-ranlib", "llvm-nm", "llvm-objcopy", "llvm-readobj"):
                executable = destination / "usr/lib/llvm-23/bin" / name
                if not executable.exists():
                    raise RuntimeError("locked LLVM tool missing: " + name)
                script = '#!/bin/sh\nexport LD_LIBRARY_PATH=' + shlex.quote(str(destination / "usr/lib/x86_64-linux-gnu"))
                script += '${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\nexec ' + shlex.quote(str(executable)) + ' "$@"\n'
                path = wrappers / name
                install_wrapper(path, script)
        compiler = llvm_bin() / ("clang-cl.exe" if os.name == "nt" else "clang")
        version = subprocess.check_output([str(compiler), "--version"], text=True, env=llvm_environment()).splitlines()[0]
        if "23.1." not in version:
            raise RuntimeError("LLVM compiler does not match the locked major/minor version: " + version)
        write_json(destination / ".complete.json", {"identity": identity, "version": version})
        artifacts.register(BUILD, destination, "toolchains/" + key, spec.get("sha256", identity),
                           [source.relative_to(BUILD).as_posix() for source in sources])
        print(version, flush=True)
    return destination


_prepared_musl = None
MUSL_RUNTIME_ENTRIES = ('.complete.json', 'lib/libc++.a', 'lib/libc++abi.a', 'lib/libunwind.a',
                        'sysroot/lib/libc.so', 'include/c++/v1/__config',
                        'clang-resource/lib/linux/libclang_rt.builtins-x86_64.a')


def prepare_musl():
    """Provision once per execution; recover if its entry artifacts disappear."""
    global _prepared_musl
    if os.name == 'nt':
        return _prepare_musl()
    # Nested dependency builders share this interpreter. Repeating the complete
    # LLVM source inventory there adds minutes on WSL/NTFS without new inputs.
    # A new invocation always starts cold; no build-only persistent shortcut.
    if _prepared_musl and all(path.is_file() for path in _prepared_musl):
        return
    _prepare_musl()
    metadata = read_json(BUILD / 'toolchains/wrappers/linux-musl/toolchain.json')
    runtime = Path(metadata['runtime'])
    _prepared_musl = [runtime / relative for relative in MUSL_RUNTIME_ENTRIES + ('licenses/musl.txt',)]
    _prepared_musl += [BUILD / 'toolchains/wrappers/linux-musl' / name for name in ('cc', 'cxx', 'ar', 'ranlib')]
    _prepared_musl += [llvm_bin() / name for name in ('clang', 'clang++', 'ld.lld', 'llvm-ar', 'llvm-ranlib')]


def _prepare_musl():
    if os.name == "nt":
        script = subprocess.check_output(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-a", "-u", str(ROOT / "tools/bootstrap.py")], text=True).strip()
        subprocess.run(["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B", script, "musl"], check=True)
        return
    prepare_llvm()
    spec = LOCK["llvm-runtimes-source"]
    source = extract_source(spec, "llvm-runtimes")
    musl_include = Path("/usr/include/x86_64-linux-musl")
    musl_libraries = Path("/usr/lib/x86_64-linux-musl")
    if not (musl_include / "stdlib.h").is_file() or not (musl_libraries / "libc.so").is_file():
        raise RuntimeError("musl development headers, libraries and loader are required")
    key = hashlib.sha256((spec["sha256"] + sha256(musl_libraries / "libc.so") + "llvm-musl-v1").encode()).hexdigest()[:12]
    destination = BUILD / "toolchains" / ("musl-llvm-" + key)
    with WorkLock(BUILD / "state/bootstrap-musl-runtime.lock"), artifacts.lease(BUILD, source):
        if not all((destination / relative).is_file() for relative in MUSL_RUNTIME_ENTRIES):
            sysroot = destination / "sysroot"
            (sysroot / "lib").mkdir(parents=True, exist_ok=True)
            includes = sysroot / "include"
            if includes.is_symlink():
                if includes.resolve() != musl_include.resolve():
                    raise RuntimeError("unexpected musl sysroot include link")
                includes.unlink()
            includes.mkdir(exist_ok=True)
            for path in musl_include.iterdir():
                if not (includes / path.name).exists():
                    (includes / path.name).symlink_to(path, target_is_directory=path.is_dir())
            # Linux UAPI headers are system build prerequisites, not another
            # libc. They are not copied into the shipped SDK.
            for name, path in (("linux", Path("/usr/include/linux")), ("asm-generic", Path("/usr/include/asm-generic")),
                               ("asm", Path("/usr/include/x86_64-linux-gnu/asm"))):
                if not path.is_dir():
                    raise RuntimeError("Linux UAPI development headers missing: " + str(path))
                if not (includes / name).exists():
                    (includes / name).symlink_to(path, target_is_directory=True)
            for path in musl_libraries.iterdir():
                target = sysroot / "lib" / path.name
                if not target.exists():
                    target.symlink_to(path)
            compiler = llvm_bin() / "clang"
            work = BUILD / "deps/work" / ("llvm-runtimes-" + key)
            env = llvm_environment()
            resource = Path(subprocess.check_output([str(compiler), "-print-resource-dir"], text=True, env=env).strip())
            overlay = destination / "clang-resource"
            overlay.mkdir(exist_ok=True)
            if not (overlay / "include").exists():
                (overlay / "include").symlink_to(resource / "include", target_is_directory=True)
            common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=" + str(compiler),
                      "-DCMAKE_CXX_COMPILER=" + str(llvm_bin() / "clang++"), "-DCMAKE_ASM_COMPILER=" + str(compiler),
                      "-DCMAKE_C_COMPILER_TARGET=x86_64-linux-musl", "-DCMAKE_CXX_COMPILER_TARGET=x86_64-linux-musl",
                      "-DCMAKE_ASM_COMPILER_TARGET=x86_64-linux-musl", "-DCMAKE_SYSROOT=" + str(sysroot),
                      "-DCMAKE_AR=" + str(llvm_bin() / "llvm-ar"), "-DCMAKE_RANLIB=" + str(llvm_bin() / "llvm-ranlib"),
                      "-DCMAKE_POSITION_INDEPENDENT_CODE=ON", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"]
            builtins = work / "builtins"
            subprocess.run(["cmake", "-S", str(source / "compiler-rt/lib/builtins"), "-B", str(builtins)] + common +
                           ["-DCMAKE_INSTALL_PREFIX=" + str(overlay), "-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON",
                            "-DCOMPILER_RT_BAREMETAL_BUILD=ON", "-DCOMPILER_RT_INCLUDE_TESTS=OFF"], env=env, check=True)
            subprocess.run(["cmake", "--build", str(builtins), "--parallel", "8"], env=env, check=True)
            subprocess.run(["cmake", "--install", str(builtins)], env=env, check=True)
            runtime = work / "cxx"
            flags = "-fuse-ld=" + str(llvm_bin() / "ld.lld") + " --rtlib=compiler-rt -resource-dir=" + str(overlay)
            subprocess.run(["cmake", "-S", str(source / "runtimes"), "-B", str(runtime)] + common +
                           ["-DCMAKE_INSTALL_PREFIX=" + str(destination), "-DLLVM_ENABLE_RUNTIMES=libunwind;libcxxabi;libcxx",
                            "-DCMAKE_C_FLAGS=" + flags, "-DCMAKE_CXX_FLAGS=" + flags,
                            "-DLIBCXX_HAS_MUSL_LIBC=ON", "-DLIBCXX_ENABLE_SHARED=OFF", "-DLIBCXX_ENABLE_STATIC=ON",
                            "-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON", "-DLIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY=ON",
                            "-DLIBCXXABI_ENABLE_SHARED=OFF", "-DLIBCXXABI_ENABLE_STATIC=ON", "-DLIBCXXABI_USE_LLVM_UNWINDER=ON",
                            "-DLIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY=ON", "-DLIBUNWIND_ENABLE_SHARED=OFF",
                            "-DLIBUNWIND_ENABLE_STATIC=ON", "-DLIBUNWIND_USE_COMPILER_RT=ON", "-DLIBCXX_USE_COMPILER_RT=ON",
                            "-DLIBCXXABI_USE_COMPILER_RT=ON", "-DLIBCXX_INCLUDE_TESTS=OFF", "-DLIBCXXABI_INCLUDE_TESTS=OFF",
                            "-DLIBUNWIND_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_TESTS=OFF"], env=env, check=True)
            subprocess.run(["cmake", "--build", str(runtime), "--parallel", "8"], env=env, check=True)
            subprocess.run(["cmake", "--install", str(runtime)], env=env, check=True)
            write_json(destination / ".complete.json", {"source_sha256": spec["sha256"], "musl_sha256": sha256(musl_libraries / "libc.so")})
        notices = destination / "licenses"
        notices.mkdir(exist_ok=True)
        for component in ("libcxx", "libcxxabi", "libunwind", "compiler-rt"):
            shutil.copyfile(source / component / "LICENSE.TXT", notices / (component + ".txt"))
        shutil.copyfile(source / "libc/LICENSE.TXT", notices / "llvm-libc-headers.txt")
        musl_notice = Path("/usr/share/doc/musl/copyright")
        if not musl_notice.is_file():
            raise RuntimeError("musl runtime's installed license notice is missing")
        shutil.copyfile(musl_notice, notices / "musl.txt")
        wrappers = BUILD / "toolchains/wrappers/linux-musl"
        wrappers.mkdir(parents=True, exist_ok=True)
        for name, operation in (("cc", "clang"), ("cxx", "clang++"), ("ar", "llvm-ar"), ("ranlib", "llvm-ranlib")):
            flags = ""
            if name in ("cc", "cxx"):
                flags = " --target=x86_64-linux-musl --sysroot=" + shlex.quote(str(destination / "sysroot"))
                flags += " -fuse-ld=" + shlex.quote(str(llvm_bin() / "ld.lld")) + " --rtlib=compiler-rt --unwindlib=libunwind -static-libgcc"
                flags += " -resource-dir=" + shlex.quote(str(destination / "clang-resource")) + " -L" + shlex.quote(str(destination / "lib"))
                flags += " -Wno-unused-command-line-argument"
                if name == "cxx":
                    flags += " -stdlib=libc++ -static-libstdc++ -nostdinc++ -isystem " + shlex.quote(str(destination / "include/c++/v1"))
            wrapper = wrappers / name
            install_wrapper(wrapper, '#!/bin/sh\nexec ' + shlex.quote(str(llvm_bin() / operation)) + flags + ' "$@"\n')
        write_json(wrappers / "toolchain.json", {"runtime": str(destination), "source": str(source), "llvm": str(llvm_directory()), "key": key})
        artifacts.register(BUILD, destination, "toolchains/musl-runtimes", spec["sha256"],
                           [source.relative_to(BUILD).as_posix(), llvm_directory().relative_to(BUILD).as_posix()])
        print("Clang musl toolchain ready: " + str(wrappers), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=("llvm", "musl", "gn", "cmake"))
    args = parser.parse_args()
    BUILD.mkdir(exist_ok=True)
    try:
        if args.component == "cmake":
            if os.name == "nt":
                from ci import linux_path
                subprocess.run(["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B",
                                linux_path(Path(__file__)), "cmake"], check=True)
            else:
                print(prepare_cmake())
        elif args.component == "gn":
            print(prepare_gn())
        elif args.component == "musl":
            prepare_musl()
        else:
            prepare_llvm()
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
