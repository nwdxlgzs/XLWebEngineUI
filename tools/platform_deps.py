#!/usr/bin/env python3
"""Build the locked native Linux platform stack with Clang/musl."""
import sys
sys.dont_write_bytecode = True
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import shutil

from ci_support import WorkLock, read_json, write_json
from dependency_support import ROOT, BUILD, extract_source
import artifacts


def extension_key(base_key, compiler_key, configuration):
    identity = {"sources": read_json(ROOT / "third_party/platform-extensions.lock.json")["dependencies"],
                "recipes": read_json(ROOT / "third_party/recipes/x11-extensions.json")["components"],
                "base_key": base_key, "compiler_key": compiler_key, "configuration": configuration,
                "driver_revision": 1}
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:12]


def build_extensions(configuration, jobs):
    from ci import target_environment, Commands
    from deps import dependency_compiler_identity
    env, flags = target_environment("linux-x64-musl")
    compiler_key, versions = dependency_compiler_identity("linux-x64-musl", env, flags)
    base = read_json(BUILD / "reports/platform-x11" / (configuration + ".json"))
    if base.get("state") != "passed":
        raise RuntimeError("X11 base must be built before its extensions")
    sources = read_json(ROOT / "third_party/platform-extensions.lock.json")["dependencies"]
    recipes = read_json(ROOT / "third_party/recipes/x11-extensions.json")["components"]
    key = extension_key(base["key"], compiler_key, configuration)
    prefix = BUILD / "deps/platform-extensions/linux-x64-musl" / key / configuration
    prefix.mkdir(parents=True, exist_ok=True)
    base_prefix = BUILD / base["prefix"]
    references = [base["prefix"]]
    artifacts.register(BUILD, prefix, "platform-extension-install/linux-x64-musl/" + configuration, key, references)
    wrappers = BUILD / "toolchains/wrappers/linux-musl"
    pc_dirs = [p / relative for p in (prefix, base_prefix) for relative in ("lib/pkgconfig", "share/pkgconfig")]
    env.update(CC=str(wrappers / "cc"), CXX=str(wrappers / "cxx"), AR=str(wrappers / "ar"),
               RANLIB=str(wrappers / "ranlib"), CFLAGS="-O0 -g -fPIC" if configuration == "Debug" else "-O2 -g -fPIC",
               PKG_CONFIG_LIBDIR=os.pathsep.join(map(str, pc_dirs)), PKG_CONFIG_PATH=os.pathsep.join(map(str, pc_dirs)),
               PYTHON=sys.executable, PYTHONDONTWRITEBYTECODE="1")
    report = {"key": key, "base_key": base["key"], "state": "running", "compiler": versions,
              "prefix": prefix.relative_to(BUILD).as_posix(), "components": {}, "errors": []}
    report_path = BUILD / "reports/platform-x11-extensions" / (configuration + ".json")
    with WorkLock(BUILD / "state" / ("platform-x11-extensions-" + key + ".lock")), contextlib.ExitStack() as leases:
        leases.enter_context(artifacts.lease(BUILD, prefix))
        leases.enter_context(artifacts.lease(BUILD, base_prefix))
        for name, recipe in recipes.items():
            try:
                source = extract_source(sources[name], name)
                references.append(source.relative_to(BUILD).as_posix())
                work = BUILD / "deps/platform-extension-work" / key / configuration / name
                work.mkdir(parents=True, exist_ok=True)
                artifacts.register(BUILD, work, "platform-extension-work/" + name + "/" + configuration, key,
                                   [source.relative_to(BUILD).as_posix(), base["prefix"]])
                stamp = read_json(work / ".build.json", {})
                if (stamp.get("key") != key or stamp.get("state") != "compiled-and-installed"
                        or not all((prefix / item).is_file() for item in recipe["libraries"])):
                    with artifacts.lease(BUILD, work), artifacts.lease(BUILD, source):
                        command = Commands(work / "commands.log", dict(env), verbose=False)
                        command.run(["sh", source / "configure", "--prefix=" + str(prefix), "--libdir=" + str(prefix / "lib"),
                                     "--host=x86_64-linux-musl", "--build=x86_64-pc-linux-gnu",
                                     "--disable-shared", "--enable-static"] + recipe["options"], cwd=work)
                        command.run(["make", "-j", str(jobs)], cwd=work)
                        command.run(["make", "install"], cwd=work)
                    for relative in sources[name]["license_files"]:
                        destination = prefix / "share/licenses" / name / relative
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(source / relative, destination)
                    stamp = {"key": key, "state": "compiled-and-installed", "source_sha256": sources[name]["sha256"],
                             "libraries": recipe["libraries"], "finished_at": time.time()}
                    write_json(work / ".build.json", stamp)
                report["components"][name] = stamp
                print("X11 extension " + name + ": compiled with musl and installed", flush=True)
            except Exception as error:
                report["errors"].append({"component": name, "error": str(error)})
                print(str(error), file=sys.stderr, flush=True)
            write_json(report_path, report)
        artifacts.register(BUILD, prefix, "platform-extension-install/linux-x64-musl/" + configuration, key, references)
        report["state"] = "failed" if report["errors"] else "passed"
        write_json(report_path, report)
    return 1 if report["errors"] else 0


def build_x11(configuration="Release", jobs=8):
    from ci import target_environment, Commands
    from deps import dependency_compiler_identity
    env, flags = target_environment("linux-x64-musl")
    compiler_key, versions = dependency_compiler_identity("linux-x64-musl", env, flags)
    lock = read_json(ROOT / "third_party/platform-sources.lock.json")["dependencies"]
    recipes = read_json(ROOT / "third_party/recipes/x11.json")["components"]
    identity = {"sources": lock, "recipes": recipes, "compiler_key": compiler_key,
                "configuration": configuration, "recipe_driver_revision": 1}
    key = hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()[:12]
    prefix = BUILD / "deps/platform/linux-x64-musl" / key / configuration
    prefix.mkdir(parents=True, exist_ok=True)
    report = {"key": key, "prefix": prefix.relative_to(BUILD).as_posix(), "configuration": configuration,
              "state": "running", "compiler": versions, "components": {}, "errors": []}
    report_path = BUILD / "reports/platform-x11" / (configuration + ".json")
    artifacts.register(BUILD, prefix, "platform-install/linux-x64-musl/" + configuration, key)
    wrappers = BUILD / "toolchains/wrappers/linux-musl"
    env.update(CC=str(wrappers / "cc"), CXX=str(wrappers / "cxx"), AR=str(wrappers / "ar"),
               RANLIB=str(wrappers / "ranlib"), CFLAGS="-O0 -g -fPIC" if configuration == "Debug" else "-O2 -g -fPIC",
               CXXFLAGS="-O0 -g -fPIC" if configuration == "Debug" else "-O2 -g -fPIC",
               PKG_CONFIG_PATH=str(prefix / "lib/pkgconfig") + os.pathsep + str(prefix / "share/pkgconfig"),
               PKG_CONFIG_LIBDIR=str(prefix / "lib/pkgconfig") + os.pathsep + str(prefix / "share/pkgconfig"),
               PYTHON=sys.executable, PYTHONDONTWRITEBYTECODE="1")
    completed = set()
    failures = {}
    references = []

    def build(name):
        if name in completed:
            return
        if name in failures:
            raise RuntimeError(name + ": an earlier build failed")
        try:
            for other in recipes[name]["depends"]:
                build(other)
            spec = lock[name]
            source = extract_source(spec,name)
            references.append(source.relative_to(BUILD).as_posix())
            work = BUILD / "deps/platform-work" / key / configuration / name
            work.mkdir(parents=True,exist_ok=True)
            artifacts.register(BUILD,work,"platform-work/" + name + "/" + configuration,key,[references[-1]])
            stamp = read_json(work / ".build.json",{})
            if stamp.get("key") != key or stamp.get("state") != "compiled-and-installed":
                command = Commands(work / "commands.log",env,verbose=False)
                with artifacts.lease(BUILD,source), artifacts.lease(BUILD,work):
                    command.run(["sh",source / "configure","--prefix=" + str(prefix),"--libdir=" + str(prefix / "lib"),
                                 "--host=x86_64-linux-musl","--build=x86_64-pc-linux-gnu",
                                 "--disable-shared","--enable-static"] + recipes[name]["options"],cwd=work)
                    command.run(["make","-j",str(jobs)],cwd=work)
                    command.run(["make","install"],cwd=work)
                licenses = prefix / "share/licenses" / name
                licenses.mkdir(parents=True,exist_ok=True)
                import shutil
                for relative in spec["license_files"]:
                    shutil.copyfile(source / relative,licenses / Path(relative).name)
                stamp = {"key": key,"state":"compiled-and-installed","source_sha256":spec["sha256"],
                         "revision":spec["revision"],"work":work.relative_to(BUILD).as_posix(),"finished_at":time.time()}
                write_json(work / ".build.json",stamp)
            completed.add(name)
            report["components"][name] = stamp
            write_json(report_path,report)
            print("X11 " + name + ": compiled with musl and installed",flush=True)
        except Exception as error:
            failures[name] = str(error)
            raise

    with WorkLock(BUILD / "state" / ("platform-x11-" + key + "-" + configuration + ".lock")), artifacts.lease(BUILD,prefix):
        for name in recipes:
            try:
                build(name)
            except Exception as error:
                report["errors"].append({"component":name,"error":str(error)})
                print(str(error),file=sys.stderr,flush=True)
        artifacts.register(BUILD,prefix,"platform-install/linux-x64-musl/" + configuration,key,references)
        report["state"] = "failed" if report["errors"] else "passed"
        write_json(report_path,report)
    return 1 if report["errors"] else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command",choices=("x11",))
    parser.add_argument("--configuration",choices=("Debug","Release"),default="Release")
    parser.add_argument("--jobs",type=int,default=8)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    if os.name == "nt":
        script = subprocess.check_output(["wsl.exe","-d","Ubuntu","--exec","wslpath","-a","-u",str(Path(__file__).resolve())],text=True).strip()
        return subprocess.call(["wsl.exe","-d","Ubuntu","--exec","python3","-B",script] + sys.argv[1:])
    result = build_x11(args.configuration,min(args.jobs,8))
    return result or build_extensions(args.configuration, min(args.jobs, 8))


if __name__ == "__main__":
    sys.exit(main())
