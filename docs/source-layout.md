# source layout

start with the [user guide](../GUIDE.md) for the instrument, or
[building](../BUILDING.md) to compile it.

## directories

| path | responsibility |
| --- | --- |
| `core/audio/` | fixed-point audio types and helpers |
| `core/dsp/` | shared signal-processing utilities |
| `core/engine/` | modules, sequencing, bank order, scenes and project persistence |
| `core/synth/` | sample I/O and synthesis support |
| `core/ui/` | shared drawing primitives and bitmap fonts |
| `platform/3ds/` | console startup, audio output, input, views and screen rendering |
| `assets/` | editable asset sources, generated inputs, provenance and licenses |
| `sample_library/starter_cc0/` | optional redistributable sample pack |
| `tools/` | host tests, generators, capture helpers and offline packaging |
| `licenses/` | third-party license texts |
| `distribution/` | source dependencies, repository preparation and release checklist |

## finding a change

- **sound or a parameter:** start with `core/engine/unit.h`, `units.h` and
  `units.cpp`; larger modules have dedicated `*_impl.inc` files.
- **sequencing and bank order:** `core/engine/engine.*` and the associated
  implementation files. UI entry points are `ui_sequencer.cpp`, `ui_order.cpp`
  and `ui_transition.cpp` in `platform/3ds/`.
- **project loading/saving:** `core/engine/patch_io.cpp`. samples are referenced
  by path; project files are not self-contained sample archives.
- **a module editor:** the `ui_module_*` files. shared frame, heading and fader
  helpers live beside them.
- **startup or view navigation:** `platform/3ds/main.cpp`, `ui_nav.*` and the
  startup menu files.
- **host checks or asset regeneration:** start with the [tools guide](../tools/README.md)
  and the relevant Makefile target. do not regenerate assets as routine setup.

## ownership and conventions

UI rendering consumes published snapshots and bounded UI state. keep live engine
access and audio guards out of draw functions. advance gesture/animation state
in input or tick code, not while drawing. the real-time audio path must not
allocate, access storage/network or wait on UI work.

persisted module IDs and project formats need explicit compatibility handling.
keep parameter bases, current modulation and sequencer locks distinct. cover
release, cancel, navigation and project replacement for held gestures.

new module editors use `module_frame_layout.h`, `editor_style` and
`module_heading::header(kind, color)`. title origin is `(14, 8)`, small header
text uses y14, footer text y228, and title masks fit 57×25. keep the shared frame
rather than enlarging it for a new name. real parameter sliders use
`module_fader`'s lancet renderer; the existing performance mixer intentionally
keeps its separate handle style. use palette roles instead of fixed theme colors.

extend the picker metadata, generated title data and actual native fixture when
adding a module; do not hide it from coverage. `make test-module-frame` is the
quick gate. changed module UI also needs a scoped native capture through
`python3 tools/test_module_frame.py --capture` and review of both screens.

## public source and local material

the repository candidate contains current source, selected English documentation,
asset originals and build inputs. the development workspace also retains older
design notes, test receipts, captures and device backups. these are not a public
source dependency and are not copied wholesale into the candidate.

some historical tools need those excluded baselines or design inputs; this is
not a promise that every old renderer works in a clean checkout. the documented
quick checks are the starting point. record missing prerequisites as blocked,
not passed. see [contributing](../CONTRIBUTING.md) for reporting and testing scope.
