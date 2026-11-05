# inst — embedded acoustic source bank

this is a small, edited **recording library**, not oscillator approximations or a
soundfont loader. it supplies one source with stable families `piano / violin /
brass / flute / bass / drums`. only the generated const pcm/zone table is linked
into the program; the source wavs do not go on the sd card.

## provenance and license

all 22 archived recordings are distributed under **cc0 1.0 universal** by their
upstream projects. verbatim licenses and relevant readmes are retained in
[`licenses/`](licenses/). this is not an invented license for unrelated sounds.

- **vs chamber orchestra 2: community edition**, versilian studios / sam gossner
  and simon dalzell; sample cutting credited upstream to elan hickler / soundemote.
  <https://github.com/sgossner/VSCO-2-CE>, pinned commit
  `440300901dfe9275fd84e0b7763af1f8443ae62e`.
- the upright piano is explicitly credited by its `Info.txt` to **simon dalzell,
  ivy audio**, winter 2015–16, with redistribution permission from versilian
  studios. its original credits and mapping chart are preserved.
- **versilian community sample library**, versilian studios llc.
  <https://github.com/sgossner/VCSL>, pinned commit
  `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e`. used for six acoustic percussion hits,
  not synth drums renamed as acoustic recordings.
- authoritative vsco pitch metadata is pinned separately from its sfz branch at
  `6dd651d55dde97fd4028699be9d4481f26917891`. those sfzs are retained as provenance;
  they are **not** a runtime dependency or a claim of full sfz compatibility.

[`sources.json`](sources.json) records each exact upstream path, immutable download
url, local filename, byte count, git blob hash and sha256, including all licenses
and pitch-reference files. only those individual files were downloaded, not
whole repositories. raw wav sources total **43,449,106 bytes**. no starter library,
user recording, commercial sample pack or external mutable sample slot is used.

vsco's original readme additionally *asks* not to sell the raw samples directly
and encourages attribution and public improvements. that original text is
preserved alongside its cc0 license, not silently discarded. this derivative
and its preparation are documented openly with the requested credits.

## prepared bank

- **1,221,500 bytes** of shared mono signed pcm16 at **24,000 hz** (about 1.165 mib).
- 610,728 playable frames plus 22 single-frame interpolation guards.
- 22 zones: 19 recorded zones (4 piano, 3 each violin/trumpet/flute, 6 drums) plus 3 generated clean bass plucks.
- 440 bytes of fixed zone descriptors, plus tiny family-range tables.
- pcm sha256: `3890f6e948434dcbbef1f7b11fcb890f01735a784d6cdb2aed1db52c2291eb7f`.
- embedded header sha256:
  `453bb02ac62f546cb8ff69df30d0e39072b270baab2ef3af0cab9b511b28637f`.

[`bank.json`](bank.json) contains exact per-zone source starts, frame counts,
root/key zones, loop start/end, crossfade length, pcm hashes, dc/peak measurements
and generator/filter/source-manifest hashes. [`bank.pcm`](bank.pcm) is the exact
little-endian concatenation represented by `core/engine/inst_bank_data.h`.

| family | original articulation | roots (midi) | playback |
| --- | --- | --- | --- |
| piano | upright piano, dynamic layer 2 | 33, 49, 65, 81 | finite recorded decay, retained 2.7 / 2.4 / 1.9 / 1.5 s |
| violin | solo arco vibrato, forte | 55, 67, 79 | ~1.05 s attack/body, prepared sustained loop |
| brass | trumpet sustain, velocity layer 3 | 53, 67, 81 | ~0.9 s attack/body, prepared sustained loop |
| flute | non-vibrato sustain, medium/strong layer | 60, 72, 84 | ~0.9 s attack/body, prepared sustained loop |
| bass | clean generated pluck; no recorded room/noise | 36, 45, 59 | finite 1.5 / 1.5 / 1.3 s, same key zones |
| drums | bass drum, snare on, closed hat, stick tom, open hat, stick cymbal | 36, 38, 42, 45, 46, 49 | six unpitched finite hits |

roots come from actual upstream `pitch_keycenter`, **not a universal interpretation
of octave names**: this library calls flute/trumpet/bass middle c `c3`, but violin
middle c `c4`. the piano uses numbered filenames and its own mapping chart. the
preparation script validates all 16 pitched selections against the pinned sfzs.

## bass-only sound replacement (2026-10-30)

only the three bass PCM regions were replaced to avoid recorded background noise.
they are newly generated plucks, not denoised VSCO recordings or a claim of realistic
contrabass emulation. original bass WAVs/pins/licenses remain archived unchanged.
all other 19 zone bytes, all descriptors, roots/IDs and runtime controls stay exact.

preparation generates a fundamental plus two faster-decaying harmonics (q15 gains
32767/8500/2600), a 4ms smooth onset and 90ms final safety fade. fundamental T60 is
1400/1200/1000ms for roots36/45/59. fixed-precision Decimal builds Q30 coefficients;
integer recurrence produces the final mono24khz PCM offline. no recorded background,
random noise, new runtime synthesis, new dependency or per-voice state is introduced.
`bank.json` explicitly marks each bass source as archived reference only and records
the generation recipe. the generated replacement PCM is distributed under CC0-1.0.

## deterministic preparation

from the repository root, with ordinary python 3 (no numpy, scipy, ffmpeg, paid
service or network needed when pinned source files are present):

```sh
python3 tools/prepare_inst_bank.py          # rebuild bank.pcm, bank.json, header
python3 tools/prepare_inst_bank.py --check  # rebuild in memory; exact byte comparison
python3 tools/prepare_inst_bank.py --verify # pinned source/license/root + output hashes
```

`--fetch` is explicit opt-in restoration of *missing* originals from their pinned
urls. it checks bytes/sha256/git blob and never updates a pin or replaces a
mismatch. an ordinary build, check, and application startup do not download.

1. decode the selected stereo 44.1 khz pcm16/24 recording without reducing its
   precision; arithmetic-average channels. retain 2 ms before the first relative
   −42 db onset threshold crossing.
2. resample with a 64-tap hann-windowed sinc, 80 exact rational phases, 10.8 khz
   cutoff, integer q30 coefficients/accumulation. coefficients are calculated with
   fixed-precision decimal sine, not platform-dependent libm or a changing codec.
3. for sustained zones, search a bounded 15 ms endpoint interval, then bake a
   60 ms smoothstep crossfade against the real source immediately preceding the
   loop start. the final sample equals `source[loop_start-1]`; the interpolation
   guard equals `source[loop_start]`. wrapping follows an ordinary adjacent
   recorded pair rather than forcing a zero seam or crossfading per audio sample.
4. remove dc (cycle mean for sustained recordings), add a 1 ms start safety fade,
   and a 60 ms retained-tail fade for finite recordings; normalize peak to 24576.
   these 19 recorded zones are unchanged; no synthetic attack or sustain is added to them.
5. write little-endian pcm, exact descriptors, hashes and the const c++ table.

there is one velocity layer / round robin per zone. shortening the recorded piano/drum
recordings preserves their **retained natural decay**, not their complete original
multi-second room/release tails. the sustain loops repeat about half a second of
real performance; this is deliberately compact, not a concert-library claim.
mono conversion and resampling reduce bandwidth. high/low transpositions outside
an instrument's natural range change its character; audio-rate playback uses
bounded linear interpolation, not oversampling or a formant-preserving pitch
shifter. no hardware listening or performance quality is established by hashes.

runtime behavior, tests and limits: [`../../docs/inst-bank.md`](../../docs/inst-bank.md).
