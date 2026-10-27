#pragma once

// Module-editor captions on either 240px LCD: 8px text with four clear
// pixels below. This is a render-only anchor, not a touch target or a new bar.
#include "module_frame_layout.h"
namespace seance::platform::ui3ds::module_footer_layout {
using module_frame_layout::kScreenHeight;
using module_frame_layout::kTextHeight;
using module_frame_layout::kBottomInset;
constexpr int kTextY = module_frame_layout::kFooterTextY;
// The generic legacy editor already has a dark footer band; keep two pixels
// above its glyphs as well as the same four-pixel bottom inset.
constexpr int kLegacyBandTop = kTextY - 2;
}
