#pragma once
// Generated mechanical RULE block from approved mock-001; tests/rule_ui_transfer.py.
// PRODUCTION seams: owned DTO strings, shared title/captions, native triangles,
// true signed ROOT and edit/live flags; pending lower state is NOT actual top state.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <citro2d.h>
#include "editor_style.h"
#include "module_heading.h"
#include "module_fader.h"
#include "module_fader_painter.h"
#include "ui_palette.h"
#include "../../core/ui/draw.h"
namespace seance::platform::ui3ds::rule_visual {
namespace m=editor_style;
using seance::ui::rect; using seance::ui::line;
using seance::ui::text; using seance::ui::text_w;
inline void tri(float x0,float y0,float x1,float y1,float x2,float y2,uint32_t c){
    C2D_DrawTriangle(x0,y0,c,x1,y1,c,x2,y2,c,0.f);
}
inline uint32_t mix(uint32_t a, uint32_t b, int t) {           // t 0..255, ABGR, opaque
    t = std::clamp(t, 0, 255);
    uint32_t out = 0xff000000u;
    for (int s = 0; s < 24; s += 8)
        out |= uint32_t((int((a >> s) & 255) * (255 - t) + int((b >> s) & 255) * t + 127) / 255) << s;
    return out;
}
inline void right_text(int right, int y, const char* s, uint32_t c) { text(right - text_w(s), y, s, c); }
inline int ease_q8(int t) { t = std::clamp(t, 0, 256); return t * t * (768 - 2 * t) / 65536; } // smoothstep

struct RuleFrame {
    static constexpr int kCells = 15, kRings = 11, kKnobs = 10;
    bool valid = false;
    char source[40] = {};
    char scale[24] = {};
    int rule = 30, gen = 0, sounding = 0;
    int period_ms = 320;
    uint16_t rows[kRings] = {};        // bit c = cell alive; [0] newest generation
    uint16_t born[kRings] = {};        // bit c = this cell's birth was played as a note
    int rings = 0;                     // valid rows
    uint8_t spoke[kCells] = {};        // sounding notes: rings since birth (0 = silent)
    int push_q8 = 256;                 // 0..256, 100 ms after a generation step
    int phase_q8 = 0;                  // progress toward the next generation (real clock)
    // lower screen
    struct Knob { char value[16]={}; int edit_q8=0,live_q8=0; bool linked=false,disabled=false,quiet=false; };
    Knob knobs[kKnobs] = {};
    bool lens=false;
    int edit_rule=90; uint16_t edit_row=128; bool pending=false,seed=false;
    char status[32]={};
    int selected = 0;
};

inline constexpr const char* names[]={"rate","density","root","range","attack","release","mutate","spread","voices","level"};
template<class Pen> inline void knob_grid(const Pen& pen,const RuleFrame& f){
    for(int i=0;i<10;++i){const auto& k=f.knobs[i];
        const int x0=(i&1)?174:14,x1=(i&1)?305:143,y=102+(i/2)*24;
        const int r0=x0+4,r1=x1-2;
        if(!f.lens){pen.text(x0,y,names[i],i==f.selected?palette::spot():palette::ink());pen.right(x1,y,k.value,palette::ghost());}
        module_fader::horizontal(pen,r0,r1,y+14,r0+(r1-r0)*k.edit_q8/256,r0+(r1-r0)*k.live_q8/256,
            {k.linked,k.disabled,k.quiet},i==2?(r0+r1)/2:-1);
    }
}

namespace rule_detail {
constexpr float kCx = 200.f, kCy = 132.f, kInner = 16.f, kOuter = 92.f, kGap = 1.5f;
inline float ring_edge(float k) {               // radius of the inner edge of (fractional) ring k
    float total = 0.f;                           // ring weights grow outward: older = wider
    for (int i = 0; i < RuleFrame::kRings; ++i) total += 1.f + 0.16f * float(i);
    const float scale = (kOuter - kInner - kGap * RuleFrame::kRings) / total;
    float r = kInner;
    int i = 0;
    for (; float(i + 1) <= k && i < RuleFrame::kRings; ++i) r += (1.f + 0.16f * float(i)) * scale + kGap;
    if (i < RuleFrame::kRings) r += (k - float(i)) * ((1.f + 0.16f * float(i)) * scale + kGap);
    return r;
}
constexpr float kPi = 3.14159265f;
inline float angle(int cell) { return (-90.f + float(cell - 7) * 24.f) * kPi / 180.f; }
struct Geometry {
    float sector_cos[15][4]{},sector_sin[15][4]{},cosine[15]{},sine[15]{},dot_cos[24]{},dot_sin[24]{};
    Geometry(){
        for(int cell=0;cell<15;++cell){const float a=angle(cell);cosine[cell]=__builtin_cosf(a);sine[cell]=__builtin_sinf(a);
            const float a0=a-10.5f*kPi/180.f,span=21.f*kPi/180.f;
            for(int s=0;s<4;++s){const float u=a0+span*float(s)/3;sector_cos[cell][s]=__builtin_cosf(u);sector_sin[cell][s]=__builtin_sinf(u);}}
        for(int i=0;i<24;++i){const float a=-kPi/2.f+2.f*kPi*float(i)/24;dot_cos[i]=__builtin_cosf(a);dot_sin[i]=__builtin_sinf(a);}
    }
};
inline const Geometry geometry; // fixed read-only preparation, never per-frame DSP
inline void sector(float r0, float r1, int cell, uint32_t c) {
    constexpr int kSteps = 3;
    for (int s = 0; s < kSteps; ++s) {
        const float c0=geometry.sector_cos[cell][s],s0=geometry.sector_sin[cell][s],c1=geometry.sector_cos[cell][s+1],s1=geometry.sector_sin[cell][s+1];
        tri(kCx + c0 * r0, kCy + s0 * r0, kCx + c1 * r0, kCy + s1 * r0, kCx + c1 * r1, kCy + s1 * r1, c); // PRODUCTION: C2D_DrawTriangle
        tri(kCx + c0 * r0, kCy + s0 * r0, kCx + c1 * r1, kCy + s1 * r1, kCx + c0 * r1, kCy + s0 * r1, c);
    }
}
} // namespace rule_detail

inline void rule_draw_top(const RuleFrame& f) {
    using namespace rule_detail;
    m::paper(400);
    if (!f.valid) return;
    module_heading::header(UnitKind::Rule, m::spot()); // PRODUCTION: shared private title
    char buf[40];
    std::snprintf(buf, sizeof buf, "gen %03d", f.gen);
    m::header_text(76, 386 - text_w(buf) - 8, f.source, palette::ink());
    right_text(m::right_edge(400), m::kHeaderTextY, buf, palette::ink());
    std::snprintf(buf, sizeof buf, "rule %d", f.rule);
    text(14, 38, buf, palette::spot());
    std::snprintf(buf, sizeof buf, f.sounding == 1 ? "%d note" : "%d notes", f.sounding);
    right_text(386, 38, buf, palette::ink());

    const float push = float(ease_q8(f.push_q8)) / 256.f;   // rings move outward one step per generation
    const uint32_t paper = palette::bg();
    for (int k = f.rings - 1; k >= 0; --k) {
        const float kf = float(k) - 1.f + push;
        if (kf < -0.999f) continue;
        const float r0 = std::max(kInner, ring_edge(std::max(0.f, kf)));
        const float r1 = std::max(r0, ring_edge(std::max(0.f, kf) + 1.f) - kGap) - (kf < 0.f ? (ring_edge(1.f) - kInner) * -kf : 0.f);
        if (r1 <= r0) continue;
        const int age = std::min(120, k * 10);
        for (int c = 0; c < RuleFrame::kCells; ++c) {
            if (!(f.rows[k] >> c & 1)) continue;
            const bool played = f.born[k] >> c & 1;
            sector(r0, r1, c, mix(played ? palette::spot() : palette::ink(), paper, age));
        }
    }
    seance::ui::circle(kCx, kCy, kInner - 1.5f, palette::subdued(), false);   // window rims (structure, not data)
    seance::ui::circle(kCx, kCy, kOuter + 0.5f, palette::subdued(), false);
    for (int c = 0; c < RuleFrame::kCells; ++c) {           // spokes = sounding notes, length = held rings
        if (!f.spoke[c]) continue;
        const float r0 = kInner + 1.f;
        const float r1 = ring_edge(std::min(float(f.spoke[c]) - 1.f + push, float(RuleFrame::kRings))) + 1.f;
        if (r1 <= r0 + 1.f) continue;
        const float ca=geometry.cosine[c],sa=geometry.sine[c];
        line(int(kCx + ca * r0 + .5f), int(kCy + sa * r0 + .5f), int(kCx + ca * r1 + .5f), int(kCy + sa * r1 + .5f), palette::playhead());
    }
    for (int c = 0; c < RuleFrame::kCells; ++c) {            // scale ring: one tick per cell, roots longer
        const float ca=geometry.cosine[c],sa=geometry.sine[c];
        const float r1 = kOuter + (c % 5 == 0 ? 7.f : 4.f);
        line(int(kCx + ca * (kOuter + 2.f) + .5f), int(kCy + sa * (kOuter + 2.f) + .5f), int(kCx + ca * r1 + .5f), int(kCy + sa * r1 + .5f),
             c % 5 == 0 ? palette::ghost() : palette::subdued());
    }
    constexpr int kDots = 24;                                // centre: real clock to the next generation
    const int lit = f.phase_q8 * kDots / 256;
    for (int i = 0; i < kDots; ++i) {
        rect(kCx + geometry.dot_cos[i] * 9.f - .5f, kCy + geometry.dot_sin[i] * 9.f - .5f, 1.f, 1.f,
             i <= lit ? palette::spot() : palette::subdued());
    }
    char tail[24]; std::snprintf(tail, sizeof tail, "%d.%02d s / gen", f.period_ms / 1000, (f.period_ms % 1000) / 10);
    m::footer(400, f.status[0]?f.status:f.scale, tail);
}

template<class Pen> inline void bottom_content(const Pen& pen,const RuleFrame& f) {
    if (!f.valid) return;
    pen.text(76,m::kHeaderTextY,f.source,palette::ink());
    pen.right(305, m::kHeaderTextY, "setup", palette::ink());

    // Wolfram rule table: eight neighbourhoods 111..000, tap toggles the output bit (= rule number).
    char buf[8]; std::snprintf(buf, sizeof buf, "%03d", f.edit_rule);
    pen.text(14, 38, "rule", palette::ink());
    pen.right(305, 38, buf, palette::spot());
    for (int i = 0; i < 8; ++i) {
        const int pattern = 7 - i, x = 14 + i * 292 / 8 + (292 / 8 - 23) / 2, y = 50;
        for (int b = 0; b < 3; ++b) {
            const bool on = pattern >> (2 - b) & 1;
            const int bx = x + b * 8;
            if (on) pen.rect(float(bx), float(y), 7.f, 7.f, palette::ink());
            else pen.frame(float(bx), float(y), 7.f, 7.f, palette::subdued());
        }
        const bool out = f.edit_rule >> pattern & 1;
        if (out) pen.rect(float(x + 8), float(y + 10), 7.f, 7.f, palette::spot());
        else pen.frame(float(x + 8), float(y + 10), 7.f, 7.f, palette::subdued());
    }
    // current generation, tap = seed / toggle a cell
    for (int c = 0; c < RuleFrame::kCells; ++c) {
        const int x = 18 + c * 19, y = 75;
        const bool on = f.edit_row >> c & 1, played = !f.pending && (f.born[0] >> c & 1);
        if (on) pen.rect(float(x), float(y), 17.f, 9.f, played ? palette::spot() : palette::ink());
        else pen.frame(float(x), float(y), 17.f, 9.f, palette::subdued());
    }
    knob_grid(pen,f);
    pen.text(m::kLeft,m::kTextY,f.pending?"scale > / row pending":f.seed?"scale > / seed":"scale > / l mod",m::ghost());pen.right(m::right_edge(320),m::kTextY,"b back",m::ghost());
}
} // namespace seance::platform::ui3ds::rule_visual
