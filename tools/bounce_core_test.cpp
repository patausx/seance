// Focused master/chain bounce host contract. No platform/GPU/audio device shims.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/play_window.h"
#include "engine/patch_io.h"
#include "synth/wav_writer.h"
#include "synth/wav_loader.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
using namespace seance;
using namespace trackr::synth;
static uint64_t checks=0, twin_pcm=0, captured_pcm=0, chain_pcm=0;
static bool forbid_heap=false;
static unsigned heap_violations=0;
void* operator new(std::size_t n) { if(forbid_heap) ++heap_violations; if(void* p=std::malloc(n?n:1))return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) {return ::operator new(n);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {if(forbid_heap)++heap_violations;return std::malloc(n?n:1);}
void* operator new[](std::size_t n,const std::nothrow_t& tag) noexcept {return ::operator new(n,tag);}
void operator delete(void* p) noexcept {if(forbid_heap&&p)++heap_violations;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
struct NoHeap { NoHeap(){forbid_heap=true;} ~NoHeap(){forbid_heap=false;} };
struct Tone final : Unit {
    uint32_t position=0, calls=0, ticks=0; uint64_t fragments=0; int amplitude;
    explicit Tone(int a):amplitude(a){}
    void control_tick() override {++ticks;}
    void process(q15* out,int n) override {++calls;fragments=fragments*1099511628211ull+(unsigned)n;for(int i=0;i<n;++i)out[i]=(q15)(((position++%83)<41)?amplitude:-amplitude);}
};
static void clear_bank(){auto& b=SampleBank::instance();for(int i=0;i<SAMPLE_BANK_SIZE;++i)b.clear_slot(i);b.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);}
static void setup(Engine& e,int bpm,int swing,bool phase,bool fx){
    e.set_bpm(bpm);e.set_swing(swing);
    if(phase)e.set_clock_mod(ClockTarget::Swing,ModSource::SigilA,1);
    e.chains[0].units[0]=new Tone(fx?32000:11000);e.chains[0].pan=-32767;
    e.chains[1].units[0]=new Tone(fx?27000:7000);e.chains[1].pan=32767;
    if(fx){e.apply_global_fx(GlobalFxTarget::Drive,8192);e.apply_global_fx(GlobalFxTarget::Crush,5000);e.set_transition_fx(TransitionFxKind::Echo,60);Sample preview;preview.channels=2;preview.data.resize(32000*4*2);for(size_t i=0;i<preview.data.size();++i)preview.data[i]=(q15)((int)(i%97)*100-4800);e.swap_sample_preview(preview);}
    e.transport_play();
}
static void render(Engine& e,int n){q15 out[1024*2];while(n){int c=std::min(n,1024);{NoHeap g;e.render(out,c);}n-=c;}}
static BounceSnapshot arm(Engine& e,int bars,BounceSource source=BounceSource::Master){BouncePlan p;{NoHeap g;CHECK(e.plan_bounce(bars,p,source)==BounceResult::Ok);}CHECK(Engine::stage_bounce(p)==BounceResult::Ok);{NoHeap g;CHECK(e.commit_bounce(p)==BounceResult::Ok);}CHECK(e.bounce_snapshot().source==source);return e.bounce_snapshot();}
static void discard(Engine& e){Sample retired;{NoHeap g;CHECK(e.discard_bounce(retired)==BounceResult::Ok);}}
static void complete(Engine& e,int bars,BounceSource source=BounceSource::Master){auto v=arm(e,bars,source);render(e,(int)(v.end_frame-e.transport_frame+7));CHECK(e.bounce_snapshot().status==BounceStatus::Complete);}
static bool wav_read(void* ptr,uint32_t first,uint32_t count,int16_t* out){auto* pair=static_cast<std::pair<Engine*,BounceSnapshot>*>(ptr);NoHeap g;return pair->first->read_bounce_frames(pair->second,first,count,out);}
static void exact_capture(){
    for(int phase=0;phase<2;++phase)for(int bpm:{20,137,300})for(int bars:{1,2,4,8}){
        clear_bank();Engine e,twin;setup(e,bpm,phase?73:57,phase,bpm!=20);setup(twin,bpm,phase?73:57,phase,bpm!=20);
        if(bpm==20&&bars>1){BouncePlan rejected;CHECK(e.plan_bounce(bars,rejected)==BounceResult::MemoryBudget);continue;}
        q15 a[1031*2],b[1031*2];{NoHeap g;e.render(a,123);twin.render(b,123);}CHECK(!std::memcmp(a,b,246*sizeof(q15)));
        auto v=arm(e,bars);CHECK(e.sample_slot_refs(v.slot).bounce==1);
        CHECK(v.start_frame>e.transport_frame);CHECK(v.end_frame-v.start_frame==v.target_frames);
        if(!phase){const uint64_t fpb=(32000u*60u+(unsigned)bpm/2u)/(unsigned)bpm;CHECK(v.start_frame==fpb*4);CHECK(v.target_frames==fpb*4*bars);}
        else {const uint64_t inc=((uint64_t)bpm*65536*65536+32000u*30)/(32000u*60);CHECK(v.start_frame==((4ull<<32)+inc-1)/inc);CHECK(v.end_frame==(((4ull*(bars+1))<<32)+inc-1)/inc);}
        std::vector<q15> expected((size_t)v.target_frames*2);uint64_t cursor=123;int block=0;
        const int sizes[]={1,17,32,47,513,1031,3,127};
        while(cursor<v.end_frame+71){int n=(int)std::min<uint64_t>(sizes[block++%8],v.end_frame+71-cursor);
            {NoHeap g;e.render(a,n);twin.render(b,n);}CHECK(!std::memcmp(a,b,(size_t)n*2*sizeof(q15)));twin_pcm+=(uint64_t)n*2;
            uint64_t begin=std::max(cursor,v.start_frame),end=std::min(cursor+n,v.end_frame);
            if(end>begin)std::memcpy(expected.data()+(begin-v.start_frame)*2,a+(begin-cursor)*2,(size_t)(end-begin)*2*sizeof(q15));
            cursor+=n;
        }
        v=e.bounce_snapshot();CHECK(v.status==BounceStatus::Complete);CHECK(v.frames==v.target_frames);CHECK(v.reason==BounceReason::None);
        const Sample* sample=e.bounce_sample(v);CHECK(sample&&sample->channels==2&&sample->playable_frames()==v.frames);
        CHECK(!std::memcmp(expected.data(),sample->data.data(),expected.size()*sizeof(q15)));captured_pcm+=expected.size();
        bool stereo=false;for(uint32_t i=0;i<v.frames;++i)if(expected[i*2]!=expected[i*2+1]){stereo=true;break;}CHECK(stereo);
        render(e,471);e.transport_stop();e.transport_reset();CHECK(e.bounce_sample(v));CHECK(!std::memcmp(expected.data(),sample->data.data(),expected.size()*sizeof(q15)));
        BouncePlan blocked;CHECK(e.plan_bounce(1,blocked)==BounceResult::Busy);discard(e);CHECK(!e.bounce_sample(v));
    }
    std::puts("ok exact capture: 18 accepted + 6 capacity-refused clock/bpm/bars cases, stereo final mix, arbitrary block overlap, zero audio heap");
}
static void lifecycle(){
    clear_bank();Engine e;setup(e,300,50,false,false);
    arm(e,1);e.stop_bounce();CHECK(e.bounce_snapshot().status==BounceStatus::Cancelled);CHECK(e.bounce_snapshot().frames==0);discard(e);
    auto v=arm(e,1);render(e,(int)(v.start_frame-e.transport_frame)+61);v=e.bounce_snapshot();CHECK(v.frames==61);e.transport_stop();CHECK(e.bounce_snapshot().status==BounceStatus::Partial);v=e.bounce_snapshot();
    q15 copy[122];CHECK(e.read_bounce_frames(v,0,61,copy));CHECK(!e.read_bounce_frames(v,60,2,copy));e.transport_reset();render(e,200);q15 after[122];CHECK(e.read_bounce_frames(v,0,61,after));CHECK(!std::memcmp(copy,after,sizeof copy));discard(e);
    e.transport_play();auto temp=arm(e,1);render(e,(int)(temp.start_frame-e.transport_frame)+7);e.set_bpm(299);render(e,33);CHECK(e.bounce_snapshot().status==BounceStatus::Partial);CHECK(e.bounce_snapshot().frames==7);CHECK(e.bounce_snapshot().reason==BounceReason::TempoChanged);discard(e);
    e.set_bpm(300);arm(e,1);e.beats_per_bar=3;render(e,3);CHECK(e.bounce_snapshot().reason==BounceReason::MeterChanged);CHECK(e.bounce_snapshot().status==BounceStatus::Cancelled);discard(e);e.beats_per_bar=4;
    e.set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1);BouncePlan bad;CHECK(e.plan_bounce(1,bad)==BounceResult::TempoModulated);e.set_clock_mod(ClockTarget::Tempo,ModSource::None,0);e.transport_reset();CHECK(e.plan_bounce(1,bad)==BounceResult::Ok);CHECK(Engine::stage_bounce(bad)==BounceResult::Ok);e.transport_reset();CHECK(e.commit_bounce(bad)==BounceResult::Stale);
    BouncePlan p;CHECK(e.plan_bounce(1,p)==BounceResult::Ok);CHECK(Engine::stage_bounce(p)==BounceResult::Ok);auto& bank=SampleBank::instance();bank.clear_slot(p.slot);CHECK(e.commit_bounce(p)==BounceResult::Stale);
    BouncePlan p2;CHECK(e.plan_bounce(1,p2)==BounceResult::Ok);CHECK(Engine::stage_bounce(p2)==BounceResult::Ok);e.chains[2].units[0]=new PlayUnit();static_cast<PlayUnit*>(e.chains[2].units[0])->sample_slot=p2.slot;CHECK(e.commit_bounce(p2)==BounceResult::Stale);delete e.replace_unit(2,0,nullptr);
    auto changed=arm(e,1);bank.clear_slot(changed.slot);render(e,64);CHECK(e.bounce_snapshot().reason==BounceReason::SampleChanged);discard(e);
    CHECK(e.bounce_blocks_patch_change()==false);auto held=arm(e,1);CHECK(e.bounce_blocks_patch_change());RecUnit* alias=new RecUnit();e.chains[3].units[0]=alias;RecWorkspacePlan rp;CHECK(e.plan_recorder(*alias,false,rp,held.slot)==SampleEditResult::SharedSlot);delete e.replace_unit(3,0,nullptr);discard(e);
    BouncePlan budget;CHECK(e.plan_bounce(1,budget)==BounceResult::Ok);CHECK(Engine::stage_bounce(budget)==BounceResult::Ok);bank.set_pcm_budget_for_test(100);CHECK(e.commit_bounce(budget)==BounceResult::MemoryBudget);bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);
    bank.set_pcm_budget_for_test(100);BouncePlan memory;CHECK(e.plan_bounce(1,memory)==BounceResult::MemoryBudget);bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);
    for(int i=0;i<SAMPLE_BANK_SIZE;++i){Sample sample;sample.data.resize(2);CHECK(bank.replace_sample(i,std::move(sample),SampleOrigin::Runtime)==SampleEditResult::Ok);}BouncePlan full;CHECK(e.plan_bounce(1,full)==BounceResult::NoFreeSlot);
    std::puts("ok lifecycle: cancelled/partial/reset/tempo/meter, stale plans/slots/refs, capacity/no slots");
}
static void export_long(){
    clear_bank();Engine e;setup(e,118,50,false,false);complete(e,8);auto v=e.bounce_snapshot();CHECK(BOUNCE_MAX_FRAMES==524288);CHECK(v.frames==520672);CHECK(SampleBank::instance().pcm_bytes_used()==2082688);
    std::pair<Engine*,BounceSnapshot> context{&e,v};int16_t scratch[1024*2];const char* path="/tmp/seance-bounce-long.wav";
    CHECK(save_wav_stream(path,2,v.frames,32000,scratch,1024,wav_read,&context)==WavSaveResult::Ok);{NoHeap g;CHECK(e.mark_bounce_saved(v,path));}CHECK(e.bounce_snapshot().saved);
    Sample decoded;WavDecodeLimits limits;limits.workspace_bytes=WAV_DECODE_WORKSPACE_BYTES;CHECK(load_wav_to_sample(path,decoded,32000,ENGINE_SR*20,limits)==WavLoadResult::Ok);CHECK(decoded.playable_frames()==v.frames&&decoded.channels==2);const auto& original=e.bounce_sample(v)->data;CHECK(std::equal(decoded.data.begin(),decoded.data.end()-2,original.begin()));CHECK(decoded.data[decoded.data.size()-1]==0&&decoded.data[decoded.data.size()-2]==0);
    FILE* wav=std::fopen(path,"rb");CHECK(wav);CHECK(std::fseek(wav,44,SEEK_SET)==0);std::vector<q15> payload(original.size());CHECK(std::fread(payload.data(),sizeof(q15),payload.size(),wav)==payload.size());CHECK(std::fclose(wav)==0);CHECK(payload==original);
    discard(e);CHECK(!SampleBank::instance().slot(v.slot).has_storage());std::remove(path);
    e.set_bpm(117);BouncePlan too_long;CHECK(e.plan_bounce(8,too_long)==BounceResult::MemoryBudget);e.set_bpm(20);BouncePlan far_too_long;CHECK(e.plan_bounce(8,far_too_long)==BounceResult::MemoryBudget);
    // Independently exercise the exact decoder ceiling with a native-rate stereo
    // fixture; accepted musical takes need not land on precisely this frame count.
    Sample cap;cap.channels=2;cap.data.resize((size_t)BOUNCE_MAX_FRAMES*2);for(size_t i=0;i<cap.data.size();++i)cap.data[i]=(q15)(i%1111);CHECK(save_sample_wav(path,cap,32000)==WavSaveResult::Ok);Sample at_cap;CHECK(load_wav_to_sample(path,at_cap,32000,ENGINE_SR*20,limits)==WavLoadResult::Ok);CHECK(at_cap.playable_frames()==BOUNCE_MAX_FRAMES);std::remove(path);
    std::puts("ok wav: 16.27s eight-bar stereo payload exact + 524288-frame4MiB decoder cap; >cap refused; legacy final-frame zero preserved");
}
static void send_take(){
    for(int bars:{1,2,4,8}){
        clear_bank();Engine e;setup(e,300,50,false,false);complete(e,bars);e.transport_stop();auto token=e.bounce_snapshot();
        BounceSendPlan unsaved;CHECK(e.plan_bounce_send(0,unsaved)==BounceResult::NotSaved);CHECK(e.mark_bounce_saved(token,"/tmp/already-successfully-saved.wav"));
        // All banks have unrelated lane data, selected locks/scenes/routes to remove.
        for(int bank=0;bank<NUM_PATTERNS;++bank){auto& p=e.pattern_banks[bank];for(int c=0;c<4;++c){p.length[c]=uint8_t(11+c);p.steps[c][3].flags=SeqStep::Trig;p.microtiming[c][3]=3;p.steps[c][3].lock_count=1;p.steps[c][3].locks[0]={0,0,123};}}
        e.pattern=e.pattern_banks[0];e.scenes[0][0].valid=true;e.scenes[1][0].valid=true;e.macro_pads[0].route_count=2;e.macro_pads[0].routes[0].chain=0;e.macro_pads[0].routes[1].chain=1;
        Pattern old=e.pattern,oldbanks[NUM_PATTERNS];std::memcpy(oldbanks,e.pattern_banks,sizeof oldbanks);Unit* untouched=e.chains[1].units[0];
        BounceSendPlan send;{NoHeap g;CHECK(e.plan_bounce_send(0,send)==BounceResult::Ok);}CHECK(Engine::stage_bounce_send(send)==BounceResult::Ok);
        CHECK(send.prepared->modern());CHECK(send.prepared->params[PlayUnit::P_MODE].base==0);CHECK(send.prepared->params[PlayUnit::P_LOOP].base==0);
        {NoHeap g;CHECK(e.commit_bounce_send(send)==BounceResult::Ok);}CHECK(send.prepared==nullptr);CHECK(e.chains[1].units[0]==untouched);CHECK(e.scenes[1][0].valid);CHECK(!e.scenes[0][0].valid);CHECK(e.macro_pads[0].route_count==1&&e.macro_pads[0].routes[0].chain==1);
        for(int bank=-1;bank<NUM_PATTERNS;++bank){const auto& p=bank<0?e.pattern:e.pattern_banks[bank];const auto& before=bank<0?old:oldbanks[bank];for(int c=1;c<4;++c){CHECK(p.length[c]==before.length[c]);CHECK(p.direction[c]==before.direction[c]);CHECK(!std::memcmp(p.steps[c],before.steps[c],sizeof p.steps[c]));CHECK(!std::memcmp(p.microtiming[c],before.microtiming[c],sizeof p.microtiming[c]));}CHECK(p.length[0]==(bars==8?64:bars*16));CHECK(p.steps[0][0].condition()==(bars==8?SeqStep::Condition::FirstOfTwo:SeqStep::Condition::Always));for(int s=1;s<64;++s)CHECK(p.steps[0][s].flags==0&&p.steps[0][s].lock_count==0&&p.microtiming[0][s]==0);}
        auto* play=static_cast<PlayUnit*>(e.chains[0].units[0]);CHECK(play->sample_slot==token.slot);CHECK(play_window(SampleBank::instance().slot(token.slot),play->params[0].base,0,32767,0,false,true).increment==65536);
        CHECK(e.sample_slot_refs(token.slot).play==1&&e.sample_slot_refs(token.slot).bounce==1);
        // Actual ordinary sequencer: last quarter of long take still playing;
        // 8-bar FirstOfTwo must not restart it halfway through.
        e.set_sequencer_enabled(true);e.transport_reset();e.transport_play();render(e,64);const uint64_t duration=(uint64_t)e.frames_per_beat()*bars*4;render(e,(int)(duration*3/4));CHECK(play->playback_active());CHECK(play->playback_phase()>0xa0000000u);render(e,(int)(duration/4+e.frames_per_beat()/4+128));CHECK(play->playback_active());CHECK(play->playback_phase()<0x10000000u);
        // Same ordinary persisted representation after SNC7 reload (no binding).
        const char* patch="/tmp/seance-bounce-send.snc";CHECK(patchio::save(e,patch));Engine loaded;patchio::SampleManifest manifest;CHECK(patchio::load(loaded,patch,&manifest));std::remove(patch);auto* lp=static_cast<PlayUnit*>(loaded.chains[0].units[0]);CHECK(lp&&lp->kind==UnitKind::SamplePlayer&&lp->modern());CHECK(lp->sample_slot==token.slot);CHECK(lp->params[PlayUnit::P_MODE].base==0&&lp->params[PlayUnit::P_SPEED].base==8192);CHECK(loaded.pattern.length[0]==e.pattern.length[0]);CHECK(loaded.pattern.steps[0][0].condition()==e.pattern.steps[0][0].condition());CHECK(!std::strcmp(manifest.slots[token.slot].path,"/tmp/already-successfully-saved.wav"));
        loaded.set_sequencer_enabled(true);loaded.transport_reset();loaded.transport_play();render(loaded,(int)((uint64_t)loaded.frames_per_beat()*bars*3));CHECK(lp->playback_active());CHECK(lp->playback_phase()>0x80000000u);
        e.transport_stop();CHECK(e.bounce_sample(token));discard(e);CHECK(e.sample_slot_refs(token.slot).play==1);CHECK(e.sample_slot_refs(token.slot).bounce==0);
    }
    std::puts("ok send: 1/2/4/8 bars ordinary one-shot PLAY, all8 persisted lanes, FirstOfTwo full-duration repeat, other chains intact");
}
static void send_safety(){
    clear_bank();Engine e;setup(e,300,50,false,false);complete(e,1);e.transport_stop();auto take=e.bounce_snapshot();CHECK(e.mark_bounce_saved(take,"/tmp/saved.wav"));
    auto* rec=new RecUnit();e.chains[2].units[0]=rec;CHECK(e.prepare_recorder(*rec)==SampleEditResult::Ok);rec->trigger();q15 input[32]={};rec->capture_input(input,32);rec->stop();CHECK(rec->take_needs_save());BounceSendPlan unsafe;CHECK(e.plan_bounce_send(2,unsafe)==BounceResult::UnsafeChain);rec->mark_take_saved(rec->take_generation());
    BounceSendPlan changed;CHECK(e.plan_bounce_send(2,changed)==BounceResult::Ok);CHECK(Engine::stage_bounce_send(changed)==BounceResult::Ok);rec->clear_take();CHECK(e.commit_bounce_send(changed)==BounceResult::Stale);CHECK(e.chains[2].units[0]==rec);
    BounceSendPlan topology;CHECK(e.plan_bounce_send(0,topology)==BounceResult::Ok);CHECK(Engine::stage_bounce_send(topology)==BounceResult::Ok);Unit* previous=e.replace_unit(0,1,new OscUnit());CHECK(!previous);CHECK(e.commit_bounce_send(topology)==BounceResult::Stale);CHECK(e.chains[0].units[0]==topology.expected[0]);
    BounceSendPlan stopped;CHECK(e.plan_bounce_send(1,stopped)==BounceResult::Ok);CHECK(Engine::stage_bounce_send(stopped)==BounceResult::Ok);e.transport_play();CHECK(e.commit_bounce_send(stopped)==BounceResult::Stale);e.transport_stop();discard(e);CHECK(e.commit_bounce_send(stopped)==BounceResult::Stale);
    std::puts("ok send safety: unsaved REC refusal, stale REC generation/topology/transport/take tokens, no partial replacements");
}
static void chain_sources(){
    // Four distinct pipelines, one take per source; an independent direct-unit
    // oracle has no Engine/mixer/preview state and never reads chain scratch.
    for(int ci=0;ci<NUM_CHAINS;++ci){
        clear_bank();Engine e,twin;setup(e,137,57,ci&1,true);setup(twin,137,57,ci&1,true);
        Sample audition;audition.channels=1;audition.data.resize(257);for(size_t i=0;i<audition.data.size();++i)audition.data[i]=(q15)(int(i%71)*180-6000);
        CHECK(SampleBank::instance().replace_sample(0,std::move(audition),SampleOrigin::Runtime)==SampleEditResult::Ok);
        for(Engine* engine:{&e,&twin}){
            for(int c=0;c<NUM_CHAINS;++c){
                delete engine->replace_unit(c,0,new Tone(4500+c*1700));
                auto* fx=new FoldUnit();fx->params[FoldUnit::P_TRIM].base=23000;for(int p=0;p<fx->num_params;++p)fx->params[p].snap();
                engine->chains[c].units[3]=fx;engine->chains[c].volume=(q15)(6000+c*2100);engine->chains[c].pan=(q15)(c&1?27000:-29000);
            }
            // Private PLAY audition references the selected chain, but this
            // ordinary PLAY is silent and overwritten by its downstream Tone.
            engine->chains[ci].units[1]=engine->chains[ci].units[0];
            auto* play=new PlayUnit();play->sample_slot=0;for(int p=0;p<play->num_params;++p)play->params[p].snap();CHECK(play->bind_edit_source());engine->chains[ci].units[0]=play;
        }
        Tone oracle(4500+ci*1700);FoldUnit fx;fx.params[FoldUnit::P_TRIM].base=23000;for(int p=0;p<fx.num_params;++p)fx.params[p].snap();
        q15 a[1031*2],b[1031*2],mono[1031];{NoHeap g;e.render(a,123);twin.render(b,123);oracle.process(mono,123);fx.process(mono,123);}CHECK(!std::memcmp(a,b,246*sizeof(q15)));
        const auto source=(BounceSource)((int)BounceSource::Chain1+ci);auto v=arm(e,1,source);
        std::vector<q15> expected(v.target_frames);uint64_t cursor=123;int block=0;bool preview=false,muted=false,unmuted=false;
        const int sizes[]={1,17,32,47,513,1031,3,127};
        while(cursor<v.end_frame+71){
            if(!preview&&cursor>=v.start_frame+v.target_frames/2){
                for(Engine* engine:{&e,&twin}){CHECK(engine->start_play_preview(ci,0,0,257,true));engine->set_chain_volume_smooth(ci,0);engine->chains[ci].pan=32767;}
                preview=true;
            }
            if(!muted&&cursor>=v.end_frame-4000){e.chains[ci].mute=twin.chains[ci].mute=true;muted=true;}
            if(!unmuted&&cursor>=v.end_frame-1800){e.chains[ci].mute=twin.chains[ci].mute=false;unmuted=true;}
            const int n=(int)std::min<uint64_t>(sizes[block++%8],v.end_frame+71-cursor);
            {NoHeap g;e.render(a,n);twin.render(b,n);oracle.process(mono,n);fx.process(mono,n);}
            CHECK(!std::memcmp(a,b,(size_t)n*2*sizeof(q15)));twin_pcm+=(uint64_t)n*2;
            const uint64_t begin=std::max(cursor,v.start_frame),end=std::min(cursor+n,v.end_frame);
            for(uint64_t f=begin;f<end;++f)expected[f-v.start_frame]=e.chains[ci].mute?0:mono[f-cursor];
            cursor+=n;
        }
        v=e.bounce_snapshot();const auto* sample=e.bounce_sample(v);CHECK(v.status==BounceStatus::Complete&&v.source==source);CHECK(sample&&sample->channels==1&&sample->playable_frames()==v.target_frames);
        CHECK(std::equal(expected.begin(),expected.end(),sample->data.begin()));chain_pcm+=expected.size();
        CHECK(std::count(expected.begin(),expected.end(),0)>1000);CHECK(std::any_of(expected.begin(),expected.end(),[](q15 x){return x!=0;}));CHECK(preview&&muted&&unmuted);
        for(int c=0;c<NUM_CHAINS;++c){const int slot=c==ci?1:0;const auto* x=static_cast<Tone*>(e.chains[c].units[slot]);const auto* y=static_cast<Tone*>(twin.chains[c].units[slot]);CHECK(x->position==cursor&&x->position==y->position);CHECK(x->calls==y->calls&&x->ticks==y->ticks&&x->fragments==y->fragments);}
        discard(e);
    }
    // Empty selected chain cannot retain the previous fragment or another chain.
    clear_bank();Engine empty,twin;setup(empty,300,50,false,true);setup(twin,300,50,false,true);auto v=arm(empty,1,BounceSource::Chain4);q15 a[514],b[514];
    while(empty.transport_frame<v.end_frame+7){const int n=(int)std::min<uint64_t>(257,v.end_frame+7-empty.transport_frame);{NoHeap g;empty.render(a,n);twin.render(b,n);}CHECK(!std::memcmp(a,b,(size_t)n*2*sizeof(q15)));}
    v=empty.bounce_snapshot();const auto* sample=empty.bounce_sample(v);CHECK(sample&&sample->channels==1);CHECK(std::all_of(sample->data.begin(),sample->data.end(),[](q15 x){return x==0;}));discard(empty);
    std::puts("ok chain sources: 4 exact post-FOLD mono oracles, mute/unmute+empty silence, twin PCM/fragments/ticks unchanged, mixer/master/browser+PLAY preview excluded");
}
static void mono_lifecycle(){
    clear_bank();Engine e;setup(e,300,50,false,false);auto& bank=SampleBank::instance();const auto bad=(BounceSource)255;
    CHECK(BouncePlan{}.source==BounceSource::Master&&BounceSnapshot{}.source==BounceSource::Master);
    CHECK(bounce_channels(BounceSource::Master)==2&&bounce_channels(BounceSource::Chain4)==1&&!bounce_source_valid(bad)&&bounce_channels(bad)==0);
    BouncePlan invalid;CHECK(e.plan_bounce(1,invalid,bad)==BounceResult::Invalid);
    BouncePlan shape;CHECK(e.plan_bounce(1,shape,BounceSource::Chain2)==BounceResult::Ok);shape.source=bad;CHECK(Engine::stage_bounce(shape)==BounceResult::Invalid);CHECK(!shape.staged.has_storage());shape.source=BounceSource::Chain2;CHECK(Engine::stage_bounce(shape)==BounceResult::Ok);
    shape.source=bad;CHECK(e.commit_bounce(shape)==BounceResult::Invalid);shape.source=BounceSource::Master;CHECK(e.commit_bounce(shape)==BounceResult::Invalid);shape.source=BounceSource::Chain2;
    shape.staged.channels=2;CHECK(e.commit_bounce(shape)==BounceResult::Invalid);shape.staged.channels=1;shape.staged.logical_frames=1;CHECK(e.commit_bounce(shape)==BounceResult::Invalid);shape.staged.logical_frames=0;shape.staged.data.push_back(0);CHECK(e.commit_bounce(shape)==BounceResult::Invalid);shape.staged.data.pop_back();
    bank.set_pcm_budget_for_test((size_t)shape.capacity_frames*2-1);CHECK(e.commit_bounce(shape)==BounceResult::MemoryBudget);bank.set_pcm_budget_for_test((size_t)shape.capacity_frames*2);
    BouncePlan stereo;CHECK(e.plan_bounce(1,stereo)==BounceResult::MemoryBudget);
    e.transport_reset();CHECK(e.commit_bounce(shape)==BounceResult::Stale);
    complete(e,1,BounceSource::Chain2);e.transport_stop();const auto v=e.bounce_snapshot();const auto* sample=e.bounce_sample(v);CHECK(sample&&sample->channels==1&&sample->data.size()==v.frames);CHECK(bank.pcm_bytes_used()==(size_t)v.frames*2);
    q15 copy[19];std::fill_n(copy,19,(q15)12345);{NoHeap g;CHECK(e.read_bounce_frames(v,11,17,copy+1));}CHECK(copy[0]==12345&&copy[18]==12345);CHECK(std::equal(copy+1,copy+18,sample->data.begin()+11));CHECK(!e.read_bounce_frames(v,v.frames-1,2,copy));
    for(auto source:{BounceSource::Master,BounceSource::Chain1,bad}){auto forged=v;forged.source=source;CHECK(!e.bounce_sample(forged));CHECK(!e.read_bounce_frames(forged,0,1,copy));CHECK(!e.mark_bounce_saved(forged,"/tmp/forged.wav"));}CHECK(!e.bounce_snapshot().saved);
    const char* path="/tmp/seance-bounce-mono.wav";std::pair<Engine*,BounceSnapshot> context{&e,v};q15 scratch[1024];CHECK(save_wav_stream(path,1,v.frames,32000,scratch,1024,wav_read,&context)==WavSaveResult::Ok);
    FILE* wav=std::fopen(path,"rb");CHECK(wav);unsigned char header[44];CHECK(std::fread(header,1,sizeof header,wav)==sizeof header);CHECK(header[22]==1&&header[23]==0&&header[34]==16);std::vector<q15> raw(v.frames);CHECK(std::fread(raw.data(),sizeof(q15),raw.size(),wav)==raw.size());CHECK(std::fgetc(wav)==EOF);CHECK(std::fclose(wav)==0);CHECK(raw==sample->data);
    WavDecodeLimits limits;limits.workspace_bytes=WAV_DECODE_WORKSPACE_BYTES;Sample decoded;CHECK(load_wav_to_sample(path,decoded,32000,ENGINE_SR*20,limits)==WavLoadResult::Ok);CHECK(decoded.channels==1&&decoded.playable_frames()==v.frames);CHECK(std::equal(decoded.data.begin(),decoded.data.end()-1,raw.begin()));CHECK(decoded.data.back()==0);CHECK(e.mark_bounce_saved(v,path));
    BounceSendPlan send;CHECK(e.plan_bounce_send(3,send)==BounceResult::Ok);send.take.source=bad;CHECK(Engine::stage_bounce_send(send)==BounceResult::Invalid);send.take.source=v.source;CHECK(Engine::stage_bounce_send(send)==BounceResult::Ok);send.take.source=BounceSource::Chain1;CHECK(e.commit_bounce_send(send)==BounceResult::Stale);CHECK(!e.chains[3].units[0]);send.take.source=v.source;
    {NoHeap g;CHECK(e.commit_bounce_send(send)==BounceResult::Ok);}auto* play=static_cast<PlayUnit*>(e.chains[3].units[0]);CHECK(play&&play->sample_slot==v.slot&&play->params[PlayUnit::P_SPEED].base==8192&&play->params[PlayUnit::P_MODE].base==0);CHECK(e.sample_slot_refs(v.slot).bounce==1&&e.sample_slot_refs(v.slot).play==1);
    discard(e);CHECK(!e.bounce_sample(v));CHECK(!e.read_bounce_frames(v,0,1,copy));CHECK(!e.mark_bounce_saved(v,path));CHECK(bank.slot(v.slot).channels==1&&bank.slot(v.slot).data==raw);CHECK(e.sample_slot_refs(v.slot).bounce==0&&e.sample_slot_refs(v.slot).play==1);
    play->trigger();q15 audio[64]={};{NoHeap g;play->process(audio,64);}CHECK(play->playback_active());CHECK(std::any_of(audio,audio+64,[](q15 x){return x!=0;}));delete e.replace_unit(3,0,nullptr);bank.clear_slot(v.slot);std::remove(path);
    bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);e.set_bpm(117);BouncePlan cap;CHECK(e.plan_bounce(8,cap,BounceSource::Chain2)==BounceResult::MemoryBudget);e.set_bpm(118);CHECK(e.plan_bounce(8,cap,BounceSource::Chain2)==BounceResult::Ok);CHECK(cap.capacity_frames==520672&&BOUNCE_MAX_FRAMES==524288);
    e.transport_play();complete(e,1,BounceSource::Chain2);const auto stale=e.bounce_snapshot();bank.clear_slot(stale.slot);CHECK(!e.bounce_sample(stale)&&e.bounce_snapshot().reason==BounceReason::SampleChanged);discard(e);
    std::puts("ok mono lifecycle: half-size admission/storage, packed guarded read+exact mono WAV, legacy decode edge, ordinary PLAY sharing, invalid source/geometry/token and stale protection, unchanged frame cap");
}
static void quick_start(){
    for(int phase=0;phase<3;++phase){
        clear_bank();Engine e,twin;setup(e,137,57,phase!=0,false);setup(twin,137,57,phase!=0,false);
        render(e,123);render(twin,123);e.transport_stop();twin.transport_stop();
        // A removed route leaves the applied Q32 domain active until reset.
        if(phase==2){e.set_clock_mod(ClockTarget::Swing,ModSource::None,0);twin.set_clock_mod(ClockTarget::Swing,ModSource::None,0);}
        BouncePlan p;{NoHeap g;CHECK(e.plan_bounce(1,p)==BounceResult::Ok);}CHECK(Engine::stage_bounce(p)==BounceResult::Ok);
        {NoHeap g;CHECK(e.commit_bounce(p,true)==BounceResult::Ok);twin.transport_reset();twin.transport_play();}
        const auto v=e.bounce_snapshot();CHECK(e.transport_playing&&e.transport_frame==0&&e.beat_count==0&&e.frame_in_beat==0);
        const uint64_t inc=((137ull*65536*65536)+32000u*30)/(32000u*60);
        const uint64_t length=phase?((4ull<<32)+inc-1)/inc:4ull*((32000u*60u+137u/2)/137u);
        CHECK(v.start_frame==0&&v.end_frame==length&&v.target_frames==length);
        q15 a[514],b[514];std::vector<q15> expected((size_t)length*2);
        {NoHeap g;e.render(a,1);twin.render(b,1);}CHECK(!std::memcmp(a,b,2*sizeof(q15)));CHECK(e.bounce_snapshot().frames==1);
        expected[0]=a[0];expected[1]=a[1];
        uint32_t done=1;while(done<length){const int n=(int)std::min<uint64_t>(257,length-done);{NoHeap g;e.render(a,n);twin.render(b,n);}CHECK(!std::memcmp(a,b,(size_t)n*2*sizeof(q15)));std::memcpy(expected.data()+done*2,a,(size_t)n*2*sizeof(q15));done+=n;}
        CHECK(e.bounce_snapshot().status==BounceStatus::Complete);CHECK(e.bounce_sample(e.bounce_snapshot())->data==expected);discard(e);
        // Same opt-in while playing must not reset or capture the current bar.
        const uint64_t before=e.transport_frame;BouncePlan running;CHECK(e.plan_bounce(1,running)==BounceResult::Ok);CHECK(Engine::stage_bounce(running)==BounceResult::Ok);
        {NoHeap g;CHECK(e.commit_bounce(running,true)==BounceResult::Ok);}CHECK(e.transport_playing&&e.transport_frame==before);CHECK(e.bounce_snapshot().start_frame>before);discard(e);
    }
    clear_bank();Engine legacy;setup(legacy,300,50,false,false);legacy.transport_stop();legacy.transport_reset();
    const auto armed=arm(legacy,1);CHECK(!legacy.transport_playing&&armed.start_frame==4ull*legacy.frames_per_beat());CHECK(!legacy.start_bounce_preview());
    render(legacy,37);CHECK(legacy.bounce_snapshot().frames==0);legacy.transport_play();render(legacy,(int)armed.start_frame);CHECK(legacy.bounce_snapshot().frames==0);render(legacy,1);CHECK(legacy.bounce_snapshot().frames==1);discard(legacy);
    std::puts("ok quick start: first-frame capture legacy/appliedQ32/removed-routeQ32, exact lengths+PCM, running next-bar and default stopped pre-roll retained");
}
static void quick_start_failures(){
    clear_bank();Engine e;setup(e,300,50,false,false);render(e,149);e.transport_stop();
    auto stable=[&](){CHECK(!e.transport_playing&&e.transport_frame==149&&e.beat_count==0&&e.frame_in_beat==149);CHECK(e.bounce_snapshot().slot<0);};
    BouncePlan invalid;{NoHeap g;CHECK(e.plan_bounce(3,invalid)==BounceResult::Invalid);CHECK(Engine::stage_bounce(invalid)==BounceResult::Invalid);CHECK(e.commit_bounce(invalid,true)==BounceResult::Stale);}stable();
    BouncePlan p;CHECK(e.plan_bounce(1,p)==BounceResult::Ok);{NoHeap g;CHECK(e.commit_bounce(p,true)==BounceResult::Invalid);}stable();CHECK(Engine::stage_bounce(p)==BounceResult::Ok);
    p.staged.logical_frames=1;{NoHeap g;CHECK(e.commit_bounce(p,true)==BounceResult::Invalid);}stable();p.staged.logical_frames=0;
    auto& bank=SampleBank::instance();bank.set_pcm_budget_for_test(1);{NoHeap g;CHECK(e.commit_bounce(p,true)==BounceResult::MemoryBudget);}stable();bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);
    bank.clear_slot(p.slot);{NoHeap g;CHECK(e.commit_bounce(p,true)==BounceResult::Stale);}stable();
    e.set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1);BouncePlan mod;{NoHeap g;CHECK(e.plan_bounce(1,mod)==BounceResult::TempoModulated);CHECK(e.commit_bounce(p,true)==BounceResult::TempoModulated);}CHECK(!e.transport_playing&&e.transport_frame==149);
    std::puts("ok quick start failures: invalid plan/stage/storage, stale generation, budget and modulated tempo never reset/start transport");
}
static void exact_preview(){
    for(auto source:{BounceSource::Master,BounceSource::Chain1})for(bool partial:{false,true}){
        clear_bank();Engine e;setup(e,300,50,false,true);
        const auto armed=arm(e,1,source);render(e,(int)(armed.start_frame-e.transport_frame)+(partial?61:(int)armed.target_frames));CHECK(!e.start_bounce_preview());e.transport_stop();
        const auto take=e.bounce_snapshot();CHECK(take.status==(partial?BounceStatus::Partial:BounceStatus::Complete));
        const auto* sample=e.bounce_sample(take);CHECK(sample);const q15* storage=sample->data.data();std::vector<q15> original=sample->data;
        // Nonunity master controls and continuous project tones remain active:
        // audition must return raw capture, not their mix or a second FX pass.
        e.apply_global_fx(GlobalFxTarget::Crush,27000);
        Pattern pattern=e.pattern,banks[NUM_PATTERNS];std::memcpy(banks,e.pattern_banks,sizeof banks);
        Unit* units[NUM_CHAINS][UNITS_PER_CHAIN];q15 volumes[NUM_CHAINS],pans[NUM_CHAINS];bool mutes[NUM_CHAINS];
        for(int c=0;c<NUM_CHAINS;++c){std::copy(std::begin(e.chains[c].units),std::end(e.chains[c].units),units[c]);volumes[c]=e.chains[c].volume;pans[c]=e.chains[c].pan;mutes[c]=e.chains[c].mute;}
        const auto transport=e.transport_frame;const auto tick=e.sequence_tick;const auto used=SampleBank::instance().pcm_bytes_used();
        {NoHeap g;CHECK(e.start_bounce_preview());CHECK(e.bounce_preview_active());CHECK(e.bounce_preview_position()==0);CHECK(!e.sample_preview_active());}
        uint32_t position=0;q15 out[513*2];while(position<take.frames){
            const uint32_t remaining=take.frames-position;const int n=(int)std::min<uint32_t>(513,remaining+37);
            {NoHeap g;e.render(out,n);}
            for(int i=0;i<n;++i){const uint32_t f=position+(uint32_t)i;for(int ch=0;ch<2;++ch)CHECK(out[i*2+ch]==(f<take.frames?original[(size_t)f*sample->channels+(sample->channels==2?ch:0)]:0));}
            position+=std::min<uint32_t>((uint32_t)n,remaining);CHECK(e.bounce_preview_position()==position);CHECK(e.bounce_preview_active()==(position<take.frames));
        }
        CHECK(!e.bounce_preview_active()&&e.bounce_preview_position()==take.frames);
        CHECK(e.bounce_sample(take)==sample&&sample->data.data()==storage&&sample->data==original);CHECK(SampleBank::instance().pcm_bytes_used()==used);
        CHECK(e.transport_frame==transport&&e.sequence_tick==tick&&!e.transport_playing);CHECK(!std::memcmp(&e.pattern,&pattern,sizeof pattern));CHECK(!std::memcmp(e.pattern_banks,banks,sizeof banks));
        for(int c=0;c<NUM_CHAINS;++c){CHECK(std::equal(std::begin(e.chains[c].units),std::end(e.chains[c].units),units[c]));CHECK(e.chains[c].volume==volumes[c]&&e.chains[c].pan==pans[c]&&e.chains[c].mute==mutes[c]);}
        {NoHeap g;e.render(out,32);}CHECK(std::any_of(out,out+64,[](q15 x){return x!=0;})); // no hidden permanent solo
        {NoHeap g;CHECK(e.start_bounce_preview());e.render(out,17);CHECK(e.bounce_preview_position()==17);e.stop_bounce_preview();CHECK(!e.bounce_preview_active()&&e.bounce_preview_position()==0);}
        for(int i=0;i<17;++i)for(int ch=0;ch<2;++ch)CHECK(out[i*2+ch]==original[(size_t)i*sample->channels+(sample->channels==2?ch:0)]);
        CHECK(sample->data==original);discard(e);
    }
    std::puts("ok exact preview: complete/partial raw stereo and mono-duplicated PCM, one-shot end/restart/stop, no extra FX, PCM/storage/topology/rhythm intact, zero guarded heap");
}
static void preview_lifecycle(){
    clear_bank();Engine e;setup(e,300,50,false,false);CHECK(!e.start_bounce_preview());
    auto armed=arm(e,1,BounceSource::Chain1);CHECK(!e.start_bounce_preview());render(e,(int)(armed.start_frame-e.transport_frame)+43);e.transport_stop();const auto take=e.bounce_snapshot();
    {NoHeap g;CHECK(e.start_bounce_preview());e.transport_play();CHECK(!e.bounce_preview_active()&&e.bounce_preview_position()==0);CHECK(!e.start_bounce_preview());e.transport_stop();CHECK(e.start_bounce_preview());e.transport_reset();CHECK(!e.bounce_preview_active());}
    Sample browser;browser.data.assign(101,777);{NoHeap g;CHECK(e.start_bounce_preview());e.swap_sample_preview(browser);CHECK(e.sample_preview_active()&&!e.bounce_preview_active());CHECK(e.start_bounce_preview());CHECK(!e.sample_preview_active());}
    auto* play=new PlayUnit();play->sample_slot=take.slot;for(int p=0;p<play->num_params;++p)play->params[p].snap();CHECK(play->bind_edit_source());e.chains[3].units[0]=play;
    uint32_t token=0;{NoHeap g;token=e.start_play_preview(3,0,0,take.frames,true);CHECK(token&&e.play_preview_active(token)&&!e.bounce_preview_active());CHECK(e.start_bounce_preview());CHECK(!e.play_preview_active(token));CHECK(e.mark_bounce_saved(take,"/tmp/preview-test-already-saved.wav"));CHECK(e.bounce_preview_active());}
    // Explicit discard invalidates audition even if a PLAY reference retains PCM.
    discard(e);CHECK(!e.bounce_preview_active()&&e.bounce_preview_position()==0&&!e.start_bounce_preview());CHECK(SampleBank::instance().slot(take.slot).has_storage());
    delete e.replace_unit(3,0,nullptr);SampleBank::instance().clear_slot(take.slot);
    e.transport_play();auto next=arm(e,1);render(e,(int)(next.start_frame-e.transport_frame)+29);e.transport_stop();next=e.bounce_snapshot();CHECK(next.generation!=take.generation);CHECK(!e.bounce_preview_active());CHECK(e.start_bounce_preview());
    // Replacement can reuse the address/slot: generation, not a cached pointer,
    // must prevent reading it as the held take, before and during render.
    Sample replacement;replacement.channels=2;replacement.data.assign(58,2345);CHECK(SampleBank::instance().replace_sample(next.slot,std::move(replacement),SampleOrigin::Runtime)==SampleEditResult::Ok);
    CHECK(!e.bounce_preview_active()&&e.bounce_preview_position()==0);render(e,17);CHECK(!e.start_bounce_preview());discard(e);CHECK(SampleBank::instance().slot(next.slot).data.front()==2345);
    e.transport_play();complete(e,1);e.transport_stop();CHECK(e.start_bounce_preview());Engine other;e.swap_patch_state(other);CHECK(!e.bounce_preview_active()&&!other.bounce_preview_active());discard(e);
    std::puts("ok preview lifetime: stopped/immutable-only, start/reset/project swap, browser+PLAY exclusion, saved hold, discard/shared slot, stale replacement generations");
}
int main(){exact_capture();lifecycle();export_long();send_take();send_safety();chain_sources();mono_lifecycle();quick_start();quick_start_failures();exact_preview();preview_lifecycle();clear_bank();CHECK(heap_violations==0);std::printf("bounce core: 11/11 checks=%llu twin_pcm=%llu captured_pcm=%llu chain_pcm=%llu audio_heap=%u sizes=%zu/%zu/%zu/%zu\n",(unsigned long long)checks,(unsigned long long)twin_pcm,(unsigned long long)captured_pcm,(unsigned long long)chain_pcm,heap_violations,sizeof(BounceSnapshot),sizeof(BounceState),sizeof(BouncePlan),sizeof(BounceSendPlan));}
