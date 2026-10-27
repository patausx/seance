// Narrow INST proof: actual units.cpp implementation, real immutable PCM and
// mutable SampleBank. No UI/media/SDK stubs. The scalar oracle intentionally uses
// direct time/phase formulae (division allowed in tests), not the DSP ramp state.
#include "engine/units.h"
#include "engine/inst_bank_data.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

using seance::InstUnit;
using seance::q15;
namespace bank=seance::inst_bank;
static bool guarded=false;
static uint64_t heap_calls=0, processed_frames=0, scalar_comparisons=0, visual_comparisons=0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void __real_free(void*);
extern "C" void* __real__Znwm(size_t);
extern "C" void* __real__Znam(size_t);
extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&);
extern "C" void* __real__ZnamRKSt9nothrow_t(size_t,const std::nothrow_t&);
extern "C" void* __wrap_malloc(size_t n){if(guarded)++heap_calls;return __real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s){if(guarded)++heap_calls;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n){if(guarded)++heap_calls;return __real_realloc(p,n);}
extern "C" void __wrap_free(void* p){if(guarded&&p)++heap_calls;__real_free(p);}
extern "C" void* __wrap__Znwm(size_t n){if(guarded)++heap_calls;return __real__Znwm(n);}
extern "C" void* __wrap__Znam(size_t n){if(guarded)++heap_calls;return __real__Znam(n);}
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t& t){if(guarded)++heap_calls;return __real__ZnwmRKSt9nothrow_t(n,t);}
extern "C" void* __wrap__ZnamRKSt9nothrow_t(size_t n,const std::nothrow_t& t){if(guarded)++heap_calls;return __real__ZnamRKSt9nothrow_t(n,t);}
struct Guard {
    Guard(){assert(!guarded);guarded=true;}
    ~Guard(){guarded=false;assert(heap_calls==0);}
};
static void set(InstUnit& u,int p,q15 value){u.params[p].base=u.params[p].effective=u.params[p].target=value;}
static void configure(InstUnit& u,int family,int midi,unsigned length=4000,unsigned attack=0,unsigned release=3000){
    set(u,InstUnit::P_INSTRUMENT,InstUnit::instrument_value(family));
    set(u,InstUnit::P_NOTE,InstUnit::note_value(midi));
    set(u,InstUnit::P_LENGTH,InstUnit::length_value(length));
    set(u,InstUnit::P_ATTACK,InstUnit::attack_value(attack));
    set(u,InstUnit::P_RELEASE,InstUnit::release_value(release));
    set(u,InstUnit::P_TONE,32767);set(u,InstUnit::P_LEVEL,32767);
    u.arp_voice={};u.reset();
}
static InstUnit::Visual visual(const InstUnit& u){Guard guard;return u.visual();}
static void same_visual(const InstUnit::Visual& a,const InstUnit::Visual& b){
    assert(a.strikes==b.strikes&&a.active==b.active&&a.active<=4);
    for(int i=0;i<4;++i){const auto& x=a.voices[i];const auto& y=b.voices[i];
        assert(x.age==y.age&&x.envelope==y.envelope&&x.instrument==y.instrument&&x.note==y.note&&x.stage==y.stage);
        assert(x.stage<=uint8_t(InstUnit::Stage::Release)&&x.envelope<=32767);
    }
    ++visual_comparisons;
}
static void render(InstUnit& u,q15* output,int count){Guard guard;u.process(output,count);processed_frames+=unsigned(count);}
static void advance(InstUnit& u,unsigned frames){
    q15 data[512];while(frames){const unsigned n=std::min(512u,frames);render(u,data,int(n));frames-=n;}
}
static uint64_t hash(const q15* pcm,unsigned n,uint64_t h=1469598103934665603ull){
    for(unsigned i=0;i<n;++i){h^=uint16_t(pcm[i]);h*=1099511628211ull;}return h;
}
static const bank::Zone& zone(int family,int midi){
    for(int i=0;i<bank::ZONE_COUNT;++i){const auto& z=bank::ZONES[i];
        if(z.instrument==family&&midi>=z.low&&midi<=z.high)return z;}
    assert(false);return bank::ZONES[0];
}
static int64_t floor_div(int64_t value,int64_t divisor){
    const int64_t q=value/divisor;return value<0&&value%divisor ? q-1:q;
}
static uint32_t increment(int semitones){
    // Reference constant table in Q24; reconstruct with a direct rational shift.
    constexpr uint32_t ratios[]={16777216,17774841,18831788,19951585,21137968,22394897,
        23726566,25137421,26632170,28215802,29893600,31671166};
    int octave=0;while(semitones<0){semitones+=12;--octave;}while(semitones>=12){semitones-=12;++octave;}
    const uint64_t numerator=uint64_t(ratios[semitones])*24000u;
    const uint64_t denominator=uint64_t(32000u)<<(8-octave);
    return uint32_t((numerator+denominator/2)/denominator);
}
struct Scalar {
    const bank::Zone& z;
    uint32_t inc,hold,attack,release,end;
    Scalar(int family,int midi,unsigned length=4000,unsigned atk=0,unsigned rel=3000):z(zone(family,midi)),
        inc(family==5 ? 49152u:increment(midi-int(z.root))),
        hold(family==5 ? std::max(32u,atk*32u):length*32u),attack(std::max(32u,atk*32u)),release(rel*32u),
        end(z.loop_end ? 0u:uint32_t(((uint64_t(z.frames)<<16)+inc-1)/inc)){}
    uint32_t finish()const{return end ? std::min(end,hold+release):hold+release;}
    uint32_t envelope(uint32_t age)const{
        if(age>=finish())return 0;
        const uint32_t start=std::min(65536u,uint32_t(uint64_t(hold)*65536u/attack));
        if(age>=hold)return start-uint32_t(uint64_t(age-hold)*start/release);
        return std::min(65536u,uint32_t(uint64_t(age)*65536u/attack));
    }
    int32_t mono(uint32_t age)const{
        if(age>=finish())return 0;
        const uint64_t position=uint64_t(age)*inc;
        uint32_t index=uint32_t(position>>16);
        if(z.loop_end&&index>=z.loop_end)index=z.loop_start+(index-z.loop_start)%(z.loop_end-z.loop_start);
        assert(index<z.frames);
        const int32_t a=bank::PCM[z.offset+index],b=bank::PCM[z.offset+index+1];
        const int32_t sample=a+int32_t(floor_div(int64_t(b-a)*(position&65535u),65536));
        int32_t out=int32_t(floor_div(int64_t(sample)*envelope(age),65536));
        if(end&&end-age<=64)out=int32_t(floor_div(int64_t(out)*(end-age-1),64));
        return out;
    }
    q15 output(uint32_t age,int64_t level=int64_t(32767)*65536)const{
        return q15(floor_div(int64_t(mono(age))*level,int64_t(1)<<33));
    }
};

static void mappings(){
    Guard guard;
    InstUnit u;assert(u.kind==seance::UnitKind::Inst&&u.num_params==7&&u.ready());
    assert(InstUnit::note_midi(u.params[InstUnit::P_NOTE].base)==60);
    assert(InstUnit::length_ms(u.params[InstUnit::P_LENGTH].base)==480);
    assert(InstUnit::attack_ms(u.params[InstUnit::P_ATTACK].base)==8);
    assert(InstUnit::release_ms(u.params[InstUnit::P_RELEASE].base)==640);
    const auto props=u.sequencer_properties();
    assert(props.source&&!props.gate_processor&&props.lifecycle==seance::SequenceLifecycle::Strike);
    assert(props.pitch_param==InstUnit::P_NOTE&&props.level_param==InstUnit::P_LEVEL&&props.duration_param==InstUnit::P_LENGTH);
    for(int p=0;p<7;++p)assert(!u.params[p].bipolar);
    int previous_note=24,previous_instrument=0;unsigned l=20,a=0,r=5;
    for(int v=-32768;v<=32767;++v){const q15 q=q15(v);
        const int note=InstUnit::note_midi(q),instrument=InstUnit::instrument_index(q);
        assert(note>=previous_note&&note<=96&&instrument>=previous_instrument&&instrument<6);
        previous_note=note;previous_instrument=instrument;
        assert(InstUnit::length_ms(q)>=l&&InstUnit::length_ms(q)<=4000);l=InstUnit::length_ms(q);
        assert(InstUnit::attack_ms(q)>=a&&InstUnit::attack_ms(q)<=1000);a=InstUnit::attack_ms(q);
        assert(InstUnit::release_ms(q)>=r&&InstUnit::release_ms(q)<=3000);r=InstUnit::release_ms(q);
    }
    for(unsigned ms=0;ms<=5000;++ms){
        assert(InstUnit::length_ms(InstUnit::length_value(ms))==std::clamp(ms,20u,4000u));
        assert(InstUnit::attack_ms(InstUnit::attack_value(ms))==std::min(ms,1000u));
        assert(InstUnit::release_ms(InstUnit::release_value(ms))==std::clamp(ms,5u,3000u));
    }
    for(int i=0;i<6;++i){assert(InstUnit::instrument_index(InstUnit::instrument_value(i))==i);
        assert(std::strlen(InstUnit::instrument_name(i))>0&&std::strlen(InstUnit::drum_name(i))>0);
        assert(InstUnit::drum_index(InstUnit::drum_note(i))==i);}
    for(int midi=24;midi<=96;++midi){
        assert(InstUnit::note_midi(InstUnit::note_value(midi))==midi);
        assert(zone(5,midi).root==InstUnit::drum_note(InstUnit::drum_index(midi)));
    }
    for(const int extreme:{INT_MIN,INT_MAX}){
        assert(InstUnit::note_midi(InstUnit::note_value(extreme))==(extreme<0?24:96));
        assert(InstUnit::instrument_index(InstUnit::instrument_value(extreme))==(extreme<0?0:5));
        assert(InstUnit::drum_index(extreme)==(extreme<0?0:5));
        assert(InstUnit::drum_note(extreme)==(extreme<0?36:49));
    }
    assert(InstUnit::length_value(UINT_MAX)==32767&&InstUnit::attack_value(UINT_MAX)==32767&&InstUnit::release_value(UINT_MAX)==32767);
}
static void bank_bounds(){
    assert(sizeof(bank::PCM)==1221500&&sizeof(bank::PCM)<=1536u*1024u);
    uint32_t expected_offset=0;
    for(int i=0;i<bank::ZONE_COUNT;++i){const auto& z=bank::ZONES[i];
        assert(z.offset==expected_offset&&z.frames>2048&&z.root>=z.low&&z.root<=z.high);
        assert(z.offset+z.frames<bank::PCM_FRAMES&&bank::PCM[z.offset]==0);
        assert(z.instrument<6&&z.low>=24&&z.high<=96&&z.low<=z.high);
        int peak=0;for(uint32_t frame=0;frame<z.frames;++frame)peak=std::max(peak,std::abs(int(bank::PCM[z.offset+frame])));
        assert(peak==24576);
        if(z.loop_end){
            assert(z.instrument>=1&&z.instrument<=3&&z.loop_end==z.frames&&z.loop_start>0&&z.loop_end-z.loop_start>14000);
            assert(bank::PCM[z.offset+z.frames]==bank::PCM[z.offset+z.loop_start]);
            assert(bank::PCM[z.offset+z.frames-1]==bank::PCM[z.offset+z.loop_start-1]);
            int64_t sum=0;for(uint32_t j=z.loop_start;j<z.loop_end;++j)sum+=bank::PCM[z.offset+j];
            assert(std::abs(sum)<=int64_t(z.loop_end-z.loop_start)*2); // under two PCM LSB of cycle DC
        }else assert(bank::PCM[z.offset+z.frames-1]==0&&bank::PCM[z.offset+z.frames]==0);
        expected_offset=z.offset+z.frames+1;
    }
    assert(expected_offset==bank::PCM_FRAMES);
}
static void all_keys(){
    uint64_t family_hash[6]={};q15 pcm[512];
    for(int family=0;family<6;++family)for(int midi=24;midi<=96;++midi){
        InstUnit u;configure(u,family,midi);
        {Guard guard;u.trigger();}
        uint64_t energy=0,h=1469598103934665603ull;
        for(int block=0;block<8;++block){render(u,pcm,512);h=hash(pcm,512,h);for(q15 sample:pcm)energy+=unsigned(std::abs(int(sample)));}
        assert(energy>0);if(midi==60)family_hash[family]=h;
        const auto v=visual(u);assert(v.active==1&&v.strikes==1&&v.voices[0].instrument==family&&v.voices[0].note==midi&&v.voices[0].age==4096);
    }
    for(int a=0;a<6;++a)for(int b=a+1;b<6;++b)assert(family_hash[a]!=family_hash[b]);
}
static void scalar_audio(){
    q15 pcm[257];
    for(int zindex=0;zindex<bank::ZONE_COUNT;++zindex){const auto& z=bank::ZONES[zindex];
        // Root: entire natural finite/release tail and repeated loop wraps.
        // Zone edges: interpolation at fastest/slowest transposition, plus
        // pitched release entered during an attack longer than the requested hold.
        // Drums instead enter release only after attack, regardless of LENGTH.
        for(int scenario=0;scenario<3;++scenario){
            const int midi=scenario==0?z.root:scenario==1?z.low:z.high;
            const unsigned hold=scenario==2?20u:2400u,attack=scenario==2?1000u:17u,release=scenario==2?5u:400u;
            Scalar reference(z.instrument,midi,hold,attack,release);
            InstUnit u;configure(u,z.instrument,midi,hold,attack,release);
            {Guard guard;u.trigger();}
            const uint32_t limit=reference.finish()+129;
            for(uint32_t at=0;at<limit;){
                const unsigned count=std::min<unsigned>(257,limit-at);render(u,pcm,int(count));
                for(unsigned i=0;i<count;++i){const q15 expected=reference.output(at+i);
                    if(pcm[i]!=expected){std::fprintf(stderr,"oracle zone=%d scenario=%d age=%u actual=%d expected=%d\n",zindex,scenario,at+i,int(pcm[i]),int(expected));assert(false);}
                    ++scalar_comparisons;
                }
                at+=count;
                const auto v=visual(u);
                if(at<reference.finish()){
                    assert(v.active==1&&v.voices[0].age==at);
                    assert(v.voices[0].envelope==std::min(32767u,reference.envelope(at)>>1));
                    const uint8_t stage=at>=reference.hold?3:at>=reference.attack?2:1;
                    assert(v.voices[0].stage==stage);
                }else assert(v.active==0);
            }
        }
    }
}
static void drums_length_inert(){
    // Both q15 LENGTH endpoints, every canonical drum, all four attack/release
    // endpoint combinations plus defaults. The oracle has no drum hold-length.
    // Check exact transition/retirement edges and finite ends during slow attack.
    unsigned cases=0;uint64_t invariant_frames=0;
    const unsigned attacks[]={0,0,1000,1000,8},releases[]={5,3000,5,3000,640};
    for(int drum=0;drum<InstUnit::DRUM_COUNT;++drum)for(int shape=0;shape<5;++shape){
        const int midi=InstUnit::drum_note(drum);
        InstUnit low,high;
        {Guard guard;
            configure(low,5,midi,20,attacks[shape],releases[shape]);
            configure(high,5,midi,4000,attacks[shape],releases[shape]);
            // A stale, opposite effective value must not leak in either.
            low.params[InstUnit::P_LENGTH].effective=32767;
            high.params[InstUnit::P_LENGTH].effective=0;
            low.trigger();high.trigger();
        }
        const Scalar reference(5,midi,20,attacks[shape],releases[shape]);
        assert(reference.hold==reference.attack);
        std::array<q15,259> x{},y{};
        for(uint32_t age=0;age<reference.finish()+129;){
            unsigned count=std::min(257u,reference.finish()+129-age);
            for(const uint32_t edge:{reference.attack,reference.finish()})
                if(edge>age)count=std::min(count,edge-age);
            x.fill(12345);y.fill(-23456);
            {Guard guard;low.control_tick();high.control_tick();}
            render(low,x.data()+1,int(count));render(high,y.data()+1,int(count));
            assert(x[0]==12345&&y[0]==-23456);
            for(unsigned i=count+1;i<x.size();++i)assert(x[i]==12345&&y[i]==-23456);
            for(unsigned i=0;i<count;++i){
                const q15 expected=reference.output(age+i);
                assert(x[i+1]==y[i+1]&&x[i+1]==expected&&y[i+1]==expected);
                assert(std::abs(int(x[i+1]))<=6144); // one bounded voice at unity LEVEL
                scalar_comparisons+=2;
            }
            invariant_frames+=count;age+=count;
            const auto a=visual(low),b=visual(high);same_visual(a,b);
            assert(a.strikes==1&&a.active<=1);
            if(age<reference.finish()){
                assert(a.active==1&&a.voices[0].age==age&&a.voices[0].instrument==5&&a.voices[0].note==midi);
                assert(a.voices[0].envelope==std::min(32767u,reference.envelope(age)>>1));
                assert(a.voices[0].stage==uint8_t(age<reference.attack ? InstUnit::Stage::Attack:InstUnit::Stage::Release));
            }else assert(a.active==0);
        }
        assert(low.params[InstUnit::P_LENGTH].base==0&&low.params[InstUnit::P_LENGTH].target==0);
        assert(high.params[InstUnit::P_LENGTH].base==32767&&high.params[InstUnit::P_LENGTH].target==32767);
        {Guard guard;low.reset();high.reset();}
        same_visual(visual(low),visual(high));assert(!visual(low).active&&!visual(low).strikes);
        ++cases;
    }
    assert(cases==30);
    std::printf("pass inst drums LENGTH inert: all 6 canonical keys, %u endpoint/default shapes, %llu exact min/max PCM frames; attack -> release/finite end, guarded bounds and heap\n",
        cases,(unsigned long long)invariant_frames);
}
static void silent_latching_and_arp(){
    InstUnit a,b;q15 x[257],y[257];
    {Guard guard;
        for(int n=0;n<20;++n){set(a,InstUnit::P_INSTRUMENT,InstUnit::instrument_value(n%6));set(a,InstUnit::P_NOTE,InstUnit::note_value(24+n));a.control_tick();}
    }
    std::fill(std::begin(x),std::end(x),1234);render(a,x,257);for(q15 v:x)assert(v==0);assert(visual(a).strikes==0);
    configure(a,0,60,480,8,640);configure(b,0,60,480,8,640);
    {Guard guard;
        // Discrete target wins even when the old effective value says drums.
        a.params[InstUnit::P_INSTRUMENT].effective=32767;a.params[InstUnit::P_NOTE].effective=0;
        a.trigger();b.trigger();
        set(a,InstUnit::P_INSTRUMENT,32767);set(a,InstUnit::P_NOTE,32767);set(a,InstUnit::P_LENGTH,0);
        set(a,InstUnit::P_ATTACK,32767);set(a,InstUnit::P_RELEASE,0);set(a,InstUnit::P_TONE,0);
        a.set_arp_pitch(12);a.control_tick();
    }
    for(int block=0;block<120;++block){render(a,x,257);render(b,y,257);assert(!std::memcmp(x,y,sizeof x));same_visual(visual(a),visual(b));}
    {Guard guard;a.trigger();}
    const auto v=visual(a);assert(v.strikes==2&&v.active==2&&v.voices[0].instrument==0&&v.voices[1].instrument==5&&v.voices[1].note==96);
    for(const auto pair: {std::pair<int,int>{60,12},{24,-24},{96,48}}){
        configure(a,1,pair.first);configure(b,1,std::clamp(pair.first+pair.second,24,96));
        {Guard guard;a.set_arp_pitch(pair.second);a.trigger();b.trigger();}
        for(int block=0;block<8;++block){render(a,x,257);render(b,y,257);assert(!std::memcmp(x,y,sizeof x));same_visual(visual(a),visual(b));}
    }
    {Guard guard;a.reset();}
    const auto before=visual(a);q15 untouched=123;
    {Guard guard;a.process(&untouched,0);a.process(&untouched,-1);a.process(nullptr,23);a.control_tick();}
    assert(untouched==123);same_visual(before,visual(a));
    render(a,x,257);for(q15 sample:x)assert(sample==0);assert(!visual(a).active&&!visual(a).strikes);
}
static void level_ramp(){
    InstUnit u;configure(u,3,72);Scalar scalar(3,72);
    {Guard guard;u.trigger();}
    advance(u,60000);uint32_t age=60000;
    int64_t current=int64_t(32767)*65536;
    q15 sample;
    // Several mid-ramp changes eventually exercise nonzero quotient remainders.
    const int targets[]={0,13991,3,32767,11111,0,0,32767};
    const unsigned lengths[]={37,11,9,5,17,128,64,128};
    for(int segment=0;segment<8;++segment){
        {Guard guard;set(u,InstUnit::P_LEVEL,q15(targets[segment]));u.control_tick();}
        const int64_t begin=current,delta=int64_t(targets[segment])*65536-begin;
        for(unsigned at=1;at<=lengths[segment];++at){
            current=begin+delta*std::min(128u,at)/128; // signed truncation matches exact rational ramp
            {Guard guard;u.control_tick();} // unchanged targets must not restart it
            render(u,&sample,1);
            assert(sample==scalar.output(age,current));++scalar_comparisons;++age;
            if(segment==6)assert(sample==0);
        }
    }
    assert(visual(u).active==1&&visual(u).voices[0].age==age);
}
static void stealing(){
    InstUnit u;configure(u,1,67);Scalar old(1,67),fresh(0,60);
    {Guard guard;for(int i=0;i<4;++i)u.trigger();}
    assert(visual(u).active==4&&visual(u).strikes==4);advance(u,2048);
    const int32_t last=old.mono(2047);
    {Guard guard;set(u,InstUnit::P_INSTRUMENT,InstUnit::instrument_value(0));set(u,InstUnit::P_NOTE,InstUnit::note_value(60));u.trigger();}
    q15 sample;
    for(unsigned at=0;at<64;++at){
        render(u,&sample,1);
        const int64_t replacement=floor_div(int64_t(last)*(64-at)+int64_t(fresh.mono(at))*at,64);
        const int64_t mixed=3*int64_t(old.mono(2048+at))+replacement;
        const q15 expected=q15(floor_div(mixed*32767*65536,int64_t(1)<<33));
        assert(sample==expected);++scalar_comparisons;
    }
    auto v=visual(u);assert(v.active==4&&v.strikes==5&&v.voices[0].instrument==0&&v.voices[0].age==64);
    {Guard guard;for(int i=0;i<100;++i)u.trigger();}
    assert(visual(u).active==4&&visual(u).strikes==105);
    advance(u,224001);assert(!visual(u).active);render(u,&sample,1);assert(sample==0);
}
static void partitions_extremes(){
    InstUnit a,b;configure(a,0,60);configure(b,0,60);q15 x[149],y[149];
    for(int event=0;event<600;++event){
        {Guard guard;
            for(InstUnit* u:{&a,&b}){
                const q15 value=event%3==0 ? -32768:event%3==1 ? 32767:q15(event*193%32768);
                set(*u,InstUnit::P_INSTRUMENT,InstUnit::instrument_value(event%6));
                set(*u,InstUnit::P_NOTE,value);set(*u,InstUnit::P_ATTACK,value);
                set(*u,InstUnit::P_LENGTH,event%5 ? value:InstUnit::length_value(4000));
                set(*u,InstUnit::P_RELEASE,q15(-value-1));set(*u,InstUnit::P_TONE,q15(-value-1));
                if(event%13==0)set(*u,InstUnit::P_LEVEL,value);
                u->control_tick();
                if(event==301)u->reset();
                if(event%3==0)u->trigger();
                if(event%31==0)for(int repeat=0;repeat<9;++repeat)u->trigger();
            }
        }
        const int count=1+event*47%149;std::fill_n(x,count,q15(12345));std::fill_n(y,count,q15(-23456));
        render(a,x,count);for(int i=0;i<count;++i)render(b,y+i,1);
        assert(!std::memcmp(x,y,size_t(count)*sizeof(q15)));same_visual(visual(a),visual(b));
        const auto frozen=visual(a);for(int i=0;i<4;++i)same_visual(frozen,visual(a));
    }
    {Guard guard;a.reset();b.reset();}
    render(a,x,149);render(b,y,149);assert(!std::memcmp(x,y,sizeof x));for(q15 sample:x)assert(sample==0);
}
static void independent_sample_bank(){
    auto& samples=trackr::synth::SampleBank::instance();
    trackr::synth::Sample sample;sample.data.assign(128,12345);sample.root_note=43;sample.loop_start=3;sample.loop_end=100;
    assert(samples.replace_sample(3,std::move(sample),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
    const auto* pointer=samples.slot(3).data.data();const auto generation=samples.generation(3);const auto bytes=samples.pcm_bytes_used();
    {
        Guard guard;InstUnit many[16];q15 pcm[32];
        for(int i=0;i<16;++i){set(many[i],InstUnit::P_INSTRUMENT,InstUnit::instrument_value(i%6));many[i].trigger();many[i].process(pcm,32);many[i].control_tick();(void)many[i].visual();many[i].reset();}
    }
    assert(samples.slot(3).data.data()==pointer&&samples.generation(3)==generation&&samples.pcm_bytes_used()==bytes);
    assert(samples.slot(3).root_note==43&&samples.slot(3).loop_start==3&&samples.slot(3).loop_end==100);
    for(q15 value:samples.slot(3).data)assert(value==12345);
    InstUnit a,b;configure(a,0,60);configure(b,0,60);{Guard guard;a.trigger();b.trigger();}
    q15 x[512],y[512];render(a,x,512);render(b,y,512);assert(!std::memcmp(x,y,sizeof x));
    trackr::synth::Sample replacement;replacement.data.assign(32768,-12345);
    assert(samples.replace_sample(3,std::move(replacement),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
    for(int i=0;i<32;++i){render(a,x,512);render(b,y,512);assert(!std::memcmp(x,y,sizeof x));}
    for(q15 value:samples.slot(3).data)assert(value==-12345);
    samples.clear_slot(3);
}
int main(){
    mappings();std::puts("pass inst mappings/defaults: all q15 values, inverse milliseconds, GM nearest zones, strike semantics");
    bank_bounds();all_keys();std::puts("pass inst immutable bank: 22 zones, 438 family/key combinations audible, real finite/loop boundaries and DC");
    scalar_audio();std::puts("pass inst scalar PCM: roots and both zone edges, linear interpolation, loops, early/natural/finite release, stage/envelope");
    drums_length_inert();
    silent_latching_and_arp();std::puts("pass inst silence/latching/arp: no edit retrigger, discrete target locks, one transposition, reset and frozen visuals");
    level_ramp();stealing();std::puts("pass inst live level: exact 128 samples and retargets; four-voice bound, 64-sample steal continuity, full retirement");
    partitions_extremes();independent_sample_bank();std::puts("pass inst partitions/extremes/ownership: exact PCM and visuals, repeated steals, 16 shared-bank units, mutable SampleBank untouched");
    std::printf("inst core: %llu processed frames / %llu independent scalar comparisons / %llu frozen comparisons / %llu guarded heap calls; unit=%zu visual=%zu bank=%zu bytes\n",
        (unsigned long long)processed_frames,(unsigned long long)scalar_comparisons,(unsigned long long)visual_comparisons,
        (unsigned long long)heap_calls,sizeof(InstUnit),sizeof(InstUnit::Visual),sizeof(bank::PCM));
}
