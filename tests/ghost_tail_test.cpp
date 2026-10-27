#include "ghost_tuning_test.h"
#include "../core/engine/ghost_decay.h"
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <stdexcept>

using namespace seance;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(0)
static uint32_t delay(q15 tone,q15 fine=0) {
    uint32_t d=(uint32_t)((149ULL*(32767u+(32767u-(uint16_t)tone)*3u)*256u)>>15);
    return (uint32_t)(((uint64_t)d*GhostUnit::fine_delay_ratio(fine))>>24);
}
static void setup(GhostUnit& g,bool modern,int tone,int decay,int smear,int drift=0) {
    g.long_tail=modern;
    const int v[]={tone,decay,smear,drift,0,32767,0,28000};
    for(int p=0;p<8;++p){g.params[p].base=(q15)v[p];g.params[p].snap();}
}
static uint64_t tuned_legacy_hash() {
    uint64_t h=1469598103934665603ULL;
    for(int tone:{0,15000,32767})for(int fine:{-24000,13000}) {
        GhostUnit g;setup(g,false,tone,29000,12000,7000);g.params[6].effective=(q15)fine;
        uint32_t rng=71;q15 out[128];
        for(int block=0;block<2000;++block){
            for(auto& x:out){rng=rng*1664525u+1013904223u;x=block<250?(q15)(rng>>16):0;}
            g.params[1].effective=(q15)(10000+block%7*3700);g.params[2].effective=(q15)(block%4*10000);
            g.params[4].effective=block>=500&&block<1300?32767:0;
            g.process(out,128);for(auto x:out){h^=(uint16_t)x;h*=1099511628211ULL;}
        }
    }
    return h;
}
static void curve_test() {
    const double l0=-std::log(18000./32768),l1=-std::log(.999);
    for(int tone:{0,15000,32767})for(int fine:{-32767,0,32767}) {
        int previous=0;
        for(int v=0;v<=32767;++v) {
            const auto d=delay((q15)tone,(q15)fine);
            const auto g=ghost_decay::gain(v,d);
            CHECK(g>=previous&&g<=32755);previous=g;
            // Compare fixed point exp against the actual piecewise loss interpolation.
            const double p=v*32./32767;const int i=(int)p;
            const double lo=l0*std::pow(l1/l0,i/32.);
            const double hi=l0*std::pow(l1/l0,std::min(32,i+1)/32.);
            const double expected=32768*std::exp(-(lo+(hi-lo)*(p-i))*d/delay(15000));
            CHECK(std::abs(g-expected)<1.1);
        }
    }
    for(int v:{0,8192,16384,24576,32767}) {
        const int g=ghost_decay::gain(v,delay(15000));
        std::printf("curve %3d%%: feedback %.6f, nominal unfiltered first-well t60 %.3fs\n",v*100/32767,g/32768.,-std::log(1000.)*delay(15000)/256/32000/std::log(g/32768.));
    }
}
static std::vector<uint8_t> bytes(const char* path) {
    std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
static void put(const char* path,const std::vector<uint8_t>& b) {
    std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());
}
static void persistence_test() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    auto* g=new GhostUnit;delete a->replace_unit(0,0,g);CHECK(g->long_tail);
    const char* path="/tmp/seance_ghost_tail.snc";
    for(bool modern:{false,true}) {
        setup(*g,modern,21000,29000,12000);
        g->params[6].base=-2345;g->params[7].src=ModSource::SigilB;g->params[7].mod_depth=4000;
        CHECK(a->set_step_lock(0,0,0,1,31000));
        CHECK(patchio::save(*a,path));const auto saved=bytes(path);
        CHECK(patchio::load(*b,path));
        auto* loaded=static_cast<GhostUnit*>(b->chains[0].units[0]);CHECK(loaded->long_tail==modern);
        CHECK(b->pattern.steps[0][0].locks[0].value==31000);
        CHECK(loaded->params[6].base==-2345&&loaded->params[7].src==ModSource::SigilB);
        std::unique_ptr<Unit> clone(clone_unit(loaded));CHECK(static_cast<GhostUnit*>(clone.get())->long_tail==modern);
        CHECK(patchio::save(*b,path));CHECK(bytes(path)==saved);
    }
    for(int version=1;version<=6;++version) {
        std::vector<uint8_t> old={'S','N','C',(uint8_t)('0'+version),(uint8_t)version};
        for(int c=0;c<NUM_CHAINS;++c) {
            old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);
            const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);
            if(!c){old[first]=(uint8_t)UnitKind::Ghost;for(int p=0;p<6;++p){old[first+3+p*5]=(uint8_t)(p*3000);old[first+4+p*5]=(uint8_t)((p*3000)>>8);}}
        }
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);
        if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}
        if(version>=4)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);
        if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}
        if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(path,old);CHECK(patchio::load(*b,path));
        auto* loaded=static_cast<GhostUnit*>(b->chains[0].units[0]);CHECK(!loaded->long_tail);
        for(int p=0;p<6;++p)CHECK(loaded->params[p].base==p*3000);
        CHECK(patchio::save(*b,path)&&patchio::load(*a,path));CHECK(!static_cast<GhostUnit*>(a->chains[0].units[0])->long_tail);
    }
    std::remove(path);
}
static void freeze_test() {
    for(bool modern:{false,true}) {
        GhostUnit a,b;setup(a,modern,15000,31000,12000,6000);setup(b,modern,15000,31000,12000,6000);
        q15 x[128],y[128];
        for(int block=0;block<2000;++block) {
            for(int i=0;i<128;++i)x[i]=y[i]=block<100?(q15)((i*327+block*193)%28000-14000):0;
            if(block==100)a.params[4].effective=b.params[4].effective=32767;
            if(block>102&&block<1850){b.long_tail=!modern;for(int p:{0,1,2,3,6,7})b.params[p].effective=(block&1)?32767:0;}
            if(block==1850){setup(b,modern,15000,31000,12000,6000);b.params[4].effective=32767;}
            if(block==1900)a.params[4].effective=b.params[4].effective=0;
            a.process(x,128);b.process(y,128);CHECK(!std::memcmp(x,y,sizeof x));
        }
    }
}
struct Stats { double mean=0,rms=0,ac=0;int peak=0; };
static Stats stats(const std::vector<q15>& v,size_t start,size_t end) {
    Stats s;double square=0;for(size_t i=start;i<end;++i){s.mean+=v[i];square+=(double)v[i]*v[i];s.peak=std::max(s.peak,std::abs((int)v[i]));}
    s.mean/=end-start;s.rms=std::sqrt(square/(end-start));s.ac=std::sqrt(std::max(0.,s.rms*s.rms-s.mean*s.mean));return s;
}
static std::vector<q15> render(bool modern,int tone,int decay,int smear,int input,int drift,int seconds) {
    GhostUnit g;setup(g,modern,tone,decay,smear,drift);
    std::vector<q15> out(seconds*32000);uint32_t rng=9173;
    const double hz=GhostUnit::root_millihz((q15)tone,0)/1000.;
    for(size_t n=0;n<out.size();++n){
        rng=rng*1664525u+1013904223u;
        if(input==0)out[n]=n==0?24000:0;
        else if(n<32000)out[n]=input==1?(q15)(12000*std::sin(6.283185307179586*hz*n/32000)):
            input==2?(q15)((int16_t)(rng>>16)/2):32767;
    }
    for(size_t n=0;n<out.size();n+=128)g.process(out.data()+n,128);
    return out;
}
static void wav(const std::string& path,const std::vector<q15>& v) {
    std::ofstream f(path,std::ios::binary);
    auto u16=[&](uint16_t x){f.put(x&255);f.put(x>>8);};auto u32=[&](uint32_t x){u16(x&65535);u16(x>>16);};
    f.write("RIFF",4);u32(36+v.size()*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(32000);u32(64000);u16(2);u16(16);f.write("data",4);u32(v.size()*2);for(auto x:v)u16((uint16_t)x);CHECK(f.good());
}
static void comparisons(const char* folder) {
    if(folder)std::filesystem::create_directories(folder);
    std::ofstream csv;if(folder){csv.open(std::string(folder)+"/comparison.csv");csv<<"mode,tone,decay,smear,input,drift,peak,mean_last,rms_early,ac_early,rms_late,ac_late\n";}
    double largest_dc=0;int cases=0;
    for(int tone:{0,15000,32767})for(int decay:{16384,28000,32767})for(int smear:{0,12000,32767})for(int input:{0,1,2,3})for(bool modern:{false,true}) {
        const int drift=(decay==28000)?6000:0;
        const auto v=render(modern,tone,decay,smear,input,drift,20);
        const auto full=stats(v,0,v.size()),early=stats(v,32000,64000),late=stats(v,v.size()-32000,v.size());
        CHECK(full.peak<30000);if(modern)largest_dc=std::max(largest_dc,std::abs(late.mean));
        if(folder)csv<<(modern?"long":"old")<<','<<tone<<','<<decay<<','<<smear<<','<<input<<','<<drift<<','<<full.peak<<','<<late.mean<<','<<early.rms<<','<<early.ac<<','<<late.rms<<','<<late.ac<<'\n';
        // Small listening set: 28k decay, default smear, three registers/input types.
        if(folder&&decay==28000&&smear==12000&&input<3) {
            const std::string name=std::to_string(tone)+"_"+(input==0?"impulse":input==1?"tone":"noise")+"_"+(modern?"long":"old")+".wav";
            wav(std::string(folder)+"/"+name,v);
        }
        ++cases;
    }
    std::printf("comparison: %d paired-matrix renders x20s, max modern last-second DC %.3f LSB\n",cases,largest_dc);
    for(int tone:{0,15000,32767})for(int smear:{0,12000,32767})for(int input:{0,2,3}) {
        if(tone!=15000&&smear!=12000)continue;
        // The fourth well is 4.58x longer than the root, up to ~385s nominal t60.
        const auto v=render(true,tone,32767,smear,input,0,900);
        const auto last=stats(v,v.size()-32000,v.size());
        if(last.peak)std::printf("remaining tone=%d smear=%d input=%d mean=%g ac=%g peak=%d\n",tone,smear,input,last.mean,last.ac,last.peak);
        CHECK(last.peak==0); // no rounding limit cycle, including DC excitation
    }
    // Continuous worst input reaches a bounded state and then actually releases.
    for(int input:{0,1,2})for(int smear:{0,32767}) {
        GhostUnit g;setup(g,true,input?32767:0,32767,smear,32767);g.params[6].effective=input?-32767:32767;
        q15 buf[128];uint32_t rng=11;int peak=0;
        for(int block=0;block<240000;++block){ // 60 seconds driven, 900 seconds silence
            for(auto& x:buf){rng=rng*1664525u+1013904223u;x=block>=15000?0:input==0?32767:input==1?-32768:(q15)(rng>>16);}
            g.process(buf,128);
            for(auto x:buf){peak=std::max(peak,std::abs((int)x));if(block>=239750)CHECK(x==0);}
        }
        CHECK(peak<30000);
    }
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--fingerprint"){std::printf("%016llx %016llx\n",(unsigned long long)ghost_legacy_fingerprint(),(unsigned long long)tuned_legacy_hash());return 0;}
        CHECK(argc==1||(argc==3&&std::string(argv[1])=="--dump"));
        CHECK(test_ghost_tuning());CHECK(tuned_legacy_hash()==0xd08875d7b2377669ULL);curve_test();persistence_test();freeze_test();comparisons(argc==3?argv[2]:nullptr);
        std::puts("ok: ghost tail / legacy / curve / freeze / persistence / comparisons");return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
