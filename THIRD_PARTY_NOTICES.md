# third-party notices

seance application code is licensed under **GPL-3.0-or-later**; see
[LICENSE](LICENSE). that does not relicense the fonts, recordings, or libraries
below. retain their original notices and preferred editable sources in source
releases, and include the applicable notices with binary releases.

the menu recordings are the maintainer's own work. ownership and inclusion of
originals and embedded PCM in the seance source/binary distribution were
confirmed on 2026-10-24; see the provenance section below. this does not imply
permission to publish the project before its separate release approval.

## fonts

- **Gossip Med Pixel**, copyright 2025 Deborah Khodanovich; designer:
  <https://dvorit.ca>. original supplied as `Gossip-MedPixel.otf`, SHA-256
  `6ac1f95bfcf19a501bcd2b228bdae9a22321785d743c7424f36f35fe5138520c`.
- license: **SIL Open Font License 1.1**, reproduced verbatim in
  [assets/fonts/OFL.txt](assets/fonts/OFL.txt).
- original reserved font names: **Gossip** and **Gossip Icons**. the original
  font is unchanged. bitmap derivatives include **seance note**, **seance menu**,
  **Seance Filter**, **Seance Gran**, and the module-heading subsets. subsetting,
  rasterization, packed masks/metrics, and composition changes are copyright
  2026 seance contributors. derivative primary names do not use the reserved
  names. generated headers retain their font notices.
- keep `assets/fonts/README.md`, the OFL text, and the original OTF together with
  the derivative sources. the renderer/generator code remains application code;
  the glyph data remains OFL. including a font does not put the application
  itself under the OFL.

## embedded acoustic instrument bank

all 22 selected recordings are distributed under **CC0 1.0 Universal** by their
upstream projects. this statement is limited to the individually identified
bank sources, not arbitrary audio in this workspace.

- **VS Chamber Orchestra 2: Community Edition**, Versilian Studios / Sam Gossner
  and Simon Dalzell. upstream sample-cutting credit: Elan Hickler / Soundemote.
  <https://github.com/sgossner/VSCO-2-CE>, pinned commit
  `440300901dfe9275fd84e0b7763af1f8443ae62e`.
- the upright piano is credited to **Simon Dalzell, Ivy Audio**, winter 2015–16.
  the upstream `Info.txt`, mapping chart, and redistribution statement are
  retained with the source material.
- **Versilian Community Sample Library**, Versilian Studios LLC,
  <https://github.com/sgossner/VCSL>, pinned commit
  `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e`, supplies the percussion hits.
- pitch-reference SFZ metadata is pinned separately at
  `6dd651d55dde97fd4028699be9d4481f26917891`; it is preparation provenance,
  not a runtime SFZ loader.

see [assets/instruments/README.md](assets/instruments/README.md),
`assets/instruments/sources.json`, `assets/instruments/bank.json`, and
[assets/instruments/licenses/](assets/instruments/licenses/) for the exact
inputs, retained upstream credits, licenses, and processing. preserve the
upstream requests for attribution and not selling the raw sample collection
alongside the actual CC0 licenses; do not describe those requests as additional
CC0 restrictions. the prepared bank is embedded in the application, so these
notices matter even when no external sample pack is installed.

## optional starter samples

the separate `sample_library/starter_cc0/` pack contains selected, converted
recordings from the **Stargate Sample Pack**:
<https://github.com/stargatedaw/stargate-sample-pack>.

license: **CC0 1.0 Universal**. its `README.md` records the original-to-prepared
filename mapping; `CC0-1.0.txt` reproduces the license. keep both with the optional
pack. users do not need this pack to use the synthesizers or embedded instrument
bank. user-imported or recorded sounds do not acquire this license.

## menu recordings — author confirmation

- `assets/menu_audio/source/1.wav`
- `assets/menu_audio/source/2.wav`
- their prepared derivatives `assets/menu_audio/menu_drone.bin` and
  `assets/menu_audio/menu_entry.bin`

the seance maintainer confirmed these are their own recordings and approved
including the originals and embedded PCM with seance's source and binaries on
2026-10-24, in response to the release-preparation redistribution query. only
the WAV filenames changed; source hashes, prepared PCM and playback are unchanged.
see [assets/menu_audio/README.md](assets/menu_audio/README.md) and the manifest
for exact source identities and processing.

this records the author's project-distribution confirmation, not a CC0
dedication or a separately selected standalone audio license. do not infer
other assets' license terms for these recordings. the `.bin` files are linked
into the executable; source releases retain the original WAVs and preparation
script. the separate instruction not to publish yet remains in force.

## devkitPro runtime libraries

local package inventory reviewed for release preparation:

| library | package version | upstream source |
| --- | --- | --- |
| libctru | 2.7.0-1 | <https://github.com/devkitPro/libctru/tree/v2.7.0> |
| citro2d | 1.7.0-1 | <https://github.com/devkitPro/citro2d/tree/v1.7.0> |
| citro3d | 1.7.1-2 | <https://github.com/devkitPro/citro3d/tree/v1.7.1> |

these are statically linked dependencies. the following project-level notices
were checked against the upstream versions above. keep any additional per-file
notices when redistributing their source. this is not an exhaustive audit of
all code or compiler runtime routines in the final linked executable.

### libctru project-level notice

reproduced from the license section of upstream `README.md` at `v2.7.0`:

```text
  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any
  damages arising from the use of this software.

  Permission is granted to anyone to use this software for any
  purpose, including commercial applications, and to alter it and
  redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you
     must not claim that you wrote the original software. If you use
     this software in a product, an acknowledgment in the product
     documentation would be appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and
     must not be misrepresented as being the original software.
  3. This notice may not be removed or altered from any source
     distribution.
```

### citro2d and citro3d project-level notices

citro2d `v1.7.0/LICENSE`:

```text
Copyright (C) 2017-2018 fincs

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you
   must not claim that you wrote the original software. If you use
   this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and
   must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
   distribution.
```

citro3d `v1.7.1/LICENSE` has the same terms, with this copyright line:

```text
Copyright (C) 2014-2018 fincs
```

## toolchain and corresponding-source review

building uses devkitARM/GCC, newlib, and devkitPro tools. they are not all linked
into seance merely because they run during the build. actual linked runtime
components and their applicable notices/exceptions must be recorded from the
release build; do not assume one license applies to the entire toolchain.

for GPL binary distribution, make the matching application source, generated
inputs' editable sources, required scripts, and any required dependency sources
available in compliance with GPLv3 sections 1 and 6. **static linking does not by
itself establish the System Libraries exception.** permissive library licensing
also does not by itself settle the application's corresponding-source duties.
see [distribution/dependency-sources.md](distribution/dependency-sources.md) for
the conservative bundling plan and remaining review. no claim is made here that
an application-only source ZIP or a link to an upstream default branch is a
complete corresponding-source release.

## startup and compiler runtime evidence

`devkitarm-crtls 1.2.6-1` supplies the 3DSX startup object. its
`3dsx_crt0.s` is under MPL-2.0; the original source archive and saved package
recipe are in `distribution/dependency-sources/`, with the license text in
`licenses/MPL-2.0.txt`. retain that source alongside binary distribution.

`licenses/GCC-RUNTIME-EXCEPTION-3.1.txt` and `licenses/GCC-GPL-3.0.txt` contain
the GCC runtime terms; `licenses/compiler-runtime-evidence.json` records the
installed header evidence and selected runtime archive hashes. exception
eligibility and final linked-object coverage still require release review.
newlib's complete runtime/license audit remains pending. bundling these notices
is not a claim that every linked runtime component has been cleared.

## explicitly excluded material

private imported sample collections, user recordings/projects, device captures,
promotional media, and historical build/output directories are not licensed for
redistribution by this notice. in particular the private mixed-license sample
library includes MusicRadar material whose local provenance prohibits sample
redistribution. do not include it in the public repository or release pack.
