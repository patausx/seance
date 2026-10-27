// Host-only Strain visual study; links the existing DSP, never the live engine.
#include "engine/units.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
using namespace seance;
namespace {
constexpr int N=1024, BURST=512;
using Buffer=std::array<q15,N>;
using Params=std::array<q15,6>;
constexpr Params BASE={15000,0,15000,12000,9000,24500};
void require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"strain probe: %s\n",why);std::exit(1);}}
void setup(StrainUnit& u,const Params& p){for(int i=0;i<6;++i){u.params[i].base=p[i];u.params[i].snap();}u.reset();}
Buffer source(){
    Buffer b{};
    // 250 Hz, nominal .75 FS; 2ms raised-cosine attack, 14ms release,
    // exactly 16ms digital silence after the burst. Same input for every state.
    for(int i=0;i<BURST;++i){
        const double env=i<64 ? .5-.5*std::cos(3.141592653589793*i/64.)
            : .5+.5*std::cos(3.141592653589793*(i-64)/447.);
        b[i]=(q15)std::lround(24575.*env*std::sin(2.*3.141592653589793*i/128.));
    }
    return b;
}
Buffer render(StrainUnit& u,Buffer b,int subdivision=0){
    for(int block=0;block<N;block+=CONTROL_INTERVAL){
        // Explicit reproducible phase: reset() tick, then tick before each block,
        // matching the engine's 32-frame audio/control cadence, no parameter slew.
        u.control_tick();
        for(int i=block;i<block+CONTROL_INTERVAL;){
            int n=subdivision==1 ? 1 : subdivision==2 ? 1+(i*17)%19 : CONTROL_INTERVAL;
            n=std::min(n,block+CONTROL_INTERVAL-i);u.process(b.data()+i,n);i+=n;
        }
    }
    return b;
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    static_assert(ENGINE_SR==32000 && CONTROL_INTERVAL==32,"reference timing changed");
    const std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);
    const Buffer input=source();
    const struct Case{const char* name;int param,value;} cases[]={
        {"01_default",0,15000},{"02_teeth",3,32767},{"03_body",2,27852},
        {"04_feedback",4,32767},{"05_bias",1,19660},{"06_drive",0,32767}};
    StrainUnit defaults;
    for(int p=0;p<6;++p)require(defaults.params[p].base==BASE[p],"constructor defaults drifted");
    std::ofstream summary(dir/"summary.tsv");
    summary<<"case\tdrive\tbias\tbody\tteeth\tfeedback\tmix\tmin\tmax\tmean_q15\ttail_peak_q15\tmotion_end\n";
    int checks=0;
    for(const auto& c:cases){
        Params p=BASE;p[c.param]=(q15)c.value;
        StrainUnit u;setup(u,p);const Buffer output=render(u,input);
        const int motion=u.visual_motion();
        std::ofstream samples(dir/(std::string(c.name)+".tsv"));
        samples<<"sample\tin\tout\n";
        int64_t sum=0;int tail=0;
        for(int i=0;i<N;++i){
            samples<<i<<'\t'<<input[i]<<'\t'<<output[i]<<'\n';sum+=output[i];
            if(i>=BURST)tail=std::max(tail,std::abs(int(output[i])));
        }
        summary<<c.name;for(auto value:p)summary<<'\t'<<value;
        summary<<'\t'<<*std::min_element(output.begin(),output.end())<<'\t'<<*std::max_element(output.begin(),output.end())
               <<'\t'<<double(sum)/N<<'\t'<<tail<<'\t'<<motion<<'\n';
        u.reset();require(render(u,input)==output,"reset reproducibility");++checks;
        setup(u,p);require(render(u,input,1)==output,"samplewise PCM at identical control ticks");++checks;
        setup(u,p);require(render(u,input,2)==output,"irregular subdivision PCM at identical control ticks");++checks;
        setup(u,p);require(render(u,Buffer{})==Buffer{},"reset silence");++checks;
        Params dry=p;dry[StrainUnit::P_MIX]=0;setup(u,dry);require(render(u,input)==input,"mix zero exact dry");++checks;
    }
    // Bias baseline cancellation, including full drive/teeth/feedback extremes.
    for(int bias:{-32767,0,32767}){
        Params p={32767,(q15)bias,32767,32767,32767,32767};StrainUnit u;setup(u,p);
        require(render(u,Buffer{})==Buffer{},"extreme bias silence");++checks;
    }
    std::printf("strain review: six real DSP traces; %d reset/silence/dry/subdivision checks passed\n",checks);
    std::puts("fixed 32ms / +/-1fs; 16ms synthetic burst then silence; reset state, 1kHz control ticks");
    std::puts("not a runtime UI capture, live audio, static transfer function or hardware acceptance");
}
