#include "workspace_layout.h"

namespace seance {
namespace {

constexpr WorkspaceNodePosition DEFAULTS[UNITS_PER_CHAIN] = {
    { 28,  44 },
    { 94, 164 },
    {206, 124 },
    {316,  42 },
    {390, 154 },
    {458,  78 },
};

} // namespace

void workspace_layout_defaults(WorkspaceLayout& layout) {
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot)
            layout.node[chain][slot] = DEFAULTS[slot];
}

bool workspace_layout_valid(const WorkspaceLayout& layout) {
    for (int chain = 0; chain < NUM_CHAINS; ++chain)
        for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
            const WorkspaceNodePosition& point = layout.node[chain][slot];
            if (point.x < 4 || point.x > WorkspaceLayout::WIDTH - WorkspaceLayout::NODE_WIDTH - 4 ||
                point.y < 4 || point.y > WorkspaceLayout::HEIGHT - WorkspaceLayout::NODE_HEIGHT - 4)
                return false;
        }
    return true;
}

} // namespace seance
