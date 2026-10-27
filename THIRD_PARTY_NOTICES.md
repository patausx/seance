# third-party notices

seance application code is licensed under **GPL-3.0-or-later**; see
[LICENSE](LICENSE). that does not relicense the fonts, recordings, or libraries
below. retain their original notices and preferred editable sources in source
releases, and include the applicable notices with binary releases.

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

**VSCO 2 Community Edition / VCSL**, Versilian Studios, and the included Ivy
Audio piano recordings: **CC0 1.0 Universal**. source identities, upstream
credits and original license texts are retained in
[assets/instruments/](assets/instruments/README.md).

## optional starter samples

the optional [starter pack](sample_library/starter_cc0/README.md) uses the
**Stargate Sample Pack**, **CC0 1.0 Universal**. its source mapping and license
text are included with the pack. these terms do not apply to user-imported audio.

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
