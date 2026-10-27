# branding assets

seance uses a multilingual gothic wordmark in the startup menu and circular
launcher icons. These are artwork, not a general-purpose font. The UI typeface
and its license are documented separately in [assets/fonts](../fonts/README.md).

## current artwork

- `circle_logo_reference.png` is the approved 1024px master for the black circular
  launcher mark containing СЕАНС / SEANCE / SITZUNG / SESIÓN.
- `../icon.png` is the 48×48 launcher icon; `icon_small.png` is the 24×24 icon.
  Both are reduced independently from the circular master. At 24px the lettering
  is a visual texture rather than readable small text.
- `gothic_wordmark_pixel_master.png` is the 86×26 English pixel master, authored
  in `tools/pixel_wordmark.py`. `gothic_wordmark_header_mask.png` is its integer
  2× version. `gothic_wordmark_native_mask.png` places the integer 4× lettering
  on a 400×240 canvas, with its visible bounds at `(28, 64)` to `(372, 168)`.
- The startup wordmark cycles through `seance → сеанс → sitzung → sesión`.
  Each word holds for four seconds, then morphs over eight seconds: a 48-second
  loop. `tools/generate_wordmark_morph.py` contains the additional hand-authored
  glyphs and produces `platform/3ds/wordmark_morph_data.h`. The Cyrillic and
  accented text is an intentional part of the artwork, not UI translation.

Normal 3DS builds use the committed PNGs and generated headers. They do not need
Pillow, local preview folders, uploads, installed desktop fonts or an image
service.

## check without changing assets

Run from the repository root with Python 3 and Pillow installed:

```sh
python3 tools/generate_circle_logo.py --check
python3 tools/pixel_wordmark.py --check
python3 tools/generate_wordmark_morph.py --check
python3 tools/generate_wordmark_reference.py --check
python3 tools/test_branding.py
```

`test_branding.py` can also inspect the RGB565 icon planes in a built SMDH:

```sh
python3 tools/test_branding.py seance.smdh
```

The corresponding generators have explicit `--install` options for updating
assets. Use them only when intentionally changing the artwork. After changing
the English pixel master, regenerate both the morph data and test reference.
Asset checks do not establish console rendering speed or launcher cache behavior.

## readme animation

`readme-wordmark.gif` presents the production wordmark morph on a flat
`#212830` background with `#f4f4f4` ink. it is branding artwork, not a console
capture. the runtime logo and its 48-second cycle are unchanged. the GIF samples
at 10 fps, coalesces identical holds, and uses exact nearest-neighbor 2x pixels
on an 800x320 canvas. no uploaded reference is needed to regenerate it.

```sh
python3 tools/render_readme_wordmark.py --install
python3 tools/render_readme_wordmark.py --check
```

Python 3, Pillow and a C++17 host compiler are required. `--check` compares the
decoded GIF pixels and timing against the production C++ renderer and checks all
four endpoints against the editable wordmark artwork. it does not alter assets.

## retained references and older studies

`gothic_wordmark_source_mask.png` is the earlier extracted lettering reference.
`gathering_reference.png`, `threshold_master_44x49.png`,
`threshold_icon_{48,24}_mask.png` and `sigil_icon_{48,24}_{rim,core}_mask.png`
retain previous emblem designs and their authored native masks.

`render_gathering_icon.py`, `render_gate_icon.py` and `render_sigil_icon.py` are
historical renderers. Their `--install` options replace the current launcher
icons with those older designs; they are not the current asset-check commands.
Other `render_*_study.py` scripts create optional design previews and may require
local study inputs that are not included in the public source package.
