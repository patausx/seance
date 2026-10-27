// Focused real-engine REC dictaphone proof. No platform/engine/bank/patch stubs.
// REC_DICTAPHONE_WAVE_REGRESSION selects three unchanged historical wave cases;
// the old dual_engine case saves an active take, intentionally forbidden now.
#if defined(REC_DICTAPHONE_COMPATIBILITY)
// Same fixture is compiled against retained pre-edit and current real cores.
// All takes are explicitly finalized before serialization, unlike the excluded
// historical active-save assertion. Files retain raw engine/recorded PCM.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace seance;
using namespace trackr::synth;
namespace {
unsigned long long compat_checks=0;
#define COMPAT(x) do { ++compat_checks; if(!(x)) { std::fprintf(stderr,"compat FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void fixture_pcm(const std::string& path,const std::vector<q15>& pcm) {
    std::ofstream file(path,std::ios::binary);
    for(q15 v:pcm) { const unsigned char b[]={static_cast<unsigned char>(v),static_cast<unsigned char>(uint16_t(v)>>8)}; file.write((const char*)b,2); }
    COMPAT(file.good());
}
}
int main(int argc,char** argv) {
    COMPAT(argc==2); auto& bank=SampleBank::instance();
    for(int fixture=0;fixture<6;++fixture) {
        for(int slot=0;slot<SAMPLE_BANK_SIZE;++slot) bank.clear_slot(slot);
        bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);
        auto e=std::make_unique<Engine>(); auto* osc=new OscUnit;
        osc->params[OscUnit::P_PITCH].base=13900; osc->params[OscUnit::P_PITCH].snap();
        osc->params[OscUnit::P_GAIN].base=9000; osc->params[OscUnit::P_GAIN].snap(); osc->control_tick();
        delete e->replace_unit(0,0,osc); auto* rec=new RecUnit; rec->sample_slot=16;
        rec->params[RecUnit::P_LEN].base=512; rec->params[RecUnit::P_LEN].snap(); delete e->replace_unit(0,1,rec);
        if(fixture==4) delete e->replace_unit(0,2,new FilterUnit);
        if(fixture==1) e->chains[0].mute=true;
        if(fixture==2) rec->bypass=true;
        if(fixture==3) { rec->params[RecUnit::P_MON].base=0; rec->params[RecUnit::P_MON].snap(); }
        COMPAT(e->prepare_recorder(*rec,true)==SampleEditResult::Ok);
        if(fixture==5) e->transport_play();
        COMPAT(e->request_recorder_record(0,1,true,fixture==5?LaunchQuantize::Beat:LaunchQuantize::Immediate)==SampleEditResult::Ok);
        const int count=fixture==1?16384:(fixture==5?20097:4097);
        const int partition=fixture==0?1:(fixture==4?17:257);
        std::array<q15,514> buf{}; std::vector<q15> output;
        for(int at=0;at<count;) { const int n=std::min(partition,count-at); e->render(buf.data(),n); output.insert(output.end(),buf.begin(),buf.begin()+2*n); at+=n; }
        rec->stop(); COMPAT(!rec->is_recording() && rec->recorded_frames()>0); rec->mark_take_saved(rec->take_generation());
        const std::string prefix=std::string(argv[1])+"/fixture-"+std::to_string(fixture);
        fixture_pcm(prefix+"-engine.pcm",output); fixture_pcm(prefix+"-take.pcm",bank.slot(16).data);
        COMPAT(patchio::save(*e,(prefix+".snc").c_str()));
        std::printf("fixture rec-%d rendered=%d recorded=%u capacity=%u\n",fixture,count,rec->recorded_frames(),rec->capacity_frames());
    }
    std::printf("PASS compatibility six real-engine fixtures (%llu checks)\n",compat_checks);
    return 0;
}
#elif defined(REC_DICTAPHONE_WAVE_REGRESSION)
#define main historical_rec_wave_main
#include "rec_wave_cache_test.cpp"
#undef main
int main(int argc, char** argv) {
    CHECK(argc == 2); directory = argv[1];
    streaming(); restored_and_replaced(); focus_clone_persistence();
    CHECK(allocations == 0);
    std::printf("PASS rec-wave unchanged selected 3/3: %llu checks, %llu oracle PCM, peak %llu PCM + %llu summaries, allocations=%llu\n",
        (unsigned long long)checks, (unsigned long long)oracle_samples,
        (unsigned long long)peak_pcm, (unsigned long long)peak_summary,
        (unsigned long long)allocations);
    return 0;
}
#elif defined(REC_DICTAPHONE_GRAN_REGRESSION)
#define main historical_gran_clean_main
#include "gran_clean_core_test.cpp"
#undef main
int main() {
    frozen_legacy(); boundaries_windows(); headroom_tails(); scan_clock(); source_identity_and_pause();
    std::printf("PASS gran-clean unchanged selected 5/6: %llu checks; persistence_layers excluded to avoid its hardcoded /tmp output\n",checks);
    return 0;
}
#else
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "synth/wav_writer.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace seance;
using namespace trackr::synth;
static uint64_t rec_test_allocations=0;
#if defined(__SANITIZE_ADDRESS__)
extern "C" void __sanitizer_malloc_hook(const volatile void*,std::size_t) { ++rec_test_allocations; }
#endif
namespace {
using Cmd = RecorderCommand;
using Result = SampleEditResult;
using Q = LaunchQuantize;
using Pcm = std::vector<q15>;
using Bytes = std::vector<unsigned char>;
uint64_t checks = 0;
std::string directory;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#x); std::exit(1); } } while(0)
SampleBank& bank() { return SampleBank::instance(); }
void reset_bank() {
    bank().set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);
    for (int s=0; s<SAMPLE_BANK_SIZE; ++s) bank().clear_slot(s);
}
void set(Unit& u, int p, int v) { u.params[p].base=q15(v); u.params[p].snap(); }
q15 signal(uint32_t frame, uint32_t salt=0) {
    return q15(int(((frame + salt*7919u)*1664525u + 1013904223u) >> 8)%28001-14000);
}
Pcm input(uint32_t n, uint32_t offset=0, uint32_t salt=0) {
    Pcm pcm(n); for (uint32_t i=0;i<n;++i) pcm[i]=signal(offset+i,salt); return pcm;
}
void feed(RecUnit& rec, const Pcm& pcm, int partition=0) {
    for (size_t at=0;at<pcm.size();) {
        const int n=int(std::min(pcm.size()-at, partition>0?size_t(partition):pcm.size()));
        rec.capture_input(pcm.data()+at,n); at+=size_t(n);
    }
}
Sample sound(uint32_t frames, uint32_t salt=0) {
    Sample s; s.data=input(frames,0,salt); s.logical_frames=frames;
    s.root_note=57; s.loop_start=frames/5; s.loop_end=frames;
    s.chops[0]=0; if(frames>16) s.chops[1]=frames/3;
    s.slice_reverse_mask=3; s.slice_speed_q8[0]=512;
    s.exact_native_endpoint=true; return s;
}
void install(int slot, Sample sample, SampleOrigin origin=SampleOrigin::Runtime, const char* path="") {
    CHECK(bank().replace_sample(slot,std::move(sample),origin,path)==Result::Ok);
}
RecUnit& rec_at(Engine& e, int chain=0, int slot=0, int sample=16, int length=8192) {
    auto* r=new RecUnit; r->sample_slot=sample; set(*r,RecUnit::P_LEN,length);
    delete e.replace_unit(chain,slot,r); CHECK(e.prepare_recorder(*r,true)==Result::Ok); return *r;
}
void render(Engine& e,uint64_t count,int partition=257) {
    std::array<q15,1024*2> out{};
    while(count) { int n=int(std::min<uint64_t>(count,uint64_t(std::min(partition,1024)))); e.render(out.data(),n); count-=n; }
}
void until_before(Engine& e,uint64_t endpoint) {
    CHECK(endpoint>e.transport_frame); render(e,endpoint-e.transport_frame-1);
    CHECK(e.transport_frame+1==endpoint);
}
void cross(Engine& e,uint64_t endpoint) {
    CHECK(endpoint>=e.transport_frame); render(e,endpoint-e.transport_frame+1);
    CHECK(e.transport_frame==endpoint+1);
}
void recorded(RecUnit& r,const Pcm& pcm) {
    CHECK(r.recorded_frames()==pcm.size());
    CHECK(bank().slot(r.sample_slot).playable_frames()==pcm.size());
    CHECK(std::equal(pcm.begin(),pcm.end(),bank().slot(r.sample_slot).data.begin()));
}
struct SampleImage {
    int slot; Sample value; SampleProvenance provenance; uint32_t generation; const q15* address;
    explicit SampleImage(int index):slot(index),value(bank().slot(index)),provenance(bank().provenance(index)),
        generation(bank().generation(index)),address(bank().slot(index).data.data()) {}
    void exact(bool same_address=true) const {
        const Sample& now=bank().slot(slot);
        CHECK(now.data==value.data && now.channels==value.channels && now.root_note==value.root_note);
        CHECK(now.loop_start==value.loop_start && now.loop_end==value.loop_end && now.reversed==value.reversed);
        CHECK(now.logical_frames==value.logical_frames && now.slice_reverse_mask==value.slice_reverse_mask);
        CHECK(now.exact_native_endpoint==value.exact_native_endpoint);
        CHECK(std::equal(std::begin(now.chops),std::end(now.chops),std::begin(value.chops)));
        CHECK(std::equal(std::begin(now.slice_speed_q8),std::end(now.slice_speed_q8),std::begin(value.slice_speed_q8)));
        CHECK(bank().generation(slot)==generation);
        const auto& p=bank().provenance(slot);
        CHECK(p.origin==provenance.origin && p.missing==provenance.missing && !std::strcmp(p.path,provenance.path));
        if(same_address) CHECK(now.data.data()==address);
    }
};
struct TakeImage {
    RecUnit& rec; int slot; uint32_t frames,target,take,saved; uint64_t state,session; bool open,paused,recording;
    SampleImage sample;
    explicit TakeImage(RecUnit& r):rec(r),slot(r.sample_slot),frames(r.recorded_frames()),target(r.active_target_frames()),
        take(r.take_generation()),saved(r.saved_generation()),state(r.state_token()),session(r.session_token()),
        open(r.writer_open()),paused(r.is_paused()),recording(r.is_recording()),sample(r.sample_slot) {}
    void exact() const {
        CHECK(rec.sample_slot==slot && rec.recorded_frames()==frames && rec.active_target_frames()==target);
        CHECK(rec.take_generation()==take && rec.saved_generation()==saved);
        CHECK(rec.state_token()==state && rec.session_token()==session);
        CHECK(rec.writer_open()==open && rec.is_paused()==paused && rec.is_recording()==recording); sample.exact();
    }
};
RecWorkspacePlan plan(Engine& e, RecUnit& r, int target=-1) {
    RecWorkspacePlan p; CHECK(e.plan_recorder(r,true,p,target)==Result::Ok);
    CHECK(Engine::stage_recorder(p)==Result::Ok); return p;
}
void take(Engine& e,RecUnit& r,int chain=0,int slot=0,uint32_t frames=193,bool saved=false) {
    CHECK(e.request_recorder_command(chain,slot,Cmd::Start,Q::Immediate)==Result::Ok);
    CHECK(r.writer_open() && r.is_recording()); feed(r,input(frames,0,3),31); r.stop();
    CHECK(!r.writer_open() && r.recorded_frames()==frames && r.take_needs_save());
    if(saved) { r.mark_take_saved(r.take_generation()); CHECK(!r.take_needs_save()); }
}
Bytes bytes(const std::string& path) {
    std::ifstream f(path,std::ios::binary); CHECK(f.good()); return {std::istreambuf_iterator<char>(f),{}};
}
void put(const std::string& path,const Bytes& data) {
    std::ofstream f(path,std::ios::binary); f.write((const char*)data.data(),data.size()); CHECK(f.good());
}

void lifecycle() {
    const int partitions[]={1,2,7,31,32,63,64,127,257,997};
    for(int block:partitions) {
        reset_bank(); RecUnit r; set(r,RecUnit::P_LEN,1200); CHECK(r.prepare_slot()==Result::Ok);
        CHECK(!r.writer_open() && !r.is_recording() && !r.is_paused() && !r.recorded_frames());
        CHECK(r.state_token()!=0); CHECK(!r.pause() && !r.resume());
        const auto empty_state=r.state_token(); r.trigger();
        CHECK(r.writer_open() && r.is_recording() && !r.is_paused());
        const auto session=r.session_token(), start_state=r.state_token(); const auto take_gen=r.take_generation();
        CHECK(session && start_state!=empty_state); const auto target=r.active_target_frames();
        const auto capacity=r.capacity_frames(); const auto* memory=bank().slot(r.sample_slot).data.data();
        Pcm expected=input(73,0,1); feed(r,expected,block); recorded(r,expected);
        CHECK(r.pause()); CHECK(r.writer_open() && r.is_paused() && !r.is_recording());
        CHECK(r.state_token()!=start_state && r.session_token()==session && r.take_generation()==take_gen);
        const TakeImage paused(r); std::array<q15,RecUnit::WAVE_POINTS> wave{},wave_after{};
        r.copy_visual_wave(wave.data()); feed(r,input(16001,73,2),block); paused.exact();
        r.copy_visual_wave(wave_after.data()); CHECK(wave==wave_after);
        r.trigger(); paused.exact(); CHECK(!r.pause()); paused.exact();
        // A new effective/base length is not a second segment's recording limit.
        set(r,RecUnit::P_LEN,32767); CHECK(r.resume());
        CHECK(r.writer_open() && r.is_recording() && !r.is_paused());
        CHECK(r.active_target_frames()==target && r.capacity_frames()==capacity && r.session_token()==session);
        const auto resume_state=r.state_token(); CHECK(resume_state!=paused.state);
        CHECK(!r.resume()); CHECK(r.state_token()==resume_state);
        const Pcm next=input(211,73,7); expected.insert(expected.end(),next.begin(),next.end()); feed(r,next,block);
        CHECK(r.pause()); feed(r,input(8193,0,11),block); CHECK(r.resume());
        const Pcm last=input(31,284,9); expected.insert(expected.end(),last.begin(),last.end()); feed(r,last,block);
        r.stop(); CHECK(!r.writer_open() && !r.is_paused() && !r.is_recording()); recorded(r,expected);
        CHECK(bank().slot(r.sample_slot).data.data()==memory && r.session_token()==session && r.take_needs_save());
        CHECK(bank().slot(r.sample_slot).loop_end==expected.size() && bank().slot(r.sample_slot).chops[0]==0);
        const TakeImage final(r); CHECK(!r.resume() && !r.pause()); r.trigger(); final.exact();
        r.mark_take_saved(take_gen-1); CHECK(r.take_needs_save());
        r.mark_take_saved(take_gen); CHECK(!r.take_needs_save());
    }
    // Explicit stop from a paused empty reservation closes it, not a silent resume.
    reset_bank(); RecUnit empty; CHECK(empty.prepare_slot()==Result::Ok); empty.trigger();
    CHECK(empty.pause() && empty.writer_open()); empty.stop(); CHECK(!empty.writer_open() && !empty.recorded_frames());
    CHECK(!empty.resume());
    std::puts("ok lifecycle: 10 segment partitions; exact concatenated PCM/no paused silence, stable session/storage/latched limit, stop vs resume");
}

void limits() {
    for(int block:{1,17,32,257,1025}) {
        reset_bank(); RecUnit r; set(r,RecUnit::P_LEN,0); CHECK(r.prepare_slot()==Result::Ok); r.trigger();
        CHECK(r.active_target_frames()==1024); const auto session=r.session_token();
        Pcm a=input(1000,0,5); feed(r,a,block); CHECK(r.pause()); feed(r,input(2000),block);
        CHECK(r.recorded_frames()==1000 && r.resume()); Pcm b=input(1025,1000,5); feed(r,b,block);
        Pcm exact=input(1024,0,5); recorded(r,exact); CHECK(!r.writer_open() && !r.is_recording() && !r.is_paused());
        CHECK(r.session_token()==session && !r.resume() && r.take_needs_save());
        TakeImage done(r); feed(r,input(47),block); r.trigger(); done.exact();
        // Capacity, not a future higher effective parameter, remains the bound.
        r.clear_take(); set(r,RecUnit::P_LEN,32767); r.trigger(); CHECK(r.active_target_frames()==1024);
        feed(r,input(2048),block); CHECK(r.recorded_frames()==1024 && !r.writer_open());
    }
    std::puts("ok limits: exact automatic finalization + capacity, 5 block partitions, no resume over limit");
}

void engine_pcm() {
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep;
    auto* osc=new OscUnit; set(*osc,OscUnit::P_PITCH,13900); set(*osc,OscUnit::P_GAIN,9000); osc->control_tick();
    delete e.replace_unit(0,0,osc); RecUnit& r=rec_at(e,0,1,16,2000);
    CHECK(e.request_recorder_record(0,1,true,Q::Immediate)==Result::Ok);
    e.chains[0].mute=true; r.bypass=true; render(e,311,19); CHECK(r.recorded_frames()==311);
    const auto first=Pcm(bank().slot(r.sample_slot).data.begin(),bank().slot(r.sample_slot).data.begin()+311);
    CHECK(std::any_of(first.begin(),first.end(),[](q15 n){return n!=0;}));
    CHECK(e.request_recorder_command(0,1,Cmd::Pause,Q::Immediate)==Result::Ok); const TakeImage paused(r);
    EngineUiSnapshot published; e.capture_ui_snapshot(published,0,1);
    CHECK(published.rec_waveform_session==r.session_token() && (published.rec_waveform_valid&16));
    CHECK(!(published.rec_waveform_valid&2));
    e.trigger_chain(0); e.request_chain_trigger(0,Q::Immediate); render(e,4097,37); paused.exact();
    CHECK(e.recording_active_or_pending());
    CHECK(e.request_chain_record(0,true,Q::Immediate)!=Result::Ok); paused.exact();
    CHECK(e.request_recorder_command(0,1,Cmd::Resume,Q::Immediate)==Result::Ok);
    e.chains[0].mute=false; r.bypass=false; set(r,RecUnit::P_MON,0); render(e,97,1);
    CHECK(r.recorded_frames()==408 && std::equal(first.begin(),first.end(),bank().slot(r.sample_slot).data.begin()));
    CHECK(e.request_recorder_record(0,1,false,Q::Immediate)==Result::Ok);
    CHECK(!r.writer_open() && !e.recording_active_or_pending());
    const TakeImage stopped(r); e.trigger_chain(0); render(e,1024); stopped.exact();
    std::puts("ok actual oscillator→rec engine: mute/bypass capture, paused whole-chain trigger exclusion, monitor-off resume, bool wrapper");
}

void commands() {
    for(Q grid:{Q::Sixteenth,Q::Beat,Q::Bar}) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep;
        RecUnit& a=rec_at(e,0,1,16,20000); RecUnit& b=rec_at(e,2,4,17,20000);
        e.transport_play(); render(e,123);
        CHECK(e.request_recorder_command(0,1,Cmd::Start,grid)==Result::Ok);
        const auto start=e.next_recorder_deadline(); CHECK(start>e.transport_frame);
        CHECK(e.recorder_pending_command(0,1)==Cmd::Start && e.recorder_pending_command(2,4)==Cmd::None);
        CHECK(e.recording_active_or_pending() && !a.writer_open());
        CHECK(e.request_recorder_command(2,4,Cmd::Start,Q::Immediate)==Result::Busy);
        CHECK(e.next_recorder_deadline()==start && e.recorder_pending_command(0,1)==Cmd::Start && !b.writer_open());
        const auto duplicate=e.request_recorder_command(0,1,Cmd::Start,grid);
        CHECK(duplicate==Result::Busy || duplicate==Result::Ok); CHECK(e.next_recorder_deadline()==start);
        e.request_chain_trigger(3,Q::Bar); until_before(e,start); CHECK(!a.writer_open()); cross(e,start);
        CHECK(a.is_recording() && !b.writer_open() && e.recorder_pending_command(0,1)==Cmd::None);
        render(e,71); const auto session=a.session_token();
        CHECK(e.request_recorder_command(0,1,Cmd::Pause,Q::Sixteenth)==Result::Ok);
        const auto pause=e.next_recorder_deadline(); CHECK(e.recorder_pending_command(0,1)==Cmd::Pause);
        until_before(e,pause); CHECK(a.is_recording()); cross(e,pause); CHECK(a.is_paused() && a.writer_open());
        const auto frames=a.recorded_frames(); render(e,199); CHECK(a.recorded_frames()==frames);
        CHECK(e.request_recorder_command(0,1,Cmd::Resume,Q::Sixteenth)==Result::Ok);
        const auto resume=e.next_recorder_deadline(); CHECK(e.recorder_pending_command(0,1)==Cmd::Resume);
        until_before(e,resume); CHECK(a.is_paused() && a.recorded_frames()==frames); cross(e,resume);
        CHECK(a.is_recording() && a.session_token()==session); render(e,31);
        CHECK(e.request_recorder_command(0,1,Cmd::Stop,Q::Sixteenth)==Result::Ok);
        const auto stop=e.next_recorder_deadline(); CHECK(e.recorder_pending_command(0,1)==Cmd::Stop);
        until_before(e,stop); CHECK(a.writer_open()); cross(e,stop);
        CHECK(!a.writer_open() && !e.recording_active_or_pending());
    }
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
    e.transport_play(); CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Beat)==Result::Ok);
    const SampleImage before(r.sample_slot); CHECK(e.request_recorder_command(0,0,Cmd::Stop,Q::Beat)==Result::Ok);
    CHECK(e.recorder_pending_command(0,0)==Cmd::None && !e.recording_active_or_pending());
    render(e,e.frames_per_beat()+1); CHECK(!r.writer_open() && !r.recorded_frames()); before.exact();
    CHECK(e.request_recorder_command(-1,0,Cmd::Start,Q::Immediate)==Result::InvalidSlot);
    CHECK(e.request_recorder_command(0,6,Cmd::Start,Q::Immediate)==Result::InvalidSlot);
    CHECK(e.request_recorder_command(1,0,Cmd::Start,Q::Immediate)==Result::InvalidSlot);
    CHECK(e.request_recorder_command(0,0,Cmd::Pause,Q::Immediate)!=Result::Ok);
    CHECK(e.request_recorder_command(0,0,Cmd::Resume,Q::Immediate)!=Result::Ok);
    e.transport_stop(); CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Bar)==Result::Ok);
    CHECK(r.is_recording() && e.recorder_pending_command(0,0)==Cmd::None); r.stop();
    std::puts("ok typed command grids, endpoint isolation, unrelated/repeated busy, cancel queued start, invalid-state/slot and stopped-transport immediate");
}

void legacy_queue() {
    for(int stale=0;stale<4;++stale) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep;
        RecUnit& r=rec_at(e); RecUnit& other=rec_at(e,1,1,17); e.transport_play();
        CHECK(e.request_chain_record(0,true,Q::Beat)==Result::Ok);
        CHECK(e.request_recorder_command(1,1,Cmd::Start,Q::Immediate)==Result::Busy && !other.writer_open());
        if(stale==0) { delete e.replace_unit(0,3,new OscUnit); CHECK(e.move_unit_before(0,0,3)==2); }
        if(stale==1) { const auto state=r.state_token(); r.~RecUnit(); auto* current=new(&r) RecUnit; CHECK(current->state_token()!=state); }
        if(stale==2) e.set_external_sample_slot(r.sample_slot);
        if(stale==3) { auto replacement=bank().slot(r.sample_slot); install(r.sample_slot,std::move(replacement)); }
        const SampleImage before(r.sample_slot); cross(e,e.frames_per_beat());
        CHECK(!r.writer_open() && !r.recorded_frames() && !e.pending_rec_start); before.exact();
    }
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
    e.transport_play(); CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Beat)==Result::Ok);
    CHECK(e.request_chain_record(0,true,Q::Immediate)==Result::Busy); e.interrupt_recorders();
    CHECK(e.request_chain_record(0,true,Q::Immediate)==Result::Ok); feed(r,input(37)); CHECK(r.pause());
    CHECK(e.request_chain_record(0,true,Q::Immediate)==Result::Busy);
    CHECK(e.request_chain_record(0,false,Q::Beat)==Result::Ok); cross(e,e.frames_per_beat());
    CHECK(!r.writer_open() && r.recorded_frames()==37 && !e.recording_active_or_pending());
    std::puts("ok legacy queued chain REC revalidates moved/same-address/external owner/replaced sample, focused/legacy exclusion, paused Start refusal and Stop finalization");
}

void pending_stale() {
    for(int scenario=0;scenario<7;++scenario) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
        e.transport_play(); CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Beat)==Result::Ok);
        const auto endpoint=e.next_recorder_deadline(); std::unique_ptr<Unit> removed;
        if(scenario==0) { r.clear_take(); } // same zero length, changed state token
        if(scenario==1) { bank().clear_slot(r.sample_slot); CHECK(r.prepare_slot()==Result::Ok); }
        if(scenario==2) { r.sample_slot=18; CHECK(r.prepare_slot()==Result::Ok); }
        if(scenario==3) { delete e.replace_unit(0,3,new OscUnit); CHECK(e.move_unit_before(0,0,3)==2); }
        if(scenario==4) { removed.reset(e.replace_unit(0,0,new OscUnit)); }
        if(scenario==5) {
            Sample replacement=bank().slot(r.sample_slot); // pointer changes but generation does not
            bank().slot(r.sample_slot).data.swap(replacement.data);
        }
        if(scenario==6) {
            const auto state=r.state_token(); r.~RecUnit(); auto* again=new(&r) RecUnit;
            set(*again,RecUnit::P_LEN,8192); CHECK(again->state_token()!=state);
        }
        cross(e,endpoint); CHECK(!r.writer_open() && !r.recorded_frames());
        CHECK(e.recorder_pending_command(0,0)==Cmd::None && !e.recording_active_or_pending());
    }
    // Paused prefix has the same bank generation and frame count across transitions.
    for(Cmd pending:{Cmd::Pause,Cmd::Resume,Cmd::Stop}) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
        CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Immediate)==Result::Ok); feed(r,input(37));
        if(pending==Cmd::Resume) CHECK(r.pause());
        e.transport_play(); CHECK(e.request_recorder_command(0,0,pending,Q::Beat)==Result::Ok);
        const auto endpoint=e.next_recorder_deadline();
        if(pending==Cmd::Resume) { CHECK(r.resume()); CHECK(r.pause()); }
        else { CHECK(r.pause()); CHECK(r.resume()); }
        const auto state=r.state_token(); const bool paused=r.is_paused(); cross(e,endpoint);
        CHECK(r.writer_open() && r.is_paused()==paused && r.state_token()==state);
        CHECK(e.recorder_pending_command(0,0)==Cmd::None); r.stop();
    }
    // A replaced paused writer cannot append to/finalize the new bank content.
    for(int changed=0;changed<3;++changed) {
        reset_bank(); RecUnit r; CHECK(r.prepare_slot()==Result::Ok); r.trigger(); feed(r,input(19)); CHECK(r.pause());
        const int slot=r.sample_slot;
        if(changed==0) install(slot,sound(128,17));
        if(changed==1) { auto copy=bank().slot(slot); bank().slot(slot).data.swap(copy.data); }
        if(changed==2) r.sample_slot=64;
        const SampleImage replacement(slot); CHECK(!r.resume()); replacement.exact();
        r.stop(); CHECK(!r.writer_open()); replacement.exact();
    }
    std::puts("ok stale pending: unit replacement/move/reconstruction, state/session, bank generation, sample pointer/slot; paused ABA transitions and stale writer storage");
}

void ownership() {
    for(int alias=0;alias<4;++alias) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
        take(e,r,0,0,193,true); const int original=r.sample_slot; const SampleImage preserved(original);
        CHECK(e.recorder_is_private(r)); CHECK(e.sample_slot_refs(original).rec==1);
        if(alias==1) { auto* p=new PlayUnit; p->sample_slot=original; delete e.replace_unit(1,0,p); }
        if(alias==2) { auto* g=new GrainUnit; g->sample_slot=original; delete e.replace_unit(2,3,g); }
        if(alias==3) { e.set_external_sample_slot(original); CHECK(e.external_sample_slot()==original); }
        const auto refs=e.sample_slot_refs(original,&r);
        CHECK(refs.total()==(alias?1:0)); CHECK(refs.external==(alias==3?1:0));
        CHECK(e.recorder_is_private(r)==(alias==0));
        auto p=plan(e,r); CHECK((p.target_slot==original)==(alias==0));
        CHECK(e.commit_recorder_restart(p,0,0,Q::Immediate)==Result::Ok);
        CHECK(r.writer_open() && !r.recorded_frames());
        if(alias) { CHECK(r.sample_slot!=original); preserved.exact(); }
        feed(r,input(64,0,8)); r.stop();
        if(alias) preserved.exact();
        CHECK(e.sample_slot_refs(original).total()>=1);
        if(alias==3) { e.set_external_sample_slot(); CHECK(e.external_sample_slot()==-1); CHECK(e.sample_slot_refs(original).total()==0); }
    }
    // Clipboard alias alone remains an owner after its installed PLAY is removed.
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
    take(e,r); const auto original=r.sample_slot; auto* p=new PlayUnit; p->sample_slot=original; delete e.replace_unit(1,0,p);
    e.set_external_sample_slot(original); delete e.remove_unit(1,0); CHECK(e.sample_slot_refs(original,&r).external==1);
    const SampleImage old(original); CHECK(e.clear_recorder(r)==Result::Ok);
    CHECK(r.sample_slot!=original && !r.recorded_frames()); old.exact();
    e.set_external_sample_slot(100); CHECK(e.external_sample_slot()==-1);
    std::puts("ok private/PLAY/GRAN/external clipboard ownership, sample_slot_refs totals, COW restart and clipboard-only clear preserve source metadata/PCM");
}

void occupied() {
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e); take(e,r);
    install(25,sound(59,7),SampleOrigin::SdWav,"/fixture/existing.wav");
    const TakeImage take_before(r); const SampleImage target_before(25);
    RecWorkspacePlan denied; CHECK(e.plan_recorder(r,true,denied,25)!=Result::Ok); take_before.exact(); target_before.exact();
    CHECK(e.retarget_recorder(r,25)!=Result::Ok); take_before.exact(); target_before.exact();
    // A populated destination appearing after staging must be revalidated.
    auto stale=plan(e,r,26); stale.discard_unsaved=true; install(26,sound(67,9)); const SampleImage late(26);
    CHECK(e.commit_recorder_restart(stale,0,0,Q::Immediate)!=Result::Ok); take_before.exact(); late.exact();
    // Auto allocation skips populated unreferenced targets and external empty slots.
    e.set_external_sample_slot(r.sample_slot); for(int i=17;i<64;++i) if(i!=25) install(i,sound(3,i));
    auto alternate=plan(e,r); CHECK(alternate.target_slot<16 && alternate.target_slot!=r.sample_slot);
    for(int i=0;i<16;++i) install(i,sound(3,i));
    CHECK(e.plan_recorder(r,true,denied)==Result::NoFreeSlot); take_before.exact(); target_before.exact();
    std::puts("ok populated requested/late destination refusal, auto skips populated targets, full-bank rollback");
}

void stale_plan() {
    for(int scenario=0;scenario<8;++scenario) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
        take(e,r,0,0,193,true); auto p=plan(e,r,22); p.discard_unsaved=true;
        std::unique_ptr<Unit> removed;
        if(scenario==0) r.restore_take(r.recorded_frames());
        if(scenario==1) { Sample s=bank().slot(r.sample_slot); install(r.sample_slot,std::move(s)); }
        if(scenario==2) { install(22,sound(11,2)); }
        if(scenario==3) { e.set_external_sample_slot(22); }
        if(scenario==4) { removed.reset(e.replace_unit(0,0,new OscUnit)); }
        if(scenario==5) { delete e.replace_unit(0,3,new OscUnit); CHECK(e.move_unit_before(0,0,3)==2); }
        if(scenario==6) {
            const auto state=r.state_token(); r.~RecUnit(); auto* again=new(&r) RecUnit; set(*again,RecUnit::P_LEN,8192);
            again->restore_take(193); CHECK(again->state_token()!=state);
        }
        if(scenario==7) { Sample s=bank().slot(r.sample_slot); bank().slot(r.sample_slot).data.swap(s.data); }
        const TakeImage now(r); const SampleImage target(22);
        CHECK(e.commit_recorder_restart(p,0,0,Q::Immediate)!=Result::Ok); now.exact(); target.exact();
        CHECK(e.commit_recorder(p)!=Result::Ok); now.exact(); target.exact();
    }
    // Freed pointer must be validated against installation before dereference (ASAN).
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e); auto p=plan(e,r,23);
    delete e.replace_unit(0,0,new OscUnit); const SampleImage target(23);
    CHECK(e.commit_recorder(p)!=Result::Ok); CHECK(e.commit_recorder_restart(p,0,0,Q::Immediate)!=Result::Ok); target.exact();
    std::puts("ok staged stale commits: state/take/source generation/pointer, populated target/external owner, moved/deleted/freed/same-address reconstructed recorder");
}

void restart() {
    for(bool consent:{false,true}) for(bool saved:{false,true}) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e); take(e,r,0,0,113,saved);
        const TakeImage before(r); auto p=plan(e,r); p.discard_unsaved=consent;
        const auto result=e.commit_recorder_restart(p,0,0,Q::Immediate);
        if(!consent&&!saved) { CHECK(result==Result::Busy); before.exact(); }
        else { CHECK(result==Result::Ok); CHECK(r.writer_open() && !r.recorded_frames()); CHECK(r.session_token()!=before.session); r.stop(); }
    }
    for(int failure=0;failure<5;++failure) {
        reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e); take(e,r);
        // Force COW, hence staging and final memory projection are meaningful.
        e.set_external_sample_slot(r.sample_slot); auto p=plan(e,r); p.discard_unsaved=true;
        if(failure==0) bank().set_pcm_budget_for_test(bank().pcm_bytes_used());
        if(failure==1) p.staged.data.clear();
        if(failure==2) e.set_external_sample_slot(p.target_slot);
        if(failure==4) set(r,RecUnit::P_LEN,32767);
        if(failure==3) {
            auto& other=rec_at(e,1,2,31); (void)other; e.transport_play();
            CHECK(e.request_recorder_command(1,2,Cmd::Start,Q::Bar)==Result::Ok);
        }
        const TakeImage before(r); const SampleImage target(p.target_slot);
        CHECK(e.commit_recorder_restart(p,0,0,Q::Immediate)!=Result::Ok); before.exact(); target.exact();
    }
    // Quantized retake reserves the exact empty new take; stop cancels only start.
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e); take(e,r);
    auto p=plan(e,r); p.discard_unsaved=true; e.transport_play();
    CHECK(e.commit_recorder_restart(p,0,0,Q::Beat)==Result::Ok);
    CHECK(!r.writer_open() && !r.recorded_frames() && e.recorder_pending_command(0,0)==Cmd::Start);
    CHECK(e.request_recorder_command(0,0,Cmd::Stop,Q::Immediate)==Result::Ok);
    CHECK(!e.recording_active_or_pending()); render(e,e.frames_per_beat()+1); CHECK(!r.writer_open());
    // Successful save followed by staging failure keeps a saved, intact old take.
    reset_bank(); auto fp=std::make_unique<Engine>(); auto& f=*fp; RecUnit& s=rec_at(f); take(f,s);
    s.mark_take_saved(s.take_generation()); f.set_external_sample_slot(s.sample_slot);
    bank().set_pcm_budget_for_test(bank().pcm_bytes_used()); const TakeImage saved(s); RecWorkspacePlan denied;
    CHECK(f.plan_recorder(s,true,denied)==Result::MemoryBudget); saved.exact(); CHECK(!s.take_needs_save());
    std::puts("ok restart exact consent/saved matrix, staged invalid/budget/owner/pending rollback, queued fresh start cancellation, saved-prefix preservation");
}

void busy_save() {
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
    install(3,sound(512,4)); auto* play=new PlayUnit; play->sample_slot=3; delete e.replace_unit(2,0,play);
    delete e.replace_unit(0,1,new OscUnit); // audible unit makes same-chain audition refusal meaningful
    CHECK(play->bind_edit_source()); CHECK(!e.recording_active_or_pending());
    const std::string path=directory+"/busy-guard.snc"; const Bytes sentinel={'k','e','e','p'}; put(path,sentinel);
    CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Immediate)==Result::Ok); feed(r,input(91)); CHECK(r.pause());
    const TakeImage paused(r); CHECK(e.recording_active_or_pending()); RecWorkspacePlan p;
    CHECK(e.plan_recorder(r,true,p)==Result::Busy); CHECK(e.prepare_recorder(r)==Result::Busy);
    CHECK(e.clear_recorder(r)==Result::Busy); CHECK(e.retarget_recorder(r,21)==Result::Busy);
    CHECK(r.prepare_slot()==Result::Busy); paused.exact();
    r.clear_take(); r.restore_take(5); r.adopt_take(5); r.adopt_workspace(); r.reset(); paused.exact();
    r.mark_take_saved(r.take_generation()); CHECK(r.take_needs_save()); paused.exact();
    CHECK(e.start_play_preview(2,0,0,128,false)==0);
    CHECK(!e.begin_param_temp(2,0,PlayUnit::P_GAIN)); CHECK(!e.audition_sequence_step(0,0));
    CHECK(e.audition_sequence_step(2,0)); e.cancel_sequence_audition(); // existing audition policy is chain-local
    patchio::PatchDocument doc; CHECK(doc.valid()); CHECK(!patchio::capture(e,doc));
    CHECK(!patchio::save(e,path.c_str()) && bytes(path)==sentinel); paused.exact();
    // This is the actual core serializer guard, not a platform export/crop test.
    r.stop(); CHECK(!e.recording_active_or_pending()); CHECK(patchio::capture(e,doc));
    CHECK(patchio::save(doc,path.c_str())); const Bytes stopped=bytes(path); CHECK(stopped!=sentinel);
    const uint32_t preview=e.start_play_preview(2,0,0,128,false); CHECK(preview!=0); e.stop_play_preview(preview);
    CHECK(e.clear_recorder(r)==Result::Ok); e.transport_play();
    CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Beat)==Result::Ok);
    CHECK(e.recording_active_or_pending() && !r.writer_open());
    CHECK(!patchio::capture(e,doc) && !patchio::save(e,path.c_str()) && bytes(path)==stopped);
    CHECK(e.start_play_preview(2,0,0,128,false)==0);
    CHECK(e.request_recorder_command(0,0,Cmd::Stop,Q::Immediate)==Result::Ok);
    CHECK(!e.recording_active_or_pending());
    std::puts("ok paused open-writer mutation/prepare/adopt/restore/save-mark refusal, PLAY preview/temp globally busy + audition chain-local, real project capture/save and queued-start refusal preserve output");
}

void interrupt() {
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep;
    RecUnit& active=rec_at(e,0,0,16); RecUnit& paused=rec_at(e,1,2,17); RecUnit& queued=rec_at(e,2,1,18);
    CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Immediate)==Result::Ok); feed(active,input(31));
    CHECK(e.request_recorder_command(1,2,Cmd::Start,Q::Immediate)==Result::Ok); feed(paused,input(43)); CHECK(paused.pause());
    const SampleImage a(active.sample_slot),b(paused.sample_slot); const auto sa=active.session_token(),sb=paused.session_token();
    e.transport_play(); CHECK(e.request_recorder_command(2,1,Cmd::Start,Q::Bar)==Result::Ok);
    e.request_chain_trigger(3,Q::Bar); CHECK(e.pending_triggers!=0 && e.recording_active_or_pending());
    const auto allocations_before=rec_test_allocations; e.interrupt_recorders();
    CHECK(rec_test_allocations==allocations_before);
    CHECK(!active.writer_open() && !paused.writer_open() && !queued.writer_open());
    CHECK(active.recorded_frames()==31 && paused.recorded_frames()==43 && !queued.recorded_frames());
    CHECK(active.session_token()==sa && paused.session_token()==sb);
    CHECK(!e.pending_triggers && !e.pending_rec_start && !e.pending_rec_stop && !e.recording_active_or_pending());
    CHECK(e.recorder_pending_command(2,1)==Cmd::None); CHECK(active.take_needs_save() && paused.take_needs_save());
    // Finalization adds the normal chop0; PCM and slot allocation/generation stay.
    CHECK(bank().slot(active.sample_slot).data==a.value.data && bank().generation(active.sample_slot)==a.generation);
    CHECK(bank().slot(paused.sample_slot).data==b.value.data && bank().generation(paused.sample_slot)==b.generation);
    const TakeImage stopped_a(active),stopped_b(paused); e.interrupt_recorders(); stopped_a.exact(); stopped_b.exact();
    render(e,e.frames_per_beat()*5u); CHECK(!queued.writer_open() && !queued.recorded_frames());
    CHECK(e.request_chain_record(2,true,Q::Bar)==Result::Ok); CHECK(e.pending_rec_start!=0);
    e.request_chain_trigger(3,Q::Bar); e.interrupt_recorders();
    CHECK(!e.pending_rec_start && !e.pending_rec_stop && !e.pending_triggers && !e.recording_active_or_pending());
    // Engine-local clipboard ownership belongs to the runtime, not either patch.
    auto other=std::make_unique<Engine>(); e.set_external_sample_slot(21); other->set_external_sample_slot(22);
    e.swap_patch_state(*other); CHECK(e.external_sample_slot()==21 && other->external_sample_slot()==22);
    CHECK(e.sample_slot_refs(21).external==1 && other->sample_slot_refs(22).external==1);
    std::puts("ok interrupt finalizes recording/paused prefixes, cancels focused/legacy queues+triggers, idempotent; external ownership stays with engine across patch swap");
}

void serialization() {
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e); take(e,r,0,0,137,true);
    // Authored metadata must survive REC restore and aliases, not be regenerated.
    auto& s=bank().slot(r.sample_slot); s.chops[1]=57; s.slice_reverse_mask=2; s.slice_speed_q8[1]=384;
    s.root_note=62; s.exact_native_endpoint=true;
    bank().set_provenance(r.sample_slot,SampleOrigin::Recording,"/fixture/dictaphone.wav");
    auto* play=new PlayUnit; play->sample_slot=r.sample_slot; delete e.replace_unit(1,2,play); CHECK(play->bind_edit_source());
    auto* gran=new GrainUnit; gran->sample_slot=r.sample_slot; delete e.replace_unit(2,1,gran);
    e.set_external_sample_slot(r.sample_slot); const SampleImage before(r.sample_slot);
    const auto path=directory+"/finalized.snc"; CHECK(patchio::save(e,path.c_str())); before.exact();
    auto restored=std::make_unique<Engine>(); patchio::SampleManifest manifest;
    CHECK(patchio::load(*restored,path.c_str(),&manifest)); CHECK(restored->external_sample_slot()==-1);
    auto* rr=static_cast<RecUnit*>(restored->chains[0].units[0]); CHECK(rr && rr->kind==UnitKind::Recorder);
    CHECK(!rr->writer_open() && !rr->is_paused() && !rr->is_recording());
    rr->restore_take(before.value.playable_frames()); CHECK(rr->recorded_frames()==137 && !rr->take_needs_save()); before.exact();
    CHECK(restored->sample_slot_refs(rr->sample_slot).play==1 && restored->sample_slot_refs(rr->sample_slot).grain==1);
    CHECK(!std::strcmp(manifest.slots[rr->sample_slot].path,"/fixture/dictaphone.wav"));
    // Stable source edits encoded in existing optional chunks; no paused-session format.
    CHECK(manifest.has_chops && manifest.chops[rr->sample_slot][1]==57);
    CHECK(manifest.has_slice_reverse && manifest.slice_reverse[rr->sample_slot]==2);
    CHECK(manifest.has_slice_speed && manifest.slice_speed_q8[rr->sample_slot][1]==384);
    CHECK(rr->session_token()!=r.session_token());
    std::unique_ptr<Unit> clone(clone_unit(&r)); auto* cr=static_cast<RecUnit*>(clone.get());
    CHECK(cr && cr->sample_slot==r.sample_slot && !cr->writer_open() && !cr->recorded_frames());
    CHECK(cr->state_token()!=r.state_token());
    std::puts("ok finalized SNC7 real roundtrip: aliases/chops/reverse/speed/provenance preserved, external ownership/runtime session not serialized, clone starts closed");
}
void native_endpoint() {
    // Merely staging/queuing/cancelling an empty workspace must not opt it in.
    reset_bank(); auto ep=std::make_unique<Engine>(); auto& e=*ep; RecUnit& r=rec_at(e);
    const int slot=r.sample_slot; CHECK(!bank().slot(slot).exact_native_endpoint);
    e.transport_play(); CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Beat)==Result::Ok);
    CHECK(!bank().slot(slot).exact_native_endpoint);
    CHECK(e.request_recorder_command(0,0,Cmd::Stop,Q::Immediate)==Result::Ok);
    CHECK(!bank().slot(slot).exact_native_endpoint); e.transport_stop();
    // Accepted start, pause/resume and finalization retain the flag and last frame.
    CHECK(e.request_recorder_command(0,0,Cmd::Start,Q::Immediate)==Result::Ok);
    CHECK(bank().slot(slot).exact_native_endpoint);
    const Pcm first=input(79,0,7), second=input(58,79,7);
    Pcm expected=first; expected.insert(expected.end(),second.begin(),second.end());
    CHECK(expected.back()!=0); feed(r,first,13); CHECK(r.pause());
    CHECK(bank().slot(slot).exact_native_endpoint); CHECK(r.resume()); feed(r,second,11); r.stop();
    recorded(r,expected); CHECK(bank().slot(slot).exact_native_endpoint);
    // Real core WAV writer -> SNC -> manifest decode -> bank commit, no platform
    // exporter shim and no manual endpoint flag assignment on the new take.
    const auto wav=directory+"/native-endpoint.wav", snc=directory+"/native-endpoint.snc";
    CHECK(save_sample_wav(wav.c_str(),bank().slot(slot),ENGINE_SR,r.recorded_frames())==WavSaveResult::Ok);
    bank().set_provenance(slot,SampleOrigin::Recording,wav.c_str()); r.mark_take_saved(r.take_generation());
    CHECK(patchio::save(e,snc.c_str())); const SampleImage original(slot);
    auto restored=std::make_unique<Engine>(); patchio::SampleManifest manifest;
    CHECK(patchio::load(*restored,snc.c_str(),&manifest)); CHECK(manifest.exact_native[slot]); original.exact();
    auto decoded=std::make_unique<patchio::RestoredSamples>();
    CHECK(patchio::decode_samples(manifest,ENGINE_SR,ENGINE_SR,*decoded)==1);
    CHECK(decoded->ready[slot] && decoded->decoded[slot].data==expected);
    CHECK(patchio::commit_restored_samples(manifest,*decoded,restored.get()));
    CHECK(bank().slot(slot).data==expected && bank().slot(slot).exact_native_endpoint);
    auto* rr=static_cast<RecUnit*>(restored->chains[0].units[0]); CHECK(rr && rr->kind==UnitKind::Recorder);
    const SampleImage reloaded(slot); rr->restore_take(expected.size()); rr->stop(); reloaded.exact();
    CHECK(rr->recorded_frames()==expected.size());
    // Existing false AND true shared flags remain authoritative. Neither source
    // reconstruction nor rejected trigger/stop migrates old metadata in place.
    for(bool exact:{false,true}) {
        reset_bank(); auto old=std::make_unique<Engine>(); Sample s=sound(113,4); s.exact_native_endpoint=exact;
        install(19,std::move(s),SampleOrigin::Recording,"/old/shared.wav");
        auto* oldrec=new RecUnit; oldrec->sample_slot=19; delete old->replace_unit(0,0,oldrec);
        auto* alias=new PlayUnit; alias->sample_slot=19; delete old->replace_unit(1,0,alias);
        const SampleImage before(19); oldrec->restore_take(113); oldrec->trigger(); oldrec->stop(); before.exact();
        CHECK(bank().slot(19).exact_native_endpoint==exact);
        oldrec->adopt_take(113); CHECK(bank().slot(19).exact_native_endpoint==exact);
        const SampleImage adopted(19); oldrec->stop(); adopted.exact();
    }
    // Invalid fresh start cannot upgrade a populated legacy source either.
    reset_bank(); RecUnit rejected; Sample legacy=sound(31,9); legacy.exact_native_endpoint=false;
    install(rejected.sample_slot,std::move(legacy)); const SampleImage unchanged(rejected.sample_slot);
    rejected.trigger(); CHECK(!rejected.writer_open()); unchanged.exact();
    std::puts("ok native endpoint: accepted new start only; stage/queued cancel/rejected/old restore/adopt/stop unchanged; real WAV/SNC decode+commit all137 PCM frames exact including final nonzero frame");
}
using Case=void(*)();
struct NamedCase { const char* name; Case run; };
const NamedCase cases[]={
    {"lifecycle",lifecycle},{"limits",limits},{"engine-pcm",engine_pcm},{"commands",commands},
    {"legacy-queue",legacy_queue},{"pending-stale",pending_stale},{"ownership",ownership},{"occupied",occupied},
    {"stale-plan",stale_plan},{"restart",restart},{"busy-save",busy_save},{"interrupt",interrupt},{"serialization",serialization},
    {"native-endpoint",native_endpoint}
};
} // namespace
int main(int argc,char** argv) {
    CHECK(argc==3); directory=argv[1];
    for(const auto& test:cases) if(std::string(argv[2])==test.name) {
        test.run(); reset_bank();
        std::printf("PASS rec-dictaphone %s checks=%llu\n",test.name,(unsigned long long)checks); return 0;
    }
    std::fprintf(stderr,"unknown focused case: %s\n",argv[2]); return 2;
}
#endif
