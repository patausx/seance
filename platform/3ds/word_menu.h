#pragma once
// Shared sigil word-disclosure grammar. UI-only motion is captured outside the
// audio guard; drawing consumes Frame only. No clocks, engine, heap or textures
// here; mutually exclusive consumers reuse sigil_surface::bottom_cache.
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace seance::platform::ui3ds::word_menu {
constexpr unsigned kRevealMs=140, kFocusMs=100, kStallMs=200;
struct Layout {int branch=0,x=0,y=0,step=0,left=0,right=0;};
struct State {
    uint64_t last=0,opened=0,focused=0;
    unsigned identity=0;
    int item=-1;
    float from_y=0,from_width=0,y=0,width=0;
};
struct Frame {unsigned reveal=255;int underline_y=0,underline_width=0;};
static_assert(std::is_trivially_copyable<State>::value&&sizeof(State)<=48,"bounded pointer-free word-menu state");
static_assert(std::is_trivially_copyable<Frame>::value&&sizeof(Frame)<=12,"pointer-free word-menu motion");
inline uint32_t faded(uint32_t color,unsigned alpha){return (color&0xffffffu)|(((color>>24)*alpha/255)<<24);}
inline float ease(uint64_t now,uint64_t start,unsigned duration){
    const float t=now<start?1.f:std::min(1.f,float(now-start)/duration);
    return t*t*(3.f-2.f*t);
}
// identity=0 closes immediately; opening restarts only disclosure/focus motion.
// label must return the current frozen label for each valid row, not live data.
template<class Label>
Frame capture(State& s,uint64_t now,unsigned identity,int item,const Layout& l,Label label,bool opening=false){
    // Original sigil reset clears the active identity, not retained closed
    // underline values; the next active menu initializes them immediately.
    if(now<s.last||now-s.last>kStallMs)s.identity=0;
    Frame out{};
    if(identity){
        const float target_y=l.y+item*l.step+12;
        const float target_width=ui::text_w(label(item))-1;
        if(opening||s.identity!=identity){
            s.opened=s.focused=now;s.item=item;
            s.from_y=s.y=target_y;s.from_width=s.width=target_width;
        }else{
            const float t=ease(now,s.focused,kFocusMs);
            const float old_y=l.y+s.item*l.step+12;
            const float old_width=ui::text_w(label(s.item))-1;
            s.y=s.from_y+(old_y-s.from_y)*t;
            s.width=s.from_width+(old_width-s.from_width)*t;
            if(s.item!=item){s.from_y=s.y;s.from_width=s.width;s.focused=now;s.item=item;}
        }
        out.reveal=unsigned(ease(now,s.opened,kRevealMs)*255);
        out.underline_y=int(s.y+.5f);out.underline_width=int(s.width+.5f);
    }
    s.identity=identity;s.last=now;
    return out;
}
// Exact sigil tree/text/underline law. Consumer redraws its own sharp anchor
// first and may add native per-row details through extra(row,y,alpha,selected).
template<class Painter,class Label,class Extra>
void draw(const Painter& p,const Layout& l,const Frame& f,int count,int selected,
          Label label,Extra extra,uint32_t ink,uint32_t spot,uint32_t branch){
    if(count<=0)return;
    const int reveal=int(f.reveal),root=l.y+selected*l.step+4;
    const int first=root+(l.y+4-root)*reveal/255;
    const int last=root+(l.y+(count-1)*l.step+4-root)*reveal/255;
    p.line(l.branch,first,l.branch,last,faded(branch,unsigned(reveal)));
    const unsigned alpha=unsigned(40+215*reveal/255);
    for(int i=0;i<count;++i){
        const bool chosen=i==selected;const auto color=chosen?spot:ink;const int y=l.y+i*l.step;
        if(y+4>=first&&y+4<=last)p.line(l.branch,y+4,l.branch+(l.x-7-l.branch)*reveal/255,y+4,
                                      faded(chosen?color:branch,unsigned(reveal)));
        p.text(l.x,y,label(i),faded(color,alpha));extra(i,y,alpha,chosen);
    }
    p.line(l.x,f.underline_y,l.x+f.underline_width,f.underline_y,faded(spot,unsigned(40+215*reveal/255)));
}
} // namespace seance::platform::ui3ds::word_menu
