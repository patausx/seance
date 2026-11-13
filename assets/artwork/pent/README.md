# pent artwork

PENT's glow, white scaffold and blood-ribbon assets.

## resources

- [pent-glow-particles-v1.png](pent-glow-particles-v1.png): 24 frames of 256×200.
- [pent-liquid-blood-v1.png](pent-liquid-blood-v1.png): 24 frames of 256×192.
  Both sheets use six columns/four rows, with frame metadata in `*.png.frames.json`.
- [glow-scene.json](glow-scene.json) and [liquid-scene.json](liquid-scene.json): source recipes.
- [reactive-v1/fx.png](reactive-v1/fx.png) and [atlas.png](reactive-v1/atlas.png):
  static FX and white/head atlas; [atlas-scene.json](reactive-v1/atlas-scene.json) is the atlas recipe.
- [pent_flow_strip_v1.png](pent_flow_strip_v1.png): 256×16 strip with two 128×16 periods;
  [pent_flow_strip_v1.json](pent_flow_strip_v1.json) contains its metadata.

## generated headers

All headers live in `platform/3ds/`:

- `pent_visual_data.h`: retained 24-frame sheets.
- `pent_reactive_data.h`: static FX and white/head atlas.
- `pent_flow_data.h`: seamless strip used by the connected ribbon UVs.

From the repository root:

```sh
python3 -B tools/generate_pent_visual.py --check
python3 -B tools/generate_pent_reactive.py --check
python3 -B tools/pent_flow_generate.py --check
```

The first two generators need Python 3 and FFmpeg; omit `--check` to rebuild their
headers. The flow generator uses the Python standard library and only creates
files when all its outputs are absent; use `--check` for the existing strip.
Normal 3DS builds use the checked-in headers.

Font/title attribution: [font notices](../../fonts/README.md).
Project licenses and credits: [THIRD_PARTY_NOTICES.md](../../../THIRD_PARTY_NOTICES.md).
