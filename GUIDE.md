# seance guide

user guide for the current `0.1.0-alpha.1` source. start with [installation](#installation).
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
- [module reference](#module-reference): all 33 active modules, controls and patch ideas

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

the current source update does not announce a new binary release. if no application
archive is supplied, see [building](BUILDING.md) to build from source.

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
| hold `R` in the sequencer | open the mode circle over the score; choose with the circle pad, a touch or a shortcut, then release `R` to commit |
| `R + B` in the sequencer | cancel mode selection |
| hold `B` in the sequencer | open the bank panel: `L`/`R` bank, up/down track, left/right length, `A` direction, `X` copy, `Y` paste |
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
| sources | [osc](#osc), [fm](#fm), [pent](#pent), [motor](#motor), [noise](#noise), [bird](#bird), [inst](#inst), [play](#play), [scrt](#scrt), [paul](#paul), [rule](#rule), [rot](#rot), [gran](#gran), [mic](#mic) | create sound, play a sample or bring in the microphone |
| tone | [filt](#filt), [tilt](#tilt), [mouth](#mouth), [fold](#fold), [strain](#strain), [bit](#bit), [ring](#ring), [shift](#shift), [comp](#comp), [vca](#vca) | shape frequencies, dynamics and gain |
| time | [chor](#chor), [dly](#dly), [rev](#rev), [ghost](#ghost), [swarm](#swarm), [fract](#fract), [glitch](#glitch) | chorus, echoes, space and live-buffer processing |
| tools | [arp](#arp), [rec](#rec) | clocked note patterns and in-chain recording |

**inst** includes built-in instruments and drums. **play** edits and plays a loaded
WAV; **scrt** scratches or holds it, **paul** stretches it spectrally, and **gran**
turns it into overlapping grains. these are separate source modules. **swarm**, in
contrast, works on the live incoming signal. **strain** is distortion, not swarm.
use each editor's named controls and upper display; parameter order varies.

the starter VCA uses a triggered envelope: **A** or a sequencer hit opens it.
the VCA editor also offers an optional trigger-armed cycle. sound can continue
through delay/reverb tails after a source stops.

### stereo chains

the stereo foundation retains both channels through **filter / vca / rec / arp**
when a stereo pipe is admitted. unsupported processors refuse the **whole chain's
audio**, never silently fold left/right or skip an effect. ordinary mono chains
keep their existing behavior. comp/mouth/glitch retain their own bypass crossfade
and still require stereo support even when bypassed. [pent](#pent) offers explicit
stereo; its default remains mono. the relic stereo source itself is [retired](#relic).

REC captures its actual input width; saved WAVs retain it. chain bounce is likewise
mono or stereo according to the admitted pipe, while master bounce is stereo.
the existing bounce **use in chain** path uses mono PLAY and loses stereo; it is
not a stereo replay path.

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

the upper arc shows the selected sigil's phase/output and its first saved destination,
with stored base and live value kept separate. a filter destination also shows a
model of its response, not a measured audio spectrum. inactive/off routes remain
visible rather than being replaced by a different active destination.

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
- **short B**, on release, decreases/disables it. holding B opens the
  [bank panel](#bank-panel) and does not also apply a minus.
- **Y** listens to that step: one manual chain hit using its note, slice and
  valid locks, without moving transport or requiring its trigger/condition.
- **X** clears only the focused step and deselects the group.
- note changes use semitones in **chromatic**, or the chosen scale's degrees for
  absolute-note targets. timing, conditions and retriggers keep their own value
  vocabularies; use the upper readout for the whole value.
- **START** plays/stops; **R+START** stops/resets. **Y does not leave the score**;
  use **SELECT / go to** to change views.

### bank panel

hold **B**. the eight bank numbers stand in one row; empty banks are dim and
**play** marks the sounding one. below them all four tracks show their length
and direction, and the upper display shows what the chosen bank stores.

- **L / R** choose the bank to edit. this never launches it.
- **up / down** choose a track, **left / right** change its length, **A** its direction.
- **X** copies the chosen bank; **Y** pastes the copy into the chosen bank. **clear**
  is a touch word. a bank that already holds something asks a second time.
  the sounding bank cannot be replaced or cleared; stop first.
- the panel also works with the stylus while B is held: tap a number, drag a
  length rail, tap a direction.
- releasing **B** keeps the chosen bank and the new lengths/directions.
  **SELECT** discards them. paste and clear are written at once.

### continuous sources

a track whose source sounds continuously and has no gate after it (for example an
osc without a vca, pent, rule or rot) shows its level as a curve in **trig** mode.

- tap a step to set or remove its trigger; a small tick under the curve marks it.
  on these rows the trigger is set by tap only.
- drag the stylus along the row to draw the curve. one stroke is one undo step.
- **A / B** raise or lower the selected point; the rail below sets it directly.
- while the edited bank plays, a small solid diamond on the selected row shows
  the value that is actually applied, including modulation.

### project scale

In **note**, tap **chrom** (or the current word, such as **c minor**) in the editor
row. Choose the tonic and scale from its two word branches; lift to commit.
Dragging out cancels the press; **B** or a tap outside closes. The setting is shared
by all tracks/banks and saved with the project. **R** keeps its mode-circle gestures.

Touch and A/B/group edits on absolute-note targets follow the selected degrees.
Changing the scale never rewrites existing notes; Lock, paste and undo stay raw.
Relative transposition and compatibility speed entry keep their previous mappings.
**OSC qnt** and **PLAY tape qnt** additionally snap their nominal pitch/modulation
to the scale. Other continuous modulation, musical PLAY Normal/Stretch and RULE's
local SCALE are not globally quantized. Samples still use their existing
user-prepared-c4 assumption, not automatic pitch detection.

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

lock mode stores a per-step parameter override, not a new module base. choose the
parameter with the circle pad (left/right module, up/down parameter), or tap the
address in the editor row (such as **02 filt / cut**) and pick the module and
parameter from the two lists. picking only selects the target; nothing is written
until you change a value. unsupported/structural operations remain unavailable.

**primary pitch holds until the next accepted written pitch**, even across empty
steps or a new trigger with no note. an accepted pitch record does not need its own
trigger; an explicit zero/base note is still a new record. a failed condition does
not change the held pitch. the same rule applies to a generic lock on that primary
pitch parameter. level, slice and other ordinary locks end at each step boundary,
including a step whose condition fails. holding pitch does not extend a voice's
gate or release. stop/reset, project loading or replacing/changing the pitch target
releases this runtime hold; switching playing banks with the same target retains it.
unwritten step readouts still show stored/base values, not a prediction of conditional
playback. older sparse-note patterns can therefore sound different in this build.

slice mode uses the selected PLAY's existing cut IDs. slice IDs may be sparse;
changing a slice walks available IDs, rather than assuming consecutive values.
editing the cuts themselves belongs to the PLAY editor, not to the step grid.

## play and samples

put your WAV files in `sdmc:/3ds/seance/samples/`. the source name in PLAY, GRAN,
SCRT, PAUL, RULE or ROT opens the shared browser. browsing, listening and assigning are
separate actions; browsing a file does not replace a module's sample.

### sample browser

choose **library / project / recordings** from the source word or **L/R**. drag to
scroll, or select with up/down. the upper display scrolls long filenames so their
suffixes can be read; list edges fade when more rows lie outside the view.

- tap a file row and lift to select and audition it; dragging past rows does not
  audition them. tap a folder to open it. **B / left** returns to its parent;
  **B** at the root closes the browser.
- **Y / listen** restarts or stops audition. **A / use** installs the successfully
  loaded selection and returns to the editor. auto preview affects d-pad selection,
  not explicit file taps.
- **X / delete** in library/recordings asks before deleting the actual WAV from SD.
  **cancel** is selected by default; explicitly choose **delete wav** to proceed.
  loaded/busy sources are protected, and a changed target invalidates confirmation.
- **remove** in project clears an eligible bank sound but keeps its WAV. shared or
  busy sounds can refuse removal. deleting a WAV can break references in other
  saved projects that are not open; keep backups. there is no deletion undo.

supported input: PCM WAV 8/16/24/32-bit and 32-bit float, mono or stereo, up to
20 seconds per load. the engine runs at 32 kHz. MP3, OGG, ADPCM and other WAV
encodings are not supported. sample memory is shared and finite.

### play editing

PLAY has one inline editor: **trim / chop / loop** selects the source-time edit
lane; **normal / stretch / tape** selects playback. scratch/hold and spectral
stretching belong to [SCRT](#scrt) and [PAUL](#paul), not to PLAY modes. the upper
waveform provides context for the lower edit line.

- in trim/loop, drag a visible end to resize the interval, or drag inside it to
  move the interval. a press alone does not jump an end to the touch position.
  **loop** selects the edit lane; the separate **loop on/off** enables playback looping.
- in chop, tap a free point on the line and lift to split directly. there is no add
  mode or double-tap action. tap a part label to select it without changing the
  musical slice assigned in the sequencer; repeated label taps do not delete or reset it.
- drag a shared boundary horizontally to move the seam. the first source start
  and final source end are fixed. close ends use upper/lower grab lanes; zoom in
  when parts are too narrow to select comfortably.
- to merge, hold an internal boundary still for **450 ms**, then move vertically
  beyond the edit lane's **12 px** outer margin. the joined part is shown and
  auditioned before lift. return inside to remove the merge preview; a valid lift
  commits. the chronological left part keeps its ID, reverse, pitch and old rate;
  the removed ID becomes invalid. this does not delete PCM, renumber parts or
  rewrite sequencer slice/lock records. revisit steps using the removed ID.
- **fine** and **- / scale / +** sit beneath the lane. fine reduces boundary travel
  to 1/8; zoom centers on the selected interval/part, and tapping the scale resets
  to **1x**, the full source. drag zoomed background to pan the view. offscreen
  boundaries are not substitute grips at the view edges.
- tap the active trim/chop/loop word again for its contextual actions: fades/reset
  trim, auto/live cuts and part nudges, or loop seam/crossfade and snap. auto-cut
  replacement and clearing need confirmation. cuts are shared by cards using that
  bank sample; save a project copy before replacing a prepared set. in **live cuts**,
  **arm source pass** starts a pass, **A / mark cursor** inserts at the actual source
  position, and **stop / disarm** ends that pass.

### play playback and audition

pitch (or tape speed), gain, one shot/gate, loop and global reverse stay inline.
when gate is selected, scrub its duration word; stretch's **rate** is beside fine
and zoom. pitch/rate are set manually, not inferred from a file's key or BPM.

- **normal** changes source playback pitch and duration together.
- **stretch** separates pitch from temporal rate; raising pitch does not accelerate
  the source-time scan.
- **tape** couples playback speed and pitch.
- chop's selected part has its own **-24…+24 st** pitch and reverse controls, separate
  from global pitch/direction and from historical saved per-part rates. small
  badges identify edited parts where space permits. selecting a part alone does
  not write the musical slice value.

an audio-edit drag starts audition after movement and updates the same audition
as it moves; **lift or cancellation stops drag-owned audio**. **A** separately
starts/stops audition of the selected interval/part (outside an armed live-cut pass).
an already-running A audition can accompany a drag and survives a normal lift.
panning or selecting alone does not start drag audition. source, document or navigation changes cancel the held
control; lift and begin a fresh gesture. **rand** is a separate header action.

### play files

**files** is in the source header. it offers **save copy / source options / detach
raw / crop copy**. copy options cover DC removal, normalization, fades and reversal;
these operate on source-domain audio, not on the pitched/stretched chain output.
save, detach and crop require confirmation and never overwrite the original WAV.
detach/crop uses a new copy on this card; save copy keeps its current source. use
[REC](#rec) or [bounce](#bounce) to capture the audible processed result.

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

master capture is final stereo. single-chain capture retains the admitted chain's
actual mono or stereo width, after its modules but before chain volume/pan and
master processing. muted/empty chains record silence; a zero chain fader alone
does not silence this pre-fader tap. changing channel width cancels an armed take
or ends a partial take; the existing **use in chain** path converts to mono PLAY,
not stereo replay.
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
- **report a bug:** include the version/source revision, console model, CIA/3DSX, reproduction
  steps, and a small redistributable project if possible. never attach your
  entire private sample collection or console credentials.

a clean build and host tests are not device listening, FPS or long-session
acceptance. test installation, starter sound, drawing, sample loading, microphone,
recording/export, project round-trip, sleep/wake and a sustained session before
calling a candidate hardware-tested. see [building](BUILDING.md).

## module reference

33 active modules, grouped by the same roles as the picker:
[sources](#sources) · [tone](#tone) · [time](#time) · [tools](#tools).
select a card and use **edit / X** to open its controls.

chains run left to right. a source replaces the incoming audio; a processor
works on what reaches it. putting two sources in series does not mix them:
use separate chains for layers. **arp** is a note tool placed *before* its
source; **rec** captures the signal at its own position in the chain.

the patch ideas below fit within one chain's six-card limit. they are starting
points, not presets: add the cards, choose any required sample, and trigger the
chain or start transport as appropriate. use [sigils](#sigils) to move controls,
[locks](#locks-and-slices) for per-step changes, and [bounce](#bounce) to turn a
finished chain or mix into new sample material.

this describes current modules. older projects can retain legacy engines with
fewer controls; where an **extend** action is offered, it deliberately changes
that card's sound/features. save a project copy before converting. not every
module offers conversion, and newer project features may require a newer build.

### sources

#### osc

**oscillator.** pitched sine, triangle, saw and pulse shapes, with continuous
shape morphing on current cards. pitch and width set the voice; fold adds
harmonics, sub adds a lower octave, and drift loosens the tuning. **qnt** gives
semitone pitch editing.

**try:** `osc → filt → vca` for a bass or lead; route a slow sigil to width or
filter cutoff. trigger the VCA to hear its envelope open.

#### pent

**five coupled oscillators.** one root and five independent tuning ratios form a
continuous source; each oscillator phase-modulates its two pentagram neighbours.
default tuning is d2 with **1 : 5/4 : 3/2 : 7/4 : 2**. no WAV or sample bank is needed.

- main keeps **spread / bond / drift / drive**. spread detunes the five voices;
  bond changes their coupling depth, drift its slow pitch movement, drive the
  oscillator nonlinearity. **motion → depth** sets the drift excursion.
- tap a–e to select a voice without changing its tuning; tap its ratio for presets
  or continuous tuning. **voices** also keeps root, level and mono/stereo.
- **X** opens the selected control disclosure. **A** keeps triggering the chain
  while the menu is closed; inside a menu it accepts the focused choice. **B**
  closes; **L / mod** edits the selected parameter's modulation.
- mono works with the existing mono effects. explicit stereo keeps five pans but
  requires an admitted stereo chain; unsupported effects refuse instead of folding
  or being skipped. return to mono when using those effects.
- the white star and blood motion are an ornament driven by the processed drift
  phase, not an audio spectrum or a per-voice PM meter. sequencer stop makes that
  ornament quiet, but does not silence the continuous source.

for rhythmic notes, add a VCA and trigger it; use level/mute to silence a drone.
pent projects require a build supporting SNCK20; older builds reject them. root
spans MIDI0–120, with a12khz per-voice ceiling; extreme nonlinear settings may alias.

**try:** `pent → filt → vca → dly` in mono, with a slow sigil on bond. use
`arp → pent → filt → vca → rec` for an explicit stereo recording.

#### fm

**two-operator synthesizer.** one oscillator modulates another for clean tones,
bells, basses or rough percussion. start with pitch, ratio and index; feedback
adds edge. attack/decay, index fall and feedback fall shape a strike. current
cards also offer operator waves and a pitch envelope; **drone** sustains the voice.

**try:** `fm → dly → rev` with a short decay and a little index fall for ringing,
changing bell patterns.

#### motor

**jittered oscillator with a resonant body.** saw/pulse character, coupled
oscillators and unstable tuning make it useful for buzzing basses and mechanical
drones. explore load, resonance and jitter; current cards add pulse width,
hard sync, jitter speed and separate **free / note** tuning.

**try:** `motor → tilt → vca` for a rough pulse. use note mode if a preceding
arp should play it; free tuning is not an arp target.

#### noise

**texture and percussion source.** white, pink, crackle and metal colours can
run as a drone or a triggered hit. grain rate/density controls the excitation;
tone, low cut and resonance shape its body. attack/decay set the hit, and a
pitch-like filter sweep adds movement on current cards.

**try:** `noise → filt → rev` in hit mode for hats and small percussion, or in
drone mode with slow cutoff modulation for a shifting bed of noise.

#### bird

**synthetic calls.** a finite whistle, not a sample player or an automatic flock.
pitch, sweep and length set the call; trill and syllable articulate it, contour
bends its pitch path, and air adds breath. one trigger makes one call.

**try:** `bird → dly → rev`; sequence sparse hits and lock sweep or length to
make phrases rather than identical repetitions.

#### inst

**built-in instruments.** **bank** selects sampled piano, violin, brass, flute,
bass or one-shot drums; no external bank is needed. choose an instrument, then note
or drum sound. length, attack, release, tone and level shape the sample voice;
length has no effect on one-shot drums.

**model** selects struck modal **vibes**, synthesized rather than sampled. **sound**
keeps note, decay, body and level with a position/hardness field; **env** contains
damping, strength and amplitude-envelope controls. model traits latch at a strike;
level can change live, but editing controls does not trigger a new note. press **A**
to strike.

**try:** `inst → chor → rev` for a small melodic part, or choose drums and use
per-step note/sound changes for a rhythm from one card.

#### play

**sample player and editor.** load a WAV and edit trim/chop/loop directly on the
source-time line. normal/stretch/tape, global playback controls and selected-part
pitch/reverse stay inline. tap/lift splits; hold an internal seam and move outward
to preview a left-ID-preserving merge. drag edits audition until lift. **files**
handles source copies; [SCRT](#scrt) and [PAUL](#paul) are separate sample sources.

**try:** `play → filt → dly`; assign cuts in the editor, then choose them with
sequencer slice mode. see [samples](#play-and-samples) for loading and editing.

#### scrt

**scratch / hold sample source.** scratch reads one moving head and is silent when
stationary; hold uses two local overlapping readers. neither changes PLAY's engine.

- choose **speed / position** for the starting axis pair; tap **x … > / y … >**
  to assign each axis independently. position, speed and gain are available in
  both algorithms; hold also offers window and pitch.
- pressing the field starts the source head at the horizontal source point under
  the stylus, not at an old edge. field parameters are absolute, with Y increasing
  upward. ordinary gestures do not write parameter bases or step locks.
- **latch on** keeps the point and sound after lifting; **latch off** releases it.
  a normal lift retains the latest point, while cancellation does not commit it.
  latch is independent from sequencer gates and stays on when transport stops;
  switch it off explicitly. source/configuration changes, mute/bypass and recording
  disarm it. loading a project never starts a saved latch.
- for automated movement, select **position** in setup and use **mod / held L**
  to assign a sigil. this works even with speed/pitch on the field. open the sound
  with latch or sequencer steps; assigning a sigil alone does not trigger it.
- with speed zero and unchanging position, scratch is silent. a held field point
  with nonzero speed still moves the source head; it is not a frozen source frame.

hold speed spans **-4…+4x**, independently of **-24…+24 st** reader pitch. its window
is **16…6400 source frames**, default 3200. changing the window live is not promised
to be transparent or click-free. effects belong in downstream modules, not an FX
page inside SCRT.

axis assignments are saved, but latch, gestures and the live head are not. older
builds may reject projects with custom axes; keep a backup before changing builds.

**try:** `scrt → filt → rec`; load a sample, choose hold, play the XY field, then
record a short passage. explicitly turn latch off when finished.

#### paul

**spectral sample source.** stretches a loaded sample into a texture; pitch is
independent from stretch. main keeps position, stretch, window and freeze.
stretch spans **1…64x** (default 16x); windows are **2048 / 4096 / 8192 frames**
(default 4096).
tap the direction word and lift to cycle **forward / reverse / ping-pong**.
changing direction keeps the current location; reverse does not mirror saved
position. ping-pong reflects between the region edges; loop-off ends its current
leg. freeze stops automatic source advance while fresh spectral phases keep
sounding; position edits or modulation can still seek while frozen.

setup keeps flow/gate, gain, pitch, gate duration, start/end and loop. **flow** scans
the source; **gate** gives manual/sequencer hits a finite duration. very short gates
can end before spectral warmup, especially with large windows. the main position
strip uses full-source coordinates even with a narrowed region; setup position
stays region-relative. direction is saved card configuration, not a step lock or
modulation slot, and is disabled during protected input/recording/lock contexts.

use **rec** downstream to record the result; there is no separate save-to-sample
control. source analysis advances in hops, so visible cursor steps are not
necessarily audio breakups. older PAUL builds may reject reverse/ping-pong projects;
keep backups. sustained console performance and listening still need device checks.

**try:** `paul → rev → rec`; load a sustained sound, start with flow and the default
window, then freeze a region and record the evolving texture.

#### rule

**generative sample score.** load one WAV through the source name, then start
transport. a fifteen-cell ring automaton creates notes on births, holds accepted
living cells and releases dead ones; the sample itself is never rewritten.

- **rate / density / root / range / attack / release / mutate / spread / voices /
  level** are the ten main controls. root transposes the source by -24…+24 st;
  it is not automatic tuning or the sample's absolute MIDI root.
- tap a neighbourhood tile to change a rule bit; tap a current-row cell to seed
  it. edits take effect at the next actual generation, without resetting existing
  readers. an automatic phrase edit starts from the currently sounding rule.
- **scale** selects the five-step scale; setup selects the repeating 90/30/110
  phrase or manual rule and the nominal source-root metadata. **L / mod** and
  selected-bank parameter locks use the ordinary controls.

RULE is dry mono. add DLY/REV downstream for space; stereo source files are read
as mono without changing their PCM. the stopped sequencer's one-generation
preview can be silent when that generation has no accepted birth. use the
browser to audition the source. RULE projects require SNCL21 support or newer.

**try:** `rule → filt → dly → rev`; load a struck or sustained note, start the
phrase, then change a table bit to hear another pattern emerge.

#### rot

**decaying sample tape.** choose a WAV through the source name. ROT reads a
private mono loop and accumulates local smear, holes, bit erosion, crackle and wow
on it, pass after pass. the bank sample and WAV are never damaged.

- the strip's two grips set **start / length**, up to eight seconds per loop.
  a changed region is prepared afresh; ghost target grips show a pending window.
  old-region damage is not a reversible edit, and preparing a region is silent.
- ten main controls are **pitch / level / rot / spread / smear / holes / crackle /
  wow / bits / heal**. pitch is -24…+24 st; all twelve parameters, including
  grips, support the ordinary MOD pane and selected-bank locks.
- **rot / heal / hold** commit on lift. rot damages the tape, heal pulls it back
  toward the original, hold freezes damage and pass progression while the tape
  readers keep sounding; it is saved configuration, not a held stylus command.
  setup edits the deterministic seed and 4…100 pass count.
- a ROT card you add from the picker **keeps rotting until you hold or heal it**:
  it never heals or stops by itself, and the pass count is how many passes the
  damage takes to reach full speed. its seven damage controls (rot … bits) start
  at 75%. 50% is the full strength of earlier builds; the upper half goes further,
  up to twelve times as many rot spots per pass, deeper holes, coarser bits and
  denser, louder crackle. at 100% a five-second loop is mostly gone in about 20
  seconds and a one-second loop in a few seconds; a shorter loop always rots
  faster, because one damage step takes one loop length.
- a ROT card from a project saved by an earlier build keeps the earlier run: decay
  over the pass count, automatic healing over the last passes, then a frozen final
  tape, with 100% on a damage control as its full strength.
- start transport to let the tape rot. **A** while stopped auditions one bounded
  loop after preparation. any trigger on the chain starts a fresh tape, so leave
  the ROT track without triggers while it decays. source preparation can take a
  noticeable fraction of a second, longer with a long imported file.

The upper film shows the actual pass envelopes and rot masks, previous layers,
real crossed crackle bins and healed scars. it does not compute the audio.
ROT is dry mono, not an incoming-audio effect. add REV/DLY as separate modules.
New ROT projects require SNCM22 support; older builds reject them, and a project
holding a ROT card added with this build cannot be opened by earlier builds.
several ROT cards can exhaust the shared private-workspace budget; reduce the
patch rather than overwrite a source. console CPU/memory headroom and listening
remain device checks, not guaranteed by the host tests.

**try:** `rot → rev → rec`; load a sustained sound, start transport, hold a stage
of decay you like, and record it downstream. shorten the loop for faster decay.

#### relic

**temporarily retired.** relic is absent from the picker and current audio/UI
runtime after reported performance problems; its exact implementation is archived,
not redesigned or replaced with another module. persisted kind31 remains reserved.
projects containing relic, even only in an inactive scene, refuse safely before
changing the live project or sample bank. their files are not edited or deleted;
open them with the archived pre-retirement implementation instead. ordinary
projects and all other modules retain their existing behavior.

this removal does not claim a measured console speedup or automatically update an
installed binary.

#### gran

**granular sample source.** a loaded WAV becomes overlapping grains instead of
one continuous playback head. position chooses where to read; size and density
change the texture, while spray and pitch spread (**p.dev**) loosen the cloud.
this reads a loaded sample; use **swarm** to granulate incoming audio instead.

- **grain / cloud** exposes position, size, spray, speed, gain and scan, plus
  density, jitter, voices, attack/release and p.dev. **flow / gate / burst** selects
  launch behavior; the pad restarts, holds or bursts according to that mode.
- **setup** contains window, burst count, zoom and clock, plus **reading**:
  **pitch sign / forward / reverse / mixed**. pitch sign preserves signed speed;
  the other choices use its magnitude and set grain direction independently.
- **reverse %**, editable in mixed only, spans **0…100** (default 50). it sets a
  balanced share of newly spawned reverse grains, not a random chance. 0 is all
  forward and 100 all reverse. direction changes apply to new grains; existing
  heads/tails finish in their latched direction. scan direction is independent.
- reading/reverse % is saved card configuration, not a parameter lock, sigil or
  scene destination. older cards can retain the legacy engine; their **old >**
  conversion is explicit. newer reading settings require a compatible build.

coherent overlapping grains and release tails can overload the output; lower gain
and listen at a safe level. these controls do not add automatic headroom or limiting.

**try:** `gran → mouth → rev`; load a voice or sustained note and move position
slowly with a sigil to discover textures inside it.

#### mic

**live microphone source.** brings the console's microphone into the chain.
gain sets input level and low cut removes some low-frequency rumble. it does
not record by itself: place **rec** downstream to keep the result.

**try:** `mic → filt → rec` to capture a processed sound, or `mic → swarm → rev`
for live texture. keep speaker volume low to avoid acoustic feedback.

### tone

#### filt

**resonant filter.** low-pass, high-pass, band-pass and notch responses carve
out different parts of a sound. cutoff and resonance are the main gestures;
drive adds colour; morph blends the selected response with its complementary
signal (input minus the filtered output), rather than selecting another mode.

**try:** `osc → filt → vca`; draw a sigil for cutoff, then use step locks to
accent particular hits with resonance or drive.

#### tilt

**spectral balance.** tilts the sound around a pivot: positive tilt adds air
and removes body; negative tilt does the reverse. pivot and range decide where
and how strongly it leans. drive adds colour; wet trim and mix control the result.
**zero** resets tilt only, not every control.

**try:** `play → tilt → comp` to rebalance a loop, or move tilt gently with a
slow sigil for a broad tonal change rather than a resonant sweep.

#### mouth

**vowel filter.** three formant resonances give incoming audio a voice-like
shape without changing its source pitch. slide continuously through a/e/i/o/u;
size shifts the resonances, resonance sharpens them, and mix/out balance the effect.
this shapes a sound, rather than generating speech.

**try:** `osc → mouth → vca`; a harmonically rich oscillator and a moving vowel
make a more pronounced talking sound than a pure sine.

#### fold

**wavefolder.** folds a waveform back on itself to create extra harmonics.
drive pushes into the shaping, morph moves among fold/wrap/diode character,
and bias makes it asymmetric. current cards add smooth and tone, with trim/mix
to balance the processed signal.

**try:** `osc → fold → filt → vca`; start with a sine or triangle and slowly
raise drive to hear a simple tone acquire edges.

#### strain

**feedback distortion.** drive, bias, body and teeth combine saturation,
folding and tone shaping; feedback feeds the result back into the processor.
current cards add motion and its rate, plus wet trim. **strain** is distortion;
**swarm** is the live granular effect.

**try:** `motor → strain → tilt → vca`. begin with low feedback and output
level: extreme settings can keep sounding after the source stops.

#### bit

**bit and sample-rate reduction.** bits coarsens amplitude; rate slows the
internal capture clock without changing the engine's sample rate. drive and
tone shape the roughness. current cards add clock variation, timed dropout,
dither and further controls in setup; mix keeps some original signal.

**try:** `inst → bit → dly` for broken drum-machine textures. automate rate
for changing grit while keeping enough dry signal to preserve the attack.

#### ring

**ring modulation.** multiplies input by a carrier, creating metallic sidebands.
a slow carrier makes pulses; faster settings produce clangorous tones. depth
and mix are different: depth changes modulation, mix blends the finished effect.
current cards add carrier waves, free/note tuning, drive and follow-time control.
follow reacts to input level; it is not pitch tracking.

**try:** `play → ring → rev` on a voice or percussion sample. tune the carrier
by ear; note mode sets the carrier's pitch, not the output's musical key.

#### shift

**frequency shifter.** moves every frequency by the same number of hertz, not
by a shared semitone interval. small shifts create beating against the dry sound;
larger ones pull harmonics apart. feedback repeats the shift, damp darkens its
return, and **fine** makes small changes easier. **zero** is not bypass.

**try:** `inst → shift → rev` with a small shift and some dry mix for moving,
inharmonic tails. use bypass or zero mix when you need the unchanged input.

#### comp

**compressor.** reduces peaks in the incoming chain. lower threshold to catch
more signal, then adjust ratio, attack and release; makeup restores wet level
and mix allows parallel compression. detail contains knee, detector high-pass
and free/synced release. it is not a lookahead limiter or external sidechain.

**try:** `play → comp → rec` to capture a more even loop. compare at similar
output levels rather than judging the louder version as automatically better.

#### vca

**triggered gain envelope.** attack, hold and decay open and close the incoming
sound; gain, floor and curves shape the result. **duck** reverses the action.
current setup adds free/synced stage lengths, soft/reset/ignore retriggering and
one-shot/cycle operation. cycle needs a trigger; it does not start just by loading.

**try:** `osc → filt → vca` and sequence hits for shaped notes. put `dly` after
the VCA for tails that outlast each envelope, or before it to chop the echoes too.

### time

#### chor

**two-voice chorus.** short modulated delays thicken incoming sound. rate and
depth set the motion, mix sets its amount, and spread changes the relative phase
of the two delay voices; it is not stereo width. **hold dry** temporarily compares
the unchanged input without rewriting your settings.

**try:** `inst → chor → rev` with a restrained mix for a less static sustained
part, or turn up depth for an obviously wavering texture.

#### dly

**delay.** repeating echoes with time, feedback and mix. wow and saturation
colour the repeats; current cards add feedback tone and tempo sync with note
divisions/feel. tape time changes bend pitch; clean changes crossfade between
times. hold recirculates the buffer but is not a lossless freeze; clear empties it.

**try:** `fm → dly → rev`; sync the delay to a dotted division so a few short
strikes become an interlocking pattern.

#### rev

**reverb.** a diffuse tail around the incoming sound. size, decay, damp and mix
set the space; current cards also offer predelay, send low-cut and local wet
ducking. freeze holds the reverb state rather than saving a recording. use rec
or bounce if you want to keep the result as a wav.

**try:** `bird → rev → rec` for a small call turned into sample material, or
put the reverb before a VCA to shape the whole tail with each trigger.

#### ghost

**resonant memory.** live input excites four drifting delay wells rather than
being played from a sample file. root/fine tune them, decay feeds the memory,
smear darkens its return, and drift/cycle move it. freeze seals the existing
memory against fresh input. use it as an audible layer of resonance, not just
a background room.

**try:** `mic → ghost → rec`; feed it a short sound, let the tail develop,
and record a fragment to reuse in play or gran.

#### swarm

**live granular processor.** turns recent incoming audio into overlapping grains.
position is how far back to read; size, rate, spray and pitch shape the swarm.
freeze retains history and brings active grains to full wet; **hold**, in setup,
instead stops new grain launches and lets existing ones finish. setup also adds
reverse, sync, pitch envelope and cluster. unlike **gran**, it needs incoming
audio history, not a wav.

**try:** `inst → swarm → rev`; play a few notes into the buffer, freeze it,
then move position and pitch to play the captured texture.

#### fract

**fragment repeater.** press the waveform to repeat recent input. pull its right
boundary left to shrink a temporary reading window, then right to re-expand it;
start near the right edge for full stylus travel. lift clears squeeze and returns
live, or the underlying fragment when the separate **hold** is on. the gesture
does not edit saved damage. size chooses the next capture; speed includes reverse,
and the separate damage control still progressively shortens the underlying loop.
chance/length allow automatic interventions; chance starts at zero.

the squeeze minimum is **62.5 ms**, or the full loop if it is already shorter;
short fragments are not expanded to reach that minimum. hold is independent of
the manual grab, and captured audio/temporary squeeze is not saved in a project.

**try:** `play → fract → filt → dly`; grab near the right waveform edge, squeeze
and release. the sequencer keeps running.

#### glitch

**bar rearranger.** alters a sixteen-cell, 4/4 pattern using slices of the previous
complete bar. density sets how often it intervenes; jump, repeat, drop, reverse
and pitch are relative operation weights, not separate effect amounts. **keep**
retains the pattern, not the audio; **reroll** requests another pattern.

**try:** `play → glitch → comp` on a running loop. allow a complete bar of
history first; keep a pattern you like while the source continues changing.

### tools

#### arp

**clocked note generator.** place it before the source it should play, then start
transport. it targets the first eligible, non-bypassed source to its right in
that chain: osc, fm, pent, bird, play, inst, paul, or a current note-mode motor. gran,
scrt, rule, rot, noise and mic are not targets. incoming audio passes through; arp is not a
sound source.

rate, gate, order and octaves shape the phrase; **note 1–4** set relative offsets
and can be switched off individually. it can also retrigger eligible downstream
VCAs. it does not play all sources at once or send notes to another chain.

**try:** `arp → inst → vca → dly`; choose an instrument, start transport and
adjust rate/gate. put the arp before the source, not after the effects.

#### rec

**in-chain recorder.** captures the audio arriving at its own position, with
length, monitor and workspace-slot controls. **save wav** exports a take;
**> play / > gran** saves it before inserting playback when there is room.
monitor controls pass-through, not whether recording happens.

**try:** `mic → mouth → rec` to keep a processed voice, then send the take to
play and sequence it. use [bounce](#bounce) for the master mix or bar-aligned
chain loops; see [recording](#microphone-and-recording) for finishing/saving takes.
