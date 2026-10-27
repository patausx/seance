#!/usr/bin/env python3
"""Prepare a new local, isolated release candidate. No network or deployment."""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from package_release import (
    VERSION_RE, build_main, build_samples, build_source, digest, manifest,
    public_source_payload, required,
)


def json_file(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def inventory(files: dict[str, bytes]) -> dict[str, str]:
    return {name: digest(data) for name, data in sorted(files.items())}


def export_source(root: Path, destination: Path) -> dict[str, str]:
    files = public_source_payload(root)
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in sorted(files.items()):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return inventory(files)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="new directory; never overwritten")
    parser.add_argument("--source-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--makerom", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.expanduser().resolve()
    if output.exists():
        parser.error("output already exists; select a new candidate directory")
    if args.jobs < 1:
        parser.error("jobs must be positive")
    # Never mix candidate output into one of the exported input trees.
    for directory in ("core", "platform", "assets", "tools", "tests", "scripts", "sample_library", "docs", "distribution", "licenses", ".github"):
        if output.is_relative_to(root / directory):
            parser.error("output must not be inside an exported input directory")
    version = required(root, "VERSION").decode("ascii").strip()
    if not VERSION_RE.fullmatch(version):
        parser.error("unsafe VERSION")
    output.mkdir(parents=True, exist_ok=False)
    source = output / "source"
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    before = export_source(root, source)
    json_file(output / "source-inventory.json", before)
    receipt = {
        "schema": "seance-local-publication-v1", "version": version,
        "started_utc": started, "published": False, "deployed": False,
        "source_files": len(before), "source_only": args.source_only,
        "publication_ready": False,
        "pending": [
            "final linked-runtime/newlib license and corresponding-source audit",
            "final public repository/release URLs and publication authorization",
            "human-authored Universal-DB submission under its current AI policy",
            "real-console install/audio/performance/sleep/storage acceptance",
        ],
        "checks": "packaging/inventories only; host/device checks are separately recorded",
    }
    json_file(output / "local-receipt.json", receipt)
    if args.source_only:
        print(f"exported {len(before)} source files to {source}; not published")
        return
    makerom = args.makerom or shutil.which("makerom") or Path.home() / "tools/3ds/makerom"
    makerom = Path(makerom).expanduser().resolve()
    if not makerom.is_file() or not os.access(makerom, os.X_OK):
        raise SystemExit("makerom is missing/not executable; use --makerom")
    env = dict(os.environ)
    env.update({"TZ": "UTC", "LC_ALL": "C.UTF-8", "SOURCE_DATE_EPOCH": "315532800"})
    devkitpro = Path(env.get("DEVKITPRO", "/opt/devkitpro"))
    env.setdefault("DEVKITPRO", str(devkitpro))
    env.setdefault("DEVKITARM", str(devkitpro / "devkitARM"))
    env["PATH"] = f"{devkitpro / 'tools/bin'}:{env['DEVKITARM']}/bin:{env.get('PATH', '')}"
    command = ["make", f"-j{args.jobs}", "cia", f"MAKEROM={makerom}"]
    receipt["build_command"] = command
    tool_versions = {}
    for label, cmd in {
        "compiler": [str(Path(env["DEVKITARM"]) / "bin/arm-none-eabi-g++"), "--version"],
        "make": ["make", "--version"],
    }.items():
        result = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        tool_versions[label] = result.stdout.splitlines()[0] if result.stdout else f"exit {result.returncode}"
    receipt["tool_versions"] = tool_versions
    tick = time.monotonic()
    with (output / "build.log").open("wb") as log:
        result = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT)
    receipt["build_exit"] = result.returncode
    receipt["build_seconds"] = round(time.monotonic() - tick, 3)
    json_file(output / "local-receipt.json", receipt)
    if result.returncode:
        print((output / "build.log").read_text(errors="replace")[-6000:])
        raise SystemExit(f"build failed; see {output / 'build.log'}")
    # Never package a build whose source changed during compilation.
    after = inventory(public_source_payload(source))
    if before != after or before != inventory(public_source_payload(root)):
        raise SystemExit("source inputs changed during build; candidate must not be packaged")
    artifacts = {}
    for name in ("seance.3dsx", "seance.cia", "seance.smdh", "seance.elf"):
        data = required(source, name)
        if not data:
            raise SystemExit(f"empty build artifact: {name}")
        artifacts[name] = {"bytes": len(data), "sha256": digest(data)}
    receipt["artifacts"] = artifacts
    build_main(source, version)
    build_source(source, version)
    build_samples(source)
    downloads = output / "github-release"
    downloads.mkdir()
    for name in ("seance.3dsx", "seance.cia"):
        shutil.copyfile(source / name, downloads / name)
    for path in sorted((source / "release").glob("*.zip")):
        shutil.copyfile(path, downloads / path.name)
    # ELF/SMDH and detailed logs stay local, not accidental GitHub assets.
    sums = {p.name: p.read_bytes() for p in downloads.iterdir() if p.is_file()}
    (downloads / "SHA256SUMS.txt").write_bytes(manifest(sums))
    receipt["downloads"] = {n: {"bytes": len(b), "sha256": digest(b)} for n, b in sorted(sums.items())}
    receipt["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    receipt["source_unchanged_during_build"] = True
    json_file(output / "local-receipt.json", receipt)
    print(f"local candidate: {output}")
    print(f"downloads: {downloads}; publication gates remain open; nothing uploaded")


if __name__ == "__main__":
    main()
