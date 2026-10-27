# source layout

| path | purpose |
| --- | --- |
| `core/` | portable engine, DSP, sample I/O and drawing primitives |
| `platform/3ds/` | console startup, audio, input and screen rendering |
| `assets/` | current artwork, original resources and prepared build inputs |
| `tools/` | generators for the resources actually used by the application |
| `tests/` | optional host checks, fixtures, stubs and test make rules |
| `scripts/` | source export, audits and release packaging |
| `sample_library/starter_cc0/` | optional sample pack; not needed for synthesis |
| `licenses/` | third-party license texts |
| `distribution/` | required dependency sources and redistribution notes |

## entry points

- sound modules: `core/engine/unit.h`, `units.h`, `units.cpp` and `*_impl.inc`;
- sequencing/order/scenes: `core/engine/engine.*` and adjacent implementation files;
- project persistence: `core/engine/patch_io.cpp`;
- console lifecycle: `platform/3ds/main.cpp`;
- editors: `platform/3ds/ui_module_*`; shared UI helpers live beside them.

UI drawing uses frozen snapshots and bounded state, not live engine access.
audio processing must not allocate, access storage/network or wait for UI work.
keep persisted IDs/formats compatible, and distinguish stored parameter bases,
modulation and sequencer locks. module editors share the frame/heading/fader
helpers; the performance mixer intentionally has its own handle style.

normal ARM builds do not depend on `tests/`. explicit host test goals load
`tests/Makefile`. resource generation is also separate from a normal build.
helper source admission is explicit in `scripts/source-files.json`; local
studies, proof receipts, captures, backups and private samples are excluded.
