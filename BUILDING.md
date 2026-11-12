# building seance

## requirements

- devkitPro devkitARM, the `3ds-dev` group, libctru, citro2d and citro3d;
- GNU make and a POSIX shell;
- makerom only for CIA/CCI output.

in an existing devkitPro installation:

```sh
sudo dkp-pacman -S --needed 3ds-dev
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
make -j2
```

this produces `seance.elf`, `seance.smdh` and `seance.3dsx`. for a CIA:

```sh
make -j2 cia MAKEROM=/path/to/makerom
```

`MAKEROM` defaults to `$HOME/tools/3ds/makerom`; CIA packaging uses devkitPro's
`smdhtool`, not bannertool. `seance.rsf` holds the application's title identity.
`VERSION` supplies the version string, currently `0.1.0-alpha.1`; editing it
invalidates C++ objects and the ELF link, so incremental output cannot retain the
old version merely because the source files did not change. a source update does
not imply a new release version. keep the existing title identity for updates.
make does not track arbitrary flag/toolchain/library changes: for release evidence,
build a new isolated source copy, not a reused development build directory.

normal builds use checked-in font data, artwork, instrument samples and menu
PCM. they require no Python, Pillow, ffmpeg, test files or downloaded sample
pack. `make clean` removes local application output. `make run` / `make send`
are optional network deployment commands; they are not build steps.

## resources and release packages

[tools/](tools/README.md) contains only asset generators. regenerate deliberately
when changing resources, not after unpacking the source. asset READMEs retain
original inputs, licenses and commands.

[scripts/](scripts/README.md) contains source export and packaging commands.
release scripts need Python 3.10+; rebuilding menu audio additionally needs the
conversion tools named by its generator. source export and packaging never
publish, upload or install anything.

check an exported source tree with:

```sh
python3 -B scripts/audit_public_source.py
```

this read-only audit checks exported text/paths and relative documentation links;
it is not a build, a complete secret/license review or a hardware test. local
development test suites are not included in the public source export.

review [third-party notices](THIRD_PARTY_NOTICES.md) and
[dependency sources](distribution/dependency-sources.md) when redistributing.
the **historical 2026-10-24 source archives** retain libctru 2.7.0, citro2d 1.7.0,
citro3d 1.7.1 and devkitarm-crtls 1.2.6; their old hashes are not current installed
provenance. local package metadata inspected 2026-11-06 reports libctru 2.7.1-1
and devkitarm-gcc 16.1.0-1. those differ from retained libctru/GCC15 evidence.
record the actual libraries selected by the final build and obtain matching
sources/recipes/notices before distribution; do not relabel historical archives.

## release coverage and source-to-artifact procedure

- `make release` builds/packages the application and starter-sample archives only.
  it does **not** deliver an application source ZIP or establish matching source.
- `make release-source` packages current allowlisted working-tree bytes separately
  without devkitARM or local tests. like `package_release.py --source`, it cannot
  prove that an existing binary was compiled from those bytes.
- for a source-bound local candidate, choose a new private directory and use
  `python3 -B scripts/prepare_public_release.py --output release/new-candidate --jobs 2 --makerom /path/to/makerom`.
  it exports exact allowlisted bytes, builds there, checks source/tool/runtime
  stability, and packages application/source/sample ZIPs from that same copy.
  nothing is published. use `--source-only` for a source inventory without artifacts.

retain together the before/after source inventories and their receipt hashes,
build command/environment and complete log, compiler/rule/runtime input hashes,
ELF/SMDH/3DSX/CIA hashes, final `build/seance.map`, download/ZIP hashes and ZIP
member manifests. audit the exported tree explicitly; an export exit 0 does not
run a privacy/rights audit. review actual linked archive members, source/patch
recipes, newlib and exception eligibility against the final map, not merely a
compiler version string. an inventory is a byte snapshot, not automatically a git
tag or a byte-reproducible ARM build. never attach stale artifacts to a new export.

`publication_ready` remains **false** in local receipts. verified menu-WAV grants
for both originals and derivative PCM, matching current dependency/runtime
source/notices, real-device acceptance and separate publication authorization
remain external gates. do not publish merely because packaging passed.

## explicit development admission

in the development workspace with the local-only test suite:

```sh
make check
```

this host-only entry point runs `make test-module-frame`'s dependency chain,
then `python3 -B tests/test_nav_instant.py`, stopping on failure. it needs GNU
make, Python 3 and host `g++` with ASAN/UBSAN, not devkitARM. `make -j check`
keeps that order and shares the frame gate with an explicit `test-module-frame`
goal. it is a quick preflight, not `make test`, every module's suite, a full
native UI capture or hardware acceptance. run the scoped audio/storage/UI checks
relevant to the change afterwards; new/changed UI also needs actual captures.

normal/public compilation and release targets do not run these gates or depend
on local tests/Python (packaging itself still needs Python). public exports omit
`tests/Makefile`; explicitly requesting `make check` there fails with a local-test
diagnostic. mixed goals such as `make check all` still require devkitARM and can
schedule the build alongside checks; use separate invocations for admission.

before a binary release, test installation, audible output, sample I/O, mic/REC,
project round-trip, sleep/wake and sustained playback on real hardware. record
the console model and launch format; Old 3DS remains untested. the development-only
`docs/hardware-session-checklist.md` is a short session sheet, not an exported
release document or a claim that any device session has passed.
