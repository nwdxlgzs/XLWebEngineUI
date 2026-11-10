#!/usr/bin/env python3
"""Generate immutable compressed resource bytes; decoded data never ships separately."""
import sys
sys.dont_write_bytecode = True
import argparse
import contextlib
import hashlib
import json
from pathlib import Path
import subprocess

from ci_support import checked_child, read_json, write_json, WorkLock
from dependency_support import ROOT, BUILD, extract_source
import artifacts


def registered_ancestor(path):
    for candidate in path.parents:
        if candidate == BUILD:
            break
        _, _, manifest, _ = artifacts.locations(BUILD, candidate)
        if manifest.is_file():
            return candidate
    raise ValueError("resource input has no registered build artifact: " + str(path))


def generate(catalog_path, encoder, output, transformed=None):
    encoder = checked_child(BUILD, encoder)
    output = checked_child(BUILD, output)
    output.mkdir(parents=True, exist_ok=True)
    records = read_json(catalog_path)["resources"]
    ids = [record["id"] for record in records]
    if (len(ids) != len(set(ids)) or any(not isinstance(value, int) or value < 0 or value > 0xFFFFFFFF for value in ids)):
        raise ValueError("resource IDs must be distinct uint32 values")
    locks = read_json(ROOT / "third_party/dependencies.lock.json")["dependencies"]
    sources = []
    for item in records:
        alignment = item.get("alignment", 16)
        if not isinstance(alignment, int) or alignment < 1 or alignment & (alignment - 1):
            raise ValueError("resource alignment must be a positive power of two")
        if item.get('transform'):
            if not transformed or item['transform'] not in transformed:
                raise ValueError('resource transform requires prepared input: ' + item['transform'])
            source = checked_child(BUILD, Path(transformed[item['transform']]))
        elif "dependency" in item:
            directory = extract_source(locks[item["dependency"]], item["dependency"])
            source = checked_child(directory, directory / item["path"])
        else:
            source = checked_child(ROOT, ROOT / item["path"])
        if not source.is_file() or source.stat().st_size == 0:
            raise ValueError("resource data is missing or empty: " + str(source))
        sources.append((item, source))
    identity = {"catalog": records, "encoder_sha256": hashlib.sha256(encoder.read_bytes()).hexdigest(),
                "generator_revision": 1, "inputs": {str(item["id"]): hashlib.sha256(source.read_bytes()).hexdigest()
                                                       for item, source in sources}}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    inputs = {registered_ancestor(encoder)}
    for _, source in sources:
        try:
            source.relative_to(BUILD)
        except ValueError:
            continue
        inputs.add(registered_ancestor(source))
    artifacts.register(BUILD, output, "embedded-resource-generation", key,
                       [path.relative_to(BUILD).as_posix() for path in sorted(inputs)])
    lock = BUILD / "state" / ("resource-generation-" + hashlib.sha256(str(output).encode()).hexdigest()[:16] + ".lock")
    with WorkLock(lock), artifacts.lease(BUILD, output), contextlib.ExitStack() as leases:
        for path in inputs:
            leases.enter_context(artifacts.lease(BUILD, path))
        old = read_json(output / "manifest.json", {})
        hashes = old.get("output_sha256", {})
        if (old.get("key") == key and hashes and set(hashes) == set(old.get("outputs", []))
                and all(checked_child(output, output / name).is_file()
                        and hashlib.sha256((output / name).read_bytes()).hexdigest() == digest for name, digest in hashes.items())):
            print("Embedded resource catalog reused: " + key[:12], flush=True)
            return old
        outputs = ["catalog.cpp", "catalog.h"]
        metadata = []
        declarations = []
        for item, source in sources:
            filename = "resource-{}.inc".format(item["id"])
            compressed = output / ("resource-{}.zst".format(item["id"]))
            subprocess.run([str(encoder), str(source), str(compressed)], check=True)
            data = compressed.read_bytes()
            with (output / filename).open("w", encoding="utf-8", newline="\n") as stream:
                for start in range(0, len(data), 24):
                    stream.write(",".join("0x{:02x}".format(value) for value in data[start:start + 24]) + ",\n")
            outputs.extend([filename, compressed.name])
            declarations.append("alignas({}) static const unsigned char resource_{}[] = {{\n#include \"{}\"\n}};\n".format(
                item.get("alignment", 16), item["id"], filename))
            metadata.append({"id": item["id"], "name": item["name"], "decoded_bytes": source.stat().st_size,
                             "compressed_bytes": len(data), "sha256": identity["inputs"][str(item["id"])],
                             "compressed_sha256": hashlib.sha256(data).hexdigest(), "alignment": item.get("alignment", 16)})
        (output / "catalog.h").write_text("#pragma once\n#include \"resources/service.h\"\nnamespace xl::detail {\n"
                                          "std::vector<ResourceSpec> builtin_resource_catalog();\n}\n", encoding="utf-8")
        entries = ["    {{{}, {{reinterpret_cast<const std::byte*>(resource_{}), sizeof(resource_{})}}, {}, {}}}".format(
            item["id"], item["id"], item["id"], item["decoded_bytes"], item["alignment"]) for item in metadata]
        (output / "catalog.cpp").write_text('#include "catalog.h"\nnamespace xl::detail {\nnamespace {\n' + "".join(declarations)
            + "}\nstd::vector<ResourceSpec> builtin_resource_catalog() {\n  return {\n" + ",\n".join(entries) + "\n  };\n}\n}\n", encoding="utf-8")
        report = {"key": key, "identity": identity, "outputs": outputs, "resources": metadata,
                  "output_sha256": {name: hashlib.sha256((output / name).read_bytes()).hexdigest() for name in outputs},
                  "encoding": "zstd-checksummed", "runtime_policy": "lazy-memory-only"}
        write_json(output / "manifest.json", report)
    print("Embedded resource catalog generated: " + key[:12], flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=ROOT / "assets/resources.json")
    parser.add_argument("--encoder", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument('--icu-data', type=Path, help='prepared Chromium common ICU data')
    args = parser.parse_args()
    generate(args.catalog.resolve(), args.encoder.resolve(), args.output.resolve(),
             {'chromium-common': args.icu_data.resolve()} if args.icu_data else None)


if __name__ == "__main__":
    main()
