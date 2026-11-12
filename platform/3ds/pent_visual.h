#pragma once
// Immutable original PENT scaffold/FX plus one local seamless blood strip.
// Full-edge flow and junction response are frozen; draw never advances them.
#include "pent_reactive.h"
#include "pent_reactive_data.h"
#include "pent_flow_geometry.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstring>
#if defined(__3DS__) || defined(SEANCE_PENT_UI_GPU_TEST)
#include <citro2d.h>
#endif
namespace seance::platform::ui3ds::pent_visual {
inline constexpr unsigned texture_bytes=2*256*256*4, gpu_objects=7;

// Exact whole-texture oracle: source coordinates, no cropping/tint/admission.
// Transparent runs are absent. Both immutable textures are always 256 x 256.
template<class Rect> inline void scanlines(unsigned texture,Rect rect){
    using namespace pent_reactive_data;
    if(texture>=2)return;
    for(unsigned i=offsets[texture];i<offsets[texture+1];++i){const auto r=runs[i];
        rect(int(r&255),int((r>>8)&255),int((r>>16)&255)+1,heights[i],colors[r>>24]);}
    // Previously transparent atlas band; original white/FX/head bytes intact.
    if(texture==1)pent_flow_data::scanlines(rect);
}
namespace detail {
inline uint32_t alpha_color(uint32_t c,uint8_t alpha){
    return (c&0xffffffu)|((((c>>24)*alpha+127)/255)<<24);
}
// Host scanline painter clips in WHOLE texture coordinates before translating.
// Only alpha is multiplied; RGB stays exact, including partially transparent FX.
inline void paint(unsigned texture,int sx,int sy,int w,int h,int dx,int dy,uint8_t alpha){
    if(!alpha)return;
    scanlines(texture,[&](int x,int y,int rw,int rh,uint32_t c){
        const int left=std::max(x,sx),top=std::max(y,sy);
        const int right=std::min(x+rw,sx+w),bottom=std::min(y+rh,sy+h);
        if(left>=right||top>=bottom)return;
        c=alpha_color(c,alpha);
        if(c>>24)ui::rect(dx+left-sx,dy+top-sy,right-left,bottom-top,c);
    });
}
}
#if defined(__3DS__) || defined(SEANCE_PENT_UI_GPU_TEST)
struct Resource {
    C3D_Tex tex[2]{};
    Tex3DS_SubTexture sub[2]={
        {256,200,0,1,1,1-200.f/256},
        {256,192,0,1,1,1-192.f/256}
    };
    bool ready=false,uploaded=false,attempted=false,allocated[2]={false,false};
    // Allocation only, after UI guard-copy and OUTSIDE the SYNCDRAW fence.
    // Failure is bounded to one attempt per resource lifetime, with own cleanup.
    void prepare(){
        if(attempted)return;
        attempted=true;
        if(!C3D_TexInit(&tex[0],256,256,GPU_RGBA8))return;
        allocated[0]=true;
        if(!C3D_TexInit(&tex[1],256,256,GPU_RGBA8)){
            C3D_TexDelete(&tex[0]);allocated[0]=false;return;
        }
        allocated[1]=true;
        for(auto& t:tex){
            C3D_TexSetFilter(&t,GPU_NEAREST,GPU_NEAREST);
            C3D_TexSetWrap(&t,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        }
        ready=true;
    }
    // Caller must have a SUCCESSFUL SYNCDRAW fence, before either screen draws.
    // Immutable decode/Morton ABGR/flush happens once: no reactive reuploads.
    void upload(){
        if(!ready||uploaded)return;
        for(unsigned texture=0;texture<2;++texture){
            auto* data=static_cast<uint8_t*>(tex[texture].data);
            std::memset(data,0,256*256*4);
            scanlines(texture,[&](int x,int y,int w,int h,uint32_t c){
                for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx){
                    const unsigned low=(xx&1)|((yy&1)<<1)|((xx&2)<<1)|((yy&2)<<2)|((xx&4)<<2)|((yy&4)<<3);
                    const unsigned at=(((yy>>3)*32+(xx>>3))*64+low)*4;
                    data[at]=uint8_t(c>>24);data[at+1]=uint8_t(c>>16);
                    data[at+2]=uint8_t(c>>8);data[at+3]=uint8_t(c);
                }
            });
            C3D_TexFlush(&tex[texture]);
        }
        uploaded=true;
    }
    // Caller owns fencing GPU readers; partial allocation cleanup is safe too.
    void shutdown(){
        for(unsigned i=0;i<2;++i)if(allocated[i]){C3D_TexDelete(&tex[i]);allocated[i]=false;}
        ready=uploaded=attempted=false;
    }
};
inline Resource resource;
inline void prepare(){resource.prepare();}
inline void upload(){resource.upload();}
inline void shutdown(){resource.shutdown();}
#else
inline void prepare(){}
inline void upload(){}
inline void shutdown(){}
#endif
inline void draw(const pent_reactive::Frame& frame){
#if defined(__3DS__) || defined(SEANCE_PENT_UI_GPU_TEST)
    if(resource.ready&&resource.uploaded){
        if(frame.glow){
            const C2D_Image image{&resource.tex[0],&resource.sub[0]};
            C2D_ImageTint tint;C2D_PlainImageTint(&tint,uint32_t(frame.glow)<<24,0);
            C2D_DrawImageAt(image,112,24,0,&tint,1,1);
        }
        const C2D_Image white{&resource.tex[1],&resource.sub[1]};
        C2D_DrawImageAt(white,112,32,0,nullptr,1,1);
        for(unsigned i=0;i<5;++i){
            const auto q=pent_flow_geometry::quad(frame,i);
            if(!q.alpha_left&&!q.alpha_right)continue;
            // One full edge in one quad, no recurring texture upload or sprite
            // cap. Two seamless periods keep every scrolling UV inside the atlas.
            const Tex3DS_SubTexture strip{128,16,q.left/256.f,1-192.f/256,
                q.right/256.f,1-208.f/256};
            const C2D_Image image{&resource.tex[1],&strip};
            C2D_ImageTint tint;C2D_PlainImageTint(&tint,uint32_t(q.alpha_left)<<24,0);
            tint.corners[1].color=tint.corners[3].color=uint32_t(q.alpha_right)<<24;
            C2D_DrawImageAtRotated(image,q.x,q.y,0,q.angle,&tint,q.length/128.f,q.height/16.f);
        }
        return;
    }
    // Bounded original white geometry only. Not a successful full-art renderer;
    // never emulate the atlas with thousands of GPU rectangles after failure.
    constexpr int x[]={240,293,153,327,187},y[]={37,202,100,100,202};
    for(int i=0;i<5;++i)ui::line(x[i],y[i],x[(i+1)%5],y[(i+1)%5],0xfff4f4f4u,2);
    for(int yy=-92;yy<=92;++yy){const int xx=pent_reactive_data::ring_x[yy+92];
        ui::rect(240-xx,128+yy,1,1,0xfff4f4f4u);ui::rect(240+xx,128+yy,1,1,0xfff4f4f4u);}
#else
    detail::paint(0,0,0,256,200,112,24,frame.glow);
    detail::paint(1,0,0,256,192,112,32,255);
    for(unsigned i=0;i<5;++i)pent_flow_geometry::paint(pent_flow_geometry::quad(frame,i),
        [](int x,int y,uint32_t c){if(c>>24)ui::rect(x,y,1,1,c);});
#endif
}
} // namespace seance::platform::ui3ds::pent_visual
