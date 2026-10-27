// Focused actual-engine REC waveform proof; no UI hardware, SD, or network.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
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
namespace {
uint64_t checks=0, pcm_reads=0, summary_reads=0, peak_pcm=0, peak_summary=0;
uint64_t oracle_samples=0, twin_samples=0, allocations=0;
bool allocation_watch=false;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using Wave=std::array<q15,64>;
using Bytes=std::vector<unsigned char>;
std::string directory;
void reads_reset() { pcm_reads=summary_reads=0; }
void bounded(uint64_t pcm=32768,uint64_t summaries=2500) {
    peak_pcm=std::max(peak_pcm,pcm_reads);peak_summary=std::max(peak_summary,summary_reads);
    CHECK(pcm_reads<=pcm);CHECK(summary_reads<=summaries);
}
struct Guard { Guard(){allocation_watch=true;} ~Guard(){allocation_watch=false;} };
void set(Unit& u,int p,int v){u.params[p].base=q15(v);u.params[p].snap();}
int magnitude(q15 v){return v<0?-int(v):int(v);}
q15 signal(uint32_t i,int seed=0) {
    const uint32_t value=(i+uint32_t(seed)*7919u)*1664525u+1013904223u;
    return q15(int((value>>8)%40001u)-20000);
}
Sample prepared(uint32_t frames,int seed=0,int channels=1) {
    Sample s;s.channels=uint8_t(channels);s.data.resize(size_t(frames)*channels);
    for(uint32_t i=0;i<frames;++i) for(int c=0;c<channels;++c)
        s.data[size_t(i)*channels+c]=c?q15(-32768):signal(i,seed);
    s.logical_frames=frames;return s;
}
void install(int slot,Sample&& s) {CHECK(SampleBank::instance().replace_sample(slot,std::move(s),SampleOrigin::Runtime)==SampleEditResult::Ok);}
Wave oracle(const Sample& s,uint32_t frames,uint32_t prefix=UINT32_MAX) {
    Wave result{};
    for(int point=0;point<64;++point){
        if(!frames||!s.num_frames())continue;
        uint32_t first=uint32_t(uint64_t(point)*frames/64);
        uint32_t end=uint32_t(uint64_t(point+1)*frames/64);
        if(end<=first)end=first+1;
        end=std::min(end,s.num_frames());
        if(first>=end)continue;
        end=first+std::min(end-first,prefix);
        int largest=-1;
        for(uint32_t i=first;i<end;++i){const q15 v=s.data[size_t(i)*s.channels];++oracle_samples;
            if(magnitude(v)>largest){largest=magnitude(v);result[point]=v;}}
    }
    return result;
}
uint64_t hash_pcm(const std::vector<q15>& data) {
    uint64_t h=1469598103934665603ULL;
    for(q15 v:data){h^=uint16_t(v);h*=1099511628211ULL;}return h;
}
Bytes read_file(const std::string& path) {std::ifstream f(path,std::ios::binary);CHECK(f.good());return {std::istreambuf_iterator<char>(f),{}};}
void write_file(const std::string& path,const void* data,size_t bytes) {std::ofstream f(path,std::ios::binary);f.write(static_cast<const char*>(data),bytes);CHECK(f.good());}

#ifndef SEANCE_REC_WAVE_LEGACY
Wave copy(const RecUnit& rec) {Wave out;reads_reset();{Guard guard;rec.copy_visual_wave(out.data());}bounded();return out;}
void exact(const RecUnit& rec){CHECK(copy(rec)==oracle(SampleBank::instance().slot(rec.sample_slot),rec.recorded_frames()));}
void warm(const RecUnit& rec,uint32_t frames,bool from_start=false) {
    const auto& sample=SampleBank::instance().slot(rec.sample_slot);
    const uint32_t publications=std::max(1u,(uint32_t((uint64_t(frames)+63)/64)+511)/512);
    uint64_t total=0;
    for(uint32_t n=1;n<=publications;++n){const Wave actual=copy(rec);if(from_start)CHECK(actual==oracle(sample,frames,n*512));total+=pcm_reads;CHECK(summary_reads==0);}
    CHECK(copy(rec)==oracle(sample,frames));CHECK(pcm_reads==0&&summary_reads==0);
    if(from_start&&frames==640000&&sample.num_frames()>=frames)CHECK(total==640000);
}
void streaming() {
    RecUnit rec;rec.sample_slot=40;set(rec,RecUnit::P_LEN,32767);
    Sample s;CHECK(RecUnit::stage_workspace(640000,s)==SampleEditResult::Ok);install(40,std::move(s));rec.adopt_workspace();
    {Guard guard;rec.trigger();}CHECK(rec.is_recording());exact(rec);
    std::array<q15,997> input{};const int sizes[]={1,2,3,7,31,63,64,65,127,128,129,255,256,257,511,512,513,997};
    const auto* data=SampleBank::instance().slot(40).data.data();uint32_t turns=0;
    while(rec.is_recording()) {
        const int n=turns<128?sizes[turns%18]:512;
        const uint32_t first=rec.recorded_frames();
        for(int i=0;i<n;++i)input[i]=signal(first+uint32_t(i));
        // Deliberate signed ties at block boundaries, including the q15 minimum.
        for(int i=0;i<n;++i){const uint32_t p=first+uint32_t(i);if(p%8191==254)input[i]=32767;if(p%8191==255)input[i]=-32767;if(p%16381==1)input[i]=-32768;}
        reads_reset();{Guard guard;rec.capture_input(input.data(),n);}CHECK(pcm_reads==0&&summary_reads==0);
        const auto& sample=SampleBank::instance().slot(40);
        for(uint32_t i=first;i<rec.recorded_frames();++i)CHECK(sample.data[i]==input[i-first]);
        CHECK(sample.data.data()==data);exact(rec);bounded(32640,2500);
        CHECK(copy(rec)==oracle(sample,rec.recorded_frames()));CHECK(pcm_reads==0&&summary_reads==0);
        ++turns;
    }
    CHECK(rec.recorded_frames()>639000);CHECK(rec.take_needs_save());
    const auto h=hash_pcm(SampleBank::instance().slot(40).data);const Wave final=copy(rec);
    const auto generation=rec.take_generation();
    for(int n=0;n<10000;++n){CHECK(copy(rec)==final);CHECK(pcm_reads==0&&summary_reads==0);}
    CHECK(hash_pcm(SampleBank::instance().slot(40).data)==h);CHECK(rec.take_generation()==generation);
    rec.trigger();CHECK(!rec.is_recording());CHECK(copy(rec)==final);CHECK(pcm_reads==0);
    rec.mark_take_saved(generation);CHECK(!rec.take_needs_save());CHECK(copy(rec)==final);
    // Early stop retains exact variable-duration bins; clear/reset/workspace use no stale peaks.
    for(int count:{1,2,63,64,65,255,256,257,4097,17313}){
        {Guard guard;rec.clear_take();rec.trigger();}CHECK(rec.is_recording());
        for(int at=0;at<count;){const int n=std::min(count-at,997);for(int i=0;i<n;++i)input[i]=q15((at+i)%2?12345:-12345);{Guard guard;rec.capture_input(input.data(),n);}at+=n;}
        {Guard guard;rec.stop();}CHECK(rec.recorded_frames()==uint32_t(count));exact(rec);
        CHECK(copy(rec)[0]==-12345);rec.reset();CHECK(copy(rec)==Wave{});
    }
    rec.adopt_workspace();CHECK(copy(rec)==Wave{});
    std::printf("ok streamed growth + signed ties + early stop/reset/clear/retrigger; %u changing snapshots; 10000 finalized zero-read snapshots\n",turns);
}
void restored_and_replaced() {
    auto& bank=SampleBank::instance();RecUnit rec;rec.sample_slot=41;
    Sample s=prepared(640000);for(uint32_t i=9999;i<640000;i+=10000)s.data[i]=-32768;
    install(41,std::move(s));reads_reset();{Guard guard;rec.adopt_take(640000);}CHECK(pcm_reads==0);CHECK(!rec.take_needs_save());
    CHECK(copy(rec)[63]!=-32768);warm(rec,640000);
    {Guard guard;rec.adopt_take(640000);}warm(rec,640000,true);
    const Wave final=copy(rec);for(int i=0;i<10000;++i){CHECK(copy(rec)==final);CHECK(pcm_reads==0);}
    // Same storage, length AND bank generation: explicit adoption is the invalidation fence.
    auto& live=bank.slot(41);const auto* pointer=live.data.data();const auto generation=bank.generation(41);
    std::fill(live.data.begin(),live.data.end(),q15(2345));{Guard guard;rec.adopt_take(640000);}CHECK(copy(rec)[63]==2345);warm(rec,640000);
    CHECK(live.data.data()==pointer&&bank.generation(41)==generation);
    // Generation-only change: return the very same allocation to its slot.
    Sample retired;bank.swap_sample_unchecked(41,retired,SampleOrigin::Runtime);
    std::fill(retired.data.begin(),retired.data.end(),q15(-8765));bank.swap_sample_unchecked(41,retired,SampleOrigin::Runtime);
    CHECK(live.data.data()==pointer&&bank.generation(41)!=generation);CHECK(copy(rec)[63]==-8765);rec.adopt_take(640000);warm(rec,640000);
    // Pointer-only replacement, preserving bank generation and element count.
    auto replacement=prepared(640000,9);const auto before=bank.generation(41);live.data.swap(replacement.data);
    CHECK(bank.generation(41)==before&&live.data.data()!=pointer);CHECK(copy(rec)==oracle(live,640000,512));rec.adopt_take(640000);warm(rec,640000);
    // Stereo must keep LEFT signed maxima; the stronger right lane is ignored.
    install(41,prepared(640000,14,2));rec.adopt_take(640000);warm(rec,640000);
    // Channels-only, then size-only mutation, without bank generation/adopt.
    live.channels=1;CHECK(copy(rec)==oracle(live,640000,512));rec.adopt_take(640000);warm(rec,640000);
    live.data.resize(8000);CHECK(copy(rec)==oracle(live,640000,512));warm(rec,640000);
    // Different slot and empty/invalid slots never leak an old finalized waveform.
    install(42,prepared(640000,37));rec.sample_slot=42;CHECK(copy(rec)==oracle(bank.slot(42),640000,512));
    bank.clear_slot(42);CHECK(copy(rec)==Wave{});rec.sample_slot=-1;CHECK(copy(rec)==Wave{});rec.sample_slot=64;CHECK(copy(rec)==Wave{});
    // Imported lengths greater than recorder capacity stay strictly bounded too.
    rec.sample_slot=42;install(42,prepared(1280001,6));rec.adopt_take(1280001);warm(rec,1280001);
    // Replacement while warming restarts all bins, without showing the retired take.
    install(42,prepared(640000,1));rec.adopt_take(640000);copy(rec);copy(rec);
    install(42,prepared(640000,2));CHECK(copy(rec)==oracle(bank.slot(42),640000,512));
    std::puts("ok 20-publication restored take; same-pointer/generation/adopt/slot/stereo/size/empty/oversize replacement; 10000 restored zero-read snapshots");
}
void focus_clone_persistence() {
    auto& bank=SampleBank::instance();auto engine=std::make_unique<Engine>();
    auto* a=new RecUnit;auto* b=new RecUnit;a->sample_slot=43;b->sample_slot=44;
    delete engine->replace_unit(0,1,a);delete engine->replace_unit(2,3,b);
    install(43,prepared(65536,2));install(44,prepared(32768,8));a->adopt_take(65536);b->adopt_take(32768);
    EngineUiSnapshot snapshot;reads_reset();{Guard guard;engine->capture_ui_snapshot(snapshot);}CHECK(pcm_reads==0&&snapshot.rec_waveform_valid==0);
    CHECK(snapshot.rec_frames[0][1]==65536&&snapshot.rec_frames[2][3]==32768);
    for(int n=0;n<3;++n){reads_reset();{Guard guard;engine->capture_ui_snapshot(snapshot,0,1);}bounded();}
    CHECK(snapshot.rec_waveform_chain==0&&snapshot.rec_waveform_slot==1);
    CHECK(std::equal(std::begin(snapshot.rec_waveform),std::end(snapshot.rec_waveform),oracle(bank.slot(43),65536).begin()));
    reads_reset();{Guard guard;engine->capture_ui_snapshot(snapshot,2,3);}bounded();CHECK(snapshot.rec_waveform_chain==2&&snapshot.rec_waveform_slot==3);
    CHECK(std::equal(std::begin(snapshot.rec_waveform),std::end(snapshot.rec_waveform),oracle(bank.slot(44),32768).begin()));
    reads_reset();{Guard guard;engine->capture_ui_snapshot(snapshot);}CHECK(pcm_reads==0&&snapshot.rec_waveform_valid==0);for(q15 v:snapshot.rec_waveform)CHECK(v==0);
    std::unique_ptr<Unit> clone(clone_unit(a));auto* c=static_cast<RecUnit*>(clone.get());CHECK(c&&c->recorded_frames()==0);CHECK(copy(*c)==Wave{});
    const std::string path=directory+"/rec-before.snc",after=directory+"/rec-after.snc";
    CHECK(patchio::save(*engine,path.c_str()));for(int i=0;i<100;++i)engine->capture_ui_snapshot(snapshot,0,1);
    CHECK(patchio::save(*engine,after.c_str()));CHECK(read_file(path)==read_file(after));
    Engine loaded;CHECK(patchio::load(loaded,path.c_str()));auto* restored=static_cast<RecUnit*>(loaded.chains[0].units[1]);
    CHECK(restored&&restored->recorded_frames()==0&&copy(*restored)==Wave{});restored->adopt_take(bank.slot(43).playable_frames());warm(*restored,65536);
    // Actual staged whole-bank restore swaps generations even for same frame counts.
    patchio::SampleManifest manifest;patchio::RestoredSamples staged;
    staged.ready[43]=true;staged.decoded[43]=prepared(65536,23);
    CHECK(patchio::commit_restored_samples(manifest,staged));restored->adopt_take(65536);warm(*restored,65536);
    std::puts("ok engine focus/no-focus/clone; serializer byte-identical before/after 100 snapshots; actual patch load and staged sample restore");
}
#endif

void setup_engine(Engine& e,int slot) {
    auto* osc=new OscUnit;set(*osc,OscUnit::P_PITCH,13900);set(*osc,OscUnit::P_GAIN,9000);osc->control_tick();
    auto* rec=new RecUnit;rec->sample_slot=slot;set(*rec,RecUnit::P_LEN,32767);
    delete e.replace_unit(0,0,osc);delete e.replace_unit(0,1,rec);delete e.replace_unit(0,2,new FilterUnit);
    CHECK(rec->prepare_slot()==SampleEditResult::Ok);rec->trigger();
}
void dual_engine() {
    // Isolate the serialized global bank from earlier lifecycle fixtures. This
    // resets test memory only; no project/sample file is removed or edited.
    for(int slot=0;slot<SAMPLE_BANK_SIZE;++slot)SampleBank::instance().clear_slot(slot);
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();setup_engine(*a,50);setup_engine(*b,51);
    auto* ra=static_cast<RecUnit*>(a->chains[0].units[1]);auto* rb=static_cast<RecUnit*>(b->chains[0].units[1]);
    std::array<q15,1024> pa{},pb{};EngineUiSnapshot snapshot;std::vector<q15> output;output.reserve(1024*1400);
    for(int block=0;block<1400;++block){
        if(block==70){set(*ra,RecUnit::P_MON,0);set(*rb,RecUnit::P_MON,0);}
        if(block==90){ra->bypass=true;rb->bypass=true;}
        if(block==110){ra->bypass=false;rb->bypass=false;set(*ra,RecUnit::P_MON,32767);set(*rb,RecUnit::P_MON,32767);}
        if(block==170){ra->stop();rb->stop();}
        if(block==180){ra->clear_take();rb->clear_take();ra->trigger();rb->trigger();}
        if(block==200){set(*ra,RecUnit::P_REC,32767);set(*rb,RecUnit::P_REC,32767);}
        const int frames=(block%19==0)?257:512;
        {Guard guard;a->render(pa.data(),frames);b->render(pb.data(),frames);a->capture_ui_snapshot(snapshot,0,1);}
        CHECK(std::equal(pa.begin(),pa.begin()+frames*2,pb.begin()));twin_samples+=frames*2;
        CHECK(ra->recorded_frames()==rb->recorded_frames()&&ra->is_recording()==rb->is_recording());
        output.insert(output.end(),pa.begin(),pa.begin()+frames*2);
    }
    const auto& sa=SampleBank::instance().slot(50);const auto& sb=SampleBank::instance().slot(51);
    CHECK(sa.data==sb.data&&sa.logical_frames==sb.logical_frames&&sa.loop_end==sb.loop_end);twin_samples+=sa.data.size();
    CHECK(hash_pcm(output)!=hash_pcm(std::vector<q15>(output.size(),0)));
    write_file(directory+"/engine-output.pcm",output.data(),output.size()*sizeof(q15));
    write_file(directory+"/recorded-take.pcm",sa.data.data(),sa.data.size()*sizeof(q15));
    CHECK(patchio::save(*a,(directory+"/engine.snc").c_str()));
    std::printf("ok dual actual engines: frequent snapshots vs none, monitor/bypass/stop/clear/gate; %llu identical q15 samples; sizeof RecUnit=%zu Unit=%zu\n",(unsigned long long)twin_samples,sizeof(RecUnit),sizeof(Unit));
}
}

// Test-only linker wrappers: track all usual C++ allocation forms without
// replacing allocator semantics (compatible with ASan's malloc/new accounting).
extern "C" void* __real__Znwm(size_t);
extern "C" void* __wrap__Znwm(size_t n){if(allocation_watch)++allocations;return __real__Znwm(n);}
extern "C" void* __real__Znam(size_t);
extern "C" void* __wrap__Znam(size_t n){if(allocation_watch)++allocations;return __real__Znam(n);}
extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&);
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t& t){if(allocation_watch)++allocations;return __real__ZnwmRKSt9nothrow_t(n,t);}
namespace seance {void rec_wave_cache_test_read(bool summary){if(summary)++summary_reads;else ++pcm_reads;}}
int main(int argc,char** argv){
    CHECK(argc>=2);directory=argv[1];
    if(argc==3&&std::string(argv[2])=="--equivalence"){dual_engine();CHECK(allocations==0);return 0;}
#ifndef SEANCE_REC_WAVE_LEGACY
    streaming();restored_and_replaced();focus_clone_persistence();dual_engine();CHECK(allocations==0);
    std::printf("PASS rec cache: %llu checks, %llu independent-oracle PCM, peak %llu PCM + %llu summaries/publication, guarded allocations=%llu\n",(unsigned long long)checks,(unsigned long long)oracle_samples,(unsigned long long)peak_pcm,(unsigned long long)peak_summary,(unsigned long long)allocations);
#else
    CHECK(false);
#endif
    return 0;
}
