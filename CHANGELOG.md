# changelog

## current source update — 0.1.0-alpha.1

source changes within the existing alpha version, not a new tagged or binary release.

- 30 modules: SCRT adds independent scratch/hold with assignable XY axes and latch;
  PAUL adds spectral stretching with freeze and forward/reverse/ping-pong reading.
- PLAY keeps trim/chop/loop, normal/stretch/tape and global/part controls inline.
  tap/lift splits; hold/outward seam gestures preview a left-ID-preserving merge.
  parts have independent -24…+24 st pitch; audio-edit drags audition until lift.
- the sample browser has library/project/recordings, scrolling full filenames and
  cancel-default WAV deletion distinct from removing a sound from the project.
- GRAN setup adds pitch-sign/forward/reverse/mixed reading and reverse percentage
  for new grains, independently of scan direction.
- sequencer primary pitch holds until the next accepted written note; ordinary
  parameter locks remain step-scoped. existing sparse-note projects can sound
  different under this rule, without rewriting their stored values.
- FRACT's waveform grab squeezes/re-expands a temporary reading window, separately
  from saved damage and hold. sigils' upper view separates actual output, saved
  routes and base/live destination values.
- public source keeps application code, editable resources, required generators
  and packaging helpers; local studies, tests, captures and receipts stay out.

## source alpha overview

- four serial audio chains with six module slots each;
- eight drawn/generated sigils, modulation routing and envelope following;
- built-in instruments, WAV sampling, microphone/REC and export;
- four sequencer tracks, up to 64 steps, eight banks, locks, slices and timing;
- live mixer, A/B parameter scenes, manual bank transitions and 16-card order;
- master/single-chain bounce and a 128-slot project library with explicit saving.

## known limits

Old 3DS is untested. dense patches can exceed the audio budget, and PAUL editor
responsiveness still needs sustained console checks. no new hardware/listening
acceptance is claimed by this source update. back up projects and their referenced
WAVs before updating; DSP firmware is not distributed.

see the [guide](GUIDE.md) for controls, [building](BUILDING.md) for build requirements
and [third-party notices](THIRD_PARTY_NOTICES.md) for dependency/resource terms.
