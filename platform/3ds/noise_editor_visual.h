#pragma once
#include "module_frame_layout.h"

namespace seance::platform::ui3ds::noise_editor_visual {
namespace f=module_frame_layout;
constexpr int kLeft=f::kLeft,kRight=f::right_edge(f::kBottomWidth);
constexpr int kColumns[2]={14,172},kEnds[2]={148,306};
constexpr int kRows[4]={91,125,159,193};
constexpr int kGraphLeft=34,kGraphRight=386,kGraphTop=72,kGraphBottom=135;
constexpr int kResponsePoints=89,kEnvelopePoints=89;
constexpr f::Rect kSetup={198,0,48,26};
constexpr f::Rect kChoice[4]={{8,29,66,27},{83,29,64,27},{154,29,79,27},{246,29,66,27}};
constexpr f::Rect kMode[2]={{8,58,44,24},{58,58,55,24}};
constexpr f::Rect kSetupControls[2]={{8,68,304,40},{8,143,304,40}};
constexpr bool contains(f::Rect r,int x,int y){return x>=r.x&&x<r.right()&&y>=r.y&&y<r.bottom();}
static_assert(kGraphRight<f::kTopWidth&&kRows[3]+27<f::kFooterTextY);
} // namespace seance::platform::ui3ds::noise_editor_visual
