#pragma once
#include <cstdint>
#include "engine.h"

namespace seance {

// Portable patch metadata for the spatial UI. It deliberately has no platform,
// renderer, camera, or gesture state, so patch I/O and host tests can own it.
struct WorkspaceNodePosition {
    int16_t x = 0;
    int16_t y = 0;
};

struct WorkspaceLayout {
    static constexpr int WIDTH = 520;
    static constexpr int HEIGHT = 300;
    static constexpr int NODE_WIDTH = 58;
    static constexpr int NODE_HEIGHT = 36;

    WorkspaceNodePosition node[NUM_CHAINS][UNITS_PER_CHAIN];
};

void workspace_layout_defaults(WorkspaceLayout& layout);
bool workspace_layout_valid(const WorkspaceLayout& layout);

} // namespace seance
