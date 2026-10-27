// Fresh core-only module RAND proof. No old suite includes or UI dependency.
#include "engine/engine.h"
#include "engine/patch_io.h"
#include "engine/delay_time.h"
#include "engine/vca_timing.h"
#include "engine/play_mode.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>
using namespace seance;
namespace fs=std::filesystem;
static uint64_t allocations=0,io_calls=0;static bool watched=false;
void* operator new(std::size_t n) {if(watched)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept{return ::operator new(n,std::nothrow);}
void operator delete(void* p) noexcept {std::free(p);}void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
extern "C" FILE* __real_fopen(const char*,const char*);
extern "C" FILE* __wrap_fopen(const char* a,const char* b){if(watched)++io_calls;return __real_fopen(a,b);}
namespace {
uint64_t checks=0,nonzero=0;unsigned groups=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"fail line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Watch {Watch(){CHECK(!watched);watched=true;}~Watch(){watched=false;CHECK(!allocations&&!io_calls);}};
using Bytes=std::vector<uint8_t>;using Bases=std::array<q15,PARAM_BANK_SIZE>;
fs::path folder;
Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);CHECK(f.good());return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
void pcm(const fs::path& p,const std::vector<q15>& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size()*2);CHECK(f.good());}
Bases bases(const Unit& u){Bases a;for(int p=0;p<PARAM_BANK_SIZE;++p)a[p]=u.params[p].base;return a;}
void install(Engine& e,int c,int s,Unit* u){CHECK(u);delete e.replace_unit(c,s,u);}
void set(Unit& u,int p,int v){u.params[p].base=q15(v);u.params[p].snap();}
void sample_bank(){auto& bank=trackr::synth::SampleBank::instance();for(int s=0;s<64;++s)bank.clear_slot(s);
    trackr::synth::Sample sample;sample.data.resize(32768);for(size_t i=0;i<sample.data.size();++i)sample.data[i]=q15(int(i*271%42001)-21000);
    sample.chops[0]=0;sample.chops[4]=4096;sample.slice_reverse_mask=16;sample.slice_speed_q8[4]=384;
    CHECK(bank.replace_sample(0,std::move(sample),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);}
Unit* variant(UnitKind kind,int v){Unit* u=make_unit(kind);CHECK(u);
    const bool old=v==1;
    switch(kind){
    case UnitKind::Delay:static_cast<DelayUnit*>(u)->set_modern(!old);if(v==2)set(*u,DelayUnit::P_SYNC,32767);break;
    case UnitKind::Reverb:CHECK(static_cast<ReverbUnit*>(u)->set_modern(!old));break;
    case UnitKind::Noise:static_cast<NoiseUnit*>(u)->set_modern(!old);break;
    case UnitKind::Motor:static_cast<MotorUnit*>(u)->set_modern(!old);if(v==2)set(*u,MotorUnit::P_MODE,32767);break;
    case UnitKind::Fold:static_cast<FoldUnit*>(u)->set_modern(!old);break;
    case UnitKind::Ring:static_cast<RingUnit*>(u)->set_modern(!old);if(v==2)set(*u,RingUnit::P_MODE,32767);break;
    case UnitKind::Bitcrush:static_cast<BitUnit*>(u)->set_modern(!old);break;
    case UnitKind::Strain:static_cast<StrainUnit*>(u)->set_modern(!old);break;
    case UnitKind::Tilt:static_cast<TiltUnit*>(u)->set_revision(old?TiltUnit::Revision::Legacy:v==2?TiltUnit::Revision::Clear:TiltUnit::Revision::Extended);break;
    case UnitKind::Vca:{auto& a=*static_cast<VcaUnit*>(u);if(old)a.set_flexible(false);if(v==2)a.set_flexible(true);if(v==3)set(*u,VcaUnit::P_SYNC,32767);break;}
    case UnitKind::SamplePlayer:{auto& a=*static_cast<PlayUnit*>(u);a.set_modern(!old);if(v==2)a.set_pitch_mode(PlayUnit::PitchMode::Normal);if(v==3)a.pitch_quantize=true;break;}
    case UnitKind::Grain:{auto& a=*static_cast<GrainUnit*>(u);a.clean=!old;a.clock_sync=v==2;break;}
    case UnitKind::Osc:{auto& a=*static_cast<OscUnit*>(u);a.clean=!old;a.shape_morph=!old;a.pitch_quantize=v==2;a.wave=v==3?OscUnit::Wave::Pulse:OscUnit::Wave::Sine;break;}
    case UnitKind::Fm:{auto& a=*static_cast<FmUnit*>(u);a.set_extended(!old);a.drone=v==2;break;}
    case UnitKind::Ghost:static_cast<GhostUnit*>(u)->long_tail=!old;break;
    case UnitKind::Comp:if(v==2)set(*u,CompUnit::P_CLOCK,32767);break;
    case UnitKind::Fract:if(v==1)set(*u,FractUnit::P_SYNC,0);break;
    case UnitKind::Stranular:if(v==2)set(*u,StranularUnit::P_SYNC,32767);break;
    case UnitKind::Inst:if(v==2)set(*u,InstUnit::P_INSTRUMENT,InstUnit::instrument_value(5));break;
    default:break;}
    return u;
}
void frozen(){fs::create_directories(folder/"frozen");sample_bank();
    for(int k=1;k<int(UnitKind::Count);++k)for(int v=0;v<2;++v){auto e=std::make_unique<Engine>();Unit* u=variant(UnitKind(k),v);install(*e,0,1,u);
        if(!u->sequencer_properties().source && u->kind!=UnitKind::Arp){auto* source=new OscUnit;source->clean=false;set(*source,OscUnit::P_GAIN,12000);install(*e,0,0,source);}
        if(u->kind==UnitKind::Arp)install(*e,0,2,new InstUnit);
        e->sigils[0].mode=SigilMode::RandomStep;e->sigils[1].mode=SigilMode::RandomSmooth;
        e->set_sigil_rate(0,31000);e->set_sigil_rate(1,24000);
        e->pattern.length[0]=16;for(int s=0;s<16;++s){auto& st=e->pattern.steps[0][s];st.flags=SeqStep::Trig;st.set_condition(SeqStep::Condition::Half);st.set_retrig_count(2);}
        e->set_sequencer_enabled(true);e->transport_play();e->trigger_chain(0);
        std::vector<q15> out(65536);for(int pos=0;pos<32768;pos+=32){q15 mic[32];for(int i=0;i<32;++i)mic[i]=q15((pos+i)*197%28000-14000);MicUnit::push(mic,32);{Watch guard;e->render(out.data()+pos*2,32);}}
        for(q15 x:out)nonzero+=x!=0;
        auto name=std::to_string(k)+"-"+std::to_string(v);pcm(folder/"frozen"/(name+".pcm"),out);
        CHECK(patchio::save(*e,(folder/"frozen"/(name+".snc")).c_str()));
#ifndef MODULE_RAND_BASELINE
        CHECK(e->module_random_revision()==0);
#endif
    }
    CHECK(nonzero>100000);++groups;std::puts("frozen: 56 kind/variant conditional-sequencer PCM + ordinary SNC traces");
}
#ifndef MODULE_RAND_BASELINE
// Independent exclusion oracle: protects commands, inactive revisions and source ids.
bool protected_param(const Unit& u,int p){if(p>=u.num_params)return true;
    switch(u.kind){
    case UnitKind::SamplePlayer:return p!=PlayUnit::P_SPEED && p!=PlayUnit::P_GAIN && p!=PlayUnit::P_NOTE && p!=PlayUnit::P_GATE;
    case UnitKind::Grain:return p==GrainUnit::P_POS || (p==GrainUnit::P_SCAN&&!static_cast<const GrainUnit&>(u).clean) || (p==GrainUnit::P_DENS&&static_cast<const GrainUnit&>(u).clock_sync);
    case UnitKind::Osc:return (p==OscUnit::P_SHAPE&&!static_cast<const OscUnit&>(u).shape_morph) || (p==OscUnit::P_PW&&!static_cast<const OscUnit&>(u).shape_morph&&static_cast<const OscUnit&>(u).wave!=OscUnit::Wave::Pulse);
    case UnitKind::Vca:return p>=VcaUnit::P_SYNC;
    case UnitKind::Delay:return p==DelayUnit::P_HOLD||p==DelayUnit::P_SYNC||p==DelayUnit::P_MODE;
    case UnitKind::Fold:return p==FoldUnit::P_MODE&&!static_cast<const FoldUnit&>(u).modern();
    case UnitKind::MicIn:return p==MicUnit::P_MON;
    case UnitKind::Recorder:return p!=RecUnit::P_LEN;
    case UnitKind::Bitcrush:return p==BitUnit::P_COMP;
    case UnitKind::Reverb:return p==ReverbUnit::P_FREEZE;
    case UnitKind::Ghost:return p==GhostUnit::P_FREEZE;
    case UnitKind::Stranular:return p==StranularUnit::P_FREEZE||p>=StranularUnit::P_HOLD;
    case UnitKind::Ring:return p==RingUnit::P_MODE;
    case UnitKind::Noise:return p==NoiseUnit::P_MODE;
    case UnitKind::Motor:return p==MotorUnit::P_SOURCE||p==MotorUnit::P_SYNC||p==MotorUnit::P_MODE;
    case UnitKind::Fract:return p>=FractUnit::P_SYNC;
    case UnitKind::Comp:return p==CompUnit::P_CLOCK;
    case UnitKind::Arp:return p==ArpUnit::P_RESET||p==ArpUnit::P_PLAY_MODE||(p>=ArpUnit::P_NOTE1&&ArpUnit::note_semitones(u.params[p].base)==ArpUnit::NOTE_OFF);
    case UnitKind::Glitch:return p==GlitchUnit::P_KEEP;
    case UnitKind::Inst:return p==InstUnit::P_INSTRUMENT||(InstUnit::instrument_index(u.params[0].base)==5&&(p==InstUnit::P_NOTE||p==InstUnit::P_LENGTH));
    default:return false;}
}
void canonical(const Unit& u,int p,q15 value){
    if(u.kind==UnitKind::Arp){if(p==ArpUnit::P_RATE)CHECK(value==ArpUnit::rate_value(ArpUnit::rate_index(value)));
        if(p==ArpUnit::P_GATE)CHECK(value==ArpUnit::gate_value(ArpUnit::gate_percent(value)));
        if(p==ArpUnit::P_ORDER)CHECK(value==ArpUnit::order_value(ArpUnit::order_index(value)));
        if(p==ArpUnit::P_OCTAVES)CHECK(value==ArpUnit::octaves_value(ArpUnit::octaves(value)));
        if(p>=ArpUnit::P_NOTE1)CHECK(value==ArpUnit::note_value(ArpUnit::note_semitones(value)));}
    if(u.kind==UnitKind::Noise&&p==NoiseUnit::P_COLOUR)CHECK(value==(value*4/32768)*8192+4096);
    if(u.kind==UnitKind::Ring&&p==RingUnit::P_WAVE&&static_cast<const RingUnit&>(u).modern())CHECK(value==(RingUnit::wave_index(value)*32768+16384)/3);
    if(u.kind==UnitKind::Bitcrush&&p==BitUnit::P_RATE&&!static_cast<const BitUnit&>(u).modern()){const int hold_minus_one=(32767-value)*31/32767;CHECK(value==32767-(hold_minus_one*32767+30)/31);}
    if(u.kind==UnitKind::Tilt&&p==TiltUnit::P_RANGE&&!static_cast<const TiltUnit&>(u).extended()){
        if(static_cast<const TiltUnit&>(u).modern()||value!=0)CHECK(value==((value*24/32768)*32768+16384)/24);}
    if(u.kind==UnitKind::Vca&&p==VcaUnit::P_DUCK)CHECK(value==0||value==32767);
    if((u.kind==UnitKind::Fm&&p==FmUnit::P_PITCH)||(u.kind==UnitKind::Osc&&p==OscUnit::P_PITCH&&static_cast<const OscUnit&>(u).pitch_quantize))CHECK(value==musical_note::encode(musical_note::decode(value)));
    if(u.kind==UnitKind::Grain&&p==GrainUnit::P_VOICES)CHECK(value==q15(((GrainUnit::voice_limit(value)-1)*32767+3)/7));
    if(u.kind==UnitKind::Bitcrush&&p==BitUnit::P_BITS){const int count=static_cast<const BitUnit&>(u).modern()?16:13;CHECK(value==((value*count/32768)*32768+16384)/count);}
    if(u.kind==UnitKind::MicIn&&p==MicUnit::P_LOW_CUT)CHECK(value==(MicUnit::low_cut_mode(value)*32768+16384)/3);
    if(u.kind==UnitKind::Delay&&p==DelayUnit::P_DIV)CHECK(value==delay_time::division_value(delay_time::division(value)));
    if(u.kind==UnitKind::Delay&&p==DelayUnit::P_FEEL)CHECK(value==delay_time::feel_value(delay_time::feel(value)));
    if(u.kind==UnitKind::Comp&&p==CompUnit::P_RELEASE&&u.params[CompUnit::P_CLOCK].base>=16384)CHECK(value==CompUnit::release_value(CompUnit::release_index(value)));
    if(u.kind==UnitKind::Vca&&static_cast<const VcaUnit&>(u).modern()&&u.params[VcaUnit::P_SYNC].base>=16384&&(p==VcaUnit::P_ATTACK||p==VcaUnit::P_DECAY||p==VcaUnit::P_HOLD)){
        const bool hold=p==VcaUnit::P_HOLD;CHECK(value==vca_timing::division_value(vca_timing::division(value,hold),hold));}
    if(u.kind==UnitKind::Fract&&(p==FractUnit::P_LENGTH||(p==FractUnit::P_SIZE&&u.params[FractUnit::P_SYNC].base>16383)))CHECK(value==(value*7/32768*32768+16384)/7);
    if(u.kind==UnitKind::SamplePlayer&&p==PlayUnit::P_GATE)CHECK(value==play_mode::gate_value(play_mode::gate_index(value)));
}
void policy_and_undo(){std::vector<q15> trace,all_pcm;sample_bank();
    for(int k=1;k<int(UnitKind::Count);++k)for(int v=0;v<4;++v){auto e=std::make_unique<Engine>();Unit* u=variant(UnitKind(k),v);install(*e,0,0,u);
        auto untouched=variant(UnitKind(k),v);install(*e,1,4,untouched);const Bases other=bases(*untouched);
        for(int p=0;p<PARAM_BANK_SIZE;++p){auto& a=u->params[p];a.src=sigil_mod_source(p%8);a.mod_depth=q15(p*111-300);a.mod_enabled=p%2;
            a.temp=1100;a.temp_override=true;a.step_lock=2300;a.step_lock_override=true;a.audition=3900;a.audition_override=true;
            a.performance=4300;a.performance_override=true;a.slew_ready=true;a.effective=7300;a.target=8800;
            if(p>=u->num_params)a.base=q15(20000+p);}
        for(int pass=0;pass<32;++pass){Param before[PARAM_BANK_SIZE];std::memcpy(before,u->params,sizeof before);const Bases old=bases(*u);
            const auto revision=e->module_random_revision();CHECK(e->module_random_available(0,0));bool result;{Watch guard;result=e->randomize_module(0,0);}CHECK(result);CHECK(e->module_random_revision()==revision+1);
            CHECK(bases(*untouched)==other);CHECK(e->can_undo_module_randomize(0,0));
            for(int p=0;p<PARAM_BANK_SIZE;++p){q15 value=u->params[p].base;trace.push_back(value);
                if(protected_param(*u,p))CHECK(value==old[p]);
                if(value!=old[p]){CHECK(value>= (u->params[p].bipolar?-32767:0));canonical(*u,p,value);}
                Param now=u->params[p];now.base=before[p].base;CHECK(!std::memcmp(&now,&before[p],sizeof(Param)));}
            // One-step undo stores only the immediately previous actual result.
            if(pass%4==0){Watch guard;CHECK(e->undo_module_randomize(0,0));CHECK(bases(*u)==old);CHECK(!e->undo_module_randomize(0,0));}
        }
        CHECK(e->can_undo_module_randomize(0,0));u->params[0].base^=1;CHECK(!e->can_undo_module_randomize(0,0));CHECK(!e->undo_module_randomize(0,0));
        for(int p=0;p<PARAM_BANK_SIZE;++p){auto& a=u->params[p];a.src=ModSource::None;a.temp_override=a.step_lock_override=a.audition_override=a.performance_override=false;}
        if(!u->sequencer_properties().source&&u->kind!=UnitKind::Arp){auto* source=new OscUnit;source->clean=false;set(*source,OscUnit::P_GAIN,11000);CHECK(e->insert_unit(0,0,source)>=0);}
        if(u->kind==UnitKind::Arp)install(*e,0,1,new InstUnit);
        e->transport_play();std::vector<q15> sound(8192);
        for(int pos=0;pos<4096;pos+=32){if(pos%512==0)e->trigger_chain(0);q15 mic[32];for(int i=0;i<32;++i)mic[i]=q15((pos+i)*113%22000-11000);MicUnit::push(mic,32);Watch g;e->render(sound.data()+pos*2,32);}
        for(q15 value:sound)nonzero+=value!=0;
        all_pcm.insert(all_pcm.end(),sound.begin(),sound.end());
    }
    pcm(folder/"rand-all-kinds.pcm",all_pcm);pcm(folder/"rand-bases.bin",trace);++groups;std::puts("policy: all28 kinds x4 variants x32 rerolls, bound/canonical/override/one-step undo guards");
}
void signal(Engine& e,int source,int wanted){auto& c=e.sigil_config[source];c.legacy=true;c.enabled=true;
    auto& s=e.sigils[source];s.mode=SigilMode::Loop;s.drawn=true;s.depth=32767;for(auto& point:s.table)point=q15(wanted>0?wanted+1:wanted);}
void tick(Engine& e,int count=1){q15 out[64];for(int i=0;i<count;++i){Watch guard;e.render(out,32);}}
void hysteresis(){for(int s=0;s<8;++s){auto e=std::make_unique<Engine>();install(*e,0,0,new InstUnit);auto* u=e->chains[0].units[0];
        CHECK(e->set_module_random_source(0,0,sigil_mod_source(s)));CHECK(!e->set_module_random_source(0,0,sigil_mod_source(s)));
        signal(*e,s,30000);tick(*e,8);CHECK(e->module_random_revision()==0); // initial high never fires
        signal(*e,s,8193);tick(*e);signal(*e,s,16384);tick(*e);CHECK(e->module_random_revision()==0);
        signal(*e,s,8192);tick(*e);signal(*e,s,16383);tick(*e);CHECK(e->module_random_revision()==0);
        signal(*e,s,16384);tick(*e);CHECK(e->sigils[s].value==16384);CHECK(e->module_random_revision()==1);
        const Bases after=bases(*u);signal(*e,s,30000);tick(*e,300);CHECK(bases(*u)==after);CHECK(e->module_random_revision()==1);
        signal(*e,s,-20000);tick(*e);CHECK(bases(*u)==after);signal(*e,s,30000);tick(*e);CHECK(e->module_random_revision()==2);
        CHECK(e->randomize_module(0,0));CHECK(e->module_random_revision()==3);tick(*e,5);CHECK(e->module_random_revision()==3);
        signal(*e,s,8192);tick(*e);e->chains[0].mute=true;signal(*e,s,30000);tick(*e);e->chains[0].mute=false;tick(*e);CHECK(e->module_random_revision()==3);
        signal(*e,s,0);tick(*e);u->bypass=true;signal(*e,s,30000);tick(*e);u->bypass=false;tick(*e);CHECK(e->module_random_revision()==3);
        u->bypass=true;CHECK(e->randomize_module(0,0));u->bypass=false;CHECK(e->module_random_revision()==4);
        e->sigil_config[s].enabled=false;tick(*e);e->sigil_config[s].enabled=true;tick(*e);CHECK(e->module_random_revision()==5);
        signal(*e,s,0);tick(*e);e->transport_reset();signal(*e,s,30000);tick(*e);CHECK(e->module_random_revision()==5);
        signal(*e,s,0);tick(*e);signal(*e,s,30000);tick(*e);CHECK(e->module_random_revision()==6);
        CHECK(e->set_module_random_source(0,0,ModSource::None));signal(*e,s,0);tick(*e);signal(*e,s,30000);tick(*e);CHECK(e->module_random_revision()==6);
        CHECK(!e->set_module_random_source(0,0,ModSource::EnvFollow));CHECK(!e->set_module_random_source(0,0,ModSource(255)));
        CHECK(!e->randomize_module(-1,0)&&!e->randomize_module(0,6)&&!e->randomize_module(3,3));
        // BASE only: highest-priority TEMP stays visible despite a new action edge.
        auto& p=u->params[InstUnit::P_LEVEL];p.temp=1000;p.temp_override=true;
        CHECK(e->set_module_random_source(0,0,sigil_mod_source(s)));signal(*e,s,0);tick(*e);signal(*e,s,30000);tick(*e);CHECK(p.target==1000);
    }++groups;std::puts("edges: all8 sources, exact thresholds, negative/disabled low, hold/off, reset, mute/bypass consume, manual high");}
void recording(){sample_bank();auto e=std::make_unique<Engine>();auto* rec=new RecUnit;rec->sample_slot=16;set(*rec,RecUnit::P_LEN,0);install(*e,0,1,rec);install(*e,0,0,new InstUnit);
    CHECK(rec->prepare_slot()==trackr::synth::SampleEditResult::Ok);CHECK(e->set_module_random_source(0,0,ModSource::SigilA));
    CHECK(e->randomize_module(0,0));const uint64_t rev=e->module_random_revision();
    auto blocked=[&](){CHECK(e->recording_active_or_pending());CHECK(!e->module_random_available(0,0));CHECK(!e->module_random_available(0,1));const Bases before=bases(*rec);signal(*e,0,0);tick(*e);signal(*e,0,30000);tick(*e);
        {Watch g;CHECK(!e->randomize_module(0,0));CHECK(!e->randomize_module(0,1));CHECK(!e->undo_module_randomize(0,0));CHECK(!e->set_module_random_source(0,0,ModSource::SigilB));}
        CHECK(bases(*rec)==before);CHECK(e->module_random_revision()==rev);};
    using Cmd=RecorderCommand;using R=trackr::synth::SampleEditResult;
    CHECK(e->request_recorder_command(0,1,Cmd::Start,LaunchQuantize::Immediate)==R::Ok);blocked();CHECK(rec->pause());blocked();rec->stop();tick(*e);CHECK(e->module_random_revision()==rev);
    rec=new RecUnit;rec->sample_slot=17;set(*rec,RecUnit::P_LEN,0);install(*e,0,1,rec);CHECK(rec->prepare_slot()==R::Ok);
    e->transport_play();tick(*e);CHECK(e->request_recorder_command(0,1,Cmd::Start,LaunchQuantize::Bar)==R::Ok);CHECK(!rec->writer_open());blocked();e->interrupt_recorders();tick(*e);CHECK(e->module_random_revision()==rev);
    CHECK(e->randomize_module(0,1));CHECK(!rec->writer_open());CHECK(rec->params[RecUnit::P_REC].base==0);
    ++groups;std::puts("recording: active, paused writer, pending quantized command block all rand edits and consume edges");}
void identity(){auto e=std::make_unique<Engine>();auto twin=std::make_unique<Engine>();
    for(auto* x:{e.get(),twin.get()}){install(*x,0,0,new InstUnit);install(*x,0,1,new FilterUnit);CHECK(x->set_module_random_source(0,0,ModSource::SigilH));CHECK(x->randomize_module(0,0));}
    Unit* original=e->chains[0].units[0];CHECK(e->move_unit_before_with_undo(0,0,UNITS_PER_CHAIN)==1);CHECK(e->chains[0].units[1]==original);CHECK(original->random_source==ModSource::SigilH);CHECK(e->can_undo_module_randomize(0,1));
    CHECK(e->undo_structural_edit());CHECK(e->chains[0].units[0]==original);CHECK(e->remove_unit_with_undo(0,0));CHECK(e->undo_structural_edit());CHECK(e->chains[0].units[0]==original);
    CHECK(e->randomize_module(0,0)&&twin->randomize_module(0,0));CHECK(bases(*original)==bases(*twin->chains[0].units[0])); // RNG stream followed identity
    std::unique_ptr<Unit> copy(clone_unit(original));CHECK(copy&&copy->random_source==original->random_source);CHECK(bases(*copy)==bases(*original));
    CHECK(e->insert_unit(0,1,copy.release())>=0);CHECK(!e->can_undo_module_randomize(0,1));signal(*e,7,30000);const auto rev=e->module_random_revision();tick(*e);CHECK(e->module_random_revision()==rev);
    signal(*e,7,0);tick(*e);CHECK(e->remove_unit_with_undo(0,0));signal(*e,7,30000);tick(*e);CHECK(e->undo_structural_edit());const auto restored=e->module_random_revision();tick(*e);CHECK(e->module_random_revision()==restored); // stale detached edge suppressed
    auto* replacement=new InstUnit;replacement->random_source=ModSource::SigilH;install(*e,0,0,replacement);tick(*e);CHECK(e->module_random_revision()==restored);
    ++groups;std::puts("identity: move/remove structural undo preserve route/rng/local undo; clone/insert/replace start disarmed");}
uint32_t r32(const Bytes& b,size_t p){CHECK(p+4<=b.size());return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;return b.size();}
Bytes mrnd(std::initializer_list<uint8_t> body){Bytes b={'M','R','N','D',uint8_t(body.size()),0,0,0};b.insert(b.end(),body);return b;}
void persistence(){sample_bank();auto e=std::make_unique<Engine>();for(int c=0;c<4;++c)for(int s=0;s<6;++s)install(*e,c,s,new InstUnit);
    auto path=folder/"plain.snc";CHECK(patchio::save(*e,path.c_str()));Bytes plain=read(path);CHECK(chunk(plain,"MRND")==plain.size());
    for(int c=0;c<4;++c)for(int s=0;s<6;++s)CHECK(e->set_module_random_source(c,s,sigil_mod_source((c*6+s)%8)));
    path=folder/"routes.snc";CHECK(patchio::save(*e,path.c_str()));Bytes route=read(path);size_t pos=chunk(route,"MRND");CHECK(pos<route.size());CHECK(r32(route,pos+4)==74);
    Bytes stripped=route;stripped.erase(stripped.begin()+pos,stripped.begin()+pos+82);CHECK(stripped==plain); // sole format delta
    auto loaded=std::make_unique<Engine>();CHECK(patchio::load(*loaded,path.c_str()));for(int c=0;c<4;++c)for(int s=0;s<6;++s)CHECK(loaded->chains[c].units[s]->random_source==sigil_mod_source((c*6+s)%8));
    auto again=folder/"routes-again.snc";CHECK(patchio::save(*loaded,again.c_str()));CHECK(read(again)==route);
    for(int s=0;s<8;++s)signal(*loaded,s,30000);
    tick(*loaded,4);CHECK(loaded->module_random_revision()==0);
    CHECK(patchio::load(*loaded,(folder/"plain.snc").c_str()));for(auto& c:loaded->chains)for(auto* u:c.units)CHECK(u->random_source==ModSource::None);
    // Out-of-order extension accepted, but never installed before occupied-slot validation.
    Bytes reordered(route.begin(),route.begin()+5);reordered.insert(reordered.end(),route.begin()+pos,route.begin()+pos+82);reordered.insert(reordered.end(),plain.begin()+5,plain.end());write(folder/"reordered.snc",reordered);CHECK(patchio::load(*loaded,(folder/"reordered.snc").c_str()));
    const std::vector<Bytes> bad={mrnd({2,1,0,0,1}),mrnd({1,0}),mrnd({1,25}),mrnd({1,1,4,0,1}),mrnd({1,1,0,6,1}),mrnd({1,1,0,0,0}),mrnd({1,1,0,0,5}),mrnd({1,1,0,0,10}),mrnd({1,1,0,0,255}),mrnd({1,2,0,0,1,0,0,2}),mrnd({1,1,0,0}),mrnd({1,1,0,0,1,4})};
    Unit* retained=loaded->chains[0].units[0];const Bases before=bases(*retained);unsigned n=0;
    for(const Bytes& badchunk:bad){Bytes file=plain;file.insert(file.end(),badchunk.begin(),badchunk.end());const auto p=folder/("bad-"+std::to_string(n++)+".snc");write(p,file);CHECK(!patchio::load(*loaded,p.c_str()));CHECK(loaded->chains[0].units[0]==retained&&bases(*retained)==before);}
    Bytes duplicate=route;duplicate.insert(duplicate.end(),route.begin()+pos,route.begin()+pos+82);write(folder/"duplicate.snc",duplicate);CHECK(!patchio::load(*loaded,(folder/"duplicate.snc").c_str()));
    auto empty=std::make_unique<Engine>();CHECK(patchio::save(*empty,(folder/"empty.snc").c_str()));Bytes no=read(folder/"empty.snc");Bytes one=mrnd({1,1,0,0,1});no.insert(no.end(),one.begin(),one.end());write(folder/"empty-route.snc",no);CHECK(!patchio::load(*loaded,(folder/"empty-route.snc").c_str()));
    route.pop_back();write(folder/"truncated.snc",route);CHECK(!patchio::load(*loaded,(folder/"truncated.snc").c_str()));
    // Save DTO owns the route snapshot; later engine mutations cannot leak into it.
    patchio::PatchDocument document;CHECK(patchio::capture(*e,document));CHECK(e->set_module_random_source(0,0,ModSource::None));CHECK(patchio::save(document,(folder/"dto.snc").c_str()));CHECK(read(folder/"dto.snc")==read(folder/"routes.snc"));
    ++groups;std::puts("persistence: absent/24routes/roundtrip/DTO/order, malformed/duplicate/empty/truncated transactional rejection");}
void deterministic_pcm(){auto a=std::make_unique<Engine>();auto b=std::make_unique<Engine>();
    for(auto* e:{a.get(),b.get()}){install(*e,0,0,new InstUnit);install(*e,0,1,new FilterUnit);install(*e,1,0,new BitUnit);e->chains[1].mute=true;CHECK(e->set_module_random_source(0,0,ModSource::SigilC));}
    std::vector<q15> out(65536),other(65536);for(int block=0;block<1024;++block){
        if(block%32==0){for(auto* e:{a.get(),b.get()}){CHECK(e->randomize_module(0,0));e->trigger_chain(0);}}
        // Independent per-unit streams: extra rerolls on unrelated unit never affect Inst.
        if(block%5==0)CHECK(a->randomize_module(1,0));
        signal(*a,2,block%16<8?0:30000);signal(*b,2,block%16<8?0:30000);
        {Watch guard;a->render(out.data()+block*64,32);b->render(other.data()+block*64,32);}
        CHECK(bases(*a->chains[0].units[0])==bases(*b->chains[0].units[0]));
    }
    CHECK(out==other);uint64_t audible=0;for(q15 v:out)audible+=v!=0;CHECK(audible>1000);nonzero+=audible;pcm(folder/"rand-pcm.pcm",out);
    ++groups;std::puts("pcm: 32768 deterministic stereo frames, independent unrelated rerolls, no RT allocation/IO");}
#endif
}
int main(int argc,char** argv){CHECK(argc==2);folder=argv[1];fs::create_directories(folder);frozen();
#ifndef MODULE_RAND_BASELINE
    policy_and_undo();hysteresis();recording();identity();persistence();deterministic_pcm();
    CHECK(groups==7);CHECK(allocations==0&&io_calls==0);
#endif
    std::printf("module rand core: %u groups, %llu checks, %llu nonzero words, %llu rt allocations, %llu rt fopen\n",groups,(unsigned long long)checks,(unsigned long long)nonzero,(unsigned long long)allocations,(unsigned long long)io_calls);return 0;}
