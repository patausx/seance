# fonts

**seance note** and **seance menu** are bitmap derivatives of **Gossip Med Pixel**
by Deborah Khodanovich, copyright 2025 ([dvorit.ca](https://dvorit.ca)).
The original `Gossip-MedPixel.otf` is unchanged.

## resources

- `platform/3ds/note_display_data.h`: seance note; selected sequencer pitch,
  octave digits and reverse marker.
- `platform/3ds/start_menu_labels_data.h`: seance menu; startup and theme labels.
- `platform/3ds/menu_labels_data.h`: seance menu; shared page/module headings and glyphs.

Ordinary list and bottom-step labels keep the project's native 6x8 face.

## rebuilding

From the repository root, with Python 3 and Pillow/FreeType:

```sh
python3 -B tools/generate_note_display.py --check
python3 -B tools/generate_start_menu_labels.py --check
python3 -B tools/generate_menu_labels.py --check
```

Omit `--check` to rebuild the corresponding header. Normal 3DS builds use the
checked-in headers.

## license

The source and both derivatives use the **SIL Open Font License 1.1**:
[OFL.txt](OFL.txt). Original Reserved Font Names are **Gossip** and **Gossip Icons**;
the derivatives use **seance note** and **seance menu** as their primary names.
Modifications copyright 2026 seance contributors: glyph subsetting, bitmap
masks/metrics and note/menu composition. OFL applies to glyph data, not the
application. Keep this notice and `OFL.txt` with derived font data and binaries.
