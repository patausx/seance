#pragma once
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::slice_boundaries {
// Integer rectangles avoid direction-dependent GPU line rasterization. The two
// ends use exactly the same closed-pixel rails, with inward caps even at edges.
inline void selected(int begin,int end,int top,int bottom,uint32_t color) {
    if(end<begin)return;
    const int cap=end-begin<4?end-begin+1:5;
    ui::rect(begin,top,1,bottom-top+1,color);
    ui::rect(end,top,1,bottom-top+1,color);
    ui::rect(begin,top,cap,1,color);ui::rect(begin,bottom,cap,1,color);
    ui::rect(end-cap+1,top,cap,1,color);ui::rect(end-cap+1,bottom,cap,1,color);
}
}
