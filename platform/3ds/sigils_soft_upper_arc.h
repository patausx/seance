#pragma once
// Selected soft sigil stroke: exact 4x4 capsule union, not a texture or blur.
// Flow is a template argument so sigils_flow can use this private painter without
// an include cycle. Geometry, coverage and occupied columns are UI-thread only.
#include "sigil_surface.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace seance::platform::ui3ds::soft_upper {
constexpr int LEFT=26,TOP=82,W=256,H=90;
// Conservative ceilings, not a console frame-time/object-budget admission:
// each segment covers <=(dx+7)*H candidate pixels; sum(dx)<=252, n<=96.
constexpr unsigned MAX_CANDIDATE_PIXELS=(252+7*96)*H;
constexpr unsigned MAX_SAMPLE_TESTS=16*MAX_CANDIDATE_PIXELS;
constexpr unsigned MAX_ARC_RECTANGLES=W*H; // one run per occupied pixel at worst
inline uint16_t coverage[W*H]={}; // Same row-major subpixel masks as the selected prototype.
struct Column {uint8_t first=H,last=0;}; // Local rows, last exclusive; first==H is empty.
inline Column columns[W];
struct ArcCache {
    int16_t y[97]={};
    uint8_t count=0;
    bool connected=false,valid=false;
};
inline ArcCache geometry;
// Last preparation/submission work, for bounded native probes (not a FPS claim).
struct Work {
    unsigned segments=0,pixels=0,tested_samples=0,edge_checks=0,cleared_pixels=0;
    unsigned span_pixels=0,rectangles=0,model_points=0,frequency_points=0;
    bool reused_geometry=false;
};
inline Work work;
inline unsigned samples(uint16_t value){
    // ARM has no scalar popcount instruction; keep this small mask count inline.
    unsigned v=value;v-=((v>>1)&0x5555u);
    v=(v&0x3333u)+((v>>2)&0x3333u);v=(v+(v>>4))&0x0f0fu;
    return (v+(v>>8))&31u;
}
inline void clear_arc(){
    // Untouched columns are already zero; no full empty-buffer fill per capture.
    for(int x=0;x<W;++x){auto& c=columns[x];
        for(int y=c.first;y<c.last;++y){coverage[y*W+x]=0;++work.cleared_pixels;}
        c={};
    }
    geometry.valid=false;
}
struct Segment {
    int ax,ay,dx,dy,span,length2;
    float inverse8length,inverse_span;
};
inline bool edge_sample(const Segment& s,int vx,int vy){
    ++work.edge_checks;
    // Only uncertain float edges use the selected prototype's double expression,
    // including <= at tangencies. Eighth-pixel coordinates/endpoints are exact.
    const double px=s.ax+double(vx)/8.0,py=s.ay+double(vy)/8.0;
    const double q=std::max(0.0,std::min(1.0,((px-s.ax)*s.dx+(py-s.ay)*s.dy)/s.length2));
    const double qx=s.ax+q*s.dx,qy=s.ay+q*s.dy;
    const double radius=(4.0-3.0*(qx-28.0)/s.span)/2.0;
    return (px-qx)*(px-qx)+(py-qy)*(py-qy)<=radius*radius;
}
inline bool inside_sample(const Segment& s,int vx,int vy){
    ++work.tested_samples;
    const int dot=vx*s.dx+vy*s.dy;
    const float q=dot<=0?0.f:dot>=8*s.length2?1.f:float(dot)*s.inverse8length;
    // Local displacement avoids subtracting two large screen coordinates.
    const float ex=float(vx)*0.125f-q*s.dx,ey=float(vy)*0.125f-q*s.dy;
    const float radius=(4.f-3.f*(float(s.ax-28)+q*s.dx)*s.inverse_span)*0.5f;
    const float delta=ex*ex+ey*ey-radius*radius;
    // At |dx|<=252, |dy|<=84 and radius<=2 the accumulated single-precision
    // edge error is below this conservative 0.002 square-pixel envelope. Never
    // guess a boundary bit: recompute it with the original analytic expression.
    return std::abs(delta)<=0.002f?edge_sample(s,vx,vy):delta<0.f;
}
template<class Flow> void project_arc(const Flow& f){
    const bool same=geometry.valid&&geometry.connected==f.connected&&geometry.count==f.count&&
        std::equal(f.y,f.y+f.count,geometry.y);
    if(same){work.reused_geometry=true;return;}
    clear_arc();
    const int n=f.count-1,span=f.connected?252:120;
    const float inverse_span=1.f/span;
    for(int i=0;i<n;++i){
        const int ax=28+span*i/n,bx=28+span*(i+1)/n,ay=f.y[i],by=f.y[i+1];
        const int dx=bx-ax,dy=by-ay,length2=dx*dx+dy*dy;
        const Segment segment{ax,ay,dx,dy,span,length2,1.f/(8.f*length2),inverse_span};
        ++work.segments;
        // Integral endpoints make floor(a-2.5)/ceil(b+2.5) exactly a-3/b+3.
        const int l=std::max(LEFT,ax-3),r=std::min(LEFT+W-1,bx+3);
        const int t=std::max(TOP,std::min(ay,by)-3),b=std::min(TOP+H-1,std::max(ay,by)+3);
        for(int x=l;x<=r;++x)for(int y=t;y<=b;++y){
            auto& mask=coverage[(y-TOP)*W+x-LEFT];if(mask==0xffffu)continue;
            ++work.pixels;
            // Every subpixel is within sqrt(2)*3/8 of its pixel center. A
            // center >sqrt(7) from even the INFINITE line cannot touch radius2.
            // Integer reject also handles steep segments without soft doubles.
            const int cross=dx*(y-ay)-dy*(x-ax);
            if(int64_t(cross)*cross>7LL*length2)continue;
            const uint16_t before=mask;
            for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
                const uint16_t bit=uint16_t(1u<<(sy*4+sx));if(mask&bit)continue;
                if(inside_sample(segment,8*(x-ax)+2*sx-3,8*(y-ay)+2*sy-3))mask|=bit;
            }
            if(mask!=before){auto& c=columns[x-LEFT];
                c.first=uint8_t(std::min(int(c.first),y-TOP));c.last=uint8_t(std::max(int(c.last),y-TOP+1));
            }
        }
    }
    std::copy(f.y,f.y+f.count,geometry.y);geometry.count=f.count;
    geometry.connected=f.connected;geometry.valid=true;
}
inline uint32_t arc_color(int x,bool connected){
    namespace m=menu_style;
    const unsigned weight=connected?unsigned(std::max(0,std::min(126,x-154))):0;
    return sigil_surface::Cache::blend(m::ink(),m::spot(),weight,126);
}
inline bool same_column(int a,int b){
    if(columns[a].first!=columns[b].first||columns[a].last!=columns[b].last)return false;
    for(int y=columns[a].first;y<columns[a].last;++y)
        if(coverage[y*W+a]!=coverage[y*W+b])return false;
    return true;
}
template<class Flow> void arc(const Flow& f){
    work.span_pixels=work.rectangles=0;
    if(!f.valid||!geometry.valid)return;
    // No 256x90 empty-grid walk in draw. Merge identical adjacent INK columns
    // (quiet or x<=154); the gradient remains an exact one-column color sample.
    for(int x=0;x<W;){
        const auto c=columns[x];if(c.first==H){++x;continue;}
        int end=x+1;
        while(end<W&&(!f.connected||LEFT+end<=154)&&same_column(x,end))++end;
        const uint32_t rgb=arc_color(LEFT+x,f.connected)&0xffffffu;
        for(int y=c.first;y<c.last;){
            const unsigned a=samples(coverage[y*W+x]);++work.span_pixels;
            if(!a){++y;continue;}
            int last=y+1;
            while(last<c.last&&samples(coverage[last*W+x])==a){++last;++work.span_pixels;}
            ui::rect(LEFT+x,TOP+y,end-x,last-y,rgb|((a*255/16)<<24));++work.rectangles;y=last;
        }
        x=end;
    }
}
} // namespace seance::platform::ui3ds::soft_upper
