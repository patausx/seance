# seance note / bitmap display face

seance note is a small bitmap derivative of **gossip med pixel**, by Deborah
Khodanovich (copyright 2025). the original `Gossip-MedPixel.otf` is unchanged;
sha256 `6ac1f95bfcf19a501bcd2b228bdae9a22321785d743c7424f36f35fe5138520c`.
source: user-provided gossip.zip; designer https://dvorit.ca.

both the source font and this bitmap derivative are licensed under **sil open
font license 1.1**, reproduced in `OFL.txt`. original reserved font names are
Gossip and Gossip Icons. the derivative's primary name is **seance note**.
modifications copyright 2026 seance contributors: subset, offline rasterization,
packed masks/metrics and dedicated note composition. code renderer/generator is
project code; glyph data remains OFL. this does not relicense the app.

`python3 tools/generate_note_display.py` writes
`platform/3ds/note_display_data.h`; `--check` verifies it. uses Pillow/FreeType
on the host only. no font engine, file I/O, heap allocation, texture or new
font dependency on 3ds. checked-in bitmap header is sufficient to build.

- large a–g and 0–9: original med pixel at 56 ppem, threshold128;
- small octave digits and reverse r: 28 ppem, threshold128;
- 28 glyphs, 1904 bytes row-packed 1bpp, plus metrics;
- fixed baselines, original ink shape; no horizontal/fractional bitmap scaling;
- explicit sharp, plus and minus UI strokes: original # is empty, plus absent;
- only the large selected pitch on the top sequencer uses this face;
- bottom step labels remain ordinary project 6x8; splash is untouched.

keep this notice and `OFL.txt` alongside packaged binaries and derived font data.
see `research/gossip/README.md` for the original study; its previews predate this
runtime integration and are not the implementation source.

## seance menu / startup labels

**seance menu** is a separate bitmap derivative of the same unmodified source,
under the same SIL OFL 1.1 and copyright/notice above. Original reserved font
names are not used as its primary name. Modifications copyright 2026 seance
contributors: three cropped labels (`new patch`, `library`, `settings`) at
28 ppem, threshold128, row-packed 1bpp; 692 bytes plus metrics. Generated with
`python3 tools/generate_start_menu_labels.py` (`--check` verifies), shipped as
`platform/3ds/start_menu_labels_data.h`. Existing Seance Note and splash data are
unchanged. Keep this combined notice and `OFL.txt` alongside binaries.

### shared menu headings

`python3 tools/generate_menu_labels.py` (`--check` verifies) separately bakes
`platform/3ds/menu_labels_data.h`: 24 complete cropped labels at the same original
28 ppem / threshold128 raster, 2488 row-packed 1bpp bytes plus metrics. Labels are
`patch table`, `go to`, `add module`, `empty` and all twenty plain module names.
This is an extension of **seance menu**, under the same OFL/copyright above, not
an alteration of the original OTF, startup labels/generator, seance note or splash.
The shared helper coalesces identical rows and ink runs; ordinary list entries
remain the native 6x8 face. Keep this combined notice with the derived data and
packaged binaries.
