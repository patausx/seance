#pragma once
// UI ONLY. Saved coefficients and the DSP frequency map are never normalized.
#include "filter_response_model.h"
#include <algorithm>
#include <cmath>

namespace seance::platform::ui3ds::filter_interaction {
constexpr int cutoff_width=228, raw_low=129;
constexpr int raw_ceiling=filter_response_model::F_MAX, raw_max=32767;
constexpr double pi=3.141592653589793238462643383279502884;
constexpr double log_span=6.684611727667927296; // ln(16000/20)
constexpr double pixels_per_log=cutoff_width/log_span;
// Use double UI arithmetic so every legal raw coefficient round-trips exactly,
// including 4916 legacy values compressed into the one-pixel high tail.
inline double nominal(int raw) {
    return double(ENGINE_SR)/pi*std::asin(double(raw)/65536.0);
}
inline double low_coordinate() { return pixels_per_log*std::log(nominal(raw_low)/20.0); }
inline double ceiling_coordinate() { return pixels_per_log*std::log(nominal(raw_ceiling)/20.0); }
inline double cutoff_coordinate(int raw) {
    raw=std::max(0,std::min(raw_max,raw));
    if(raw<raw_low)return low_coordinate()-1.0+double(raw)/raw_low;
    if(raw>raw_ceiling)return ceiling_coordinate()+double(raw-raw_ceiling)/(raw_max-raw_ceiling);
    return pixels_per_log*std::log(nominal(raw)/20.0);
}
inline q15 cutoff_from_coordinate(double x) {
    const double low=low_coordinate(),high=ceiling_coordinate();
    double raw;
    if(x<=low-1.0)return 0;
    if(x>=high+1.0)return q15(raw_max);
    if(x<low)raw=(x-low+1.0)*raw_low;
    else if(x>high)raw=raw_ceiling+(x-high)*(raw_max-raw_ceiling);
    else {
        const double hz=20.0*std::exp(x/pixels_per_log);
        raw=65536.0*std::sin(pi*hz/double(ENGINE_SR));
    }
    return q15(std::max(0,std::min(raw_max,int(std::lround(raw)))));
}
inline q15 cutoff_drag(q15 origin,int dx) {
    // No down/retrace rounding, including raw0/1 and the old ceiling plateau.
    return dx==0?origin:cutoff_from_coordinate(cutoff_coordinate(origin)+double(dx));
}
struct Box {
    int x,y,w,h;
    constexpr bool contains(int tx,int ty) const {return tx>=x&&tx<x+w&&ty>=y&&ty<y+h;}
};
constexpr Box bypass{242,0,38,24};
constexpr Box mode(int index) { return {7+61*index,28,58,31}; }
constexpr Box cutoff{8,61,266,119},resonance{278,61,42,119};
constexpr Box drive{10,182,137,43},morph{168,182,137,43};
constexpr Box cutoff_name{8,61,48,18},resonance_name{278,61,42,14};
constexpr Box drive_name{10,182,42,18},morph_name{168,182,42,18};
constexpr int lower_x=32,lower_y=94,lower_width=228,lower_height=63;
constexpr int upper_x=32,upper_y=110,upper_width=348,upper_height=75;
} // namespace seance::platform::ui3ds::filter_interaction
