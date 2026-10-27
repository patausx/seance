#pragma once

namespace seance::platform::ui3ds::seq_layout {
// Eight-pixel side margins; 19x31 touch bands, 18px cut-corner squares.
constexpr int PAGE_STEPS = 16;
constexpr int X0 = 8, PITCH = 19, CELL_W = 18, CELL_H = 31, BADGE_H = 18;
constexpr int ROWS[4] = {45, 83, 121, 159};
constexpr int EDITOR_Y = 194;
// Ordinary NOTE / LEVEL / TIME rail: equal side margins, exact middle at x160.
// Score cells, categorical editors and the inline LOCK/SLICE rails do not move.
constexpr int VALUE_X0 = 38, VALUE_X1 = 282, VALUE_TRAVEL = VALUE_X1 - VALUE_X0, VALUE_Y = 218;
// Eight equal 45-degree directions, all nodes and labels inside one circle.
// Existing mode IDs/chords keep their meaning: NW,N,E,SE,NE,W,SW,S.
constexpr int RING_X = 160, RING_Y = 110, RING_RADIUS = 100;
constexpr int NODE_RADIUS = 72, NODE_DIAGONAL = 51;
constexpr int MODE_COUNT = 8;
constexpr int SPOKE_X[MODE_COUNT] = {109, 160, 232, 211, 211, 88, 109, 160};
constexpr int SPOKE_Y[MODE_COUNT] = {59, 38, 110, 161, 59, 110, 161, 182};
constexpr int MODE_X[MODE_COUNT] = {85, 136, 208, 187, 187, 64, 85, 136};
constexpr int MODE_Y[MODE_COUNT] = {39, 18, 90, 151, 39, 90, 151, 172};
constexpr int MODE_W = 48, MODE_H = 34;
constexpr int mode_label_y(int mode) {
    return SPOKE_Y[mode] + (SPOKE_Y[mode] <= RING_Y ? -17 : 10);
}
} // namespace seance::platform::ui3ds::seq_layout
