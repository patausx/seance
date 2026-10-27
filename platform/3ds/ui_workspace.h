#pragma once
#include <cstdint>
#include "../../core/engine/workspace_layout.h"

namespace seance::platform::ui3ds {

// UI-only bounded patch-table coordinates. The DSP remains the existing linear
// six-unit chain; these positions describe the user's spatial arrangement.
using WorkspacePoint = WorkspaceNodePosition;

constexpr int WORKSPACE_W = WorkspaceLayout::WIDTH;
constexpr int WORKSPACE_H = WorkspaceLayout::HEIGHT;
constexpr int WORKSPACE_VIEW_Y = 74;
constexpr int WORKSPACE_VIEW_H = 126; // fixed add/capacity row below; world coordinates unchanged
constexpr int WORKSPACE_NODE_W = WorkspaceLayout::NODE_WIDTH;
constexpr int WORKSPACE_NODE_H = WorkspaceLayout::NODE_HEIGHT;

const WorkspacePoint& workspace_point(int chain, int slot);
void workspace_set_point(int chain, int slot, int x, int y);
void workspace_reset_all();
void workspace_reset_chain(int chain);
void workspace_get_layout(WorkspaceLayout& out);
void workspace_apply_layout(const WorkspaceLayout& layout);
void workspace_shift_slots_right(int chain, int first_slot, int empty_slot);
void workspace_move_slot(int chain, int from_slot, int to_slot);
void workspace_remove_slot(int chain, int slot);

int workspace_camera_x(int chain);
int workspace_camera_y(int chain);
void workspace_set_camera(int chain, int x, int y);

int workspace_screen_x(int chain, int slot);
int workspace_screen_y(int chain, int slot);

} // namespace seance::platform::ui3ds
