// Focused real PLAY editor model/DSP/persistence proof. No UI, PCM export or device.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "ui/play_editor_tools.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <unistd.h>
using namespace seance;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
using Bytes = std::vector<uint8_t>;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static void set(PlayUnit& p, int i, q15 v) { p.params[i].base = v; p.params[i].snap(); }
static void sample(int slot=5, uint32_t count=100003, bool stereo=false) {
    Sample s; s.channels=stereo?2:1; s.data.resize((size_t)count*s.channels);
    for(uint32_t i=0;i<count;++i) for(int c=0;c<s.channels;++c) s.data[(size_t)i*s.channels+c]=(q15)(10000+(i%997));
    CHECK(SampleBank::instance().replace_sample(slot,std::move(s),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
}
static PlayEditState state(uint32_t n=100003) {
    PlayEditState e; e.enabled=true; e.source_frames=n; e.start=1001; e.end=n-1001;
    e.fade_in=123; e.fade_out=321; e.loop_start=2003; e.loop_end=n-2003; e.crossfade=71;
    return e;
}
static void apply(PlayUnit& p, const PlayEditState& e) {
    CHECK(p.set_edit_state(e,SampleBank::instance().generation(p.sample_slot)));
}
static void projection() {
    static_assert(std::is_trivially_copyable<PlayEditState>::value,"bounded capture");
    auto e=state(); CHECK(play_edit_valid(e));
    auto w=play_edit_window(e.source_frames,nullptr,0,8192,0,32767,0,false,true,e);
    CHECK(w.valid&&w.start==1001&&w.end==99002&&w.loop_start==2003&&w.crossfade==71);
    auto one=e; ++one.start; ++one.loop_start;
    auto n=play_edit_window(e.source_frames,nullptr,0,8192,0,32767,0,false,true,one);
    CHECK(n.start==w.start+1&&n.end==w.end); // one frame, not one Q15 tick
    n=play_edit_window(e.source_frames,nullptr,0,8192,1024,30000,0,false,true,e);
    CHECK(n.start==1001+(uint64_t)1024*(e.source_frames-1)/32767);
    CHECK(n.end-n.start==(e.end-e.start)-(uint64_t)(32767-30000)*e.source_frames/32767);
    uint32_t cuts[Sample::MAX_CHOPS]; std::fill_n(cuts,Sample::MAX_CHOPS,UINT32_MAX);
    cuts[0]=0; cuts[7]=5000; cuts[3]=8000;
    const q15 cut8=(q15)((8u*32768+16)/17);
    w=play_edit_window(e.source_frames,cuts,1u<<7,8192,0,32767,cut8,false,true,e);
    CHECK(w.valid&&w.slice==8&&w.start==5000&&w.end==8000&&w.increment<0);
    cuts[7]=UINT32_MAX;
    CHECK(!play_edit_window(e.source_frames,cuts,0,8192,0,32767,cut8,false,true,e).valid);
    CHECK(!play_edit_window(e.source_frames+1,nullptr,0,8192,0,32767,0,false,true,e).valid);
    CHECK(!play_edit_window(e.source_frames,nullptr,0,8192,0,32767,0,false,false,e).valid);
    for(int field=0;field<8;++field) { auto bad=e;
        if(field==0) bad.source_frames=0;
        if(field==1) bad.start=bad.end;
        if(field==2) bad.end=UINT32_MAX;
        if(field==3) bad.fade_in=UINT32_MAX;
        if(field==4) bad.fade_out=UINT32_MAX;
        if(field==5) bad.loop_start=0;
        if(field==6) bad.loop_end=UINT32_MAX;
        if(field==7) bad.crossfade=UINT32_MAX;
        CHECK(!play_edit_valid(bad));
    }
    // Metadata-only extreme source projection cannot wrap intermediate arithmetic.
    e.source_frames=UINT32_MAX; e.start=UINT32_MAX-1000; e.end=UINT32_MAX;
    e.loop_start=e.start; e.loop_end=e.end; e.anchor_start=32000;
    w=play_edit_window(UINT32_MAX,nullptr,0,-8192,32767,32767,0,false,true,e);
    CHECK(w.valid&&w.end==UINT32_MAX&&w.end-w.start>=2);
    std::puts("ok exact/Q15 projection, stable slices, strict validation and wide arithmetic");
}
static void dsp_and_binding() {
    sample(); auto e=state(); PlayUnit p; p.sample_slot=5; apply(p,e);
    Param before[PARAM_BANK_SIZE];
    set(p,PlayUnit::P_MODE,0);set(p,PlayUnit::P_LOOP,32767);p.control_tick();p.trigger();
    q15 out;
    p.process(&out,1);CHECK(p.visual_source_frame()==e.start); // attack, not loop-in
    for(uint32_t i=1;i<e.loop_end-e.start+300;++i) {
        p.process(&out,1);CHECK(p.playback_active());
        if(i>=e.loop_end-e.start)CHECK(p.visual_source_frame()>=e.loop_start+e.crossfade&&p.visual_source_frame()<e.loop_end);
    }
    set(p,PlayUnit::P_SPEED,-8192);p.control_tick();p.trigger();p.process(&out,1);
    CHECK(p.visual_source_frame()==e.end-1);
    for(uint32_t i=1;i<e.end-e.loop_start+300;++i) {
        p.process(&out,1);CHECK(p.playback_active());
        if(i>=e.end-e.loop_start)CHECK(p.visual_source_frame()>=e.loop_start&&p.visual_source_frame()<e.loop_end-e.crossfade);
    }
    // Independent loop survives trim edit until next trigger. Two different
    // render partitions must produce exactly the same PCM, including seams.
    for(int speed:{-32000,-8192,9191,32767}) {
        PlayUnit a,b; a.sample_slot=b.sample_slot=5;apply(a,e);apply(b,e);
        for(auto* u:{&a,&b}){set(*u,0,(q15)speed);set(*u,3,32767);u->control_tick();u->trigger();}
        std::vector<q15>x(130000),y(x.size());a.process(x.data(),(int)x.size());
        size_t done=0;while(done<y.size()){int n=(int)std::min<size_t>(1+done%129,y.size()-done);b.process(y.data()+done,n);done+=n;}
        CHECK(x==y);
    }
    // Repeat is preview-only. No musical Param/lock/TEMP bytes are touched.
    std::memcpy(before,p.params,sizeof before);CHECK(p.preview_frames(101,401,true,SampleBank::instance().generation(5)));
    q15 block[1024];p.process(block,1024);CHECK(p.playback_active());
    CHECK(!std::memcmp(before,p.params,sizeof before));
    p.release();p.process(block,64);CHECK(!p.playback_active());
    CHECK(!p.preview_frames(101,401,true,SampleBank::instance().generation(5)-1));
    CHECK(p.preview_frames(101,401,false,SampleBank::instance().generation(5)));
    p.process(block,301);CHECK(!p.playback_active());
    CHECK(!std::memcmp(before,p.params,sizeof before));
    // Same-size replacement invalidates both exact state and an active preview.
    CHECK(p.preview_frames(101,401,true,SampleBank::instance().generation(5)));
    sample();CHECK(!p.edit_source_matches());p.process(block,1024);CHECK(!p.playback_active());
    for(auto v:block)CHECK(v==0);
    CHECK(!p.projected_window(0,32767,0).valid);
    p.trigger();p.process(block,1024);CHECK(!p.playback_active());
    CHECK(p.bind_edit_source());CHECK(p.edit_source_matches()); // explicit user/hydration authorization
    // Slot identity and logical growth also invalidate, even with unchanged PCM.
    p.sample_slot=6;CHECK(!p.edit_source_matches());p.sample_slot=5;
    auto& s=SampleBank::instance().slot(5);s.logical_frames=90000;CHECK(!p.edit_source_matches());
    s.logical_frames=UINT32_MAX;
    // Fades and final-frame interpolation on the opt-in one-shot path.
    e.start=101;e.end=501;e.loop_start=101;e.loop_end=501;e.fade_in=100;e.fade_out=100;e.crossfade=0;
    apply(p,e);set(p,0,8192);set(p,3,0);p.trigger();p.process(block,401);
    CHECK(!p.playback_active()&&block[0]==0&&block[399]==0&&block[200]>9000);
    CHECK(block[50]>0&&block[50]<block[200]);
    set(p,0,-8192);p.trigger();p.process(block,401);
    CHECK(!p.playback_active()&&block[0]==0&&block[399]==0&&block[200]>9000);
    sample(5,100003,true);apply(p,e);e.start=499;e.end=501;e.loop_start=499;e.loop_end=501;
    e.fade_in=e.fade_out=0;apply(p,e);p.trigger();p.process(block,3);CHECK(!p.playback_active());
    CHECK(p.visual_source_frame()==499&&block[0]!=0&&block[1]!=0&&block[2]==0);
    std::puts("ok exact attack/loop/reverse/fades/final frame, partition PCM, transient preview and source identity guards");
}
static uint32_t r32(const Bytes& b,size_t p){CHECK(p+4<=b.size());return b[p]|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24;}
static void w32(Bytes& b,size_t p,uint32_t v){CHECK(p+4<=b.size());for(int i=0;i<4;++i)b[p+i]=(uint8_t)(v>>(8*i));}
static size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;return b.size();}
static Bytes read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void put(const std::string& path,const Bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
static void persistence() {
    sample();const auto e=state();auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    auto* p=new PlayUnit;p->sample_slot=5;apply(*p,e);delete a->replace_unit(0,0,p);
    auto* q=new PlayUnit;q->sample_slot=5;auto other=e;other.start+=7;apply(*q,other);delete a->replace_unit(3,5,q);
    CHECK(a->set_step_lock(0,0,0,PlayUnit::P_START,234));a->capture_scene(0,0);
    const std::string path="/tmp/seance-play-editor-"+std::to_string((long)getpid())+".snc";
    CHECK(patchio::save(*a,path.c_str()));const Bytes good=read(path);const size_t at=chunk(good,"PEDT");
    CHECK(at<good.size()&&r32(good,at+4)==80);
    CHECK(patchio::load(*b,path.c_str()));auto* loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);
    CHECK(play_edit_equal(loaded->edit_state(),e)&&!loaded->edit_source_matches());
    CHECK(!loaded->projected_window(0,32767,0).valid);
    for(auto& ch:b->chains)for(auto* u:ch.units)if(u&&u->kind==UnitKind::SamplePlayer)CHECK(static_cast<PlayUnit*>(u)->bind_edit_source());
    CHECK(patchio::save(*b,path.c_str()));CHECK(read(path)==good);
    std::unique_ptr<Unit> clone(clone_unit(loaded));auto* cp=static_cast<PlayUnit*>(clone.get());
    CHECK(play_edit_equal(cp->edit_state(),e)&&cp->edit_source_matches());
    CHECK(b->pattern.steps[0][0].locks[0].value==234&&b->scenes[0][0].base[0][1]==0);
    // PEDT+UNIT before CORE, and PEDT after all other chunks, produce same state.
    for(bool first:{false,true}) {
        Bytes reordered=good, pedt(good.begin()+at,good.begin()+at+88);
        reordered.erase(reordered.begin()+at,reordered.begin()+at+88);
        if(first){size_t up=chunk(reordered,"UNIT");Bytes unit(reordered.begin()+up,reordered.begin()+up+32);reordered.erase(reordered.begin()+up,reordered.begin()+up+32);reordered.insert(reordered.begin()+5,unit.begin(),unit.end());reordered.insert(reordered.begin()+5,pedt.begin(),pedt.end());}
        else reordered.insert(reordered.end(),pedt.begin(),pedt.end());
        put(path,reordered);CHECK(patchio::load(*b,path.c_str()));loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(play_edit_equal(loaded->edit_state(),e));
    }
    auto reject=[&](Bytes bad){put(path,bad);auto* sentinel=b->chains[0].units[0];CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[0].units[0]==sentinel);CHECK(play_edit_equal(static_cast<PlayUnit*>(sentinel)->edit_state(),e));};
    // Optional source endpoint policy is strict and transactional as well.
    const size_t sx=chunk(good,"SEXT");CHECK(sx<good.size());
    {auto bad=good;bad[sx+8]=2;reject(bad);}
    {auto bad=good;bad[sx+9]=63;reject(bad);}
    {auto bad=good;bad[sx+10]=2;reject(bad);}
    {auto bad=good;Bytes copy(good.begin()+sx,good.begin()+sx+74);bad.insert(bad.end(),copy.begin(),copy.end());reject(bad);}
    // All record fields, targeting, malformed sizes/versions and duplicate cases.
    for(int field=0;field<8;++field){auto bad=good;w32(bad,at+13+4*field,field==0?0:UINT32_MAX);reject(bad);}
    for(int field=0;field<3;++field){auto bad=good;bad[at+10+field]=255;reject(bad);}
    {auto bad=good;bad[at+12]=4;reject(bad);} // mismatched source slot
    {auto bad=good;bad[chunk(bad,"CORE")+15]=69;reject(bad);} // raw out-of-range slot may not modulo-match PEDT
    {auto bad=good;bad[at+10]=1;reject(bad);} // empty target card
    {auto bad=good;bad[chunk(bad,"UNIT")+8]=0;reject(bad);} // legacy target
    {auto bad=good;bad[at+8]=2;reject(bad);}
    {auto bad=good;bad[at+9]=0;reject(bad);}
    {auto bad=good;bad[at+45]=255;bad[at+46]=255;reject(bad);} // signed Q15 anchor
    {auto bad=good;bad[at+49]=0;bad[at+50]=0;reject(bad);} // duplicate card record
    {auto bad=good;Bytes copy(good.begin()+at,good.begin()+at+88);bad.insert(bad.end(),copy.begin(),copy.end());reject(bad);}
    {auto bad=good;w32(bad,at+4,81);reject(bad);}
    {auto bad=good;bad.resize(at+30);reject(bad);}
    // Removing the optional chunk leaves existing modern params and old audio opt-out.
    {auto absent=good;absent.erase(absent.begin()+at,absent.begin()+at+88);put(path,absent);CHECK(patchio::load(*b,path.c_str()));CHECK(!static_cast<PlayUnit*>(b->chains[0].units[0])->edit_state().enabled);}
    sample();CHECK(!patchio::capture(*a,*std::make_unique<patchio::PatchDocument>()));
    std::remove(path.c_str());std::puts("ok exact per-card roundtrip/clone, reordered optional PEDT, transactional malformed/context/duplicate rejection");
}
static void hydration() {
    sample();auto&bank=SampleBank::instance();const auto edit=state();
    bank.set_provenance(5,trackr::synth::SampleOrigin::SdWav,"/fixture/original.wav");
    bank.slot(5).chops[3]=1234;bank.slot(5).slice_reverse_mask=1u<<3;
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    auto*p=new PlayUnit;p->sample_slot=5;apply(*p,edit);delete a->replace_unit(0,0,p);
    const std::string path="/tmp/seance-play-hydration-"+std::to_string((long)getpid())+".snc";
    CHECK(patchio::save(*a,path.c_str()));const Bytes original=read(path);
    auto manifest=std::make_unique<patchio::SampleManifest>();
    CHECK(patchio::load(*b,path.c_str(),manifest.get()));
    p=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(p->pending_edit_source()&&!p->edit_source_matches());
    // Metadata-only staged save must not associate another project's live file/cuts.
    bank.set_provenance(5,trackr::synth::SampleOrigin::SdWav,"/fixture/unrelated.wav");
    bank.slot(5).chops[3]=7777;bank.slot(5).slice_reverse_mask=0;
    CHECK(patchio::save(*b,path.c_str()));CHECK(read(path)==original);
    auto restored=std::make_unique<patchio::RestoredSamples>();
    restored->decoded[5].data.assign(edit.source_frames,10000);restored->ready[5]=true;
    const uint32_t before=bank.generation(5);
    const size_t budget=bank.pcm_budget_bytes();bank.set_pcm_budget_for_test(1);
    CHECK(!patchio::commit_restored_samples(*manifest,*restored,b.get()));
    CHECK(bank.generation(5)==before&&!p->edit_source_matches());bank.set_pcm_budget_for_test(budget);
    CHECK(patchio::commit_restored_samples(*manifest,*restored,b.get()));
    CHECK(p->edit_source_matches()&&!p->pending_edit_source());
    CHECK(p->projected_window(0,32767,0).start==edit.start&&bank.slot(5).chops[3]==1234);
    // Missing hydration empties the bank but retains source path/cuts/exact frames.
    put(path,original);CHECK(patchio::load(*b,path.c_str(),manifest.get()));p=static_cast<PlayUnit*>(b->chains[0].units[0]);
    auto absent=std::make_unique<patchio::RestoredSamples>();manifest->slots[5].missing=true;
    CHECK(patchio::commit_restored_samples(*manifest,*absent,b.get()));
    CHECK(bank.slot(5).empty()&&!p->edit_source_matches()&&p->pending_edit_source());
    CHECK(patchio::save(*b,path.c_str()));
    auto c=std::make_unique<Engine>();auto saved=std::make_unique<patchio::SampleManifest>();
    CHECK(patchio::load(*c,path.c_str(),saved.get()));
    CHECK(play_edit_equal(static_cast<PlayUnit*>(c->chains[0].units[0])->edit_state(),edit));
    CHECK(saved->slots[5].missing&&!std::strcmp(saved->slots[5].path,"/fixture/original.wav")&&saved->chops[5][3]==1234&&saved->slice_reverse[5]==(1u<<3));
    p->sample_slot=6;CHECK(!patchio::save(*b,path.c_str()));p->sample_slot=5;
    sample();CHECK(!p->pending_edit_source()&&!patchio::save(*b,path.c_str()));
    // Explicit preview reverse is latched independently of base SLCE and survives
    // control ticks. A normal trigger releases preview ownership.
    PlayUnit preview;preview.sample_slot=5;set(preview,0,8192);
    CHECK(preview.preview_frames(101,401,true,bank.generation(5),true));
    q15 out;preview.process(&out,1);CHECK(preview.preview_active()&&preview.visual_source_frame()==400);
    preview.control_tick();preview.process(&out,1);CHECK(preview.visual_source_frame()==399);
    preview.trigger();CHECK(!preview.preview_active());
    std::remove(path.c_str());std::puts("ok hydration binds only committed sources; staged/missing metadata roundtrip without unrelated PCM; explicit preview direction");
}
static q15 slice_value(int id) { return (q15)(((id+1)*32768+16)/17); }
static void rates_and_scrub() {
    namespace pe = seance::play_editor;
    pe::Cuts cuts=pe::Cuts::equal(4,10000);cuts.speed_q8[1]=512;cuts.reverse=2;
    auto changed=cuts;changed.speed_q8[1]=128;CHECK(changed!=cuts);
    CHECK(cuts.move(1,3000,10000)&&cuts.speed_q8[1]==512);
    const int inserted=cuts.insert(4000,10000);CHECK(inserted==4&&cuts.speed_q8[4]==256);
    cuts.speed_q8[4]=1024;CHECK(cuts.merge(4,10000)==1&&cuts.speed_q8[4]==256&&cuts.speed_q8[1]==512);
    for (auto rate : pe::Cuts::equal(8,10000).speed_q8) CHECK(rate==256);
    sample(5,12000,true);auto& bank=SampleBank::instance();auto& s=bank.slot(5);
    s.chops[0]=0;s.chops[7]=4000;s.chops[3]=8000;
    s.slice_speed_q8[7]=512;s.slice_speed_q8[3]=64;s.slice_reverse_mask=1u<<7;
    auto w=play_window(s,8192,0,32767,slice_value(7),false,true);
    CHECK(w.valid&&w.slice==8&&w.increment==-131072);
    CHECK(play_window(s,8192,0,32767,slice_value(7),false,false).increment==-65536);
    PlayUnit p;p.sample_slot=5;set(p,PlayUnit::P_SLICE,slice_value(7));p.control_tick();p.trigger();
    q15 pcm[4096];p.process(pcm,100);CHECK(p.visual_source_frame()==7798);
    set(p,PlayUnit::P_SLICE,slice_value(3));p.control_tick();p.process(pcm,100);CHECK(p.visual_source_frame()==7598);
    s.slice_speed_q8[7]=128;p.control_tick();p.process(pcm,100);CHECK(p.visual_source_frame()==7548); // same latched id, live rate
    CHECK(p.preview_frames(1000,5000,true,bank.generation(5),false,1024));
    p.process(pcm,100);CHECK(p.visual_source_frame()==1396);p.control_tick();p.process(pcm,100);CHECK(p.visual_source_frame()==1796);
    CHECK(!p.preview_frames(1000,5000,true,bank.generation(5),false,0));
    CHECK(!p.set_preview_speed(63)&&!p.set_preview_speed(1025));CHECK(p.set_preview_speed(256));
    CHECK(p.visual_source_frame()==1796);p.process(pcm,10);CHECK(p.visual_source_frame()==1809); // no restart
    // Normal scratch uses the accepted exact/cut window and ordinary overrides.
    PlayEditState edit;edit.enabled=true;edit.source_frames=12000;edit.start=4501;edit.end=9999;
    edit.loop_start=edit.start;edit.loop_end=edit.end;apply(p,edit);
    set(p,PlayUnit::P_SLICE,slice_value(7));set(p,PlayUnit::P_SCRUB,32767);set(p,PlayUnit::P_POSITION,0);set(p,PlayUnit::P_SPEED,8192);
    p.reset();p.control_tick();p.process(pcm,128);CHECK(!p.playback_active()); // no implicit trigger
    p.trigger();p.process(pcm,128);CHECK(p.scrub_active()&&p.visual_source_frame()==4501);
    for(int i=0;i<128;++i)CHECK(pcm[i]==0);
    auto& pos=p.params[PlayUnit::P_POSITION];pos.step_lock_override=true;pos.step_lock=32767;pos.resolve(0);p.control_tick();
    uint32_t previous=p.visual_source_frame();bool audible=false;
    for(int i=0;i<2000;++i){p.process(pcm,1);CHECK(p.visual_source_frame()>=previous&&p.visual_source_frame()<8000);previous=p.visual_source_frame();audible|=pcm[0]!=0;}
    CHECK(audible&&p.visual_source_frame()==7999);p.process(pcm,512);for(int i=0;i<512;++i)CHECK(pcm[i]==0);
    pos.temp_override=true;pos.temp=0;pos.slew_ready=false;pos.resolve(0);p.control_tick();previous=p.visual_source_frame();audible=false;
    for(int i=0;i<2000;++i){p.process(pcm,1);CHECK(p.visual_source_frame()<=previous&&p.visual_source_frame()>=4501);previous=p.visual_source_frame();audible|=pcm[0]!=0;}
    CHECK(audible&&p.visual_source_frame()==4501&&pos.base==0&&pos.step_lock==32767);
    pos.temp_override=pos.step_lock_override=false;pos.src=ModSource::SigilB;pos.mod_depth=32767;pos.slew_ready=false;pos.resolve(16384);p.control_tick();p.process(pcm,4096);
    CHECK(p.visual_source_frame()==4501+(uint64_t)3498*pos.effective/32767);
    p.release();p.process(pcm,64);CHECK(!p.playback_active());
    {
        PlayUnit raw,shaped;raw.sample_slot=shaped.sample_slot=5;auto fades=edit;fades.fade_in=fades.fade_out=2000;apply(shaped,fades);
        for(auto* u:{&raw,&shaped}){CHECK(u->preview_frames(4501,8000,true,bank.generation(5),false,256,true));CHECK(u->preview_scrub(0,true));CHECK(u->preview_scrub(8192));}
        q15 x[256],y[256];raw.process(x,256);shaped.process(y,256);bool quieter=false;
        for(int i=0;i<256;++i){CHECK(y[i]>=0&&y[i]<=x[i]);quieter|=y[i]<x[i];}CHECK(quieter);
    }
    // Real scratch is sample-partition invariant, mono/stereo, either gesture direction.
    for(bool stereo:{false,true}) {
        sample(5,12000,stereo);PlayUnit a,b;a.sample_slot=b.sample_slot=5;
        for(auto* u:{&a,&b}) {
            CHECK(u->preview_frames(100,9000,true,bank.generation(5),true,512,true));
            CHECK(u->preview_scrub(17000,true));u->process(pcm,128);for(int i=0;i<128;++i)CHECK(pcm[i]==0);
        }
        for(q15 target:{q15(32767),q15(0),q15(23000),q15(23000)}) {
            CHECK(a.preview_scrub(target)&&b.preview_scrub(target));q15 x[2048],y[2048];a.process(x,2048);
            for(int i=0;i<2048;){int n=std::min(2048-i,1+i%97);b.process(y+i,n);i+=n;}CHECK(!std::memcmp(x,y,sizeof x));
        }
        a.process(pcm,2048);for(int i=0;i<2048;++i)CHECK(pcm[i]==0);
        CHECK(a.preview_active());sample(5,12000,stereo);a.process(pcm,128);CHECK(!a.playback_active());for(int i=0;i<128;++i)CHECK(pcm[i]==0);
    }
    // Private engine audition follows source controls, never steals source voice or mutates params.
    auto eng=std::make_unique<Engine>();eng->transport_playing=false;eng->sequencer_enabled=false;
    auto* source=new PlayUnit;source->sample_slot=5;delete eng->replace_unit(0,0,source);
    auto token=eng->start_play_preview(0,0,100,9000,true,false,256,true);CHECK(token);
    set(*source,PlayUnit::P_POSITION,32767);eng->render(pcm,2048);CHECK(eng->play_preview_position(token)>100);
    CHECK(!source->playback_active()&&source->params[PlayUnit::P_POSITION].base==32767);
    const auto newer=eng->start_play_preview(0,0,100,9000,true,false,256,true);CHECK(newer!=token);
    CHECK(!eng->scrub_play_preview(token,0));CHECK(!eng->set_play_preview_speed(token,512));
    const auto head=eng->play_preview_position(newer);CHECK(eng->set_play_preview_speed(newer,512));CHECK(eng->play_preview_position(newer)==head);
    eng->stop_play_preview(token);CHECK(eng->play_preview_active(newer));
    Param saved[PARAM_BANK_SIZE];std::memcpy(saved,source->params,sizeof saved);
    CHECK(eng->scrub_play_preview(newer,0));CHECK(!std::memcmp(saved,source->params,sizeof saved));eng->render(pcm,2048);CHECK(eng->play_preview_position(newer)<1000);
    eng->stop_play_preview(newer);eng->render(pcm,128);CHECK(!eng->play_preview_active(newer));
    // Explicit selected rate and live global speed/gain on the independent bus.
    token=eng->start_play_preview(0,0,100,9000,true,false,512);eng->render(pcm,128);const auto first=eng->play_preview_position(token);
    set(*source,PlayUnit::P_SPEED,16384);eng->render(pcm,128);const auto second=eng->play_preview_position(token);
    CHECK(first>100&&second-first>=500);
    set(*source,PlayUnit::P_GAIN,0);eng->render(pcm,128);for(int i=0;i<256;++i)CHECK(pcm[i]==0);
    eng->chains[0].mute=true;eng->render(pcm,32);CHECK(!eng->play_preview_active(token));
    for(bool phase:{false,true}) for(bool held:{false,true}) {
        auto gated=std::make_unique<Engine>();auto* voice=new PlayUnit;voice->sample_slot=5;delete gated->replace_unit(0,0,voice);
        set(*voice,PlayUnit::P_SCRUB,32767);set(*voice,PlayUnit::P_HOLD,held?32767:0);set(*voice,PlayUnit::P_GATE,play_mode::gate_value(0));
        if(phase){CHECK(gated->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1));CHECK(gated->set_clock_mod(ClockTarget::Tempo,ModSource::None,0));}
        gated->set_bpm(120);gated->set_sequence_length(0,64);gated->set_sequencer_enabled(true);
        gated->pattern.steps[0][0].flags|=SeqStep::Trig;gated->pattern_banks[1]=gated->pattern;gated->transport_play();gated->request_pattern(1,false);
        const uint64_t increment=((uint64_t)120*65536*65536+ENGINE_SR*30u)/(ENGINE_SR*60u);
        const int span=phase?int((((uint64_t)1<<28)+increment-1)/increment):1000;
        gated->render(pcm,span);CHECK(voice->playback_active()&&voice->scrub_active());
        gated->render(pcm,63);CHECK(voice->playback_active());gated->render(pcm,1);CHECK(!voice->playback_active());
    }
    sample(5,12000);bank.slot(5).chops[0]=0;bank.slot(5).slice_speed_q8[0]=512;
    PlayUnit bound;bound.sample_slot=5;set(bound,PlayUnit::P_SLICE,slice_value(0));bound.control_tick();bound.trigger();
    sample(5,12000);bound.control_tick();bound.process(pcm,128);CHECK(!bound.playback_active());
    for(int i=0;i<128;++i)CHECK(pcm[i]==0);
    std::puts("ok stable-id cut rates, explicit preview rate/live controls, true bidirectional stationary-silent scratch, overrides, partition PCM and tokens");
}

static void position_rate_persistence() {
    sample(5,12000);auto& bank=SampleBank::instance();auto& s=bank.slot(5);
    s.chops[0]=0;s.chops[7]=4000;s.slice_speed_q8[7]=333;s.slice_reverse_mask=1u<<7;
    bank.set_provenance(5,trackr::synth::SampleOrigin::SdWav,"/fixture/rate.wav");
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto* p=new PlayUnit;p->sample_slot=5;delete a->replace_unit(0,0,p);
    set(*p,PlayUnit::P_POSITION,12000);set(*p,PlayUnit::P_SCRUB,32767);p->params[PlayUnit::P_POSITION].src=ModSource::SigilD;p->params[PlayUnit::P_POSITION].mod_depth=-10000;
    CHECK(a->set_step_lock(0,0,0,PlayUnit::P_POSITION,17000));a->capture_scene(0,0);
    auto* second=new PlayUnit;second->sample_slot=5;delete a->replace_unit(1,0,second);
    PlayEditState e; e.enabled=true;e.source_frames=12000;e.start=e.loop_start=2;e.end=e.loop_end=11999;apply(*second,e); // plain alias precedes exact alias
    CHECK(a->set_step_lock(1,0,0,PlayUnit::P_SCRUB,32767)); // lock-only must opt in
    const std::string path="/tmp/seance-play-rate-"+std::to_string((long)getpid())+".snc";
    CHECK(patchio::save(*a,path.c_str()));const Bytes good=read(path);const auto pp=chunk(good,"PPOS"),sp=chunk(good,"PSPD");
    CHECK(pp<good.size()&&sp<good.size()&&r32(good,pp+4)==26&&r32(good,sp+4)==2051);
    auto manifest=std::make_unique<patchio::SampleManifest>();CHECK(patchio::load(*b,path.c_str(),manifest.get()));
    auto* loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(loaded->num_params==PlayUnit::P_COUNT&&!loaded->edit_state().enabled);
    CHECK(loaded->params[8].base==12000&&loaded->params[8].src==ModSource::SigilD&&loaded->params[8].mod_depth==-10000&&loaded->params[9].base==32767);
    CHECK(b->pattern.steps[1][0].locks[0].param==PlayUnit::P_SCRUB&&manifest->slice_speed_q8[5][7]==333);
    std::unique_ptr<Unit> clone(clone_unit(loaded));CHECK(clone->params[8].base==12000&&static_cast<PlayUnit*>(clone.get())->pending_edit_source()->speed_q8[7]==333);
    sample(5,12000);bank.set_provenance(5,trackr::synth::SampleOrigin::Runtime,"/unrelated.wav");
    CHECK(patchio::save(*b,path.c_str()));CHECK(read(path)==good); // no PEDT needed for staged rate preservation
    auto restored=std::make_unique<patchio::RestoredSamples>();restored->ready[5]=true;restored->decoded[5].data.assign(12000,10000);
    CHECK(patchio::commit_restored_samples(*manifest,*restored,b.get()));CHECK(bank.slot(5).slice_speed_q8[7]==333&&!loaded->pending_edit_source());
    auto reject=[&](Bytes bad){put(path,bad);auto* old=b->chains[0].units[0];CHECK(!patchio::load(*b,path.c_str()));CHECK(old==b->chains[0].units[0]);};
    for(size_t at:{pp,sp}) {
        auto bad=good;bad[at+8]=2;reject(bad);
        bad=good;Bytes copy(good.begin()+at,good.begin()+at+8+r32(good,at+4));bad.insert(bad.end(),copy.begin(),copy.end());reject(bad);
        bad=good;w32(bad,at+4,r32(good,at+4)+1);reject(bad);
        bad=good;bad.resize(at+12);reject(bad);
    }
    {auto bad=good;bad[pp+9]=0;reject(bad);}
    {auto bad=good;bad[pp+10]=255;reject(bad);}
    {auto bad=good;bad[pp+22]=0;bad[pp+23]=0;reject(bad);} // duplicate card
    {auto bad=good;bad[pp+13]=255;reject(bad);} // negative position
    {auto bad=good;bad[pp+14]=255;reject(bad);} // invalid modulation source
    {auto bad=good;bad[chunk(bad,"UNIT")+8]=0;reject(bad);}
    for(uint16_t rate:{uint16_t(0),uint16_t(63),uint16_t(1025),uint16_t(65535)}) {
        auto bad=good;const size_t at=sp+11+(5*16+7)*2;bad[at]=(uint8_t)rate;bad[at+1]=(uint8_t)(rate>>8);reject(bad);
    }
    {auto bad=good;bad[sp+9]=63;reject(bad);}
    {auto bad=good;bad[sp+10]=15;reject(bad);}
    {auto bad=good;const size_t at=sp+11+(5*16+8)*2;bad[at]=0;bad[at+1]=2;reject(bad);} // empty-id rate
    // Optional extension ordering does not depend on CORE/UNIT/CHOP.
    for(const char* id:{"PPOS","PSPD"}) {
        auto reordered=good;const auto at=chunk(reordered,id),end=at+8+r32(reordered,at+4);Bytes value(reordered.begin()+at,reordered.begin()+end);
        reordered.erase(reordered.begin()+at,reordered.begin()+end);reordered.insert(reordered.begin()+5,value.begin(),value.end());put(path,reordered);CHECK(patchio::load(*b,path.c_str(),manifest.get()));
    }
    // Missing hydration retains metadata-only rates/cuts with unrelated replacement invalidation.
    put(path,good);CHECK(patchio::load(*b,path.c_str(),manifest.get()));manifest->slots[5].missing=true;
    auto missing=std::make_unique<patchio::RestoredSamples>();CHECK(patchio::commit_restored_samples(*manifest,*missing,b.get()));
    loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(loaded->pending_edit_source()->speed_q8[7]==333);
    CHECK(patchio::save(*b,path.c_str()));auto c=std::make_unique<Engine>();CHECK(patchio::load(*c,path.c_str(),manifest.get()));CHECK(manifest->slice_speed_q8[5][7]==333&&manifest->slots[5].missing);
    // Removing PPOS leaves the old eight controls active and appended data/routes dormant.
    auto old=good;old.erase(old.begin()+pp,old.begin()+pp+8+r32(old,pp+4));put(path,old);CHECK(patchio::load(*b,path.c_str()));
    loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(loaded->modern()&&loaded->params[8].base==0&&loaded->params[9].base==0&&b->pattern.steps[1][0].lock_count==0);
    // Defaults emit neither optional chunk; recorder metadata reset cannot inherit a cut rate.
    sample(5,12000);auto plain=std::make_unique<Engine>();auto* clean=new PlayUnit;clean->sample_slot=5;delete plain->replace_unit(0,0,clean);
    CHECK(patchio::save(*plain,path.c_str()));auto bytes=read(path);CHECK(chunk(bytes,"PPOS")==bytes.size()&&chunk(bytes,"PSPD")==bytes.size());
    bank.slot(5).slice_speed_q8[0]=512;RecUnit rec;rec.sample_slot=5;rec.clear_take();for(auto rate:bank.slot(5).slice_speed_q8)CHECK(rate==256);
    std::remove(path.c_str());std::puts("ok optional PPOS/PSPD strict transactional validation, order/default compatibility, locks, clone, staged/missing/hydrated cut rates");
}
static void hold_dsp() {
    auto& bank=SampleBank::instance();sample(5,12000,true);
    auto& s=bank.slot(5);for(size_t i=0;i<s.data.size();++i)s.data[i]=(q15)(1000+i/2);
    q15 pcm[8192];
    // HOLD alone cannot enable musical scrub, and legacy ignores the new control.
    for(bool modern:{false,true}) {
        PlayUnit a,b;a.sample_slot=b.sample_slot=5;a.set_modern(modern);b.set_modern(modern);
        set(b,PlayUnit::P_HOLD,32767);
        for(auto*u:{&a,&b}){u->control_tick();u->trigger();CHECK(!u->scrub_active());}
        q15 x[1024],y[1024];a.process(x,1024);b.process(y,1024);CHECK(!std::memcmp(x,y,sizeof x));
    }
    for(int speed:{-32767,-8192,8192,32767}) {
        PlayUnit a,b;a.sample_slot=b.sample_slot=5;
        for(auto*u:{&a,&b}) {
            set(*u,PlayUnit::P_HOLD,32767);set(*u,PlayUnit::P_SPEED,(q15)speed);
            CHECK(u->preview_frames(1000,10000,false,bank.generation(5),false,1024,true));
            CHECK(u->preview_scrub(0,true));
        }
        for(q15 target:{q15(0),q15(32767),q15(16000),q15(16000),q15(0)}) {
            CHECK(a.preview_scrub(target)&&b.preview_scrub(target));q15 x[4096],y[4096];a.process(x,4096);
            for(int i=0;i<4096;){int n=std::min(4096-i,1+i%97);b.process(y+i,n);i+=n;}
            CHECK(!std::memcmp(x,y,sizeof x)&&a.playback_active());
            CHECK(a.visual_source_frame()==1000+(uint64_t)8999*target/32767);
            for(int i=64;i<4096;++i){CHECK(x[i]>2000&&x[i]<11500);CHECK(std::abs((int)x[i]-x[i-1])<80);}
            if(target==0)CHECK(x[4095]>2700&&x[4095]<2900);
            if(target==32767)CHECK(x[4095]>10000&&x[4095]<10300);
        }
        CHECK(a.set_preview_speed(64));a.process(pcm,2048);CHECK(a.playback_active());
        a.release();a.process(pcm,64);CHECK(!a.playback_active());a.process(pcm,64);for(int i=0;i<64;++i)CHECK(!pcm[i]);
    }
    // Window is latched to trim + stable cut; position sigil/locks only move inside it.
    PlayUnit p;p.sample_slot=5;PlayEditState edit;edit.enabled=true;edit.source_frames=12000;
    edit.start=edit.loop_start=4501;edit.end=edit.loop_end=9999;apply(p,edit);
    s.chops[7]=4000;s.chops[3]=8000;s.slice_reverse_mask=1u<<7;s.slice_speed_q8[7]=512;
    set(p,PlayUnit::P_HOLD,32767);set(p,PlayUnit::P_SCRUB,32767);set(p,PlayUnit::P_SLICE,slice_value(7));
    p.control_tick();p.trigger();p.process(pcm,4096);CHECK(p.visual_source_frame()==4501&&pcm[4095]>6000&&pcm[4095]<6500);
    auto& pos=p.params[PlayUnit::P_POSITION];pos.src=ModSource::SigilB;pos.mod_depth=32767;pos.slew_ready=false;pos.resolve(32767);
    set(p,PlayUnit::P_SLICE,slice_value(3));p.control_tick();p.process(pcm,4096);
    CHECK(p.visual_source_frame()==4501+(uint64_t)3498*pos.effective/32767&&pcm[4095]>8000&&pcm[4095]<8400);
    sample(5,12000,true);p.process(pcm,128);CHECK(!p.playback_active());for(int i=0;i<128;++i)CHECK(!pcm[i]);
    // Two-frame sources, signed maximum speed/rate and exact ends are bounded.
    for(bool stereo:{false,true}) {
        sample(5,2,stereo);PlayUnit tiny;tiny.sample_slot=5;set(tiny,PlayUnit::P_HOLD,32767);set(tiny,PlayUnit::P_SPEED,-32767);
        CHECK(tiny.preview_frames(0,2,true,bank.generation(5),true,1024,true));
        for(q15 target:{q15(0),q15(32767)}){CHECK(tiny.preview_scrub(target,true));tiny.process(pcm,8192);CHECK(tiny.playback_active());for(int i=64;i<8192;++i)CHECK(pcm[i]>9000);}
    }
    // Engine's existing dynamic parameter copy supports HOLD, including live changes.
    sample(5,12000);auto eng=std::make_unique<Engine>();eng->transport_playing=false;eng->sequencer_enabled=false;
    auto* source=new PlayUnit;source->sample_slot=5;delete eng->replace_unit(0,0,source);set(*source,PlayUnit::P_HOLD,32767);
    const auto token=eng->start_play_preview(0,0,1000,9000,false,false,256,true);CHECK(token);CHECK(eng->scrub_play_preview(token,16000,true));
    eng->render(pcm,2048);CHECK(eng->play_preview_active(token)&&pcm[4095]>0&&!source->playback_active());
    set(*source,PlayUnit::P_HOLD,0);eng->render(pcm,2048);for(int i=0;i<4096;++i)CHECK(!pcm[i]);
    set(*source,PlayUnit::P_HOLD,32767);eng->render(pcm,128);CHECK(pcm[255]>0);
    eng->stop_play_preview(token);eng->render(pcm,128);CHECK(!eng->play_preview_active(token));
    std::puts("ok hold sustained overlap/retarget/seams/partition, latched trim+cut/sigil, tiny sources, rate/reverse, independent preview and source/release ownership");
}
static void hold_persistence() {
    sample(5,12000);auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    for(int slot=0;slot<4;++slot){auto*p=new PlayUnit;p->sample_slot=5;delete a->replace_unit(0,slot,p);}
    const std::string path="/tmp/seance-play-hold-"+std::to_string((long)getpid())+".snc";
    CHECK(patchio::save(*a,path.c_str()));const auto plain=read(path);CHECK(chunk(plain,"PHLD")==plain.size());
    auto& p=*static_cast<PlayUnit*>(a->chains[0].units[0]);
    set(p,PlayUnit::P_POSITION,8192);set(p,PlayUnit::P_SCRUB,32767);set(p,PlayUnit::P_HOLD,32767);
    p.params[PlayUnit::P_HOLD].src=ModSource::SigilD;p.params[PlayUnit::P_HOLD].mod_depth=-12345;
    CHECK(a->set_step_lock(0,0,1,PlayUnit::P_HOLD,32767)); // lock only
    a->scenes[0][1].kind[2]=UnitKind::SamplePlayer;a->scenes[0][1].base[2][PlayUnit::P_HOLD]=32767; // scene only
    a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,3,PlayUnit::P_HOLD,0,32767}; // macro only
    CHECK(patchio::save(*a,path.c_str()));const Bytes good=read(path);const auto h=chunk(good,"PHLD"),pp=chunk(good,"PPOS");
    CHECK(h<good.size()&&r32(good,h+4)==30&&r32(good,pp+4)==14);
    const auto pc=chunk(plain,"CORE"),gc=chunk(good,"CORE");CHECK(r32(plain,pc+4)==r32(good,gc+4));
    CHECK(!std::memcmp(plain.data()+pc,good.data()+gc,8+r32(good,gc+4))); // all appended CORE bytes stay zero
    CHECK(patchio::load(*b,path.c_str()));auto* loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);
    CHECK(loaded->params[10].base==32767&&loaded->params[10].src==ModSource::SigilD&&loaded->params[10].mod_depth==-12345);
    CHECK(b->pattern.steps[0][0].locks[0].param==10&&b->scenes[0][1].base[2][10]==32767&&b->macro_pads[0].routes[0].param==10);
    CHECK(patchio::save(*b,path.c_str()));CHECK(read(path)==good);
    std::unique_ptr<Unit> cloned(clone_unit(loaded));CHECK(cloned->params[10].base==32767&&cloned->params[10].src==ModSource::SigilD);
    auto without=[&](const char* id){auto bytes=good;const auto at=chunk(bytes,id),end=at+8+r32(bytes,at+4);bytes.erase(bytes.begin()+at,bytes.begin()+end);return bytes;};
    put(path,without("PPOS"));CHECK(patchio::load(*b,path.c_str()));loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);
    CHECK(loaded->params[8].base==0&&loaded->params[9].base==0&&loaded->params[10].base==32767&&b->pattern.steps[0][0].lock_count==1);
    put(path,without("PHLD"));CHECK(patchio::load(*b,path.c_str()));loaded=static_cast<PlayUnit*>(b->chains[0].units[0]);
    CHECK(loaded->params[8].base==8192&&loaded->params[9].base==32767&&loaded->params[10].base==0&&loaded->params[10].src==ModSource::None);
    CHECK(b->pattern.steps[0][0].lock_count==0&&b->scenes[0][1].base[2][10]==0&&b->macro_pads[0].route_count==0);
    auto reordered=without("PHLD");reordered.insert(reordered.begin()+5,good.begin()+h,good.begin()+h+38);put(path,reordered);CHECK(patchio::load(*b,path.c_str()));
    auto reject=[&](Bytes bad){put(path,bad);auto*old=b->chains[0].units[0];CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[0].units[0]==old);};
    for(size_t field:{size_t(8),size_t(9),size_t(10),size_t(11),size_t(13),size_t(14)}){auto bad=good;bad[h+field]=255;reject(bad);}
    {auto bad=good;bad[h+9]=0;reject(bad);}
    {auto bad=good;bad[h+17]=0;bad[h+18]=0;reject(bad);} // duplicate card
    {auto bad=good;bad[h+10]=1;reject(bad);} // empty target
    {auto bad=good;bad[chunk(bad,"UNIT")+8]=0;reject(bad);} // legacy target
    {auto bad=good;bad.insert(bad.end(),good.begin()+h,good.begin()+h+38);reject(bad);}
    {auto bad=good;w32(bad,h+4,31);reject(bad);}
    {auto bad=good;bad.resize(h+12);reject(bad);}
    std::remove(path.c_str());std::puts("ok independent PHLD/PPOS layers, unchanged CORE, defaults/clone/order and strict transactional malformed rejection");
}
int main(){projection();dsp_and_binding();persistence();hydration();rates_and_scrub();position_rate_persistence();hold_dsp();hold_persistence();std::printf("play editor core: 8/8 passed (%u checks)\n",checks);}
