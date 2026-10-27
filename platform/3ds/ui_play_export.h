#pragma once
#include <cstdint>
namespace seance { class PlayUnit; }
namespace seance::platform::ui3ds {
struct PlayExportOptions {
    uint32_t first = 0, end = 0;
    bool normalize = false, dc = false, bake = false, reverse = false, detach = false;
    bool crop = false; // raw projected trim -> new source, retarget only this PLAY
};
// Call on UI thread OUTSIDE AudioOut::Guard. Returns a stable notice until next
// call. Copy creates a WAV/new slot without retargeting PLAY. Detach duplicates
// the entire unprocessed source and retargets only this PLAY, preserving edits.
// Crop keeps the projected whole trim, rebases exact edits/cut IDs, ignores copy
// processing flags and preserves originals. UI owns source-switch undo.
const char* play_export_copy(PlayUnit& play, const PlayExportOptions& options);
}
