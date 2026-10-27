# contributing

seance is Nintendo 3DS homebrew written in C/C++. read [README.md](README.md),
[GUIDE.md](GUIDE.md), [BUILDING.md](BUILDING.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before changing code or assets.
use the maintainer's confirmed repository when one is available. clean export
and the private-first review workflow are described in
[repository preparation](distribution/repository.md).

## scope and licensing

- keep a change focused; explain the behavior before and after, affected module,
  and how to reproduce it. do not mix UI layout, DSP, file-format changes, and
  unrelated cleanup without a reason.
- application code contributions are under **GPL-3.0-or-later**. only contribute
  material you can license. preserve third-party notices; font data and audio
  have their own terms.
- disclose directly incorporated LLM-generated code, documentation, art, or
  music. do not claim machine-generated work is solely human-authored. upstream
  projects may have stricter contribution policies; Universal-DB currently does.
- never add private recordings, commercial sample packs, console dumps,
  credentials, personal network addresses, or another person's projects. new
  assets need a source URL or retained provenance, license text, and rights review.

## build locally

use a current devkitPro/devkitARM installation with libctru, citro2d, and citro3d.
see [BUILDING.md](BUILDING.md) for environment/tool requirements and the exact
release-preparation commands. ordinary compilation uses checked-in generated
headers and prepared audio; it must not silently download or regenerate assets.

```sh
make -j2
```

CIA packaging additionally needs devkitPro's `smdhtool` and `makerom`; the
current build does not use `bannertool`. setting up a compiler or packaging
a CIA is not a hardware test.
never run upload/deployment targets as part of a contribution check.

## checks and evidence

these focused host checks compile/render current code and are the documented
starting point, not a claim that every historical test is portable:

```sh
make test-module-frame
python3 tools/test_nav_instant.py
```

check each script's help/source for its dependencies before running it. some
checks generate ignored render/build outputs. include the actual commands,
tool versions, exit status, and output summary with a change. use the additional
focused tests relevant to the changed behavior, plus a 3DS build for production
code. use sanitizer builds where supported. do not modify a test baseline just
to make a failure disappear.

some historical regression scripts refer to retained render baselines, device
captures, or other files that are not included in the public source export. a
missing historical fixture is a **blocked check**, not a passing check and not
proof of a runtime regression. for example, older sequence-hit comparisons use
a `ui_mockups/playhead_press_runtime/core/baseline/manifest.json` fixture. record
that limitation explicitly; prefer a fresh, self-contained test for new work.

## audio, UI, and persistence changes

- avoid allocation, filesystem/network access, or blocking operations on the
  real-time audio path. preserve clear ownership between UI and audio state.
- cover parameter boundaries, variant-specific discrete values, cancellation,
  undo, recording guards, and existing patch-file compatibility as applicable.
- write documentation, comments and user-facing prose in English. preserve
  deliberate multilingual artwork, Unicode filename coverage and third-party
  names/notices; these are not a prose translation target.
- keep both screens within native bounds; inspect the relevant palettes and
  input paths. host rendering alone does not establish PICA performance or
  physical stylus/controller behavior.
- record which console model, launch method, firmware/homebrew environment,
  test patch, and duration were actually tried. distinguish listening, device,
  host, sanitizer, and compile-only results. never infer Old/New 3DS equivalence,
  frame rate, or glitch-free audio from a successful host run.
- use the shared module frame, headings, palette roles and lancet faders; the
  performance mixer's handle style is intentionally separate. see
  [source layout and conventions](docs/source-layout.md).
- retain editable asset inputs and deterministic preparation scripts when
  changing generated data. a generated header alone is not a substitute for
  corresponding source.

## reports and review

for a bug report include release version/hash, console and launch method,
minimal steps, expected/actual behavior, and a small shareable reproducer if
possible. provide no private samples. for a patch include focused test results,
known gaps, any format migration, and asset/license provenance.

review and merge do not authorize a release, network deployment, or publication.
maintainers must separately clear [the publication checklist](distribution/publication-checklist.md).
