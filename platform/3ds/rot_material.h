#pragma once
// Mechanical approved ROT material + enumerated cache seam. tests/rot_ui_transfer.py.
// Resource is UI-owned; upload/free only after the caller's successful GPU fence.
#include "rot_visual_types.h"
#include "ui_palette.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
#include <citro2d.h>
#endif
namespace seance::platform::ui3ds::rot_visual {
using seance::ui::text;using seance::ui::text_w;
inline uint32_t mix(uint32_t a, uint32_t b, int t) {           // t 0..255, ABGR, opaque
    t = std::clamp(t, 0, 255);
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8)
        out |= uint32_t((int((a >> s) & 255) * (255 - t) + int((b >> s) & 255) * t + 127) / 255) << s;
    return out;
}
inline uint32_t hash3(int a, int b, int c) {                     // stable per pass, no per-frame flicker
    uint32_t h = uint32_t(a) * 0x9E3779B1u ^ uint32_t(b) * 0x85EBCA77u ^ uint32_t(c) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; return h;
}
inline int bayer4(int x, int y) {
    static constexpr int k[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return k[(y & 3) * 4 + (x & 3)];
}
inline void right_text(int right, int y, const char* s, uint32_t c) { text(right - text_w(s), y, s, c); }
inline int ease_q8(int t) { t = std::clamp(t, 0, 256); return t * t * (768 - 2 * t) / 65536; } // smoothstep

// Theme-aware non-palette colours (need approval, see header).
inline uint32_t blood() { return palette::visual_graphite() ? palette::rgb(186, 24, 40) : palette::rgb(156, 14, 42); }
inline uint32_t crust() { return palette::visual_graphite() ? palette::rgb(96, 10, 22) : palette::rgb(78, 6, 24); }
// rot mask 0..255 -> ink: clean ink, then blood, then dark crust.
inline uint32_t rot_ink(int r) {
    if (r < 40) return palette::ink();
    if (r < 150) return mix(palette::ink(), blood(), (r - 40) * 255 / 110);
    return mix(blood(), crust(), (r - 150) * 255 / 105);
}

namespace rot_detail {
struct Slot { float center, half; };
inline Slot slot(float k) {                     // k=0 current band, k>=1 strata compress upward
    const Slot band{170.f, 30.f};
    auto fixed = [](int i) {
        float bottom = 134.f;
        for (int j = 1; j < i; ++j) bottom -= float(std::max(2, 11 - j) + 1);
        const float h = float(std::max(2, 11 - i));
        return Slot{bottom - h / 2.f, h / 2.f};
    };
    if (k <= 0.f) return band;
    if (k < 1.f) { const Slot s = fixed(1); return {band.center + (s.center - band.center) * k, band.half + (s.half - band.half) * k}; }
    const int i = int(k); const float t = k - float(i);
    const Slot a = fixed(i), b = fixed(i + 1);
    return {a.center + (b.center - a.center) * t, a.half + (b.half - a.half) * t};
}
}
template<class Pen> inline void material_band(const RotFrame& f,const Artwork& a,const Pen& pen) {          // the current, audible pass: full material treatment
    const int cy = 170, half = 30, pass = f.pass;
    for (int x = 0; x < RotFrame::kCols; ++x) {
        const int sx = 14 + x, r = a.mask[0][x];
        const int h = a.env[0][x] * half / 255;
        const uint32_t c = rot_ink(r);
        if (r <= 150) pen.rect(float(sx), float(cy - h), 1.f, float(2 * h + 1), c);
        else {                                   // crust disintegrates: ordered-dither holes
            const int lose = (r - 150) * 10 / 105;   // 0..10 of 16 cells removed
            for (int y = cy - h; y <= cy + h; ++y)
                if (bayer4(sx, y) >= lose) pen.rect(float(sx), float(y), 1.f, 1.f, c);
        }
        if (r > 60) {                             // mold fuzz grows past the edges (stable per pass)
            const int reach = 1 + r / 80;
            for (int k = 1; k <= reach; ++k) {
                if (int(hash3(sx, k, pass) & 255) < r - 40) pen.rect(float(sx), float(cy - h - k), 1.f, 1.f, mix(blood(), crust(), k * 60));
                if (int(hash3(sx, -k, pass) & 255) < r - 40) pen.rect(float(sx), float(cy + h + k), 1.f, 1.f, mix(blood(), crust(), k * 60));
            }
        }
        const int healed = int(a.scar[x]) - r;    // stitches across healed wounds
        if (healed > 90 && sx % 6 == 0) {
            const int top = cy - std::max(h, 4) - 3, bottom = cy + std::max(h, 4) + 3;
            pen.line(sx, top, sx, bottom, palette::ghost());
            pen.line(sx - 1, top, sx + 1, top, palette::ghost());
            pen.line(sx - 1, bottom, sx + 1, bottom, palette::ghost());
        }

    }
}
template<class Pen> inline void material_full_band(const RotFrame& f,const Artwork& a,const Pen& pen) {          // the current, audible pass: full material treatment
    const int cy = 170, half = 30, pass = f.pass;
    for (int x = 0; x < RotFrame::kCols; ++x) {
        const int sx = 14 + x, r = a.mask[0][x];
        const int h = a.env[0][x] * half / 255;
        const uint32_t c = rot_ink(r);
        if (r <= 150) pen.rect(float(sx), float(cy - h), 1.f, float(2 * h + 1), c);
        else {                                   // crust disintegrates: ordered-dither holes
            const int lose = (r - 150) * 10 / 105;   // 0..10 of 16 cells removed
            for (int y = cy - h; y <= cy + h; ++y)
                if (bayer4(sx, y) >= lose) pen.rect(float(sx), float(y), 1.f, 1.f, c);
        }
        if (r > 60) {                             // mold fuzz grows past the edges (stable per pass)
            const int reach = 1 + r / 80;
            for (int k = 1; k <= reach; ++k) {
                if (int(hash3(sx, k, pass) & 255) < r - 40) pen.rect(float(sx), float(cy - h - k), 1.f, 1.f, mix(blood(), crust(), k * 60));
                if (int(hash3(sx, -k, pass) & 255) < r - 40) pen.rect(float(sx), float(cy + h + k), 1.f, 1.f, mix(blood(), crust(), k * 60));
            }
        }
        const int healed = int(a.scar[x]) - r;    // stitches across healed wounds
        if (healed > 90 && sx % 6 == 0) {
            const int top = cy - std::max(h, 4) - 3, bottom = cy + std::max(h, 4) + 3;
            pen.line(sx, top, sx, bottom, palette::ghost());
            pen.line(sx - 1, top, sx + 1, top, palette::ghost());
            pen.line(sx - 1, bottom, sx + 1, bottom, palette::ghost());
        }
        if (a.spark[x]) {                         // real crackle impulse: a splinter jumps out
            const int s = a.spark[x], len = 2 + s / 40;
            const uint32_t sc = mix(palette::bg(), palette::playhead(), s);
            pen.line(sx, cy - h - 2 - len, sx, cy - h - 2, sc);
            pen.line(sx, cy + h + 2, sx, cy + h + 2 + len * 2 / 3, sc);
        }
    }
}
template<class Pen> inline void material_sparks(const RotFrame& f,const Artwork& a,const Pen& pen){
    (void)f;
    const int cy=170,half=30;
    for(int x=0;x<372;++x){const int sx=14+x,h=a.env[0][x]*half/255;
        if (a.spark[x]) {                         // real crackle impulse: a splinter jumps out
            const int s = a.spark[x], len = 2 + s / 40;
            const uint32_t sc = mix(palette::bg(), palette::playhead(), s);
            pen.line(sx, cy - h - 2 - len, sx, cy - h - 2, sc);
            pen.line(sx, cy + h + 2, sx, cy + h + 2 + len * 2 / 3, sc);
        }
    }
}
template<class Pen> inline void material_source(const RotFrame& f,const Artwork& a,const Pen& pen){
    // loop strip: whole source in subdued ink, the loop window carries the CURRENT rotted pass.
    const int cy = 76, half = 13;
    for (int x = 0; x < RotFrame::kStrip; ++x) {
        const int sx = 14 + x;
        if (x >= std::clamp(f.loop_x0,0,292) && x < std::clamp(f.loop_x1,std::clamp(f.loop_x0,0,292),292)) {
            const int c = (x - std::clamp(f.loop_x0,0,292)) * RotFrame::kCols / std::max(1, std::clamp(f.loop_x1,std::clamp(f.loop_x0,0,292),292) - std::clamp(f.loop_x0,0,292));
            const int h = a.env[0][c] * half / 255;
            pen.rect(float(sx), float(cy - h), 1.f, float(2 * h + 1), rot_ink(a.mask[0][c]));
        } else {
            const int h = a.source_env[x] * half / 255;
            if (h) pen.rect(float(sx), float(cy - h), 1.f, float(2 * h + 1), palette::subdued());
        }
    }
}
// Explicit optimization seam: preallocated atlas, no allocations/uploads in draw.
inline constexpr unsigned texture_bytes=512*512*4+512*128*4;
inline constexpr unsigned top_art_quads=13,bottom_art_quads=1;
inline unsigned morton(unsigned x,unsigned y){
    const unsigned low=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
    return ((y>>3)*64+(x>>3))*64+low;
}
struct Texture {
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
    C3D_Tex tex{};
#else
    // Honest software oracle storage; never counted as hardware submissions.
    uint32_t pixels[512*512]={};
#endif
    unsigned height=0;
    bool allocated=false;
    bool init(unsigned h){
        height=h;
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        allocated=C3D_TexInit(&tex,512,h,GPU_RGBA8);
        if(allocated){C3D_TexSetFilter(&tex,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);}
#else
        allocated=true;
#endif
        return allocated;
    }
    void clear(){
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        std::memset(tex.data,0,512*height*4);
#else
        std::fill(pixels,pixels+512*height,0u);
#endif
    }
    void set(int x,int y,uint32_t c){
        if(x<0||y<0||x>=512||y>=int(height))return;
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        auto* p=static_cast<uint8_t*>(tex.data)+morton(unsigned(x),unsigned(y))*4;
        p[0]=uint8_t(c>>24);p[1]=uint8_t(c>>16);p[2]=uint8_t(c>>8);p[3]=uint8_t(c);
#else
        pixels[y*512+x]=c;
#endif
    }
    uint32_t get(unsigned x,unsigned y) const {
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        const auto* p=static_cast<const uint8_t*>(tex.data)+morton(x,y)*4;
        return uint32_t(p[3])|uint32_t(p[2])<<8|uint32_t(p[1])<<16|uint32_t(p[0])<<24;
#else
        return pixels[y*512+x];
#endif
    }
    void flush(){
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        C3D_TexFlush(&tex);
#endif
    }
    void shutdown(){
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        if(allocated)C3D_TexDelete(&tex);
#endif
        allocated=false;
    }
};
struct Raster {
    Texture& texture; int ox=0,oy=0;
    void rect(float x,float y,float w,float h,uint32_t c) const {
        // Exact mock integer-edge law for cached band/stitches/strip.
        for(int yy=int(std::lround(y));yy<int(std::lround(y+h));++yy)
            for(int xx=int(std::lround(x));xx<int(std::lround(x+w));++xx)texture.set(xx+ox,yy+oy,c);
    }
    void line(int x0,int y0,int x1,int y1,uint32_t c) const {
        // All ROT material lines are axis aligned and inclusive.
        rect(float(std::min(x0,x1)),float(std::min(y0,y1)),float(std::abs(x1-x0)+1),float(std::abs(y1-y0)+1),c);
    }
};
// Each stratum stores both START and SETTLED native integer-edge coverage.
// Intervening source-causal 100ms motion uses nearest-scaled start coverage:
// its subpixel mismatch versus the software rect oracle is published by tests.
struct Strip { int row,height; float half,center; };
inline Strip strip(int layer,bool settled){
    const float half=rot_detail::slot(float(settled?layer:layer-1)).half;
    int row=80;
    for(int i=1;i<layer;++i)for(bool end:{false,true})row+=((int(2*rot_detail::slot(float(end?i:i-1)).half)+9)/8)*8;
    if(settled)row+=((int(2*rot_detail::slot(float(layer-1)).half)+9)/8)*8;
    const int height=((int(2*half)+9)/8)*8;
    const float reference=rot_detail::slot(float(settled?layer:layer-1)).center;
    return {row,height,half,float(height)/2+reference-std::floor(reference)};
}
inline constexpr int source_row=464;
struct Resource {
    Texture atlas,sparks;
    bool attempted=false,ready=false,uploaded=false,spark_uploaded=false;
    uint64_t source=0,artwork=0,material=0,spark=0;
    int pass=-1,loop0=-1,loop1=-1;
    bool spark_moving=false;
    Resource()=default;
    Resource(const Resource&)=delete; Resource& operator=(const Resource&)=delete;
    // Caller-owned UI lifetime; explicitly fence before shutdown, not a static destructor.
    void prepare(){
        if(attempted)return;
        attempted=true;
        if(!atlas.init(512))return;
        if(!sparks.init(128)){atlas.shutdown();return;}
        ready=true;
    }
    // false fence => no writes/flush, even on source cancellation. Must be called
    // outside AudioOut guard AFTER successful existing SYNCDRAW, before screen draws.
    bool upload_after_fence(const RotFrame& f,const Artwork& a,bool synchronized){
        if(!synchronized||!ready)return false;
        if(!shows_art(f)){uploaded=spark_uploaded=false;return true;}
        const int l0=std::clamp(f.loop_x0,0,292),l1=std::clamp(f.loop_x1,l0,292);
        const bool changed=!uploaded||source!=f.source_serial||artwork!=f.artwork_serial||material!=f.material_revision||pass!=f.pass||loop0!=l0||loop1!=l1;
        if(changed){
            atlas.clear();
            material_band(f,a,Raster{atlas,-14,-130});
            for(int layer=1;layer<std::clamp(f.history,0,12);++layer)for(bool end:{false,true}){
                const auto s=strip(layer,end);
                for(int x=0;x<372;++x){const int amp=a.env[layer][x];if(!amp)continue;
                    const float h=std::max(.5f,s.half*float(amp)/255.f);
                    Raster{atlas,0,s.row}.rect(float(x),s.center-h,1,2*h,rot_ink(a.mask[layer][x]));}
            }
            material_source(f,a,Raster{atlas,-14,source_row-63});
            atlas.flush();
            source=f.source_serial;artwork=f.artwork_serial;material=f.material_revision;pass=f.pass;loop0=l0;loop1=l1;uploaded=true;
        }
        if(changed||!spark_uploaded||spark!=f.spark_serial||spark_moving!=moving(f)){
            sparks.clear();
            if(moving(f)){
                // Exact original column interleaving: neighbouring stitches may
                // overwrite earlier splinters. Only actual differences overlay.
                material_full_band(f,a,Raster{sparks,-14,-130});
                for(unsigned y=0;y<80;++y)for(unsigned x=0;x<372;++x)
                    if(sparks.get(x,y)==atlas.get(x,y))sparks.set(int(x),int(y),0);
            }
            sparks.flush();spark=f.spark_serial;spark_uploaded=true;spark_moving=moving(f);
        }
        return true;
    }
    bool matches(const RotFrame& f) const {
        return ready&&uploaded&&source==f.source_serial&&artwork==f.artwork_serial&&material==f.material_revision&&pass==f.pass&&loop0==std::clamp(f.loop_x0,0,292)&&loop1==std::clamp(f.loop_x1,std::clamp(f.loop_x0,0,292),292);
    }
    void shutdown_after_fence(bool synchronized){
        if(!synchronized)return;
        atlas.shutdown();sparks.shutdown();ready=attempted=uploaded=spark_uploaded=false;pass=-1;
    }
    void quad(const Texture& t,int row,int width,int height,float x,float y,float scale_y=1,int age=0) const {
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        const Tex3DS_SubTexture sub{uint16_t(width),uint16_t(height),0,1-float(row)/t.height,float(width)/512,1-float(row+height)/t.height};
        const C2D_Image image{const_cast<C3D_Tex*>(&t.tex),&sub};
        C2D_ImageTint tint;C2D_PlainImageTint(&tint,palette::bg(),float(age)/255);
        C2D_DrawImageAt(image,x,y,0,age?&tint:nullptr,1,scale_y);
#else
        const int y0=int(std::lround(y)),y1=int(std::lround(y+height*scale_y));
        for(int yy=y0;yy<y1;++yy){const int sy=std::clamp(int(std::floor((yy+.5f-y)/scale_y)),0,height-1);
            for(int xx=0;xx<width;++xx){auto c=t.pixels[(row+sy)*512+xx];if(c>>24){if(age)c=mix(c,palette::bg(),age);ui::rect(x+xx,float(yy),1,1,c);}}}
#endif
    }
    bool draw_top_art(const RotFrame& f) const {
        if(!matches(f)||!shows_art(f))return false;
#if defined(__3DS__) || defined(SEANCE_ROT_UI_GPU_TEST)
        C2D_SetTintMode(C2D_TintSolid); // match native chrome's restored tint authority
#endif
        const float drop=float(ease_q8(f.drop_q8))/256.f; // paused retains the admitted source pose
        for(int layer=std::clamp(f.history,0,12)-1;layer>=1;--layer){
            const float k=float(layer)-1+drop;const auto slot=rot_detail::slot(k);const auto s=strip(layer,drop>=1);
            const float scale=slot.half/s.half;
            quad(atlas,s.row,372,s.height,14,slot.center-s.center*scale,scale,std::min(255,int(k*15)));
        }
        quad(atlas,0,372,80,14,130);
        if(moving(f)&&spark_uploaded&&spark==f.spark_serial)quad(sparks,0,372,80,14,130);
        return true;
    }
    bool draw_bottom_art(const RotFrame& f) const {
        if(!matches(f)||!shows_art(f))return false;
        quad(atlas,source_row,292,27,14,63);return true;
    }
};
}
