# building seance

## requirements

- devkitPro devkitARM and the `3ds-dev` group, including libctru, citro2d and citro3d;
- GNU make and a POSIX shell;
- makerom for CIA/CCI output only.

in an existing devkitPro installation, run from the repository root:

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

`MAKEROM` defaults to `$HOME/tools/3ds/makerom`. CIA packaging uses devkitPro's
`smdhtool` and the checked-in `assets/banner.bnr` HOME Menu banner; `bannertool`
is not required for normal builds.

`VERSION` supplies the application version; changing it rebuilds C++ objects
and relinks the ELF. keep the title identity in `seance.rsf` for updates.
use `make clean` before rebuilding after changing compiler flags, the toolchain
or libraries: make does not track those changes automatically.

normal builds use checked-in fonts, artwork and embedded instrument samples.
Python, Pillow, ffmpeg and an external sample pack are not required.
`make clean` removes local build output. `make run` / `make send` use network
deployment and are not needed to compile.

## resources and release packages

[asset tools](tools/README.md) prepare resources when their inputs change.
asset READMEs describe the original inputs, licenses and regeneration commands.

[release scripts](scripts/README.md) need Python 3.10+ and handle source export
and packaging. they do not download dependencies, upload, install or deploy.
check an exported source tree's text, paths and documentation links with:

```sh
python3 -B scripts/audit_public_source.py
```

see [third-party notices](THIRD_PARTY_NOTICES.md) and
[dependency sources](distribution/dependency-sources.md) for redistribution
requirements and the available source snapshots.

## release coverage and source-to-artifact procedure

`make release` builds and packages the application. `make release-source`
packages source separately without devkitARM. packaging existing binaries does
not rebuild them.

for application and source archives built from the same isolated copy, choose
a new output directory:

```sh
python3 -B scripts/prepare_public_release.py --output release/new-candidate --jobs 2 --makerom /path/to/makerom
```

use `--source-only` to export without building. the [script reference](scripts/README.md)
describes options and output locations.
