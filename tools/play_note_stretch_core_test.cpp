// Focused PLAY absolute-note/stretch core proof. No persistence, UI, media,
// hardware, or broad regression suite. Generated test PCM stays in memory.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#define private public
#include "engine/engine.h"
#undef private
using namespace seance;
using P = PlayUnit;
using Mode = P::PitchMode;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
static unsigned assertions = 0;
static size_t allocations = 0;
#if defined(__SANITIZE_ADDRESS__)
extern "C" void __sanitizer_malloc_hook(const volatile void*, std::size_t) { ++allocations; }
#endif
#define CHECK(x) do { ++assertions; if (!(x)) { std::fprintf(stderr,"play note/stretch failure line %d: %s\n",__LINE__,#x); std::abort(); } } while (0)
static void set(Unit& p, int param, q15 value) { p.params[param].base=value; p.params[param].snap(); }
static void fixture(int n=131072, int channels=1, bool tone=true) {
    Sample s; s.channels=uint8_t(channels); s.data.resize(size_t(n)*channels);
    constexpr double c4=261.6255653005986;
    for (int i=0;i<n;++i) for(int c=0;c<channels;++c)
        s.data[size_t(i)*channels+c]=q15(tone ? 12000*std::sin(i*6.283185307179586*c4/ENGINE_SR) : 5000+(i*37)%18000);
    CHECK(SampleBank::instance().replace_sample(0,std::move(s),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
}
static void prepare(P& p, Mode mode, int note=60, int speed=8192, bool loop=false) {
    p.set_pitch_mode(mode); set(p,P::P_NOTE,musical_note::encode(note)); set(p,P::P_SPEED,q15(speed));
    set(p,P::P_LOOP,loop?32767:0); set(p,P::P_MODE,0); p.control_tick(); p.trigger();
}
static void block(P& p,q15* out,int count) { const auto before=allocations; p.process(out,count); CHECK(allocations==before); }
static std::vector<q15> pcm(P& p,int n,bool odd=false) {
    std::vector<q15> out(n); int done=0;
    while(done<n) { const int count=std::min(n-done,odd ? 1+(done*37)%263 : 512); block(p,out.data()+done,count); done+=count; }
    return out;
}
static void advance(P& p,int n) { q15 out[512]; while(n) { const int count=std::min(n,512); block(p,out,count); n-=count; } }
static double frequency(const std::vector<q15>& out,int begin=2048) {
    int count=0; double first=0,last=0;
    for(int i=begin;i+1<int(out.size());++i) if(out[i]<=0&&out[i+1]>0) {
        const double position=i-double(out[i])/(int(out[i+1])-out[i]);
        if(!count) first=position;
        last=position; ++count;
    }
    CHECK(count>8); return (count-1)*ENGINE_SR/(last-first);
}
static void exact_lifetime(P& p,int frames) {
    CHECK(frames>=1); advance(p,frames-1); CHECK(p.playback_active());
    q15 output=123; block(p,&output,1); CHECK(p.playback_active());
    block(p,&output,1); CHECK(!p.playback_active()&&output==0);
}
static void helpers_clone() {
    static_assert(P::P_NOTE==11&&P::P_COUNT==12&&PARAM_BANK_SIZE==12,"append-only note");
    static_assert(sizeof(play_stretch::State)<=32,"no pcm scratch");
    for(int midi=24;midi<=96;++midi) CHECK(musical_note::decode(musical_note::encode(midi))==midi);
    CHECK(musical_note::decode(q15(-32768))==24);
    CHECK(musical_note::encode(-999)==0&&musical_note::encode(999)==32767);
    CHECK(musical_note::ratio_q16(-999)==2048&&musical_note::ratio_q16(999)==2097152);
    for(int n=-60;n<=60;++n) CHECK(musical_note::ratio_q16(n)==int(65536*std::pow(2.,n/12.)+.5));
    CHECK(P::projection_increment(Mode::Legacy,8192,musical_note::encode(60),false)==0);
    CHECK(P::projection_increment(Mode(255),8192,musical_note::encode(60),false)==0);
    for(int midi=24;midi<=96;++midi)for(int speed:{-32768,-1,0,2048,8192,32767})for(bool reverse:{false,true})for(int cut:{0,64,256,1024,65535}) {
        const int sign=((speed<0)!=reverse)?-1:1;
        CHECK(P::projection_increment(Mode::Normal,q15(speed),musical_note::encode(midi),reverse,uint16_t(cut))==sign*musical_note::ratio_q16(midi-60));
        const int tempo=std::max(16384,std::min(262144,int(int64_t(std::abs(speed))*8*Sample::cut_speed(uint16_t(cut))/256)));
        CHECK(P::projection_increment(Mode::Stretch,q15(speed),musical_note::encode(midi),reverse,uint16_t(cut))==sign*tempo);
    }
    char label[8]; musical_note::name(label,sizeof(label),60); CHECK(!std::strcmp(label,"c4"));
    musical_note::name(label,sizeof(label),61); CHECK(!std::strcmp(label,"c#4"));
    musical_note::name(label,sizeof(label),0); CHECK(!std::strcmp(label,"c-1"));
    char tiny[2]={'!','!'}; musical_note::name(tiny,sizeof(tiny),61); CHECK(tiny[1]==0); musical_note::name(nullptr,0,60);
    P p; CHECK(p.pitch_mode()==Mode::Legacy&&p.num_params==11&&p.actual_note()==60);
    CHECK(p.sequencer_properties().pitch_param==P::P_SPEED);
    for(Mode mode:{Mode::Normal,Mode::Stretch}) {
        prepare(p,mode,73); CHECK(p.num_params==12&&p.sequencer_properties().pitch_param==P::P_NOTE);
        auto& note=p.params[P::P_NOTE]; note.src=ModSource::SigilC; note.mod_depth=1234; note.mod_enabled=false;
        note.target=musical_note::encode(84); note.effective=musical_note::encode(65);
        note.step_lock_override=note.audition_override=note.performance_override=note.temp_override=true;
        std::unique_ptr<Unit> cloned(clone_unit(&p)); auto* copy=static_cast<P*>(cloned.get());
        CHECK(copy->pitch_mode()==mode&&copy->num_params==12&&!copy->playback_active()&&!copy->stretch_.ready);
        const auto& c=copy->params[P::P_NOTE]; CHECK(c.base==note.base&&c.target==note.base&&c.effective==note.base);
        CHECK(c.src==note.src&&c.mod_depth==1234&&!c.mod_enabled&&!c.step_lock_override&&!c.audition_override&&!c.performance_override&&!c.temp_override);
    }
    p.set_modern(false); CHECK(p.pitch_mode()==Mode::Legacy&&p.num_params==6&&!p.playback_active());
    p.set_pitch_mode(Mode::Stretch); CHECK(p.pitch_mode()==Mode::Legacy&&p.num_params==6);
    p.set_modern(true); CHECK(p.pitch_mode()==Mode::Legacy&&p.num_params==11);
    p.set_pitch_mode(Mode(255)); CHECK(p.pitch_mode()==Mode::Legacy);
    std::puts("ok helpers, immutable ratios, mode exposure/sequencer mapping, clone fresh note runtime");
}
static void normal_pitch() {
    fixture();
    for(int note:{48,60,72}) for(int speed:{1,2048,8192,32767,-1,-8192}) {
        P p; prepare(p,Mode::Normal,note,speed);
        const int expected=musical_note::ratio_q16(note-60)*(speed<0?-1:1);
        CHECK(p.inc_q16_==expected&&p.note_inc_q16_==expected);
        const double measured=frequency(pcm(p,16000));
        CHECK(std::abs(measured/(261.6255653005986*std::pow(2.,(note-60)/12.))-1)<.0004);
    }
    auto& s=SampleBank::instance().slot(0); s.chops[0]=1024; s.chops[1]=100000; s.root_note=1;
    for(int cut:{64,256,1024}) for(bool reverse:{false,true}) {
        s.slice_speed_q8[0]=uint16_t(cut); s.slice_reverse_mask=reverse?1:0;
        P p; p.set_pitch_mode(Mode::Normal); set(p,P::P_SLICE,2048); set(p,P::P_NOTE,musical_note::encode(72));
        set(p,P::P_SPEED,3000); p.pitch_quantize=true; p.control_tick(); p.trigger();
        CHECK(p.inc_q16_==(reverse?-131072:131072)); CHECK(p.win_start_==1024&&p.win_end_==100000);
        CHECK(std::abs(frequency(pcm(p,12000))/523.2511306011972-1)<.0004);
    }
    fixture(2048);
    for(int note:{48,60,72}) for(int sign:{-1,1}) {
        P p; prepare(p,Mode::Normal,note,8192*sign);
        exact_lifetime(p,note==48?4096:note==60?2048:1024);
    }
    P unslewed; prepare(unslewed,Mode::Normal); auto& note=unslewed.params[P::P_NOTE];
    note.target=musical_note::encode(72); note.effective=musical_note::encode(61); unslewed.control_tick();
    CHECK(unslewed.inc_q16_==131072&&unslewed.actual_note()==72);
    const auto untouched=note;
    unslewed.set_arp_pitch(12); CHECK(!std::memcmp(&untouched,&note,sizeof(note)));
    CHECK(unslewed.inc_q16_==262144&&unslewed.actual_note()==84);
    note.target=musical_note::encode(96); unslewed.set_arp_pitch(48); CHECK(unslewed.actual_note()==120&&unslewed.inc_q16_==2097152);
    note.target=0; unslewed.set_arp_pitch(-24); CHECK(unslewed.actual_note()==0&&unslewed.inc_q16_==2048);
    std::puts("ok normal c3/c4/c5 frequency and exact traversal; speed/cut/quantize/root metadata do not detune; immediate target and single arp offset");
}
static void stretch_pitch_lifetime() {
    fixture();
    double maximum_error=0;
    for(int note:{48,60,72}) for(int speed:{2048,4096,8192,16384,32767}) for(int sign:{-1,1}) {
        P p; prepare(p,Mode::Stretch,note,speed*sign);
        CHECK(p.inc_q16_==speed*8*sign&&p.note_inc_q16_==musical_note::ratio_q16(note-60)*sign);
        const double measured=frequency(pcm(p,16000));
        const double error=std::abs(measured/(261.6255653005986*std::pow(2.,(note-60)/12.))-1);
        maximum_error=std::max(error,maximum_error);
        if(error>=.008)std::fprintf(stderr,"frequency note=%d speed=%d: %.5f error %.5f\n",note,speed*sign,measured,error);
        CHECK(error<.008); // bounded waveform matcher; not a mastering-quality claim
    }
    fixture(2000);
    for(int note:{24,48,60,72,96}) for(int speed:{2048,4096,8192,16384,32767}) for(int sign:{-1,1}) {
        P p; prepare(p,Mode::Stretch,note,speed*sign);
        exact_lifetime(p,int((uint64_t(2000)*65536+speed*8-1)/(speed*8)));
    }
    auto& s=SampleBank::instance().slot(0); s.chops[0]=0;
    for(int speed:{0,1,-1,32767,-32767}) for(int cut:{64,1024}) {
        s.slice_speed_q8[0]=uint16_t(cut); P p; p.set_pitch_mode(Mode::Stretch);
        set(p,P::P_SPEED,q15(speed)); set(p,P::P_SLICE,2048); p.control_tick(); p.trigger();
        CHECK(std::abs(p.inc_q16_)>=16384&&std::abs(p.inc_q16_)<=262144);
        CHECK((p.inc_q16_<0)==(speed<0)&&std::abs(p.note_inc_q16_)==65536);
    }
    std::printf("ok stretch 30 pitch/direction/tempo tones (max measured error %.5f), 50 independent exact lifetimes, .25..4x clamp\n",maximum_error);
}
static PlayEditState edit_for(uint32_t source,uint32_t a,uint32_t b) {
    PlayEditState edit; edit.enabled=true; edit.source_frames=source; edit.start=a; edit.end=b; edit.loop_start=a; edit.loop_end=b; return edit;
}
static void bounds_stereo_short_fades() {
    for(Mode mode:{Mode::Normal,Mode::Stretch}) for(int channels:{1,2}) for(int span:{2,3,17,2048}) for(int sign:{-1,1}) {
        fixture(span+20,channels,false); auto& s=SampleBank::instance().slot(0);
        for(auto& value:s.data)value=-30000;
        for(int i=10;i<10+span;++i)for(int c=0;c<channels;++c)s.data[size_t(i)*channels+c]=q15(6000+c*2000);
        P p; p.set_pitch_mode(mode); auto edit=edit_for(span+20,10,10+span);
        CHECK(p.set_edit_state(edit,SampleBank::instance().generation(0)));
        prepare(p,mode,72,8192*sign,true); auto out=pcm(p,5000,true);
        // Poison outside the accepted trim would immediately produce a negative
        // sample. 2-frame loops must sustain actual PCM, not an all-zero fade.
        CHECK(std::all_of(out.begin(),out.end(),[](q15 x){return x>0;}));
        CHECK(p.playback_active()&&p.visual_source_frame()>=10&&p.visual_source_frame()<uint32_t(10+span));
        CHECK(out.back()>int(channels==1?5900:6900));
        s.chops[0]=10; s.chops[1]=10+span; set(p,P::P_SLICE,2048); p.control_tick(); p.trigger();
        CHECK(p.win_start_==10&&p.win_end_==uint32_t(10+span));
        out=pcm(p,4000); CHECK(std::all_of(out.begin(),out.end(),[](q15 x){return x>0;}));
        auto changed=edit; changed.start=0; changed.end=span+20; changed.loop_start=0; changed.loop_end=span+20;
        CHECK(p.set_edit_state(changed,SampleBank::instance().generation(0)));
        CHECK(p.win_start_==10&&p.win_end_==uint32_t(10+span)); // accepted voice never widens
    }
    // Both exact endpoints are really read, including native 2-frame mono/stereo
    // one-shots. Check the known trigger ramp rather than bypassing real DSP.
    for(Mode mode:{Mode::Normal,Mode::Stretch})for(int channels:{1,2})for(int sign:{-1,1}) {
        fixture(2,channels,false); auto& source=SampleBank::instance().slot(0);
        for(int c=0;c<channels;++c) {source.data[c]=q15(4000+c*2000);source.data[channels+c]=q15(12000+c*2000);}
        P p; prepare(p,mode,60,8192*sign); const auto out=pcm(p,3);
        for(int i=0;i<2;++i) {
            const int frame=sign>0?i:1-i;
            const int value=(frame?12000:4000)+(channels==2?1000:0);
            const int gain=(((32767*32767)>>15)*((i+1)*1024))>>15;
            CHECK(out[i]==q15(value*gain>>15));
        }
        CHECK(out[2]==0&&!p.playback_active());
    }
    fixture(4096,1,false); auto& s=SampleBank::instance().slot(0); std::fill(s.data.begin(),s.data.end(),q15(16000));
    for(Mode mode:{Mode::Normal,Mode::Stretch})for(int sign:{-1,1}) {
        P p; p.set_pitch_mode(mode); auto edit=edit_for(4096,100,4000); edit.fade_in=256; edit.fade_out=256;
        CHECK(p.set_edit_state(edit,SampleBank::instance().generation(0))); prepare(p,mode,60,8192*sign);
        const auto out=pcm(p,3900); CHECK(out.front()==0&&out.back()==0&&out[512]>15900&&out[3380]>15900);
    }
    // After the lead-in, neither head/matcher may read trim-only material outside
    // the authored sustain loop. The outer trim is intentionally poison.
    for(Mode mode:{Mode::Normal,Mode::Stretch})for(int sign:{-1,1}) {
        fixture(8192,2,false); auto& source=SampleBank::instance().slot(0); std::fill(source.data.begin(),source.data.end(),q15(-25000));
        std::fill(source.data.begin()+2048*2,source.data.begin()+6144*2,q15(10000));
        P p; p.set_pitch_mode(mode); auto edit=edit_for(8192,0,8192); edit.loop_start=2048; edit.loop_end=6144; edit.crossfade=128;
        CHECK(p.set_edit_state(edit,SampleBank::instance().generation(0))); prepare(p,mode,48,16384*sign,true);
        advance(p,20000); CHECK(p.loop_entered_); const auto out=pcm(p,15000,true);
        CHECK(std::all_of(out.begin(),out.end(),[](q15 value){return value>9900;}));
        // Stop on an unconsumed edge, then reverse live. No renewed trim attack
        // or cursor escape is allowed after the sustain loop has been entered.
        int attempts=0; q15 value=0;
        while(sign>0?p.pos_<6144:p.pos_>=2048) {block(p,&value,1);CHECK(++attempts<10000);}
        set(p,P::P_SPEED,q15(-16384*sign));p.control_tick();block(p,&value,1);
        CHECK(value>9900&&p.visual_source_frame()>=2048&&p.visual_source_frame()<6144&&p.playback_active());
    }
    for(Mode mode:{Mode::Normal,Mode::Stretch}) {
        fixture(2000); P p; p.set_pitch_mode(mode); set(p,P::P_SLICE,32767); p.control_tick(); p.trigger();
        CHECK(!p.playback_active()); // nonexistent slice never becomes whole source
        CHECK(!p.preview_frames(0,2001,false,SampleBank::instance().generation(0)));
        CHECK(!p.preview_frames(0,2,false,SampleBank::instance().generation(0)+1));
        set(p,P::P_SPEED,-1); CHECK(p.preview_frames(10,12,false,SampleBank::instance().generation(0)));
        CHECK(p.pos_==11&&p.inc_q16_<0); // sign is not lost to legacy tiny-speed floor
    }
    fixture(16,2,false);
    for(Mode mode:{Mode::Normal,Mode::Stretch})for(bool upper:{false,true}) {
        P p; prepare(p,mode,upper?96:24,-32767,true); p.set_arp_pitch(upper?48:-24);
        CHECK(p.actual_note()==(upper?120:0)); auto out=pcm(p,6000,true);
        CHECK(p.playback_active()&&std::all_of(out.begin(),out.end(),[](q15 value){return value>=0;}));
    }
    std::puts("ok 32 poison-trim/cut stereo/reverse/short-window sustained cases, exact 2-frame endpoints, loop/sign-flip reads, missing cuts, final MIDI extrema and authored fades");
}
static void partition_preview_stale() {
    fixture(32768);
    for(Mode mode:{Mode::Normal,Mode::Stretch}) for(int sign:{-1,1}) {
        P a,b; for(P* p:{&a,&b}) { p->set_pitch_mode(mode); auto edit=edit_for(32768,512,32000); edit.loop_start=8000; edit.loop_end=22000; edit.crossfade=512; edit.fade_in=256; edit.fade_out=1024;
            CHECK(p->set_edit_state(edit,SampleBank::instance().generation(0))); prepare(*p,mode,67,10000*sign,true); }
        CHECK(pcm(a,75003)==pcm(b,75003,true)); CHECK(a.pos_==b.pos_&&a.frac_==b.frac_&&a.stretch_.tick==b.stretch_.tick);
        a.set_arp_pitch(12); a.preview(0,32767,0); CHECK(a.actual_note()==67&&std::abs(a.note_inc_q16_)==musical_note::ratio_q16(7));
        CHECK(a.preview_frames(1000,3000,true,SampleBank::instance().generation(0),sign<0,1024));
        CHECK(a.actual_note()==67&&a.win_start_==1000&&a.win_end_==3000);
        if(mode==Mode::Normal)CHECK(std::abs(a.inc_q16_)==musical_note::ratio_q16(7));
        else CHECK(std::abs(a.inc_q16_)==262144);
        advance(a,5000); CHECK(a.playback_active()); CHECK(a.preview_seek(1500)); CHECK(!a.stretch_.ready);
        advance(a,70); a.release(); advance(a,63); CHECK(a.playback_active()); advance(a,1); CHECK(!a.playback_active());
        prepare(a,mode); advance(a,100); const int source=a.sample_slot; a.sample_slot=63;
        auto silence=pcm(a,128); CHECK(std::all_of(silence.begin(),silence.end(),[](q15 x){return x==0;}));
        CHECK(!a.playback_active()); a.sample_slot=source;
    }
    for(Mode mode:{Mode::Normal,Mode::Stretch}) {
        for(int failure=0;failure<6;++failure) {
            fixture(4096); P p; prepare(p,mode,72,8192,true); advance(p,100);
            auto& source=SampleBank::instance().slot(0);
            if(failure==0) fixture(4096); // same-sized replacement with new generation
            if(failure==1) source.data.resize(2);
            if(failure==2) source.channels=3;
            if(failure==3) source.logical_frames=32;
            if(failure==4) p.sample_slot=-1;
            if(failure==5) p.sample_slot=64;
            auto silence=pcm(p,64); CHECK(!p.playback_active()); CHECK(std::all_of(silence.begin(),silence.end(),[](q15 x){return x==0;}));
            p.control_tick(); p.trigger(); // malformed/absent sources are safely rejected
            if(failure==2||failure>=4)CHECK(!p.playback_active());
        }
    }
    fixture(32768);
    for(int sign:{-1,1}) {
        P a,b; prepare(a,Mode::Stretch,48,4096*sign,true); prepare(b,Mode::Stretch,48,4096*sign,true);
        for(int step=0;step<8;++step) {
            const int notes[8]={48,72,60,84,36,96,60,60}; const int speeds[8]={4096,16384,8192,2048,32767,8192,8192,4096};
            for(P* p:{&a,&b}) {set(*p,P::P_NOTE,musical_note::encode(notes[step]));set(*p,P::P_SPEED,q15(speeds[step]*sign));p->control_tick();}
            CHECK(pcm(a,1777)==pcm(b,1777,true));
        }
    }
    P normal,stretch; prepare(normal,Mode::Normal); prepare(stretch,Mode::Stretch);
    CHECK(pcm(normal,32000)==pcm(stretch,32000,true));
    prepare(stretch,Mode::Stretch,72); advance(stretch,3000); CHECK(stretch.stretch_.ready);
    stretch.set_pitch_mode(Mode::Normal); CHECK(!stretch.playback_active()&&!stretch.stretch_.ready);
    // Scratch/hold remain the existing gesture paths, independent of note/stretch.
    for(bool hold:{false,true}) {
        P legacy,modern;
        for(P* p:{&legacy,&modern}) { if(p==&modern)p->set_pitch_mode(Mode::Stretch); set(*p,P::P_SCRUB,32767); set(*p,P::P_HOLD,hold?32767:0); set(*p,P::P_POSITION,12000); set(*p,P::P_SPEED,10000); p->control_tick(); p->trigger(); }
        set(modern,P::P_NOTE,musical_note::encode(96)); modern.control_tick();
        CHECK(pcm(legacy,7000)==pcm(modern,7000,true));
        set(modern,P::P_SCRUB,0); modern.control_tick(); CHECK(!modern.stretch_.ready); advance(modern,50); CHECK(modern.stretch_.ready);
    }
    std::puts("ok arbitrary partitions/zero allocation, source guards, preview arp isolation, release, mode resets, separate scratch/hold and exact 1x/unison");
}
static void engine_arp() {
    fixture();
    auto render=[](Engine& e,int frames) { q15 out[1024]; while(frames) {int count=std::min(512,frames); const auto before=allocations; e.render(out,count);CHECK(allocations==before);frames-=count;} };
    for(Mode mode:{Mode::Normal,Mode::Stretch})for(bool legato:{false,true}) {
        auto e=std::make_unique<Engine>(); auto* arp=new ArpUnit; auto* play=new P;
        e->chains[0].units[0]=arp; e->chains[0].units[1]=play; e->sequencer_enabled=false;
        play->set_pitch_mode(mode); set(*play,P::P_NOTE,musical_note::encode(60)); set(*play,P::P_LOOP,32767);
        set(*arp,ArpUnit::P_NOTE2,ArpUnit::note_value(12)); set(*arp,ArpUnit::P_NOTE3,0); set(*arp,ArpUnit::P_GATE,32767); set(*arp,ArpUnit::P_PLAY_MODE,legato?32767:0);
        e->transport_play(); render(*e,4001);
        CHECK(play->actual_note()==72&&play->note_inc_q16_==131072);
        CHECK(mode==Mode::Normal?play->inc_q16_==131072:play->inc_q16_==65536);
        CHECK(legato?play->visual_source_frame()>3900:play->visual_source_frame()<8);
        CHECK(play->params[P::P_NOTE].base==musical_note::encode(60));
        e->transport_stop(); render(*e,80); CHECK(!play->arp_voice.gate&&play->arp_voice.semitones==0);
    }
    std::puts("ok actual engine arp single offset, restart versus legato, source note/base ownership and render allocation");
}
static void legacy_identity() {
    // FNV-1a fingerprints captured by compiling the pre-change units.cpp/.h
    // backups 20261002_082843 in isolation (no engine/persistence changes).
    static constexpr uint64_t expected[16] = {
        0x79355558198aac09ull,0x5399bb45711fb73cull,0xfdfc331df9f9828cull,0x99ad33f0a36290e2ull,
        0x7ee29c6bce45b910ull,0xdb0d7886ab7bf9f1ull,0xfb83a6a923f3190bull,0x7c30641fc76e17a5ull,
        0x2e69df38ee313f3cull,0x042a6ed0291440daull,0x2487ae592ba2f7edull,0xa280e1722a9fc6fdull,
        0xf8e2fa897ea91b1eull,0x03cb65d55ce3731eull,0xf9b2a91bb560ae72ull,0xbee534dfbcd28f2cull
    };
    for(int scenario=0;scenario<16;++scenario) {
        auto& s=SampleBank::instance().slot(0); s=Sample{}; s.channels=(scenario&1)?2:1; s.data.resize(12000*s.channels);
        for(size_t i=0;i<s.data.size();++i)s.data[i]=q15(int((i*173+127)%54000)-27000);
        s.chops[0]=512; s.chops[1]=7000; s.slice_reverse_mask=(scenario&2)?1:0; s.slice_speed_q8[0]=(scenario&4)?384:256;
        P p; p.set_modern(scenario>=4); set(p,P::P_SPEED,(scenario&1)?-7300:9300); set(p,P::P_START,1200); set(p,P::P_LEN,25000);
        set(p,P::P_LOOP,32767); set(p,P::P_SLICE,2048); p.pitch_quantize=(scenario&2)!=0;
        if(scenario>=8) {auto edit=edit_for(12000,900,6000);edit.loop_start=1700;edit.loop_end=5200;edit.crossfade=128;edit.fade_in=30;edit.fade_out=150;CHECK(p.set_edit_state(edit,SampleBank::instance().generation(0)));}
        if(scenario>=12) {set(p,P::P_SCRUB,32767);set(p,P::P_POSITION,12000);set(p,P::P_HOLD,(scenario&2)?32767:0);}
        p.control_tick();p.trigger();if(scenario&4)p.set_arp_pitch(7);
        uint64_t hash=1469598103934665603ull;q15 out[173];int done=0;
        while(done<9000) {const int n=std::min(173,9000-done);block(p,out,n);for(int i=0;i<n;++i){hash^=uint16_t(out[i])&255;hash*=1099511628211ull;hash^=uint16_t(out[i])>>8;hash*=1099511628211ull;}done+=n;if(done>4000)set(p,P::P_POSITION,26000);}
        CHECK(hash==expected[scenario]); CHECK(p.pitch_mode()==Mode::Legacy&&p.num_params==(scenario>=4?11:6));
    }
    std::puts("ok 16 pre-change legacy PCM fingerprints (144000 frames): six/eleven params, mono/stereo, reverse, quantize, cut speed, edit/fades, scratch/hold and arp");
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0); fixture();
    helpers_clone(); normal_pitch(); stretch_pitch_lifetime(); bounds_stereo_short_fades(); partition_preview_stale(); engine_arp(); legacy_identity();
    std::printf("play note/stretch core 7/7 groups passed; %u assertions; state %zu bytes\n",assertions,sizeof(play_stretch::State));
}
