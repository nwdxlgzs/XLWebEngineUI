"""Small, stdlib-only primitives shared by local CI and its contract tests."""
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import time
from pathlib import Path


def host_identity():
    # WSL and Windows may share a hostname but never share a PID namespace.
    return "{}|{}|{}".format(platform.system(), platform.node(), platform.release())


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".{}.tmp".format(os.getpid()))
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(str(temporary), str(path))


def read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        return default


def relocate_directory(source, destination):
    """Bounded retry for Windows scanners holding freshly installed SDK files."""
    source, destination = Path(source), Path(destination)
    if destination.exists():
        raise FileExistsError(str(destination))
    for attempt in range(8):
        try:
            source.rename(destination)
            return
        except PermissionError as error:
            if os.name != "nt" or getattr(error, "winerror", None) not in (5, 32, 33) or attempt == 7:
                raise
            time.sleep(min(0.05 * (2 ** attempt), 0.5))


def linklike(path):
    info = Path(path).lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def checked_child(build_root, candidate):
    """Refuse root deletion, lexical escapes, symlinks and Windows junctions."""
    root = Path(build_root).absolute()
    path = Path(candidate).absolute()
    if linklike(root):
        raise ValueError("build root is a link or junction")
    try:
        relative = path.relative_to(root)
    except ValueError:
        raise ValueError("cleanup target is outside build")
    if not relative.parts or any(part in (".", "..") for part in relative.parts):
        raise ValueError("cleanup cannot delete build root or traverse parents")
    current = root
    for part in relative.parts:
        current = current / part
        if current.exists() or current.is_symlink():
            if linklike(current):
                raise ValueError("cleanup cannot traverse a link or junction")
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        raise ValueError("resolved cleanup target is outside build")
    return path


def process_alive(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    if os.name == "nt":
        # os.kill(pid, 0) is not a portable Windows liveness probe.
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            return ctypes.get_last_error() == 5  # access denied: protect conservatively
        try:
            code = wintypes.DWORD()
            return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def process_started_at(pid):
    """Identify a PID generation; reuse after a crash must not pin old output."""
    if not isinstance(pid, int) or pid <= 0:
        return None
    try:
        if os.name == 'nt':
            import ctypes
            from ctypes import wintypes
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
            kernel.GetProcessTimes.restype = wintypes.BOOL
            handle = kernel.OpenProcess(0x1000, False, pid)
            if not handle:
                return None
            try:
                times = [wintypes.FILETIME() for _ in range(4)]
                if not kernel.GetProcessTimes(handle, *[ctypes.byref(value) for value in times]):
                    return None
                value = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
                return value / 10000000 - 11644473600
            finally:
                kernel.CloseHandle(handle)
        if platform.system() == 'Linux':
            # comm may contain spaces and closing parentheses; field 22 follows
            # the final ')' and nineteen fields beginning with field 3 (state).
            fields = Path('/proc/{}/stat'.format(pid)).read_text().rsplit(')', 1)[1].split()
            boot = next(float(line.split()[1]) for line in Path('/proc/stat').read_text().splitlines() if line.startswith('btime '))
            return boot + int(fields[19]) / os.sysconf('SC_CLK_TCK')
    except (OSError, ValueError, IndexError, StopIteration):
        pass
    return None


def record_active(record):
    if record.get("state") in ("passed", "failed", "interrupted"):
        return False
    if record.get("host") != host_identity():
        return True  # a foreign host must verify its own running jobs
    pid = record.get('pid')
    if not process_alive(pid):
        return False
    birth = process_started_at(pid)
    expected = record.get('process_started_at')
    if birth is not None and expected is not None:
        return abs(birth - expected) < .1
    recorded = record.get('started_at', record.get('last_used'))
    if birth is not None and recorded is not None and birth > recorded + 1:
        return False
    return True


class WorkLock:
    def __init__(self, path):
        self.path = Path(path)

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor = os.open(str(self.path), os.O_CREAT | os.O_RDWR)
        self.stream = os.fdopen(descriptor, "r+b", buffering=0)
        self.stream.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.stream.close()
            raise RuntimeError("work directory is locked: {}".format(self.path))
        self.stream.seek(0)
        self.stream.write(json.dumps({"state": "running", "pid": os.getpid(), "host": host_identity()}).encode("utf-8"))
        self.stream.truncate()
        return self

    def __exit__(self, *_):
        self.stream.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(self.stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(self.stream.fileno(), fcntl.LOCK_UN)
        # Keep the inode: unlinking lock files races a new lock owner on POSIX.
        self.stream.close()


def work_lock_path(build_root, work):
    relative = Path(work).relative_to(Path(build_root)).as_posix()
    key = hashlib.sha256(relative.encode("utf-8")).hexdigest()
    return Path(build_root) / "state/work-locks" / (key + ".lock")


def cleanup_work(build_root, retain=3, dry_run=False):
    root = Path(build_root)
    groups = {}
    for info_path in (root / "work").glob("*/*/*/.ci-work.json"):
        work = checked_child(root, info_path.parent)
        info = read_json(info_path)
        if not info or info.get("host") != host_identity() or record_active(info):
            continue
        groups.setdefault((info["target"], info["configuration"]), []).append((info["last_used"], work))
    removed = []
    for entries in groups.values():
        entries.sort(key=lambda entry: (entry[0], str(entry[1])), reverse=True)
        for _, work in entries[retain:]:
            try:
                with WorkLock(work_lock_path(root, work)):
                    info = read_json(work / ".ci-work.json")
                    if not info or record_active(info):
                        continue
                    checked_child(root, work)
                    if not dry_run:
                        shutil.rmtree(work)
                    removed.append(str(work))
            except RuntimeError:
                continue
    return removed


def source_manifest(root):
    completed = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                               cwd=str(root), stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    manifest = {}
    for name in completed.stdout.decode("utf-8").split("\0"):
        if not name or name in ("WORKMEM.MD", "TODO.MD") or name.startswith("build/"):
            continue
        path = Path(root) / name
        if path.is_file():
            manifest[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    return dict(sorted(manifest.items()))


def manifest_digest(manifest):
    return hashlib.sha256(json.dumps(manifest, sort_keys=True).encode("utf-8")).hexdigest()


def select_impact(manifest, baseline, settings):
    if baseline is None:
        return {"scope": "global", "modules": sorted(settings["module_paths"]),
                "changed": sorted(manifest), "reason": "no successful baseline"}
    changed = sorted(name for name in set(manifest) | set(baseline) if manifest.get(name) != baseline.get(name))
    if not changed:
        return {"scope": "docs", "modules": [], "changed": [], "reason": "no source changes"}
    modules = set()
    global_change = False
    for name in changed:
        if name.lower().endswith(".md") or name in (".gitignore", ".gitattributes"):
            continue
        if any(name == prefix or name.startswith(prefix) for prefix in settings["global_paths"]):
            global_change = True
            continue
        matched = [module for module, prefixes in settings["module_paths"].items()
                   if any(name == prefix or name.startswith(prefix) for prefix in prefixes)]
        if not matched:
            global_change = True
        modules.update(matched)
    if global_change:
        return {"scope": "global", "modules": sorted(settings["module_paths"]),
                "changed": changed, "reason": "build/toolchain/public or unknown impact"}
    pending = list(modules)
    while pending:
        for dependent in settings["dependents"].get(pending.pop(), []):
            if dependent not in modules:
                modules.add(dependent)
                pending.append(dependent)
    return {"scope": "modules" if modules else "docs", "modules": sorted(modules),
            "changed": changed, "reason": "changed modules and transitive consumers"}


def retention_candidates(build_root, retain):
    if retain < 1:
        raise ValueError("retention must be positive")
    root = Path(build_root)
    candidates = []
    runs = root / "runs"
    if not runs.exists():
        return candidates
    for bucket in sorted(runs.iterdir()):
        checked_child(root, bucket)
        if not bucket.is_dir():
            continue
        finished = []
        for run in bucket.iterdir():
            checked_child(root, run)
            record = read_json(run / "report.json")
            if not record or record_active(record):
                continue
            finished.append((float(record.get("finished_at", run.stat().st_mtime)), run))
        finished.sort(key=lambda item: (item[0], item[1].name), reverse=True)
        candidates.extend(path for _, path in finished[retain:])
    return candidates


def cleanup_runs(build_root, retain=3, dry_run=False):
    candidates = retention_candidates(build_root, retain)
    for candidate in candidates:
        checked_child(build_root, candidate)
        if not dry_run:
            shutil.rmtree(candidate)
    return [str(path) for path in candidates]


def cleanup_package_staging(build_root, dry_run=False):
    """CPack's expanded temporary copies are not retained SDK deliverables."""
    root = Path(build_root)
    removed = []
    for result in (root / 'runs').glob('*/*/*/result.json'):
        checked_child(root, result)
        run = result.parent.parent
        outer = read_json(run / 'report.json', {})
        target = read_json(result, {})
        if not outer or record_active(outer) or record_active(target):
            continue
        staging = result.parent / 'packages/_CPack_Packages'
        if staging.exists():
            checked_child(root, staging)
            if not dry_run:
                shutil.rmtree(staging)
            removed.append(str(staging))
    return removed


def cleanup_validation_scratch(build_root, dry_run=False):
    """Drop completed SDK copies; retain test logs, audit and final archives."""
    root = Path(build_root)
    results = list((root / 'runs').glob('*/*/*/result.json'))
    results += list((root / 'runs').glob('*/*/result.json'))  # older development runs
    removed = []
    for result in results:
        removed_before = len(removed)
        checked_child(root, result)
        target = read_json(result, {})
        if target.get('host') != host_identity() or target.get('state') != 'passed':
            continue
        run = result.parent.parent
        outer = read_json(run / 'report.json')
        if outer and record_active(outer):
            continue
        if outer and outer.get('mode') in ('full', 'release'):
            if read_json(run / 'sdk-audit.json', {}).get('state') != 'passed':
                continue
        for config in ('Debug', 'Release'):
            folder = result.parent / config
            if not folder.is_dir():
                continue
            checked_child(root, folder)
            if not dry_run:
                evidence = checked_child(root, folder / 'evidence')
                for relative, name in (('consumer-build/Testing/Temporary/LastTest.log', 'consumer-tests.log'),
                                       ('consumer-build/Testing/Temporary/LastTestsFailed.log', 'consumer-failures.log'),
                                       ('sdk-relocated/share/doc/XLWebEngineUI/RESOURCE-BUILD-INFO.json', 'RESOURCE-BUILD-INFO.json')):
                    source = checked_child(root, folder / relative)
                    if source.is_file():
                        evidence.mkdir(exist_ok=True)
                        shutil.copyfile(source, evidence / name)
            for name in ('consumer-build', 'consumer-source', 'sdk-relocated', 'sdk-install', 'symbol-files'):
                path = checked_child(root, folder / name)
                if path.is_dir():
                    if not dry_run:
                        shutil.rmtree(path)
                    removed.append(str(path))
        if not dry_run and len(removed) > removed_before:
            target['validation_scratch'] = 'released; evidence logs and final archives retained'
            write_json(result, target)
    return removed
