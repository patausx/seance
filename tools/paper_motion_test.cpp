// One focused host boundary: real ARM-conditional menu_style + draw/font.
// Records UV/data and native glyph rectangles; no image exports or GPU claims.
#include "../platform/3ds/menu_style.h"
#include "../platform/3ds/ui_palette.h"
#include "../platform/3ds/patch_paper.h"
#include "../platform/3ds/paper_presence.h"
#include "../core/ui/draw.h"
#include <citro2d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace m = seance::platform::ui3ds::menu_style;
namespace p = seance::platform::ui3ds::palette;
namespace l = seance::platform::ui3ds::page_layout;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "fail paper-motion:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
static bool guard = false, fail_texture = false;
static unsigned allocations = 0, init_calls = 0, deletes = 0, flushes = 0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t n) { if (guard) ++allocations; return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t s) { if (guard) ++allocations; return __real_calloc(n, s); }
extern "C" void* __wrap_realloc(void* ptr, size_t n) { if (guard) ++allocations; return __real_realloc(ptr, n); }
void* operator new(size_t n) { if (guard) ++allocations; if (void* ptr = std::malloc(n ? n : 1)) return ptr; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, size_t) noexcept { std::free(ptr); }

struct Rect { float x, y, w, h; u32 color; };
struct Trace { std::array<Rect, 2048> rects{}; size_t count = 0; };
struct Image { C3D_Tex* tex; Tex3DS_SubTexture uv; float x, y; unsigned alpha; };
static Trace trace;
static Image image{};
static unsigned images = 0;
static C3D_Tex* live_texture = nullptr;
static bool linear_filter = false, clamped = false;
static void reset() { trace.count = 0; images = 0; }
static bool close(double a, double b) { return std::fabs(a - b) < 0.000002; }
static unsigned channel(u32 c, unsigned shift) { return (c >> shift) & 255; }
static uint64_t texture_hash() {
    CHECK(live_texture && live_texture->data);
    const auto* data = static_cast<const u8*>(live_texture->data);
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < 64 * 64 * 4; ++i) hash = (hash ^ data[i]) * 1099511628211ull;
    return hash;
}
// Independent tile-bit interleave and RGBA8 byte decoder, not runtime helpers.
static u32 texel(int x, int y) {
    x = std::clamp(x, 0, 63); y = std::clamp(y, 0, 63);
    unsigned within = 0;
    for (unsigned bit = 0; bit < 3; ++bit) {
        within |= ((unsigned(x) >> bit) & 1) << (2 * bit);
        within |= ((unsigned(y) >> bit) & 1) << (2 * bit + 1);
    }
    const size_t offset = (size_t((y / 8) * 8 + x / 8) * 64 + within) * 4;
    const auto* bytes = static_cast<const u8*>(live_texture->data) + offset;
    return C2D_Color32(bytes[3], bytes[2], bytes[1], bytes[0]);
}
static double sampled(const Image& call, int x, int y, unsigned shift) {
    const auto& uv = call.uv;
    const double tx = (uv.left + (x + 0.5 - call.x) / uv.width * (uv.right - uv.left)) * 64 - 0.5;
    const double ty = (1 - uv.top + (y + 0.5 - call.y) / uv.height * (uv.top - uv.bottom)) * 64 - 0.5;
    const int ix = int(std::floor(tx)), iy = int(std::floor(ty));
    const double fx = tx - ix, fy = ty - iy;
    return (1 - fy) * ((1 - fx) * channel(texel(ix, iy), shift) + fx * channel(texel(ix + 1, iy), shift)) +
           fy * ((1 - fx) * channel(texel(ix, iy + 1), shift) + fx * channel(texel(ix + 1, iy + 1), shift));
}
static void same_geometry(const Trace& baseline, bool same_colors = false) {
    CHECK(trace.count == baseline.count && trace.count > 0 && images == 0);
    for (size_t i = 0; i < trace.count; ++i) {
        const auto& a = trace.rects[i]; const auto& b = baseline.rects[i];
        CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
        CHECK(a.color >> 24); // no invisible/blank entry glyphs
        if (same_colors) CHECK(a.color == b.color);
    }
}
static Trace title() { reset(); m::page_heading(l::Title::PatchTable, m::ink()); return trace; }
static Trace hero() { reset(); m::heading(30, 75, "sine", m::ink()); return trace; }
static Trace small() { reset(); m::heading(30, 140, "value 42", m::ink()); return trace; }

bool C3D_TexInit(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format) {
    ++init_calls;
    CHECK(width == 64 && height == 64 && format == GPU_RGBA8 && !live_texture);
    if (fail_texture) return false;
    tex->data = std::malloc(width * height * 4);
    CHECK(tex->data);
    tex->width = width; tex->height = height; live_texture = tex;
    std::memset(tex->data, 0xa5, width * height * 4);
    return true;
}
void C3D_TexSetFilter(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM mag, GPU_TEXTURE_FILTER_PARAM min) {
    CHECK(tex == live_texture && mag == GPU_LINEAR && min == GPU_LINEAR); linear_filter = true;
}
void C3D_TexSetWrap(C3D_Tex* tex, GPU_TEXTURE_WRAP_PARAM s, GPU_TEXTURE_WRAP_PARAM t) {
    CHECK(tex == live_texture && s == GPU_CLAMP_TO_EDGE && t == GPU_CLAMP_TO_EDGE); clamped = true;
}
void C3D_TexFlush(C3D_Tex* tex) { CHECK(tex == live_texture); ++flushes; }
void C3D_TexBind(int unit, C3D_Tex* tex) { CHECK(unit == 0 && tex == live_texture); }
void C3D_TexDelete(C3D_Tex* tex) {
    CHECK(tex == live_texture && tex->data); std::free(tex->data); tex->data = nullptr;
    live_texture = nullptr; ++deletes;
}
bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint* tint, float sx, float sy) {
    CHECK(img.tex == live_texture && img.subtex && linear_filter && clamped);
    CHECK(depth == 0 && sx == 1 && sy == 1);
    CHECK(x >= 0 && y >= 0 && x + img.subtex->width <= 400 && y + img.subtex->height <= 240);
    CHECK(img.subtex->width > 0 && img.subtex->height > 0);
    unsigned alpha = 255;
    if (tint) {
        alpha = tint->corners[0].color >> 24;
        for (const auto& corner : tint->corners) CHECK(corner.blend == 0 && corner.color >> 24 == alpha);
    }
    image = {img.tex, *img.subtex, x, y, alpha}; ++images;
    return true;
}
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color) {
    CHECK(z == 0 && x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 400 && y + h <= 240);
    CHECK(trace.count < trace.rects.size()); trace.rects[trace.count++] = {x, y, w, h, color}; return true;
}
bool C2D_DrawLine(float, float, u32, float, float, u32, float, float) { CHECK(false); return false; }

static void static_fallback() {
    reset(); m::paper(320);
    CHECK(images == 0 && trace.count > 0);
    for (int y = 0; y < 240; ++y) {
        unsigned covered = 0;
        for (size_t i = 0; i < trace.count; ++i) {
            const auto& r = trace.rects[i];
            if (y >= r.y && y < r.y + r.h) {
                CHECK(r.x == 0 && r.w == 320 && r.color == p::paper_color(y)); ++covered;
            }
        }
        CHECK(covered == 1 && m::paper_color(y) == p::paper_color(y));
    }
}
static Image full_paper() {
    reset(); m::paper(400); CHECK(images == 1 && trace.count == 0);
    CHECK(image.x == 0 && image.y == 0 && image.uv.width == 400 && image.uv.height == 240 && image.alpha == 255);
    return image;
}
static void crop(const Image& full, int x, int y, int w, int h, bool row = false, int alpha = 255) {
    const uint64_t hash = texture_hash(); const unsigned flushed = flushes;
    reset();
    if (row) m::paper(w, y, y + h); else m::paper_rect(x, y, w, h, alpha);
    CHECK(images == 1 && trace.count == 0 && image.tex == full.tex);
    CHECK(image.x == x && image.y == y && image.uv.width == w && image.uv.height == h);
    CHECK(std::abs(int(image.alpha) - alpha) <= 1);
    const auto& a = image.uv; const auto& b = full.uv;
    CHECK(close(a.left, b.left + x / 400.0 * (b.right - b.left)));
    CHECK(close(a.right, b.left + (x + w) / 400.0 * (b.right - b.left)));
    CHECK(close(a.top, b.top + y / 240.0 * (b.bottom - b.top)));
    CHECK(close(a.bottom, b.top + (y + h) / 240.0 * (b.bottom - b.top)));
    for (int yy : {y, y + h / 2, y + h - 1}) for (int xx : {x, x + w / 2, x + w - 1})
        for (unsigned shift : {0u, 8u, 16u}) {
            const double foreground = sampled(image, xx, yy, shift);
            CHECK(std::fabs(foreground - sampled(full, xx, yy, shift)) < 0.001);
            const double over = (foreground * image.alpha + 180 * (255 - image.alpha)) / 255;
            CHECK(over + 1e-9 >= std::min(foreground, 180.0) && over - 1e-9 <= std::max(foreground, 180.0));
        }
    CHECK(texture_hash() == hash && flushes == flushed);
}
static void exercise(p::Theme theme) {
    p::set(theme); m::ambient_shutdown(); static_fallback();
    const Trace baseline = title(), hero_before = hero(), small_before = small();
    fail_texture = true;
    CHECK(!m::ambient_init()); m::ambient_frame(1000); CHECK(!m::ambient_active());
    static_fallback(); title(); same_geometry(baseline, true); m::ambient_shutdown();
    fail_texture = false;
    const unsigned initializations = init_calls, old_deletes = deletes, first_flush = flushes;
    CHECK(m::ambient_init()); CHECK(!m::ambient_active()); static_fallback();
    CHECK(m::ambient_init() && init_calls == initializations + 1);
    auto* retained = live_texture->data;
    const uint64_t unused = texture_hash();
    guard = true;
    bool tone_changed = false;
    Trace entry{};
    for (unsigned elapsed : {0u, 80u, 170u, 1200u, 20000u}) {
        m::patch_capture(false,false,1200+elapsed);
        m::ambient_frame(1200+elapsed); CHECK(m::ambient_active());
        static_fallback(); // exact rows, no drift/upload on inactive work pages
        for (int y=0;y<240;++y) CHECK(m::patch_sample(false,17,y)==p::paper_color(y));
        m::paper(320); title(); same_geometry(baseline);
        if (elapsed == 0) entry = trace;
        for (size_t i = 0; i < trace.count; ++i) {
            const auto& r = trace.rects[i];
            tone_changed |= r.color != entry.rects[i].color;
            const int contrast = std::abs(int(channel(r.color, 0)) - int(channel(p::paper_color(int(r.y)), 0)));
            const int original = std::abs(int(channel(m::ink(), 0)) - int(channel(p::paper_color(int(r.y)), 0)));
            CHECK(contrast * 100 >= original * 60);
        }
        const Trace frozen_title = trace; title(); same_geometry(frozen_title, true);
        reset(); m::heading(20, 13, "patch table", m::ink()); same_geometry(frozen_title, true);
        hero(); same_geometry(hero_before, true); small(); same_geometry(small_before, true);
        reset(); seance::ui::text(30, 140, "value 42", m::ink()); same_geometry(small_before, true);
        m::paper(400); reset(); m::page_heading(l::Title::GoTo, m::ink());
        m::paper(320); title(); same_geometry(frozen_title, true);
        CHECK(texture_hash()==unused && flushes==first_flush);
    }
    CHECK(tone_changed); // existing ink sheen remains independent of static paper
    CHECK(live_texture->data == retained && init_calls == initializations + 1 && allocations == 0);
    guard = false;
    m::ambient_shutdown(); CHECK(!m::ambient_active() && !live_texture && deletes == old_deletes + 1);
    static_fallback(); title(); same_geometry(baseline, true);
}
static void patch_field() {
    namespace field = seance::platform::ui3ds::patch_paper;
    uint8_t initial[field::Size*field::Size], loop[field::Size*field::Size];
    field::paint(initial,0); field::paint(loop,field::PeriodMs);
    CHECK(!std::memcmp(initial,loop,sizeof(initial)));
    // Independent Python double-precision source checkpoints, sampled at the
    // reduced grid's centers (the source's centered resize subtracts two px).
    CHECK(initial[0]==255 && initial[5*64+10]==132 && initial[32*64+20]==255);
    CHECK(initial[60*64+40]==255 && initial[63*64+63]==0);
    field::paint(loop,17900);
    int boundary_delta=0;
    for(size_t i=0;i<sizeof(initial);++i) boundary_delta=std::max(boundary_delta,std::abs(int(initial[i])-int(loop[i])));
    CHECK(boundary_delta>0 && boundary_delta<16); // ordinary adjacent 10Hz step
    field::Clock clock;
    clock.capture(true,false,1000); CHECK(clock.phase==0);
    clock.capture(true,false,1123); CHECK(clock.phase==123);
    clock.capture(true,true,1140); clock.capture(true,true,100000); CHECK(clock.phase==123);
    clock.capture(true,false,100100); CHECK(clock.phase==123);
    clock.capture(true,false,100177); CHECK(clock.phase==200);
    clock.capture(false,false,100200); clock.capture(true,false,200000); CHECK(clock.phase==200);
    clock.capture(true,false,199000); CHECK(clock.phase==200); // backwards clock
    clock.capture(true,false,217000); CHECK(clock.phase==200);
}
static void patch_gpu() {
    p::set(p::Theme::Graphite); CHECK(m::ambient_init());
    auto* retained=live_texture->data;
    m::ambient_frame(0); const uint64_t quiet=texture_hash();
    const unsigned quiet_flush=flushes;
    guard=true;
    m::patch_capture(true,false,760); // cold entry starts at static paper
    static_fallback();
    m::patch_capture(true,false,1000); // 240ms entrance is now settled
    CHECK(texture_hash()==quiet && flushes==quiet_flush); // capture NEVER uploads
    m::ambient_frame(1000);
    CHECK(texture_hash()!=quiet && flushes==quiet_flush+1); // mode, even same bucket
    const uint64_t initial=texture_hash();
    const Image top=full_paper();
    CHECK(close(top.uv.left,0) && close(top.uv.right,1));
    CHECK(close(top.uv.top,1) && close(top.uv.bottom,1-240./496));
    crop(top,37,53,81,49); crop(top,0,89,400,1,true);
    reset(); m::paper(320); const Image bottom=image;
    CHECK(images==1 && trace.count==0 && close(bottom.uv.left,.1) && close(bottom.uv.right,.9));
    CHECK(close(bottom.uv.top,1-256./496) && close(bottom.uv.bottom,0));
    reset(); m::paper_rect(17,39,53,21,128);
    CHECK(images==1 && close(image.uv.left,57./400) && close(image.uv.top,1-295./496));
    for(bool upper:{false,true}) {
        const Image& full=upper?top:bottom;
        for(int y:{0,17,119,239})for(int x:{0,41,159,319})for(unsigned shift:{0u,8u,16u}) {
            const auto cpu=m::patch_sample(upper,x+.5f,y+.5f);
            CHECK(std::fabs(channel(cpu,shift)-sampled(full,x,y,shift))<=.501);
        }
    }
    // The full strong graphite field and all restored crops stay within the
    // approved endpoints; CPU cache samples are the same filtered source.
    bool dark=false,pale=false;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto value=texel(x,y);
        CHECK(channel(value,0)>=17 && channel(value,0)<=66);
        CHECK(channel(value,8)>=18 && channel(value,8)<=61);
        CHECK(channel(value,16)>=22 && channel(value,16)<=75);
        dark|=value==p::rgb(17,18,22); pale|=value==p::rgb(66,61,75);
    }
    CHECK(dark && pale);
    const Trace steady=title();
    m::patch_capture(true,false,1100); CHECK(texture_hash()==initial);
    m::ambient_frame(1100); CHECK(texture_hash()!=initial);
    const uint64_t phase100=texture_hash();
    title(); same_geometry(steady,true); // patch title does not shimmer
    const unsigned before_modal=flushes;
    m::patch_capture(true,true,1120); m::ambient_frame(1120);
    m::patch_capture(true,true,20120); m::ambient_frame(20120);
    CHECK(texture_hash()==phase100 && flushes==before_modal);
    title(); same_geometry(steady,true);
    // Palette-only repaint during the same frozen bucket; no phase reset.
    p::set(p::Theme::Pink); m::patch_capture(true,true,20120);
    CHECK(texture_hash()==phase100 && flushes==before_modal);
    m::ambient_frame(20120); CHECK(texture_hash()!=phase100 && flushes==before_modal+1);
    // Light palettes use moving shadows only: a stronger dark endpoint up to
    // untouched paper, with no pale wash that can disappear on the LCD.
    const auto pink=p::bg(), pink_dark=seance::platform::ui3ds::theme_config::mix(pink,p::gray(0),64);
    dark=pale=false;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto value=texel(x,y);
        for(unsigned shift:{0u,8u,16u})
            CHECK(channel(value,shift)>=channel(pink_dark,shift) && channel(value,shift)<=channel(pink,shift));
        dark|=value==pink_dark; pale|=value==pink;
    }
    CHECK(dark && pale);
    p::animate_to(p::Theme::Graphite,20120);
    int previous=int(channel(m::patch_sample(true,43,91),0));
    for(unsigned t=1;t<=p::kTransitionMs;++t) {
        p::capture(20120+t); m::patch_capture(true,true,20120+t); m::ambient_frame(20120+t);
        const int current=int(channel(m::patch_sample(true,43,91),0));
        CHECK(current<=previous && previous-current<=3); previous=current;
    }
    CHECK(texture_hash()==phase100);
    for(auto theme:{p::Theme::Grimoire,p::Theme::BlackMass,p::Theme::Custom}) {
        p::set(theme); m::patch_capture(true,true,20300); m::ambient_frame(20300);
        CHECK(texture_hash()!=phase100);
    }
    auto custom=p::settings(); custom.custom.paper=p::rgb(20,80,40); p::set(custom);
    m::patch_capture(true,true,20300); const unsigned pre_custom=flushes;
    m::ambient_frame(20300); CHECK(flushes==pre_custom+1);
    const auto own_paper=m::patch_sample(true,43,91);
    CHECK(channel(own_paper,8)>channel(own_paper,16) && channel(own_paper,16)>channel(own_paper,0));
    p::set(p::Theme::Graphite); m::patch_capture(true,true,20300); m::ambient_frame(20300);
    CHECK(texture_hash()==phase100);
    m::patch_capture(true,false,30000); m::ambient_frame(30000); CHECK(texture_hash()==phase100);
    m::patch_capture(true,false,30100); m::ambient_frame(30100); CHECK(texture_hash()!=phase100);
    const uint64_t phase200=texture_hash();
    m::patch_capture(true,false,48100); m::ambient_frame(48100); CHECK(texture_hash()==phase200);
    m::patch_capture(false,false,49000); m::ambient_frame(49000);
    CHECK(texture_hash()==phase200); // outgoing field does not snap on navigation
    m::patch_capture(false,false,49120); m::ambient_frame(49120);
    CHECK(texture_hash()!=phase200); full_paper(); // same dual UV during fade
    m::patch_capture(false,false,49240); m::ambient_frame(49240); static_fallback();
    const auto flat_flushes=flushes;
    m::patch_capture(false,false,90000); m::ambient_frame(90000); static_fallback();
    CHECK(flushes==flat_flushes); // inactive work pages do no background upload
    m::patch_capture(true,false,99000); m::ambient_frame(99000); static_fallback();
    m::patch_capture(true,false,99240); m::ambient_frame(99240);
    CHECK(texture_hash()==phase200); // same field restored, no inactive catch-up
    CHECK(live_texture->data==retained && allocations==0);
    guard=false; m::ambient_shutdown();
}
static void patch_fallback() {
    p::set(p::Theme::Graphite); fail_texture=true;
    CHECK(!m::ambient_init());
    guard=true; m::patch_capture(true,false,0); static_fallback();
    m::patch_capture(true,false,240);
    for(bool top:{false,true}) {
        reset(); m::paper(top?400:320);
        CHECK(images==0 && trace.count==size_t(top?240:192));
        for(size_t i=0;i<trace.count;++i) {
            const auto& r=trace.rects[i]; CHECK(r.w==20 && r.h==20);
            CHECK(r.color==m::patch_sample(top,r.x+.5f,r.y+.5f));
            CHECK(r.color==m::patch_sample(top,r.x+19.5f,r.y+19.5f));
        }
        reset(); m::paper_rect(17,39,53,21);
        CHECK(images==0 && trace.count<=12);
        for(size_t i=0;i<trace.count;++i) {
            const auto& r=trace.rects[i]; CHECK(r.color==m::patch_sample(top,r.x+.5f,r.y+.5f));
        }
    }
    const auto frozen=m::patch_sample(true,43,91);
    m::patch_capture(true,true,5000); m::ambient_frame(5000);
    CHECK(m::patch_sample(true,43,91)==frozen && allocations==0);
    guard=false; m::ambient_shutdown(); fail_texture=false;
}
static void presence_clock() {
    namespace life = seance::platform::ui3ds::paper_presence;
    using S = life::Scene;
    life::Motion a;
    a.capture(S::Patch,false,1000); CHECK(a.amount==0 && a.phase()==0);
    a.capture(S::Patch,false,1120); CHECK(a.amount==128 && a.phase()==0);
    a.capture(S::Still,false,1120); CHECK(a.amount==128);
    a.capture(S::Still,false,1240); CHECK(a.amount==64);
    a.capture(S::Patch,false,1240); CHECK(a.amount==64);
    a.capture(S::Patch,false,1360); CHECK(a.amount==160);
    a.capture(S::Patch,true,1400); a.capture(S::Patch,true,90000);
    CHECK(a.amount==160 && a.phase()==0); // freeze mid-fade, not just its phase
    a.capture(S::Patch,false,91000); CHECK(a.amount==160);
    a.capture(S::Patch,false,91120); CHECK(a.amount==255 && a.phase()==0);
    a.capture(S::Patch,false,91219); CHECK(a.phase()==0);
    a.capture(S::Patch,false,91220); CHECK(a.phase()==100);
    a.capture(S::Still,false,91230); CHECK(a.amount==255 && a.phase()==100);
    a.capture(S::Still,false,91470); CHECK(a.amount==0);
    a.capture(S::Still,false,900000); CHECK(a.phase()==100);
    a.capture(S::Patch,false,900000); a.capture(S::Patch,false,900240);
    CHECK(a.amount==255 && a.phase()==100);
    a.capture(S::Patch,false,900140); CHECK(a.amount==255 && a.phase()==100);
    life::Motion b;
    b.capture(S::Startup,false,0); b.capture(S::Startup,false,240);
    CHECK(b.amount==102 && b.phase()==0);
    b.capture(S::Startup,false,340); CHECK(b.phase()==60); // 30s, update at 10Hz
    b.capture(S::Startup,false,30240); CHECK(b.phase()==0 && b.amount==102);
    b.capture(S::Patch,false,30240); CHECK(b.amount==102 && b.phase()==0);
    b.capture(S::Patch,false,30480); CHECK(b.amount==255 && b.phase()==0);
    b.capture(S::Patch,false,48480); CHECK(b.phase()==0); // patch18s remains exact
    b.capture(S::Still,false,48480); b.capture(S::Still,false,~uint64_t(0));
    CHECK(b.amount==0 && b.phase()==0); // large delta cannot overflow fade math
    life::Motion light;
    light.capture(S::Startup,false,1000,255); light.capture(S::Startup,false,1240,255);
    CHECK(light.amount==255 && light.phase()==0);
    light.capture(S::Startup,false,1340,255); CHECK(light.phase()==60);
    light.capture(S::Startup,false,1340,102); CHECK(light.amount==255); // theme retarget, no jump
    light.capture(S::Startup,false,1460,102); CHECK(light.amount>102 && light.amount<255);
    const auto halfway=light.amount;
    light.capture(S::Still,false,1460); CHECK(light.amount==halfway);
    light.capture(S::Still,false,1700); CHECK(light.amount==0);
    light.capture(S::Startup,false,1700,255); light.capture(S::Startup,false,1940,255);
    light.capture(S::Patch,false,1940); CHECK(light.amount==255); // same light shadow endpoint
}
static void presence_gpu() {
    using seance::platform::ui3ds::theme_config::mix;
    for(auto theme:{p::Theme::Pink,p::Theme::Graphite,p::Theme::Grimoire,p::Theme::BlackMass,p::Theme::Custom}) {
        p::set(theme); CHECK(m::ambient_init());
        auto* retained=live_texture->data;
        guard=true;
        m::startup_capture(true,1000); m::ambient_frame(1000); static_fallback();
        unsigned uploaded=flushes;
        for(unsigned t:{1120u,1240u}) {
            m::startup_capture(true,t); CHECK(flushes==uploaded); // CPU-only capture
            m::ambient_frame(t); CHECK(flushes==++uploaded);
            const uint64_t steady=texture_hash();
            m::ambient_frame(t); CHECK(flushes==uploaded && texture_hash()==steady);
            for(bool top:{false,true}) {
                reset(); m::paper(top?400:320); CHECK(images==1 && trace.count==0);
                const Image full=image;
                for(int y:{0,17,119,239})for(int x:{0,41,159,319})for(unsigned shift:{0u,8u,16u})
                    CHECK(std::fabs(channel(m::patch_sample(top,x+.5f,y+.5f),shift)-sampled(full,x,y,shift))<=.501);
                reset(); m::paper_rect(17,39,53,21,128);
                CHECK(images==1 && image.alpha==128 && texture_hash()==steady && flushes==uploaded);
                for(int y:{39,59})for(int x:{17,69})for(unsigned shift:{0u,8u,16u})
                    CHECK(std::fabs(sampled(full,x,y,shift)-sampled(image,x,y,shift))<.001);
            }
        }
        const auto initial=texture_hash();
        if(theme==p::Theme::Graphite) {
            bool dark=false,pale=false;
            for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                const auto c=texel(x,y);
                CHECK(channel(c,0)>=28 && channel(c,0)<=48);
                CHECK(channel(c,8)>=29 && channel(c,8)<=46);
                CHECK(channel(c,16)>=30 && channel(c,16)<=52);
                dark|=c==p::rgb(28,29,30); pale|=c==p::rgb(48,46,52);
            }
            CHECK(dark && pale);
        }
        if(!p::visual_graphite()) {
            // Light menu must show the exact shadow endpoints used on Patch,
            // rather than blending them back into bright paper at only 40%.
            const auto paper=p::bg(), shadow=mix(paper,p::gray(0),64);
            bool dark=false, pale=false;
            for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
                const auto c=texel(x,y);
                dark|=c==shadow; pale|=c==paper;
                for(unsigned shift:{0u,8u,16u})
                    CHECK(channel(c,shift)>=channel(shadow,shift) && channel(c,shift)<=channel(paper,shift));
            }
            CHECK(dark && pale);
        }
        m::startup_capture(true,1339); m::ambient_frame(1339);
        CHECK(texture_hash()==initial && flushes==uploaded);
        m::startup_capture(true,1340); m::ambient_frame(1340); CHECK(flushes==++uploaded);
        const auto moved=texture_hash(); CHECK(moved!=initial);
        m::startup_capture(false,1340); m::ambient_frame(1340); CHECK(texture_hash()==moved);
        m::startup_capture(false,1460); m::ambient_frame(1460); CHECK(flushes==++uploaded);
        const auto half=texture_hash(); CHECK(half!=moved);
        m::startup_capture(true,1460); m::ambient_frame(1460); CHECK(texture_hash()==half);
        m::startup_capture(true,1700); m::ambient_frame(1700);
        CHECK(texture_hash()==moved); // interrupted reverse resumes same visible field
        m::patch_capture(true,false,1700); m::ambient_frame(1700); CHECK(texture_hash()==moved);
        m::patch_capture(true,false,1820); m::ambient_frame(1820);
        const auto middle=texture_hash(); CHECK(middle!=moved);
        uploaded=flushes;
        m::patch_capture(true,true,1820); m::ambient_frame(1820);
        m::patch_capture(true,true,90000); m::ambient_frame(90000);
        CHECK(texture_hash()==middle && flushes==uploaded); // blur snapshot's level frozen
        m::patch_capture(true,false,90100); m::ambient_frame(90100); CHECK(texture_hash()==middle);
        m::patch_capture(true,false,90220); m::ambient_frame(90220); CHECK(texture_hash()!=middle);
        m::patch_capture(false,false,90220); m::ambient_frame(90220);
        m::patch_capture(false,false,90460); m::ambient_frame(90460); static_fallback();
        uploaded=flushes;
        m::patch_capture(false,false,999999); m::ambient_frame(999999); static_fallback();
        CHECK(flushes==uploaded && live_texture->data==retained && allocations==0);
        guard=false; m::ambient_shutdown();
    }
}
int main() {
    exercise(p::Theme::Pink); exercise(p::Theme::Graphite);
    patch_field(); patch_gpu(); patch_fallback(); presence_clock(); presence_gpu();
    std::puts("pass paper-motion: static work pages/title preservation; patch18s/startup30s/240ms reversible fade; dual-sheet/crop/blur freeze/theme/sample/fallback; retained16k gpu+5k cpu; no frame allocation or capture/draw upload (host, no exports)");
}
