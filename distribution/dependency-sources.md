# dependency sources

## what the build links

`Makefile` lists `-lcitro2d -lcitro3d -lctru -lm`; 3DSX startup uses devkitarm-crtls.
these retained source snapshots may differ from your installed libraries.
for binary redistribution, obtain sources, patches and recipes matching the
packages actually linked.

| snapshot | source archive | saved package recipe |
| --- | --- | --- |
| libctru 2.7.0 | [tar.gz](https://github.com/devkitPro/libctru/archive/refs/tags/v2.7.0.tar.gz) | `libctru.PKGBUILD` |
| citro2d 1.7.0 | [tar.gz](https://github.com/devkitPro/citro2d/archive/refs/tags/v1.7.0.tar.gz) | `citro2d.PKGBUILD` |
| citro3d 1.7.1 | [tar.gz](https://github.com/devkitPro/citro3d/archive/refs/tags/v1.7.1.tar.gz) | `citro3d.PKGBUILD` |
| devkitarm-crtls 1.2.6 | [tar.gz](https://github.com/devkitPro/devkitarm-crtls/archive/refs/tags/v1.2.6.tar.gz) | `devkitarm-crtls.PKGBUILD` |

original archives, recipes and `SHA256SUMS` are retained in
`distribution/dependency-sources/` in the source package. the
[manifest](dependency-sources/manifest.json) records original locations,
package details and checksums.

## rebuilding the snapshots

use the [devkitPro environment](../BUILDING.md#requirements) and the saved recipes.
build libctru, then citro3d, then citro2d; citro2d requires devkitPro's catnip.
set `STAGING` to an installation staging directory and run these commands from
the indicated extracted source trees:

```sh
# libctru-2.7.0/
make -C libctru
make -C libctru DESTDIR="$STAGING" install

# citro3d-1.7.1/ and devkitarm-crtls-1.2.6/ (each separately)
make
make DESTDIR="$STAGING" install

# citro2d-1.7.0/
catnip
DESTDIR="$STAGING" catnip install
```

## licenses and runtime sources

[libctru](../licenses/libctru-zlib.txt), [citro2d](../licenses/citro2d-zlib.txt)
and [citro3d](../licenses/citro3d-zlib.txt) have zlib-style project notices.
preserve their additional per-file notices in the archives.

`3dsx_crt0.s` in devkitarm-crtls is [MPL-2.0](../licenses/MPL-2.0.txt).
when distributing its executable form, also make its source available and tell
recipients how to obtain it, as required by MPL section 3.2.

only a saved `newlib.PKGBUILD` recipe is retained; newlib source and its patch
are not bundled. the included [GCC license](../licenses/GCC-GPL-3.0.txt)
and [Runtime Library Exception 3.1](../licenses/GCC-RUNTIME-EXCEPTION-3.1.txt)
are license texts, not a complete compiler/runtime source bundle.

## gpl corresponding source

binary redistribution must satisfy GPLv3 [LICENSE](../LICENSE), especially
sections 1 and 6:

- supply matching Corresponding Source, including required application/dependency
  sources, patches, build/install scripts, editable resource inputs and preparation tools.
- for network downloads under section 6(d), provide equivalent source access
  through the same designated place as the binary downloads.
- keep original licenses and [third-party notices](../THIRD_PARTY_NOTICES.md).

account for linked runtime components, including newlib, libgcc and libstdc++
where used, under their actual terms. static linking does not automatically
qualify a component as a GPL System Library. the GCC exception applies only to
files that grant it and eligible compilation; it does not cover newlib or MPL
startup code merely because they come from devkitPro.

[release scripts](../scripts/README.md) describe application/source packaging.
