// Focused Tilt DSP only: frozen old laws plus independent fractional/trim oracle.
#include "engine/units.h"
#include "engine/tilt_eq.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <vector>
using namespace seance;
using trackr::fx::sat_q15;
namespace eq = seance::tilt_eq;
namespace {
uint64_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
void set(TiltUnit& u,int p,int value){u.params[p].base=(q15)value;u.params[p].snap();}
uint32_t random(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
q15 clip(int32_t value){
    if(value>=-24000&&value<=24000)return (q15)value;
    const bool negative=value<0;
    const int64_t over=std::min<int64_t>((negative?-(int64_t)value:value)-24000,8767*4);
    const int64_t r=over*32767/(8767*4),r2=r*r>>15,r3=r2*r>>15;
    const int32_t result=24000+(int32_t)((r-r3/3)*8767>>15);
    return (q15)(negative?-result:result);
}
static constexpr uint16_t PIVOT_COEFF[65] = {
    794,847,903,963,1026,1094,1166,1243,1325,1412,1504,1603,1708,
    1819,1937,2063,2197,2339,2489,2649,2819,2999,3190,3393,3607,3834,
    4074,4328,4596,4879,5178,5493,5825,6174,6541,6927,7332,7755,8199,
    8662,9146,9649,10172,10715,11278,11858,12457,13072,13703,14348,
    15005,15672,16348,17028,17711,18394,19074,19747,20409,21059,21691,
    22304,22892,23455,23988
};
static constexpr uint32_t BOOST_Q16[33] = {
    65536,68427,71446,74598,77890,81326,84914,88660,92572,96656,100921,
    105373,110022,114876,119944,125236,130762,136531,142554,148844,155410,
    162267,169426,176901,184706,192855,201363,210247,219523,229208,239321,
    249879,260904
};
static constexpr uint16_t CUT_Q16[33] = {
    65535,62767,60115,57574,55142,52812,50580,48443,46396,44435,42558,
    40760,39037,37388,35808,34295,32846,31458,30129,28856,27636,26469,
    25350,24279,23253,22270,21329,20428,19565,18738,17947,17188,16462
};
// Literal before-modern production block, isolated from current helper and DSP.
// Tables and soft clipping above are independent, frozen copies.
struct FrozenTilt {
    enum {P_TILT=0,P_FREQ,P_DRIVE,P_MIX,P_RANGE};
    struct {q15 effective=0;} params[5];
    int32_t low_=0;
void process(q15* buf, int frames) {

    // Q16 reciprocal shelf pairs, 0..12 dB in 0.375 dB steps. Reciprocal gains
    // keep BODY/AIR endpoints symmetric in dB without adding a makeup knob.


    const q15 tilt = params[P_TILT].effective;
    const q15 freq = params[P_FREQ].effective;
    const q15 drive = params[P_DRIVE].effective;
    const q15 mix = params[P_MIX].effective;
    const q15 range = params[P_RANGE].effective;

    if (range == 0) {
        // Compatibility law for files saved before RANGE existed. Its sonics stay
        // intact, but all formerly overflowing multiplies are widened and bounded.
        const int32_t coeff = 256 + ((uint32_t)(uint16_t)freq * 12000u >> 15);
        const int32_t gain = 32767 + (int32_t)drive * 3;
        for (int i = 0; i < frames; ++i) {
            const int32_t dry = buf[i];
            low_ += (q31)(((int64_t)(dry - low_) * coeff) >> 15);
            const int32_t high = dry - low_;
            int64_t shaped = (int64_t)dry + (((int64_t)high * tilt) >> 15) -
                (((int64_t)low_ * tilt) >> 15);
            shaped = (shaped * gain) >> 15;
            if (shaped > INT32_MAX) shaped = INT32_MAX;
            if (shaped < INT32_MIN) shaped = INT32_MIN;
            const q15 colored = clip((q31)shaped);
            buf[i] = sat_q15(dry + (q31)((((int64_t)colored - dry) * mix) >> 15));
        }
        return;
    }

    // Log-spaced 125 Hz..8 kHz pivot. Interpolation avoids zippering between the
    // compact offline-generated coefficient points.
    const uint32_t scaled = (uint32_t)(uint16_t)freq * 64u;
    uint32_t pivot_index = scaled >> 15;
    uint32_t pivot_frac = scaled & 32767u;
    if (pivot_index >= 64) { pivot_index = 63; pivot_frac = 32767; }
    const int32_t coeff = PIVOT_COEFF[pivot_index] +
        (int32_t)(((int64_t)(PIVOT_COEFF[pivot_index + 1] - PIVOT_COEFF[pivot_index]) *
                   pivot_frac) >> 15);
    const uint32_t magnitude = tilt < 0 ? (uint32_t)(-(int32_t)tilt) : (uint16_t)tilt;
    // RANGE maps to a maximum +/-3..12 dB; TILT chooses a fraction of it.
    const uint32_t max_step = 8u + ((uint32_t)(uint16_t)range * 24u >> 15);
    uint32_t gain_step = (magnitude * max_step + 16383u) / 32767u;
    if (gain_step > 32) gain_step = 32;
    const uint32_t boost = BOOST_Q16[gain_step];
    const uint32_t cut = CUT_Q16[gain_step];
    const uint32_t low_gain = tilt >= 0 ? cut : boost;
    const uint32_t high_gain = tilt >= 0 ? boost : cut;
    const int32_t drive_gain_q16 = 65536 + (int32_t)drive * 6;

    for (int i = 0; i < frames; ++i) {
        const int32_t dry = buf[i];
        low_ += (q31)(((int64_t)(dry - low_) * coeff) >> 15);
        const int32_t high = dry - low_;
        const int32_t tilted = (int32_t)(((int64_t)low_ * low_gain +
                                           (int64_t)high * high_gain) >> 16);
        q15 colored;
        if (tilt == 0 && drive == 0) {
            colored = (q15)dry;
        } else if (drive == 0) {
            // Neutral TILT with zero DRIVE is an exact identity, not a hidden
            // saturator. At nonzero tilt, only the shelf itself may reach q15 rails.
            colored = sat_q15(tilted);
        } else {
            int64_t driven = ((int64_t)tilted * drive_gain_q16) >> 16;
            if (driven > INT32_MAX) driven = INT32_MAX;
            if (driven < INT32_MIN) driven = INT32_MIN;
            colored = clip((q31)driven);
        }
        buf[i] = sat_q15(dry + (q31)((((int64_t)colored - dry) * mix) >> 15));
    }
}

};
void legacy_exact(){
    uint64_t hash[2]={1469598103934665603ull,1469598103934665603ull};
    for(int law=0;law<2;++law){TiltUnit u(false);FrozenTilt old;uint32_t rng=0x8182u;
        CHECK(!u.modern()&&u.num_params==5&&u.params[5].base==eq::TRIM_UNITY);
        for(int block=0;block<200;++block){
            const int p[]={block%11==0?0:(int)(q15)random(rng),(int)(random(rng)%32768),block%7?int(random(rng)%32768):0,int(random(rng)%32768),law?1+int(random(rng)%32767):0};
            for(int k=0;k<5;++k){set(u,k,p[k]);old.params[k].effective=(q15)p[k];}
            set(u,5,block%2?0:32767); // hidden tail must have no influence
            q15 x[257],y[257];for(int i=0;i<257;++i)x[i]=y[i]=(q15)random(rng);
            u.process(x,257);old.process(y,257);
            for(int i=0;i<257;++i){CHECK(x[i]==y[i]);hash[law]^=(uint16_t)x[i];hash[law]*=1099511628211ull;}
        }
    }
    CHECK(hash[0]==0x3e6357a4402dccfbull&&hash[1]==0x8689fa73499fc074ull);
    std::printf("ok frozen legacy RANGE0+stepped PCM 102800 samples fnv=%016llx/%016llx\n",(unsigned long long)hash[0],(unsigned long long)hash[1]);
}
uint32_t gain_oracle(q15 trim){
    const int raw=std::max(0,(int)trim);
    if(raw==26214)return 65536;
    const double db=raw<=26214?-24.0+24.0*raw/26214:6.0*(raw-26214)/6553;
    if(raw==32767)return 130762;
    // Independent floating interpolation of mathematically generated knots.
    const double position=(db+24)/.375;
    const int i=(int)std::floor(position+1e-10);const double fraction=position-i;
    const double a=std::round(65536*std::pow(10.0,(-24+i*.375)/20));
    const double b=std::round(65536*std::pow(10.0,(-24+(i+1)*.375)/20));
    return (uint32_t)std::floor(a+(b-a)*fraction+1e-8);
}
void shelf_oracle(q15 tilt,q15 range,uint32_t& low,uint32_t& high){
    const double mag=std::min(32767,std::abs((int)tilt));
    const double position=mag*(8+std::max(0,(int)range)*24/32768)/32767;
    const int i=(int)position;const double f=position-i;
    uint32_t boost=(uint32_t)std::floor(BOOST_Q16[i]+(BOOST_Q16[i+1]-BOOST_Q16[i])*f+1e-8);
    uint32_t cut=(uint32_t)std::floor(CUT_Q16[i]+((int)CUT_Q16[i+1]-CUT_Q16[i])*f+1e-8);
    if(!tilt)boost=cut=65536;
    low=tilt>=0?cut:boost;high=tilt>=0?boost:cut;
}
int coeff_oracle(q15 freq){
    const unsigned pos=(uint16_t)freq*64;unsigned i=pos/32768,f=pos%32768;
    if(i>=64){i=63;f=32767;}
    return PIVOT_COEFF[i]+(PIVOT_COEFF[i+1]-PIVOT_COEFF[i])*f/32768;
}
void mappings(){
    static_assert(TiltUnit::P_TILT==0&&TiltUnit::P_FREQ==1&&TiltUnit::P_DRIVE==2&&TiltUnit::P_MIX==3&&TiltUnit::P_RANGE==4&&TiltUnit::P_TRIM==5&&TiltUnit::P_COUNT==6);
    TiltUnit u;CHECK(u.modern()&&u.num_params==6&&u.params[5].base==eq::TRIM_UNITY&&!u.params[5].bipolar&&u.params[0].bipolar);
    CHECK(eq::trim_db(0)==-24&&eq::trim_db(32767)==6&&eq::trim_db(eq::TRIM_UNITY)==0);
    CHECK(eq::trim_gain_q16(0)==4135&&eq::trim_gain_q16(32767)==130762&&eq::trim_gain_q16(eq::TRIM_UNITY)==65536);
    CHECK(eq::max_step(-32768)==8&&eq::max_step(0)==8&&eq::max_step(32767)==31);
    double worst=0;uint32_t prev=0;
    for(int raw=-32768;raw<=32767;++raw){
        const auto got=eq::trim_gain_q16((q15)raw);CHECK(got==gain_oracle((q15)raw));CHECK(got>=prev);prev=got;
        const double expected=std::pow(10.,eq::trim_db((q15)raw)/20)*65536;
        worst=std::max(worst,std::abs(got/expected-1));
        CHECK(eq::coeff((q15)raw)==coeff_oracle((q15)raw));
        CHECK(eq::coeff((q15)raw,true)==256+int((uint16_t)raw*12000u/32768));
    }
    CHECK(worst<0.00035);
    for(int range:{-32768,0,1,8192,21846,32767})for(int tilt=-32768;tilt<=32767;++tilt){uint32_t l,h,a,b;
        eq::shelf_gains((q15)tilt,(q15)range,true,l,h);shelf_oracle((q15)tilt,(q15)range,a,b);CHECK(l==a&&h==b);
        CHECK(l>=17188&&l<=249879&&h>=17188&&h<=249879);
    }
    uint32_t l,h;eq::shelf_gains(32767,0,true,l,h);CHECK(l==CUT_Q16[8]&&h==BOOST_Q16[8]);
    eq::shelf_gains(-32768,32767,true,l,h);CHECK(l==BOOST_Q16[31]&&h==CUT_Q16[31]);
    std::printf("ok exact mappings/defaults/endpoints, 393216 independent fractional pairs, trim pow relative error %.8f\n",worst);
}
q15 modern_sample(int32_t& lp,q15 dry,const int* p){
    const int a=coeff_oracle((q15)p[1]);uint32_t l,h;shelf_oracle((q15)p[0],(q15)p[4],l,h);
    lp+=(int32_t)(((int64_t)(dry-lp)*a)>>15);
    const int shaped=(int32_t)(((int64_t)lp*l+(int64_t)(dry-lp)*h)>>16);
    q15 colored=0;
    if(!p[0]&&!p[2])colored=dry;
    else if(!p[2])colored=(q15)std::clamp(shaped,-32768,32767);
    else colored=clip((int32_t)(((int64_t)shaped*(65536+p[2]*6))>>16));
    const int gain=gain_oracle((q15)p[5]);
    const int wet=gain==65536?colored:(int)(((int64_t)colored*gain)>>16);
    return (q15)std::clamp((int)dry+(int)(((int64_t)(wet-dry)*p[3])>>15),-32768,32767);
}
void modern_pcm(){
    TiltUnit u;int32_t lp=0;uint32_t rng=1774;
    for(int block=0;block<160;++block){int p[]={int((q15)random(rng)),int(random(rng)%32768),block%5?int(random(rng)%32768):0,int(random(rng)%32768),block%3?int(random(rng)%32768):0,block%11?int(random(rng)%32768):eq::TRIM_UNITY};
        for(int k=0;k<6;++k)set(u,k,p[k]);
        q15 x[257],y[257];for(int i=0;i<257;++i){x[i]=(q15)random(rng);y[i]=modern_sample(lp,x[i],p);}
        u.process(x,257);for(int i=0;i<257;++i)CHECK(x[i]==y[i]);
    }
    // Unity trim at nonzero shelf endpoints is identical to the old stepped DSP.
    for(int tilt:{-32767,32767})for(int drive:{0,32767}){TiltUnit a,b(false);for(TiltUnit* x:{&a,&b}){set(*x,0,tilt);set(*x,2,drive);set(*x,4,32767);}
        std::vector<q15>x(8192),y;for(auto& s:x)s=(q15)random(rng);y=x;a.process(x.data(),x.size());b.process(y.data(),y.size());CHECK(x==y);}
    std::puts("ok modern PCM 41120 samples independent coefficient/shelf/clip/trim oracle, unity endpoint legacy parity");
}
void trim_order_neutral(){
    TiltUnit u;std::vector<q15> input(65536);for(int i=0;i<65536;++i)input[i]=(q15)(i-32768);
    for(int range:{0,8192,32767})for(int mix:{0,8192,32767}){u.reset();set(u,0,0);set(u,2,0);set(u,3,mix);set(u,4,range);set(u,5,eq::TRIM_UNITY);auto x=input;u.process(x.data(),x.size());CHECK(x==input);}
    for(int trim:{-32768,0,int(eq::TRIM_UNITY),32767}){u.reset();set(u,0,32767);set(u,2,32767);set(u,3,0);set(u,5,trim);auto x=input;u.process(x.data(),x.size());CHECK(x==input);}
    // Positive boosted wet remains wide before mix; no early trim hard clip.
    for(int trim:{0,int(eq::TRIM_UNITY),32767})for(int mix:{8192,16384,32767}){u.reset();const int p[]={0,16384,32767,mix,0,trim};for(int k=0;k<6;++k)set(u,k,p[k]);
        q15 audio[]={32767,-32768,28000,-28000,1000,-1000};int32_t lp=0;for(auto& x:audio){q15 expected=modern_sample(lp,x,p);u.process(&x,1);CHECK(x==expected);}}
    u.set_modern(false);CHECK(!u.modern()&&u.num_params==5&&u.params[5].base==eq::TRIM_UNITY&&u.params[5].src==ModSource::None);
    u.set_modern(true);CHECK(u.modern()&&u.num_params==6&&u.params[5].effective==eq::TRIM_UNITY);
    std::puts("ok all65536 input neutral/dry identities, post-saturation/pre-mix wet trim headroom and mode hidden-unity reset");
}
void reset_blocks(){
    uint32_t rng=499;for(bool modern:{false,true}){TiltUnit a(modern),b(modern);
        for(int block=0;block<60;++block){for(int k=0;k<a.num_params;++k){const int raw=k?int(random(rng)%32768):int((q15)random(rng));set(a,k,raw);set(b,k,raw);}if(block%7==0){a.reset();b.reset();}
            std::vector<q15>x(1007),y;for(auto& s:x)s=(q15)random(rng);y=x;a.process(x.data(),x.size());
            for(int i=0;i<(int)y.size();){int n=std::min((int)y.size()-i,1+int(random(rng)%83));b.process(y.data()+i,n);i+=n;}CHECK(x==y);
        }
        a.reset();std::vector<q15> silence(4096,0);a.process(silence.data(),silence.size());for(q15 s:silence)CHECK(s==0);
        q15 untouched=1234;a.process(nullptr,4);a.process(&untouched,0);a.process(&untouched,-1);CHECK(untouched==1234);
    }
    std::puts("ok reset, silence, block subdivision/extrema/bounds and null/nonpositive calls");
}
}
int main(){legacy_exact();mappings();modern_pcm();trim_order_neutral();reset_blocks();std::printf("tilt dsp: 5/5 passed (%llu checks)\n",(unsigned long long)checks);}
