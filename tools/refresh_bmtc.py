#!/usr/bin/env python3
"""Refresh or verify the BMTC sample with SHA-256 checksums and provenance.

Modes:
  python3 tools/refresh_bmtc.py --check          verify committed files against
                                                 samples/bmtc.manifest.json
  python3 tools/refresh_bmtc.py                  re-run importer + CLI expected
                                                 output and report UNCHANGED or
                                                 exact checksum/content diffs
  python3 tools/refresh_bmtc.py --update-manifest
                                                 refresh and update manifest
  python3 tools/refresh_bmtc.py --write-manifest write manifest from current
                                                 verified files (no download)
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST_PATH = REPO / "samples" / "bmtc.manifest.json"
TRACKED_FILES = [
    "samples/bmtc.txt",
    "samples/expected-bmtc.txt",
    "web/data/bmtc.json",
]


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def collect_provenance() -> dict[str, object]:
    bmtc_txt = REPO / "samples" / "bmtc.txt"
    geo_json = REPO / "web" / "data" / "bmtc.json"

    geo = json.loads(geo_json.read_text(encoding="utf-8"))
    route_count = 0
    appearances = 0
    for line in bmtc_txt.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = stripped.split()
        if parts[0] in ("routes", "query"):
            continue
        route_count += 1
        appearances += int(parts[1])

    files = {}
    for rel in TRACKED_FILES:
        full = REPO / rel
        if not full.exists():
            raise FileNotFoundError(f"missing {rel}")
        files[rel] = {
            "sha256": sha256_of(full),
            "bytes": full.stat().st_size,
        }

    return {
        "city": geo.get("city", ""),
        "source": geo.get("source", ""),
        "license": geo.get("license", ""),
        "generated": geo.get("generated", ""),
        "routes": route_count,
        "appearances": appearances,
        "stops": len(geo.get("stops", {})),
        "files": files,
    }


def check_manifest() -> int:
    if not MANIFEST_PATH.exists():
        print(
            f"refresh-bmtc: FAIL: missing {MANIFEST_PATH.relative_to(REPO)}",
            file=sys.stderr,
        )
        return 1

    expected = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    actual = collect_provenance()
    mismatches: list[str] = []

    for key in ("city", "source", "license", "generated", "routes", "appearances", "stops"):
        if expected.get(key) != actual.get(key):
            mismatches.append(
                f"provenance.{key}: manifest={expected.get(key)!r} != actual={actual.get(key)!r}"
            )

    exp_files = expected.get("files", {})
    act_files = actual.get("files", {})
    for rel in TRACKED_FILES:
        exp_info = exp_files.get(rel, {})
        act_info = act_files.get(rel, {})
        if exp_info.get("sha256") != act_info.get("sha256"):
            mismatches.append(
                f"{rel}: sha256 {exp_info.get('sha256')} != {act_info.get('sha256')}"
            )

    if mismatches:
        print("refresh-bmtc: DIFF DETECTED:", file=sys.stderr)
        for item in mismatches:
            print(f"  - {item}", file=sys.stderr)
        return 1

    print(
        "refresh-bmtc: OK "
        f"({actual['routes']} routes, {actual['appearances']} appearances, "
        f"{actual['stops']} stops, {actual['license']}, generated {actual['generated']})"
    )
    return 0


def run_cmd(cmd: list[str], stdout_path: Path | None = None) -> None:
    if stdout_path is None:
        subprocess.run(cmd, cwd=str(REPO), check=True)
        return
    with stdout_path.open("w", encoding="utf-8") as out:
        subprocess.run(cmd, cwd=str(REPO), check=True, stdout=out)


def print_diff_preview(rel: str, before: str, after: str) -> None:
    diff = list(
        difflib.unified_diff(
            before.splitlines(),
            after.splitlines(),
            fromfile=f"a/{rel}",
            tofile=f"b/{rel}",
            lineterm="",
            n=2,
        )
    )
    if not diff:
        return
    for line in diff[:40]:
        print(f"    {line}")
    if len(diff) > 40:
        print(f"    ... ({len(diff) - 40} more diff lines)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify committed BMTC artifacts against samples/bmtc.manifest.json",
    )
    parser.add_argument(
        "--write-manifest",
        action="store_true",
        help="validate current BMTC files and write samples/bmtc.manifest.json",
    )
    parser.add_argument(
        "--update-manifest",
        action="store_true",
        help="refresh from GTFS and update samples/bmtc.manifest.json",
    )
    parser.add_argument("--routes", type=Path, help="optional local routes.csv")
    parser.add_argument("--stops", type=Path, help="optional local stops.geojson")
    args = parser.parse_args()

    if args.check:
        run_cmd([sys.executable, "tools/check_bmtc.py"])
        return check_manifest()

    if args.write_manifest:
        run_cmd([sys.executable, "tools/check_bmtc.py"])
        manifest = collect_provenance()
        MANIFEST_PATH.write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(f"wrote {MANIFEST_PATH.relative_to(REPO)}")
        return 0

    before_text: dict[str, str] = {}
    for rel in ("samples/bmtc.txt", "samples/expected-bmtc.txt"):
        path = REPO / rel
        before_text[rel] = path.read_text(encoding="utf-8") if path.exists() else ""
    before_manifest = (
        json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        if MANIFEST_PATH.exists()
        else collect_provenance()
    )

    importer = [sys.executable, "tools/import_bmtc.py"]
    if args.routes:
        importer += ["--routes", str(args.routes)]
    if args.stops:
        importer += ["--stops", str(args.stops)]
    run_cmd(importer)

    journey_bin = REPO / "build" / "journey"
    if not journey_bin.exists():
        run_cmd(["make"])
    with (REPO / "samples" / "bmtc.txt").open("r", encoding="utf-8") as inp:
        with (REPO / "samples" / "expected-bmtc.txt").open("w", encoding="utf-8") as out:
            subprocess.run(
                [str(journey_bin)], cwd=str(REPO), check=True, stdin=inp, stdout=out
            )
    run_cmd([sys.executable, "tools/check_bmtc.py"])

    after_manifest = collect_provenance()
    changed = False
    for rel in TRACKED_FILES:
        old_sha = before_manifest.get("files", {}).get(rel, {}).get("sha256")
        new_sha = after_manifest["files"][rel]["sha256"]
        if old_sha != new_sha:
            changed = True
            print(f"CHANGED {rel}: {old_sha} -> {new_sha}")
            if rel in before_text:
                print_diff_preview(
                    rel, before_text[rel], (REPO / rel).read_text(encoding="utf-8")
                )

    if not changed:
        print("refresh-bmtc: UNCHANGED (identical checksums and provenance)")
        return 0

    if args.update_manifest:
        MANIFEST_PATH.write_text(
            json.dumps(after_manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(f"updated {MANIFEST_PATH.relative_to(REPO)}")
        return 0

    print(
        "refresh-bmtc: sample changed; re-run with --update-manifest to accept",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
