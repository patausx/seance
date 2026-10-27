#!/usr/bin/env python3
"""Audit the allowlisted source locally; never publish, build or access a device."""
from __future__ import annotations

import argparse
import json
import posixpath
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit

from package_release import PUBLIC_HIDDEN_PARTS, public_source_payload

CYRILLIC = re.compile(r"[\u0400-\u04ff]+")
# Exact word runs, only in named artwork sources and the Unicode filename fixture.
# These exceptions preserve designed pixels and filename coverage, not prose.
WORD = "\u0441\u0435\u0430\u043d\u0441"
ARTWORK = {WORD, WORD.upper(), "\u043d", "\u0441", "\u0435", "\u0430"}
LANGUAGE_EXCEPTIONS = {
    "assets/branding/README.md": ARTWORK,
    "platform/3ds/wordmark_morph_data.h": {WORD},
    "tools/generate_wordmark_morph.py": ARTWORK,
    "tests/sample_browser_store_test.cpp": {"\u043a\u0438\u043a"},
}
PRIVACY_PATTERNS = {
    "personal home path": re.compile(r"/(?:home|Users)/[A-Za-z0-9_.-]+/"),
    "private network address": re.compile(
        r"\b(?:192\.168\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|"
        r"172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})\b"
    ),
    "credential-like token": re.compile(
        r"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,}|"
        r"sk-(?:proj-|or-v1-)?[A-Za-z0-9_-]{24,})\b"
    ),
    "private key": re.compile(r"-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----"),
}
LINK = re.compile(r"!?\[[^\]\n]*\]\(([^)\n]+)\)")
PRIVATE_ROOTS = {".git", "release", "promo", "research", "workspace",
                 "local_sample_library", "ui_mockups", "hardware_shots"}


def markdown_body(text: str) -> str:
    """Remove fenced examples before interpreting inline documentation links."""
    return re.sub(r"^```[^\n]*\n.*?^```[^\n]*$", "", text, flags=re.M | re.S)


def heading_ids(text: str) -> set[str]:
    ids: set[str] = set()
    for title in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", markdown_body(text), re.M):
        base = re.sub(r"[^\w\- ]", "", title.lower()).replace(" ", "-")
        slug, n = base, 0
        while slug in ids:
            n += 1
            slug = f"{base}-{n}"
        ids.add(slug)
    return ids


def audit_payload(files: dict[str, bytes]) -> dict:
    """Check one exact in-memory export; no mutation or filesystem traversal."""
    issues: list[dict] = []
    exceptions: list[dict] = []
    texts: dict[str, str] = {}
    for name, data in sorted(files.items()):
        parts = Path(name).parts
        if (not parts or name.startswith("/") or ".." in parts or "\\" in name
                or any(part == "__pycache__" or (part.startswith(".") and part not in PUBLIC_HIDDEN_PARTS)
                       for part in parts)
                or parts[0] in PRIVATE_ROOTS or Path(name).name.startswith(".env")
                or Path(name).suffix in {".snc", ".pem", ".key", ".3dsx", ".cia", ".elf", ".o"}):
            issues.append({"path": name, "kind": "unexpected private/build path"})
        if b"\0" in data:
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        texts[name] = text
        for line, value in enumerate(text.splitlines(), 1):
            runs = set(CYRILLIC.findall(value))
            if runs:
                unexpected = runs - LANGUAGE_EXCEPTIONS.get(name, set())
                if unexpected:
                    issues.append({"path": name, "line": line, "kind": "non-English Cyrillic prose"})
                else:
                    exceptions.append({"path": name, "line": line,
                                       "kind": "intentional artwork / Unicode fixture"})
            for label, pattern in PRIVACY_PATTERNS.items():
                if pattern.search(value):
                    # Never echo a matched credential into logs or JSON.
                    issues.append({"path": name, "line": line, "kind": label})
    links = 0
    for name, text in sorted(texts.items()):
        if not name.endswith(".md"):
            continue
        for match in LINK.finditer(markdown_body(text)):
            destination = match.group(1).strip().split(' "', 1)[0]
            url = urlsplit(destination.strip("<>"))
            if url.scheme or url.netloc:
                continue
            links += 1
            target = posixpath.normpath(posixpath.join(posixpath.dirname(name), unquote(url.path))) if url.path else name
            directory_exists = any(entry.startswith(target.rstrip("/") + "/") for entry in files)
            if target not in files and not directory_exists:
                issues.append({"path": name, "kind": "missing relative link", "target": target})
            elif url.fragment and target.endswith(".md") and target in texts:
                if unquote(url.fragment) not in heading_ids(texts[target]):
                    issues.append({"path": name, "kind": "missing Markdown anchor",
                                   "target": target + "#" + url.fragment})
    return {"passed": not issues, "source_files": len(files),
            "source_bytes": sum(map(len, files.values())), "text_files": len(texts),
            "relative_links_checked": links, "issues": issues,
            "intentional_language_exceptions": exceptions,
            "scope": "allowlisted UTF-8 text and paths; no binary/nested-archive rights or comprehensive secret audit",
            "published": False, "deployed": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--json", action="store_true", help="print report without writing a file")
    args = parser.parse_args()
    report = audit_payload(public_source_payload(args.root))
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        for issue in report["issues"]:
            print(f'{issue["path"]}:{issue.get("line", "-")}: {issue["kind"]}'
                  + (f' -> {issue["target"]}' if "target" in issue else ""))
        print(f'{"PASS" if report["passed"] else "FAIL"}: {report["source_files"]} source files; '
              f'{report["relative_links_checked"]} documentation links; '
              f'{len(report["intentional_language_exceptions"])} intentional artwork/Unicode lines')
        print(report["scope"])
    raise SystemExit(0 if report["passed"] else 1)


if __name__ == "__main__":
    main()
