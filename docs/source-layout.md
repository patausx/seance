# source layout

| path | purpose |
| --- | --- |
| `core/` | portable engine, DSP, sample I/O and drawing primitives |
| `platform/3ds/` | console startup, audio, input and screen rendering |
| `assets/` | current artwork, original resources and prepared build inputs |
| `tools/` | generators for the resources actually used by the application |
| `scripts/` | source export, audits and release packaging |
| `licenses/` | third-party license texts |
| `distribution/` | required dependency sources and redistribution notes |

## entry points

- module IDs/parameters: `core/engine/unit.h`; DSP: `units.cpp` and `*_impl.inc`;
- sample sources: `core/engine/units.h`, `scratch_unit.h` and `paul_unit.h`;
- sequencing/order/scenes: `core/engine/engine.*` and adjacent implementation files;
- project persistence: `core/engine/patch_io.cpp`;
- console lifecycle: `platform/3ds/main.cpp`;
- editors: `platform/3ds/ui_module_*` and `ui_play_editor.cpp`; shared UI helpers
  live beside them, including `module_frame_layout.h`, `module_fader.h` and `word_menu.h`.

UI drawing uses frozen snapshots and bounded state, not live engine access.
audio processing must not allocate, access storage/network or wait for UI work.
keep persisted IDs/formats compatible, and distinguish stored parameter bases,
modulation and sequencer locks. module editors share the frame/heading/fader
helpers; the performance mixer intentionally has its own handle style.

normal ARM builds use the checked-in prepared resources. resource generation
is separate from a normal build.
helper source admission is explicit in `scripts/source-files.json`; local
tests, studies, proof receipts, captures, backups and private samples are excluded.
the public [guide](../GUIDE.md) is the control/module reference; omitted local runtime
contracts are not dependencies of this source tree. [building](../BUILDING.md)
covers the build and read-only export audit.
