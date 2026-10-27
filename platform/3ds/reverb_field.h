#pragma once
// One folded membrane, four broad measured-return influence regions. The 19
// ribs are static engraving, NOT FFT bins, voices, history or literal tank paths.
// No engine/PCM/clock, heap or transcendental calls; prepare after audio unlock.
#include "reverb_field_data.h"
#include "../../core/engine/unit.h"
#include <algorithm>
#include <type_traits>

namespace seance::platform::ui3ds::reverb_field {
struct Point { int16_t x,y; };
struct Plot {
    Point points[ROWS][COLS]{};
    uint8_t row_strength[ROWS]{};
    uint8_t wet_strength=0;
};
static_assert(sizeof(Plot)<=2560 && std::is_trivially_copyable<Plot>::value,
              "bounded immutable field, no live audio pointers");
inline unsigned root(unsigned n) {
    unsigned r=0;
    for(unsigned bit=1u<<14;bit;bit>>=2) {
        if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;
    }
    return r;
}
inline int clamp(q15 n) { return std::max(0,int(n)); }
inline unsigned strength(q15 n) {
    // Fixed square-root display, not perceived loudness. No auto-normalization:
    // zero is zero, quiet tails cannot be promoted to arbitrary full strength.
    return root(unsigned(clamp(n))*65025u/32767u);
}
inline void build(q15 size,q15 damp,q15 wet,const q15 (&returns)[4],bool modern,
                  bool quiet,Plot& out) {
    const int half=96+(36*clamp(size)+16383)/32767;
    const int darkness=clamp(damp);
    unsigned e[4];
    for(int lane=0;lane<4;++lane)e[lane]=quiet?0:strength(modern?returns[lane]:wet);
    out.wet_strength=quiet?0:uint8_t(strength(wet));
    for(int row=0;row<ROWS;++row) {
        unsigned sum=0;
        for(int col=0;col<COLS;++col) {
            const unsigned rear=(e[0]*(COLS-1-col)+e[1]*col)/(COLS-1);
            const unsigned front=(e[2]*(COLS-1-col)+e[3]*col)/(COLS-1);
            const unsigned local=(rear*(ROWS-1-row)+front*row)/(ROWS-1);
            // Keep mixed-return energy distinct from individual raw-return
            // activity: signed cancellation may make wet quiet but lines active.
            const unsigned lift=(local*3+out.wet_strength)/4;
            const auto h=HEIGHT[row][col];
            const unsigned shape=h.broad+unsigned(h.fine)*(32767-darkness)/32767;
            const int height=int((54*lift*shape+32512)/65025);
            const int x=200+(col-16)*half/16+(row-9)*44/18;
            const int y=101+(row*61+9)/18-height;
            out.points[row][col]={int16_t(x),int16_t(y)};
            sum+=lift;
        }
        out.row_strength[row]=uint8_t(sum/COLS);
    }
}
} // namespace seance::platform::ui3ds::reverb_field
