#!/usr/bin/env python3
"""Offline regression checks for allowlisted public source and deterministic ZIPs."""
from __future__ import annotations

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

import package_release as pkg
from prepare_public_release import export_source, inventory

ROOT = Path(__file__).resolve().parent.parent


class PackagingTests(unittest.TestCase):
    def fixture(self, root: Path) -> None:
        for name in pkg.PUBLIC_ROOT_FILES:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture\n")
        for directory in (*pkg.PUBLIC_TREES, "tools"):
            base = root / directory
            base.mkdir(parents=True, exist_ok=True)
            (base / "fixture.json").write_bytes(b"{}\n")
        for name in (
            "assets/fonts/Gossip-MedPixel.otf", "assets/fonts/OFL.txt",
            "assets/menu_audio/menu_drone.bin", "assets/menu_audio/menu_entry.bin",
            "assets/menu_audio/source/original.wav", "assets/instruments/source/original.sfz",
            "core/engine/module_random_impl.inc", "platform/3ds/sequencer_value_buttons.inc",
            "licenses/dependency.txt", "distribution/dependency-sources/original.tar.gz",
            "tools/generator.py", "tools/stubs/example.h", "tools/preview.js",
        ):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())

    def test_allowlist_and_editable_sources(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            excluded = (
                ".git/config", ".local-state/secret", "release/old.3dsx", "build/object.o",
                "local_sample_library/private.wav", "promo/private.wav", "patch0.snc",
                "seance.3dsx", "tools/__pycache__/example.pyc", "tools/capture_promo_tek.py",
                "docs/private-design.md", "workspace/device.json",
                "tools/.private/check.py", "core/.history/old.cpp", "assets/branding/.local/private.png",
            )
            for name in excluded:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"must stay private")
            result = pkg.public_source_payload(root)
            self.assertFalse(set(excluded) & set(result))
            self.assertIn("core/engine/module_random_impl.inc", result)
            self.assertIn("assets/menu_audio/source/original.wav", result)
            self.assertIn("assets/instruments/source/original.sfz", result)
            self.assertIn("distribution/dependency-sources/original.tar.gz", result)
            self.assertIn("tools/stubs/example.h", result)
            self.assertIn("tools/preview.js", result)
            self.assertIn("tools/README.md", result)
            self.assertIn("docs/source-layout.md", result)
            self.assertIn(".github/fixture.json", result)

    def test_symlink_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "assets/menu_audio/leak").symlink_to(root / "LICENSE")
            with self.assertRaisesRegex(RuntimeError, "symlink"):
                pkg.public_source_payload(root)

    def test_missing_implementation_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "core/engine/module_random_impl.inc").unlink()
            with self.assertRaisesRegex(FileNotFoundError, "complete-source"):
                pkg.public_source_payload(root)

    def test_export_is_exact_and_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "input"
            root.mkdir()
            self.fixture(root)
            destination = Path(temporary) / "export"
            before = export_source(root, destination)
            self.assertEqual(before, inventory(pkg.public_source_payload(destination)))
            with self.assertRaises(FileExistsError):
                export_source(root, destination)

    def test_archive_determinism_crc_and_sums(self) -> None:
        files = {"source/b.inc": b"implementation", "source/a.txt": b"editable source\n"}
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            a, b = Path(temporary) / "a.zip", Path(temporary) / "b.zip"
            pkg.write_archive(a, files)
            pkg.write_archive(b, dict(reversed(list(files.items()))))
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with zipfile.ZipFile(a) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.namelist(), sorted([*files, "SHA256SUMS.txt"]))
                self.assertEqual(archive.read("SHA256SUMS.txt"), pkg.manifest(files))
                for info in archive.infolist():
                    self.assertEqual(info.date_time, pkg.FIXED_TIME)
                    self.assertFalse(info.filename.startswith("/"))
                    self.assertNotIn("..", Path(info.filename).parts)

    def test_unsafe_archive_names_refused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for name in ("../secret", "/absolute", "source/../../escape", "source\\escape"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    pkg.write_archive(Path(temporary) / "bad.zip", {name: b"x"})

    def test_cli_version_and_existing_output_guards(self) -> None:
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/package_release.py"), "--source", "--version", "wrong-version"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must equal VERSION", result.stdout)
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/prepare_public_release.py"), "--output", temporary, "--source-only"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("already exists", result.stdout)

    def test_current_source_includes_all_live_inputs(self) -> None:
        files = pkg.public_source_payload(ROOT)
        for directory in ("core", "platform", "assets"):
            for path in (ROOT / directory).rglob("*"):
                if path.is_file() and not path.is_symlink() and "__pycache__" not in path.parts:
                    self.assertIn(path.relative_to(ROOT).as_posix(), files)
        for name in files:
            self.assertNotIn(Path(name).parts[0], {".git", ".local-state", "release", "promo", "ui_mockups", "local_sample_library"})
        self.assertIn("assets/menu_audio/source/1.wav", files)
        for name in ("docs/source-layout.md", "tools/README.md", "distribution/repository.md",
                     "tools/start_menu_cabinet_preview.js", "tools/audit_public_source.py"):
            self.assertIn(name, files)
        self.assertNotIn("docs/perform-order.md", files)  # retained local contract/report
        self.assertIn("assets/menu_audio/source/2.wav", files)
        self.assertEqual(
            {n for n in files if n.startswith("assets/menu_audio/source/")},
            {"assets/menu_audio/source/1.wav", "assets/menu_audio/source/2.wav"},
        )
        self.assertEqual(len([n for n in files if n.startswith("sample_library/starter_cc0/wav/")]), 16)
        print(f"current allowlist: {len(files)} files, {sum(map(len, files.values()))} bytes")


if __name__ == "__main__":
    unittest.main(verbosity=2)
