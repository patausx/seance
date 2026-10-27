#pragma once
// Approved lancet drawing, not a controller or a parameter mapping. Callers keep
// their existing rail anchors and pass already-mapped, frozen pixel positions.
// No engine/clock/heap access, no touch targets, no modulation simulation.
#include <algorithm>
#include <cstdint>
#include "ui_palette.h"
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::module_fader {
struct State {
    bool linked=false;   // an enabled route: retain the live tick even at equality
    bool disabled=false;
    bool quiet=false;    // bypass/mute: keep truthful positions, suppress accent
};
namespace detail {
struct Pen {
    int x,y;
    bool vertical;
    void block(int u,int v,int w,int h,uint32_t color) const {
        if(vertical)ui::rect(x+v,y-u-w+1,h,w,color);
        else ui::rect(x+u,y+v,w,h,color);
    }
};
inline int half_width(int v,bool compact) {
    const int a=v<0?-v:v;
    if(compact)return a==0?2:a<3?1:0;
    return a==0?3:a<3?2:a<5?1:0;
}
template<class DrawingPen>
inline void diamond(const DrawingPen& pen,int at,bool filled,uint32_t color,bool compact) {
    // Coalesce equal pixel rows. These are native rects, not antialiased quads;
    // no opaque paper erasure is needed on the live/gradient paper underneath.
    const int radius=compact?3:5;
    for(int v=-radius;v<=radius;) {
        const int half=half_width(v,compact);
        int end=v+1;
        while(end<=radius && half_width(end,compact)==half)++end;
        if(filled || !half)pen.block(at-half,v,2*half+1,end-v,color);
        else {
            pen.block(at-half,v,1,end-v,color);
            pen.block(at+half,v,1,end-v,color);
        }
        v=end;
    }
}
template<class DrawingPen>
inline void draw(const DrawingPen& pen,int length,int edit,int live,State state,int neutral,bool compact) {
    if(length<=0)return;
    edit=std::max(0,std::min(length,edit));
    live=std::max(0,std::min(length,live));
    const uint32_t rail=palette::subdued();
    const uint32_t edit_ink=state.disabled?palette::menu_ghost():palette::ink();
    const uint32_t live_ink=state.quiet||state.disabled?palette::menu_ghost():palette::spot();
    const int radius=compact?3:5;
    const int hole=compact?1:2;
    // Leave the inside of the edit diamond transparent. All old paper/texture
    // pixels remain intact; no flat rectangle can cut a hole in the paper field.
    const auto axis=[&](int first,int last,uint32_t color) {
        if(first>last)std::swap(first,last);
        const int a=std::min(last,edit-hole-1),b=std::max(first,edit+hole+1);
        if(a>=first)pen.block(first,0,a-first+1,1,color);
        if(b<=last)pen.block(b,0,last-b+1,1,color);
    };
    const auto tick=[&](int u,int v,uint32_t color) {
        // End/neutral marks cannot shine through the hollow edit handle.
        const int distance=u>edit?u-edit:edit-u;
        if(v>=-radius && v<=radius && distance<=half_width(v,compact))return;
        pen.block(u,v,1,1,color);
    };
    axis(0,length,rail);
    for(int side : {-1,1}) {
        const int end=side<0?0:length;
        for(int v=-2;v<=2;++v) {
            const int a=v<0?-v:v;
            tick(end+side*(2-a),v,rail);
        }
    }
    if(neutral>=0 && neutral<=length)
        for(int v=-3;v<=3;++v)tick(neutral,v,rail);
    const bool show_live=state.linked || live!=edit;
    if(show_live && live!=edit)axis(edit,live,live_ink);
    diamond(pen,edit,false,edit_ink,compact);
    if(show_live) {
        const int distance=live>edit?live-edit:edit-live;
        if(distance<=4)pen.block(live,-2,1,5,live_ink);
        else diamond(pen,live,true,live_ink,true);
    }
}
} // namespace detail

// A fixed/inapplicable parameter may keep its existing rail-only hint.
namespace detail {
template<class DrawingPen>
inline void track(const DrawingPen& pen,int length) {
    if(length<=0)return;
    const uint32_t color=palette::subdued();
    pen.block(0,0,length+1,1,color);
    for(int side : {-1,1}) {
        const int end=side<0?0:length;
        for(int v=-2;v<=2;++v)pen.block(end+side*(2-(v<0?-v:v)),v,1,1,color);
    }
}
} // namespace detail
inline void horizontal_track(int x0,int x1,int y) {
    detail::track(detail::Pen{x0,y,false},x1-x0);
}
inline void horizontal(int x0,int x1,int y,int edit_x,int live_x,
                       State state={},int neutral_x=-1,bool compact=false) {
    detail::draw(detail::Pen{x0,y,false},x1-x0,edit_x-x0,live_x-x0,state,
                 neutral_x<0?-1:neutral_x-x0,compact);
}
inline void vertical(int x,int top,int bottom,int edit_y,int live_y,
                     State state={},int neutral_y=-1,bool compact=false) {
    detail::draw(detail::Pen{x,bottom,true},bottom-top,bottom-edit_y,bottom-live_y,state,
                 neutral_y<0?-1:bottom-neutral_y,compact);
}
// Historical needle geometry retained only for archived studies/test fixtures.
// Production sliders use the confirmed lancet above; source admission rejects
// needle_* calls outside this header. Do not reintroduce the needle in new UI.
namespace detail {
inline void needle(const Pen& pen,int at,int radius,bool hollow,uint32_t color) {
    // Study shape: one nine-pixel cross-axis stroke and a three-pixel square.
    // Separate edit/live keeps the existing hollow/solid vocabulary; no opaque
    // paper erasure, so the material and unrelated geometry never get flattened.
    pen.block(at,-radius,1,radius-1,color);
    pen.block(at,2,1,radius-1,color);
    if(hollow) {
        pen.block(at-1,-1,3,1,color);
        pen.block(at-1,1,3,1,color);
        pen.block(at-1,0,1,1,color);
        pen.block(at+1,0,1,1,color);
    } else pen.block(at-1,-1,3,3,color);
}
inline void needle_draw(const Pen& pen,int length,int edit,int live,State state,int neutral,bool compact) {
    if(length<=0)return;
    edit=std::max(0,std::min(length,edit));
    live=std::max(0,std::min(length,live));
    const bool show_live=state.linked || live!=edit;
    const uint32_t rail=palette::shadow();
    const uint32_t live_ink=state.quiet||state.disabled?palette::menu_ghost():palette::spot();
    const uint32_t edit_ink=state.disabled?palette::menu_ghost():show_live?palette::ink():live_ink;
    const auto axis=[&](int first,int last,uint32_t color) {
        if(first>last)std::swap(first,last);
        // The hollow edit center is real underlying paper, never filled rail.
        if(!show_live){pen.block(first,0,last-first+1,1,color);return;}
        const int a=std::min(last,edit-1),b=std::max(first,edit+1);
        if(a>=first)pen.block(first,0,a-first+1,1,color);
        if(b<=last)pen.block(b,0,last-b+1,1,color);
    };
    axis(0,length,rail);
    if(neutral>=0 && neutral<=length && (!show_live || neutral!=edit))
        pen.block(neutral,-3,1,7,rail);
    if(show_live && live!=edit)axis(edit,live,live_ink);
    needle(pen,edit,compact?3:4,show_live,edit_ink);
    // Equal linked values still show the short solid live marker, surrounded
    // by the taller base needle. No fake displacement, dropped route or motion.
    if(show_live)needle(pen,live,2,false,live_ink);
}
} // namespace detail
inline void needle_horizontal_track(int x0,int x1,int y) {
    if(x1>x0)ui::line(x0,y,x1,y,palette::shadow());
}
inline void needle_horizontal(int x0,int x1,int y,int edit_x,int live_x,
                              State state={},int neutral_x=-1,bool compact=false) {
    detail::needle_draw({x0,y,false},x1-x0,edit_x-x0,live_x-x0,state,
                        neutral_x<0?-1:neutral_x-x0,compact);
}
inline void needle_vertical(int x,int top,int bottom,int edit_y,int live_y,
                            State state={},int neutral_y=-1,bool compact=false) {
    detail::needle_draw({x,bottom,true},bottom-top,bottom-edit_y,bottom-live_y,state,
                        neutral_y<0?-1:bottom-neutral_y,compact);
}
} // namespace seance::platform::ui3ds::module_fader
