# seance

seance is a sampler and synthesizer for Nintendo 3DS, made by patausx.
it combines modular audio chains, drawn modulation and a step sequencer across
the console's two screens.

`0.1.0-alpha.1` · homebrew · GPL-3.0-or-later

## overview

- four audio chains, with up to six modules each;
- 28 modules for synthesis, sampling, microphone input, recording and effects;
- eight modulation sources, called **sigils**, which you can draw or generate;
- four-track sequencing with eight banks, up to 64 steps, parameter locks and slices;
- a performance mixer, parameter scenes, manual bank transitions and an order of
  up to 16 bank cards;
- WAV recording and export, a project library and editable display palettes.

seance is an independent homebrew application, not an official Nintendo product.
it is still an alpha; demanding patches may exceed the console's audio budget.

## installation

### homebrew launcher (`.3dsx`)

1. use a 3DS/2DS-family system already configured to run homebrew.
2. from an application ZIP, copy its `3ds/` folder to the root of the SD card.
   for a standalone build, put `seance.3dsx` at
   `sdmc:/3ds/seance/seance.3dsx`.
3. open seance from the Homebrew Launcher.

### home menu (`.cia`)

if a CIA is supplied with the release, install it with FBI on a system with custom
firmware. CIA and 3DSX are alternative launch methods; you do not need both.
keep the accompanying license notices when redistributing either format.

### sound and files

- audio requires DSP firmware. if you see **NO DSP**, use a DSP firmware dumper
  such as DSP1 on your own console. firmware is not included.
- built-in instruments and the starter patch work without an external sample pack.
  place your own WAV files in `sdmc:/3ds/seance/samples/`.
- projects, samples, recordings and settings live in `sdmc:/3ds/seance/`.
  **back up this folder before updating.** replace the application, not your data.

no public download URL is configured in this preparation tree. see
[building](BUILDING.md) if you are working from source.

## first session

1. choose **new patch**. the starter chain is `osc → filter → vca`.
2. start with the system volume low, then press **A** to trigger the chain.
3. press **Y** to open Sigils and draw a curve on `a`; it controls the starter's filter.
4. press **Y** to return to Chains. select a module and use **edit / X** to explore it.
5. use **SELECT → patches** and save to a slot. saving is explicit.

continue with the [user guide](GUIDE.md) for a first rhythm, samples, recording
and the performance pages.

## main controls

| input | action |
| --- | --- |
| stylus | select, draw and edit the visible controls |
| `A` | trigger a chain or accept; increase the focused value in the sequencer |
| `B` | back / close; a short press decreases a sequencer value |
| `X` | the action named by the current view |
| `Y` | Chains ↔ Sigils; listen to a step in the sequencer |
| `SELECT` | open **go to**, including projects and exit |
| `START` | play / stop the sequencer |
| `R + START` | stop and reset transport |
| hold `L` in a module | edit modulation for the selected parameter |

context-specific controls are described in the guide and on screen.

## practical limits

- audio runs at 32 kHz. input supports mono/stereo PCM WAV at 8/16/24/32-bit
  and 32-bit float, up to 20 seconds per load. MP3, OGG and ADPCM are not supported.
- sources replace incoming audio, so put effects and recorders after the source
  you want them to process.
- stop an active or paused recording before saving or changing its source or chain.
- New 3DS uses its CPU speedup. Old and New 3DS performance must be tested
  separately; a successful build or host test does not establish device performance.

## documentation

| document | contents |
| --- | --- |
| [user guide](GUIDE.md) | first steps, controls and everyday workflows |
| [building](BUILDING.md) | toolchain, compilation and focused checks |
| [contributing](CONTRIBUTING.md) | code, UI and reporting conventions |
| [source layout](docs/source-layout.md) | where the engine, UI, assets and tools live |
| [changelog](CHANGELOG.md) | features and release status |
| [repository preparation](distribution/repository.md) | clean export, private review and opening the repository |

## license

application code is licensed under **GPL-3.0-or-later**; see [LICENSE](LICENSE).
fonts, audio and other assets have their own terms and credits in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
