#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "../../core/ui/draw.h"
#include "nav_ferrofluid_data.h"

// UI-thread-only, self-contained presentation. Caller supplies a published
// post-master peak (not PCM/engine access) and an ABGR theme accent before drawing.
// reset on each open/close; capture before GPU submission; draw on the TOP only.
namespace seance::platform::ui3ds::nav_ferrofluid {
inline constexpr unsigned kUpdateMs = 50, kGapMs = 250, kTurnMs = 8000;
inline constexpr unsigned kGrid = 60, kCells = kGrid*kGrid;
// One untouched cell of padding on every side; the proven <112px vertex bound
// fits this rectangle about (200,118). Even entirely unmergeable rows stay below
// 3600 quads. Actual metal row runs are much fewer (see the focused host test).
inline constexpr unsigned kMaxDrawRects = 58*58;
static_assert(kMaxDrawRects < 3600);

namespace detail {
struct Vec { float x, y, z; };
inline Vec add(Vec a, Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec sub(Vec a, Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec cross(Vec a, Vec b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline Vec unit(Vec v) {
    const float length = std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    if (length <= .000001f) return {0,0,1};
    const float scale = 1.f/length;
    return {v.x*scale,v.y*scale,v.z*scale};
}
constexpr uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return 0xff000000u | r | (g<<8) | (b<<16);
}
// The eight actual coarse-pixel study tones, not palette::ink or a flat sphere.
inline constexpr uint32_t kGray[8] = {
    rgb(14,14,19),rgb(28,27,35),rgb(45,43,54),rgb(69,67,80),
    rgb(101,98,113),rgb(141,137,155),rgb(186,184,203),rgb(234,234,244)
};
struct Frame {
    uint8_t cells[kCells]; // 0 transparent; 1..8 gray; 9..16 theme tint
    uint32_t colors[16];
    bool ready;
};
struct Scratch {
    Vec points[data::kVertexCount], normals[data::kVertexCount];
    int16_t depth[kCells]; // world Z * 32; safely within +-3584
};
struct Motion {
    uint64_t opened = 0, last_seen = 0, updated = 0;
    float level = 0, attack = 0, last_target = 0;
    uint16_t pending_peak = 0;
    bool begun = false;
};
inline Frame frame{};
inline Scratch scratch{};
inline Motion motion{};
static_assert(sizeof(Vec) == 12 && sizeof(Scratch) == 22608);
static_assert(sizeof(Frame) <= 3672 && sizeof(Motion) <= 48);

inline void colors(uint32_t spot) {
    for (unsigned i=0; i<8; ++i) {
        frame.colors[i] = kGray[i];
        uint32_t tint = 0xff000000u;
        for (unsigned shift=0; shift<24; shift+=8)
            tint |= ((((kGray[i]>>shift)&255u)*65 + ((spot>>shift)&255u)*35 + 50)/100)<<shift;
        frame.colors[8+i] = tint;
    }
}
inline uint8_t shade(Vec normal, unsigned accent_gain) {
    // Octahedral normal lookup needs no extra normalization/sqrt. 64x64 contains
    // the original diffuse, specular^48, rim^3, softbox and accent^17 recipe.
    const float sum = std::fabs(normal.x)+std::fabs(normal.y)+std::fabs(normal.z);
    const float inv = sum > .000001f ? 1.f/sum : 0.f;
    float x = normal.x*inv, y = normal.y*inv;
    if (normal.z < 0) {
        const float old_x = x;
        x = (1.f-std::fabs(y))*(x<0 ? -1.f : 1.f);
        y = (1.f-std::fabs(old_x))*(y<0 ? -1.f : 1.f);
    }
    const int col = std::clamp(int((x*.5f+.5f)*63.f+.5f),0,63);
    const int row = std::clamp(int((y*.5f+.5f)*63.f+.5f),0,63);
    const auto light = data::kLight[row*64+col];
    const unsigned reflection = (unsigned(light.violet)*accent_gain+127)/255;
    const int r = std::min(255u, unsigned(light.r)+(32*reflection+127)/255);
    const int g = std::min(255u, unsigned(light.g)+(20*reflection+127)/255);
    const int b = std::min(255u, unsigned(light.b)+(53*reflection+127)/255);
    const unsigned luminance = unsigned(54*r+183*g+19*b+128)>>8;
    const unsigned tint = r-g > 3 && b-r > 10 ? 8 : 0;
    return uint8_t(1+tint+data::kTone[luminance]);
}
inline Vec project(Vec p) { return {30.f+p.x*.25f,29.5f-p.y*.25f,p.z}; }
inline float edge(Vec a, Vec b, float x, float y) {
    return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}
inline void triangle(const data::Face& face, unsigned accent_gain) {
    // Reverse world winding because screen Y points down. Project to the 60x60
    // sample lattice, never a full-resolution framebuffer or GPU triangle mesh.
    const Vec a=project(scratch.points[face.a]);
    const Vec b=project(scratch.points[face.c]);
    const Vec c=project(scratch.points[face.b]);
    const float area=edge(a,b,c.x,c.y);
    if (area <= .00001f) return; // back-facing/degenerate, opaque closed surface
    const int x0=std::max(1,int(std::ceil(std::min(a.x,std::min(b.x,c.x))-.5f)));
    const int x1=std::min(58,int(std::floor(std::max(a.x,std::max(b.x,c.x))-.5f)));
    const int y0=std::max(1,int(std::ceil(std::min(a.y,std::min(b.y,c.y))-.5f)));
    const int y1=std::min(58,int(std::floor(std::max(a.y,std::max(b.y,c.y))-.5f)));
    if (x0>x1 || y0>y1) return;
    const uint8_t color=shade(add(add(scratch.normals[face.a],scratch.normals[face.b]),scratch.normals[face.c]),accent_gain);
    const float ax=b.y-c.y, bx=c.y-a.y, cx=a.y-b.y;
    const float ay=c.x-b.x, by=a.x-c.x, cy=b.x-a.x;
    float ea=edge(b,c,float(x0)+.5f,float(y0)+.5f);
    float eb=edge(c,a,float(x0)+.5f,float(y0)+.5f);
    float ec=edge(a,b,float(x0)+.5f,float(y0)+.5f);
    const float scale=32.f/area;
    const float zx=(ax*a.z+bx*b.z+cx*c.z)*scale;
    const float zy=(ay*a.z+by*b.z+cy*c.z)*scale;
    float z=(ea*a.z+eb*b.z+ec*c.z)*scale;
    for (int y=y0; y<=y1; ++y) {
        float wa=ea, wb=eb, wc=ec, depth=z;
        unsigned index=unsigned(y)*kGrid+unsigned(x0);
        for (int x=x0; x<=x1; ++x, ++index) {
            if (wa>=0 && wb>=0 && wc>=0) {
                const int16_t sample=int16_t(depth);
                if (sample>scratch.depth[index]) {
                    scratch.depth[index]=sample;
                    frame.cells[index]=color;
                }
            }
            wa+=ax; wb+=bx; wc+=cx; depth+=zx;
        }
        ea+=ay; eb+=by; ec+=cy; z+=zy;
    }
}
inline void render(float yaw, uint32_t spot) {
    const float tilt=-.22f+.10f*std::sin(yaw);
    const float ca=std::cos(yaw), sa=std::sin(yaw), ct=std::cos(tilt), st=std::sin(tilt);
    const float bulk=60.f+5.f*motion.level;
    const float spike=6.f+23.f*motion.level+6.f*motion.attack;
    // Loud post-limiter peaks grow needles, not a larger balloon. The raw target
    // punches immediately; the envelope carries their release. Full extension
    // is reachable below the engine's 30000/Q15 limiter ceiling (~.916).
    const float hot=std::clamp((std::max(motion.level,motion.last_target)-.60f)/.30f,0.f,1.f);
    const float needles=hot*hot*(3.f-2.f*hot);
    const float core=bulk+(48.f-bulk)*needles;
    for (unsigned i=0; i<data::kVertexCount; ++i) {
        const auto v=data::kVertices[i];
        const float profile=float(v.spike)/32768.f;
        // Non-tip vertices stop below .70; all 30 original tips reach .83.
        // Collapse the old broad shoulders while extending only those tips.
        const float taper=std::clamp((profile-.70f)/.13f,0.f,1.f);
        const float thin=profile*taper*taper*(3.f-2.f*taper);
        const float radius=core*(float(v.bulk)/16384.f)+
            (1.f-needles)*spike*profile+needles*58.f*thin;
        const float x=float(v.x)*(radius*1.045f/16384.f);
        const float y=float(v.y)*(radius*.94f/16384.f);
        const float z=float(v.z)*(radius/16384.f);
        const float rx=x*ca+z*sa, rz=-x*sa+z*ca;
        scratch.points[i]={rx,y*ct-rz*st,y*st+rz*ct};
        scratch.normals[i]={0,0,0};
    }
    // Preserve the study's area-weighted smooth vertex normals, recomputed for
    // actual deformation. Fixed 1280 faces + 642 normalizations per update.
    for (const auto& face : data::kFaces) {
        const Vec normal=cross(sub(scratch.points[face.b],scratch.points[face.a]),
                               sub(scratch.points[face.c],scratch.points[face.a]));
        for (unsigned i : {unsigned(face.a),unsigned(face.b),unsigned(face.c)})
            scratch.normals[i]=add(scratch.normals[i],normal);
    }
    for (auto& normal : scratch.normals) normal=unit(normal);
    std::memset(frame.cells,0,sizeof(frame.cells));
    std::fill_n(scratch.depth,kCells,int16_t(-32768));
    const unsigned accent_gain=unsigned((.35f+.65f*motion.level)*255.f+.5f);
    for (const auto& face : data::kFaces) triangle(face,accent_gain);
    colors(spot);
    frame.ready=true;
}
} // namespace detail

inline constexpr unsigned kMutableBytes=sizeof(detail::frame)+sizeof(detail::scratch)+sizeof(detail::motion);
inline constexpr unsigned kReadOnlyBytes=data::kStorageBytes+sizeof(detail::kGray);
static_assert(kMutableBytes <= 26*1024, "fixed CPU buffers, no GPU texture or heap");
static_assert(kReadOnlyBytes <= 31*1024, "compact geometry + lighting lookup");

inline void reset(uint64_t now_ms) {
    detail::motion={};
    detail::motion.opened=detail::motion.last_seen=detail::motion.updated=now_ms;
    detail::motion.begun=true;
    detail::frame.ready=false; // old pixels must not survive close/reopen
}
inline void capture(uint64_t now_ms, uint16_t master_peak, bool audio_valid, uint32_t spot) {
    using namespace detail;
    if (!motion.begun || now_ms<motion.last_seen || now_ms-motion.last_seen>kGapMs) reset(now_ms);
    motion.last_seen=now_ms;
    // Latch genuine short peaks arriving between 20Hz updates. This O(1) sampling
    // never mutates the published frame; an invalid source cannot leave a latch.
    if (audio_valid) motion.pending_peak=std::max(motion.pending_peak,uint16_t(std::min(unsigned(master_peak),32767u)));
    else motion.pending_peak=0;
    if (frame.ready && now_ms-motion.updated<kUpdateMs) return;
    const float dt=frame.ready ? float(now_ms-motion.updated) : float(kUpdateMs);
    const float target=float(motion.pending_peak)/32767.f;
    const float tau=target>motion.level ? 38.f : 260.f;
    motion.level=std::clamp(motion.level+(target-motion.level)*(1.f-std::exp(-dt/tau)),0.f,1.f);
    const float onset=std::max(0.f,target-motion.last_target);
    motion.attack=std::max(onset,motion.attack*std::exp(-dt/90.f));
    motion.last_target=target;
    motion.pending_peak=0;
    motion.updated=now_ms;
    // Slow constant turn is the only idle animation: no synthetic beat, random
    // flutter or breathing. The original .8s pose is the repeatable opening pose.
    constexpr float tau_circle=6.283185307179586f;
    const float yaw=tau_circle*(.1f+float((now_ms-motion.opened)%kTurnMs)/float(kTurnMs));
    render(yaw,spot);
}
inline void draw() {
    const auto& frame=detail::frame;
    if (!frame.ready) return;
    // Read only the frozen cells/colors: no time, theme, engine or audio reads.
    // Rows coalesce same-tone cells; transparent cells leave the blur untouched.
    for (unsigned y=1; y<=58; ++y) {
        unsigned x=1;
        while (x<=58) {
            const uint8_t color=frame.cells[y*kGrid+x];
            if (!color) { ++x; continue; }
            const unsigned begin=x++;
            while (x<=58 && frame.cells[y*kGrid+x]==color) ++x;
            ui::rect(float(80+begin*4),float(y*4),float((x-begin)*4),4.f,frame.colors[color-1]);
        }
    }
}
} // namespace seance::platform::ui3ds::nav_ferrofluid
