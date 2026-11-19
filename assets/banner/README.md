# home menu banner

`assets/banner.bnr` is the 3DS HOME Menu CBMD banner embedded into CIA and CCI
builds by `makerom -banner`.

## contents

- 3D eclipse ring, halo and wordmark scene (`animation-hold.blend`, 1156
  triangles, 4-second non-looping intro animation in CGFX);
- source textures: `halo.png` and `wordmark-alpha.png`;
- stereo 2.95-second banner audio (`seance-whisper-dark.wav`, 44.1 kHz PCM16,
  two channels as required by CBMD).

normal 3DS builds use the checked-in `assets/banner.bnr` directly and do not
require Blender, pycgfx or bannertool.
