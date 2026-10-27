#include "../platform/3ds/stylus_liquid_model.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace liquid = seance::platform::ui3ds::stylus_liquid;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "fail stylus-liquid:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
static bool guard = false;
static unsigned allocations = 0, raster_frames = 0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t n) { if (guard) ++allocations; return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t s) { if (guard) ++allocations; return __real_calloc(n, s); }
extern "C" void* __wrap_realloc(void* p, size_t n) { if (guard) ++allocations; return __real_realloc(p, n); }
void* operator new(size_t n) { if (guard) ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

static bool near(float a, float b) { return std::fabs(a-b) < .0001f; }
static unsigned offset(int x, int y) {
    // Independent decoder: tiled Morton 8x8, logical row zero at screen top.
    unsigned bits = 0;
    for (unsigned bit = 0; bit < 3; ++bit) {
        bits |= ((unsigned(x) >> bit) & 1u) << (2*bit);
        bits |= ((unsigned(y) >> bit) & 1u) << (2*bit+1);
    }
    return 4u * (unsigned((y/8)*8+x/8)*64u + bits);
}
static void geometry(const liquid::Frame& f, int x, int y) {
    CHECK(f.active && f.x == x-32 && f.y == y-32);
    CHECK(f.kernels[0].x == 32.f && f.kernels[0].y == 32.f);
    CHECK(f.scale >= 0.f && f.scale <= 1.f && f.opacity >= 0.f && f.opacity <= 1.f);
    for (int k = 1; k < 4; ++k) {
        const float dx = f.kernels[k].x-f.kernels[k-1].x;
        const float dy = f.kernels[k].y-f.kernels[k-1].y;
        CHECK(dx*dx+dy*dy <= 5.8001f*5.8001f*f.scale*f.scale);
        CHECK(f.kernels[k].sigma > 0.f);
    }
}
static void connected(const std::array<uint8_t, 4096>& alpha, unsigned threshold) {
    std::array<bool, 4096> visited{};
    std::array<int, 4096> queue{};
    int count = 0, first = -1;
    for (int i = 0; i < 4096; ++i) if (alpha[i] >= threshold) {
        ++count; if (first < 0) first = i;
    }
    if (!count) return;
    queue[0] = first; visited[first] = true;
    int tail = 1;
    for (int head = 0; head < tail; ++head) {
        const int x = queue[head]%64, y = queue[head]/64;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if ((!dx && !dy) || x+dx < 0 || x+dx >= 64 || y+dy < 0 || y+dy >= 64) continue;
            const int next = (y+dy)*64+x+dx;
            if (!visited[next] && alpha[next] >= threshold) {
                visited[next] = true; queue[tail++] = next;
            }
        }
    }
    CHECK(tail == count); // one meaningful body, not independently drawn dots
}
static unsigned raster(const liquid::Frame& frame) {
    struct Guarded {
        std::array<uint8_t, 32> before;
        std::array<uint8_t, liquid::kBytes> data;
        std::array<uint8_t, 32> after;
    } a, b;
    std::memset(&a, 0xa5, sizeof a); std::memset(&b, 0xa5, sizeof b);
    std::array<uint8_t, sizeof frame> frozen{};
    std::memcpy(frozen.data(), &frame, sizeof frame);
    liquid::paint(a.data.data(), frame);
    liquid::paint(b.data.data(), frame);
    CHECK(a.data == b.data && std::memcmp(frozen.data(), &frame, sizeof frame) == 0);
    for (uint8_t c : a.before) CHECK(c == 0xa5);
    for (uint8_t c : a.after) CHECK(c == 0xa5);
    std::array<uint8_t, 4096> alpha{};
    unsigned sum = 0, peak = 0;
    int min_x = 64, max_x = -1, min_y = 64, max_y = -1;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        const unsigned p = offset(x,y);
        const uint8_t v = a.data[p];
        CHECK(v <= 217 && v == a.data[p+1] && v == a.data[p+2] && v == a.data[p+3]);
        if (!x || !y || x == 63 || y == 63) CHECK(v == 0);
        alpha[y*64+x] = v; sum += v; peak = std::max(peak, unsigned(v));
        if (v) { min_x = std::min(min_x,x); max_x = std::max(max_x,x);
                 min_y = std::min(min_y,y); max_y = std::max(max_y,y); }
    }
    if (!frame.active || frame.opacity == 0.f) CHECK(sum == 0);
    if (frame.active && frame.opacity > .05f) {
        CHECK(sum > 0 && peak > 0 && alpha[32*64+32] > 0);
        CHECK(max_x-min_x+1 <= 48 && max_y-min_y+1 <= 48);
        connected(alpha, std::max(2u, unsigned(32.f*frame.opacity)));
        connected(alpha, std::max(2u, unsigned(128.f*frame.opacity)));
    }
    ++raster_frames;
    return sum;
}
static void lifecycle() {
    liquid::State s; liquid::Frame f;
    liquid::capture(s,f,false,0,0,0); CHECK(!f.active); raster(f);
    liquid::capture(s,f,true,100,120,1000); geometry(f,100,120);
    CHECK(f.opacity == 0.f && f.scale == .25f); raster(f);
    liquid::capture(s,f,true,100,120,1219); CHECK(f.opacity < 1.f && f.opacity > .99f);
    liquid::capture(s,f,true,100,120,1220); CHECK(f.opacity == 1.f && f.scale == 1.f);
    const liquid::Frame held = f;
    liquid::capture(s,f,false,-999,999,1240); CHECK(f.opacity == 1.f);
    float opacity = f.opacity, scale = f.scale;
    unsigned mass = raster(f);
    for (uint64_t ms : {1290u,1390u,1490u,1589u}) {
        liquid::capture(s,f,false,0,0,ms); geometry(f,100,120);
        CHECK(f.opacity < opacity && f.scale < scale);
        const unsigned next = raster(f); CHECK(next <= mass);
        mass = next; opacity = f.opacity; scale = f.scale;
    }
    liquid::capture(s,f,false,0,0,1590); CHECK(!f.active && !s.alive); raster(f);
    CHECK(raster(held) > 0); // old frame is self-contained after state expiry

    liquid::capture(s,f,true,100,120,1600);
    liquid::capture(s,f,true,105,123,1620);
    liquid::capture(s,f,false,0,0,1630); const float quick = f.opacity;
    liquid::capture(s,f,false,0,0,1640); CHECK(f.opacity < quick);
    liquid::capture(s,f,true,280,200,1641); geometry(f,280,200); CHECK(f.opacity == 0.f);
    for (const auto& p : s.followers) CHECK(p.x == 280.f && p.y == 200.f);
    for (const auto invalid : {liquid::Point{-1,0},liquid::Point{320,0},
                               liquid::Point{0,-1},liquid::Point{0,240}}) {
        liquid::capture(s,f,true,100,100,1700);
        liquid::capture(s,f,true,100,100,1920); CHECK(f.opacity == 1.f);
        liquid::capture(s,f,true,int(invalid.x),int(invalid.y),1930);
        CHECK(!f.active && !s.alive); raster(f);
        liquid::capture(s,f,false,0,0,1940); CHECK(!f.active); raster(f);
    }
    liquid::capture(s,f,true,100,100,2000);
    liquid::capture(s,f,true,100,100,2220);
    liquid::capture(s,f,true,110,100,2100); geometry(f,110,100); CHECK(f.opacity == 0.f);
    liquid::capture(s,f,true,110,100,2320);
    liquid::capture(s,f,false,0,0,2300); CHECK(!f.active && !s.alive);
    liquid::capture(s,f,true,100,100,3000);
    liquid::capture(s,f,true,101,100,3251); CHECK(f.opacity == 0.f && s.started_ms == 3251);
    liquid::capture(s,f,false,0,0,3510); CHECK(!f.active && !s.alive);
    liquid::capture(s,f,true,0,0,4000);
    liquid::capture(s,f,true,319,239,4010); geometry(f,319,239); CHECK(f.opacity == 0.f);
    for (const auto& p : s.followers) CHECK(p.x == 319.f && p.y == 239.f);
    const uint64_t large = std::numeric_limits<uint64_t>::max();
    liquid::capture(s,f,true,50,50,large-300);
    liquid::capture(s,f,true,50,50,large-80); CHECK(f.opacity == 1.f);
    liquid::capture(s,f,true,50,50,0); CHECK(f.opacity == 0.f);
}
static void motion() {
    liquid::State a, b; liquid::Frame fa, fb;
    liquid::capture(a,fa,true,100,100,0); liquid::capture(b,fb,true,100,100,0);
    // Same held target over the same elapsed time: subdivision must not alter lag.
    for (uint64_t t = 10; t <= 200; t += 10) liquid::capture(a,fa,true,120,100,t);
    for (uint64_t t = 40; t <= 200; t += 40) liquid::capture(b,fb,true,120,100,t);
    for (int i = 0; i < 3; ++i) {
        CHECK(near(a.followers[i].x,b.followers[i].x));
        CHECK(near(a.followers[i].y,b.followers[i].y));
    }
    CHECK(a.followers[0].x > a.followers[1].x && a.followers[1].x > a.followers[2].x);
    geometry(fa,120,100); geometry(fb,120,100);
    const liquid::Frame before = fa;
    liquid::capture(a,fa,true,120,100,200);
    for (int k = 0; k < 4; ++k) {
        CHECK(before.kernels[k].x == fa.kernels[k].x && before.kernels[k].y == fa.kernels[k].y);
    }
    liquid::capture(a,fa,true,120,100,420);
    CHECK(fa.kernels[0].sigma != before.kernels[0].sigma);
    CHECK(fa.kernels[1].x != before.kernels[1].x); // quiet hold remains subtly organic
    for (const auto corner : {liquid::Point{0,0},liquid::Point{319,0},
                              liquid::Point{0,239},liquid::Point{319,239}}) {
        a = {};
        liquid::capture(a,fa,true,int(corner.x),int(corner.y),1000);
        liquid::capture(a,fa,true,int(corner.x),int(corner.y),1220);
        geometry(fa,int(corner.x),int(corner.y)); raster(fa);
    }
}
static void field() {
    liquid::State s; liquid::Frame f;
    uint64_t time = 10000;
    liquid::capture(s,f,true,100,110,time);
    // A single short in-memory trajectory: intro, turn, sharp local zigzags,
    // settling/organic hold, release. Never exports frames or media.
    for (int i = 0; i < 96; ++i) {
        time += 20;
        const int x = i < 32 ? 100+i*3 : i < 64 ? 193-(i-32)*3 : 100;
        const int y = i < 32 ? 110+i : i < 64 ? 141-(i-32) : 110;
        liquid::capture(s,f,true,x,y,time); geometry(f,x,y); raster(f);
    }
    for (int i = 0; i < 12; ++i) {
        time += 17; const int x = i%2 ? 123 : 100;
        liquid::capture(s,f,true,x,110,time); geometry(f,x,110); raster(f);
    }
    liquid::capture(s,f,false,0,0,++time);
    for (int i = 0; i <= 18; ++i) {
        time += 20; liquid::capture(s,f,false,0,0,time); raster(f);
    }
    CHECK(!f.active);
}
int main() {
    guard = true;
    lifecycle(); motion(); field();
    guard = false;
    CHECK(allocations == 0);
    std::printf("pass stylus-liquid 3/3: lifecycle, dt-motion, frozen/local/connected raster; "
                "%u frozen pairs; state=%zu frame=%zu bytes; guarded heap=%u; no GPU/FPS claim\n",
                raster_frames, sizeof(liquid::State), sizeof(liquid::Frame), allocations);
}
