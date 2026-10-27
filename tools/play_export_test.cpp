// Real service + real WAV/core tests. Only 3DS lock boundary and UI globals stubbed.
#include "../platform/3ds/ui_play_export.h"
#include "../platform/3ds/ui_state.h"
#include "engine/patch_io.h"
#include "synth/wav_loader.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace seance;
using namespace seance::platform::ui3ds;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
static unsigned checks=0,dirty=0;static size_t max_guard_copy=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace seance::platform::ui3ds {
Engine* g_eng=nullptr;platform::AudioOut* g_audio=nullptr;Unit* g_clip_unit=nullptr;
int g_edit_chain=0,g_edit_slot=0;bool g_seq_lock_armed=false;
View g_view=View::Chains;Overlay g_overlay=Overlay::Editor;
const char* RECORDING_DIR=nullptr;
void mark_project_mutated(){++dirty;}
}
static bool fail_read=false;static void(*after_rename)()=nullptr;
extern "C" {
size_t __real_fread(void*,size_t,size_t,FILE*);
size_t __wrap_fread(void*p,size_t a,size_t b,FILE*f){CHECK(!play_export_guard_depth);if(fail_read)return 0;return __real_fread(p,a,b,f);}
size_t __real_fwrite(const void*,size_t,size_t,FILE*);
size_t __wrap_fwrite(const void*p,size_t a,size_t b,FILE*f){CHECK(!play_export_guard_depth);return __real_fwrite(p,a,b,f);}
int __real_rename(const char*,const char*);
int __wrap_rename(const char*a,const char*b){CHECK(!play_export_guard_depth);const int r=__real_rename(a,b);if(!r&&after_rename){auto hook=after_rename;after_rename=nullptr;hook();}return r;}
void* __real_memcpy(void*,const void*,size_t);
void* __wrap_memcpy(void*a,const void*b,size_t n){if(play_export_guard_depth){max_guard_copy=std::max(max_guard_copy,n);CHECK(n<=2048*2*sizeof(int16_t));}return __real_memcpy(a,b,n);}
}
static size_t files(const std::string& path){return (size_t)std::distance(std::filesystem::directory_iterator(path),std::filesystem::directory_iterator{});}
static std::string bytes(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void guard_mutation(){if(play_export_unlocks==2){play_export_after_unlock=nullptr;g_eng->chains[0].units[0]->params[0].base=7777;}}
int main(){
    char directory[]="/tmp/seance-play-export-XXXXXX";CHECK(mkdtemp(directory));std::string path=directory;RECORDING_DIR=directory;
    std::ofstream(path+"/play_0001.wav")<<"keep original";std::ofstream(path+"/play_0002.wav.tmp")<<"keep old temp";
    auto engine=std::make_unique<Engine>();platform::AudioOut audio;g_eng=engine.get();g_audio=&audio;
    auto& bank=SampleBank::instance();for(int i=0;i<64;++i)bank.clear_slot(i);
    Sample source;source.channels=2;source.root_note=48;source.loop_start=700;source.loop_end=2300;source.reversed=true;
    source.data.resize(3000*2);for(int i=0;i<3000;++i){source.data[i*2]=(q15)(5000+(i%200-100)*80);source.data[i*2+1]=(q15)(-3000+(i%100-50)*40);}
    source.chops[0]=0;source.chops[3]=1000;source.chops[7]=1700;source.slice_reverse_mask=1u<<3;
    source.slice_speed_q8[0]=128;source.slice_speed_q8[3]=512;source.slice_speed_q8[7]=1024;
    const auto original=source.data;CHECK(bank.replace_sample(4,std::move(source),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
    auto*play=new PlayUnit;play->sample_slot=4;delete engine->replace_unit(0,0,play);
    auto*grain=new GrainUnit;grain->sample_slot=0;delete engine->replace_unit(0,1,grain);
    auto*rec=new RecUnit;rec->sample_slot=1;delete engine->replace_unit(0,2,rec);
    auto*alias=new PlayUnit;alias->sample_slot=2;delete engine->replace_unit(0,3,alias);
    bank.set_provenance(3,trackr::synth::SampleOrigin::Recording,"missing.wav",true);
    GrainUnit clipboard;clipboard.sample_slot=5;g_clip_unit=&clipboard;
    PlayEditState edit;edit.enabled=true;edit.source_frames=3000;edit.start=100;edit.end=2800;edit.fade_in=200;edit.fade_out=400;edit.loop_start=1000;edit.loop_end=2200;edit.crossfade=50;
    CHECK(play->set_edit_state(edit,bank.generation(4)));
    Param params[PARAM_BANK_SIZE];std::memcpy(params,play->params,sizeof params);
    PlayExportOptions options;options.first=200;options.end=800;options.dc=options.normalize=options.bake=options.reverse=true;
    const std::string result=play_export_copy(*play,options);CHECK(result.find("slot 07")!=std::string::npos);CHECK(dirty==1);
    CHECK(play->sample_slot==4&&bank.slot(4).data==original&&play_edit_equal(play->edit_state(),edit));
    CHECK(!std::memcmp(params,play->params,sizeof params));CHECK(!play_export_guard_depth&&max_guard_copy<=8192);
    CHECK(bytes(path+"/play_0001.wav")=="keep original"&&bytes(path+"/play_0002.wav.tmp")=="keep old temp");
    CHECK(!std::filesystem::exists(path+"/play_0002.wav"));
    const auto&copy=bank.slot(6);CHECK(copy.channels==2&&copy.playable_frames()==600&&copy.root_note==48);
    CHECK(std::string(bank.provenance(6).path).find("play_0003.wav")!=std::string::npos);
    int64_t sums[2]={};for(int i=200;i<800;++i)for(int ch=0;ch<2;++ch)sums[ch]+=original[i*2+ch];
    std::vector<int> processed(1200);int peak=0;
    for(int f=0;f<600;++f)for(int ch=0;ch<2;++ch){const int at=799-f;int v=original[at*2+ch]-(int)(sums[ch]/600);const int env=at<300?(at-100)*32767/200:32767;v=(int)((int64_t)v*env/32767);processed[f*2+ch]=v;peak=std::max(peak,std::abs(v));}
    int measured=0;for(int i=0;i<1200;++i){CHECK(copy.data[i]==(int64_t)processed[i]*29204/peak);measured=std::max(measured,std::abs((int)copy.data[i]));}CHECK(measured==29204);
    CHECK(copy.exact_native_endpoint);const auto expected_copy=copy.data;
    CHECK(bank.slot(0).empty()&&bank.slot(1).empty()&&bank.slot(2).empty()&&bank.slot(3).empty()&&bank.slot(5).empty());
    options.detach=true;options.first=1;options.end=2;CHECK(std::string(play_export_copy(*play,options)).find("detached")!=std::string::npos);
    CHECK(play->sample_slot==7&&bank.slot(7).data==original&&bank.slot(4).data==original&&dirty==2);
    CHECK(play->edit_source_matches()&&play_edit_equal(play->edit_state(),edit));CHECK(!std::memcmp(params,play->params,sizeof params));
    CHECK(bank.slot(7).root_note==48&&bank.slot(7).loop_start==700&&bank.slot(7).loop_end==2300&&bank.slot(7).reversed);
    CHECK(!std::memcmp(bank.slot(7).chops,bank.slot(4).chops,sizeof bank.slot(4).chops)&&bank.slot(7).slice_reverse_mask==(1u<<3));
    CHECK(!std::memcmp(bank.slot(7).slice_speed_q8,bank.slot(4).slice_speed_q8,sizeof source.slice_speed_q8));
    options={};options.first=0;options.end=3000;
    size_t count=files(path);play_export_unlocks=0;play_export_after_unlock=guard_mutation;
    CHECK(std::string(play_export_copy(*play,options)).find("changed")!=std::string::npos);
    CHECK(files(path)==count&&play->sample_slot==7&&dirty==2&&play->params[0].base==7777);play->params[0].base=params[0].base;
    // WAV commit succeeds, but a new recorder reference steals the planned slot
    // before decoded PCM can be installed: file retained, bank/PLAY untouched.
    after_rename=[](){auto*r=new RecUnit;r->sample_slot=8;delete g_eng->replace_unit(1,0,r);};
    CHECK(std::string(play_export_copy(*play,options)).find("context changed")!=std::string::npos);
    CHECK(files(path)==count+1&&bank.slot(8).empty()&&play->sample_slot==7&&dirty==2);count=files(path);
    fail_read=true;CHECK(std::string(play_export_copy(*play,options)).find("reload failed")!=std::string::npos);fail_read=false;
    CHECK(files(path)==count+1&&bank.slot(9).empty()&&dirty==2);count=files(path);
    const auto budget=bank.pcm_budget_bytes();bank.set_pcm_budget_for_test(1);
    CHECK(std::string(play_export_copy(*play,options)).find("memory budget")!=std::string::npos);bank.set_pcm_budget_for_test(budget);
    CHECK(files(path)==count&&bank.slot(4).data==original&&bank.slot(7).data==original);
    const std::string unavailable=path+"/absent";RECORDING_DIR=unavailable.c_str();
    CHECK(std::string(play_export_copy(*play,options)).find("sd error")!=std::string::npos);RECORDING_DIR=directory;
    CHECK(files(path)==count&&dirty==2&&play_edit_equal(play->edit_state(),edit));
    g_seq_lock_armed=true;CHECK(std::string(play_export_copy(*play,options)).find("unavailable")!=std::string::npos);g_seq_lock_armed=false;
    CHECK(max_guard_copy==8192);g_clip_unit=nullptr;
    // Cut is a real short source, not a trim write or a processed save-copy.
    auto*shared=new PlayUnit;shared->sample_slot=7;delete engine->replace_unit(1,1,shared);
    CHECK(engine->set_step_lock(0,2,0,PlayUnit::P_START,12000));const auto pattern=engine->pattern;
    PlayExportOptions crop;crop.crop=true;crop.first=edit.start;crop.end=edit.end;
    crop.normalize=crop.dc=crop.bake=crop.reverse=true; // ignored by raw crop
    fail_read=true;CHECK(std::string(play_export_copy(*play,crop)).find("reload failed")!=std::string::npos);fail_read=false;
    CHECK(play->sample_slot==7&&play_edit_equal(play->edit_state(),edit)&&bank.slot(7).data==original);
    after_rename=[](){SampleBank::instance().slot(7).slice_speed_q8[3]=256;};
    CHECK(std::string(play_export_copy(*play,crop)).find("context changed")!=std::string::npos);
    CHECK(play->sample_slot==7);bank.slot(7).slice_speed_q8[3]=512;
    CHECK(std::string(play_export_copy(*play,crop)).find("cut to slot")!=std::string::npos);
    const int cropped_slot=play->sample_slot;CHECK(cropped_slot!=7&&shared->sample_slot==7&&bank.slot(7).data==original);
    const auto&cut=bank.slot(cropped_slot);CHECK(cut.channels==2&&cut.playable_frames()==2700);
    CHECK(std::equal(cut.data.begin(),cut.data.end(),original.begin()+200));
    CHECK(cut.chops[0]==0&&cut.chops[3]==900&&cut.chops[7]==1600&&cut.slice_reverse_mask==(1u<<3));
    CHECK(cut.slice_speed_q8[0]==128&&cut.slice_speed_q8[3]==512&&cut.slice_speed_q8[7]==1024);
    CHECK(cut.loop_start==600&&cut.loop_end==2200&&cut.reversed);
    const auto cropped_edit=play->edit_state();CHECK(cropped_edit.start==0&&cropped_edit.end==2700&&cropped_edit.source_frames==2700);
    CHECK(cropped_edit.fade_in==200&&cropped_edit.fade_out==400&&cropped_edit.loop_start==900&&cropped_edit.loop_end==2100&&cropped_edit.crossfade==50);
    CHECK(cropped_edit.anchor_start==0&&cropped_edit.anchor_length==32767&&play->edit_source_matches());
    CHECK(play->params[PlayUnit::P_START].base==0&&play->params[PlayUnit::P_LEN].base==32767);
    CHECK(!std::memcmp(&pattern,&engine->pattern,sizeof pattern));
    for(int p=0;p<PARAM_BANK_SIZE;++p)if(p!=PlayUnit::P_START&&p!=PlayUnit::P_LEN)CHECK(!std::memcmp(&params[p],&play->params[p],sizeof(Param)));
    crop.first=0;crop.end=2700;count=files(path);CHECK(std::string(play_export_copy(*play,crop)).find("already whole")!=std::string::npos);CHECK(files(path)==count);
    // The new decode endpoint is opt-in. Existing loader users keep historical
    // final silence; validated SEXT preserves newly exported source slots even
    // when no PLAY/PEDT references the ordinary copy during hydration.
    Sample historical,exact;trackr::synth::WavDecodeLimits limits;
    limits.workspace_bytes=trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    const std::string detached=bank.provenance(7).path;
    CHECK(trackr::synth::load_wav_to_sample(detached.c_str(),historical,ENGINE_SR,3000,limits)==trackr::synth::WavLoadResult::Ok);
    CHECK(historical.data.back()==0&&original.back()!=0);
    limits.preserve_native_endpoint=true;
    CHECK(trackr::synth::load_wav_to_sample(detached.c_str(),exact,ENGINE_SR,3000,limits)==trackr::synth::WavLoadResult::Ok);
    CHECK(exact.data==original);
    const std::string patch=path+"/exact.snc";CHECK(patchio::save(*engine,patch.c_str()));
    auto staged=std::make_unique<Engine>();auto manifest=std::make_unique<patchio::SampleManifest>();
    CHECK(patchio::load(*staged,patch.c_str(),manifest.get()));CHECK(manifest->exact_native[7]&&manifest->exact_native[6]);
    auto decoded=std::make_unique<patchio::RestoredSamples>();
    CHECK(patchio::decode_samples(*manifest,ENGINE_SR,3000,*decoded)>=2);
    CHECK(decoded->ready[7]&&decoded->decoded[7].data==original);
    CHECK(decoded->ready[6]&&decoded->decoded[6].data==expected_copy);
    CHECK(patchio::commit_restored_samples(*manifest,*decoded,staged.get()));
    CHECK(static_cast<PlayUnit*>(staged->chains[0].units[0])->edit_source_matches()&&bank.slot(7).data==original);
    CHECK(static_cast<PlayUnit*>(staged->chains[0].units[0])->sample_slot==cropped_slot&&bank.slot(cropped_slot).slice_speed_q8[3]==512);
    CHECK(play_edit_equal(static_cast<PlayUnit*>(staged->chains[0].units[0])->edit_state(),cropped_edit));
    std::filesystem::remove_all(path); // only this test's private fixtures
    std::printf("play export: passed (%u checks); max guarded copy %zu bytes, no guarded WAV I/O\n",checks,max_guard_copy);
}
