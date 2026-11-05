#pragma once
// Private PLAY primary grips: source-time projection, invisible padded targets,
// and stable chronological cut-seam ownership. No allocation or authored writes.
#include "play_editor_layout.h"
#include "../../core/ui/play_editor_tools.h"
#include <algorithm>
#include <cstdint>
namespace seance::platform::ui3ds::play_editor_edges {
namespace pl=play_editor_layout::precision;
inline int project(uint32_t at,uint32_t first,uint32_t span){
    if(!span)return pl::left;
    const int64_t offset=std::clamp<int64_t>(int64_t(at)-first,0,span);
    return pl::left+int(uint64_t(offset)*pl::width/span);
}
struct Handles {
    play_editor_layout::Box in{},out{};
    bool has_in=false,has_out=false,overlap=false;
    int a=0,b=0;
    int hit(int x,int y)const {
        if(has_in&&in.contains(x,y))return 0;
        if(has_out&&out.contains(x,y))return 1;
        return -1;
    }
};
inline Handles handles(uint32_t start,uint32_t end,uint32_t first,uint32_t span,bool part_labels=false){
    Handles h;h.a=project(start,first,span);h.b=project(end,first,span);
    h.has_in=span&&start>=first&&uint64_t(start)<=uint64_t(first)+span;
    h.has_out=span&&end>=first&&uint64_t(end)<=uint64_t(first)+span;
    // Keep the entire 24px target inside the existing broad strip, even at a
    // real source/view boundary. Offscreen ends have no target, not a clipped one.
    const int a=std::clamp(h.a-12,pl::left,pl::left+pl::width-24);
    const int b=std::clamp(h.b-12,pl::left,pl::left+pl::width-24);
    h.overlap=h.has_in&&h.has_out&&a<b+24&&b<a+24;
    // Chop labels own 60..84. The remaining24px grip lane honestly splits into
    // two12px lanes for coincident ends. Trim/loop retain full24px split lanes.
    // No offscreen/selected-edge fallback, including a two-frame source.
    const int top=part_labels?84:60,mid=part_labels?96:84;
    h.in={a,top,a+24,h.overlap?mid:108};
    h.out={b,h.overlap?mid:top,b+24,108};
    return h;
}
inline int cut_target(const seance::play_editor::Cuts&c,int focus,bool end,uint32_t frames){
    if(!c.valid(focus,frames))return -1;
    // Imported duplicates, sub-two-frame ranges or out-of-source seams are
    // ambiguous. Refuse instead of silently choosing an array-order owner.
    for(int i=0;i<seance::play_editor::CUTS;++i){
        if(c.at[i]==UINT32_MAX)continue;
        if(!c.valid(i,frames))return -1;
        for(int j=0;j<i;++j)if(c.at[j]==c.at[i])return -1;
    }
    int target=focus;
    if(end){
        target=-1;const uint32_t boundary=c.end(focus,frames);
        for(int i=0;i<seance::play_editor::CUTS;++i)if(c.at[i]==boundary)target=i;
        if(target<0)return -1; // last cut end is the fixed source endpoint
    }
    bool left=false;
    for(auto at:c.at)if(at<c.at[target])left=true;
    return left?target:-1; // first chronological cut start is fixed by Cuts::move
}
} // namespace seance::platform::ui3ds::play_editor_edges
