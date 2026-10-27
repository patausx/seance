#pragma once
// DISPLAY ONLY. This is the transfer function of the existing Svf2 recurrence
// with rounding and state/output saturation removed, relative to POST-DRIVE input.
// Not an audio spectrum, an ideal textbook LP/HP, or a model of the soft clipper.
#include "../../core/dsp/filter.h"
#include "../../core/engine/unit.h"
#include <algorithm>
#include <cmath>

namespace seance::platform::ui3ds::filter_response_model {
constexpr int F_MAX = 32767 * 85 / 100;
constexpr float PI = 3.14159265358979323846f;
constexpr int POINTS = 117;
constexpr int X0 = 32, X1 = 264, Y0 = 76, Y1 = 146;
constexpr float MIN_HZ = 20.0f, MAX_HZ = 16000.0f;
inline int coefficient(q15 v) { return std::max(1, std::min(F_MAX, int(v))); }
inline float nominal_hz(q15 value) {
    // Undamped SVF natural-frequency convention, NOT the measured -3dB point.
    // The existing 85% coefficient ceiling is deliberately retained.
    return float(ENGINE_SR) / PI * std::asin(float(coefficient(value)) / 65536.0f);
}
struct Complex { float r, i; };
inline Complex add(Complex a, Complex b) { return {a.r+b.r,a.i+b.i}; }
inline Complex mul(Complex a, Complex b) { return {a.r*b.r-a.i*b.i,a.r*b.i+a.i*b.r}; }
inline Complex scale(Complex a, float b) { return {a.r*b,a.i*b}; }
struct Frequency {
    Complex z, one_minus_z;
};
inline Frequency at_hz(float hz) {
    hz=std::max(0.001f,std::min(float(ENGINE_SR)/2.0f,hz));
    const float w=2.0f*PI*hz/float(ENGINE_SR);
    const float s=std::sin(w), sh=std::sin(w/2.0f);
    // 2*sin(w/2)^2 avoids cancellation of 1-cos(w) near DC.
    const float a=2.0f*sh*sh;
    return {{1.0f-a,-s},{a,s}};
}
inline float magnitude_db(q15 cutoff,q15 resonance,q15 morph,
                          trackr::dsp::FilterType type, Frequency frequency) {
    using T=trackr::dsp::FilterType;
    if(type==T::Off)return 0.0f;
    const float f=float(coefficient(cutoff))/32768.0f;
    const float d=float(std::max(1024,32767-int(resonance)))/32768.0f;
    const auto z=frequency.z, a=frequency.one_minus_z;
    const auto a2=mul(a,a);
    // HP/I=a^2/D, BP/I=f*a/D, LP/I=f^2/D; outputs use the
    // exact old/new state ordering in core/dsp/filter.h.
    const auto denominator=add(add(a2,scale(mul(z,a),f*d)),scale(z,f*f));
    Complex numerator;
    if(type==T::LPF)numerator={f*f,0.0f};
    else if(type==T::HPF)numerator=a2;
    else if(type==T::BPF)numerator=scale(a,f);
    else numerator=add(denominator,scale(a,-f)); // existing notch = input - BP
    const float m=float(std::max(0,int(morph)))/32768.0f;
    numerator=add(scale(denominator,m),scale(numerator,1.0f-2.0f*m));
    const float norm=denominator.r*denominator.r+denominator.i*denominator.i;
    const float power=(numerator.r*numerator.r+numerator.i*numerator.i)/std::max(1.0e-30f,norm);
    return 10.0f*std::log10(std::max(1.0e-12f,power));
}
inline float magnitude_db(q15 cutoff,q15 resonance,q15 morph,
                          trackr::dsp::FilterType type,float hz) {
    return magnitude_db(cutoff,resonance,morph,type,at_hz(hz));
}
inline int frequency_x(float hz) {
    hz=std::max(MIN_HZ,std::min(MAX_HZ,hz));
    const float t=std::log(hz/MIN_HZ)/std::log(MAX_HZ/MIN_HZ);
    return X0+int(t*float(X1-X0)+0.5f);
}
inline int response_y(float db) {
    db=std::max(-36.0f,std::min(12.0f,db));
    return Y0+int((12.0f-db)*float(Y1-Y0)/48.0f+0.5f);
}
} // namespace seance::platform::ui3ds::filter_response_model
