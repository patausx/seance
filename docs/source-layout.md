# source layout

| path | purpose |
| --- | --- |
| `core/` | portable engine, DSP, sample I/O and drawing primitives |
| `platform/3ds/` | console startup, audio, input and screen rendering |
| `assets/` | artwork, original resources and prepared build inputs |
| `tools/` | resource preparation tools |
| `scripts/` | source export and release packaging |
| `licenses/` | third-party license texts |
| `distribution/` | dependency source snapshots and redistribution notes |

## entry points

- module IDs/parameters: `core/engine/unit.h`; DSP: `units.cpp` and `*_impl.inc`;
- sample sources: `core/engine/units.h`, `scratch_unit.h` and `paul_unit.h`;
- sequencing/order/scenes: `core/engine/engine.*` and adjacent implementation files;
- project persistence: `core/engine/patch_io.cpp`;
- console lifecycle: `platform/3ds/main.cpp`;
- editors: `platform/3ds/ui_module_*` and `ui_play_editor.cpp`; shared UI helpers
  include `module_frame_layout.h`, `module_fader.h` and `word_menu.h`.

UI drawing uses frozen snapshots and bounded state, not live engine access.
audio processing must not allocate, access storage/network or wait for UI work.
keep persisted IDs/formats compatible, and distinguish stored parameter bases,
modulation and sequencer locks. module editors share frame/heading/fader helpers;
the performance mixer has its own handle style.

the [guide](../GUIDE.md) explains controls and modules. see [building](../BUILDING.md)
for setup and [asset tools](../tools/README.md) for resource preparation.
