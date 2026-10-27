#pragma once
// GO TO borrows the existing dual-screen disclosure caches while modal. One
// read of the last completed LCD pair on entry, never a live per-frame readback.
// FrameBegin fences the previous GPU queue; remember_outputs is BEFORE FrameEnd
// (which transfers into those buffers, then swaps them in its queue callback).
#include "sigil_surface.h"

namespace seance::platform::ui3ds::nav_backdrop {
inline bool requested=false, frozen=false;
#ifdef __3DS__
inline uint8_t* previous_top=nullptr;
inline uint8_t* previous_bottom=nullptr;
#endif
inline void request() { requested=true; frozen=false; }

// Native rotated BGR8 display buffers, averaged 2x2. Pure CPU seam for tests.
// Same existing [1,2,1] separable blur +24% paper, then the approved 68% veil
// on BOTH LCDs. No extra pixel storage/texture is allocated for this popup.
template<int W>
inline void freeze_screen(sigil_surface::BasicCache<W>& cache, const uint8_t* bgr) {
    cache.clear();
    if(bgr) for(int y=0;y<sigil_surface::H;++y) for(int x=0;x<W;++x) {
        unsigned sum[3]={};
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const auto* p=bgr+((x*2+dx)*240+239-(y*2+dy))*3;
            for(int c=0;c<3;++c)sum[c]+=p[2-c];
        }
        cache.pixels[y*W+x]=0xff000000u | ((sum[0]+2)/4) |
            (((sum[1]+2)/4)<<8) | (((sum[2]+2)/4)<<16);
    }
    cache.finish();
    constexpr uint32_t veil=0xff0a0506u; // rgb(6,5,10)
    for(auto& p:cache.pixels)p=cache.blend(p,veil,68,100);
}
inline void freeze(const uint8_t* top, const uint8_t* bottom) {
    freeze_screen(sigil_surface::top_cache,top);
    freeze_screen(sigil_surface::bottom_cache,bottom);
    requested=false;frozen=true;
}
inline void gpu_frame() {
    if(!requested)return;
#ifdef __3DS__
    // GPU/display writes are not coherent with an earlier CPU capture.
    if(previous_top)GSPGPU_InvalidateDataCache(previous_top,400*240*3);
    if(previous_bottom)GSPGPU_InvalidateDataCache(previous_bottom,320*240*3);
    freeze(previous_top,previous_bottom);
#else
    freeze(nullptr,nullptr);
#endif
}
inline void remember_outputs() {
#ifdef __3DS__
    uint16_t w=0,h=0;
    auto* top=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&w,&h);
    previous_top=(w==240&&h==400&&gfxGetScreenFormat(GFX_TOP)==GSP_BGR8_OES)?top:nullptr;
    auto* bottom=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&w,&h);
    previous_bottom=(w==240&&h==320&&gfxGetScreenFormat(GFX_BOTTOM)==GSP_BGR8_OES)?bottom:nullptr;
#endif
}
inline void draw(bool top) {
    const int width=top?400:320;
    // Establish the LCD crop for a following transition paper_rect, even when
    // the cached image itself needs no paper draw.
    menu_style::paper(width,0,0);
    if(frozen && (top?sigil_surface::top_cache.draw():sigil_surface::bottom_cache.draw()))return;
    // Allocation failure / first frame: one still, dim paper quad. Do not animate
    // a hidden live page or spend thousands of CPU-fallback blur draw objects.
    const auto color=frozen ? (top?sigil_surface::top_cache.pixels[0]:sigil_surface::bottom_cache.pixels[0]) :
        sigil_surface::Cache::blend(menu_style::paper_color(120),0xff0a0506u,68,100);
    ui::rect(0,0,width,240,color);
}
} // namespace seance::platform::ui3ds::nav_backdrop
