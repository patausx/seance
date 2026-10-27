// Actual GPU adapter + model; only Citro API effects are simulated, no media.
#include <citro2d.h>
#include "platform/3ds/stylus_feedback.h"
#include "platform/3ds/stylus_liquid_model.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace seance::platform::ui3ds;
namespace {
using RGB = std::array<float,3>;
RGB screen[240][320], saved[240][320];
uint8_t storage[stylus_liquid::kBytes];
int allocations=0, deletes=0, uploads=0, binds=0, images=0, circles=0;
bool fail=false, live=false, pending=false;
C2D_TintMode mode=C2D_TintSolid;
GPU_BLENDFACTOR src=GPU_SRC_ALPHA,dst=GPU_ONE_MINUS_SRC_ALPHA;
GPU_BLENDFACTOR srca=GPU_SRC_ALPHA,dsta=GPU_ONE_MINUS_SRC_ALPHA;
struct Quad { C2D_Image image; int x,y; C2D_ImageTint tint; } queued{};
constexpr u32 accent=0xffe89ab5u; // graphite lilac: R181 G154 B232
void background(float value) {
    for(auto& row:screen) for(auto& c:row) c={value,value,value};
    for(auto& c:screen[120]) c={.96f,.96f,.96f};
}
void restored() {
    assert(!pending && mode==C2D_TintSolid && src==GPU_SRC_ALPHA && dst==GPU_ONE_MINUS_SRC_ALPHA);
    assert(srca==GPU_SRC_ALPHA && dsta==GPU_ONE_MINUS_SRC_ALPHA);
}
}
bool C3D_TexInit(C3D_Tex* t,unsigned w,unsigned h,GPU_TEXCOLOR) {
    ++allocations; assert(!live && w==64 && h==64);
    if(fail) return false;
    live=true; t->data=storage; std::memset(storage,0xcc,sizeof storage); return true;
}
void C3D_TexDelete(C3D_Tex* t) { assert(live && !pending && t->data==storage); live=false; ++deletes; }
void C3D_TexSetFilter(C3D_Tex*,GPU_TEXTURE_FILTER_PARAM,GPU_TEXTURE_FILTER_PARAM) {}
void C3D_TexSetWrap(C3D_Tex*,GPU_TEXTURE_WRAP_PARAM,GPU_TEXTURE_WRAP_PARAM) {}
void C3D_TexFlush(C3D_Tex*) { assert(live && !pending); ++uploads; }
void C3D_TexBind(int unit,C3D_Tex* t) { assert(unit==0 && t->data==storage && !pending); ++binds; }
void C3D_AlphaBlend(GPU_BLENDEQUATION,GPU_BLENDEQUATION,GPU_BLENDFACTOR s,GPU_BLENDFACTOR d,
                    GPU_BLENDFACTOR sa,GPU_BLENDFACTOR da) {
    assert(!pending); src=s; dst=d; srca=sa; dsta=da;
}
bool C2D_SetTintMode(C2D_TintMode m) { assert(!pending); mode=m; return true; }
bool C2D_DrawImageAt(C2D_Image image,float x,float y,float z,const C2D_ImageTint* tint,float sx,float sy) {
    assert(!pending && live && z==0 && sx==1 && sy==1 && int(x)==x && int(y)==y);
    assert(image.subtex->width==64 && image.subtex->height==64);
    assert(image.subtex->left==0 && image.subtex->right==1 && image.subtex->top==1 && image.subtex->bottom==0);
    queued={image,int(x),int(y),tint?*tint:C2D_ImageTint{0xffffffffu,0.f}};
    pending=true; ++images; return true;
}
void C2D_Flush() {
    if(!pending) return;
    assert(srca==GPU_ZERO && dsta==GPU_ONE); // preserve destination alpha
    const bool inverse=src==GPU_ONE_MINUS_DST_COLOR;
    assert(inverse ? (dst==GPU_ONE_MINUS_SRC_ALPHA && mode==C2D_TintSolid) :
                    (src==GPU_ZERO && dst==GPU_ONE_MINUS_SRC_COLOR && mode==C2D_TintMult));
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
        const int px=queued.x+x, py=queued.y+y;
        if(px<0||px>=320||py<0||py>=240) continue;
        const auto offset=4*stylus_liquid::detail::morton(x,y);
        const float a=storage[offset]/255.f;
        for(int c=0;c<3;++c) {
            float& d=screen[py][px][c];
            if(inverse) d=a*(1-d)+d*(1-a);
            else {
                assert(queued.tint.blend==1.f && (queued.tint.color>>24)==255);
                const float attenuation=((queued.tint.color>>(8*c))&255u)/255.f;
                d*=1-a*attenuation;
            }
            assert(std::isfinite(d)&&d>=0&&d<=1);
        }
    }
    pending=false;
}
bool C2D_DrawCircleSolid(float x,float y,float z,float radius,u32 color) {
    restored(); assert(x>=0&&x<320&&y>=0&&y<240&&z==0&&radius==3&&(color>>24)<=200);
    ++circles; return true;
}
int main() {
    using namespace stylus_feedback;
    assert(init() && init() && allocations==1);
    draw(); upload(); assert(!images&&!uploads);
    capture(true,160,120,1000,accent); capture(true,160,120,1220,accent);
    draw(); assert(images==0); // a capture without a successful upload is not stale
    upload(); upload(); assert(uploads==1);
    background(36.f/255); std::memcpy(saved,screen,sizeof screen);
    draw(); restored(); assert(images==2&&binds==1&&!circles);
    assert(screen[119][160][0]>.55f && screen[119][160][2]>screen[119][160][1]);
    assert(screen[120][160][0]<.40f); // SAME frame: white rail turns into dark ink
    assert(screen[0][0]==saved[0][0] && screen[88][128]==saved[88][128]);
    std::memcpy(saved,screen,sizeof screen);
    background(36.f/255); draw(); restored();
    assert(std::memcmp(saved,screen,sizeof screen)==0 && uploads==1); // pure frozen draw
    background(.94f); draw(); restored();
    assert(screen[119][160][0]<.4f); // CURRENT destination, never last-frame pixels
    capture(true,319,239,1240,0xff000000u); draw(); assert(images==6);
    upload(); background(.4f); draw(); restored(); assert(images==8); // unclamped corner crop
    capture(true,320,240,1260,accent); upload(); draw(); assert(images==8);
    capture(false,0,0,1280,accent); upload(); draw(); assert(images==8);
    capture(true,80,80,1300,accent); capture(true,80,80,1520,accent); upload();
    capture(false,0,0,1530,accent); upload(); draw();
    const int before=images;
    capture(false,0,0,1880,accent); upload(); draw(); assert(images==before);
    shutdown(); restored(); assert(deletes==1&&!live);
    std::puts("pass stylus gpu: two bounded quads, frozen raster, same-frame dark/light reaction, transparent locality, edges/release/invalid, upload guard, exact blend/tint restoration (API simulation)");

    fail=true; const int attempts=allocations;
    assert(!init()&&!init()&&allocations==attempts+1);
    capture(true,30,20,2000,accent); capture(true,30,20,2220,accent);
    const int texture_uploads=uploads;
    upload(); draw(); draw(); assert(circles==2&&uploads==texture_uploads&&allocations==attempts+1);
    capture(false,0,0,2230,accent); capture(false,0,0,2580,accent); draw(); assert(circles==2);
    shutdown(); assert(deletes==1);
    std::puts("pass stylus fallback: single failed init, no lazy allocation/upload, passive small contact, clean shutdown; 2/2");
}
