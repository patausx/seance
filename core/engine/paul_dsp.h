// Independently written radix-2 spectral primitives, not PaulStream/KissFFT.
#pragma once
#include "paul_tables.h"
#include <algorithm>
#include <cstdint>
namespace seance { namespace paul_dsp {
struct Complex { int32_t re=0, im=0; };
// Q8 fractional-q15 time data. Forward divides EACH stage by two (1/N),
// inverse is UNSCALED: roundtrip is unity, not 1/N twice. With q15 PCM,
// Parseval/Cauchy bound the inverse sum of magnitudes by sqrt(N)*8388608
// (<760 million for N8192); pitch splatting never increases that L1 sum.
// n and FFT lengths are powers of two, 2..8192. Fixed bit swaps replace
// logN serial iterations; all shifts are unsigned (ARMv6K has CLZ, not RBIT).
inline uint32_t reverse(uint32_t v, uint32_t n) {
    v=((v>>1)&0x55555555u)|((v&0x55555555u)<<1);
    v=((v>>2)&0x33333333u)|((v&0x33333333u)<<2);
    v=((v>>4)&0x0f0f0f0fu)|((v&0x0f0f0f0fu)<<4);
    v=((v>>8)&0x00ff00ffu)|((v&0x00ff00ffu)<<8);
    v=(v>>16)|(v<<16);
    return v>>(unsigned(__builtin_clz(n))+1);
}
inline int32_t mul(int64_t v) { return int32_t(v/(int64_t(1)<<30)); }
inline void butterfly(Complex* x,uint32_t n,uint32_t length,uint32_t index,bool inverse) {
    const uint32_t half=length/2, j=index&(half-1), a=((index&~(half-1))<<1)+j, b=a+half;
    const uint32_t phase=j<<(unsigned(__builtin_clz(length))-18);
    const int32_t c=paul_tables::cosine[phase];
    const int32_t s=paul_tables::cosine[(phase+6144)&8191]*(inverse?1:-1);
    const int32_t re=mul(int64_t(x[b].re)*c-int64_t(x[b].im)*s);
    const int32_t im=mul(int64_t(x[b].re)*s+int64_t(x[b].im)*c);
    const int64_t ar=x[a].re, ai=x[a].im;
    // A runtime 1/2 divisor emitted FOUR __aeabi_ldivmod calls per ARM
    // butterfly, even for inverse /1. Branch BEFORE constant divisions so GCC
    // emits signed bias + shifts for /2, retaining truncation toward zero.
    if(inverse){
        x[a]={int32_t(ar+re),int32_t(ai+im)};
        x[b]={int32_t(ar-re),int32_t(ai-im)};
    }else{
        x[a]={int32_t((ar+re)/2),int32_t((ai+im)/2)};
        x[b]={int32_t((ar-re)/2),int32_t((ai-im)/2)};
    }
    (void)n;
}
// `count` butterflies of one pass starting at `first` (pass index 0..n/2-1): the
// arithmetic of butterfly() item by item with the pass constants hoisted. The
// butterflies of a pass touch disjoint pairs, so any split of a pass into calls
// yields the same bits. In the first two passes the twiddle is exactly 1 or
// -/+i (cosine[0]==2^30, cosine[2048]==cosine[6144]==0): mul(v*2^30)==v, so the
// rotation is a copy/swap with the same int32 narrowing and no multiply.
inline void butterflies(Complex* x,uint32_t length,uint32_t first,uint32_t count,bool inverse) {
    const uint32_t half=length/2, mask=half-1, end=first+count;
    const unsigned shift=unsigned(__builtin_clz(length))-18;
    const int32_t sign=inverse?1:-1;
    for(uint32_t i=first;i<end;++i){
        const uint32_t j=i&mask, a=((i&~mask)<<1)+j, b=a+half;
        int32_t re,im;
        if(half<=2){
            re=x[b].re;im=x[b].im;
            if(j){
                const int32_t t=re;
                if(inverse){re=int32_t(-int64_t(im));im=t;}else{re=im;im=int32_t(-int64_t(t));}
            }
        }else{
            const uint32_t phase=j<<shift;
            const int32_t c=paul_tables::cosine[phase];
            const int32_t s=paul_tables::cosine[(phase+6144)&8191]*sign;
            re=mul(int64_t(x[b].re)*c-int64_t(x[b].im)*s);
            im=mul(int64_t(x[b].re)*s+int64_t(x[b].im)*c);
        }
        const int64_t ar=x[a].re, ai=x[a].im;
        if(inverse){
            x[a]={int32_t(ar+re),int32_t(ai+im)};
            x[b]={int32_t(ar-re),int32_t(ai-im)};
        }else{
            x[a]={int32_t((ar+re)/2),int32_t((ai+im)/2)};
            x[b]={int32_t((ar-re)/2),int32_t((ai-im)/2)};
        }
    }
}
// Exact restoring root, at most 32 iterations. Leading zero pairs would only
// shift a zero answer: skip them with two bounded 32-bit CLZs (never CLZ(0)).
inline uint32_t root(uint64_t x) {
    if(!x)return 0;
    const uint32_t high=uint32_t(x>>32);
    if(!high){
        // The same recurrence on the same numbers in 32 bits: answer stays below
        // 2^31 and answer+bit below 2^32, so every comparison and result is equal.
        uint32_t v=uint32_t(x), answer=0, bit=uint32_t(1)<<((31u-unsigned(__builtin_clz(v)))&~1u);
        for(;bit;bit>>=2){
            if(v>=answer+bit){v-=answer+bit;answer=(answer>>1)+bit;}else answer>>=1;
        }
        return answer;
    }
    const unsigned top=high?63u-unsigned(__builtin_clz(high)):31u-unsigned(__builtin_clz(uint32_t(x)));
    uint64_t answer=0, bit=uint64_t(1)<<(top&~1u);
    for(;bit;bit>>=2){
        if(x>=answer+bit){x-=answer+bit;answer=(answer>>1)+bit;}else answer>>=1;
    }
    return uint32_t(answer);
}
inline uint32_t magnitude(Complex x) {
    return root(uint64_t(int64_t(x.re)*x.re)+uint64_t(int64_t(x.im)*x.im));
}
// For every uint32 magnitude: reciprocal is rounded DOWN, so the initial
// quotient is exact or one low (m/2^32 < 1). One remainder correction makes
// this identical to uint64(m)*N*factor/sum, not an approximate gain.
// Products fit uint64 even for UINT32_MAX; no runtime division or signed shift.
template<uint32_t N, uint32_t Sum>
inline uint32_t coherent(uint32_t m,bool interior) {
    constexpr uint32_t reciprocal=uint32_t((uint64_t(N)*256<<32)/Sum);
    constexpr uint32_t reciprocal2=uint32_t((uint64_t(N)*512<<32)/Sum);
    uint32_t q=uint32_t((uint64_t(m)*(interior?reciprocal2:reciprocal))>>32);
    const uint64_t numerator=uint64_t(m)*N*(interior?512u:256u);
    if(numerator-uint64_t(q)*Sum>=Sum)++q;
    return q;
}
inline uint32_t coherent(uint32_t m,uint32_t n,bool interior) {
    return n==2048?coherent<2048,paul_tables::sum2048>(m,interior):
        n==8192?coherent<8192,paul_tables::sum8192>(m,interior):
        coherent<4096,paul_tables::sum4096>(m,interior);
}
inline uint32_t spectrum_group(uint32_t index,uint32_t n) {
    const uint32_t numerator=index*96;
    return std::min<uint32_t>(95u,n==2048?numerator/1025u:n==8192?numerator/4097u:numerator/2049u);
}
inline const uint32_t* window(uint32_t n) {return n==2048?paul_tables::hann2048:n==8192?paul_tables::hann8192:paul_tables::hann4096;}
inline const uint32_t* normalization(uint32_t n) {return n==2048?paul_tables::norm2048:n==8192?paul_tables::norm8192:paul_tables::norm4096;}
} } // namespace seance::paul_dsp
