// Generic math/filter laws only: no future masks/audio/RNG schedules.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
namespace seance { namespace rot_dsp {
struct Tables {
    int16_t sine[1025];int32_t precise_sine[4096];
    // Every integer distance/width pair, widths1..39, through8 sigma. No trajectory.
    uint32_t gaussian[6279];uint16_t clip[1025];uint8_t db[32769];
    Tables() {
        for(int i=0;i<=1024;++i){sine[i]=int16_t(std::sin(i*6.283185307179586/1024)*32767);clip[i]=uint16_t(std::tanh(double(i)/1024*1.15)/1.15*32768+.5);}
        for(int i=0;i<4096;++i)precise_sine[i]=int32_t(std::llround(std::sin(i*6.283185307179586/4096)*1073741824));
        for(int w=1;w<=39;++w)for(int d=0;d<=8*w;++d)gaussian[(w-1)*(4*w+1)+d]=uint32_t(std::exp(-.5*double(d)*d/(w*w))*1073741824+.5);
        db[0]=0;for(int i=1;i<=32768;++i)db[i]=uint8_t(std::max(0.,std::min(255.,(20*std::log10(double(i)/32768)+42)*255/42))+.5);
    }
};
// Function-local static: every call pays the thread-safe init guard. Hot loops
// take `const Tables& t=tables();` once and call the table-taking overloads
// below; the arithmetic and the table values are the same object either way.
inline const Tables& tables(){static const Tables t;return t;}
// Equal to the modulo below for EVERY input: a compare path returns only when the
// result is position, position-span or position+span; span<=0 and any position
// farther than one span outside [0,span) run the original modulo.
inline uint64_t wrap_position(int64_t position,int64_t span){if(span>0){if(position>=0){if(position<span)return uint64_t(position);if(position-span<span)return uint64_t(position-span);}else if(position>-span)return uint64_t(position+span);}const int64_t r=position%span;return uint64_t(r<0?r+span:r);}
// Exact position%span without the 64-bit modulo on the common path.
// Equal to x%span for EVERY x and span>0: x<span -> x; span<=x<2span -> x-span;
// x>=2span -> (x-span)%span == x%span. Reader positions are <span, or <span plus
// one pitched step (<=4x, i.e. <3span only for loops shorter than 4 frames).
inline uint64_t reduce(uint64_t x,uint64_t span){if(x>=span){x-=span;if(x>=span)x%=span;}return x;}
// 32-bit x%n by the same law, exact for every x and n>0.
inline uint32_t reduce32(uint32_t x,uint32_t n){if(x>=n){x-=n;if(x>=n)x%=n;}return x;}
inline int32_t sine(const Tables& t,uint32_t phase){const uint32_t p=phase>>12,i=p>>10,f=p&1023;return t.sine[i]+(int32_t(t.sine[i+1])-t.sine[i])*int32_t(f)/1024;}
inline int32_t sine(uint32_t phase){return sine(tables(),phase);}
// Result in [0,32767]: sine() interpolates between two int16(sin*32767) entries.
inline int32_t hann(const Tables& t,uint64_t pos,uint64_t span){return (32767-sine(t,uint32_t(pos*4294967296ull/span)+0x40000000u))/2;}
inline int32_t hann(uint64_t pos,uint64_t span){return hann(tables(),pos,span);}
// hann(t,pos,span)==hann_phase(t,uint32_t(pos*4294967296ull/span)) for every pos/span.
inline int32_t hann_phase(const Tables& t,uint32_t phase){return (32767-sine(t,phase+0x40000000u))/2;}
inline int32_t clip(const Tables& t,int32_t x){const uint32_t a=uint32_t(x<0?-x:x),i=std::min<uint32_t>(1024,a>>5),f=a&31;const int32_t y=i==1024?t.clip[i]:t.clip[i]+(int32_t(t.clip[i+1])-t.clip[i])*int32_t(f)/32;return x<0?-y:y;}
inline int32_t clip(int32_t x){return clip(tables(),x);}
inline uint32_t gaussian(const Tables& t,uint32_t distance,uint32_t width){width=std::max<uint32_t>(1,std::min<uint32_t>(39,width));return distance>8*width?0:t.gaussian[(width-1)*(4*width+1)+distance];}
inline uint32_t gaussian(uint32_t distance,uint32_t width){return gaussian(tables(),distance,width);}
// Periodic cubic interpolation of a generic Q30 sine; bounded integer products.
inline int32_t sine30(uint32_t phase){
    const auto& t=tables();const uint32_t i=phase>>20;const int64_t f=(phase&1048575u)*1024ll;
    const int64_t a=t.precise_sine[(i+4095)&4095],b=t.precise_sine[i],c=t.precise_sine[(i+1)&4095],d=t.precise_sine[(i+2)&4095];
    const int64_t linear=c-a,quadratic=2*a-5*b+4*c-d,cubic=3*(b-c)+d-a;
    const int64_t result=b+(f*(linear+f*(quadratic+f*cubic/1073741824)/1073741824)/1073741824)/2;
    return int32_t(std::max<int64_t>(-1073741824,std::min<int64_t>(1073741824,result)));
}
// Generic bilinear/prewarped Butterworth2. Forward/backward application on an
// already-published private tape gives |B|^2=1/(1+(tan(pi*f/SR)/tan(pi*fc/SR))^4).
// No trigonometry at a pass edge: tan's seventh-order polynomial on .049..138.
struct SmearCoefficients { int32_t b0=0,b1=0,b2=0,a1=0,a2=0; };
inline SmearCoefficients smear_coefficients(uint32_t fc) {
    fc=std::max<uint32_t>(500,std::min<uint32_t>(1400,fc));
    const int64_t Q=1073741824,x=int64_t(fc)*3373259426ull/32000;
    const int64_t x2=x*x/Q,x3=x2*x/Q,x5=x3*x2/Q,x7=x5*x2/Q;
    const int64_t t=x+x3/3+2*x5/15+17*x7/315,t2=t*t/Q,root2t=t*1518500250/Q;
    const int64_t d=Q+root2t+t2;
    SmearCoefficients c;c.a1=int32_t(2*(t2-Q)*Q/d);c.a2=int32_t((Q-root2t+t2)*Q/d);
    // Exact unity DC after coefficient quantization, symmetric numerator.
    const int64_t sum=Q+c.a1+c.a2;c.b0=c.b2=int32_t((sum+2)/4);c.b1=int32_t(sum-2*c.b0);
    return c;
}
struct SmearState {
    // Q12 sub-LSB history. Internal rail +/-4 full scale bounds every widened
    // product and their five-term sum; it is not an output makeup/clipping law.
    int32_t x1=0,x2=0,y1=0,y2=0;
    void reset(){x1=x2=y1=y2=0;}
    int32_t tick(int32_t sample,const SmearCoefficients& c) {
        const int64_t x=int64_t(sample)*4096;
        const int64_t acc=x*c.b0+int64_t(x1)*c.b1+int64_t(x2)*c.b2-int64_t(y1)*c.a1-int64_t(y2)*c.a2;
        const int32_t y=int32_t(std::max<int64_t>(-536870912,std::min<int64_t>(536870912,acc/1073741824)));
        x2=x1;x1=int32_t(x);y2=y1;y1=y;return y/4096;
    }
};
inline uint8_t envelope(const Tables& t,uint32_t magnitude,uint32_t peak){return t.db[peak?std::min<uint32_t>(32768,uint32_t(uint64_t(magnitude)*32768/peak)):0];}
inline uint8_t envelope(uint32_t magnitude,uint32_t peak){return envelope(tables(),magnitude,peak);}
} }
