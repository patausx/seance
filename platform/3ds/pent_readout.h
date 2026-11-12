#pragma once
// Pure public PENT scalar laws; usable by cards, score, MOD and frozen screens.
#include "../../core/engine/pent_unit.h"
#include "../../core/engine/musical_note.h"
#include <cstdio>
#include <algorithm>
namespace seance::platform::ui3ds::pent_readout {
inline constexpr const char* names[]={"root","spread","bond","drift","drive","level","ratio a","ratio b","ratio c","ratio d","ratio e","depth"};
struct Ratio {uint8_t n,d;};
inline constexpr Ratio ratios[]={{1,4},{1,2},{1,1},{5,4},{3,2},{7,4},{2,1},{3,1},{4,1}};
inline void value_text(char* out,size_t n,int p,q15 v){
    if(!out||!n)return;
    if(p<0||p>=PentUnit::P_COUNT){std::snprintf(out,n,"--");return;}
    if(p==PentUnit::P_ROOT){musical_note::name(out,n,PentUnit::root_note(v));return;}
    if(p>=PentUnit::P_RATIO_A&&p<=PentUnit::P_RATIO_E){
        for(const auto& r:ratios)if(v==PentUnit::ratio_value(r.n,r.d)){
            std::snprintf(out,n,"%u/%u",unsigned(r.n),unsigned(r.d));return;}
        const uint32_t x=(uint64_t(PentUnit::ratio_q16(v))*1000+32768)/65536;
        std::snprintf(out,n,"%u.%03ux",unsigned(x/1000),unsigned(x%1000));return;
    }
    if(p==PentUnit::P_SPREAD||p==PentUnit::P_DRIFT_DEPTH){
        const unsigned x=unsigned(p==PentUnit::P_SPREAD?PentUnit::spread_centicents(v):PentUnit::drift_depth_centicents(v));
        if(x%100)std::snprintf(out,n,"%u.%02uct",x/100,x%100);
        else std::snprintf(out,n,"%uct",x/100);
        return;
    }
    if(p==PentUnit::P_DRIFT){const unsigned x=(PentUnit::drift_millihz(v)+5)/10;
        if(x%10)std::snprintf(out,n,"%u.%02uhz",x/100,x%100);
        else std::snprintf(out,n,"%u.%uhz",x/100,(x%100)/10);
        return;}
    std::snprintf(out,n,"%u%%",unsigned(PentUnit::percent(v)));
}
} // namespace seance::platform::ui3ds::pent_readout
