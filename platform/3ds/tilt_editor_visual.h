#pragma once
// Bounded pre-GPU small-signal response of the actual production coefficients.
// Nonlinear saturation/fixed-point state rounding are deliberately not modelled.
#include "tilt_readout.h"
#include <algorithm>
#include <cmath>

namespace seance::platform::ui3ds::tilt_editor_visual {
using C=TiltUnit;
constexpr int X0=38,X1=386,Y0=74,Y1=178,POINTS=(X1-X0)/2+1;
constexpr double PI=3.14159265358979323846;
struct Plot {uint8_t y[POINTS]={};uint16_t pivot_x=0;uint8_t pivot_y=0;bool over=false,under=false;char low[18]={},high[18]={},pivot[18]={};};
inline int frequency_x(double hz){return X0+int(std::log(std::max(20.0,std::min(16000.0,hz))/20)/std::log(800.0)*(X1-X0)+.5);}
inline int response_y(double db){return Y0+int((12-std::max(-12.0,std::min(12.0,db)))*(Y1-Y0)/24+.5);}
struct Frequency {double real,imag;tilt_response_model::Frequency old;};
struct Transfer {
    bool extended=false;
    double b0=1,b1=0,a1=0,gain=1,mix=1;
    tilt_response_model::Transfer old={};
};
inline Frequency at_hz(double hz){
    const double w=2*PI*hz/32000;
    return {std::cos(w),-std::sin(w),tilt_response_model::at_hz(float(hz))};
}
inline Transfer transfer(const q15* values,C::Revision revision){
    Transfer t;t.extended=revision==C::Revision::Extended;
    if(!t.extended){t.old=tilt_response_model::transfer(values,revision!=C::Revision::Legacy);return t;}
    // One production coefficient/gain calculation per changed graph, not per bin.
    const auto c=tilt_extended::coefficients(values[C::P_TILT],values[C::P_FREQ],values[C::P_RANGE]);
    t.b0=double(c.b0)/tilt_extended::COEFF_ONE;t.b1=double(c.b1)/tilt_extended::COEFF_ONE;t.a1=double(c.a1)/tilt_extended::COEFF_ONE;
    t.gain=double(tilt_extended::drive_gain_q16(values[C::P_DRIVE]))/65536*double(tilt_eq::trim_gain_q16(values[C::P_TRIM]))/65536;
    t.mix=std::max(0,int(values[C::P_MIX]))/32767.0;
    return t;
}
inline double magnitude_db(const Transfer& t,Frequency f){
    if(!t.extended)return tilt_response_model::magnitude_db(t.old,f.old);
    const double nr=t.b0+t.b1*f.real,ni=t.b1*f.imag;
    const double dr=1+t.a1*f.real,di=t.a1*f.imag,den=dr*dr+di*di;
    const double r=1-t.mix+t.mix*t.gain*(nr*dr+ni*di)/den,i=t.mix*t.gain*(ni*dr-nr*di)/den;
    const double result=10*std::log10(std::max(1e-24,r*r+i*i));
    return std::abs(result)<1e-9?0:result;
}
inline double magnitude_db(const q15* values,C::Revision revision,double hz){return magnitude_db(transfer(values,revision),at_hz(hz));}
inline void calculate(const q15* values,C::Revision revision,bool bypass,Plot& out){
    // Initialized only on first pre-GPU capture, outside the audio guard. Later
    // graph changes reuse all fixed logarithmic frequency sin/cos pairs.
    static const auto frequencies=[] {struct Table{Frequency at[POINTS];};Table result;
        for(int i=0;i<POINTS;++i)result.at[i]=at_hz(20*std::pow(800.0,double(i)/(POINTS-1)));
        return result;}();
    out={};const auto t=transfer(values,revision);
    for(int i=0;i<POINTS;++i){const double db=bypass?0:magnitude_db(t,frequencies.at[i]);
        out.y[i]=uint8_t(response_y(db));out.over|=db>12.00001;out.under|=db< -12.00001;}
    const double pivot=revision==C::Revision::Extended?tilt_extended::nominal_hz(values[C::P_FREQ]):
        tilt_response_model::nominal_hz(values[C::P_FREQ],revision==C::Revision::Legacy&&values[C::P_RANGE]==0);
    out.pivot_x=uint16_t(frequency_x(pivot));out.pivot_y=uint8_t(response_y(bypass?0:magnitude_db(t,at_hz(pivot))));
    tilt_readout::hz(out.pivot,sizeof out.pivot,pivot);
    tilt_readout::db(out.low,sizeof out.low,bypass?0:magnitude_db(t,at_hz(0)));
    tilt_readout::db(out.high,sizeof out.high,bypass?0:magnitude_db(t,frequencies.at[POINTS-1]));
}
}
