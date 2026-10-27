#pragma once
// Approved DLY return-iris geometry. Abstract PARAMETER glyph, not PCM, taps or
// multiple read heads. Geometry is prepared outside AudioOut::Guard, before GPU.
// No allocation, process(), clock, trig/exp, palette or GPU access in this helper.
#include <algorithm>
#include <cstdint>
#include <type_traits>
namespace seance::platform::ui3ds::delay_iris {
constexpr int LAYERS=8, SEGMENTS=64;
struct Point {int16_t x=0,y=0;};
struct Frame {
    Point points[LAYERS][SEGMENTS+1]={};
    Point base={};
    uint8_t emphasis[LAYERS]={};
    uint8_t count=0;
};
static_assert(sizeof(Frame)<=2100 && std::is_trivially_copyable<Frame>::value,"fixed iris geometry");
// round(sin(i*pi/128)*16384), i=0..64. Quarter-wave ROM + linear interpolation.
inline constexpr int16_t SIN[65]={0,402,804,1205,1606,2006,2404,2801,3196,3590,3981,4370,4756,5139,5520,5897,6270,6639,7005,7366,7723,8076,8423,8765,9102,9434,9760,10080,10394,10702,11003,11297,11585,11866,12140,12406,12665,12916,13160,13395,13623,13842,14053,14256,14449,14635,14811,14978,15137,15286,15426,15557,15679,15791,15893,15986,16069,16143,16207,16261,16305,16340,16364,16379,16384};
inline int sine(int turn) {
    const unsigned angle=unsigned(turn)&65535,quadrant=angle>>14;
    const unsigned local=(quadrant&1)?16384-(angle&16383):angle&16383;
    const unsigned i=local>>8,f=local&255;
    const int value=i==64?SIN[64]:SIN[i]+((SIN[i+1]-SIN[i])*int(f)+128)/256;
    return quadrant>=2?-value:value;
}
inline int rounded(int value,int divisor) {
    return value>=0?(value+divisor/2)/divisor:-((-value+divisor/2)/divisor);
}
inline Point point(int angle,int rx,int ry2) {
    return {int16_t(200+rounded(rx*sine(angle+16384),16384)),
            int16_t(131+rounded(ry2*sine(angle),32768))};
}
inline unsigned root(unsigned n) {
    unsigned out=0,bit=1u<<30;
    for(int i=0;i<16;++i,bit>>=2) {
        if(n>=out+bit){n-=out+bit;out=(out>>1)+bit;}else out>>=1;
    }
    return out;
}
inline void build(Frame& out,int time,int base,int feedback,int mix) {
    out={};
    time=std::max(0,std::min(32767,time));base=std::max(0,std::min(32767,base));
    feedback=std::max(0,std::min(32767,feedback));mix=std::max(0,std::min(32767,mix));
    const int angle=-10923+time*49152/32767;
    out.base=point(-10923+base*49152/32767,166,146);
    unsigned gain=unsigned(mix);
    for(int n=0;n<LAYERS && gain>=262;++n) {
        // Visual visibility floor and sqrt emphasis, NOT a calibrated gain meter.
        out.emphasis[n]=uint8_t(38+217*root(gain*32767)/32767);
        const int start=angle+n*728;
        for(int j=0;j<=SEGMENTS;++j)out.points[n][j]=point(start+int(51883u*j/SEGMENTS),166-13*n,146-9*n);
        ++out.count;gain=gain*unsigned(feedback)/32767;
    }
}
} // namespace seance::platform::ui3ds::delay_iris
