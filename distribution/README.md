# local publication kit

**preparation only: nothing in this directory publishes, uploads or submits.**
read [the checklist](publication-checklist.md) before releasing anything.

## documents

- [screenshots, video and audio on GitHub](media.md)
- [clean export and private-first repository workflow](repository.md)
- [GitHub release preparation](github-release.md)
- [Universal-DB technical reference and current submission rules](universal-db.md)
- [placeholder-only metadata reference](universal-db-entry.json)
- [dependency source and runtime-license status](dependency-sources.md)
- [installation and overview](../README.md), [user guide](../GUIDE.md),
  [build instructions](../BUILDING.md), [third-party notices](../THIRD_PARTY_NOTICES.md)

public repository owner/name and URLs remain placeholders. do not upload the
working repository, its history, `release/`, hidden local state, samples, test captures
or a candidate directory wholesale. create a new repository from the reviewed
**allowlisted source copy**, not from this workspace's old git history.

## generate a candidate

```sh
python3 tools/prepare_public_release.py --output release/publication-new-candidate
```

run from the source root and choose a fresh output name. inside the result:

| path | purpose |
| --- | --- |
| `source/` | exact allowlisted source snapshot; clean build runs here |
| `github-release/seance.3dsx` | standalone Homebrew Launcher download |
| `github-release/seance.cia` | standalone CFW installer download |
| `github-release/seance-<version>.zip` | application, guide and binary license notices |
| `github-release/seance-<version>-source.zip` | complete corresponding application/asset source, dependency bundle and build scripts |
| `github-release/seance-cc0-starter-samples.zip` | optional, separate sample pack |
| `github-release/SHA256SUMS.txt` | hashes of prospective downloadable assets |
| `source-inventory.json` | **local** source file hashes |
| `build.log`, `local-receipt.json` | **local** build outcome and remaining gates |

ELF/SMDH and build objects remain local. ZIPs also contain an inventory of their
other entries; the checksum file does not hash itself. candidate generation
refuses to overwrite an existing candidate. deterministic ZIP packaging is not
a claim of cross-toolchain bit-reproducible application binaries.

## release gates

- dependency/source and runtime-license notices reviewed against actual build;
- final repository/download/icon/banner URLs, no placeholders in a real submission;
- genuine device installation/audio/performance/storage/sleep acceptance;
- owner's explicit publication authorization.

Universal-DB currently requires a human-authored submission and AI-use disclosure;
these generated notes are a local technical reference, **not text to submit
verbatim**. automated direct binary installation does not install adjacent notices
from GitHub: plan an archive mapping/notice installation with the maintainers,
and keep the matching source download available alongside every binary release.

until the gates are closed, all generated application/source files are local
candidates, even when a build or a host check passes.
