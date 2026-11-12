# pent — preserved source layers

these are exact copies of the two local layers selected in
`ui_mockups/pent_glow_particles_study/composed-v1/dual.png`, not new artwork.
the original studies and their receipts remain untouched.

- `pent-glow-particles-v1.png`: red glow/particles, 24 frames256×200,
  six columns/four rows; upper origin `(112,24)`.
- `pent-liquid-blood-v1.png`: white star/ring and ten dimensional bloody drops,
  24 frames256×192; upper origin `(112,32)`.
- each `.png.frames.json` retains the exact original frame metadata.
- `glow-scene.json` / `liquid-scene.json` retain the original declarative,
  provider-free pixel-render recipes. their output paths describe their original
  creation; they are not an automatic build or network request.

the static runtime header is reproduced losslessly from these pngs:

```sh
python3 -B tools/generate_pent_visual.py
python3 -B tools/generate_pent_visual.py --check
```

this conversion uses the existing ffmpeg executable only for local rgba decoding.
normal 3ds builds consume `platform/3ds/pent_visual_data.h`; they do not need
ffmpeg, python, a model, an image extension or the original experiment folders.
source hashes are recorded in that generated header. no raster resizing,
quantization or palette changes occur. font/title data is separate and retains
its original ofl notices. these layers contain no imported photos or samples;
this provenance note does not clear the application's other distribution gates.

## reactive-v1 — static derivatives, original sources retained

`reactive-v1/fx.png` is the exact frame15 `[768,400,256,200]` crop made once
with provider-free `imagen_prepare`, without scaling/quantization. `atlas.png`
is one provider-free `imagen_render` of the saved `atlas-scene.json` args:
unchanged original objects10..15 (white ring + five lines) in the top256×192;
first heads0/2/4/6/8 with polygon groups16..19/24..27/32..35/40..43/48..51.
only motion is removed and each five-object head is translated into its40×40
tile `[i*40,208,40,40]`, anchored at `(20,20)`; no shape/palette/size changes.
`provenance.json` retains exact args/hashes and verified derivative facts.

```sh
python3 -B tools/generate_pent_reactive.py
python3 -B tools/generate_pent_reactive.py --check
```

this separate generator verifies original PNG/scene hashes, the exact fx crop,
original2415 white pixels, head derivation, disjoint unclipped original-palette
tiles and lossless rectangle coverage. it emits `pent_reactive_data.h` with two
whole256×256 textures (fx padded transparent below200; white + heads atlas).
the retained head-sprite runtime changes only crop positions/alpha; it is
historical after the local full-edge flow correction.
normal ARM builds need neither this generator nor ffmpeg/python/extension.
the old sheets, scene recipes, `generate_pent_visual.py` and `pent_visual_data.h`
are retained unchanged as historical sources, not an active24-frame draw path.

## full-edge flow strip

`pent_flow_strip_v1.png/.json` and `platform/3ds/pent_flow_data.h` are a separate
local seamless128×16 period, repeated twice in previously empty atlas rows192..207.
`python3 -B tools/pent_flow_generate.py --check` verifies them. the current local
renderer scrolls five connected ribbon UVs, not old head crops; two512KiB textures
still upload once. original FX/white/head assets and their generator remain intact.
see `docs/pent-next-chat.md` for inline admission limits and the unimplemented
interior-fill/character trial; this asset does not establish device performance.
