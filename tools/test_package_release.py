#!/usr/bin/env python3
"""Offline regression checks for allowlisted public source and deterministic ZIPs."""
from __future__ import annotations

import contextlib
import io
import ast
import json
import re
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

        # Use the same reviewed admission list, but independent fixture bytes.
        manifest = (ROOT / pkg.PUBLIC_TOOLS_MANIFEST).read_bytes()
        for name in json.loads(manifest)["files"]:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        (root / pkg.PUBLIC_TOOLS_MANIFEST).write_bytes(manifest)

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
                "tools/render_new_study.py", "tools/test_unreviewed.py", "tools/generator.py",
                "tools/stubs/example.h", "tools/preview.js",
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
            self.assertIn("tools/generate_cabinet_font.py", result)
            self.assertIn("tools/sequencer_test_stubs/3ds.h", result)
            self.assertIn(pkg.PUBLIC_TOOLS_MANIFEST, result)
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

    def test_missing_admitted_tool_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            (root / "tools/generate_cabinet_font.py").unlink()
            with self.assertRaisesRegex(FileNotFoundError, "public tool input"):
                pkg.public_source_payload(root)

    def test_admitted_tool_symlink_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            path = root / "tools/generate_cabinet_font.py"
            path.unlink()
            path.symlink_to(root / "LICENSE")
            with self.assertRaisesRegex(RuntimeError, "symlink"):
                pkg.public_source_payload(root)

    def test_tool_manifest_paths_and_schema_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root)
            for unsafe in ("../secret", "/absolute", "tools/../LICENSE", "tools//double.py",
                           "tools/.private/check.py", "tools\\escape.py", "core/engine/units.cpp"):
                with self.subTest(unsafe=unsafe):
                    (root / pkg.PUBLIC_TOOLS_MANIFEST).write_text(json.dumps({
                        "schema": 1, "files": sorted([pkg.PUBLIC_TOOLS_MANIFEST, unsafe])}))
                    with self.assertRaisesRegex(ValueError, "unsafe public tool path"):
                        pkg.public_source_payload(root)
            for bad in ([], {}, {"schema": 2, "files": []}, {"schema": 1, "files": [None]},
                        {"schema": 1, "files": [pkg.PUBLIC_TOOLS_MANIFEST] * 2}):
                with self.subTest(bad=bad):
                    (root / pkg.PUBLIC_TOOLS_MANIFEST).write_text(json.dumps(bad))
                    with self.assertRaisesRegex(ValueError, "invalid public tools manifest"):
                        pkg.public_source_payload(root)

    def test_current_tool_dependencies_and_no_studies(self) -> None:
        files = pkg.public_source_payload(ROOT)
        tools = {n for n in files if n.startswith("tools/")}
        modules = {Path(n).stem: n for n in tools if n.endswith(".py")}
        local_modules = {path.stem for path in (ROOT / "tools").glob("*.py")}
        for name in tools:
            if not name.endswith(".py"):
                continue
            tree = ast.parse(files[name], filename=name)
            for node in ast.walk(tree):
                imports = ([node.module] if isinstance(node, ast.ImportFrom) else
                           [alias.name for alias in node.names] if isinstance(node, ast.Import) else [])
                for module in imports:
                    top = (module or "").split(".")[0]
                    if top in local_modules:
                        self.assertIn(top, modules, f"{name} imports an excluded local module {top}")
        for relative in re.findall(r"tools/[A-Za-z0-9_./-]+\.(?:py|cpp|h|inc)", files["Makefile"].decode()):
            self.assertIn(relative, files, f"Makefile references excluded {relative}")
        self.assertEqual({n for n in tools if Path(n).name.startswith("render_")},
                         {"tools/render_readme_wordmark.py"})
        self.assertFalse(any("study" in Path(n).name or "preview" in Path(n).name
                             or Path(n).name.startswith(("capture_", "promo_")) for n in tools))

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

    def test_application_archive_keeps_readme_animation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            root = Path(temporary)
            self.fixture(root)
            for name, data in {
                "seance.3dsx": b"test-only application fixture",
                "seance.cia": b"test-only installer fixture",
                "assets/fonts/README.md": b"font notices fixture",
                "assets/branding/readme-wordmark.gif": b"GIF89a-test-only-fixture",
            }.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            pkg.build_main(root, "test")
            with zipfile.ZipFile(root / "release/seance-test.zip") as archive:
                self.assertEqual(archive.read("assets/branding/readme-wordmark.gif"),
                                 (root / "assets/branding/readme-wordmark.gif").read_bytes())
                self.assertEqual(archive.read("GUIDE.md"), (root / "GUIDE.md").read_bytes())

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
                     "tools/public_tools.json", "tools/audit_public_source.py"):
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
