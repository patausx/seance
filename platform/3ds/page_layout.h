#pragma once
// Page titles share one ink-left edge and one font baseline on either LCD.
// These are NOT tight bitmap-top coordinates: ascenders/descenders retain the
// original 28ppem font size. No scaling, new masks, state, textures or allocation.
#include <cstdint>

namespace seance::platform::ui3ds::page_layout {
constexpr int kLeft = 20;
constexpr int kRightInset = 20;
constexpr int kBaseline = 32;
constexpr int kTop = 12;
constexpr int kBottom = 40;
// Eight patch slots fit below the title without changing the 147x29 targets,
// bottom action row, slot order or number of visible rows.
constexpr int patch_row_y(int slot) { return 44 + (slot / 2) * 32; }

// Stable indices into the existing menu_labels table; masks stay byte-exact.
enum class Title : uint8_t {
    PatchTable = 0, GoTo = 1, AddModule = 2,
    Sigils = 24, Tempo = 25, Swing = 26, Patches = 27, UnsavedChanges = 28,
    Samples = 32, Bounce = 40, ReplaceChain = 47, DiscardTake = 48,
    Perform = 50, Sequencer = 51
};

constexpr int title_y(Title title) {
    // Original Gossip-MedPixel.otf at 28ppem, threshold 128, baseline anchor.
    // Baseline offsets follow the original font's ascenders and descenders.
    switch (title) {
        case Title::GoTo: case Title::Tempo: return kBaseline - 15;
        case Title::AddModule: case Title::Sigils: case Title::Swing:
        case Title::Samples: return kBaseline - 16;
        case Title::Perform: return kBaseline - 17;
        case Title::Sequencer: return kBaseline - 12;
        default: return kBaseline - 19;
    }
}
} // namespace seance::platform::ui3ds::page_layout
