#pragma once
// One clipped command stream for native UI and its frozen disclosure background.
// The shared bottom/top pair is owned by mutually exclusive views; each owner must
// refresh on entry/open. CPU downsample + separable [1 2 1] blur only on changes.
// No framebuffer readback, heap, live engine references or per-frame blur.
#include "menu_style.h"
#include "menu_labels_data.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/font.h"
#include "../../core/ui/pixel_geometry.h"
#include <algorithm>
#include <cstring>
#if defined(__3DS__) || defined(SEANCE_RULE_UI_CACHE_GPU_TEST)
#include <citro2d.h>
#endif

namespace seance::platform::ui3ds::sigil_surface {
constexpr int W=160,H=120,TopW=200;
// UI-thread only: finish() calls are serial, never inside an audio guard/callback.
// Pixel storage: (160+200+200)*120*4 = 268800 bytes. Two 256x128 RGBA8
// textures add 262144 bytes; total 530944 (~519 KiB), excluding small metadata.
inline uint32_t scratch[TopW*H]={};
template<int Width> struct BasicCache {
    static_assert(Width>0&&Width<=TopW,"bounded native-screen blur width");
    static constexpr int W=Width;
    uint32_t pixels[W*H]={};
    bool pending=false, valid=false;
#if defined(__3DS__) || defined(SEANCE_RULE_UI_CACHE_GPU_TEST)
    C3D_Tex texture{};
    // Never written: one shared constant per width, so the cache object itself is
    // all-zero and lands in .bss (the .3dsx stores .data, not .bss).
    static constexpr Tex3DS_SubTexture sub{W,H,0,1,float(W)/256,1-float(H)/128};
    bool ready=false;
#endif
    bool init() {
#if defined(__3DS__) || defined(SEANCE_RULE_UI_CACHE_GPU_TEST)
        if(ready)return true;
        ready=C3D_TexInit(&texture,256,128,GPU_RGBA8);
        if(ready){C3D_TexSetFilter(&texture,GPU_LINEAR,GPU_LINEAR);C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);}
        return ready;
#else
        return false;
#endif
    }
    void shutdown() {
#if defined(__3DS__) || defined(SEANCE_RULE_UI_CACHE_GPU_TEST)
        if(ready)C3D_TexDelete(&texture);
        ready=false;
#endif
        valid=pending=false;
    }
    void clear() {
        for(int y=0;y<H;++y)std::fill(pixels+y*W,pixels+(y+1)*W,menu_style::paper_color(y*2));
    }
    static uint32_t blend(uint32_t a,uint32_t b,unsigned weight,unsigned total) {
        uint32_t out=0xff000000u;
        for(unsigned shift:{0u,8u,16u})out|=(( ((a>>shift)&255)*(total-weight)+((b>>shift)&255)*weight+total/2)/total)<<shift;
        return out;
    }
    void finish() {
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){
            uint32_t out=0xff000000u;
            const auto a=pixels[y*W+std::max(0,x-1)],b=pixels[y*W+x],c=pixels[y*W+std::min(W-1,x+1)];
            for(unsigned s:{0u,8u,16u})out|=(( ((a>>s)&255)+2*((b>>s)&255)+((c>>s)&255)+2)/4)<<s;
            scratch[y*W+x]=out;
        }
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){
            uint32_t out=0xff000000u;
            const auto a=scratch[std::max(0,y-1)*W+x],b=scratch[y*W+x],c=scratch[std::min(H-1,y+1)*W+x];
            for(unsigned s:{0u,8u,16u})out|=(( ((a>>s)&255)+2*((b>>s)&255)+((c>>s)&255)+2)/4)<<s;
            pixels[y*W+x]=blend(out,menu_style::paper_color(y*2),24,100);
        }
        pending=valid=true;
    }
    // After successful SYNCDRAW FrameBegin, before either screen submits draws.
    void upload() {
#if defined(__3DS__) || defined(SEANCE_RULE_UI_CACHE_GPU_TEST)
        if(!pending||!ready)return;
        auto* data=static_cast<uint8_t*>(texture.data);
        // Extend the edge into texture padding so linear filtering has no seam.
        for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x){
            const auto c=pixels[std::min(y,unsigned(H-1))*W+std::min(x,unsigned(W-1))];
            const unsigned low=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
            const unsigned at=(((y>>3)*32+(x>>3))*64+low)*4;
            data[at]=255;data[at+1]=uint8_t(c>>16);data[at+2]=uint8_t(c>>8);data[at+3]=uint8_t(c);
        }
        C3D_TexFlush(&texture);
#endif
        pending=false;
    }
    bool draw() const {
        if(!valid)return false;
#if defined(__3DS__) || defined(SEANCE_RULE_UI_CACHE_GPU_TEST)
        if(!ready)return false;
        const C2D_Image image{const_cast<C3D_Tex*>(&texture),&sub};
        C2D_DrawImageAt(image,0,0,0,nullptr,2,2);
#else
        // Software host oracle, not a GPU object count: hardware uses ONE quad.
        for(int y=0;y<H;++y)for(int x=0;x<W;){int end=x+1;while(end<W&&pixels[y*W+end]==pixels[y*W+x])++end;
            ui::rect(x*2,y*2,(end-x)*2,2,pixels[y*W+x]);x=end;}
#endif
        return true;
    }
};
using Cache=BasicCache<W>;
using TopCache=BasicCache<TopW>;
inline Cache bottom_cache;
inline TopCache top_cache;
static_assert(sizeof(bottom_cache.pixels)+sizeof(top_cache.pixels)+sizeof(scratch)==268800,
              "shared dual-screen CPU pixel budget");

template<int Width> struct BasicPainter {
    using CacheType=BasicCache<Width>;
    // Keep cache first and this type aggregate-compatible: Painter{&cache}.
    CacheType* cache=nullptr;
    // Native pixel edges, right/bottom exclusive; also clamped to the screen.
    int clip_left=0,clip_top=0,clip_right=Width*2,clip_bottom=H*2;
    int left() const {return std::max(0,clip_left);}
    int top() const {return std::max(0,clip_top);}
    int right_edge() const {return std::min(Width*2,clip_right);}
    int bottom() const {return std::min(H*2,clip_bottom);}
    bool fits(int x,int y,int w,int h) const {
        return x>=left()&&y>=top()&&x+w<=right_edge()&&y+h<=bottom();
    }
    bool visible(int x,int y,int w,int h) const {
        return w>0&&h>0&&x<right_edge()&&y<bottom()&&x+w>left()&&y+h>top();
    }
    void rect(int x,int y,int w,int h,uint32_t color) const {
        if(w<=0||h<=0)return;
        const int l=std::max(left(),x),t=std::max(top(),y),r=std::min(right_edge(),x+w),b=std::min(bottom(),y+h);
        if(r<=l||b<=t)return;
        if(!cache){ui::rect(l,t,r-l,b-t,color);return;}
        for(int yy=t/2;yy<=(b-1)/2;++yy)for(int xx=l/2;xx<=(r-1)/2;++xx){
            const unsigned area=(std::min(r,xx*2+2)-std::max(l,xx*2))*(std::min(b,yy*2+2)-std::max(t,yy*2));
            auto& dest=cache->pixels[yy*Width+xx];dest=CacheType::blend(dest,color,area*((color>>24)&255),4*255);
        }
    }
    void frame(int x,int y,int w,int h,uint32_t c) const {
        ui::pixel::frame({x,y,w,h},[&](ui::pixel::Box b){rect(b.x,b.y,b.w,b.h,c);});
    }
    void line(int x0,int y0,int x1,int y1,uint32_t c) const {
        const int l=std::min(x0,x1),t=std::min(y0,y1),w=std::abs(x1-x0)+1,h=std::abs(y1-y0)+1;
        if(!visible(l,t,w,h))return;
        if(!cache){
            ui::pixel::Box b;
            if(ui::pixel::axis_line(x0,y0,x1,y1,1,b)){rect(b.x,b.y,b.w,b.h,c);return;}
            if(fits(l,t,w,h)){ui::line(x0,y0,x1,y1,c);return;}
        }
        // Preserve existing cache coverage/blending, including inclusive axes.
        // Partial native diagonals retain original pixel phase, never bleed into
        // a neighbour. Fully-contained native diagonals still use one GPU quad.
        int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
        for(;;){rect(x0,y0,1,1,c);if(x0==x1&&y0==y1)break;int e=err*2;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
    }
    void text(int x,int y,const char* s,uint32_t c) const {
        if(!s)return;
        const int width=ui::text_w(s);
        if(!visible(x,y,width,8))return;
        if(!cache&&fits(x,y,width,8)){ui::text(x,y,s,c);return;}
        for(;*s&&x<right_edge();++s,x+=6){
            if(x+6<=left())continue;
            for(int row=std::max(0,top()-y);row<std::min(8,bottom()-y);++row){const auto bits=trackr::ui::glyph_row(*s,row);int run=-1;
                for(int col=0;col<=6;++col){bool on=col<6&&(bits&(1<<(5-col)));if(on&&run<0)run=col;if(!on&&run>=0){rect(x+run,y+row,col-run,1,c);run=-1;}}}
        }
    }
    void right(int x,int y,const char* s,uint32_t c) const {if(s)text(x-ui::text_w(s),y,s,c);}
    int heading(int x,int y,const char* s,uint32_t c) const {
        if(!s)return x;
        const menu_labels::Label* found=nullptr;
        for(const auto& l:menu_labels::labels)if(!std::strcmp(l.text,s)){found=&l;break;}
        if(!found){text(x,y,s,c);return x+ui::text_w(s);}
        if(!visible(x,y,found->width,found->height))return x+found->width;
        if(!cache&&fits(x,y,found->width,found->height)){menu_style::heading(x,y,s,c);return x+found->width;}
        const int stride=(found->width+7)/8;const auto* bits=menu_labels::bitmap+found->offset;
        for(int y0=std::max(0,top()-y);y0<std::min(int(found->height),bottom()-y);++y0){int run=-1;for(int col=0;col<=found->width;++col){
            bool on=col<found->width&&(bits[y0*stride+col/8]&(0x80>>(col&7)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){rect(x+run,y+y0,col-run,1,c);run=-1;}
        }}return x+found->width;
    }
    int number(int x,int y,const char* s,uint32_t c) const {
        for(;*s;++s){if(*s=='.'){rect(x,y+18,2,2,c);x+=4;}
            else if(*s=='/'){line(x,y+20,x+7,y,c);x+=10;}
            else {const char word[]={*s,0};x=heading(x,y,word,c)+1;}}
        return x;
    }
    void chevron(int x,int y,uint32_t c) const {line(x-3,y-1,x,y+2,c);line(x,y+2,x+3,y-1,c);}
    void bead(int x,int y,uint32_t c,bool hollow=false) const {
        for(int dy=-3;dy<=3;++dy){int dx=std::abs(dy)==3?1:std::abs(dy)==2?2:3;
            if(!hollow||std::abs(dy)==3)rect(x-dx,y+dy,dx*2+1,1,c);
            else {rect(x-dx,y+dy,1,1,c);rect(x+dx,y+dy,1,1,c);}}
    }
};
using Painter=BasicPainter<W>;
using TopPainter=BasicPainter<TopW>;
} // namespace seance::platform::ui3ds::sigil_surface
