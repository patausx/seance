# inst — embedded instrument bank

Six sampled-bank families: `piano / violin / brass / flute / bass / drums`.
The bank has **19 recorded zones plus 3 generated clean bass plucks**. Bass is
generated PCM, not denoised VSCO recordings; the original bass WAVs remain references.
The shared bank is mono signed PCM16 at **24,000 Hz**, **1,221,500 bytes**, with one
interpolation guard per zone. Source WAVs are not needed on the SD card.

## families

| id | family | articulation | roots (MIDI) | playback |
| --- | --- | --- | --- | --- |
| 0 | piano | upright, dynamic layer 2 | 33, 49, 65, 81 | finite decay |
| 1 | violin | solo arco vibrato, forte | 55, 67, 79 | sustained loop |
| 2 | brass | trumpet sustain, velocity layer 3 | 53, 67, 81 | sustained loop |
| 3 | flute | non-vibrato sustain, medium/strong layer | 60, 72, 84 | sustained loop |
| 4 | bass | generated clean pluck | 36, 45, 59 | finite decay |
| 5 | drums | kick, snare, hat, tom, openhat, cymbal | 36, 38, 42, 45, 46, 49 | unpitched one-shots |

Each zone has one layer/take. Use numeric MIDI roots: upstream octave-name
conventions differ. Short mono decays/loops and linear interpolation keep the
bank compact; extreme transpositions change its character and can alias.

## files

- [sources.json](sources.json): original files, download URLs and source metadata.
- [bank.json](bank.json): zone bounds, roots, loops and the generated bass recipe.
- [bank.pcm](bank.pcm): little-endian PCM represented by `core/engine/inst_bank_data.h`.
- [licenses/](licenses/): original licenses, credits and pitch-reference mappings.

## rebuilding and checking

From the repository root, with Python 3 and the source files listed in `sources.json`;
no extra Python packages are required:

```sh
python3 -B tools/prepare_inst_bank.py          # rebuild PCM, metadata and header
python3 -B tools/prepare_inst_bank.py --check  # compare rebuilt outputs without writing
python3 -B tools/prepare_inst_bank.py --verify # check sources, mappings and existing outputs
```

Add `--fetch` only to restore missing originals from their recorded URLs; existing
mismatches are rejected. Normal application builds use the checked-in header and
do not download source recordings.

## credits and licenses

Source recordings and the generated bass PCM are distributed under
[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
Original notices are retained in [licenses/](licenses/).

- **[VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE)** — Versilian
  Studios / Sam Gossner / Simon Dalzell; sample cutting by Elan Hickler / Soundemote.
  [Original license](licenses/vsco-LICENSE), [homepage](http://vis.versilstudios.net/vsco-community.html).
- **Upright piano** — Simon Dalzell / Ivy Audio, with redistribution permission
  from Versilian Studios; [original credits](licenses/piano-Info.txt).
- **[VCSL — Versilian Community Sample Library](https://github.com/sgossner/VCSL)** —
  Versilian Studios LLC; six acoustic percussion hits. [Original license](licenses/vcsl-LICENSE).

The retained VSCO notice asks for credit, public improvements and no direct sale
of raw samples.

Developer API, note/drum mappings and envelopes: [inst-bank.md](../../docs/inst-bank.md).
