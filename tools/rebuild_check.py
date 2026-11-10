#!/usr/bin/env python3
"""Prove a committed source tree can recover after its entire build/ is deleted."""
import sys
sys.dont_write_bytecode = True
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
import argparse
import json
import datetime
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import time
import uuid

from ci_support import checked_child, cleanup_runs, host_identity, write_json
from dependency_support import sha256

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def seed_specifications(clone, target):
    lock = json.loads((clone / "third_party/toolchains.lock.json").read_text(encoding="utf-8"))
    if target == "windows-x64":
        result = [lock["llvm-windows-x64"]]
    else:
        result = list(lock["llvm-linux-x64"]["packages"])
        if target == "linux-x64-musl":
            result.append(lock["llvm-runtimes-source"])
    dependencies = clone / "third_party/dependencies.lock.json"
    if dependencies.is_file():
        result.extend(json.loads(dependencies.read_text(encoding="utf-8"))["dependencies"].values())
    for filename in ('skia-sources.lock.json', 'graphics-sources.lock.json'):
        locked = clone / 'third_party' / filename
        if locked.is_file():
            result.extend(json.loads(locked.read_text(encoding='utf-8'))['dependencies'].values())
    return [entry for spec in result for entry in (spec['files'] if spec.get('files') else [spec])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("windows-x64", "linux-x64-musl", "linux-x64-glibc-dev"),
                        default="windows-x64" if os.name == "nt" else "linux-x64-musl")
    parser.add_argument("--seed-downloads", type=Path,
                        help="optional saved source archives, checked against the cloned lockfile before each pass")
    args = parser.parse_args()
    # Keep this source root short: its own CI will create a second build tree.
    run_id = uuid.uuid4().hex[:10]
    bucket = "rebuild--" + ("win" if args.target == "windows-x64" else args.target.replace("linux-x64-", ""))
    run = BUILD / "runs" / bucket / run_id
    run.mkdir(parents=True)
    clone = run / "s"
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=str(ROOT), text=True).strip()
    record = {"state": "running", "pid": os.getpid(), "host": host_identity(), "started_at": time.time(),
              "revision": revision, "target": args.target, "passes": []}
    try:
        with (run / "clone.log").open("w", encoding="utf-8") as output:
            subprocess.run(["git", "clone", "--no-hardlinks", "--no-local", str(ROOT), str(clone)],
                           stdout=output, stderr=subprocess.STDOUT, check=True)
        original_raw = hashlib.sha256((clone / "RAW-PLAN.MD").read_bytes()).hexdigest()
        for index in (1, 2):
            if args.seed_downloads:
                downloads = clone / "build/downloads"
                downloads.mkdir(parents=True)
                saved = []
                for specification in seed_specifications(clone, args.target):
                    source = args.seed_downloads.resolve() / specification["archive"]
                    if (not source.is_file() or source.stat().st_size != specification["bytes"]
                            or sha256(source) != specification["sha256"]):
                        raise RuntimeError("saved archive does not match cloned lock: " + specification["archive"])
                    shutil.copyfile(source, downloads / specification["archive"])
                    saved.append({"archive": specification["archive"], "sha256": specification["sha256"]})
                record["download_source"] = {"kind": "saved-verified-source-archives", "archives": saved}
            write_json(run / "report.json", record)
            print("Recovery pass {}: clean build from committed source {}".format(index, revision[:12]), flush=True)
            log = run / ("pass-{}.log".format(index))
            with log.open("w", encoding="utf-8") as output:
                result = subprocess.run([sys.executable, "-B", str(clone / "tools/ci.py"), "quick", "--targets", args.target],
                                        cwd=str(clone), stdout=output, stderr=subprocess.STDOUT)
            if result.returncode:
                print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-50:]), flush=True)
                raise RuntimeError("recovery pass {} failed".format(index))
            if subprocess.check_output(["git", "status", "--porcelain"], cwd=str(clone), text=True).strip():
                raise RuntimeError("build modified committed source or generated files outside build")
            if hashlib.sha256((clone / "RAW-PLAN.MD").read_bytes()).hexdigest() != original_raw:
                raise RuntimeError("RAW was modified")
            record["passes"].append({"index": index, "state": "passed"})
            print("Recovery pass {} passed".format(index), flush=True)
            if index == 1:
                disposable = checked_child(BUILD, clone / "build")
                print("Deleting verified disposable directory: " + str(disposable), flush=True)
                shutil.rmtree(disposable)
                if disposable.exists():
                    raise RuntimeError("build directory survived removal")
                record["entire_build_deleted"] = True
        record["state"] = "passed"
        return 0
    except Exception as error:
        record.update(state="failed", error=str(error))
        print("ERROR: " + str(error), flush=True)
        return 1
    finally:
        record["finished_at"] = time.time()
        write_json(run / "report.json", record)
        print("Report: " + str(run / "report.json"), flush=True)
        cleanup_runs(BUILD, 5)


if __name__ == "__main__":
    sys.exit(main())
