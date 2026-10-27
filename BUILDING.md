# building seance

release identity comes from `VERSION` (`0.1.0-alpha.1`). the checked-in generated
font, instrument bank and menu PCM are build inputs, not optional downloads.
read [third-party notices](THIRD_PARTY_NOTICES.md) before redistribution.

## requirements

- devkitPro's **devkitARM**, `3ds-dev`, libctru, citro2d and citro3d;
- GNU make, a POSIX shell and enough disk space for the build;
- **makerom** for a CIA; a normal 3DSX build does not need makerom;
- Python 3.10+ for packaging/host checks; g++ for host tests; Pillow for image checks;
- regeneration of original audio additionally needs tools named by each asset's
  preparation script. it is not part of a normal application build.

use devkitPro's supported installer/package manager for your system, then install
its `3ds-dev` group. for example, in an existing devkitPro setup:

```sh
sudo dkp-pacman -S --needed 3ds-dev
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
```

the local candidate was prepared against libctru **2.7.0-1**, citro2d **1.7.0-1**
and citro3d **1.7.1-2**. exact dependency source archives and package recipes belong
in `distribution/dependency-sources/`; consult its manifest for hashes and build
commands. installing a newer toolchain can change output and is not a promise of
bit-identical binaries. the local build receipt records actual tool versions.

## application

from a clean source directory:

```sh
make -j2
# outputs seance.elf, seance.smdh and seance.3dsx

make -j2 cia MAKEROM=/absolute/path/to/makerom
# also outputs seance.cia
```

`MAKEROM` defaults to `$HOME/tools/3ds/makerom`; override it explicitly if yours is
elsewhere. the current CIA path needs `smdhtool` from devkitPro, not bannertool.
`seance.rsf` contains the existing homebrew title identity; do not invent another
identity for an update of the same application. no proprietary SDK is required.

both formats embed the same current engine and assets. the CIA does not bundle
user projects, samples or DSP firmware. do not use `make run` or `make send` when
you only intend a local build; those are not needed for packaging.

## local release kit (no publication)

```sh
python3 tools/prepare_public_release.py --output release/publication-my-candidate
```

choose a **new, nonexistent output directory**. this command:

1. allowlists the public source instead of copying the workspace or git history;
2. copies it to `source/`, with complete `.inc` files and editable asset sources;
3. builds the 3DSX and CIA **there**, never using root binaries or old objects;
4. creates separate application, source and optional starter-sample ZIPs;
5. copies standalone 3DSX/CIA downloads and writes SHA-256 checksums and a local
   build receipt. nothing is uploaded, installed, committed, tagged or pushed.

if makerom is elsewhere, pass `--makerom /absolute/path/to/makerom`. use
`--source-only` to inspect the exported source without building. source export
includes the author's confirmed menu recordings `1.wav` and `2.wav` and their
embedded PCM. source export is local preparation, **not permission to publish**.
output logs/receipts are local; only the reviewed files in `github-release/`
are prospective downloadable assets.

archives have sorted names, fixed ZIP timestamps and embedded checksums. this
makes repeated packaging of **identical inputs** deterministic; it does not claim
cross-machine reproducible compiler output. `SHA256SUMS.txt` inside a ZIP covers
its other entries, not itself.

for an already-built clean source tree, the lower-level packager is:

```sh
python3 tools/package_release.py --main
python3 tools/package_release.py --source
python3 tools/package_release.py --samples
```

it never compiles. do not run it against stale root artifacts and call those a
new build. version overrides must equal `VERSION`; change the canonical version
before a deliberately versioned release and build again.

## host checks

```sh
python3 tools/test_package_release.py
python3 tools/test_audit_public_source.py
python3 tools/audit_public_source.py
make test-module-frame
python3 tools/test_nav_instant.py
make test-branding
```

these cover packaging and selected current UI contracts, not every engine path.
several older targets in the Makefile are historical harnesses that require
private archived baselines; the source release does not include past binaries,
private captures or those baselines. a missing historical fixture is not a pass.

host tests may generate ignored files in `ui_mockups/`, `release/` or `/tmp`.
sanitizer targets need an ASan/UBSan-capable compiler. normal ARM builds do not
need Pillow, ffmpeg or those historical test outputs. they do need Python 3
for the source-only module-frame preflight.

## generated assets

- `tools/generate_circle_logo.py` and `generate_wordmark_reference.py`: branding;
- font generators and `assets/fonts/`: bitmap derivatives and original OFL face;
- instrument preparation tool and `assets/instruments/`: original CC0 sources,
  mappings and generated acoustic bank;
- `tools/prepare_menu_audio.py` and `assets/menu_audio/`: original recordings,
  prepared PCM and conversion metadata. permission is still required to publish
  either original or generated forms.

regenerating assets is a deliberate source change. do it before the final build,
check its diff, then rerun relevant checks. **do not regenerate** merely because
you unpacked the source ZIP.

## device acceptance remains separate

before public release, test install/launch, audible starter, drawing, sample I/O,
mic/REC, bounce, project round-trip, sleep/wake and sustained use on real hardware.
a clean ARM build and host UI captures establish neither device FPS nor sound
quality. record console model and CIA/3DSX separately. see
[publication checklist](distribution/publication-checklist.md).
