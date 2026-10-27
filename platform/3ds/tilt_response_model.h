#pragma once
// Display only: exact coefficient/gain laws, with fixed-point rounding and
// saturation removed. Phase-aware dry/wet sum, not a spectrum or live PCM.
#include "../../core/engine/tilt_eq.h"
#include "../../core/engine/units.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace seance::platform::ui3ds::tilt_response_model {
using C=TiltUnit;
constexpr int PARAMS=6, X0=34, X1=306, Y0=93, Y1=151, POINTS=137;
constexpr float PI=3.14159265358979323846f;
inline int clamp(int v,int lo=0) {return std::max(lo,std::min(32767,v));}
inline float nominal_hz(q15 v,bool legacy_zero=false) {
    // Legacy RANGE0 uses a linear coefficient, not the newer 125..8k pivot.
    if(legacy_zero)return -32000.0f*std::log(1.0f-float(tilt_eq::coeff(v,true))/32768.0f)/(2.0f*PI);
    return 125.0f*std::exp2(float(clamp(v))*6.0f/32768.0f);
}
inline void value_text(char* out,size_t size,int p,q15 value,bool modern=true,bool legacy_zero=false) {
    if(!out || !size)return;
    const int v=clamp(value);
    if(p==C::P_TILT) {
        const int percent=int(std::lround(clamp(value,-32767)*100.0/32767.0));
        std::snprintf(out,size,percent?"%+d%%":"%d%%",percent);
    } else if(p==C::P_FREQ)std::snprintf(out,size,"%uhz",unsigned(nominal_hz(value,legacy_zero)+0.5f));
    else if(p==C::P_RANGE) {
        if(!modern && !v)std::snprintf(out,size,"legacy");
        else {char number[16];std::snprintf(number,sizeof number,"%.3f",tilt_eq::max_step(value)*0.375);
            int n=0;while(number[n])++n;while(n && number[n-1]=='0')number[--n]=0;if(n && number[n-1]=='.')number[--n]=0;
            std::snprintf(out,size,"%sdb",number);}
    } else if(p==C::P_DRIVE)std::snprintf(out,size,"%.2fx",legacy_zero?(32767.0+v*3.0)/32768.0:1.0+v*6.0/65536.0);
    else if(p==C::P_TRIM) {
        if(!modern)std::snprintf(out,size,"--");
        else {const int tenth=int(std::lround(tilt_eq::trim_db(value)*10.0));std::snprintf(out,size,"%+.1fdb",tenth/10.0);}
    } else if(p==C::P_MIX)std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
    else std::snprintf(out,size,"--");
}
struct Frequency {float real,imag;};
inline Frequency at_hz(float hz) {const float w=2*PI*hz/32000.0f;return {std::cos(w),-std::sin(w)};}
struct Transfer {float a,low,high,gain,mix;};
inline Transfer transfer(const q15* v,bool modern) {
    const bool legacy_zero=!modern && v[C::P_RANGE]==0;
    Transfer t={};t.a=float(tilt_eq::coeff(v[C::P_FREQ],legacy_zero))/32768.0f;
    if(legacy_zero) {
        const float tilt=float(v[C::P_TILT])/32768.0f;t.low=1-tilt;t.high=1+tilt;
        t.gain=(32767.0f+float(v[C::P_DRIVE])*3.0f)/32768.0f;
    } else {
        uint32_t low,high;tilt_eq::shelf_gains(v[C::P_TILT],v[C::P_RANGE],modern,low,high);
        t.low=float(low)/65536.0f;t.high=float(high)/65536.0f;
        if(v[C::P_TILT]==0 && v[C::P_DRIVE]==0)t.low=t.high=1.0f; // exact neutral DSP path
        t.gain=1.0f+float(v[C::P_DRIVE])*6.0f/65536.0f;
    }
    if(modern)t.gain*=float(tilt_eq::trim_gain_q16(v[C::P_TRIM]))/65536.0f;
    t.mix=float(v[C::P_MIX])/32768.0f;
    return t;
}
inline float magnitude_db(const Transfer& t,Frequency f) {
    const float dr=1-(1-t.a)*f.real,di=-(1-t.a)*f.imag,den=dr*dr+di*di;
    const float lr=t.a*dr/den,li=-t.a*di/den;
    const float wr=(t.high+(t.low-t.high)*lr)*t.gain,wi=(t.low-t.high)*li*t.gain;
    const float r=1-t.mix+t.mix*wr,i=t.mix*wi;
    return 10.0f*std::log10(std::max(1.0e-24f,r*r+i*i));
}
inline float magnitude_db(const q15* v,bool modern,float hz) {return magnitude_db(transfer(v,modern),at_hz(hz));}
inline int frequency_x(float hz) {return X0+int(std::log(std::max(20.0f,std::min(16000.0f,hz))/20.0f)/std::log(800.0f)*(X1-X0)+0.5f);}
inline int response_y(float db) {return Y0+int((12.0f-std::max(-12.0f,std::min(12.0f,db)))*(Y1-Y0)/24.0f+0.5f);}
struct Plot {uint8_t y[POINTS]={};uint16_t pivot_x=0;uint8_t pivot_y=0;bool over=false,under=false;};
inline void plot(const q15* v,bool modern,bool bypass,Plot& out) {
    // Reused frequency table; cache and all model computation run outside guard.
    static const auto frequencies=[] {struct Table {Frequency at[POINTS];};Table t;
        for(int n=0;n<POINTS;++n)t.at[n]=at_hz(20.0f*std::pow(800.0f,float(n)/(POINTS-1)));
        return t;}();
    out={};const auto t=transfer(v,modern);
    for(int n=0;n<POINTS;++n) {const float db=bypass?0.0f:magnitude_db(t,frequencies.at[n]);
        out.y[n]=response_y(db);out.over|=db>12;out.under|=db< -12;}
    const float hz=nominal_hz(v[C::P_FREQ],!modern && v[C::P_RANGE]==0);
    out.pivot_x=frequency_x(hz);out.pivot_y=response_y(bypass?0.0f:magnitude_db(t,at_hz(hz)));
}
}
