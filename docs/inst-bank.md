# inst — sampled-bank developer contract

This describes the six sampled families of `InstUnit`. The full unit also has
modeled `vibes` and additional controls; see [GUIDE](../GUIDE.md#inst) for the full
interface. Resources, credits and rebuild commands: [instrument bank](../assets/instruments/README.md).

## API and parameters

`InstUnit` is declared in `core/engine/inst_unit.h`, included outside namespace
`seance` by `units.h`. `inst_impl.inc` is included once, outside namespace, by
`units.cpp`. `UnitKind::Inst` is persisted ID **28**. Include the large generated
bank header only in that implementation translation unit, not in UI or other units.

These seven parameter IDs form the sampled-bank subset of the unit's twelve
parameters. All use unipolar Q15:

| id | enum | sampled-bank meaning |
| --- | --- | --- |
| 0 | `P_INSTRUMENT` | 0 piano; 1 violin; 2 brass; 3 flute; 4 bass; 5 drums |
| 1 | `P_NOTE` | nearest MIDI note 24..96 |
| 2 | `P_LENGTH` | pitched strike-to-release time, 20..4000 ms; ignored on drums |
| 3 | `P_ATTACK` | 0..1000 ms, with an internal 1 ms floor |
| 4 | `P_RELEASE` | 5..3000 ms |
| 5 | `P_TONE` | latched one-pole lowpass brightness |
| 6 | `P_LEVEL` | live output level |

`instrument_index/value/name` cover the full seven-family selector (ID 6 is modeled
vibes); use them rather than a six-bin Q15 conversion. This mapping requires
SNCG/version 16 or newer; older INST projects are refused rather than remapped.
`note_midi/value`, `drum_index/note/name`, `length_ms/value`, `attack_ms/value` and
`release_ms/value` provide the note, sound and time mappings. Negative Q15 values
clamp to zero. Time is `low + round((high-low) * (q/32767)^2)`; use the inverse-ms
helpers and format their results. Every whole millisecond in each range is representable.

For sampled families, `sequencer_properties()` reports a strike source with pitch
`P_NOTE`, level `P_LEVEL`, duration `P_LENGTH` and no flow parameter or gate processor.
Drums ignore length, including step locks. Resolved `Param.target` values for family,
note, attack, release, tone and pitched length latch on the accepted trigger, not
on the preceding `effective` slew. Later edits/ticks never retrigger, seek, replace
or retune sounding tails.

`P_LEVEL` is the only live sampled-voice control. Its target drives an exact
128-engine-sample linear ramp (4 ms at 32 kHz), with integer error carry. A mid-ramp
edit starts from the current value; unchanged ticks do not restart it. Zero mutes
without freezing voices. Reset snaps level to the current resolved target.

## notes and drums

Pitched zones partition MIDI 24..96 at root midpoints, with ties going to the lower
root. `bank.json` stores the numeric roots and key intervals. Edge zones extend
through transposition, not additional recordings. Equal-temperament rate conversion
runs at trigger; sample interpolation is linear and bounded.

For drums, note selects a sound at native rate, not transposition:

| id | canonical MIDI | name | accepted interval |
| --- | --- | --- | --- |
| 0 | 36 | kick | 24..37 |
| 1 | 38 | snare | 38..40 |
| 2 | 42 | hat | 41..43 |
| 3 | 45 | tom | 44..45 |
| 4 | 46 | openhat | 46..47 |
| 5 | 49 | cymbal | 48..96 |

`drum_index(int)` clamps extremes, including `INT_MIN/MAX`; `drum_note(index)`
returns the canonical key. This is a compact kit, not a full General MIDI kit.

Arp semitones are added once at trigger, then clamped to 24..96 before zone
selection. Every accepted arp event strikes INST; changing arp pitch without a
trigger does not retune existing tails. The engine owns the arp gain gate.

## voices and envelopes

Four fixed overlapping sample voices, one strike per trigger. Use a free slot,
then the releasing tail with the lowest envelope, otherwise the oldest voice;
a rotating cursor breaks exact ties. A steal uses a 64-sample (2 ms) convex
crossfade from the actual last scalar output, without a fifth sample-playing voice.
Changing families retains existing tails.

- Attack reaches unity over the requested time, with a minimum of 32 engine samples
  (1 ms), even when the displayed attack is zero.
- Pitched release starts at the length edge from the envelope reached there, even
  during an incomplete attack, and reaches zero after the requested release time.
- Drums never read `P_LENGTH`: release starts immediately after the attack and its
  safety floor, with no sustain plateau. Finite PCM can end during a long attack.
- Piano, generated bass and drums are finite. Their decay can end before the
  envelope; the last 64 output samples receive a safety fade. Longer length/release
  does not add sustain.
- Violin, trumpet and flute use prepared 60 ms crossfade loops, including during
  release. A loop's interpolation guard matches its loop-start sample. Source advance
  stays below eight frames and loops exceed 14,000 frames, so one wrap subtraction suffices.
- Four sample voices share 12 dB polyphony headroom, followed by the full unit's
  additional 6 dB reserve for overlapping sample/model tails. Tone is a stable
  latched Q15 coefficient; maximum tone is transparent. Rates and envelope divisions
  are computed at trigger; the sample loop uses fixed-point math without heap,
  file I/O, locks or bank searches.

Engine bypass skips `process()` and does not advance hidden voices. Transport,
mute and arp gating remain engine policy. Reset retires runtime voices and strike
history without rewriting parameters, sample slots, locks or saved data. Voice ages
and loop positions are not persisted.

## visuals and storage

`Stage` is `Off / Attack / Sustain / Release`. Each `VoiceVisual` exposes completed
engine-sample age, its 0..32767 envelope, latched family, accepted MIDI note and stage.
`Visual.active` counts sampled voices (0..4), not a mask; `strikes` counts all
accepted INST triggers since reset, including modeled strikes. Capture `visual()`
under the audio guard; it is read-only. Capture `model_visual()` separately when
showing simultaneous sample/model tails.

Compile-time bounds are `sizeof(Visual) <= 64` and `sizeof(InstUnit) <= 1600`.
The bank is shared, with no constructor buffer or per-unit PCM copy, and stays
outside `trackr::synth::SampleBank`: PLAY sample edits cannot alter INST. The 24 kHz
mono bank and short decays/loops limit fidelity; extreme pitch shifts can alias.

## checks

From the repository root:

```sh
python3 -B tools/prepare_inst_bank.py --check
```

Checks the rebuilt PCM, descriptors and metadata against the checked-in bank.
Use `--verify` to check original files and existing outputs without resampling.
