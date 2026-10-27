#pragma once
// Approved centered-paper study, reduced to one bounded scalar grid. No engine,
// heap, random state, clock reads or GPU access. RGB interpolation deliberately
// stays in the study's sRGB byte space (no extra gamma/polarity discontinuity).
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::patch_paper {
constexpr int Size = 64, Width = 400, Height = 496;
constexpr unsigned PeriodMs = 18000, UpdateMs = 100;
constexpr int FallbackStep = 20; // at most 240/192 quads, never 4096 texel quads
struct Clock {
    uint64_t last = 0;
    unsigned phase = 0;
    bool enabled = false, frozen = false;
    void capture(bool active, bool freeze, uint64_t now) {
        // Opening a modal retains the last visible field, not a not-yet-drawn
        // extrapolation. Inactive visits and modal time never accumulate debt.
        if (enabled && active && !frozen && !freeze && now >= last)
            phase = unsigned((phase + (now - last) % PeriodMs) % PeriodMs);
        enabled = active; frozen = freeze; last = now;
    }
    unsigned bucket() const { return phase / UpdateMs; }
};
inline float lattice(int x, int y) {
    uint32_t n = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + 419u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return float((n ^ (n >> 16)) & 65535u) / 32767.5f - 1.f;
}
inline float noise(float x, float y) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    float u = x - ix, v = y - iy;
    u = u*u*(3-2*u); v = v*v*(3-2*v);
    const float a = lattice(ix,iy)*(1-u) + lattice(ix+1,iy)*u;
    const float b = lattice(ix,iy+1)*(1-u) + lattice(ix+1,iy+1)*u;
    return a*(1-v) + b*v;
}
inline void paint(uint8_t (&light)[Size*Size], unsigned ms) {
    const float angle = float(ms % PeriodMs) * (6.28318530718f / PeriodMs);
    const float s = std::sin(angle), c = std::cos(angle);
    float xwarp[Size], ywarp[Size], rowwarp[Size];
    // Only 194 trig calls per changed field, rather than three for every texel.
    // The source samples every four pixels then resizes with centered filtering:
    // its continuous source coordinate is therefore screen coordinate minus 2.
    for (int i=0;i<Size;++i) {
        const float x = (i+.5f)*Width/Size-2, y = (i+.5f)*Height/Size-2;
        xwarp[i] = x + 14*std::sin(x/71-.24f*s) + 18*s;
        ywarp[i] = 27*std::sin(x/91+.30f*s) + 18*(c-1);
        rowwarp[i] = 31*std::sin(y/83+.32f*s);
    }
    for (int y=0;y<Size;++y) for (int x=0;x<Size;++x) {
        const float qx = xwarp[x] + rowwarp[y];
        const float qy = (y+.5f)*Height/Size-2 + ywarp[x];
        const float n = .65f*noise(qx/115+2,qy/115+7)
                      + .25f*noise(qx/53+9+.12f*s,qy/53+3+.10f*(c-1))
                      + .10f*noise(qx/24+5-.10f*s,qy/24+11);
        const float value = .5f + 1.45f*n;
        light[y*Size+x] = value<=0 ? 0 : value>=1 ? 255 : uint8_t(value*255+.5f);
    }
}
inline void colors(uint32_t (&lut)[256], uint32_t dark, uint32_t pale) {
    for (unsigned i=0;i<256;++i) {
        uint32_t out = 0xff000000u;
        for (unsigned shift=0;shift<24;shift+=8)
            out |= ((((dark>>shift)&255)*(255-i)+((pale>>shift)&255)*i+127)/255)<<shift;
        lut[i] = out;
    }
}
inline uint32_t sample(const uint8_t (&light)[Size*Size], const uint32_t (&lut)[256], float x, float y) {
    // Matches GPU_LINEAR/clamp sampling, including the half-texel convention.
    float tx=x*Size/Width-.5f, ty=y*Size/Height-.5f;
    tx=tx<0?0:tx>Size-1?Size-1:tx; ty=ty<0?0:ty>Size-1?Size-1:ty;
    const int ix=int(tx), iy=int(ty), ex=ix<Size-1?ix+1:ix, ey=iy<Size-1?iy+1:iy;
    const float fx=tx-ix, fy=ty-iy;
    const uint32_t a=lut[light[iy*Size+ix]], b=lut[light[iy*Size+ex]];
    const uint32_t c=lut[light[ey*Size+ix]], d=lut[light[ey*Size+ex]];
    uint32_t out=0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8) {
        const float upper=((a>>shift)&255)*(1-fx)+((b>>shift)&255)*fx;
        const float lower=((c>>shift)&255)*(1-fx)+((d>>shift)&255)*fx;
        out|=uint32_t(upper*(1-fy)+lower*fy+.5f)<<shift;
    }
    return out;
}
static_assert(Size*Size+256*sizeof(uint32_t)==5120,"patch-paper persistent pixel budget");
} // namespace seance::platform::ui3ds::patch_paper
