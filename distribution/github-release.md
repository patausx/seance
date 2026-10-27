# github release preparation

**local draft — no repository, tag, release, upload, or public download is
created by this document.** the current version is `0.1.0-alpha.1`; the proposed
tag is `v0.1.0-alpha.1`, to be confirmed by the maintainer. the Git checkout has
no remote. `patausx` is an author credit, not proof of a GitHub repository owner.

publishing is a separate, explicitly authorized action. first clear
[publication-checklist.md](publication-checklist.md), including audio notices
and corresponding source. then replace every bracketed field in
the following **GitHub-only release-text draft**, verify the actual assets, and
have a maintainer approve it. do not reuse it for Universal-DB: upstream's
current policy requires a human-authored submission.

## private repository first

for a clean source import, private review and later public visibility, follow
[repository preparation](repository.md). a private push is still an upload;
review the files and obtain approval before creating the repository. do not push
the development workspace's old history.

## release-text draft

---

# seance 0.1.0-alpha.1

an early alpha of a patchable sampler and synthesizer for Nintendo 3DS.

seance combines sample and granular playback, an embedded acoustic instrument
bank, synthesis and effects modules, eight modulation sigils, microphone
recording, and a step sequencer. it uses both screens for patching, modulation,
and performance. see the included README and GUIDE for the current controls.

this is an **alpha**, not a stable release or a compatibility guarantee. back up
existing patches, samples, and recordings before trying a new build. source,
asset licenses, third-party notices, and SHA-256 checksums accompany the release.

## installation

- **Homebrew Launcher:** use the provided `.3dsx`; place it at
  `/3ds/seance/seance.3dsx` on the SD card. a console already configured to run
  homebrew is required.
- **CIA, if provided:** use a compatible title installer on a console already
  configured for homebrew/CFW. this is not a stock-firmware installation method.
- **optional samples:** extract the separate CC0 starter pack as described by
  its README. the embedded instruments and synthesis do not require that pack.
- do not overwrite existing user data when extracting files. read the included
  license notices and platform/setup requirements before launching.

## release files

[replace with the exact generated filenames and sizes; do not list missing
assets. identify the application ZIP, complete matching source/dependency
archives, optional starter pack, standalone 3DSX/CIA if supplied, and
SHA256SUMS. source and license access must also accompany standalone downloads.]

## verification and limitations

- source revision/content identity: [exact source manifest or revision]
- toolchain/dependencies: [versions from this build]
- build/package checks: [commands actually run and outcomes]
- host tests: [commands actually run and outcomes; not "all tests" unless true]
- hardware: [console model, launch format, environment, tested features/duration;
  or explicitly "not tested for this candidate"]
- known limitations: [actual open issues and checks not performed]

successful compilation/host rendering does not establish real-device frame
rate, audio quality, memory stability, microphone behavior, or compatibility
across Old/New 3DS models. older patch readers may not preserve newer optional
metadata; keep backups and use matching versions.

## license and acknowledgments

application code: **GPL-3.0-or-later**. fonts: **SIL Open Font License 1.1**.
the embedded acoustic bank and optional starter samples have their documented
**CC0** terms. the included `THIRD_PARTY_NOTICES.md` covers provenance and
runtime-library notices.

---

## maintainer-side preparation

1. use an allowlisted source export, not the full development workspace or its
   Git history. exclude private samples, device captures, personal paths,
   historical build artifacts, and credentials. inspect the exported tree.
2. build from that source export in a clean, isolated directory. record exact
   tools/dependencies, build logs, candidate checksums, and which tests actually
   passed. do not substitute a previously deployed binary for the new build.
3. ensure matching editable font/audio sources and preparation scripts are in
   the source release, plus required dependency sources/notices. a GitHub
   auto-generated source archive is not automatically complete when linked
   dependencies or generated asset originals are outside the repository.
4. verify files directly against the build manifest and `SHA256SUMS`. a
   checksum is an integrity reference, not a signature or proof of hardware QA.
5. once authorized, confirm canonical repository/default branch, author credit,
   license metadata, and a tag pointing to the reviewed source. set this release
   to **prerelease**; do not label an alpha stable/latest to bypass store rules.
6. before any actual publication, obtain the user's separate go-ahead. creating
   a repository, pushing a branch/tag, uploading an asset, or opening an issue/PR
   is outside this local preparation task.

## local approval fields

- canonical repository: **pending — no Git remote configured**
- public release/tag/download URLs: **pending — nothing published**
- exact corresponding-source/dependency package review: **pending**
- final hardware/clean-install/update matrix: **pending**
- publication authorization: **not granted by preparation alone**
