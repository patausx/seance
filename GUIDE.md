# seance guide

user guide for `0.1.0-alpha.1`. start with [installation](#installation).
projects are saved explicitly; keep a backup of `sdmc:/3ds/seance/`.

## contents

- [installation](#installation) and [controls at a glance](#controls-at-a-glance)
- [first session](#first-session): a sound, a rhythm and a saved project
- [navigation](#startup-and-navigation) and [starter patch](#first-sound)
- [chains and modules](#chains-and-modules), [sigils](#sigils) and modulation
- [sequencer](#sequencer): values, groups, locks and slices
- [samples](#play-and-samples), [recording](#microphone-and-recording) and [bounce](#bounce)
- [perform, banks and order](#perform)
- [projects and backups](#projects-and-data-safety), [troubleshooting](#troubleshooting)

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
- audio runs at 32 kHz. input supports mono/stereo PCM WAV at 8/16/24/32-bit
  and 32-bit float, up to 20 seconds per load. MP3, OGG and ADPCM are not supported.
- **Old 3DS has not been tested; performance on older models is unknown.**
  New 3DS uses its CPU speedup, but demanding patches can still exceed the audio budget.

there is no binary release attached to the initial source import. see
[building](BUILDING.md) if you are working from source.

## controls at a glance

controls follow the current screen. a menu or held modifier owns its input;
close it before editing the view underneath.

| input | action |
| --- | --- |
| stylus | select, draw and edit the visible controls |
| d-pad | move focus or change the focused choice/value, depending on the screen |
| `A` | trigger a chain or accept; increase the focused value in the sequencer |
| `B` | back / close; a short press decreases a sequencer value on release |
| `X` | the current view's action; open the selected module from Chains |
| `Y` | Chains ↔ Sigils; in the sequencer, listen to the focused step instead |
| `SELECT` | open **go to**, including projects, tempo / swing and exit |
| `START` | play / stop the sequencer |
| `R + START` | stop and reset transport |
| hold `L` in a module | edit modulation for the selected parameter |
| `R` in Chains | open the selected module's **more** actions |
| tap `R` in the sequencer | cycle the step-editing mode |
| hold `R` in the sequencer | open the mode circle; choose with the circle pad or a shortcut, then release `R` to commit |
| `R + B` in the sequencer | cancel mode selection |
| hold `B` in the sequencer | open bank / length / direction controls |
| `L + up/down` in the sequencer | change the current value for the explicit step group |
| `L + B` in the sequencer | deselect the step group |
| `ZR` in Chains | undo the latest structural delete/reorder while it is still current |
| hold `L` with a performance fader | temporary change; release `L` to restore the anchor |

see [mode shortcuts](#mode-shortcuts) for all eight `R` shortcuts,
and [perform](#perform) for live mixer, scenes, bank transitions and order.

## first session

### 1. make a sound

choose **new patch**, keep the system volume low, and press **A**. the selected
chain plays the starter sound. press **Y**, draw on sigil `a`, then press **A**
again to hear the filter move. **Y** returns to Chains.

### 2. make a rhythm

1. use **SELECT → sequencer**. the four rows play the four audio chains.
2. in **trig** mode, tap and release steps in the first row, for example
   `1, 5, 9, 13`. tapping an active step again switches it off.
3. press **START** to play. press it again to stop.
4. use **SELECT → go to** and its **tempo / swing** branch to change the clock.
   return to the sequencer when you are done.

start with the first chain and one page. notes, more banks and locks can wait
until the basic loop is comfortable. a second chain needs its own sound modules
before its steps can make sound.

### 3. save it

open **SELECT → patches**, choose a free slot and use **save**. wait for the save
to finish. exporting a WAV is separate from saving the project.

continue below when you want to add modules, load a sample or arrange banks.

## startup and navigation

**new patch / library / settings** is the startup menu. library opens recent
projects; **all patches** opens the full library of 128 slots. the recent list
contains up to five displayed entries, ordered by successful opening rather than
by every parameter edit. failed loads leave the current project intact.

settings contains **menu sound**, **startup** (menu/last project), and the
**palette editor**. the startup drawing always animates; there is no intro toggle.
hold **R** at launch to recover the menu; **L** requests a fresh starter.
menu sound is decorative, can be disabled, and does not play your saved patch.

in the instrument, **SELECT** opens **go to**:

- patch table (Chains), sigils, sigilbay, sequencer, perform, bounce, patches, exit.
- the small **tempo / swing** branch edits the project clock: 20–300 BPM,
  50–75% swing (50 is straight).
- drag a clock value and lift to commit; moving vertically cancels the draft.
  **X** focuses tempo/swing; arrows change the focused value; **B** leaves focus.
- **Y / mod** while a clock value is focused opens its sigil/depth routing.
  the upper display separates stored base, target and applied clock.

**START** plays/stops the sequencer; **R + START** stops and resets it. these are
transport controls, not save shortcuts. a menu, confirmation or held modifier
owns its input; close it before using the editor underneath.

## first sound

**new patch** installs an audible starter:

```text
osc → filter → vca
        ↑
      sigil a
```

press **A** to strike the selected chain. press **Y** for Sigils and draw on `a`.
the starter's cutoff follows the drawing. **Y** returns to Chains.

if there is no sound, check system volume, DSP firmware, chain mute, module
bypass, and whether the VCA has been triggered. lower the level before exploring
feedback or putting the microphone near the speakers.

## chains and modules

there are **four chains**, each a serial, left-to-right path of up to **six cards**.
select a chain number, then a module. **edit / X** opens its full editor.
use the add card to choose a module; **R / more** opens context actions.

- card controls edit stored parameter bases. modulation can move the actual
  sound without making the base display chase it.
- **bypass** temporarily skips that module; **mute** silences a chain.
- drag a card's grip to preview its position; the drop commits the new order.
- **more** offers replace, copy, paste and delete. deletion has a separate
  confirmation; **B** cancels it.
- **ZR**, on the main screen, undoes the latest delete/reorder while that
  structural undo is still current. this is not an unlimited project history.

sources replace the incoming signal; processors transform it. for example,
`mic → filter → rec` captures a filtered microphone, whereas `rec → mic`
captures whatever came before the microphone.

### module families

browse the picker by role. drag to scroll; the d-pad moves one entry at a time.
selecting an entry and confirming it are separate actions.

| family | modules | use |
| --- | --- | --- |
| sources | **osc, fm, motor, noise, bird, inst, play, gran, mic** | create sound, play a sample or bring in the microphone |
| tone | **filt, tilt, mouth, fold, strain, bit, ring, shift, comp, vca** | shape frequencies, dynamics and gain |
| time | **chor, dly, rev, ghost, swarm, fract, glitch** | chorus, echoes, space and live-buffer processing |
| tools | **arp, rec** | clocked note patterns and in-chain recording |

**inst** includes built-in acoustic instruments and drums. **play** works with a
loaded WAV; **gran** turns one into overlapping grains. **swarm**, in contrast,
works on the live incoming signal. **strain** is distortion, not swarm.
use each editor's named controls and upper display; parameter order varies.

the starter VCA uses a triggered envelope: **A** or a sequencer hit opens it.
the VCA editor also offers an optional trigger-armed cycle. sound can continue
through delay/reverb tails after a source stops.

### parameter modulation

select a parameter, then **hold L** to open its modulation panel. choose a source
and signed depth. the usual relationship is a stored base plus modulation,
limited to the parameter's range. **off** removes the route, not the base value.

the panel's **params** view edits ordinary routes. sigilbay gives an overview:
drag a source socket to an empty parameter input, then edit that cable's amount
or mute. each destination has its own depth and mute; global clock routes are
separate from a module's sound parameters.

### module rand

where present, tap **rand** in the module header and lift to randomize that
module's eligible sound settings once. leaving the word's hit area cancels it.
no other module, scene, source assignment, sample, trim, slice set, recording,
freeze/hold state, routing, lock, bypass, mute or transport is randomized.

hold **L**, then choose **rand** in the panel (or hold L and tap the header word)
to assign **off / a–h** to the action itself. **rand now** performs one draw;
**undo** restores the most recent randomization; **params** returns to ordinary
modulation. undo is one step and runtime-only, not saved project history.

an assigned sigil triggers on a low-to-high transition, not every frame it stays
high. a newly assigned high source waits for a low first. sources are independent
per unit. recording or lock editing can refuse the action; finish those first.
GRAN's crowded header has no rand word: use **L → rand** there.

## sigils

the eight **LFOs (sigils), a–h**, combine hand-drawn and generated modulation.
tap the large letter to select one. draw with the stylus; **undo** swaps its last
stroke/drawn state.

- **shape** chooses drawn, sine, triangle, saw, square, random or smooth. choosing
  a generated shape preserves the drawing for later.
- **mode** chooses loop, shot, step, rand, smooth or follow. the older random
  modes own their waveform; their disabled shape controls are intentional.
- **free / sync** changes rate editing. free rate is 0.05–20 Hz; sync selects
  beat divisions. phase, polarity and restart have their own named controls.
- **step / follow** uses a selected chain. follow needs an eligible active PLAY
  cursor; with none, its output is zero.
- rate modulation offers earlier sources and the envelope follower. sync/follow
  can suspend its effect without deleting its saved settings.
- **X** opens shape; arrows select a source/change rate; **Y** returns to Chains.

**sigilbay** shows the selected source's destinations. source names there open
the same editor; there are no separately created LFO units. old projects migrate
`a–d` as their original sources and the former LFOs as `e–h`. newer routing
features can be lost if a project is resaved by an older build: keep backups.

## sequencer

open **SELECT → sequencer**. four tracks correspond to the four chains. each
stores up to 64 steps, shown as four pages of sixteen. the upper display shows
the focused step's full values and context; selection and the playing marker are
separate. no separate full-screen focus view is needed.

### mode and single-step editing

- tap **R** to cycle **trig / note / level / retrig / when / time / lock / slice**.
  **hold R** to open the mode circle. move the circle pad toward a mode, then
  release **R** to commit; **R+B** cancels. returning the pad to center keeps
  the last candidate. the [mode shortcuts](#mode-shortcuts) are another way to choose.
- **A** increases/enables the focused step's current value.
- **short B**, on release, decreases/disables it. holding B opens the bank /
  length / direction controls and does not also apply a minus.
- **Y** listens to that step: one manual chain hit using its note, slice and
  valid locks, without moving transport or requiring its trigger/condition.
- **X** clears only the focused step and deselects the group.
- note changes are semitones. timing, conditions and retriggers have their own
  value vocabularies; use the upper readout for the whole value.
- **START** plays/stops; **R+START** stops/resets. **Y does not leave the score**;
  use **SELECT / go to** to change views.

### mode shortcuts

hold **R**, press one of these buttons, then release **R** to commit. these chords
choose a mode; they do not also edit or audition a step.

| chord | mode |
| --- | --- |
| `R + X` | trig |
| `R + up` | note |
| `R + right` | level |
| `R + down` | retrig |
| `R + Y` | when |
| `R + left` | time |
| `R + A` | lock |
| `R + L` | slice |

### groups and clipboard

make a group with the score's selection gesture; its outline is independent of
the focused step. **L+B** deselects. **L+up/down** changes the current mode's value
for **every explicitly selected step**, including off-page members. it does not
resize/move the selection. without a group it does nothing.

A/B always edit just the focused step, even with a group. group changes form one
undoable edit; saturated members clamp individually. in lock mode a full member
can refuse the whole change rather than silently applying half of it. group edits
do not audition a burst of hits.

use the score's tools for copy/paste, set/offset and undo. **clear clip** in that
tools footer empties only the clipboard; it does not clear notes or the group.

### locks and slices

lock mode stores a per-step parameter override, not a new module base. select the
parameter with the lock inspector; unsupported/structural operations remain
unavailable. lock editing owns input while it is open.

slice mode uses the selected PLAY's existing cut IDs. slice IDs may be sparse;
changing a slice walks available IDs, rather than assuming consecutive values.
editing the cuts themselves belongs to the PLAY editor, not to the step grid.

## play and samples

put your WAV files in `sdmc:/3ds/seance/samples/`. the PLAY/GRAN source header
opens the browser. selecting, listening, and assigning are different actions;
browsing a file does not automatically replace a module's sample.

supported input: PCM WAV 8/16/24/32-bit and 32-bit float, mono or stereo, up to
20 seconds per load. the engine runs at 32 kHz. MP3, OGG, ADPCM and other WAV
encodings are not supported. sample memory is shared and finite.

PLAY's right-hand rail has **normal / stretch / tape / scratch / hold**, with
**config** below. scratch/hold are workspaces for the current playback engine,
not extra pitch algorithms. stretch tempo is manual, not automatic BPM sync;
root tuning is user-prepared, not detected.

- **trim / slices / loop** selects waveform tools without automatically enabling
  looping or sequenced scrub.
- waveform lanes, fine drag, snap, fades, zoom/pan, cut/seam and loop controls are
  contextual. use their named sheets rather than searching for hidden sliders.
- **config → voice** contains triggering, direction, loop and gain controls.
- **config → listen** controls the private source/chain preview and repeat range.
- **config → file** contains processing of copies, save selection and detach raw.
  save/detach/crop require confirmation; the original source WAV is not overwritten.
- **history** exposes undo and redo for sample edits. source/document changes can
  cancel a held control; lift and begin a fresh gesture.

## microphone and recording

start with **mic → rec**, or **osc → effects → rec**. REC's `< sxx >` arrows
select its workspace slot while idle; they are not a file browser.

1. set **length / monitor**, then **rec**. length latches for that take; monitor
   affects pass-through, not whether capture occurs.
2. press **stop** and let a pending capture finish. active/paused/pending
   recording blocks conflicting storage, source and topology operations.
3. **save wav** writes the take. **> play / > gran** first saves an unsaved take,
   then inserts playback when there is room. failed saving means no send.
4. save a completed take, then explicitly **clear** before recording a new one.
   clearing the workspace does not delete its exported WAV.

exports get unique names in `sdmc:/3ds/seance/recordings/`. project save exports
finalized unsaved takes first and then writes the references. an export failure
leaves the project dirty; it does not finish a pending load/exit. saving a WAV
alone does not save the project's controls: save the project separately.

## bounce

**SELECT → bounce** records the master mix or one chain to a bar-aligned loop.
choose **mix / 1 / 2 / 3 / 4**, choose bars, then **record**. a stopped song starts
aligned with sequencing enabled after admission; a running song arms for the
next bar. capture offers **cancel / stop early**.

on the result, **listen** plays the raw take once and pauses the song, without
applying the master path a second time. **save wav** exports it. **use in chain**
saves first, pauses transport and asks for explicit destination/replacement
confirmation. it replaces that destination's modules **and rhythm**, even if the
chain was empty. save a project copy first if you want to preserve the old chain.
partial takes can be saved but are not offered as complete loops for chain use.

master capture is final stereo. single-chain capture is mono, after that chain's
modules but before chain volume/pan and master processing. muted/empty chains
record silence; a zero chain fader alone does not silence this pre-fader tap.
maximum capture is 524288 frames (16.384 s); unavailable bar lengths are dimmed.
there is no automatic tail extension, normalization or crossfade. **record again**
releases the held workspace, not previously exported WAVs.

## perform

open **SELECT → perform**. the lower display has three tabs:
**order / perform / banks**. opening a tab does not launch a bank or stop an order.

### perform: levels and sound scenes

- tap a chain number to trigger it. use its separate mute control to mute/unmute.
  left/right selects a chain, **A** triggers it, and **X** toggles its mute.
- drag the four vertical faders to mix chain levels.
- hold **capture A**, change sound parameters, then hold **capture B** to store
  the scene endpoints. a short tap does not overwrite a scene.
- drag the A/B fader to morph compatible sound settings and pan. performance
  morph preserves the current mixer levels even though scene data also stores
  levels. module topology must still match the captures.
- hold **L** while using a level or scene fader for a temporary change. releasing
  the stylus keeps the temporary position; releasing **L** restores the anchor.

scenes do not change module types/order, samples, routes, bypass, mute, recording
buffers or effect tails. scene capture is not a project save.

### banks: manual rhythm changes

swipe vertically on a large bank digit to select a transition endpoint. selection
alone does not launch it. use the separately labelled **launch** action for a
queued switch, or **cancel queue** before it fires. the label shows its launch
quantum; a stopped transport does not start itself.

the transition rail has **rhythm / sound / both** modes. rhythm blends the
sequenced events of two banks, not two copies of the audio. sound uses the A/B
scenes; both needs compatible scenes and matching rail positions. complete the
rhythm rail at an endpoint and use **keep bank** to adopt it.

filter, echo and roll are temporary FX gestures. lift to release them; an echo
tail can continue. **L** also gives the main bank rail a temporary return point.

if order owns the banks, use **take over** before manual launch or rhythm changes.
this pauses order without stopping music or changing the current bank.

### order: a sequence of bank cards

order plays up to **16 cards**, four per page. each card selects one of the eight
banks and a duration of **1–16 bars**. repeated bank numbers are separate positions.

1. tap **+ card** to add the live bank with a four-bar duration.
2. tap its large number to choose a bank. **remove card** is in that chooser.
3. drag horizontally on **bars** to change the duration; lift to commit.
4. drag a card by its top strip to reorder it. hold over a page arrow to move
   between pages, then drop on a card position. an invalid drop cancels the move.
5. select the card you want to begin with, then use **start / resume**.
   if transport is stopped, press **START** separately. during playback, order
   starts at the next bar and gives the card its full duration.

**loop** repeats the route. **hold** keeps the current card running; **next >**
queues the next card for the next bar and releases hold on arrival. **pause**
returns bank control to you without stopping transport. a non-looping route ends
order only; music continues.

editing a playing or already queued card stores its new bank/duration for its
next visit, not the accepted launch. its small annotation shows the saved change.
current, paused and queued cards are protected from deletion. select a different
position or finish the order before removing a protected card.

to return from manual banks, complete any rhythm blend and press **keep bank**,
wait for any manual launch to finish or cancel it, then select the exact order
card and **resume**. resume uses that position, not a search for its bank number.
save the project to keep the card configuration; pause, hold and queued launches
are temporary playback state.

## projects and data safety

**SELECT → patches** opens 128 slots. use the library's load/save actions and
pages; an occupied destination needs overwrite confirmation. **new** creates a
starter, not an automatic slot overwrite. a loaded project is stopped until you
press play.

unsaved persistent edits require **save / discard / cancel** before new, load or
exit. if a value changes during storage and saving asks for a retry, save again;
do not assume the older copy contains those edits. stop recording first.

files use `sdmc:/3ds/seance/patch0.snc` through `patch127.snc`. preferences and
recent history use a separate `start_menu.bin` sidecar. sample/recording WAVs are
referenced from projects; keep their paths and back up the whole folder, not
just the SNC files. missing samples cannot be repaired by copying an application
binary. do not power off during storage operations.

## troubleshooting

- **NO DSP:** set up DSP firmware on your own console; it is not shipped here.
- **silence:** lower then raise volume carefully; check mute/bypass, sample
  assignment, VCA trigger and source-before-processor order.
- **glitches:** reduce dense effects/feedback/voices; New 3DS uses startup CPU
  speedup. Old 3DS has not been tested; older-model performance is unknown.
  sleep/wake and sustained performance need real-device testing.
- **save refused:** finish recording, leave modal editors, check SD free space
  and permissions, then retry without discarding the current patch.
- **report a bug:** include the version, console model, CIA/3DSX, reproduction
  steps, and a small redistributable project if possible. never attach your
  entire private sample collection or console credentials.

a clean build and host tests are not device listening, FPS or long-session
acceptance. test installation, starter sound, drawing, sample loading, microphone,
recording/export, project round-trip, sleep/wake and a sustained session before
calling a candidate hardware-tested. see [the local publication checklist](distribution/publication-checklist.md).
