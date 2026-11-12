# contributing

see [building](BUILDING.md) and the [source layout](docs/source-layout.md).
keep a change focused and describe what was actually
checked; a successful compile is not device or listening approval.

- preserve project-format compatibility, stable module IDs and user data.
- keep allocation, storage access and blocking work out of real-time audio.
- draw from published snapshots; do not read or mutate the engine while drawing.
- cover gesture release, cancellation, navigation and project replacement when
  changing input. use the shared module frame, headings, palette roles and word
  menus; distinguish edit/base values from actual live values. parameter sliders
  use the shared lancet renderer; the performance mixer keeps its separate style.
- retain editable resource inputs and their original licenses. check generated
  output without regenerating unrelated assets.
- keep current resource generators in `tools/` and packaging in `scripts/`.
  add required helper files to sorted `scripts/source-files.json`, including every
  production generator named by generated headers. keep independent generator-to-
  helper closure regression coverage; a local test helper is not automatically public.
  the public source export
  excludes local tests and research; do not link public guides to omitted files.
- development admission requires explicit `make test-module-frame` and
  `python3 -B tests/test_nav_instant.py`, plus scoped checks for the change. these
  local-only suites are not automatic normal/public make or release dependencies.
- do not commit captures, local receipts, binaries, credentials or private audio.

for a bug, include version/source revision, console model, launch format, minimal steps and the
expected/actual result. attach only a small redistributable reproducer. for code
changes, report relevant commands, outcomes and known gaps. run the read-only
public-source audit from [building](BUILDING.md#resources-and-release-packages)
when changing exported files. publication and hardware deployment are separate
from code review.
