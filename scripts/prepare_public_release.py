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


def file_record(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": digest(data)}


def build_input_record(env: dict[str, str], makerom: Path) -> dict[str, object]:
    """Local build evidence, not a linked-object or corresponding-source audit."""
    arm = Path(env["DEVKITARM"])
    pro = Path(env["DEVKITPRO"])
    compiler = arm / "bin/arm-none-eabi-g++"
    flags = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
    paths = [compiler, makerom, arm / "3ds_rules", arm / "base_rules"]
    for tool in ("smdhtool", "3dsxtool"):
        found = shutil.which(tool, path=env["PATH"])
        if not found:
            raise SystemExit(f"required build tool missing: {tool}")
        paths.append(Path(found))
    for name in ("libgcc.a", "libstdc++.a", "libc.a", "libm.a", "3dsx_crt0.o", "3dsx.specs"):
        command = [str(compiler), *flags, f"-print-file-name={name}"]
        result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        path = Path(result.stdout.strip())
        if result.returncode or not path.is_absolute() or not path.is_file():
            raise SystemExit(f"compiler-selected build input missing: {name}")
        paths.append(path)
    library_root = Path(env.get("CTRULIB", str(pro / "libctru")))
    paths.extend(library_root / "lib" / name for name in ("libctru.a", "libcitro2d.a", "libcitro3d.a"))
    return {"scope": "selected tools/rules/runtime archives; actual linked members require final map review",
            "selection_flags": flags,
            "files": {str(path): file_record(path) for path in sorted(set(paths))}}


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
    if not 1 <= args.jobs <= 2:
        parser.error("jobs must be 1 or 2")
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
        "source_inventory": {"path": "source-inventory.json", **file_record(output / "source-inventory.json")},
        "artifact_provenance": "no artifacts in source-only mode; otherwise fresh isolated build from this inventory",
        "publication_ready": False,
        "pending": [
            "matching current libctru/compiler runtime sources, final link-map/newlib notices and corresponding-source audit",
            "menu WAVs: from a free beat pack per the project owner; the pack's name and terms are not recorded in the notices",
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
    build_inputs = build_input_record(env, makerom)
    json_file(output / "build-inputs.json", build_inputs)
    receipt["build_inputs"] = {"path": "build-inputs.json", **file_record(output / "build-inputs.json")}
    json_file(output / "local-receipt.json", receipt)
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
    if build_inputs != build_input_record(env, makerom):
        raise SystemExit("tool/rule/runtime inputs changed during build; candidate must not be packaged")
    json_file(output / "source-inventory-after.json", after)
    receipt["source_inventory_after"] = {"path": "source-inventory-after.json", **file_record(output / "source-inventory-after.json")}
    receipt["link_map"] = {"path": "source/build/seance.map", **file_record(source / "build/seance.map")}
    receipt["build_log"] = {"path": "build.log", **file_record(output / "build.log")}
    receipt["build_inputs_unchanged"] = True
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
    if (before != inventory(public_source_payload(source))
            or before != inventory(public_source_payload(root))
            or build_inputs != build_input_record(env, makerom)
            or any(file_record(source / name) != record for name, record in artifacts.items())):
        raise SystemExit("candidate source/tool/artifact inputs changed during packaging; downloads are not admitted")
    receipt["finished_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
    receipt["source_unchanged_during_build"] = True
    json_file(output / "local-receipt.json", receipt)
    print(f"local candidate: {output}")
    print(f"downloads: {downloads}; publication gates remain open; nothing uploaded")


if __name__ == "__main__":
    main()
