"""Compare a pinned MDN BCD snapshot; this is data analysis, not a browser test.

Only the Python standard library is required. Outputs belong under build/.
Each __compat node is one record, including interfaces, members and subfeatures.
"""

import argparse
import base64
import collections
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile
import urllib.request


BROWSERS = ("firefox", "chrome", "firefox_android", "chrome_android")
SNAPSHOT = {
    "version": "8.1.8",
    "git_commit": "29768a4caa9a88722ef6ba4bad772eb4c3667218",
    "license": "CC0-1.0",
    "tarball_url": "https://registry.npmjs.org/@mdn/browser-compat-data/-/browser-compat-data-8.1.8.tgz",
    "tarball_sha256": "27614666b724fffe9ce738cd0582c7023237758bd403c193f5679fa69ff946a2",
    "integrity": "sha512-+RK7inkLeZ70kr9Mo+Y81JjZMpQIt0Sx40tHHU0hXGDSdf9YvlawH+4WlUpP3SE8rFLmjqGiTzL+qPPPnncWDw==",
    "data_sha256": "6d9dc7ff753812a2dcf99e93a9070bdbba11f7f3653bb531bc610c6eb9d0331a",
}
SCENARIOS = {
    "desktop_stable": ("firefox", "159", "chrome", "157", False),
    "android_stable": ("firefox_android", "159", "chrome_android", "157", False),
    "desktop_development": ("firefox", "161", "chrome", "159", True),
}
STANDARD_DEFAULT = {"default", "partial"}


def version_tuple(value):
    match = re.fullmatch(r"(\d+(?:\.\d+)*)(?:[ab]\d+)?", value)
    if not match:
        raise ValueError("Unsupported BCD version: " + repr(value))
    parts = tuple(int(p) for p in match.group(1).split("."))
    return parts + (0,) * (5 - len(parts))


def boundary(value, target, development=False):
    """Whether a version boundary has been reached; None means uncertain."""
    if value is None:
        return None
    if value is True:
        return True
    if value is False:
        return False
    if value == "preview":
        return development
    if not isinstance(value, str):
        raise TypeError(repr(value))
    if value.startswith("\u2264"):
        return True if version_tuple(value[1:]) <= target else None
    return version_tuple(value) <= target


def evaluate(statement, version, development=False):
    if statement is None:
        return {"state": "unknown", "signature": [], "notes": [], "active": [],
                "uncertain": True}
    statements = statement if isinstance(statement, list) else [statement]
    target = version_tuple(version)
    active, uncertain = [], False
    for item in statements:
        if not isinstance(item, dict):
            raise TypeError("Unexpanded support statement: " + repr(item))
        added = boundary(item.get("version_added"), target, development)
        removed = boundary(item.get("version_removed", False), target, development)
        if added is True and removed is False:
            active.append(item)
        elif added is not False and removed is not True:
            uncertain = True
    canonical = [s for s in active if not s.get("flags")
                 and not s.get("prefix") and not s.get("alternative_name")]
    named = [s for s in active if not s.get("flags")]
    if any(not s.get("partial_implementation", False) for s in canonical):
        state = "default"
    elif canonical:
        state = "partial"
    elif named:
        state = "named"
    elif active:
        state = "gated"
    else:
        state = "unknown" if uncertain else "unavailable"
    signatures, notes = set(), set()
    for item in active:
        contract = {"partial": bool(item.get("partial_implementation", False))}
        for key in ("flags", "prefix", "alternative_name"):
            if item.get(key):
                contract[key] = item[key]
        signatures.add(json.dumps(contract, sort_keys=True, ensure_ascii=False))
        note = item.get("notes", [])
        notes.update([note] if isinstance(note, str) else note)
    return {"state": state, "signature": sorted(signatures), "notes": sorted(notes),
            "active": active, "uncertain": uncertain}


def walk(node, path=()):
    if not isinstance(node, dict):
        return
    if "__compat" in node:
        yield ".".join(path), node["__compat"]
    for key in sorted(node):
        if key not in {"__compat", "__meta", "browsers"}:
            yield from walk(node[key], path + (key,))


def compare(left, right):
    if "unknown" in {left["state"], right["state"]}:
        return "unknown"
    a, b = left["state"] in STANDARD_DEFAULT, right["state"] in STANDARD_DEFAULT
    if a and not b:
        return "firefox_default_only"
    if b and not a:
        return "chromium_default_only"
    if left["state"] != right["state"] or left["signature"] != right["signature"]:
        return "other_contract_difference"
    return "same_contract"


def write_csv(path, rows, fields):
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value, ensure_ascii=False, sort_keys=True)
                             if isinstance(value, (list, dict)) else value
                             for key, value in row.items()})


def self_check():
    assert evaluate({"version_added": "100", "version_removed": "157"}, "157")["state"] == "unavailable"
    assert evaluate({"version_added": "100", "version_removed": "158"}, "157")["state"] == "default"
    assert evaluate({"version_added": "preview"}, "159")["state"] == "unavailable"
    assert evaluate({"version_added": "preview"}, "159", True)["state"] == "default"
    assert evaluate({"version_added": "\u226480"}, "157")["state"] == "default"
    assert evaluate({"version_added": "\u226480"}, "70")["state"] == "unknown"
    assert evaluate({"version_added": True}, "157")["state"] == "default"
    assert evaluate({"version_added": False}, "157")["state"] == "unavailable"
    assert evaluate(None, "157")["state"] == "unknown"
    assert evaluate({"version_added": "1", "prefix": "moz"}, "157")["state"] == "named"
    assert evaluate({"version_added": "1", "flags": [{"name": "x"}]}, "157")["state"] == "gated"
    assert evaluate({"version_added": "1", "partial_implementation": True}, "157")["state"] == "partial"
    assert evaluate([{"version_added": "1", "prefix": "moz"}, {"version_added": "2"}], "157")["state"] == "default"


def fetch_snapshot(data_path):
    request = urllib.request.Request(SNAPSHOT["tarball_url"], headers={"User-Agent": "XLWebEngineUI-BCD-comparison"})
    with urllib.request.urlopen(request, timeout=60) as response:
        payload = response.read()
    if hashlib.sha256(payload).hexdigest() != SNAPSHOT["tarball_sha256"]:
        raise ValueError("Pinned BCD tarball SHA-256 mismatch")
    if "sha512-" + base64.b64encode(hashlib.sha512(payload).digest()).decode() != SNAPSHOT["integrity"]:
        raise ValueError("Pinned BCD tarball SHA-512 integrity mismatch")
    data_path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        # Read named files only; do not extract paths or execute package code.
        data_path.write_bytes(archive.extractfile("package/data.json").read())
        (data_path.parent / "LICENSE").write_bytes(archive.extractfile("package/LICENSE").read())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--fetch", action="store_true", help="Download and verify pinned BCD if --data is absent")
    args = parser.parse_args()
    self_check()
    args.data = args.data or args.out / "data.json"
    if args.fetch and not args.data.exists():
        fetch_snapshot(args.data)
    raw = args.data.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SNAPSHOT["data_sha256"]:
        raise ValueError("Pinned BCD data.json SHA-256 mismatch")
    data = json.loads(raw)
    args.out.mkdir(parents=True, exist_ok=True)
    summary = {"snapshot": SNAPSHOT, "data_meta": data["__meta"], "data_sha256": hashlib.sha256(raw).hexdigest(),
               "method": "BCD node-level support at explicit versions; no live browser tests",
               "scenarios": {}, "self_checks": 13}
    features = list(walk(data))
    all_fields = ["feature", "category", "standard_track", "deprecated", "experimental",
                  "tags", "source_file", "mdn_url", "spec_url"]
    all_fields += [b + "_raw" for b in BROWSERS]
    all_rows = []
    for name, compat in features:
        row = {"feature": name, "category": name.split(".")[0],
               **{k: compat.get("status", {}).get(k) for k in
                  ("standard_track", "deprecated", "experimental")},
               **{k: compat.get(k, []) if k in {"tags", "spec_url"} else compat.get(k, "")
                  for k in ("tags", "source_file", "mdn_url", "spec_url")}}
        row.update({b + "_raw": compat["support"].get(b) for b in BROWSERS})
        all_rows.append(row)
    write_csv(args.out / "all-records.csv", all_rows, all_fields)
    for scenario, (browser_a, version_a, browser_b, version_b, development) in SCENARIOS.items():
        rows, classes, states = [], collections.Counter(), collections.Counter()
        categories = collections.defaultdict(collections.Counter)
        groups = set()
        notes_only = 0
        default_both = 0
        modern_classes = collections.Counter()
        for base, (_, compat) in zip(all_rows, features):
            left = evaluate(compat["support"].get(browser_a), version_a, development)
            right = evaluate(compat["support"].get(browser_b), version_b, development)
            classification = compare(left, right)
            row = dict(base, classification=classification,
                       firefox_state=left["state"], chromium_state=right["state"],
                       firefox_active=left["active"], chromium_active=right["active"],
                       firefox_notes=left["notes"], chromium_notes=right["notes"],
                       notes_differ=left["notes"] != right["notes"],
                       firefox_uncertain=left["uncertain"], chromium_uncertain=right["uncertain"])
            rows.append(row)
            classes[classification] += 1
            states[left["state"] + " / " + right["state"]] += 1
            categories[base["category"]][classification] += 1
            if classification == "same_contract" and row["notes_differ"]:
                notes_only += 1
            if left["state"] in STANDARD_DEFAULT and right["state"] in STANDARD_DEFAULT:
                default_both += 1
            if base["standard_track"] and not base["deprecated"]:
                modern_classes[classification] += 1
            if classification not in {"same_contract", "unknown"}:
                groups.update(t for t in base["tags"] if t.startswith("web-features:"))
        fields = all_fields + ["classification", "firefox_state", "chromium_state",
                               "firefox_active", "chromium_active", "firefox_notes", "chromium_notes",
                               "notes_differ", "firefox_uncertain", "chromium_uncertain"]
        differences = [r for r in rows if r["classification"] not in {"same_contract", "unknown"}]
        annotations = [r for r in rows if r["classification"] == "same_contract" and r["notes_differ"]]
        write_csv(args.out / (scenario + "-all.csv"), rows, fields)
        write_csv(args.out / (scenario + "-differences.csv"), differences, fields)
        write_csv(args.out / (scenario + "-notes-only.csv"), annotations, fields)
        summary["scenarios"][scenario] = {
            "firefox": {"browser": browser_a, "version": version_a},
            "chromium_proxy": {"browser": browser_b, "version": version_b},
            "development_estimate": development, "total_records": len(rows),
            "classes": dict(classes), "default_both": default_both,
            "state_pairs": dict(sorted(states.items())),
            "categories": {k: dict(v) for k, v in sorted(categories.items())},
            "standard_not_deprecated_classes": dict(modern_classes),
            "notes_only_records": notes_only, "different_web_feature_tags": len(groups),
            "different_web_feature_tag_list": sorted(groups),
        }
        assert sum(classes.values()) == len(features)
        assert len(differences) == sum(v for k, v in classes.items()
                                       if k not in {"same_contract", "unknown"})
    (args.out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: {a: b for a, b in v.items() if a not in
                         {"different_web_feature_tag_list", "state_pairs", "categories"}}
                      for k, v in summary["scenarios"].items()}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
