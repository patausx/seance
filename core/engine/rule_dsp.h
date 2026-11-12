// RULE online score arithmetic. No grids/notes or future RNG schedule is stored.
// Seed mixing follows NumPy SeedSequence (pool4, one u32 entropy word):
// Copyright (c) 2015 Melissa E. O'Neill; Copyright (c) 2019 NumPy Developers.
// PCG scheme: Copyright (c) 2014 Melissa O'Neill.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once
#include <cstdint>
namespace seance { namespace rule_dsp {
struct Wide { uint64_t hi=0,lo=0; };
inline Wide add(Wide a,Wide b) { const uint64_t l=a.lo+b.lo;return {a.hi+b.hi+uint64_t(l<a.lo),l}; }
inline Wide product(uint64_t a,uint64_t b) {
    const uint64_t al=uint32_t(a),ah=a>>32,bl=uint32_t(b),bh=b>>32;
    const uint64_t ll=al*bl,t=ah*bl+(ll>>32),mid=al*bh+uint32_t(t);
    return {ah*bh+(t>>32)+(mid>>32),(mid<<32)|uint32_t(ll)};
}
inline Wide multiply(Wide a,Wide b) { auto p=product(a.lo,b.lo);p.hi+=a.hi*b.lo+a.lo*b.hi;return p; }
struct Random {
    Wide state,inc; uint32_t cached=0;bool has_cached=false,failed=false;
    uint32_t hash(uint32_t v,uint32_t& k) { v^=k;k*=0x931e8875u;v*=k;return v^(v>>16); }
    void step() { state=add(multiply(state,{2549297995355413924ull,4865540595714422341ull}),inc); }
    void seed(uint32_t entropy) {
        uint32_t pool[4],words[8],k=0x43b0d7e5u;
        for(int i=0;i<4;++i)pool[i]=hash(i?0:entropy,k);
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(i!=j) {
            const uint32_t v=0xca01f9ddu*pool[j]-0x4973f715u*hash(pool[i],k);pool[j]=v^(v>>16);
        }
        k=0x8b51f9ddu;
        for(int i=0;i<8;++i){uint32_t v=pool[i%4]^k;k*=0x58f38dedu;v*=k;words[i]=v^(v>>16);}
        auto pair=[&](int i){return uint64_t(words[i])|(uint64_t(words[i+1])<<32);};
        const Wide initial={pair(0),pair(2)},sequence={pair(4),pair(6)};
        inc={(sequence.hi<<1)|(sequence.lo>>63),(sequence.lo<<1)|1u};
        state={};step();state=add(state,initial);step();has_cached=failed=false;cached=0;
    }
    uint64_t u64() { step();const uint64_t v=state.hi^state.lo;const unsigned r=unsigned(state.hi>>58);return (v>>r)|(v<<((64-r)&63)); }
    uint32_t u32() { if(has_cached){has_cached=false;return cached;}const uint64_t v=u64();cached=uint32_t(v>>32);has_cached=true;return uint32_t(v); }
    // NumPy int64 integers for small ranges uses Lemire32; shuffle(list) uses
    // random_interval's masked rejection. Cache survives interleaved u64 draws.
    uint32_t integer(uint32_t count) {
        if(count<=1)return 0;
        const uint32_t threshold=uint32_t(-count)%count;
        for(int i=0;i<128;++i){const uint64_t p=uint64_t(u32())*count;if(uint32_t(p)>=threshold)return uint32_t(p>>32);}
        failed=true;return 0; // explicit bounded fault, never a biased accepted draw
    }
    uint32_t interval(uint32_t max) {
        if(!max)return 0;
        uint32_t mask=max;mask|=mask>>1;mask|=mask>>2;mask|=mask>>4;mask|=mask>>8;mask|=mask>>16;
        for(int i=0;i<128;++i){const uint32_t v=u32()&mask;if(v<=max)return v;}failed=true;return 0;
    }
    uint64_t uniform53() { return u64()>>11; }
};
inline uint16_t next_row(uint16_t row,uint8_t rule) {
    uint16_t next=0;
    for(int c=0;c<15;++c){const unsigned index=(((row>>((c+14)%15))&1u)<<2)|(((row>>c)&1u)<<1)|((row>>((c+1)%15))&1u);next|=uint16_t(((rule>>index)&1u)<<c);}
    return next;
}
struct Score {
    Random rng;uint16_t row=128,previous=0;uint32_t generation=0;
    uint8_t accepted[3]={},count=0,rule=90;
    void reset(uint32_t seed) { rng.seed(seed);row=128;previous=0;generation=0;count=0;rule=90; }
    // Authored phrase: 64gens90/single/.6 ->30/single/.42 ->110/random/.5,
    // repeats indefinitely with continuous RNG. Manual mode uses the same online
    // birth/survival/death law, no automatic section changes.
    void advance(bool phrase,uint8_t manual_rule,uint16_t seed_row,uint32_t mutation,uint64_t density53,uint16_t edited_row=32768) {
        const uint32_t g=generation%64,section=(generation/64)%3;
        rule=phrase?(section==0?90:section==1?30:110):manual_rule;
        if(generation==0 || (phrase && g==0)) {
            previous=0;row=seed_row;
            if(phrase && section==2){row=0;for(int c=0;c<15;++c)row|=uint16_t(rng.integer(2)<<c);}
        } else if(!phrase) {
            // previous is the last ACTUAL (including mutation/edits) row, not
            // finish()'s old-rule scratch successor. Locks/mods apply this edge.
            row=next_row(previous,rule);
        }
        // An explicit row replacement owns this edge exactly; no mutation draw
        // or stale intermediate successor. Ordinary mutation resumes next edge.
        if(edited_row<32768)row=edited_row;
        else if(g && mutation && g%mutation==0)row^=uint16_t(1u<<rng.integer(15));
        uint8_t born[15];int n=0;for(int c=0;c<15;++c)if((row&(1u<<c)) && !(previous&(1u<<c)))born[n++]=uint8_t(c);
        for(int i=n-1;i>0;--i){const unsigned j=rng.interval(unsigned(i));const uint8_t t=born[i];born[i]=born[j];born[j]=t;}
        count=0;
        // Short circuit exactly like the recipe: once3 accepted, NO more draws.
        for(int i=0;i<n;++i)if(count<3 && rng.uniform53()<density53)accepted[count++]=born[i];
        ++generation;
    }
    void finish() { previous=row;row=next_row(row,rule); }
};
} }
