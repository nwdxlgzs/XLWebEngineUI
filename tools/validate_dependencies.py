#!/usr/bin/env python3
"""Validate real dependency calls, native presentation and compressed ICU data."""
import sys
sys.dont_write_bytecode = True
import argparse
import os
from pathlib import Path
import subprocess

from ci_support import read_json, write_json, WorkLock
from dependency_support import ROOT, BUILD


def validate(target, jobs=6):
    from ci import Commands, target_environment
    env, flags = target_environment(target)
    report_path = BUILD / "reports/dependency-validation" / (target + ".json")
    command = Commands(report_path.with_suffix(".log"), env, verbose=False)
    report = {"target": target, "configuration": "Release", "state": "running", "sdk_merged": False,
              "legacy_native": "not-verified"}
    with WorkLock(BUILD / "state" / ("dependency-validation-" + target + ".lock")):
        try:
            common = ["--target", target, "--configuration", "Release", "--jobs", str(jobs)]
            if target == "linux-x64-musl":
                command.run([sys.executable, "-B", ROOT / "tools/platform_deps.py", "x11", "--jobs", str(jobs)])
            command.run([sys.executable, "-B", ROOT / "tools/deps.py", "build"] + common)
            # This pass also builds the pinned host resource compressor.
            command.run([sys.executable, "-B", ROOT / "tools/deps.py", "verify"] + common)
            first = read_json(BUILD / "deps/integration" / target / "Release/report.json")
            work = BUILD / first["work"]
            encoder = work / ("xl_resource_pack.exe" if os.name == "nt" else "xl_resource_pack")
            generated = BUILD / "generated/resources" / target
            from icu_data import prepare as prepare_icu_data
            from dependency_support import extract_source
            lock = read_json(ROOT / 'third_party/dependencies.lock.json')['dependencies']
            filtered = prepare_icu_data(extract_source(lock['icu'], 'icu'), BUILD / first['components']['icu']['prefix'],
                                        target, 'Release', env, flags, jobs)
            command.run([sys.executable, "-B", ROOT / "tools/embed_resources.py", "--encoder", encoder,
                         "--output", generated, '--icu-data', filtered])
            command.run([sys.executable, "-B", ROOT / "tools/deps.py", "verify", "--native", "--embedded-catalog", generated] + common)
            final = read_json(BUILD / "deps/integration" / target / "Release/report.json")
            if (final.get("state") != "passed" or not final.get("native_window_required")
                    or "musl_abi" not in final and target == "linux-x64-musl"):
                raise RuntimeError("dependency validation did not establish the required native and ABI evidence")
            report.update(state="passed", integration_key=final["key"], work=final["work"],
                          embedded_catalog=read_json(generated / "manifest.json")["key"],
                          musl_abi=final.get("musl_abi"), decoder_compression_symbols=final.get("decoder_compression_symbols"))
        except Exception as error:
            report.update(state="failed", error=str(error))
            print(str(error), file=sys.stderr, flush=True)
        write_json(report_path, report)
    return 0 if report["state"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("windows-x64", "linux-x64-musl"), required=True)
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    if os.name == "nt" and args.target.startswith("linux-"):
        script = subprocess.check_output(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-a", "-u", str(Path(__file__).resolve())], text=True).strip()
        return subprocess.call(["wsl.exe", "-d", "Ubuntu", "--exec", "python3", "-B", script] + sys.argv[1:])
    return validate(args.target, min(args.jobs, 8))


if __name__ == "__main__":
    sys.exit(main())
