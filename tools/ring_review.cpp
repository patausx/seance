// Read-only review probe, not a new DSP implementation or hardware capture.
// Build command is recorded by tools/render_ring_clear_study.py.
// It links only this host probe and the unchanged core/engine/units.cpp.
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
constexpr int N=1024;
using Buffer=std::array<q15,N>;
using Params=std::array<q15,6>;
constexpr Params BASE={19000,24576,0,0,24576,0};
void require(bool ok,const char* what){if(!ok){std::fprintf(stderr,"probe failed: %s\n",what);std::exit(1);}}
void setup(RingUnit& u,const Params& p){for(int i=0;i<6;++i){u.params[i].base=p[i];u.params[i].snap();}u.reset();}
Buffer input(){
    Buffer b{};
    // Same deterministic 250 Hz burst in every case, nominal peak .75 FS.
    // 4 ms raised-cosine attack, then raised-cosine decay to sample 1023.
    for(int i=0;i<N;++i){
        const double env=i<128 ? .5-.5*std::cos(3.141592653589793*i/128.)
            : .5+.5*std::cos(3.141592653589793*(i-128)/895.);
        b[i]=(q15)std::lround(24575.*env*std::sin(2.*3.141592653589793*i/128.));
    }
    return b;
}
int carrier(uint16_t phase){
    const int tri=phase<32768 ? int(phase)*2-32768 : 32767-(int(phase)-32768)*2;
    return trackr::fx::sat_q15((int32_t)((int64_t(tri)*(65536-std::abs(tri)))>>15));
}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);
    const Buffer source=input();
    const struct Case {const char* name;int p,value;} cases[]={
        {"01_clean",3,0},{"02_bridge",3,32767},{"03_feedback",2,22937},{"04_follow",5,32767}};
    std::ofstream summary(dir/"summary.tsv");
    summary<<"case\tfreq\tdepth\tfeedback\tdiode\tmix\tfollow\tmin\tmax\tmean_q15\tenvelope_max\n";
    for(const auto& c:cases){
        Params settings=BASE;settings[c.p]=(q15)c.value;
        RingUnit u;setup(u,settings);Buffer output=source;
        std::ofstream samples(dir/(std::string(c.name)+".tsv"));
        samples<<"sample\tin\tout\tcarrier\tenvelope\tphase\n";
        int64_t sum=0;int envelope_max=0;
        for(int i=0;i<N;++i){
            u.process(&output[i],1);
            samples<<i<<'\t'<<source[i]<<'\t'<<output[i]<<'\t'<<carrier(u.visual_phase())
                   <<'\t'<<u.visual_envelope()<<'\t'<<u.visual_phase()<<'\n';
            sum+=output[i];envelope_max=std::max(envelope_max,int(u.visual_envelope()));
        }
        summary<<c.name;for(auto p:settings)summary<<'\t'<<p;
        summary<<'\t'<<*std::min_element(output.begin(),output.end())<<'\t'<<*std::max_element(output.begin(),output.end())
               <<'\t'<<double(sum)/N<<'\t'<<envelope_max<<'\n';
        // Confirm the samplewise phase observation does not change the PCM.
        RingUnit block;setup(block,settings);Buffer whole=source;block.process(whole.data(),N);
        require(whole==output,"samplewise vs block PCM");
        block.reset();whole=source;block.process(whole.data(),N);require(whole==output,"reset reproducibility");
        Buffer zero{};setup(block,settings);block.process(zero.data(),N);require(zero==Buffer{},"reset silence");
        Params dry_settings=settings;dry_settings[RingUnit::P_MIX]=0;
        setup(block,dry_settings);whole=source;block.process(whole.data(),N);require(whole==source,"mix zero exact dry");
        dry_settings=settings;dry_settings[RingUnit::P_DEPTH]=0;
        setup(block,dry_settings);whole=source;block.process(whole.data(),N);require(whole==source,"depth zero exact dry");
        // Variable non-power-of-two block sizes, same effective controls.
        setup(block,settings);whole=source;
        for(int i=0;i<N;){const int n=std::min(1+(i*17)%83,N-i);block.process(whole.data()+i,n);i+=n;}
        require(whole==output,"irregular block PCM");
    }
    std::puts("ring review: four real DSP reference traces; 24 dry/silence/reset/subdivision probes passed");
    std::puts("synthetic host investigation only; not a UI, listening, spectrum or hardware acceptance test");
}
