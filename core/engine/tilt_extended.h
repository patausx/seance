#pragma once
// New Tilt revision only. Old mappings in tilt_eq.h remain byte-for-byte frozen.
// Audio/control path below is integer-only. Display helpers at the bottom are
// never called by process/control_tick; no lazy LUT construction or allocations.
#include "unit.h"
#include "tilt_extended_data.h"
#include <algorithm>
#include <cmath>

namespace seance::tilt_extended {
constexpr int32_t COEFF_ONE = int32_t(1) << 28;
struct Coefficients { int32_t b0=COEFF_ONE,b1=0,a1=0; };
inline int uni(q15 v){return std::max(0,int(v));}
inline int bi(q15 v){return std::max(-32767,int(v));}
inline int32_t range_q16(q15 range){return 3*65536+int32_t(int64_t(uni(range))*9*65536/32767);}
inline int32_t amount_q16(q15 tilt,q15 range){return int32_t(int64_t(bi(tilt))*range_q16(range)/32767);}
inline uint32_t gain_q24(int32_t db_q16){
    const int32_t v=std::max<int32_t>(0,std::min<int32_t>(24*65536,db_q16+12*65536));
    const unsigned index=unsigned(v)/3072u,fraction=unsigned(v)%3072u;
    if(index>=512)return GAIN_Q24[512];
    return GAIN_Q24[index]+uint32_t(uint64_t(GAIN_Q24[index+1]-GAIN_Q24[index])*fraction/3072u);
}
inline uint32_t pivot_q28(q15 freq){
    const uint32_t v=uint32_t(uni(freq))*256u,index=v/32767u,fraction=v%32767u;
    if(index>=256)return PIVOT_Q28[256];
    return PIVOT_Q28[index]+uint32_t(uint64_t(PIVOT_Q28[index+1]-PIVOT_Q28[index])*fraction/32767u);
}
inline Coefficients coefficients(q15 tilt,q15 freq,q15 range){
    const int64_t k=pivot_q28(freq),g=int64_t(gain_q24(amount_q16(tilt,range)))<<4;
    const int64_t kg=k*g/COEFF_ONE,den=COEFF_ONE+kg;
    Coefficients c;
    c.a1=int32_t((kg-COEFF_ONE)*COEFF_ONE/den);
    c.b0=int32_t((g+k)*COEFF_ONE/den);
    c.b1=int32_t((k-g)*COEFF_ONE/den);
    // Exact neutral identity even after a parameter sweep or arbitrary old state.
    if(!tilt){c.b0=COEFF_ONE;c.b1=c.a1;}
    return c;
}
inline uint32_t drive_gain_q16(q15 drive){return 65536u+uint32_t(uni(drive))*6u;}
// Continuous odd saturation: drive=0 is EXACT wide clean, not a branch into a
// fixed knee. No AGC/compensation or oversampling; final mix can still clip.
inline int32_t colour(int32_t wet,q15 drive){
    if(drive<=0)return wet;
    const int64_t v=int64_t(wet)*drive_gain_q16(drive)/65536;
    const int64_t magnitude=v<0?-v:v;
    const int64_t den=32768+int64_t(drive)*magnitude/32767;
    return int32_t(v*32768/den);
}
// UI/test-only display helpers. Values are the actual quantized gain/pole laws.
inline double range_db(q15 v){return double(range_q16(v))/65536.;}
inline double amount_db(q15 tilt,q15 range){return double(amount_q16(tilt,range))/65536.;}
inline double nominal_hz(q15 v){return std::atan(double(pivot_q28(v))/COEFF_ONE)*32000./6.2831853071795864769*2.;}
inline double drive_db(q15 v){return 20.*std::log10(double(drive_gain_q16(v))/65536.);}
}
