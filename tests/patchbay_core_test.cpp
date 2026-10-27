// Universal sigils core regression. Former separate-LFO version is in the approved archive.
#include "engine/engine.h"
#include "engine/patch_io.h"
#include "engine/project_session.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <vector>
using namespace seance;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"sigils FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static bool watching = false;
static unsigned allocations = 0;
extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t,size_t); void* __real_realloc(void*,size_t);
void* __real__Znwm(size_t); void* __real__Znam(size_t);
void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&) noexcept;
void* __real__ZnamRKSt9nothrow_t(size_t,const std::nothrow_t&) noexcept;
void* __wrap_malloc(size_t n) { if(watching)++allocations; return __real_malloc(n); }
void* __wrap_calloc(size_t n,size_t z) { if(watching)++allocations; return __real_calloc(n,z); }
void* __wrap_realloc(void* p,size_t n) { if(watching)++allocations; return __real_realloc(p,n); }
void* __wrap__Znwm(size_t n) { if(watching)++allocations; return __real__Znwm(n); }
void* __wrap__Znam(size_t n) { if(watching)++allocations; return __real__Znam(n); }
void* __wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t& t) noexcept { if(watching)++allocations; return __real__ZnwmRKSt9nothrow_t(n,t); }
void* __wrap__ZnamRKSt9nothrow_t(size_t n,const std::nothrow_t& t) noexcept { if(watching)++allocations; return __real__ZnamRKSt9nothrow_t(n,t); }
}
static void frames(Engine& e,int n) { q15 out[128]; while(n) { int k=std::min(n,64); e.render(out,k); n-=k; } }
static void ticks(Engine& e,int n=1) { frames(e,n*CONTROL_INTERVAL); }
static SigilConfig modern(SigilShape shape=SigilShape::Sine) { SigilConfig c; c.legacy=false;c.shape=shape;return c; }
static uint32_t increment(unsigned mhz) {return uint32_t((uint64_t(mhz)<<32)/1000000);}
static void preview_helper() {
    auto e=std::make_unique<Engine>(); q15 guarded[130];
    for(int legacy=0;legacy<2;++legacy)for(int shape=0;shape<(legacy?1:5);++shape)
    for(SigilMode mode:{SigilMode::Loop,SigilMode::OneShot,SigilMode::Steps,SigilMode::Follow}) {
        auto c=modern(SigilShape(shape));c.legacy=legacy;c.unipolar=!legacy;c.phase_degrees=211;
        CHECK(e->set_sigil_config(0,c) || sigil_config_equal(e->sigil_config[0],c));
        auto& s=e->sigils[0];s.mode=mode;s.running=s.follow_active=s.drawn=true;s.depth=-19000;
        for(int i=0;i<128;++i)s.table[i]=q15(i*500-32768);
        for(int count:{1,17,128}) {
            std::fill(std::begin(guarded),std::end(guarded),q15(1111));
            const Sigil before=s;allocations=0;watching=true;
            const bool periodic=e->capture_sigil_wave(0,guarded+1,count);watching=false;
            CHECK(periodic && allocations==0 && !std::memcmp(&before,&s,sizeof s));
            CHECK(guarded[0]==1111 && guarded[count+1]==1111);
            for(int p=0;p<count;++p) {
                uint32_t phase=count==1?0:uint32_t(uint64_t(p)*UINT32_MAX/(count-1));
                if(legacy){Sigil oracle=s;oracle.phase=phase;oracle.phase_inc=0;CHECK(guarded[p+1]==oracle.step());continue;}
                if(mode==SigilMode::Steps)phase=(phase&0xf0000000u)+0x08000000u;
                int32_t raw;
                if(shape){raw=sigil_wave_sample(SigilShape(shape),phase);}
                else {const unsigned pos=phase>>25,next=(pos==127&&mode==SigilMode::OneShot)?pos:(pos+1)%128;
                    raw=mode==SigilMode::Steps?s.table[pos]:trackr::fx::lerp_q15(s.table[pos],s.table[next],uint16_t(phase>>9));}
                raw=std::max(-32767,int(raw));raw=(raw+32767)/2;
                CHECK(guarded[p+1]==raw*s.depth/32767);
            }
        }
    }
    std::fill(std::begin(guarded),std::end(guarded),q15(1234));
    CHECK(!e->capture_sigil_wave(0,nullptr,128) && !e->capture_sigil_wave(0,guarded,0) && !e->capture_sigil_wave(0,guarded,129));
    for(q15 v:guarded)CHECK(v==1234);
    CHECK(!e->capture_sigil_wave(-1,guarded+1,128));for(int i=1;i<=128;++i)CHECK(guarded[i]==0);CHECK(guarded[0]==1234&&guarded[129]==1234);
    for(int reason=0;reason<8;++reason){
        auto c=modern(SigilShape::Sine);auto&s=e->sigils[0];s.mode=SigilMode::Loop;s.drawn=s.running=s.follow_active=true;
        if(reason==0)c.shape=SigilShape::Random;
        if(reason==1)c.shape=SigilShape::Smooth;
        if(reason==2)s.mode=SigilMode::RandomStep;
        if(reason==3)s.mode=SigilMode::RandomSmooth;
        if(reason==4)c.enabled=false;
        if(reason==5){c.shape=SigilShape::Drawn;s.drawn=false;}
        if(reason==6){s.mode=SigilMode::Follow;s.follow_active=false;}
        if(reason==7){s.mode=SigilMode::OneShot;s.running=false;}
        e->set_sigil_config(0,c);s.value=-1234;const Sigil before=s;
        CHECK(!e->capture_sigil_wave(0,guarded+1,128) && !std::memcmp(&before,&s,sizeof s));
        for(int i=1;i<=128;++i)CHECK(guarded[i]==(reason<4?-1234:0));
    }
    auto twin=std::make_unique<Engine>();auto c=modern(SigilShape::Random);c.rate_millihz=20000;
    e->sigils[0].mode=SigilMode::Loop;e->set_sigil_config(0,c);twin->set_sigil_config(0,c);e->reset_sigil(0);twin->reset_sigil(0);
    e->sigils[0].depth=twin->sigils[0].depth=32767;
    for(int i=0;i<200;++i){CHECK(!e->capture_sigil_wave(0,guarded+1,128));ticks(*e);ticks(*twin);CHECK(e->sigils[0].value==twin->sigils[0].value&&e->sigils[0].phase==twin->sigils[0].phase);}
}
static void waves_config() {
    static_assert(int(ModSource::EnvFollow)==5 && int(ModSource::SigilE)==6 && int(ModSource::SigilH)==9 && NUM_SIGILS==8,"stable ids");
    for(int i=0;i<8;++i)CHECK(mod_source_sigil(sigil_mod_source(i))==i);
    CHECK(sigil_mod_source(-1)==ModSource::None && sigil_mod_source(8)==ModSource::None);
    CHECK(mod_source_sigil(ModSource::None)==-1 && mod_source_sigil(ModSource::EnvFollow)==-1 && mod_source_sigil(ModSource::Count)==-1);
    for(unsigned i=0;i<4096;++i) {
        uint32_t phase=i*0x100000u; double t=double(i)/4096, tri=t<.25?4*t:t<.75?2-4*t:4*t-4;
        CHECK(std::abs(int(sigil_wave_sample(SigilShape::Sine,phase))-int(std::lround(32767*std::sin(6.283185307179586*t))))<=4);
        CHECK(std::abs(int(sigil_wave_sample(SigilShape::Triangle,phase))-int(std::lround(32767*tri)))<=1);
        CHECK(sigil_wave_sample(SigilShape::Saw,phase)==int(std::floor(t*65534))-32767);
        CHECK(sigil_wave_sample(SigilShape::Square,phase)==(i<2048?32767:-32767));
        CHECK(sigil_wave_sample(SigilShape::Random,phase)==0 && sigil_wave_sample(SigilShape::Smooth,phase)==0);
    }
    auto e=std::make_unique<Engine>(); auto c=modern(); e->sigils[0].set_point(12,-12345);
    CHECK(e->set_sigil_config(0,c) && !e->set_sigil_config(0,c));
    CHECK(!e->set_sigil_config(-1,c) && !e->set_sigil_config(8,c));
    for(int which=0;which<6;++which) {
        auto bad=c;
        if(which==0)bad.rate_millihz=49;
        if(which==1)bad.rate_millihz=20001;
        if(which==2)bad.phase_degrees=360;
        if(which==3)bad.shape=SigilShape::Count;
        if(which==4)bad.restart=SigilRestart::Count;
        if(which==5)bad.legacy=true;
        CHECK(!e->set_sigil_config(0,bad) && sigil_config_equal(e->sigil_config[0],c));
    }
    c.shape=SigilShape::Square;e->set_sigil_config(0,c);c.shape=SigilShape::Drawn;e->set_sigil_config(0,c);
    CHECK(e->sigils[0].drawn && e->sigils[0].table[12]==-12345);
    preview_helper();
    std::puts("ok 1: independent periodic oracles/config/drawing + const bounded exact waveform preview and RNG isolation");
}
static void clocks_routes_reset() {
    auto e=std::make_unique<Engine>();auto c=modern();c.phase_degrees=90;e->set_sigil_config(0,c);
    CHECK(e->sigils[0].phase==0x40000000u && e->sigils[0].value==32767);
    ticks(*e,7);CHECK(!e->transport_playing && e->sigils[0].phase==0x40000000u+7*increment(1000));
    c.phase_degrees=180;e->set_sigil_config(0,c);CHECK(e->sigils[0].phase==0x80000000u+7*increment(1000));
    e->reset_sigil(0);CHECK(e->sigils[0].phase==0x80000000u && e->sigils[0].value==0);
    c.restart=SigilRestart::Transport;e->set_sigil_config(0,c);ticks(*e,9);e->transport_play();CHECK(e->sigils[0].phase==0x80000000u);
    ticks(*e,5);uint32_t before=e->sigils[0].phase;e->transport_play();CHECK(e->sigils[0].phase==before);
    e->transport_stop();ticks(*e,5);CHECK(e->sigils[0].phase==before+5*increment(1000));e->transport_play();CHECK(e->sigils[0].phase==0x80000000u);
    c.restart=SigilRestart::Free;e->set_sigil_config(0,c);ticks(*e,3);before=e->sigils[0].phase;e->transport_stop();e->transport_play();CHECK(e->sigils[0].phase==before);
    e->set_bpm(137);e->set_sigil_sync(0,true);constexpr int num[6]={1,1,1,2,4,8},den[6]={4,2,1,1,1,1};
    for(int d=0;d<6;++d){e->set_sigil_div(0,d);ticks(*e);CHECK(e->sigils[0].phase_inc==uint32_t((uint64_t(137)*num[d]<<32)/(60000u*den[d])));}
    e->set_sigil_config(7,modern(SigilShape::Square));
    CHECK(e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilH,37));ticks(*e,10);CHECK(e->effective_bpm_q16()>137*65536);
    int32_t previous_tempo=e->effective_bpm_q16();ticks(*e);CHECK(e->sigils[0].phase_inc==uint32_t((uint64_t(previous_tempo)*8*65536)/60000));
    CHECK(!e->set_clock_mod(ClockTarget::Tempo,ModSource::EnvFollow,20));
    for(int dst=0;dst<8;++dst)for(int src=0;src<8;++src){
        auto old=e->sigil_mods[dst];e->set_sigil_mod(dst,sigil_mod_source(src),1234);
        CHECK(e->sigil_mods[dst].src==(src<dst?sigil_mod_source(src):old.src));
    }
    e->set_sigil_config(4,modern(SigilShape::Square));e->set_sigil_config(5,modern());
    e->set_sigil_mod(4,ModSource::None,0);e->set_sigil_mod(5,ModSource::SigilE,12000);ticks(*e);
    CHECK(e->sigils[5].phase_inc>increment(1000));e->set_sigil_sync(5,true);ticks(*e);
    CHECK(e->sigils[5].phase_inc<increment(4000)); // synced rate ignores the +7Hz bend
    auto keep=std::make_unique<Engine>();keep->set_sigil_rate(1,32767);const uint32_t old_rate=keep->sigils[1].phase_inc;
    auto converted=modern(SigilShape::Triangle);converted.legacy_rate=true;converted.rate_millihz=50;converted.phase_degrees=200;converted.unipolar=true;
    CHECK(keep->set_sigil_config(1,converted));ticks(*keep,10);CHECK(keep->sigils[1].phase_inc==old_rate && old_rate>increment(20000) && keep->sigil_rate(1)==32767);
    keep->sigils[0].drawn=true;for(auto&p:keep->sigils[0].table)p=12000;
    keep->set_sigil_rate(1,18000);keep->set_sigil_mod(1,ModSource::SigilA,8000);ticks(*keep);
    const int32_t mod=(int32_t(keep->sigils[0].value)*8000)>>15;Sigil rate_oracle;rate_oracle.set_rate_norm(q15(18000+mod),CONTROL_RATE);
    CHECK(keep->sigils[1].phase_inc==rate_oracle.phase_inc);
    converted.legacy_rate=false;converted.rate_millihz=1000;CHECK(keep->set_sigil_config(1,converted));ticks(*keep);
    CHECK(keep->sigils[1].phase_inc==increment(1000+int32_t(int64_t(mod)*19950/32767)));
    keep->set_sigil_mod(1,ModSource::None,0);CHECK(keep->sigils[1].phase_inc==increment(1000));
    std::puts("ok 2: clocks/restart/no cycles + independent legacy rate retained above20Hz and old modulation until explicit free edit");
}
static void random_depth_legacy() {
    auto e=std::make_unique<Engine>();auto c=modern(SigilShape::Random);c.rate_millihz=20000;e->set_sigil_config(4,c);
    q15 seq[104];const q15 initial=e->sigils[4].value;
    for(auto&v:seq){ticks(*e);v=e->sigils[4].value;}
    CHECK(seq[48]==initial && seq[50]!=initial);e->reset_sigil(4);
    for(q15 v:seq){ticks(*e);CHECK(v==e->sigils[4].value);}
    c.unipolar=true;e->set_sigil_config(4,c);e->reset_sigil(4);CHECK(e->sigils[4].value==(int(initial)+32767)/2);
    e->sigils[4].depth=-16384;ticks(*e);CHECK(e->sigils[4].value<0);
    c.unipolar=false;c.shape=SigilShape::Smooth;e->sigils[4].depth=32767;e->set_sigil_config(4,c);e->reset_sigil(4);
    CHECK(e->sigils[4].value==0);ticks(*e,25);CHECK(std::abs(int(e->sigils[4].value)-initial/2)<8);
    q15 middle=e->sigils[4].value;ticks(*e,10);CHECK(e->sigils[4].value!=middle && e->sigils[4].value!=initial);
    c.enabled=false;e->set_sigil_config(4,c);uint32_t phase=e->sigils[4].phase;ticks(*e,5);CHECK(e->sigils[4].value==0 && e->sigils[4].phase_inc==0 && e->sigils[4].phase==phase);
    // Unchanged legacy Sigil::step is the oracle for all six old modes, including
    // its historic RNG start and 32-bit smoothstep arithmetic. Modern shapes differ.
    for(int mode=0;mode<int(SigilMode::Count);++mode){
        auto old=std::make_unique<Engine>();Sigil reference;
        old->sigils[0].mode=reference.mode=SigilMode(mode);old->sigils[0].depth=reference.depth=-19000;
        for(int p=0;p<128;++p){old->sigils[0].set_point(p,q15((p-64)*400));reference.set_point(p,q15((p-64)*400));}
        old->set_sigil_rate(0,32767);reference.set_rate_norm(32767,CONTROL_RATE);
        for(int n=0;n<250;++n){ticks(*old);CHECK(old->sigils[0].value==reference.step() && old->sigils[0].phase==reference.phase);}
        old->set_sigil_rate(0,-32768);reference.set_rate_norm(-32768,CONTROL_RATE);
        CHECK(old->sigils[0].phase_inc==(mode==int(SigilMode::Follow)?0:reference.phase_inc));
    }
    auto modern_override=std::make_unique<Engine>();
    modern_override->sigils[0].mode=SigilMode::RandomSmooth;
    c=modern(SigilShape::Square);c.phase_degrees=99;c.unipolar=true;c.rate_millihz=9000;
    modern_override->set_sigil_config(0,c);
    Sigil mode_oracle;mode_oracle.mode=SigilMode::RandomSmooth;mode_oracle.phase_inc=increment(9000);
    for(int n=0;n<500;++n){ticks(*modern_override);CHECK(mode_oracle.step()==modern_override->sigils[0].value);}
    std::puts("ok 3: deterministic hold vs genuine smooth, signed depth/polarity, disabled, old six-mode oracle");
}
class Probe final:public Unit{public:int triggers=0;Probe(){num_params=PARAM_BANK_SIZE;for(auto&p:params){p.base=16000;p.bipolar=true;}}void process(q15*,int)override{}void trigger()override{++triggers;}};
static int target(const Param&p,q15 v){return std::max(p.bipolar?-32767:0,std::min(32767,int(p.control_source())+(p.mod_enabled?((int(v)*p.mod_depth)>>15):0)));}
static void realtime_follow() {
    auto e=std::make_unique<Engine>();for(int i=0;i<8;++i){auto c=modern(SigilShape::Square);c.phase_degrees=uint16_t(i*40);e->set_sigil_config(i,c);}
    for(int ch=0;ch<4;++ch)for(int sl=0;sl<6;++sl){auto*u=new Probe;delete e->replace_unit(ch,sl,u);for(int p=0;p<12;++p){auto&v=u->params[p];v.src=sigil_mod_source(p%8);v.mod_depth=p%2?-12000:12000;v.mod_enabled=p%3!=0;}}
    allocations=0;watching=true;
    for(int n=0;n<40;++n){ticks(*e);for(auto&ch:e->chains)for(auto*u:ch.units)for(int p=0;p<12;++p)CHECK(u->params[p].target==target(u->params[p],e->sigils[p%8].value));EngineUiSnapshot snap;e->capture_ui_snapshot(snap);e->reset_sigil(n%8);}
    watching=false;CHECK(allocations==0);
    auto&p=e->chains[0].units[0]->params[0];p.mod_enabled=true;CHECK(e->set_step_lock(0,0,0,0,1000));e->pattern.steps[0][0].flags=SeqStep::Trig;e->pattern.length[0]=2;
    e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();frames(*e,4000);CHECK(p.step_lock_override && p.base==16000 && p.target==target(p,e->sigils[0].value));
    p.mod_enabled=false;ticks(*e);ParamInspector out;e->capture_param_inspector(0,0,0,out);CHECK(!out.mod_enabled && out.source_value==0 && out.target==1000 && out.range_min==1000 && out.range_max==1000);
    auto f=std::make_unique<Engine>();auto& sample=trackr::synth::SampleBank::instance().slot(40);sample.channels=1;sample.logical_frames=UINT32_MAX;sample.data.assign(4096,12000);
    auto*play=new PlayUnit;play->sample_slot=40;play->params[PlayUnit::P_LOOP].base=0;play->params[PlayUnit::P_LOOP].snap();play->control_tick();delete f->replace_unit(2,0,play);
    auto c=modern(SigilShape::Saw);c.phase_degrees=111;f->set_sigil_config(7,c);f->sigils[7].mode=SigilMode::Follow;f->set_step_dest(7,2);f->trigger_chain(2);
    frames(*f,1024);CHECK(f->sigils[7].follow_active && f->sigils[7].phase>0 && f->sigils[7].phase_inc==0);
    CHECK(f->sigils[7].value==sigil_wave_sample(SigilShape::Saw,f->sigils[7].phase));
    uint32_t before=f->sigils[7].phase;frames(*f,1024);CHECK(f->sigils[7].phase>before);frames(*f,4097);CHECK(!f->sigils[7].follow_active && f->sigils[7].value==0);
    play->params[PlayUnit::P_SPEED].base=-8192;play->params[PlayUnit::P_SPEED].snap();play->control_tick();f->trigger_chain(2);frames(*f,1024);CHECK(f->sigils[7].follow_active && f->sigils[7].phase>0 && (play->visual_state()&PlayUnit::VisualReverse));sample={};
    auto g=std::make_unique<Engine>();auto*probe=new Probe;delete g->replace_unit(0,0,probe);
    auto step=modern(SigilShape::Square);step.rate_millihz=20000;g->set_sigil_config(0,step);g->sigils[0].mode=SigilMode::Steps;ticks(*g,49);CHECK(probe->triggers==7);
    g->sigils[0].mode=SigilMode::OneShot;g->reset_sigil(0);ticks(*g,51);CHECK(!g->sigils[0].running && g->sigils[0].phase==UINT32_MAX && g->sigils[0].value==-32767);ticks(*g);CHECK(g->sigils[0].value==0);
    step.unipolar=true;g->sigils[0].mode=SigilMode::Loop;g->sigils[0].depth=-16384;g->set_sigil_config(0,step);g->reset_sigil(0);
    auto& rail=probe->params[0];rail.src=ModSource::SigilA;rail.mod_depth=10000;ticks(*g);g->capture_param_inspector(0,0,0,out);CHECK(out.range_min==11000 && out.range_max==16000);
    std::puts("ok 4: 288 routes,8snapshot,locks/mute,zero allocations,PLAY follow,modern step triggers/shot/polarity rails");
}
using Bytes=std::vector<uint8_t>;
static Bytes readfile(const char* path){FILE*f=std::fopen(path,"rb");CHECK(f);std::fseek(f,0,SEEK_END);long n=std::ftell(f);CHECK(n>=0);std::rewind(f);Bytes b(size_t(n),0);CHECK(std::fread(b.data(),1,b.size(),f)==b.size());std::fclose(f);return b;}
static void writefile(const char* path,const Bytes& b){FILE*f=std::fopen(path,"wb");CHECK(f);CHECK(std::fwrite(b.data(),1,b.size(),f)==b.size());CHECK(!std::fclose(f));}
static uint32_t u32(const Bytes&b,size_t p){CHECK(p+4<=b.size());return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
static size_t chunk(const Bytes&b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+u32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
static Bytes without(Bytes b,const char* id){size_t p=chunk(b,id),len=8+u32(b,p+4);b.erase(b.begin()+p,b.begin()+p+len);return b;}
static void persistence() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    for(int i=0;i<8;++i){auto c=modern(SigilShape(i%7));c.legacy_rate=i%3==0;c.unipolar=i%2;c.restart=SigilRestart(i%2);c.rate_millihz=uint16_t(50+i*2800);c.phase_degrees=uint16_t(i*47);a->set_sigil_config(i,c);
        a->set_sigil_sync(i,i%2);a->set_sigil_div(i,i%6);a->set_step_dest(i,(i+1)%4);a->set_sigil_mod(i,i?sigil_mod_source(i-1):ModSource::EnvFollow,q15(100*i));
        a->sigils[i].mode=SigilMode(i%6);a->sigils[i].depth=q15(30000-i*7000);a->sigils[i].drawn=true;for(int j=0;j<128;++j)a->sigils[i].table[j]=i?q15(i*100+j):0;}
    a->set_clock_mod(ClockTarget::Tempo,ModSource::SigilH,7);
    for(int ch=0;ch<4;++ch)for(int sl=0;sl<6;++sl){auto*u=make_unit(UnitKind::Comp);CHECK(u);delete a->replace_unit(ch,sl,u);for(int p=0;p<u->num_params;++p){auto&v=u->params[p];v.src=ModSource(1+(ch+sl+p)%9);v.mod_depth=q15(-7000+p*100);v.mod_enabled=(p+sl)%3!=0;}}
    CHECK(patchio::save(*a,"roundtrip.snc") && patchio::load(*b,"roundtrip.snc"));
    for(int i=0;i<8;++i){CHECK(sigil_config_equal(a->sigil_config[i],b->sigil_config[i]));CHECK(a->sigils[i].mode==b->sigils[i].mode && a->sigils[i].depth==b->sigils[i].depth && a->sigils[i].drawn==b->sigils[i].drawn && !std::memcmp(a->sigils[i].table,b->sigils[i].table,sizeof a->sigils[i].table));CHECK(a->sigil_mods[i].src==b->sigil_mods[i].src && a->sigil_mods[i].depth==b->sigil_mods[i].depth && a->sigil_sync[i]==b->sigil_sync[i] && a->sigil_div[i]==b->sigil_div[i] && a->step_dest[i]==b->step_dest[i]);}
    CHECK(b->clock_mod(ClockTarget::Tempo).src==ModSource::SigilH);
    for(int ch=0;ch<4;++ch)for(int sl=0;sl<6;++sl)for(int p=0;p<9;++p){auto&x=a->chains[ch].units[sl]->params[p];auto&y=b->chains[ch].units[sl]->params[p];CHECK(x.src==y.src && x.mod_depth==y.mod_depth && x.mod_enabled==y.mod_enabled);}
    const Bytes saved=readfile("roundtrip.snc");size_t pos=chunk(saved,"USIG"),len=8+u32(saved,pos+4);CHECK(len==2253 && u32(saved,chunk(saved,"SIGL")+4)==1044 && u32(saved,chunk(saved,"ROUT")+4)==26);
    Bytes reordered=without(saved,"USIG");reordered.insert(reordered.begin()+5,saved.begin()+pos,saved.begin()+pos+len);writefile("first.snc",reordered);CHECK(patchio::load(*b,"first.snc"));
    const auto before=b->sigil_config[0];Unit*unit_before=b->chains[0].units[0];
    auto reject=[&](const Bytes&bad){writefile("bad.snc",bad);CHECK(!patchio::load(*b,"bad.snc"));CHECK(unit_before==b->chains[0].units[0] && sigil_config_equal(before,b->sigil_config[0]));};
    for(int off:{0,1,2,3,4,5,6,7,9,11,12,17,20,21,22,2198}){Bytes bad=saved;bad[pos+8+off]=255;reject(bad);}
    Bytes contradiction=saved;contradiction[chunk(saved,"SIGL")+9]^=1;reject(contradiction);
    Bytes invalid_source=saved;invalid_source[chunk(saved,"CORE")+8+5+3+2]=255;reject(invalid_source);
    for(const char*id:{"USIG","SIGL","ROUT","CORE"}){size_t p=chunk(saved,id),n=8+u32(saved,p+4);Bytes dup=saved;dup.insert(dup.end(),saved.begin()+p,saved.begin()+p+n);reject(dup);}
    Bytes truncated=saved;truncated.resize(pos+len-1);reject(truncated);Bytes length=saved;length[pos+4]--;reject(length);
    auto fresh=std::make_unique<Engine>();b->swap_patch_state(*fresh);for(int i=0;i<8;++i)CHECK(sigil_config_equal(b->sigil_config[i],SigilConfig{}) && b->sigils[i].value==0);CHECK(fresh->chains[0].units[0]==unit_before);
    ProjectSession session;session.mark_saved(3);fresh->reset_sigil(0);CHECK(!session.dirty());
    std::puts("ok 5: complete8 roundtrip/zero drawings/mute, four-wide mirror, reordered USIG, malformed transactional rejects and swap");
}
static Bytes pbay_chunk(int shape=0) {
    Bytes p={'P','B','A','Y',85,0,0,0,1,4,4,6,12};
    for(int i=0;i<4;++i){unsigned rate=1000+1000*i,phase=45*i;p.push_back(uint8_t(i?1:0));p.push_back(uint8_t(shape));p.push_back(0);p.push_back(2);p.push_back(uint8_t(rate));p.push_back(uint8_t(rate>>8));p.push_back(uint8_t(phase));p.push_back(uint8_t(phase>>8));}
    p.insert(p.end(),48,0);p[8+37]=1;return p;
}
static void migration_pcm() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    for(Engine*e:{a.get(),b.get()}){delete e->replace_unit(0,0,make_unit(UnitKind::Osc));delete e->replace_unit(0,1,make_unit(UnitKind::Vca));for(int p=0;p<128;++p)e->sigils[0].set_point(p,q15((p-64)*400));auto&v=e->chains[0].units[0]->params[0];v.src=ModSource::SigilA;v.mod_depth=700;e->trigger_chain(0);}
    for(int i=4;i<8;++i)b->set_sigil_config(i,modern(SigilShape(i-2)));
    q15 x[128],y[128];bool audible=false;for(int n=0;n<200;++n){a->render(x,64);b->render(y,64);CHECK(!std::memcmp(x,y,sizeof x));for(q15 v:x)audible|=v!=0;}CHECK(audible);
    CHECK(patchio::save(*a,"old.snc"));Bytes old=without(readfile("old.snc"),"USIG");writefile("absent.snc",old);CHECK(patchio::load(*b,"absent.snc"));for(int i=0;i<8;++i)CHECK(sigil_config_equal(b->sigil_config[i],SigilConfig{}));
    auto c=std::make_unique<Engine>();CHECK(patchio::load(*c,"absent.snc"));b->trigger_chain(0);c->trigger_chain(0);for(int n=0;n<100;++n){b->render(x,64);c->render(y,64);CHECK(!std::memcmp(x,y,sizeof x));}
    // PBAY independent binary fixtures; exact former wave/seed values on e..h.
    for(int shape=0;shape<5;++shape){Bytes p=pbay_chunk(shape),data=old;data.insert(data.begin()+5,p.begin(),p.end());writefile("pbay.snc",data);CHECK(patchio::load(*b,"pbay.snc"));
        CHECK(!b->sigil_config[4].enabled && !b->chains[0].units[0]->params[0].mod_enabled);
        for(int i=1;i<4;++i){CHECK(b->sigil_config[i+4].shape==SigilShape(shape+1) && !b->sigil_config[i+4].legacy && !b->sigil_config[i+4].legacy_rate);uint32_t state=0x4c464f31u+uint32_t(i)*0x9e3779b9u;state=state*1664525u+1013904223u;int held=int(state>>16)-32768;if(held==-32768)held=-32767;CHECK(b->sigils[i+4].value==(shape==4?held:sigil_wave_sample(SigilShape(shape+1),sigil_phase_offset(uint16_t(i*45)))));}
        ticks(*b,100);CHECK(b->sigils[4].value==0);
        for(int off:{0,1,2,3,4,5,6,7,8,10,12,38}){Bytes bad=data;bad[5+8+off]=255;writefile("bad-pbay.snc",bad);CHECK(!patchio::load(*b,"bad-pbay.snc"));}
        Bytes dual=readfile("old.snc");dual.insert(dual.end(),p.begin(),p.end());writefile("dual.snc",dual);CHECK(!patchio::load(*b,"dual.snc"));
    }
    Bytes legacy={'S','N','C','1',1};auto append16=[&](int v){legacy.push_back(uint8_t(v));legacy.push_back(uint8_t(v>>8));};
    for(int ch=0;ch<4;++ch){append16(32767);legacy.push_back(0);for(int sl=0;sl<6;++sl){legacy.push_back(uint8_t(ch==0&&sl==0?UnitKind::Vca:UnitKind::Empty));legacy.push_back(0);legacy.push_back(0);for(int p=0;p<6;++p){append16(p?0:16000);legacy.push_back(p?255:1);append16(p?0:2000);}}}
    size_t sig=chunk(old,"SIGL");legacy.insert(legacy.end(),old.begin()+sig+8,old.begin()+sig+8+u32(old,sig+4));legacy.insert(legacy.end(),12,0);writefile("legacy.snc",legacy);CHECK(patchio::load(*c,"legacy.snc"));CHECK(c->chains[0].units[0]->params[0].src==ModSource::SigilA && c->chains[0].units[0]->params[1].src==ModSource::None);for(int i=4;i<8;++i)CHECK(!c->sigils[i].drawn && c->sigils[i].value==0);
    auto e=std::make_unique<Engine>(),f=std::make_unique<Engine>();auto cfg=modern(SigilShape::Smooth);cfg.rate_millihz=17234;e->set_sigil_config(0,cfg);f->set_sigil_config(0,cfg);frames(*e,7913);for(int i=0;i<7913;++i)frames(*f,1);CHECK(e->sigils[0].phase==f->sigils[0].phase && e->sigils[0].value==f->sigils[0].value);
    std::puts("ok 6: old a-d PCM twins, absent extension/SNC1, PBAY five-shape migration/seeds/inactive/mute, rejects and partitions");
}
int main(){waves_config();clocks_routes_reset();random_depth_legacy();realtime_follow();persistence();migration_pcm();std::puts("universal sigils core: 6/6 passed");}
