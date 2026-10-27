#!/usr/bin/env python3
"""Build deterministic Seance release archives from verified workspace artifacts."""

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


def required(root: Path, relative: str) -> bytes:
    path = root / relative
    if not path.is_file():
        raise FileNotFoundError(f"required release input is missing: {relative}")
    return path.read_bytes()


PUBLIC_ROOT_FILES = (
    "Makefile", "seance.rsf", "VERSION", "README.md", "GUIDE.md", "LICENSE",
    "BUILDING.md", "CHANGELOG.md", "CONTRIBUTING.md", "THIRD_PARTY_NOTICES.md",
    ".gitignore", "assets/icon.png", "assets/icon_blank.png",
    "docs/source-layout.md", "docs/inst-bank.md", "tools/README.md",
)
# Asset directories intentionally include originals, licenses and generation metadata.
PUBLIC_TREES = (
    "core", "platform", "assets/branding", "assets/fonts", "assets/instruments",
    "assets/menu_audio", "sample_library/starter_cc0", "licenses", "distribution", ".github",
)
PUBLIC_TOOLS_MANIFEST = "tools/public_tools.json"
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
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise RuntimeError(f"symlink/escaped public input: {relative}")
        if path.is_file():
            files[relative] = path.read_bytes()

    for relative in PUBLIC_ROOT_FILES:
        required(root, relative)
        add(root / relative)
    for directory in PUBLIC_TREES:
        base = root / directory
        if not base.is_dir() or base.is_symlink():
            raise FileNotFoundError(f"required public source directory: {directory}")
        for path in sorted(base.rglob("*")):
            add(path)
    # Explicit admission: local studies/previews never enter a source export
    # merely because a developer adds a script to tools/.
    tools = json.loads(required(root, PUBLIC_TOOLS_MANIFEST))
    if not isinstance(tools, dict):
        raise ValueError("invalid public tools manifest")
    names = tools.get("files")
    if (tools.get("schema") != 1 or not isinstance(names, list)
            or not names or not all(isinstance(name, str) for name in names)
            or names != sorted(set(names)) or PUBLIC_TOOLS_MANIFEST not in names):
        raise ValueError("invalid public tools manifest")
    for relative in names:
        parts = Path(relative).parts
        if (not relative.startswith("tools/") or "\\" in relative
                or Path(relative).as_posix() != relative or ".." in parts
                or any(part.startswith(".") for part in parts)):
            raise ValueError("unsafe public tool path")
        add(root / relative)
        if relative not in files:
            raise FileNotFoundError(f"missing public tool input: {relative}")
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


def license_payload(root: Path) -> dict[str, bytes]:
    files = {
        "3ds/seance/licenses/LICENSE": required(root, "LICENSE"),
        "3ds/seance/licenses/THIRD_PARTY_NOTICES.md": required(root, "THIRD_PARTY_NOTICES.md"),
        "3ds/seance/licenses/Seance-Note-OFL.txt": required(root, "assets/fonts/OFL.txt"),
        "3ds/seance/licenses/Seance-Note-README.md": required(root, "assets/fonts/README.md"),
    }
    for path in sorted((root / "licenses").rglob("*")):
        if path.is_file():
            files[f"3ds/seance/licenses/{path.relative_to(root / 'licenses').as_posix()}"] = path.read_bytes()
    return files


def build_main(root: Path, version: str) -> None:
    files = {
        "3ds/seance/seance.3dsx": required(root, "seance.3dsx"),
        "seance.cia": required(root, "seance.cia"),
        "GUIDE.md": required(root, "GUIDE.md"),
        "README.md": required(root, "README.md"),
        "assets/branding/readme-wordmark.gif": required(root, "assets/branding/readme-wordmark.gif"),
        "LICENSE": required(root, "LICENSE"),
        "3ds/seance/licenses/Seance-Note-OFL.txt": required(root, "assets/fonts/OFL.txt"),
        "3ds/seance/licenses/Seance-Note-README.md": required(root, "assets/fonts/README.md"),
    }
    files.update(license_payload(root))
    write_archive(root / "release" / f"seance-{version}.zip", files)


def build_source(root: Path, version: str) -> None:
    write_archive(root / "release" / f"seance-{version}-source.zip", source_payload(root))


def build_samples(root: Path) -> None:
    source = root / "sample_library" / "starter_cc0"
    wavs = sorted((source / "wav").glob("*.wav"))
    if len(wavs) != 16:
        raise RuntimeError(f"expected 16 starter WAV files, found {len(wavs)}")
    files: dict[str, bytes] = {
        "README.md": required(source, "README.md"),
        "CC0-1.0.txt": required(source, "CC0-1.0.txt"),
    }
    for path in wavs:
        with wave.open(str(path), "rb") as wav:
            actual = (wav.getnchannels(), wav.getsampwidth(), wav.getframerate())
            if actual != (1, 2, 32000):
                raise RuntimeError(f"unexpected WAV format for {path.name}: {actual}")
        files[f"3ds/seance/samples/{path.name}"] = path.read_bytes()
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
