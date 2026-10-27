# dependency source and notice plan

**final release review pending.** the local source bundle now contains the
checksum-verified libctru, citro2d, citro3d and devkitarm-crtls source archives,
saved package recipes and provenance in [dependency-sources/manifest.json](dependency-sources/manifest.json).
check their bytes against `dependency-sources/SHA256SUMS`. this is a local
preparation result, not a claim of fully reviewed corresponding-source compliance.
newlib's final linked-runtime/license audit is still pending.

## what the build links

`Makefile` lists `-lcitro2d -lcitro3d -lctru -lm`. the reviewed local devkitPro
package inventory was:

| package | source tag | source archive |
| --- | --- | --- |
| libctru 2.7.0-1 | `v2.7.0` | <https://github.com/devkitPro/libctru/archive/refs/tags/v2.7.0.tar.gz> |
| citro2d 1.7.0-1 | `v1.7.0` | <https://github.com/devkitPro/citro2d/archive/refs/tags/v1.7.0.tar.gz> |
| citro3d 1.7.1-2 | `v1.7.1` | <https://github.com/devkitPro/citro3d/archive/refs/tags/v1.7.1.tar.gz> |
| devkitarm-crtls 1.2.6-1 | `v1.2.6` | <https://github.com/devkitPro/devkitarm-crtls/archive/refs/tags/v1.2.6.tar.gz> |

### locally verified archives

| file in `dependency-sources/` | bytes | SHA-256 |
| --- | ---: | --- |
| `libctru-2.7.0.tar.gz` | 371148 | `c0b8a5048e15a0ae02772e6db0188f5571e8c26d064d7962212a40681f0d18b3` |
| `citro2d-1.7.0.tar.gz` | 49046 | `f311d1decb64668980b7a7a917a9b936a40025265df921e251f5f0537b94b86b` |
| `citro3d-1.7.1.tar.gz` | 61920 | `c920515a791442e81e0c2c8867dde81bf14aca17eacb2709b4a3a3c61699250c` |
| `devkitarm-crtls-1.2.6.tar.gz` | 17427 | `0c7b991a2a8427bca0a64db37138ecc95276eef3958a9be6da6310afd1a1066f` |

all four match the **effective final** `sha256sums` assignment in their saved
recipes. some recipes contain an earlier assignment superseded later in the
same file. downloads used the recipe archive endpoints over HTTPS; exact
request URLs and pinned recipe URLs are recorded in the manifest. no archive
was repacked. extracted notices preserve the upstream text, and complete
per-file notices remain in the original archives.

version alone is not proof that an installed archive has no local patches.
retain final package versions and linked-library hashes; compare their actual
package recipes/sources as part of release review. inspect the link map/runtime
inventory for additional relevant libraries, including newlib, libgcc and
libstdc++ where used. a build tool is not necessarily a linked component.

## verified upstream references

public HTTP GET reviewed on 2026-10-24:

- libctru project-level license: <https://github.com/devkitPro/libctru/blob/v2.7.0/README.md#license>
- citro2d license: <https://github.com/devkitPro/citro2d/blob/v1.7.0/LICENSE>
- citro3d license: <https://github.com/devkitPro/citro3d/blob/v1.7.1/LICENSE>
- libctru recipe: <https://github.com/devkitPro/pacman-packages/blob/master/libctru/PKGBUILD>
- citro2d recipe: <https://github.com/devkitPro/pacman-packages/blob/master/citro2d/PKGBUILD>
- citro3d recipe: <https://github.com/devkitPro/pacman-packages/blob/master/citro3d/PKGBUILD>

all three project-level library notices are zlib-style permissive terms. their
notices are reproduced in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
preserve additional per-file licenses in full source trees. do not replace
original copyrights or infer a license for unreviewed runtime components.

at review time the current package recipes built these tags using:

```sh
# inside the extracted libctru-2.7.0 source tree
make -C libctru
make -C libctru DESTDIR="$STAGING" install

# inside the extracted citro2d-1.7.0 source tree; devkitPro catnip required
catnip
DESTDIR="$STAGING" catnip install

# inside the extracted citro3d-1.7.1 source tree
make
make DESTDIR="$STAGING" install

# inside the extracted devkitarm-crtls-1.2.6 source tree
make
make DESTDIR="$STAGING" install
```

these are recipe descriptions, **not commands executed by documentation
preparation**. use devkitPro's supported environment and dependency order
(libctru, citro3d, citro2d), and retain the exact package revisions/recipes used.
source recipes on a default branch can change: pin a commit or store the recipe
and its hash when collecting final release sources.

## gpl corresponding source

the authoritative project license is [LICENSE](../LICENSE), particularly GPLv3
section 1 (Corresponding Source / System Libraries) and section 6 (conveying
non-source forms). permissive source licenses allow these libraries to be
redistributed, but do not independently answer what the GPL application must
supply with its binaries.

static linking does **not** automatically make a library a System Library.
some build tools, unmodified general-purpose tools, runtime exceptions, or
qualifying System Libraries may be excluded under the actual terms; record the
reason for each exclusion rather than assuming everything from devkitPro is
exempt. compiler runtime exceptions require checking the applicable component,
license and exception; there is no blanket "devkitPro exception" asserted here.

for a conservative release:

1. retain the exact unmodified source archives, their full notices, and package
   recipes for libctru/citro2d/citro3d next to the matching application source.
   retain patches if anything was modified. this avoids relying on an unreviewed
   System Libraries claim for these three static dependencies.
2. record archive hashes and exact recipe revisions. check that the archives
   actually match the packages used for the final build, rather than merely
   downloading a similarly named tag. account for the remaining linked runtime
   components with their applicable source/notices or documented exclusion.
3. include all application build scripts, `.inc` sources, editable font/audio
   inputs, preparation tools, and any required installation information. a
   generated header or object file is not its preferred form for modification.
4. for public network distribution, arrange equivalent source access through
   the same designated place as the binary downloads, consistent with GPLv3
   section 6(d). an accompanying source archive is the intended practical route.
   a README link to a moving upstream branch alone is not this plan.
5. document the exact source/dependency asset names and their relationship to
   the binary checksums. ensure standalone files and future store downloads
   point users to the same notices and corresponding source. verify the source
   is actually reachable when binaries become reachable.

this plan must be reviewed against the actual complete package before a claim
of GPL-compliant public binary distribution. local preparation has not created
a hosted source offer, satisfied missing audio rights, or performed a legal
assessment of every toolchain runtime object.

## retained runtime evidence

`dependency-sources/manifest.json` records installed library hashes, source and
recipe checksums, and pinned recipe commit
`b9e8b0de6a6423557fd2a57dc100bf327c79dd4a`. archives match the saved recipes;
the installed dependency objects have not been rebuilt for a byte comparison.

`devkitarm-crtls 1.2.6-1` contributes `3dsx_crt0.o`; its source
`3dsx_crt0.s` uses **MPL-2.0**, not the GCC runtime exception. the original
`devkitarm-crtls-1.2.6.tar.gz`, recipe, and `licenses/MPL-2.0.txt` are included.
GCC 15.2.0 runtime exception and GPL texts are in `licenses/`; header evidence
and selected runtime archive hashes are in
`licenses/compiler-runtime-evidence.json`. these do not grant an exception to
newlib or startup code. the saved newlib recipe is a review aid, not bundled
newlib source or a completed license audit.

## pending evidence

- final link/runtime inventory, newlib notices and treatment of each component;
- clean application rebuild and archive verification, recorded by the candidate;
- public source/download access plan, notice delivery, and publication approval.
