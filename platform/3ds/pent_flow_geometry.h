#pragma once
// Local full-edge ribbon mapping; the original white star/ring never move.
// One immutable seamless period spans the WHOLE five-edge circuit. Both ends
// of adjoining ribbons evaluate the same longitudinal UV at their shared vertex.
#include "pent_reactive.h"
#include "pent_flow_data.h"
#include <algorithm>
#include <cmath>

namespace seance::platform::ui3ds::pent_flow_geometry {
inline constexpr float edge_length[5]={173.303202509f,173.216627377f,174.f,173.216627377f,173.303202509f};
inline constexpr float edge_angle[5]={1.259994240f,-2.511947471f,0.f,2.511947471f,-1.259994240f};
inline constexpr float overlap=2.f; // rectangular film overlap, never a ball/cap sprite
struct Quad {
    float x,y,angle,length,height,left,right;
    uint8_t alpha_left,alpha_right;
};
inline float junction(const pent_reactive::Frame& frame,unsigned vertex){
    return float(vertex)*pent_flow_data::period/5.f-
        float(frame.flow)*pent_flow_data::period/pent_reactive::cycle_turn+float(frame.warp[vertex%5])/256.f;
}
inline Quad quad(const pent_reactive::Frame& frame,unsigned edge){
    const unsigned next=(edge+1)%5;
    const float length=edge_length[edge];
    // vertex5 is vertex0 plus exactly one seamless period, not an independent
    // reset. A wave crosses every apex and ordinary line intersection in place.
    const float span=junction(frame,edge+1)-junction(frame,edge);
    const float start=junction(frame,edge)-overlap*span/length;
    const float left=start-std::floor(start/pent_flow_data::period)*pent_flow_data::period;
    const unsigned breadth=(unsigned(frame.breadth[edge])+frame.breadth[next]+1)/2;
    return {
        (pent_reactive::vertex_x[edge]+pent_reactive::vertex_x[next])*.5f,
        (pent_reactive::vertex_y[edge]+pent_reactive::vertex_y[next])*.5f,
        edge_angle[edge],length+2*overlap,float(std::clamp(breadth,128u,192u))/16.f,
        left,left+span*(length+2*overlap)/length,
        frame.heads[edge].alpha,frame.heads[next].alpha
    };
}
// Ordinary host builds only: native nearest strip painter. The console always
// submits five rotated textured quads; it NEVER runs this pixel loop/fallback.
// Independent native test oracles decode Morton/ABGR from the actual GPU bytes.
#if !defined(__3DS__) && !defined(SEANCE_PENT_UI_GPU_TEST)
template<class Pixel> inline void paint(const Quad& q,Pixel pixel){
    const float c=std::cos(q.angle),s=std::sin(q.angle);
    const float hw=q.length*.5f,hh=q.height*.5f;
    const float ex=std::abs(c)*hw+std::abs(s)*hh,ey=std::abs(s)*hw+std::abs(c)*hh;
    for(int y=int(std::floor(q.y-ey));y<int(std::ceil(q.y+ey));++y)
        for(int x=int(std::floor(q.x-ex));x<int(std::ceil(q.x+ex));++x){
            const float dx=x+.5f-q.x,dy=y+.5f-q.y;
            const float u=(dx*c+dy*s+hw)/q.length,v=(-dx*s+dy*c+hh)/q.height;
            if(u<0||u>=1||v<0||v>=1)continue;
            const unsigned tx=unsigned(std::clamp(int(std::floor(q.left+u*(q.right-q.left))),0,255));
            const unsigned ty=unsigned(std::clamp(int(std::floor(v*pent_flow_data::height)),0,15));
            const uint32_t color=pent_flow_data::pixel(tx,ty);
            const unsigned alpha=unsigned(q.alpha_left+(q.alpha_right-int(q.alpha_left))*u+.5f);
            pixel(x,y,(color&0xffffffu)|((((color>>24)*alpha+127)/255)<<24));
        }
}
#endif
} // namespace seance::platform::ui3ds::pent_flow_geometry
