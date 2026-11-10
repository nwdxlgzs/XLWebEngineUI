"""Hash-locked downloads and source extraction, rooted in disposable build/."""
import hashlib
import concurrent.futures
import ctypes
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import time
import urllib.request
import uuid

from ci_support import WorkLock, checked_child, read_json, write_json
import artifacts

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def publish_directory(unpacked, destination):
    """Allow Windows scanners to release newly extracted directory handles."""
    for attempt in range(8):
        try:
            unpacked.rename(destination)
            return
        except PermissionError as error:
            if os.name != 'nt' or getattr(error, 'winerror', None) not in (5, 32, 33):
                raise
            if attempt == 7:
                # The caller owns the extraction lock. A success stamp is only
                # written after this completes, so partial copies cannot be used.
                shutil.copytree(unpacked, destination, symlinks=True, copy_function=shutil.copyfile)
                return
            time.sleep(min(.1 * (2 ** attempt), 1))


def decompress_zstd_with_git(source, destination):
    """Use Git for Windows' existing BSD-licensed decoder via stdlib ctypes.

    LLVM's official archive has a 1 GiB window, beyond libarchive's default.
    This avoids requiring an unpinned Python package for bootstrapping.
    """
    git = shutil.which("git")
    library = Path(git).resolve().parent.parent / "mingw64/bin/libzstd.dll" if git else None
    if os.name != "nt" or not library or not library.is_file():
        raise RuntimeError("Git for Windows with mingw64/bin/libzstd.dll is required to unpack LLVM's Zstd archive")
    with os.add_dll_directory(str(library.parent)):
        decoder = ctypes.CDLL(str(library))
    size = ctypes.c_size_t

    class Input(ctypes.Structure):
        _fields_ = [("src", ctypes.c_void_p), ("size", size), ("pos", size)]

    class Output(ctypes.Structure):
        _fields_ = [("dst", ctypes.c_void_p), ("size", size), ("pos", size)]

    decoder.ZSTD_createDStream.restype = ctypes.c_void_p
    decoder.ZSTD_freeDStream.argtypes = [ctypes.c_void_p]
    decoder.ZSTD_initDStream.argtypes = [ctypes.c_void_p]
    decoder.ZSTD_initDStream.restype = size
    decoder.ZSTD_DCtx_setMaxWindowSize.argtypes = [ctypes.c_void_p, size]
    decoder.ZSTD_DCtx_setMaxWindowSize.restype = size
    decoder.ZSTD_decompressStream.argtypes = [ctypes.c_void_p, ctypes.POINTER(Output), ctypes.POINTER(Input)]
    decoder.ZSTD_decompressStream.restype = size
    decoder.ZSTD_isError.argtypes = [size]
    decoder.ZSTD_isError.restype = ctypes.c_uint
    decoder.ZSTD_getErrorName.argtypes = [size]
    decoder.ZSTD_getErrorName.restype = ctypes.c_char_p

    def checked(result):
        if decoder.ZSTD_isError(result):
            raise RuntimeError(decoder.ZSTD_getErrorName(result).decode("utf-8"))
        return result

    context = decoder.ZSTD_createDStream()
    if not context:
        raise MemoryError("Zstd decoder allocation failed")
    try:
        checked(decoder.ZSTD_initDStream(context))
        checked(decoder.ZSTD_DCtx_setMaxWindowSize(context, 2 * 1024 * 1024 * 1024))
        output = ctypes.create_string_buffer(1024 * 1024)
        remaining = 1
        with Path(source).open("rb") as compressed, Path(destination).open("wb") as expanded:
            while True:
                data = compressed.read(128 * 1024)
                if not data:
                    break
                buffer = ctypes.create_string_buffer(data)
                incoming = Input(ctypes.cast(buffer, ctypes.c_void_p), len(data), 0)
                while True:
                    outgoing = Output(ctypes.cast(output, ctypes.c_void_p), len(output), 0)
                    remaining = checked(decoder.ZSTD_decompressStream(context, ctypes.byref(outgoing), ctypes.byref(incoming)))
                    expanded.write(output.raw[:outgoing.pos])
                    if incoming.pos == incoming.size and outgoing.pos < outgoing.size:
                        break
        if remaining:
            raise RuntimeError("truncated Zstd archive")
    finally:
        decoder.ZSTD_freeDStream(context)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def range_request_valid(headers, start, end, total, received):
    """Only bytes from the requested immutable range can extend a partial."""
    import re
    ranges = re.findall(r"(?im)^content-range:\s*bytes\s+(\d+)-(\d+)/(\d+)\s*$", headers)
    return bool(ranges and tuple(map(int, ranges[-1])) == (start, end, total)
                and 0 < received <= end - start + 1)


def download_ranges(spec, url, directory):
    """Bounded parallel HTTP ranges; partial chunks survive transient failures."""
    chunk_bytes = 2 * 1024 * 1024
    total = spec["bytes"]
    directory.mkdir(parents=True, exist_ok=True)
    jobs = list(range((total + chunk_bytes - 1) // chunk_bytes))
    curl = shutil.which("curl")
    if not curl:
        raise RuntimeError("curl is required for parallel resumable downloads")
    legacy = BUILD / "downloads" / (spec["archive"] + ".part")
    if legacy.is_file() and not any(directory.glob("*.chunk")):
        # Previous single-stream transfers wrote a contiguous prefix. Reuse
        # those bytes; only the final locked SHA256 can make them trusted.
        with legacy.open("rb") as previous:
            for index in jobs:
                data = previous.read(min(chunk_bytes, total - index * chunk_bytes))
                if not data:
                    break
                (directory / (str(index) + ".chunk")).write_bytes(data)

    def fetch(index):
        start = index * chunk_bytes
        end = min(total, start + chunk_bytes) - 1
        chunk = directory / (str(index) + ".chunk")
        required = end - start + 1
        if chunk.exists() and chunk.stat().st_size > required:
            chunk.unlink()
        for attempt in range(12):
            present = chunk.stat().st_size if chunk.exists() else 0
            if present == required:
                return
            offset = start + present
            piece = directory / (str(index) + ".piece")
            headers = directory / (str(index) + ".headers")
            result = subprocess.run([curl, "--fail", "--location", "--silent", "--show-error",
                                     "--connect-timeout", "15", "--max-time", "120", "--speed-time", "30",
                                     "--speed-limit", "1024", "--range", "{}-{}".format(offset, end),
                                     "--dump-header", str(headers), "--output", str(piece), url],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            received = piece.stat().st_size if piece.exists() else 0
            header_text = headers.read_text(encoding="iso-8859-1") if headers.exists() else ""
            if range_request_valid(header_text, offset, end, total, received):
                with chunk.open("ab") as output, piece.open("rb") as source:
                    shutil.copyfileobj(source, output)
            elif received or (headers.exists() and "200 OK" in header_text):
                raise RuntimeError("source did not honor the requested HTTP byte range")
            piece.unlink(missing_ok=True)
            headers.unlink(missing_ok=True)
            if chunk.exists() and chunk.stat().st_size == required:
                return
        raise RuntimeError("range download exhausted retries: chunk " + str(index))

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        futures = [pool.submit(fetch, index) for index in jobs]
        finished = 0
        interval = max(1, len(jobs) // 10)
        for future in concurrent.futures.as_completed(futures):
            future.result()
            finished += 1
            if finished % interval == 0 or finished == len(jobs):
                print("{}: {}/{} chunks".format(spec["archive"], finished, len(jobs)), flush=True)
    merged = directory / "complete.download"
    with merged.open("wb") as output:
        for index in jobs:
            with (directory / (str(index) + ".chunk")).open("rb") as source:
                shutil.copyfileobj(source, output)
    return merged


def download(spec, family):
    directory = BUILD / "downloads"
    directory.mkdir(parents=True, exist_ok=True)
    final = checked_child(BUILD, directory / spec["archive"])
    with WorkLock(BUILD / "state" / ("download-" + spec["sha256"] + ".lock")):
        if final.is_file() and final.stat().st_size == spec["bytes"] and sha256(final) == spec["sha256"]:
            artifacts.register(BUILD, final, "downloads/" + family, spec["sha256"])
            return final
        failures = []
        for url in spec["urls"] * 3:
            if spec["bytes"] >= 8 * 1024 * 1024 and ("/releases/download/" in url or "apt.llvm.org/" in url):
                chunks = directory / "parts" / spec["sha256"]
                chunks.mkdir(parents=True, exist_ok=True)
                artifacts.register(BUILD, chunks, "download-partials/" + family)
                try:
                    print("Fetching resumable ranges: " + spec["archive"], flush=True)
                    with artifacts.lease(BUILD, chunks):
                        temporary = download_ranges(spec, url, chunks)
                        if temporary.stat().st_size != spec["bytes"] or sha256(temporary) != spec["sha256"]:
                            raise RuntimeError("range download size or SHA256 mismatch")
                        os.replace(str(temporary), str(final))
                        artifacts.register(BUILD, final, "downloads/" + family, spec["sha256"])
                    return final
                except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
                    failures.append(str(error))
                    continue
            temporary = final.with_name(final.name + "." + uuid.uuid4().hex[:8] + ".part")
            temporary.touch()
            artifacts.register(BUILD, temporary, "download-partials/" + family)
            try:
                print("Fetching " + spec["archive"], flush=True)
                with artifacts.lease(BUILD, temporary):
                    curl = shutil.which("curl")
                    if curl:
                        subprocess.run([curl, "--fail", "--location", "--silent", "--show-error",
                                        "--connect-timeout", "15", "--max-time", "7200",
                                        "--speed-time", "30", "--speed-limit", "1024",
                                        "--output", str(temporary), url], check=True)
                    else:
                        with urllib.request.urlopen(url, timeout=30) as response, temporary.open("wb") as output:
                            shutil.copyfileobj(response, output, length=256 * 1024)
                    if temporary.stat().st_size != spec["bytes"] or sha256(temporary) != spec["sha256"]:
                        raise RuntimeError("download size or SHA256 mismatch")
                    os.replace(str(temporary), str(final))
                    artifacts.register(BUILD, final, "downloads/" + family, spec["sha256"])
                    return final
            except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
                failures.append(str(error))
                temporary.unlink(missing_ok=True)
        raise RuntimeError("all locked sources failed for {}: {}".format(spec["archive"], "; ".join(failures)))


def validate_members(members, root):
    for entry in members:
        name = PurePosixPath(entry.name)
        if (name.is_absolute() or ".." in name.parts or "\\" in entry.name or ":" in entry.name
                or not name.parts or name.parts[0] != root):
            raise RuntimeError("unsafe source archive path")
        if not (entry.isfile() or entry.isdir() or entry.issym() or entry.islnk()):
            raise RuntimeError("unsupported source archive member")
        if entry.issym() or entry.islnk():
            link = PurePosixPath(entry.linkname)
            if link.is_absolute() or "\\" in entry.linkname or ":" in entry.linkname:
                raise RuntimeError("absolute or platform-dependent source archive link")
            combined = list(name.parent.parts if entry.issym() else ())
            for part in link.parts:
                if part == "..":
                    if len(combined) <= 1:
                        raise RuntimeError("source archive link escapes root")
                    combined.pop()
                elif part != ".":
                    combined.append(part)
            if not combined or combined[0] != root:
                raise RuntimeError("source archive link escapes root")


def extract_source(spec, name):
    if spec.get('files'):
        return extract_file_set(spec, name)
    source = download(spec, name)
    destination = BUILD / "deps/src" / name / spec["sha256"][:12]
    with artifacts.lease(BUILD, source), WorkLock(BUILD / "state" / ("extract-" + name + ".lock")):
        stamp = read_json(destination / ".source.json", {})
        prefixes = spec.get("extract_prefixes", [])
        if stamp.get('sha256') == spec['sha256'] and stamp.get('extract_prefixes', []) == prefixes and not stamp.get('files'):
            # Older caches did not retain an inventory. Derive it from the
            # locked archive, never from a possibly incomplete extracted tree.
            with tarfile.open(str(source), 'r:*') as package:
                stamp['files'] = source_file_names(package.getmembers(), spec, prefixes)
            write_json(destination / '.source.json', stamp)
        missing = stamp.get('files') and set(stamp['files']) - tree_files(destination)
        if stamp.get("sha256") != spec["sha256"] or stamp.get("extract_prefixes", []) != prefixes or missing:
            temporary = BUILD / "deps/src" / ("unpack-" + uuid.uuid4().hex[:12])
            temporary.mkdir(parents=True)
            try:
                print("Extracting " + name, flush=True)
                with tarfile.open(str(source), "r:*") as package:
                    members = package.getmembers()
                    validate_members(members, spec["archive_root"])
                    if prefixes:
                        def wanted(member):
                            relative = PurePosixPath(member.name).relative_to(spec["archive_root"]).as_posix()
                            return (relative == "." or any(relative == prefix or relative.startswith(prefix + "/")
                                                           or prefix.startswith(relative + "/") for prefix in prefixes))
                        members = [entry for entry in members if wanted(entry)]
                    package.extractall(str(temporary), members=members)
                destination.parent.mkdir(parents=True, exist_ok=True)
                if destination.exists():
                    shutil.rmtree(checked_child(BUILD, destination))
                publish_directory(temporary / spec["archive_root"], destination)
                write_json(destination / ".source.json", {"sha256": spec["sha256"], "revision": spec.get("revision"),
                           "extract_prefixes": prefixes, 'files': source_file_names(members, spec, prefixes)})
            finally:
                shutil.rmtree(checked_child(BUILD, temporary))
        artifacts.register(BUILD, destination, "sources/" + name, spec["sha256"], [source.relative_to(BUILD).as_posix()])
    return destination


def extract_file_set(spec, name):
    """Small pinned upstream file sets avoid downloading an entire monorepo."""
    destination = BUILD / 'deps/src' / name / spec['sha256'][:12]
    references = []
    with WorkLock(BUILD / 'state' / ('extract-' + name + '.lock')):
        for item in spec['files']:
            relative = PurePosixPath(item['path'])
            if relative.is_absolute() or '..' in relative.parts or '\\' in str(relative) or ':' in str(relative):
                raise RuntimeError('unsafe source file-set path')
            path = checked_child(BUILD, destination.joinpath(*relative.parts))
            downloaded = download(item, name)
            references.append(downloaded.relative_to(BUILD).as_posix())
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.is_file() or sha256(path) != item['sha256']:
                shutil.copyfile(downloaded, path)
        write_json(destination / '.source.json', dict(sha256=spec['sha256'], revision=spec['revision'],
                   files=sorted(item['path'] for item in spec['files'])))
        artifacts.register(BUILD, destination, 'sources/' + name, spec['sha256'], references)
    return destination


def tree_files(root, expected=None):
    """Directory enumeration detects deleted inputs without hashing every file."""
    root = Path(root)
    result = set()
    required_directories = None
    if expected is not None:
        required_directories = set()
        for filename in expected:
            parent = Path(filename).parent
            while parent != Path('.'):
                required_directories.add(parent.as_posix())
                parent = parent.parent
    for parent, directories, files in os.walk(root):
        parent = Path(parent)
        for name in files:
            result.add((parent / name).relative_to(root).as_posix())
        for name in directories:
            path = parent / name
            if path.is_symlink(): result.add(path.relative_to(root).as_posix())
        if required_directories is not None:
            directories[:] = [name for name in directories
                              if (parent / name).relative_to(root).as_posix() in required_directories]
    return result


def source_file_names(members, spec, prefixes):
    result = []
    for member in members:
        if member.isdir(): continue
        relative = PurePosixPath(member.name).relative_to(spec['archive_root']).as_posix()
        if not prefixes or any(relative == prefix or relative.startswith(prefix + '/') for prefix in prefixes):
            result.append(relative)
    return sorted(result)


def restore_overlay_files(source, overlay):
    """Restore missing overlay inputs while leaving tracked patched files alone."""
    expected = read_json(source / '.source.json', {}).get('files')
    if not expected:
        raise RuntimeError('upstream source inventory is missing')
    for relative in sorted(set(expected) - tree_files(overlay, expected)):
        original, destination = source / relative, overlay / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if original.is_symlink():
            destination.symlink_to(os.readlink(original), target_is_directory=original.is_dir())
        else:
            shutil.copyfile(original, destination)
