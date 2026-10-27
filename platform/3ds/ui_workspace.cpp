#include "ui_workspace.h"

namespace seance::platform::ui3ds {
namespace {

WorkspaceLayout g_layout;
int16_t g_camera_x[NUM_CHAINS] = {};
int16_t g_camera_y[NUM_CHAINS] = {};
bool g_initialized = false;

int clamp_i(int value, int lo, int hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

void ensure_layout() {
    if (g_initialized) return;
    workspace_layout_defaults(g_layout);
    g_initialized = true;
}

} // namespace

const WorkspacePoint& workspace_point(int chain, int slot) {
    ensure_layout();
    return g_layout.node[clamp_i(chain, 0, NUM_CHAINS - 1)]
                        [clamp_i(slot, 0, UNITS_PER_CHAIN - 1)];
}

void workspace_set_point(int chain, int slot, int x, int y) {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN) return;
    ensure_layout();
    g_layout.node[chain][slot].x = (int16_t)clamp_i(x, 4, WORKSPACE_W - WORKSPACE_NODE_W - 4);
    g_layout.node[chain][slot].y = (int16_t)clamp_i(y, 4, WORKSPACE_H - WORKSPACE_NODE_H - 4);
}

void workspace_reset_all() {
    workspace_layout_defaults(g_layout);
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        g_camera_x[chain] = 0;
        g_camera_y[chain] = 0;
    }
    g_initialized = true;
}

void workspace_reset_chain(int chain) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    WorkspaceLayout defaults;
    workspace_layout_defaults(defaults);
    ensure_layout();
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot)
        g_layout.node[chain][slot] = defaults.node[chain][slot];
    g_camera_x[chain] = 0;
    g_camera_y[chain] = 0;
}

void workspace_get_layout(WorkspaceLayout& out) {
    ensure_layout();
    out = g_layout;
}

void workspace_apply_layout(const WorkspaceLayout& layout) {
    if (!workspace_layout_valid(layout)) return;
    g_layout = layout;
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        g_camera_x[chain] = 0;
        g_camera_y[chain] = 0;
    }
    g_initialized = true;
}

void workspace_shift_slots_right(int chain, int first_slot, int empty_slot) {
    if (chain < 0 || chain >= NUM_CHAINS || first_slot < 0 ||
        first_slot >= UNITS_PER_CHAIN || empty_slot <= first_slot ||
        empty_slot >= UNITS_PER_CHAIN) return;
    ensure_layout();
    for (int slot = empty_slot; slot > first_slot; --slot)
        g_layout.node[chain][slot] = g_layout.node[chain][slot - 1];
    const WorkspacePoint& source = g_layout.node[chain][first_slot - 1 >= 0 ? first_slot - 1 : 0];
    workspace_set_point(chain, first_slot, source.x + 82, source.y + 48);
}

void workspace_move_slot(int chain, int from_slot, int to_slot) {
    if (chain < 0 || chain >= NUM_CHAINS || from_slot < 0 ||
        from_slot >= UNITS_PER_CHAIN || to_slot < 0 ||
        to_slot >= UNITS_PER_CHAIN || from_slot == to_slot) return;
    ensure_layout();
    const WorkspacePoint moved = g_layout.node[chain][from_slot];
    if (to_slot < from_slot) {
        for (int slot = from_slot; slot > to_slot; --slot)
            g_layout.node[chain][slot] = g_layout.node[chain][slot - 1];
    } else {
        for (int slot = from_slot; slot < to_slot; ++slot)
            g_layout.node[chain][slot] = g_layout.node[chain][slot + 1];
    }
    g_layout.node[chain][to_slot] = moved;
}

void workspace_remove_slot(int chain, int slot) {
    if (chain < 0 || chain >= NUM_CHAINS || slot < 0 || slot >= UNITS_PER_CHAIN) return;
    ensure_layout();
    for (int index = slot; index + 1 < UNITS_PER_CHAIN; ++index)
        g_layout.node[chain][index] = g_layout.node[chain][index + 1];
    WorkspaceLayout defaults;
    workspace_layout_defaults(defaults);
    g_layout.node[chain][UNITS_PER_CHAIN - 1] = defaults.node[chain][UNITS_PER_CHAIN - 1];
}

int workspace_camera_x(int chain) {
    ensure_layout();
    return g_camera_x[clamp_i(chain, 0, NUM_CHAINS - 1)];
}

int workspace_camera_y(int chain) {
    ensure_layout();
    return g_camera_y[clamp_i(chain, 0, NUM_CHAINS - 1)];
}

void workspace_set_camera(int chain, int x, int y) {
    if (chain < 0 || chain >= NUM_CHAINS) return;
    ensure_layout();
    g_camera_x[chain] = (int16_t)clamp_i(x, 0, WORKSPACE_W - 320);
    g_camera_y[chain] = (int16_t)clamp_i(y, 0, WORKSPACE_H - WORKSPACE_VIEW_H);
}

int workspace_screen_x(int chain, int slot) {
    return workspace_point(chain, slot).x - workspace_camera_x(chain);
}

int workspace_screen_y(int chain, int slot) {
    return WORKSPACE_VIEW_Y + workspace_point(chain, slot).y - workspace_camera_y(chain);
}

} // namespace seance::platform::ui3ds
