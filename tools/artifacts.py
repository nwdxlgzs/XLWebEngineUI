"""Registered disposable artifacts with leases, references and bounded retention."""
import contextlib
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import time
import uuid

from ci_support import WorkLock, checked_child, host_identity, process_started_at, read_json, record_active, write_json


def artifact_key(relative):
    return hashlib.sha256(relative.encode("utf-8")).hexdigest()


def locations(build, path):
    build = Path(build)
    path = checked_child(build, path)
    relative = path.relative_to(build).as_posix()
    key = artifact_key(relative)
    return path, relative, build / "state/artifacts" / (key + ".json"), build / "state/artifact-locks" / (key + ".lock")


def registration(path, relative, family, content_key, references, old, last_used=None):
    return {"schema": 1, "path": relative, "family": family, "content_key": content_key,
            "references": list(references), "created_at": old.get("created_at", time.time()),
            "last_used": time.time() if last_used is None else last_used,
            "kind": "directory" if path.is_dir() else "file"}


def register(build, path, family, content_key=None, references=(), last_used=None):
    path, relative, manifest, lock = locations(build, path)
    if not path.exists():
        raise ValueError("cannot register a missing artifact: " + relative)
    with WorkLock(lock):
        old = read_json(manifest, {})
        record = registration(path, relative, family, content_key, references, old, last_used)
        write_json(manifest, record)
    return path


@contextlib.contextmanager
def lease(build, path, owner_pid=None):
    """A short metadata lock serializes GC with lease creation, not artifact use."""
    path, relative, manifest, lock = locations(build, path)
    token = uuid.uuid4().hex
    owner_pid = os.getpid() if owner_pid is None else owner_pid
    if not isinstance(owner_pid, int) or owner_pid <= 0:
        raise ValueError("lease owner must be a positive process id")
    lease_path = Path(build) / "state/artifact-leases" / artifact_key(relative) / (token + ".json")
    with WorkLock(lock):
        record = read_json(manifest)
        if not record or not path.exists():
            raise ValueError("artifact has not been registered: " + relative)
        write_json(lease_path, {"state": "running", "host": host_identity(), "pid": owner_pid,
                                "process_started_at": process_started_at(owner_pid), "started_at": time.time()})
        record["last_used"] = time.time()
        write_json(manifest, record)
    try:
        yield path
    finally:
        lease_path.unlink(missing_ok=True)


@contextlib.contextmanager
def registered_directory(build, path, family):
    """Create, register and lease a unique output directory under one GC lock."""
    path, relative, manifest, lock = locations(build, path)
    token = uuid.uuid4().hex
    lease_path = Path(build) / "state/artifact-leases" / artifact_key(relative) / (token + ".json")
    with WorkLock(lock):
        path.mkdir(parents=True)  # An existing directory is never adopted here.
        write_json(manifest, registration(path, relative, family, None, (), {}))
        write_json(lease_path, {"state": "running", "host": host_identity(), "pid": os.getpid(),
                                "process_started_at": process_started_at(os.getpid()), "started_at": time.time()})
    try:
        yield path
    finally:
        # Complete/failing work is ordered by its last use, not just its start.
        with WorkLock(lock):
            record = read_json(manifest)
            if record:
                record["last_used"] = time.time()
                write_json(manifest, record)
            lease_path.unlink(missing_ok=True)


def pinned_content_keys(root):
    keys = set()

    def visit(value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key == "sha256" and isinstance(item, str):
                    keys.add(item)
                else:
                    visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)

    for lockfile in (Path(root) / "third_party").glob("*.lock.json"):
        visit(read_json(lockfile))
    return keys


def live_references(build):
    """Retained work directories keep the exact dependency versions they use."""
    references = set()
    for metadata in (Path(build) / "work").glob("*/*/*/.ci-work.json"):
        checked_child(build, metadata)
        info = read_json(metadata, {})
        references.update(info.get("artifact_references", []))
    return references


def active_leases(build, relative):
    directory = Path(build) / "state/artifact-leases" / artifact_key(relative)
    for metadata in directory.glob("*.json"):
        checked_child(build, metadata)
        if record_active(read_json(metadata, {})):
            return True
    return False


def collect(build, retain=3, pinned=(), dry_run=False, families=None):
    if retain < 1:
        raise ValueError("retention must be positive")
    build = Path(build)
    registry = build / "state/artifacts"
    records = {}
    for manifest in registry.glob("*.json"):
        checked_child(build, manifest)
        record = read_json(manifest)
        if not record:
            continue
        path, relative, expected, _ = locations(build, build / record["path"])
        if expected != manifest:
            raise ValueError("artifact registry key does not match its path")
        if path.exists():
            records[relative] = (record, manifest)
        elif not dry_run:
            manifest.unlink()
    protected = set(live_references(build))
    pinned = set(pinned)
    for relative, (record, _) in records.items():
        if record.get("content_key") in pinned or active_leases(build, relative):
            protected.add(relative)
    groups = {}
    for relative, (record, _) in records.items():
        groups.setdefault(record["family"], []).append((record["last_used"], relative))
    # Retained validation/catalog artifacts must keep their dependency closure
    # even after the producing process and its leases have ended.
    for entries in groups.values():
        entries.sort(reverse=True)
        protected.update(relative for _, relative in entries[:retain])
    # References can form a chain (work -> compiler -> download).
    pending = list(protected)
    while pending:
        pair = records.get(pending.pop())
        if not pair:
            continue
        for reference in pair[0].get("references", []):
            if reference not in protected:
                protected.add(reference)
                pending.append(reference)
    candidates = []
    for entries in groups.values():
        candidates.extend(relative for _, relative in entries[retain:] if relative not in protected)
    removed = []
    for relative in candidates:
        if families is not None and records[relative][0]['family'] not in families:
            continue
        path, _, manifest, lock = locations(build, build / relative)
        try:
            with WorkLock(lock):
                current = read_json(manifest)
                if not current or active_leases(build, relative):
                    continue
                # Another process may have used it after the retention snapshot.
                if current["last_used"] != records[relative][0]["last_used"]:
                    continue
                if not dry_run:
                    if current["kind"] == "directory":
                        shutil.rmtree(path)
                    else:
                        path.unlink()
                    manifest.unlink()
                removed.append(relative)
        except RuntimeError:
            continue  # metadata currently in use; next GC will try again
    return removed


def prune_local_leases(build, dry_run=False):
    """Each OS retires only its own dead leases in a shared Windows/WSL tree."""
    removed = []
    for directory in (Path(build) / 'state/artifact-leases').glob('*'):
        checked_child(build, directory)
        if not directory.is_dir():
            continue
        for metadata in directory.glob('*.json'):
            checked_child(build, metadata)
            record = read_json(metadata, {})
            record.setdefault('started_at', metadata.stat().st_mtime)
            if record.get('host') != host_identity() or record_active(record):
                continue
            if not dry_run:
                metadata.unlink(missing_ok=True)
            removed.append(metadata.relative_to(build).as_posix())
    return removed


def register_report_files(build):
    """Bring older ad-hoc logs and screenshots under the current retention rule."""
    folder = Path(build) / 'reports'
    if not folder.exists():
        return
    checked_child(build, folder)
    kinds = {'.log': 'logs', '.json': 'snapshots', '.png': 'screenshots', '.ppm': 'screenshots', '.html': 'pages', '.bin': 'probes'}
    for path in folder.iterdir():
        checked_child(build, path)
        if not path.is_file() or path.suffix not in kinds:
            continue
        prefix = path.stem.split('-', 1)[0]
        target = 'windows-x64' if prefix in ('windows', 'win') else 'linux-x64-musl' if prefix in ('musl', 'linux') else 'common'
        kind = 'full-logs' if path.suffix == '.log' and 'full' in path.stem else kinds[path.suffix]
        register(build, path, 'reports/{}/{}'.format(kind, target), last_used=path.stat().st_mtime)
    legacy = folder / 'whitespace-reference'
    if legacy.exists():
        checked_child(build, legacy)
        if legacy.is_dir() and (legacy / 'dump.html').is_file() and (legacy / 'reference.html').is_file():
            register(build, legacy, 'reports/text-reference/windows-x64', last_used=max(p.stat().st_mtime for p in legacy.iterdir()))


def compiler_cache(build, toolchain_key, generation):
    root = Path(build) / "cache/llvm" / toolchain_key / generation
    root.mkdir(parents=True, exist_ok=True)
    register(build, root, "compiler-cache/llvm")
    return root
