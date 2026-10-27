# contributing

see [building](BUILDING.md), the [source layout](docs/source-layout.md), and
[tests](tests/README.md). keep a change focused and describe what was actually
checked; a successful compile is not device or listening approval.

- preserve project-format compatibility, stable module IDs and user data.
- keep allocation, storage access and blocking work out of real-time audio.
- draw from published snapshots; do not read or mutate the engine while drawing.
- cover gesture release, cancellation, navigation and project replacement when
  changing input. use the shared module frame, headings and palette roles.
- retain editable resource inputs and their original licenses. check generated
  output without regenerating unrelated assets.
- keep tests in `tests/`, current resource generators in `tools/`, and packaging
  in `scripts/`. add required helper files to `scripts/source-files.json`.
- do not commit captures, local receipts, binaries, credentials or private audio.

for a bug, include version, console model, launch format, minimal steps and the
expected/actual result. attach only a small redistributable reproducer. for code
changes, report relevant commands, outcomes and known gaps. publication and
hardware deployment are separate from code review.
