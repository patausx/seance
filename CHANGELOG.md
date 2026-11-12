# changelog

## current source update — 0.1.0-alpha.1

source changes within the existing alpha version, not a new tagged or binary release.

- 33 active modules: SCRT adds independent scratch/hold with assignable XY axes and
  latch; PAUL adds spectral stretching with freeze and forward/reverse/ping-pong reading.
  PENT32, RULE33 and ROT34 are active; Relic31 is reserved and `UnitKind::Count = 35`
  is the enum extent, not the active-module count.
- relic is temporarily retired after reported performance problems, with its exact
  implementation archived. its persisted31 ID remains reserved; actual/scene-only
  relic projects refuse without changing files or live state. no placeholder or DSP
  redesign. filter/vca/rec/arp stereo foundations and actual-width bounce remain.
- PLAY keeps trim/chop/loop, normal/stretch/tape and global/part controls inline.
  tap/lift splits; hold/outward seam gestures preview a left-ID-preserving merge.
  parts have independent -24…+24 st pitch; audio-edit drags audition until lift.
- the sample browser has library/project/recordings, scrolling full filenames and
  cancel-default WAV deletion distinct from removing a sound from the project.
- GRAN setup adds pitch-sign/forward/reverse/mixed reading and reverse percentage
  for new grains, independently of scan direction.
- a GRAN card you add (picker or REC send) starts at original pitch with three to
  four overlapping grains and timing scatter, instead of an octave up with about
  one grain. existing projects and cards keep their saved knob values; level and
  the mono reading of stereo samples are unchanged.
- a ROT card you add from the picker keeps rotting until you hold or heal it: no
  automatic healing and no frozen final tape. its damage controls start at 75%;
  50% equals the earlier full strength and the upper half goes further. cards in
  existing projects keep the earlier run and sound. a project with such a new
  card is refused by earlier builds.
- projects, names and preferences are written by renaming only onto a vacant name,
  keeping the previous file until the new one is in place: some SD cards refuse a
  rename onto an existing file, which made saving fail.
- **patches** lists only saved projects, seven rows at a time, with open / rename /
  copy / delete per row and inline confirmations. a first save names the project
  and takes a free place by itself; the 128 file places are unchanged.
- note mode has a project scale (tonic and scale); OSC qnt and PLAY tape qnt follow
  it. existing notes are not rewritten.
- unsaved work is snapshotted while transport is stopped and the console is idle.
  if a snapshot is left at startup, seance offers to recover it as an unsaved
  project. it is a fallback, not a save, and holds no unsaved audio.
- sequencer primary pitch holds until the next accepted written note; ordinary
  parameter locks remain step-scoped. existing sparse-note projects can sound
  different under this rule, without rewriting their stored values.
- sequencer: the R mode circle opens over the score with layer icons; holding B
  shows all eight banks in a row with the four tracks' lengths and directions,
  whole-bank copy/paste/clear and the bank's content on the upper display (L/R
  bank, up/down track); the lock target is picked from module/parameter lists;
  a continuous source's row in trig mode shows its triggers, takes a drawn level
  curve and marks the applied value while playing. in lock mode X removes the
  selected lock, and the sequencer reopens on the layer and step you left.
  stored projects are unchanged.
- FRACT's waveform grab squeezes/re-expands a temporary reading window, separately
  from saved damage and hold. sigils' upper view separates actual output, saved
  routes and base/live destination values.
- public source keeps application code, editable resources, required generators
  and packaging helpers, including the three relic generators; local studies,
  tests, captures and receipts stay out. VERSION edits invalidate C++ and ELF.
  release/source packaging and mandatory explicit development preflight remain
  separate from normal compilation; no new version, publication or rights grant.
- development-only `make check` runs the required frame gate then immediate-navigation
  gate on the host, in order and stopping on failure; normal builds/releases stay
  independent of local tests. current build/test docs distinguish quick, scoped and
  historical checks, with a short local hardware-session checklist for console work.

## source alpha overview

- four serial audio chains with six module slots each;
- eight drawn/generated sigils, modulation routing and envelope following;
- built-in instruments, WAV sampling, microphone/REC and export;
- four sequencer tracks, up to 64 steps, eight banks, locks, slices and timing;
- live mixer, A/B parameter scenes, manual bank transitions and 16-card order;
- master/single-chain bounce and a project library (128 places) with explicit saving.

## known limits

Old 3DS is untested. dense patches can exceed the audio budget, and PAUL editor
responsiveness still needs sustained console checks. no new hardware/listening
acceptance is claimed by this source update. back up projects and their referenced
WAVs before updating; DSP firmware is not distributed. current toolchain/runtime
corresponding-source review remains pending; old dependency hashes are historical
evidence, not current release clearance. the menu sounds come from a free beat
pack whose own terms are not reproduced in this source.

see the [guide](GUIDE.md) for controls, [building](BUILDING.md) for build requirements
and [third-party notices](THIRD_PARTY_NOTICES.md) for dependency/resource terms.
