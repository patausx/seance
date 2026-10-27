#!/usr/bin/env python3
"""Offline regression tests for the publication text/path/link audit."""
from __future__ import annotations

import _paths  # shared asset and release modules

import unittest
from pathlib import Path

from audit_public_source import WORD, audit_payload, heading_ids
from package_release import public_source_payload


class PublicAuditTests(unittest.TestCase):
    def issue_kinds(self, files: dict[str, bytes]) -> set[str]:
        return {entry["kind"] for entry in audit_payload(files)["issues"]}

    def test_english_links_directories_and_anchors(self) -> None:
        report = audit_payload({
            "README.md": b"# start\n[guide](docs/guide.md#first-sound)\n[assets](assets/)\n[web](https://example.invalid/)\n",
            "docs/guide.md": b"# guide\n## first sound\n[back](../README.md#start)\n",
            "assets/license.txt": b"license\n",
        })
        self.assertTrue(report["passed"], report)
        self.assertEqual(report["relative_links_checked"], 3)

    def test_missing_link_and_anchor(self) -> None:
        kinds = self.issue_kinds({"README.md": b"# start\n[missing](absent.md)\n[bad](#absent)\n"})
        self.assertEqual(kinds, {"missing relative link", "missing Markdown anchor"})

    def test_fenced_examples_are_not_links_or_headings(self) -> None:
        report = audit_payload({"README.md": b"# start\n```md\n[example](not-a-file)\n## not a heading\n```\n"})
        self.assertTrue(report["passed"])
        self.assertEqual(heading_ids("# a\n## a\n"), {"a", "a-1"})
        self.assertEqual(heading_ids("# start\n```md\n## hidden\n```\n"), {"start"})

    def test_non_english_prose_fails(self) -> None:
        report = audit_payload({"README.md": "# \u043f\u0440\u0438\u0432\u0435\u0442\n".encode()})
        self.assertFalse(report["passed"])
        self.assertEqual(report["issues"][0]["kind"], "non-English Cyrillic prose")

    def test_artwork_and_filename_exceptions_are_scoped(self) -> None:
        report = audit_payload({
            "platform/3ds/wordmark_morph_data.h": ("// " + WORD).encode(),
            "tests/sample_browser_store_test.cpp": "/nonexistent/\u043a\u0438\u043a 01.WAV".encode(),
        })
        self.assertTrue(report["passed"])
        self.assertEqual(len(report["intentional_language_exceptions"]), 2)
        self.assertIn("non-English Cyrillic prose", self.issue_kinds({"README.md": WORD.encode()}))
        self.assertIn("non-English Cyrillic prose", self.issue_kinds({
            "platform/3ds/wordmark_morph_data.h": (WORD + " \u043f\u0440\u0438\u0432\u0435\u0442").encode(),
        }))

    def test_privacy_patterns_fail_without_echoing_values(self) -> None:
        examples = (
            ("personal home path", "/home/" + "reviewer/project"),
            ("personal home path", "/Users/" + "reviewer/project"),
            ("private network address", "192." + "168.1.2"),
            ("private network address", "172." + "31.2.3"),
            ("credential-like token", "ghp_" + "x" * 30),
            ("credential-like token", "sk-" + "or-v1-" + "x" * 40),
            ("private key", "-----BEGIN " + "PRIVATE KEY-----"),
        )
        for kind, value in examples:
            with self.subTest(kind=kind):
                report = audit_payload({"tests/example.py": value.encode()})
                self.assertFalse(report["passed"])
                self.assertIn(kind, {item["kind"] for item in report["issues"]})
                self.assertNotIn(value, repr(report))

    def test_expected_system_paths_and_non_private_versions(self) -> None:
        self.assertTrue(audit_payload({"Makefile": b"/tmp/seance_test\n/usr/share/fonts\nversion 2.7.0\n"})["passed"])

    def test_unexpected_private_paths_fail(self) -> None:
        for name in ("release/receipt.json", ".env", "patch0.snc", ".git/config",
                     "assets/.local-state/secret", "tests/.private/check.py", "seance.3dsx", "../escaped.txt"):
            with self.subTest(name=name):
                self.assertIn("unexpected private/build path", self.issue_kinds({name: b"fixture"}))

    def test_reviewed_metadata_allowed_but_other_hidden_state_refused(self) -> None:
        self.assertTrue(audit_payload({
            ".gitignore": b"build/\n",
            ".github/ISSUE_TEMPLATE/bug.md": b"# bug report\n",
        })["passed"])
        for name in ("assets/.notes/draft.txt", "tests/.cache/state.json", ".local-state/config"):
            with self.subTest(name=name):
                self.assertIn("unexpected private/build path", self.issue_kinds({name: b"local"}))

    def test_binary_assets_are_not_text_audited(self) -> None:
        report = audit_payload({"assets/audio.bin": b"\0\xff\xfe"})
        self.assertTrue(report["passed"])
        self.assertEqual(report["text_files"], 0)
        self.assertIn("no binary", report["scope"])

    def test_documentation_cannot_link_outside_export(self) -> None:
        report = audit_payload({"docs/guide.md": b"[private](../../private-note.md)\n"})
        self.assertFalse(report["passed"])
        self.assertEqual(report["issues"][0]["kind"], "missing relative link")

    def test_current_allowlisted_source(self) -> None:
        root = Path(__file__).resolve().parent.parent
        report = audit_payload(public_source_payload(root))
        self.assertTrue(report["passed"], report["issues"])
        self.assertGreater(report["relative_links_checked"], 50)
        self.assertEqual({entry["path"] for entry in report["intentional_language_exceptions"]}, {
            "assets/branding/README.md", "platform/3ds/wordmark_morph_data.h",
            "tools/generate_wordmark_morph.py", "tests/sample_browser_store_test.cpp",
        })


if __name__ == "__main__":
    unittest.main(verbosity=2)
