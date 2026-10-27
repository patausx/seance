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
`VERSION` supplies the version string. keep the existing identity for updates.

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

review [third-party notices](THIRD_PARTY_NOTICES.md) and
[dependency sources](distribution/dependency-sources.md) when redistributing.
the retained dependency versions are libctru 2.7.0, citro2d 1.7.0, citro3d 1.7.1
and devkitarm-crtls 1.2.6. a newer toolchain may change compiler output.

before a binary release, test installation, audible output, sample I/O, mic/REC,
project round-trip, sleep/wake and sustained playback on real hardware. record
the console model and launch format; Old 3DS remains untested.
