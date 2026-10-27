# menu audio assets

- `source/1.wav`: menu drone; `source/2.wav`: entry sound. the original WAVs and
  prepared PCM are included here. normal builds use the checked-in PCM.
- `menu_drone.bin`: stereo signed 16-bit little-endian PCM, 32 kHz, 832000 frames
  (26 seconds). The original 30-second drone has a four-second equal-power tail /
  head overlap, rotated so its wrap continues into the original source at 4s.
  There is no silence pad or per-cycle fade to zero.
- `menu_entry.bin`: same PCM format, 108976 frames (3.4055 seconds), constant DC
  removed, no upward normalization, 1 dB ceiling, 12ms attack / 180ms end taper.
- Original WAVs are not linked. The two `.bin` files add 3763904 bytes (~3.59 MiB)
  of immutable PCM to the executable. `manifest.json` records hashes and bounds.

Rebuild only when intentionally changing the source/preparation:

```sh
python3 tools/prepare_menu_audio.py
```

Requires Python's standard library and ffmpeg; application builds need neither.
The Makefile links prepared `.bin` files with devkitPro `bin2o`. Playback levels,
fades and integration are documented in `docs/menu-audio.md`. This is prepared
source audio, not a console capture or a claim of a listening-approved loop.
