# tools

run commands from the repository root. [building](../BUILDING.md) covers the
compiler and Python requirements; [source layout](../docs/source-layout.md)
explains the main code directories.

## start here

| command | purpose |
| --- | --- |
| `make test-module-frame` | shared module geometry, source admission and bounded captions |
| `python3 tools/test_nav_instant.py` | navigation/input ownership source checks |
| `python3 tools/test_package_release.py` | source allowlist, export and archive regression tests |
| `python3 tools/test_audit_public_source.py` | regression tests for the text/path/link audit |
| `python3 tools/audit_public_source.py` | English prose, selected privacy patterns and documentation links in the exported source |
| `make test-branding` | launcher icons, wordmark and editable source coverage |
| `python3 tools/render_readme_wordmark.py --check` | README GIF pixels/timing, whole-mark softening and 1.5x playback from production geometry |

these are focused checks, not a full regression suite or real-device acceptance.
some tests compile host fixtures or write ignored output; read a runner before
using it. test names alone do not establish scope or current compatibility.

## modules and UI

- `test_*_core.py` runners exercise portable engine behavior for their named area.
- `test_*_ui.py` runners build native host fixtures for the named editor/workflow.
- `test_perform_order_core.py` and `test_perform_order_ui.py` cover order cards;
  `test_picker_scroll.py` covers the picker scroll model.
- `test_module_frame.py --capture` separately produces scoped native captures
  for module UI work. the quick geometry gate does not generate them.
- shared fixtures live beside their runners: `sequencer_test.cpp`,
  `osc_input_dispatch_test.cpp`, `menu_stage1_test.cpp`, `*_cases.inc`,
  `*_test.h` and small stub directories.

files deliberately stay at their working paths: runners, includes and Makefile
rules rely on those relative locations. do not move scripts into new folders
without updating their imports and consumers together.

## assets and packaging

- `generate_*` scripts prepare fonts, title masks, branding and other checked-in
  data. use a generator's `--check` option where supported. regeneration is a
  source change, not an installation step.
- `prepare_inst_bank.py` and `prepare_menu_audio.py` prepare the documented audio
  inputs; asset READMEs describe their tools, provenance and licenses.
- `prepare_public_release.py --source-only --output <new-directory>` writes an
  allowlisted source copy without building or networking.
- without `--source-only`, that preparer builds in the new copy and packages it.
  `package_release.py` only packages existing inputs; it does not compile them.

see [repository preparation](../distribution/repository.md) before making a
private repository or publishing downloads.

## studies, captures and historical helpers

`render_*`, `capture_*`, `verify_*` and some older tests are retained development
helpers. a renderer may be an authored design study, a wrapper around an actual
native host raster, or an editorial composition; it is not necessarily a device
capture. some need excluded local artwork, receipts, binaries or archived
baselines and will not run from the source copy alone. archived inputs use the
optional `.local-state/` directory, which is excluded from source exports; normal
builds and the documented quick checks do not need it.

a missing old baseline is a blocked check, not a pass. do not recreate an old
receipt with new source hashes or regenerate historical media just to satisfy a
helper. new verification belongs in a new output directory. keep historical
results separate from current host, ARM and hardware results.
