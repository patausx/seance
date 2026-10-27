#pragma once
#include "module_frame_layout.h"

namespace seance::platform::ui3ds::motor_editor_visual {
constexpr int kCols=354, kPlotLeft=32, kPlotRight=kPlotLeft+kCols-1;
constexpr int kPlotTop=76, kPlotZero=106, kPlotBottom=136;
constexpr int kSpectrumTop=168, kSpectrumBottom=207;
constexpr int kLeft=14, kRight=172, kRailLength=134;
constexpr int kLabelY[4]={72,112,152,192};
constexpr int kRailY[4]={93,133,173,213};
constexpr int kHitY[4]={61,101,141,181};
static_assert(module_frame_layout::kLeft==14 && module_frame_layout::kHeaderTextY==14 &&
              module_frame_layout::kFooterTextY==228,"MOTOR uses the shared frame");
static_assert(kRailY[3]<module_frame_layout::kFooterTextY-4 && kPlotRight<400 && kSpectrumBottom<228,
              "MOTOR body belongs inside the shared frame");
}
