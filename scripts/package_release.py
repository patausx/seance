#!/usr/bin/env python3
"""Package deterministic release archives from existing bytes; does not verify build origin."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import wave
import zipfile
from pathlib import Path

FIXED_TIME = (1980, 1, 1, 0, 0, 0)
VERSION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z._+-]*$")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def manifest(files: dict[str, bytes]) -> bytes:
    lines = [f"{digest(files[name])}  {name}" for name in sorted(files)]
    return ("\n".join(lines) + "\n").encode("utf-8")


def write_archive(path: Path, files: dict[str, bytes]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = dict(files)
    for name in payload:
        if not name or name.startswith("/") or "\\" in name or ".." in Path(name).parts:
            raise ValueError(f"unsafe archive path: {name}")
    payload["SHA256SUMS.txt"] = manifest(payload)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    with zipfile.ZipFile(
        temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for name in sorted(payload):
            info = zipfile.ZipInfo(name, FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, payload[name])
    with zipfile.ZipFile(temporary) as archive:
        bad = archive.testzip()
        if bad is not None:
            raise RuntimeError(f"archive verification failed at {bad}")
    temporary.replace(path)
    print(f"wrote {path} ({path.stat().st_size} bytes, sha256 {digest(path.read_bytes())})")


def input_path(root: Path, relative: str) -> Path:
    """Reject symlinks in every input component, including in-root aliases."""
    parts = Path(relative).parts
    if not parts or Path(relative).is_absolute() or ".." in parts or "\\" in relative:
        raise ValueError("unsafe release input path")
    path = root
    for part in parts:
        path = path / part
        if path.is_symlink():
            raise RuntimeError(f"symlink/escaped public input: {relative}")
    if not path.resolve().is_relative_to(root):
        raise RuntimeError(f"symlink/escaped public input: {relative}")
    return path


def required(root: Path, relative: str) -> bytes:
    path = input_path(root.resolve(), relative)
    if not path.is_file():
        raise FileNotFoundError(f"required release input is missing: {relative}")
    return path.read_bytes()


PUBLIC_ROOT_FILES = (
    "Makefile", "seance.rsf", "VERSION", "README.md", "GUIDE.md", "LICENSE",
    "BUILDING.md", "CHANGELOG.md", "CONTRIBUTING.md", "THIRD_PARTY_NOTICES.md",
    ".gitignore", "assets/icon.png", "assets/icon_small.png",
    "docs/source-layout.md", "docs/inst-bank.md",
    "distribution/README.md", "distribution/dependency-sources.md",
)
# Asset directories intentionally include originals, licenses and generation metadata.
PUBLIC_TREES = (
    "core", "platform", "assets/branding", "assets/artwork", "assets/fonts", "assets/instruments",
    "assets/menu_audio", "sample_library/starter_cc0", "licenses", "distribution/dependency-sources", ".github",
)
PUBLIC_HELPERS_MANIFEST = "scripts/source-files.json"
# Export only reviewed repository metadata, never arbitrary hidden local state.
PUBLIC_HIDDEN_PARTS = {".github", ".gitignore"}


def public_source_payload(root: Path) -> dict[str, bytes]:
    """Allowlist source; never export git history, user samples or local receipts."""
    root = root.resolve()
    files: dict[str, bytes] = {}

    def add(path: Path) -> None:
        relative = path.relative_to(root).as_posix()
        if any(part == "__pycache__" or (part.startswith(".") and part not in PUBLIC_HIDDEN_PARTS)
               for part in path.relative_to(root).parts):
            return
        path = input_path(root, relative)
        if path.is_file():
            files[relative] = path.read_bytes()

    for relative in PUBLIC_ROOT_FILES:
        required(root, relative)
        add(root / relative)
    for directory in PUBLIC_TREES:
        base = input_path(root, directory)
        if not base.is_dir():
            raise FileNotFoundError(f"required public source directory: {directory}")
        for path in sorted(base.rglob("*")):
            add(path)
    # Admit asset/release helpers explicitly, not by directory recursion.
    # Local tests, experiments and release receipts are never included.
    tools = json.loads(required(root, PUBLIC_HELPERS_MANIFEST))
    if not isinstance(tools, dict):
        raise ValueError("invalid helper source manifest")
    names = tools.get("files")
    if (tools.get("schema") != 1 or not isinstance(names, list)
            or not names or not all(isinstance(name, str) for name in names)
            or names != sorted(set(names)) or PUBLIC_HELPERS_MANIFEST not in names):
        raise ValueError("invalid helper source manifest")
    for relative in names:
        parts = Path(relative).parts
        if (not parts or parts[0] not in {"tools", "scripts"}
                or "\\" in relative or Path(relative).as_posix() != relative
                or ".." in parts or any(part.startswith(".") for part in parts)):
            raise ValueError("unsafe helper source path")
        add(root / relative)
        if relative not in files:
            raise FileNotFoundError(f"missing helper source input: {relative}")
    # Fail closed if the important editable/embedded inputs are absent.
    for relative in (
        "assets/fonts/Gossip-MedPixel.otf", "assets/fonts/OFL.txt",
        "assets/menu_audio/menu_drone.bin", "assets/menu_audio/menu_entry.bin",
        "core/engine/module_random_impl.inc", "platform/3ds/sequencer_value_buttons.inc",
    ):
        if relative not in files:
            raise FileNotFoundError(f"missing complete-source input: {relative}")
    return files


def source_payload(root: Path) -> dict[str, bytes]:
    return {f"source/{name}": data for name, data in public_source_payload(root).items()}


# Keep application documentation at its existing relative paths. These are only
# the guide/notices and their linked documents/resources, not a source export.
# Matching application/dependency source remains a separate distribution duty.
APPLICATION_DOC_FILES = (
    "README.md", "GUIDE.md", "BUILDING.md", "CHANGELOG.md",
    "assets/branding/readme-wordmark.gif",
    "tools/README.md", "scripts/README.md",
    "assets/instruments/README.md", "assets/instruments/sources.json",
    "assets/instruments/bank.json", "assets/instruments/bank.pcm",
    "docs/inst-bank.md",
    "sample_library/starter_cc0/README.md", "sample_library/starter_cc0/CC0-1.0.txt",
    "distribution/dependency-sources.md", "distribution/dependency-sources/manifest.json",
)


def license_payload(root: Path) -> dict[str, bytes]:
    """Retain notices verbatim at source-layout paths; never relocate link bases."""
    root = root.resolve()
    bases = [input_path(root, directory) for directory in ("licenses", "assets/instruments/licenses")]
    for base in bases:
        if not base.is_dir():
            raise FileNotFoundError(f"required release notice directory: {base.relative_to(root).as_posix()}")
    files = {name: required(root, name) for name in (
        "LICENSE", "THIRD_PARTY_NOTICES.md", "assets/fonts/OFL.txt", "assets/fonts/README.md",
    )}
    for base in bases:
        for path in sorted(base.rglob("*")):
            relative = path.relative_to(root).as_posix()
            path = input_path(root, relative)
            if path.is_file():
                files[relative] = required(root, relative)
    return files


def build_main(root: Path, version: str) -> None:
    files = {
        "3ds/seance/seance.3dsx": required(root, "seance.3dsx"),
        "seance.cia": required(root, "seance.cia"),
    }
    files.update({name: required(root, name) for name in APPLICATION_DOC_FILES})
    files.update(license_payload(root))
    write_archive(root / "release" / f"seance-{version}.zip", files)


def build_source(root: Path, version: str) -> None:
    write_archive(root / "release" / f"seance-{version}-source.zip", source_payload(root))


def build_samples(root: Path) -> None:
    root = root.resolve()
    directory = "sample_library/starter_cc0"
    wav_directory = input_path(root, f"{directory}/wav")
    wavs = [input_path(root, path.relative_to(root).as_posix())
            for path in sorted(wav_directory.glob("*.wav"))]
    if len(wavs) != 16:
        raise RuntimeError(f"expected 16 starter WAV files, found {len(wavs)}")
    files: dict[str, bytes] = {
        "README.md": required(root, f"{directory}/README.md"),
        "CC0-1.0.txt": required(root, f"{directory}/CC0-1.0.txt"),
    }
    for path in wavs:
        with wave.open(str(path), "rb") as wav:
            actual = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate())
            if actual != (1, 2, 32000):
                raise RuntimeError(f"unexpected WAV format for {path.name}: {actual}")
        files[f"3ds/seance/samples/{path.name}"] = required(root, path.relative_to(root).as_posix())
    write_archive(root / "release" / "seance-cc0-starter-samples.zip", files)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version")
    parser.add_argument("--main", action="store_true", help="build only the app archive")
    parser.add_argument("--samples", action="store_true", help="build only the sample archive")
    parser.add_argument("--source", action="store_true", help="build only the complete source archive")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    canonical_version = required(root, "VERSION").decode("ascii").strip()
    version = args.version or canonical_version
    if not VERSION_RE.fullmatch(version):
        raise SystemExit("unsafe release version")
    if version != canonical_version:
        raise SystemExit("release version must equal VERSION; change VERSION and rebuild first")
    all_archives = not (args.main or args.samples or args.source)
    if args.main or all_archives:
        build_main(root, version)
    if args.source or all_archives:
        build_source(root, version)
    if args.samples or all_archives:
        build_samples(root)


if __name__ == "__main__":
    main()
