#pragma once
#include "module_frame_layout.h"

namespace seance::platform::ui3ds::swarm_layout {
using Rect = module_frame_layout::Rect;
constexpr Rect setup{164,0,48,24};
constexpr Rect bypass{224,0,33,22};
constexpr Rect freeze{108,28,104,28};
constexpr Rect time(int col) { return {col ? 166 : 8,56,146,81}; }
constexpr int time_left(int col) { return col ? 172 : 14; }
constexpr int time_right(int col) { return col ? 306 : 148; }
constexpr int center(int slot) { return 48+75*slot; }
constexpr Rect fader(int slot) { return {center(slot)-36,140,72,80}; }
constexpr int time_rail=126, fader_top=164, fader_bottom=198;
constexpr Rect extra(int col,int row) { return {col ? 166 : 8,row ? 109 : 66,146,43}; }
constexpr Rect hold{8,157,100,28}, shape{112,157,106,28}, span{224,157,88,28};
constexpr Rect step{8,190,100,30}, fine{112,190,112,30}, zero{236,190,76,30};
constexpr bool contains(Rect r,int x,int y) { return x>=r.x&&x<r.right()&&y>=r.y&&y<r.bottom(); }
static_assert(time(0).right()<time(1).x && time(1).right()==312);
static_assert(freeze.bottom()==time(0).y && time(0).bottom()<fader(0).y);
static_assert(fader(3).right()<=312 && fader(3).bottom()<=224);
static_assert(time_right(1)==module_frame_layout::right_edge(320));
} // namespace seance::platform::ui3ds::swarm_layout
