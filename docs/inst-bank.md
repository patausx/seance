# inst — compact embedded multisample core

inst is a new source, not a new play mode and not an oscillator acoustic preset.
its complete provenance, verbatim cc0 licenses, immutable source urls/hashes,
preparation and exact zone metadata are in
[`assets/instruments/`](../assets/instruments/README.md).

## public contract

`InstUnit` is declared in `core/engine/inst_unit.h`, included outside namespace
`seance` by `units.h`. `inst_impl.inc` is included once, outside namespace, by
`units.cpp`. `UnitKind::Inst` is append-only persisted id **28**. the embedding
includes the generated table in that one implementation translation unit only;
other units and ui code must not include the large data header.

seven params, all unipolar q15:

| id | enum | meaning/default |
| --- | --- | --- |
| 0 | `P_INSTRUMENT` | 0 piano; stable ids 1 violin, 2 brass, 3 flute, 4 bass, 5 drums |
| 1 | `P_NOTE` | nearest midi note 24..96; default 60 (c4) |
| 2 | `P_LENGTH` | pitched strike-to-release time, 20..4000 ms; default 480 ms; **ignored on drums** |
| 3 | `P_ATTACK` | 0..1000 ms; default 8 ms |
| 4 | `P_RELEASE` | 5..3000 ms; default 640 ms |
| 5 | `P_TONE` | latched one-pole lowpass brightness; default about 60% |
| 6 | `P_LEVEL` | live output level; default about 75% |

`instrument_index/value/name`, `note_midi/value`, `drum_index/note/name`,
`length_ms/value`, `attack_ms/value`, and `release_ms/value` are the public ui
mappings. negative q15 values clamp to zero. note and family are nearest-index
mappings; time is `low + round((high-low) * (q/32767)^2)`, with exact integer
inverse-ms helpers. all whole millisecond values in those ranges are
representable; callers should format the helpers, not a second guessed formula.

`sequencer_properties()` reports a strike source, pitch `P_NOTE`, level `P_LEVEL`,
duration `P_LENGTH`; no flow param or downstream gate-processor claim. the stable
duration param is meaningful only for pitched families; drums ignore it, including
step locks, and their ui length control is dim/inert. sequencer force-resolved locks
arrive in `Param.target`. target values for family, note, attack, release and tone
(and length for pitched families only) are latched on the **accepted trigger**, not
the old `effective` slew halfway between two instrument IDs. a later edit or control
tick cannot retrigger, seek, replace, or retune a sounding recording.

`P_LEVEL` is the sole live voice control. its resolved target drives one exact
128-engine-sample linear ramp (4 ms at 32 khz), with integer error carry and an
exact final value. changing target midway starts from the actual current value;
repeated unchanged control ticks do not restart the ramp. zero mutes without
freezing voices. reset clears voices/strike history and snaps level to the current
resolved target. no params, sample slots, locks or saved data are rewritten by a
family change, trigger, process or reset.

## mapping and acoustic limits

pitched key zones are midpoint partitions of their measured/declared roots,
with ties going to the lower root; the table covers every note 24..96. explicit
roots/key intervals are generated into `bank.json`. the edge zones are extended
beyond the natural acoustic register, **not** newly sampled pitches. fixed
12-semitone equal-temperament lookup and integer rate conversion run only on a
trigger; sample interpolation is linear and bounded, with no overshoot.

for drums the note selects a **sound**, not transposition. nearest-key mapping
is fixed, and all notes in one interval play the same recording at native rate:

| id | canonical midi | name | accepted interval |
| --- | --- | --- | --- |
| 0 | 36 | kick | 24..37 |
| 1 | 38 | snare | 38..40 |
| 2 | 42 | hat | 41..43 |
| 3 | 45 | tom | 44..45 |
| 4 | 46 | openhat | 46..47 |
| 5 | 49 | cymbal | 48..96 |

`drum_index(int)` also clamps integers below/above that range safely, including
`INT_MIN/MAX`; `drum_note(index)` returns a canonical key for the ui rail. these
are a compact acoustic kit drawn from bass drum/snare/hat/tom/cymbal recordings,
not a full general midi kit or a promise of a particular drum-kit brand.

arp semitones are added to the resolved note once at trigger and the resulting
note clamps to 24..96 before zone selection. there is **no** second arp pitch
multiplier in process. the engine owns the existing arp gain gate. every accepted
arp event produces an inst strike; its legato optimization is explicitly only for
play, not inst. changing only the arp semitone without a trigger does not repitch
an accepted recording or its old tails. inst is not a continuous legato synthesizer.

## voice and envelope policy

four fixed overlapping sample voices, one new strike per trigger; this is tail
polyphony, not an added chord sequencer. use a free slot first, otherwise the
quietest releasing tail; if none releases, steal the oldest voice. a cursor
breaks exact ties deterministically. no random round robin or filesystem lookup
is performed. a steal retains the actual last scalar voice output and applies a
64-sample (2 ms) convex crossfade into the new strike; it does not keep a fifth
sample-playing voice or seek an obsolete descriptor.

- attack progresses to unity over the requested time, with an internal minimum
  of **32 samples / 1 ms** even when the displayed attack is zero.
- for pitched families, release starts at the requested length edge, even if
  attack has not completed, from exactly the envelope reached on that edge. it
  reaches zero after the requested release time; all divisions are calculated
  once at trigger.
- drums never read `P_LENGTH`: their hold edge equals the attack duration **after
  the 32-sample safety floor**, then release closes the tail. there is no sustain
  plateau. finite pcm can end sooner, including during a long attack. this is an
  attack/release one-shot contract, not a hidden length-dependent gate.
- piano, pizzicato bass and percussion are finite recordings. their retained
  natural decay may end before the requested envelope. the last 64 output
  samples receive an additional safety fade so a slowed filter cannot leave a
  discontinuity at the sample end. longer length/release cannot invent sustain.
- violin, trumpet and flute use the prepared 60 ms baked crossfade loops while
  sounding, including during envelope release. loop interpolation uses a prepared
  adjacent-source guard, and bounds need at most one wrap subtraction per frame.
- source preprocessing removes dc and applies start/finite-tail fades. a maximum
  source advance below 8 frames and loops longer than 14000 frames are fixed-bank
  invariants. descriptor/pcm bounds and guard semantics are checked by the suite.
- fixed **12 db polyphony headroom** allows all four normalized voices to sum
  without overflow or clipping. this is deliberately not a hidden compressor,
  limiter or automatic gain normalizer. tone is a latched, stable q15 one-pole
  coefficient; maximum tone is transparent. no per-sample float, division/modulo,
  exp, allocation, locks, random source or bank search exists in inst process.

as for the existing sources, engine bypass skips `process()`: it does not become
an internal dry/wet effect or silently advance hidden voices. transport/chain
bypass/mute/arp gate policy stays with the engine. reset retires all runtime
voices immediately; those voices, their ages/loop positions and strike counts
are not persisted. parent integration owns insertion, snapshots, save/load,
sequencer routing and ui lifecycle tests.

## frozen telemetry and memory

`Stage : uint8_t { Off, Attack, Sustain, Release }` and `Visual` expose only
accepted voices. each `VoiceVisual` contains completed engine-sample age, own
0..32767 envelope, latched instrument and actual accepted midi note, and stage.
`Visual.active` is a **count** (0..4), not a mask; `strikes` counts accepted
triggers since reset. the scalar steal de-click bridge is not a fictitious fifth
voice. an off slot is cleared. capture under the existing audio guard, exactly
like other unit visuals. repeated `visual()` calls do not advance state or read
pcm, allocate, draw or consult master output.

compile-time bounds: `sizeof(Visual) <= 64`, `sizeof(InstUnit) <= 1536`. measured
host sizes in the focused build: **56 bytes visual / 896 bytes inst unit**, including
the inherited param bank. shared pcm is **1,221,500 bytes**, plus 440 descriptor
bytes and small mapping constants;
there is no constructor buffer or per-unit copy of the bank. immutable data stays
outside `trackr::synth::SampleBank`, so importing/removing/replacing a play sample
cannot alter inst. bank preparation's raw sources are not embedded or loaded at
runtime. 24 khz mono and short looping recordings are intentional quality/size
limits, and extreme pitch shifts can sound artificial or alias.

## focused proof

```sh
python3 tools/test_inst_core.py --sanitize
```

this one runner checks pinned licensing/source hashes and pitch metadata, rebuilds
the generated bank in memory for exact reproducibility, then compiles the actual
`units.cpp` + `sampler.cpp` with a small dedicated executable. included-source
hashes are guarded across compile/run so concurrent parent edits cannot produce
a stale-current claim. no synthetic substitute inst implementation is linked.

coverage: all q15 mappings and inverse times; 22 descriptors and every one of
438 family/key combinations; independent scalar pcm oracle at roots and both zone
edges; loop guards, finite end and pitched release during attack; latched edits/locks;
all six canonical drums at minimum/maximum length with every attack/release endpoint
combination and defaults, exact pcm/telemetry equality, transition/end edges and
sentinel output bounds;
arp once/clamps; exact live level ramp including retargets/zero; 64-sample steal
continuity; four-voice bound, repeated stealing/reset; arbitrary block partition
identity and frozen visuals; guarded construction/trigger/process/control/visual
heap calls; independence from a real mutable sample bank and sixteen inst units.

### initial baseline — superseded for the drum contract

**the first baseline focused compile/run passed** with address/undefined/leak
sanitizers enabled. exact deterministic bank rebuild passed; actual source hashes
were stable through compile/run. its six groups did **not** test the required drum
length-inert contract, and this earlier result is not final drum proof. retained
baseline measurements:

- 5,144,814 processed frames; 2,866,124 independent scalar pcm comparisons.
- 3,145 frozen-state comparisons, plus scalar stage/envelope checks.
- zero guarded heap calls, including construction of sixteen independent units.
- all 438 family/key combinations produced nonzero pcm; this is a signal check,
  not a claim of listening approval or acoustic realism at extreme transpositions.
- existing `sampler.cpp` emitted its pre-existing unused `acc` warning; the new
  inst code emitted no warnings. no production change was made to silence it.
- captured output: `/tmp/seance-inst-core.log`; preparation log:
  `/tmp/seance-inst-bank-build.log`.

### final drum contract correction

runtime now ignores drum length entirely and starts release at the floored attack
edge; pitched behavior, sample data, public api and memory sizes are unchanged.
the independent scalar oracle follows that family-specific envelope, and the added
length-invariance group covers six canonical keys × four attack/release endpoint
combinations plus the default shape (30 paired cases).

**final drum proof: pass**, on the one focused sanitizer compile/run after this
correction: `python3 tools/test_inst_core.py --sanitize`. all seven reported groups
passed with address/undefined/leak sanitizers and stable compiled-source hashes:

- 504,766 paired minimum/maximum length pcm frames were byte-identical across the
  30 cases, with independent scalar comparison of both streams, exact attack /
  release / finite-end telemetry, sentinel bounds and no guarded heap calls.
- full focused run: 6,152,906 processed frames; 3,874,216 independent scalar pcm
  comparisons; 5,179 frozen-state comparisons; zero guarded heap calls.
- deterministic asset rebuild passed unchanged: pcm **1,221,500 bytes**, unit
  **896 bytes**, visual **56 bytes**; no public fields/signatures or assets changed.
- final output: `/tmp/seance-inst-core-final.log`. the earlier
  `/tmp/seance-inst-core.log` was preserved byte-for-byte (hash checked), remains
  explicitly the pre-fix baseline and is not the final drum-contract proof.
- only the same historical `sampler.cpp` unused `acc` warning appeared; new inst
  code is warning-clean. no runtime/test changes followed this final pass.

no full-app, broad regression, arm build, new media, deployment, hardware listening
or hardware timing is part of this core proof. the mandatory project navigation
check is requested by the final documentation edit separately; it is not counted
as one of the inst proof groups.
