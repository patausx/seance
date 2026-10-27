// Focused actual bounce ribbon proof. Sequencer harness supplies engine fixture,
// native font/raster and hardware boundaries ONLY; its groups/main are never run.
#include "../platform/3ds/ui_bounce.h"
#include "../platform/3ds/ui_nav.h"
#define main unused_inherited_sequencer_main
#include "sequencer_test.cpp"
#undef main
// Private frame/target access without exported test hooks or a copied controller.
#include "../platform/3ds/ui_bounce.cpp"
#include "../platform/3ds/menu_labels_data.h"

namespace seance::platform::ui3ds {
bool g_has_dsp=true;
int bounce_workspace_resets=0;
void workspace_reset_chain(int) {++bounce_workspace_resets;}
}
namespace bounce_main {
struct touchPosition {int px=0,py=0;} pen;
void hidTouchRead(touchPosition* t){*t=pen;}
int fallthrough=0;
void touch_frame(u32 down,u32 held) {
#include <touch.inc>
    ++fallthrough;
}
bool buttons(u32 down,u32 held) {
    bool stage_skip_touch=false;
#include <buttons.inc>
    CHECK(!(down & ~(KEY_TOUCH|KEY_ZL)));
    return stage_skip_touch;
}
void patch_browser_draw(){CHECK(false);}
void confirm_discard_draw(){CHECK(false);}
void picker_draw(){CHECK(false);}
void chains_view_draw(){CHECK(false);}
void editor_draw(){CHECK(false);}
void sample_browser_draw(){CHECK(false);}
void sequencer_view_draw(){CHECK(false);}
void performance_view_draw(){CHECK(false);}
void sigils_view_draw(){CHECK(false);}
void patchbay_draw(){CHECK(false);}
#include <bottom.inc>
}
namespace ribbon_cases {
namespace b=seance::platform::ui3ds;
struct Rect {int x,y,w,h;u32 color;bool operator==(const Rect& r)const{return x==r.x&&y==r.y&&w==r.w&&h==r.h&&color==r.color;}};
std::vector<Rect> rects;
bool observe_rects=false;
struct Box {int x=0,y=0,w=0,h=0;};
bool intersects(Box a,Box c){return a.x<c.x+c.w&&a.x+a.w>c.x&&a.y<c.y+c.h&&a.y+a.h>c.y;}
bool inside(Box a,Box c){return a.x>=c.x&&a.y>=c.y&&a.x+a.w<=c.x+c.w&&a.y+a.h<=c.y+c.h;}
int pairs=0,frozen_pairs=0,pair_peak=0,target_cases=0,geometry_labels=0,gesture_cases=0;
std::string output;
std::ofstream manifest;
void geometry(bool top,const std::vector<Rect>& actual);
void click(int x,int y){bounce_main::pen={x,y};bounce_main::touch_frame(KEY_TOUCH,KEY_TOUCH);bounce_main::touch_frame(0,0);}
void key(u32 down,u32 held=0){CHECK(bounce_main::buttons(down,held?held:down));}
void render_frames(Engine& e,uint64_t frames){q15 out[512*2];while(frames){const int n=int(std::min<uint64_t>(frames,511));e.render(out,n);frames-=n;}}
void clear_bank(){auto& bank=trackr::synth::SampleBank::instance();for(int i=0;i<64;++i)bank.clear_slot(i);bank.set_pcm_budget_for_test(bank.DEFAULT_PCM_BUDGET_BYTES);}
struct BounceFixture : Fixture {
    BounceFixture(){nav_close();g_view=View::Bounce;g_overlay=Overlay::None;g_has_dsp=true;bounce_enter();bounce_workspace_resets=0;click(48,56);click(196,130);}
    ~BounceFixture(){bounce_cancel_input();trackr::synth::Sample retired;engine->discard_bounce(retired);}
    void arm(int bars=1){const int i=bars==1?0:bars==2?1:bars==4?2:3;click(52+i*72,130);key(KEY_A);CHECK(engine->bounce_snapshot().status==BounceStatus::Armed&&engine->sequencer_enabled);}
    void complete(int bars=1){arm(bars);const auto t=engine->bounce_snapshot();render_frames(*engine,t.end_frame-engine->transport_frame+100);CHECK(engine->bounce_snapshot().status==BounceStatus::Complete);}
    void stop_song(){if(engine->transport_playing)key(KEY_START);}
};
std::vector<u32> draw_bounce(bool top){
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();rects.clear();pixels.assign(width*240,0);observe_rects=true;
    if(top){bounce_draw_top();bounce_draw_indicator();}else bounce_main::draw_bottom_surface(View::Bounce,Overlay::None);
    observe_rects=false;
    CHECK(outside_rects==0&&outside_lines==0);
    if(text_overlaps)for(const auto& t:texts)std::fprintf(stderr,"text %d,%d %s\n",t.x,t.y,t.text.c_str());
    CHECK(text_overlaps==0);geometry(top,rects);return pixels;
}
void ppm(const std::string& file,const std::vector<u32>& p,int w){std::ofstream out(file,std::ios::binary);out<<"P6\n"<<w<<" 240\n255\n";for(u32 c:p){const char rgb[]={char(c&255),char((c>>8)&255),char((c>>16)&255)};out.write(rgb,3);}CHECK(out.good());}
void pair(const char* name=nullptr){
    bounce_capture();const auto captured=b::frame;
    const auto top=draw_bounce(true);const int topcount=objects;
    const auto bottom=draw_bounce(false);const int bottomcount=objects;
    pair_peak=std::max(pair_peak,topcount+bottomcount);CHECK(topcount+bottomcount<=8192);++pairs;
    auto* e=g_eng;auto* a=g_audio;g_eng=nullptr;g_audio=nullptr;
    CHECK(draw_bounce(true)==top);CHECK(draw_bounce(false)==bottom);CHECK(std::memcmp(&captured,&b::frame,sizeof captured)==0);++frozen_pairs;
    g_eng=e;g_audio=a;
    if(name&&!output.empty()){
        ppm(output+"/"+name+"_top.ppm",top,400);ppm(output+"/"+name+"_bottom.ppm",bottom,320);
        manifest<<name<<'\t'<<topcount<<'\t'<<bottomcount<<'\t'<<topcount+bottomcount<<'\n';
    }
}
bool has_text(const char* text){return std::any_of(texts.begin(),texts.end(),[&](const auto& t){return t.text==text;});}
void native_text(const char* text){CHECK(has_text(text));}
void same_take(const BounceSnapshot& a,const BounceSnapshot& c){CHECK(a.generation==c.generation&&a.slot==c.slot&&a.frames==c.frames&&a.source==c.source&&a.saved==c.saved&&a.status==c.status);}

// Nine original bounce functional scenarios retained, with only approved layout
// coordinates/current native labels changed. Additional ribbon checks follow.
void input_and_recording(){
    clear_bank();BounceFixture f;pair();key(KEY_A|KEY_TOUCH);CHECK(f.engine->bounce_snapshot().active());
    const auto armed=f.engine->bounce_snapshot();pair();bounce_main::touch_frame(0,0);CHECK(f.engine->bounce_snapshot().generation==armed.generation);
    key(KEY_B);CHECK(g_view==View::Chains&&!nav_is_open());
    nav_open();NavChoice choice;nav_buttons(0,choice);nav_touch_down(220,110,choice);CHECK(choice==NavChoice::Bounce&&!nav_is_open());g_view=View::Bounce;bounce_enter();
    CHECK(f.engine->transport_playing&&armed.start_frame==0);render_frames(*f.engine,16001);CHECK(f.engine->bounce_snapshot().status==BounceStatus::Recording);pair();
    g_view=View::Performance;bounce_capture();auto* e=g_eng;g_eng=nullptr;width=400;objects=outside_rects=outside_lines=0;pixels.assign(400*240,0);bounce_draw_indicator();CHECK(objects==1);g_eng=e;
    render_frames(*f.engine,armed.target_frames);CHECK(f.engine->bounce_snapshot().status==BounceStatus::Complete);g_view=View::Bounce;bounce_enter();pair();
    const auto complete=f.engine->bounce_snapshot();key(KEY_A);CHECK(f.engine->bounce_snapshot().generation==complete.generation);CHECK(!f.engine->transport_playing&&f.engine->bounce_preview_active());CHECK(bounce_main::fallthrough==0);
}
void check_wav(const BounceSnapshot& take,const std::string& path,int channels){
    const auto& bank=trackr::synth::SampleBank::instance();CHECK(std::filesystem::exists(path));
    trackr::synth::Sample decoded;trackr::synth::WavDecodeLimits limits;limits.workspace_bytes=trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    CHECK(trackr::synth::load_wav_to_sample(path.c_str(),decoded,ENGINE_SR,ENGINE_SR*20,limits)==trackr::synth::WavLoadResult::Ok);
    CHECK(decoded.channels==channels&&decoded.num_frames()==take.frames);
    // Existing same-rate decoder omits final frame; raw WAV verifies EVERY sample.
    CHECK(std::equal(decoded.data.begin(),decoded.data.end()-channels,bank.slot(take.slot).data.begin()));
    std::ifstream wav(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(wav)),{});
    CHECK(bytes.size()==44+size_t(take.frames)*channels*2);CHECK(uint8_t(bytes[22])==channels&&uint8_t(bytes[23])==0);
    for(size_t i=0;i<size_t(take.frames)*channels;++i){const uint16_t v=uint8_t(bytes[44+i*2])|(uint16_t(uint8_t(bytes[45+i*2]))<<8);CHECK(q15(v)==bank.slot(take.slot).data[i]);}
}
void save_send_discard(){
    clear_bank();BounceFixture f;f.complete();f.stop_song();Unit* others[3]={f.engine->chains[1].units[0],f.engine->chains[2].units[0],f.engine->chains[3].units[0]};
    const char* valid=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";key(KEY_Y);CHECK(!f.engine->bounce_snapshot().saved&&f.engine->chains[0].units[0]->kind==UnitKind::Fm);
    RECORDING_DIR=valid;key(KEY_Y);CHECK(f.engine->bounce_snapshot().saved);pair();const auto t=f.engine->bounce_snapshot();auto& bank=trackr::synth::SampleBank::instance();
    const std::string path=bank.provenance(t.slot).path;check_wav(t,path,2);
    key(KEY_B);key(KEY_X);CHECK(bank.provenance(t.slot).path==path);key(KEY_Y);click(52,98);key(KEY_A);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::SamplePlayer&&bounce_workspace_resets==1);
    for(int i=1;i<4;++i)CHECK(f.engine->chains[i].units[0]==others[i-1]);
    const auto* play=static_cast<PlayUnit*>(f.engine->chains[0].units[0]);CHECK(play->sample_slot==t.slot&&play->params[PlayUnit::P_MODE].base==0);
    click(160,198);CHECK(f.engine->bounce_snapshot().slot<0&&bank.slot(t.slot).playable_frames()==t.frames);CHECK(std::filesystem::exists(path)&&!bounce_guard_project_change());
}
void cancellation_and_limits(){
    clear_bank();BounceFixture f;
    bounce_main::pen={160,194};bounce_main::touch_frame(KEY_TOUCH,KEY_TOUCH);bounce_main::pen={190,194};bounce_main::touch_frame(0,KEY_TOUCH);bounce_main::touch_frame(0,0);CHECK(f.engine->bounce_snapshot().slot<0);
    bounce_touch_down(160,194);bounce_cancel_input();bounce_touch_release();CHECK(f.engine->bounce_snapshot().slot<0);key(KEY_A,KEY_A|KEY_L);CHECK(f.engine->bounce_snapshot().slot<0);
    g_has_dsp=false;key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);pair();g_has_dsp=true;
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,10);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);pair();
    f.engine->set_clock_mod(ClockTarget::Tempo,ModSource::None,0);render_frames(*f.engine,32000);f.engine->transport_reset();f.arm();const auto armed=f.engine->bounce_snapshot();render_frames(*f.engine,armed.start_frame+1234);
    f.engine->set_bpm(130);render_frames(*f.engine,64);const auto t=f.engine->bounce_snapshot();CHECK(t.status==BounceStatus::Partial&&t.reason==BounceReason::TempoChanged);pair();
    key(KEY_Y);CHECK(!f.engine->bounce_snapshot().saved);f.stop_song();click(160,198);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);
    auto& bank=trackr::synth::SampleBank::instance();bank.set_pcm_budget_for_test(1);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);pair();bank.set_pcm_budget_for_test(bank.DEFAULT_PCM_BUDGET_BYTES);
    f.engine->set_bpm(20);render_frames(*f.engine,32000);f.engine->transport_reset();click(268,130);pair();
}
void stale_confirmations(){
    clear_bank();BounceFixture f;f.complete();f.stop_song();key(KEY_Y);click(52,98);
    auto* before=f.engine->chains[0].units[0];f.engine->chains[0].units[0]=new OscUnit;key(KEY_A);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::Osc);delete before;pair();
    click(160,198);CHECK(f.engine->bounce_snapshot().slot<0);f.complete();click(160,198);bounce_touch_down(40,194);trackr::synth::Sample retired;f.engine->discard_bounce(retired);bounce_touch_release();CHECK(f.engine->bounce_snapshot().slot<0);
}
void repeat_bounce_cache(){
    clear_bank();BounceFixture f;std::vector<std::string> paths;int previous=-1;
    for(int generation=0;generation<12;++generation){
        f.complete();f.stop_song();const auto take=f.engine->bounce_snapshot();CHECK(take.status==BounceStatus::Complete);key(KEY_Y);click(52,98);key(KEY_A);CHECK(f.engine->chains[0].units[0]->kind==UnitKind::SamplePlayer);
        auto& bank=trackr::synth::SampleBank::instance();paths.push_back(bank.provenance(take.slot).path);if(previous>=0)CHECK(!bank.slot(previous).has_storage());
        click(160,198);CHECK(f.engine->bounce_snapshot().slot<0);CHECK(bank.pcm_bytes_used()==size_t(take.target_frames)*4);for(const auto& path:paths)CHECK(std::filesystem::exists(path));previous=take.slot;
    }
    g_clip_unit=new PlayUnit;static_cast<PlayUnit*>(g_clip_unit)->sample_slot=previous;f.complete();f.stop_song();key(KEY_Y);click(52,98);key(KEY_A);CHECK(trackr::synth::SampleBank::instance().slot(previous).has_storage());delete g_clip_unit;g_clip_unit=nullptr;
}
void navigation_targets(){
    clear_bank();BounceFixture f;const NavChoice choices[]={NavChoice::Chains,NavChoice::Sigils,NavChoice::Patchbay,NavChoice::Sequencer,NavChoice::Performance,NavChoice::Bounce,NavChoice::Patch,NavChoice::Exit};
    for(int i=0;i<8;++i){nav_open();NavChoice choice;nav_buttons(0,choice);nav_touch_down(i<4?80:220,64+(i%4)*42,choice);CHECK(choice==choices[i]&&!nav_is_open());}
    nav_open();nav_capture();width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(320*240,0);nav_draw();CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);nav_close();
}
void chain_source_and_mono(){
    clear_bank();BounceFixture f;bounce_touch_down(116,56);bounce_touch_hold(150,56);bounce_touch_release();bounce_capture();CHECK(b::frame.source==BounceSource::Master);
    bounce_touch_down(116,56);bounce_cancel_input();bounce_touch_release();bounce_capture();CHECK(b::frame.source==BounceSource::Master);
    for(int source=1;source<=4;++source){key(KEY_DOWN);pair();CHECK(int(b::frame.source)==source);f.arm();const auto t=f.engine->bounce_snapshot();CHECK(int(t.source)==source);CHECK(trackr::synth::SampleBank::instance().slot(t.slot).channels==1);click(48,56);key(KEY_UP);CHECK(f.engine->bounce_snapshot().source==t.source&&f.engine->bounce_snapshot().generation==t.generation);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);}
    key(KEY_DOWN);f.engine->chains[3].mute=true;pair();CHECK(b::frame.source_muted);f.engine->chains[3].mute=false;
    click(48,56);click(278,56);f.engine->chains[3].units[0]->trigger();f.complete();f.stop_song();const auto take=f.engine->bounce_snapshot();CHECK(take.source==BounceSource::Chain4&&take.status==BounceStatus::Complete);click(48,56);bounce_enter();pair();CHECK(b::frame.source==BounceSource::Chain4);
    const char* valid=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";key(KEY_X);CHECK(!f.engine->bounce_snapshot().saved&&f.engine->bounce_snapshot().source==BounceSource::Chain4);RECORDING_DIR=valid;key(KEY_X);CHECK(f.engine->bounce_snapshot().saved);
    auto& bank=trackr::synth::SampleBank::instance();const std::string path=bank.provenance(take.slot).path;CHECK(path.find("_chain4.wav")!=std::string::npos);check_wav(take,path,1);
    Unit* original=f.engine->chains[3].units[0];key(KEY_Y);click(124,98);key(KEY_A);CHECK(f.engine->chains[3].units[0]==original&&f.engine->chains[1].units[0]->kind==UnitKind::SamplePlayer);CHECK(static_cast<PlayUnit*>(f.engine->chains[1].units[0])->sample_slot==take.slot&&f.engine->bounce_snapshot().source==BounceSource::Chain4);
    click(160,198);CHECK(std::filesystem::exists(path)&&bank.slot(take.slot).channels==1&&bank.slot(take.slot).playable_frames()==take.frames);click(48,56);bounce_capture();CHECK(b::frame.source==BounceSource::Master);
}
void simple_states_and_listen(){
    clear_bank();BounceFixture f;pair();CHECK(b::frame.take.slot<0);f.arm();CHECK(f.engine->transport_playing&&f.engine->bounce_snapshot().start_frame==0);pair();
    click(24,204);CHECK(f.engine->bounce_snapshot().active());render_frames(*f.engine,1234);click(160,194);const auto take=f.engine->bounce_snapshot();CHECK(take.status==BounceStatus::Partial);pair();
    const auto* original=f.engine->chains[0].units[0];click(160,60);CHECK(f.engine->bounce_preview_active()&&!f.engine->transport_playing);render_frames(*f.engine,137);CHECK(f.engine->bounce_preview_position()==137);pair();CHECK(b::frame.listening);
    CHECK(f.engine->chains[0].units[0]==original&&f.engine->bounce_snapshot().generation==take.generation);key(KEY_A);CHECK(!f.engine->bounce_preview_active());bounce_touch_down(160,60);bounce_touch_hold(190,60);bounce_touch_release();CHECK(!f.engine->bounce_preview_active());
    key(KEY_A);CHECK(f.engine->bounce_preview_active());nav_open();bounce_cancel_input();CHECK(!f.engine->bounce_preview_active());nav_close();key(KEY_A);render_frames(*f.engine,take.frames+16);CHECK(!f.engine->bounce_preview_active()&&f.engine->bounce_preview_position()==take.frames);pair();
    key(KEY_A);key(KEY_B);CHECK(g_view==View::Chains&&!f.engine->bounce_preview_active());g_view=View::Bounce;bounce_enter();CHECK(bounce_guard_project_change());click(160,198);pair();CHECK(b::frame.modal==b::Modal::Discard);
    key(KEY_B);CHECK(f.engine->bounce_snapshot().generation==take.generation);click(160,198);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0&&!f.engine->transport_playing);pair();
    f.complete();key(KEY_A);CHECK(f.engine->bounce_preview_active());key(KEY_START);CHECK(!f.engine->bounce_preview_active()&&f.engine->transport_playing);key(KEY_X);CHECK(f.engine->bounce_snapshot().saved);CHECK(!bounce_guard_project_change()&&f.engine->bounce_snapshot().slot<0);
}
void fast_send_and_saved_exit(){
    clear_bank();BounceFixture f;for(auto*& u:f.engine->chains[1].units){delete u;u=nullptr;}f.complete();CHECK(f.engine->transport_playing);Unit* original=f.engine->chains[0].units[0];const char* directory=RECORDING_DIR;
    RECORDING_DIR="/not-present/seance-bounce-test";key(KEY_Y);CHECK(f.engine->transport_playing&&!f.engine->bounce_snapshot().saved);RECORDING_DIR=directory;key(KEY_Y);CHECK(!f.engine->transport_playing&&f.engine->bounce_snapshot().saved);pair();CHECK(b::frame.modal==b::Modal::Send&&b::frame.chain==1&&!b::frame.occupied[1]);
    const auto take=f.engine->bounce_snapshot();const std::string path=trackr::synth::SampleBank::instance().provenance(take.slot).path;key(KEY_A);CHECK(f.engine->chains[1].units[0]->kind==UnitKind::SamplePlayer&&f.engine->chains[0].units[0]==original&&f.engine->transport_frame==0&&!f.engine->transport_playing);
    CHECK(!bounce_guard_project_change()&&f.engine->bounce_snapshot().slot<0&&std::filesystem::exists(path)&&trackr::synth::SampleBank::instance().slot(take.slot).playable_frames()==take.frames);
}

// Independent bitmap oracle: decodes baked bits into pixels; never calls word()
// or draws the UI again. Real submitted rects must equal those pixels exactly.
struct Label {std::string text;Box box,target;const uint8_t* bitmap;int stride;u32 color;};
std::vector<Label> expected_labels(bool top){
    const auto& f=b::frame;const auto& t=f.take;std::vector<Label> labels;
    auto word=[&](int x,int baseline,const std::string& text,int size,u32 color,bool center=false,Box target=Box{}){
        const auto* glyph=b::find_word(text.c_str(),size);CHECK(glyph);
        if(center)x-=glyph->width/2;
        labels.push_back({text,{x,baseline+glyph->top,glyph->width,glyph->height},target,b::bounce_type::bitmap+glyph->offset,(glyph->width+7)/8,color});
    };
    if(!f.valid)return labels;
    if(top){
        const b::menu_labels::Label* heading=nullptr;for(const auto& glyph:b::menu_labels::labels)if(!std::strcmp(glyph.text,"bounce"))heading=&glyph;CHECK(heading);
        labels.push_back({"bounce",{24,20,heading->width,heading->height},{},b::menu_labels::bitmap+heading->offset,(heading->width+7)/8,m::ink()});
        if(t.active()){
            const unsigned progress=t.target_frames?std::min(unsigned(t.bars),unsigned(uint64_t(t.frames)*t.bars/t.target_frames)):0;
            word(200,135,std::to_string(b::waiting(t)?0u:std::min(unsigned(t.bars),progress+1)),40,m::ink(),true);
        }else if(f.wave)word(24,80,f.listening?"listening":t.status==BounceStatus::Partial?"partial take":"take ready",22,m::ink());
        else if(t.slot<0)word(200,135,std::to_string(b::LENGTHS[f.selected]),40,m::ink(),true);
        return labels;
    }
    const Box primary{68,177,184,36};
    if(f.modal!=b::Modal::None){
        const bool send=f.modal==b::Modal::Send;word(24,39,send?"use in chain":"record again?",28,m::ink());
        if(send)for(int i=0;i<4;++i)word(52+i*72,98,std::to_string(i+1),22,f.chain==i?m::spot():m::ghost(),true,{16+i*72,66,72,61});
        word(24,202,send?(f.occupied[f.chain]?"replace":"use chain"):"release take",22,send&&!f.can_send?m::ghost():m::spot(),false,{16,177,144,36});
        word(216,202,"cancel",22,m::ink(),false,{176,177,128,36});
    }else if(t.active()){
        const unsigned bars=std::max(1u,std::min(8u,unsigned(t.bars)));const uint32_t elapsed=b::waiting(t)?0:std::min(t.frames,t.target_frames);
        const int current=elapsed&&t.target_frames?int(std::min(uint64_t(bars-1),uint64_t(elapsed)*bars/t.target_frames)):-1;
        for(unsigned i=0;i<bars;++i){const int left=30+int((260*i+bars/2)/bars),right=30+int((260*(i+1)+bars/2)/bars)-6;word((left+right)/2,116,std::to_string(i+1),22,int(i)==current?m::spot():m::ghost(),true);}
        word(160,202,b::waiting(t)?"cancel":"stop early",28,m::ink(),true,primary);
    }else if(t.slot>=0){
        if(t.take()){
            word(88,64,f.listening?"stop listening":"listen",22,m::spot(),false,{68,39,184,36});
            word(88,104,t.saved?"wav saved":"save wav",22,t.saved?m::ghost():m::ink(),false,{68,79,184,36});
            if(t.status==BounceStatus::Complete)word(88,144,"use in chain",22,m::ink(),false,{68,119,184,36});
        }
        word(160,202,"record again",22,t.take()?m::ghost():m::ink(),true,primary);
    }else{
        constexpr int centers[]={48,116,170,224,278},bounds[]={20,86,143,197,251,304};
        for(int i=0;i<5;++i)word(centers[i],67,i?std::to_string(i):"mix",22,int(f.source)==i?m::spot():m::ghost(),true,{bounds[i],42,bounds[i+1]-bounds[i],42});
        for(int i=0;i<4;++i)word(52+i*72,141,std::to_string(b::LENGTHS[i]),28,!f.available[i]?palette::shadow():f.selected==i?m::spot():m::ghost(),true,{16+i*72,116,72,42});
        word(160,202,"record",28,f.available[f.selected]?m::ink():m::ghost(),true,primary);
    }
    return labels;
}
void geometry(bool top,const std::vector<Rect>& actual){
    const auto labels=expected_labels(top);
    for(size_t i=0;i<labels.size();++i){
        const auto& label=labels[i];CHECK(inside(label.box,{0,0,top?400:320,240}));
        if(label.target.w){CHECK(label.target.w>=32&&label.target.h>=24);CHECK(inside(label.box,label.target));}
        for(size_t j=0;j<i;++j){if(intersects(label.box,labels[j].box))std::fprintf(stderr,"glyph overlap: %s / %s\n",label.text.c_str(),labels[j].text.c_str());CHECK(!intersects(label.box,labels[j].box));}
        for(const auto& text:texts){if(intersects(label.box,{text.x,text.y,text.w,text.h}))std::fprintf(stderr,"glyph/native overlap: %s / %s\n",label.text.c_str(),text.text.c_str());CHECK(!intersects(label.box,{text.x,text.y,text.w,text.h}));}
        std::vector<uint8_t> observed(size_t(label.box.w)*label.box.h);
        for(const auto& r:actual){
            if(r.color!=label.color||!intersects(label.box,{r.x,r.y,r.w,r.h}))continue;
            CHECK(inside({r.x,r.y,r.w,r.h},label.box));
            for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)observed[size_t(y-label.box.y)*label.box.w+x-label.box.x]=1;
        }
        for(int y=0;y<label.box.h;++y)for(int x=0;x<label.box.w;++x){const bool ink=(label.bitmap[y*label.stride+x/8]&(0x80u>>(x&7)))!=0;if(bool(observed[size_t(y)*label.box.w+x])!=ink)std::fprintf(stderr,"glyph missing/extra: %s (%d,%d) top=%d\n",label.text.c_str(),x,y,top);CHECK(bool(observed[size_t(y)*label.box.w+x])==ink);}
        ++geometry_labels;
    }
    if(!top&&b::frame.modal==b::Modal::None&&b::frame.take.take()){
        for(const auto& text:texts){
            if(text.text=="a")CHECK(inside({text.x,text.y,text.w,text.h},{68,39,184,36}));
            if(text.text=="x")CHECK(inside({text.x,text.y,text.w,text.h},{68,79,184,36}));
            if(text.text=="y")CHECK(inside({text.x,text.y,text.w,text.h},{68,119,184,36}));
        }
    }
}
struct Target {Box box;b::Action action;};
void audit_targets(){
    using A=b::Action;std::vector<Target> targets;const auto take=b::current();
    if(b::modal!=b::Modal::None){
        if(b::modal==b::Modal::Send)for(int i=0;i<4;++i)targets.push_back({{16+i*72,66,72,61},A(int(A::Length1)+i)});
        targets.push_back({{16,177,144,36},A::Confirm});targets.push_back({{176,177,128,36},A::Cancel});
    }else if(take.active())targets.push_back({{68,177,184,36},A::Record});
    else if(take.slot>=0){
        if(take.take()){
            targets.push_back({{68,39,184,36},A::Listen});if(!take.saved)targets.push_back({{68,79,184,36},A::Save});
            if(take.status==BounceStatus::Complete)targets.push_back({{68,119,184,36},A::Send});
        }
        targets.push_back({{68,177,184,36},A::Again});
    }else{
        constexpr int bounds[]={20,86,143,197,251,304};for(int i=0;i<5;++i)targets.push_back({{bounds[i],42,bounds[i+1]-bounds[i],42},A(int(A::Master)+i)});
        for(int i=0;i<4;++i)targets.push_back({{16+i*72,116,72,42},A(int(A::Length1)+i)});
        targets.push_back({{68,177,184,36},A::Record});
    }
    for(size_t i=0;i<targets.size();++i){CHECK(targets[i].box.w>=32&&targets[i].box.h>=24);CHECK(inside(targets[i].box,{0,0,320,240}));for(size_t j=0;j<i;++j)CHECK(!intersects(targets[i].box,targets[j].box));}
    // Every pixel, including target edges and all retired negative space.
    for(int y=-1;y<=240;++y)for(int x=-1;x<=320;++x){A expected=A::None;for(const auto& target:targets)if(inside({x,y,1,1},target.box))expected=target.action;CHECK(b::action_at(x,y)==expected);}
    ++target_cases;
}
void all_sources_lengths_and_waiting(){
    constexpr int centers[]={48,116,170,224,278};
    for(int source=0;source<5;++source)for(int i=0;i<4;++i){
        clear_bank();BounceFixture f;click(centers[source],56);click(52+i*72,130);pair();audit_targets();CHECK(int(b::frame.source)==source&&b::frame.selected==i);
        key(KEY_START);render_frames(*f.engine,123);key(KEY_A);auto t=f.engine->bounce_snapshot();CHECK(t.status==BounceStatus::Armed&&t.start_frame>f.engine->transport_frame&&int(t.source)==source&&t.bars==b::LENGTHS[i]);pair();audit_targets();
        render_frames(*f.engine,t.start_frame-f.engine->transport_frame+std::max(1u,t.target_frames/3));CHECK(f.engine->bounce_snapshot().status==BounceStatus::Recording);pair();audit_targets();
        key(KEY_A);CHECK(f.engine->bounce_snapshot().status==BounceStatus::Partial);pair();audit_targets();
        const auto partial=f.engine->bounce_snapshot();click(160,140);key(KEY_Y);same_take(partial,f.engine->bounce_snapshot());CHECK(b::modal==b::Modal::None);
    }
}
void ownership_and_saved_locality(){
    clear_bank();BounceFixture f;
    for(int mode=0;mode<8;++mode){
        const auto original=f.engine->bounce_snapshot();bounce_touch_down(160,194);
        if(mode==0)bounce_touch_release(false);
        if(mode==1){bounce_touch_hold(172,194);bounce_touch_release();}
        if(mode==2){bounce_touch_hold(148,194);bounce_touch_release();}
        if(mode==3){bounce_touch_hold(160,206);bounce_touch_release();}
        if(mode==4){bounce_touch_hold(160,182);bounce_touch_release();}
        if(mode==5){nav_open();bounce_touch_release();nav_close();}
        if(mode==6){g_overlay=Overlay::Picker;bounce_touch_release();g_overlay=Overlay::None;}
        if(mode==7){auto other=std::make_unique<Engine>();g_eng=other.get();bounce_touch_release();CHECK(other->bounce_snapshot().slot<0);g_eng=f.engine.get();}
        same_take(original,f.engine->bounce_snapshot());++gesture_cases;
    }
    bounce_touch_down(116,56);bounce_touch_hold(127,56);bounce_touch_release();bounce_capture();CHECK(b::frame.source==BounceSource::Chain1);++gesture_cases;
    click(48,56);f.complete();f.stop_song();bounce_capture();const auto before=draw_bounce(false);const auto take=f.engine->bounce_snapshot();click(160,98);CHECK(f.engine->bounce_snapshot().saved);bounce_capture();const auto after=draw_bounce(false);
    for(int y=0;y<240;++y)for(int x=0;x<320;++x)if(!inside({x,y,1,1},{68,79,184,36}))CHECK(before[y*320+x]==after[y*320+x]);
    const auto saved=f.engine->bounce_snapshot();const std::string path=trackr::synth::SampleBank::instance().provenance(take.slot).path;const auto files=std::distance(std::filesystem::directory_iterator(RECORDING_DIR),std::filesystem::directory_iterator());
    const int old_mutations=mutations;click(160,98);same_take(saved,f.engine->bounce_snapshot());CHECK(mutations==old_mutations&&std::distance(std::filesystem::directory_iterator(RECORDING_DIR),std::filesystem::directory_iterator())==files&&std::filesystem::exists(path));pair();audit_targets();
    // A press on the old split-save x24 cannot save; old stop-at110 cannot stop.
    click(24,134);same_take(saved,f.engine->bounce_snapshot());click(160,198);f.arm();click(160,110);CHECK(f.engine->bounce_snapshot().active());key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);++gesture_cases;
    f.complete();click(160,198);audit_targets();bounce_touch_down(40,194);key(KEY_B);bounce_touch_release();CHECK(f.engine->bounce_snapshot().take()&&b::modal==b::Modal::None);++gesture_cases;
    // Replacing a modal owner cannot apply its stale destructive confirmation.
    click(160,198);auto other=std::make_unique<Engine>();g_eng=other.get();bounce_buttons(KEY_A,KEY_A);CHECK(other->bounce_snapshot().slot<0&&b::modal==b::Modal::None);g_eng=f.engine.get();CHECK(f.engine->bounce_snapshot().take());++gesture_cases;
    // Keep the old modal press physically held while the engine replaces its
    // generation; a release cannot discard the newly completed unsaved take.
    click(160,198);bounce_touch_down(40,194);trackr::synth::Sample retired;CHECK(f.engine->discard_bounce(retired)==BounceResult::Ok);
    BouncePlan plan;CHECK(f.engine->plan_bounce(1,plan)==BounceResult::Ok);CHECK(Engine::stage_bounce(plan)==BounceResult::Ok);CHECK(f.engine->commit_bounce(plan,true)==BounceResult::Ok);
    auto replacement=f.engine->bounce_snapshot();render_frames(*f.engine,replacement.end_frame-f.engine->transport_frame+100);replacement=f.engine->bounce_snapshot();CHECK(replacement.status==BounceStatus::Complete&&!replacement.saved);
    bounce_touch_release();same_take(replacement,f.engine->bounce_snapshot());pair();CHECK(b::modal==b::Modal::None);++gesture_cases;
}
RecUnit* protected_recorder(BounceFixture& f){
    auto*& unit=f.engine->chains[2].units[0];delete unit;auto* rec=new RecUnit;unit=rec;CHECK(f.engine->prepare_recorder(*rec)==trackr::synth::SampleEditResult::Ok);rec->trigger();q15 input[32]={};rec->capture_input(input,32);rec->stop();CHECK(rec->take_needs_save());return rec;
}
void failures_and_service_states(){
    clear_bank();BounceFixture f;f.complete();const auto take=f.engine->bounce_snapshot();const auto hash=pattern_hash(f.engine->pattern);Unit* original=f.engine->chains[0].units[0];
    const char* valid=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";click(160,98);same_take(take,f.engine->bounce_snapshot());CHECK(f.engine->transport_playing&&f.engine->chains[0].units[0]==original&&pattern_hash(f.engine->pattern)==hash);pair();audit_targets();RECORDING_DIR=valid;
    key(KEY_Y);click(52,98);pair();audit_targets();native_text("replaces modules + rhythm in all 8 banks");native_text("saved wav kept / play output is mono");key(KEY_B);
    for(auto*& u:f.engine->chains[1].units){delete u;u=nullptr;}key(KEY_Y);pair();audit_targets();native_text("sets this chain's rhythm in all 8 banks");key(KEY_B);
    auto* rec=protected_recorder(f);key(KEY_Y);click(196,98);pair();audit_targets();CHECK(!b::frame.can_send);native_text("chain has a protected recording");key(KEY_A);CHECK(f.engine->chains[2].units[0]==rec&&rec->take_needs_save()&&f.engine->bounce_snapshot().saved);pair();key(KEY_B);
    click(160,198);CHECK(f.engine->bounce_snapshot().slot<0);f.arm();f.engine->stop_bounce();CHECK(f.engine->bounce_snapshot().status==BounceStatus::Cancelled&&f.engine->bounce_snapshot().frames==0);pair();audit_targets();
    click(160,60);click(160,98);click(160,140);CHECK(f.engine->bounce_snapshot().slot>=0);click(160,198);CHECK(f.engine->bounce_snapshot().slot<0);
    f.arm();auto t=f.engine->bounce_snapshot();trackr::synth::SampleBank::instance().clear_slot(t.slot);pair();audit_targets();CHECK(b::frame.take.reason==BounceReason::SampleChanged);key(KEY_A);CHECK(f.engine->bounce_snapshot().slot<0);
}
void capture_state(const std::string& theme,int state){
    clear_bank();BounceFixture f;const char* names[]={"setup","waiting","recording","take","listening","saved","partial","save_error","send_used","send_empty","send_protected","discard","no_audio","audio_unavailable","muted","empty"};
    if(state==1){key(KEY_START);render_frames(*f.engine,123);f.arm(4);}
    if(state==2){f.arm(4);render_frames(*f.engine,f.engine->bounce_snapshot().target_frames*3/8);}
    if(state>=3&&state<=11){
        f.complete(4);
        const auto t=f.engine->bounce_snapshot();const auto& pcm=trackr::synth::SampleBank::instance().slot(t.slot).data;CHECK(std::any_of(pcm.begin(),pcm.end(),[](q15 s){return s!=0;}));
        if(state==4){key(KEY_A);render_frames(*f.engine,t.frames/3);}
        if(state==5)key(KEY_X);
        if(state==6){click(160,198);key(KEY_A);f.stop_song();f.arm(4);render_frames(*f.engine,f.engine->bounce_snapshot().target_frames/4);key(KEY_A);}
        if(state==7){const char* valid=RECORDING_DIR;RECORDING_DIR="/not-present/seance-bounce-test";key(KEY_X);RECORDING_DIR=valid;}
        if(state==8){key(KEY_Y);click(52,98);}
        if(state==9){for(auto*& unit:f.engine->chains[1].units){delete unit;unit=nullptr;}key(KEY_Y);}
        if(state==10){protected_recorder(f);key(KEY_Y);click(196,98);}
        if(state==11)click(160,198);
    }
    if(state==12){f.arm(4);f.engine->stop_bounce();}
    if(state==13){g_has_dsp=false;key(KEY_A);}
    if(state==14){click(278,56);f.engine->chains[3].mute=true;}
    if(state==15){click(278,56);for(auto*& unit:f.engine->chains[3].units){delete unit;unit=nullptr;}}
    const auto name=theme+"_"+names[state];pair(name.c_str());
}
int run(bool capture,const std::string& destination){
    int groups=0;try{
        if(capture){output=destination;std::filesystem::create_directories(output);manifest.open(output+"/manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\n";}
        for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
            palette::set(theme);
            if(capture){for(int state=0;state<16;++state)capture_state(theme==palette::Theme::Graphite?"graphite":"pink",state);}
            else for(auto test:{input_and_recording,save_send_discard,cancellation_and_limits,stale_confirmations,repeat_bounce_cache,navigation_targets,chain_source_and_mono,simple_states_and_listen,fast_send_and_saved_exit,all_sources_lengths_and_waiting,ownership_and_saved_locality,failures_and_service_states}){test();++groups;std::printf("ok ribbon group %d\n",groups);std::fflush(stdout);}
        }
        clear_bank();
        if(capture){manifest.close();std::ofstream receipt(output+"/capture.json");receipt<<"{\"pairs\":"<<pairs<<",\"themes\":2,\"peak_quads\":"<<pair_peak<<",\"engine_generated_audio\":true,\"hardware\":false,\"frozen_pairs\":"<<frozen_pairs<<"}\n";std::printf("bounce ribbon capture: %d pairs; 0 regression groups; native C++ surfaces only\n",pairs);}
        else std::printf("bounce ribbon UI: passed; %d/24 groups; %d frozen pairs / %d pairs; %d glyph-mask checks; %d exhaustive target cases; %d ownership gestures; peak %d/8192; real main, WAV and engine; graphite+pink\n",groups,frozen_pairs,pairs,geometry_labels,target_cases,gesture_cases,pair_peak);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"bounce ribbon failed after %d groups: %s\n",groups,error.what());return 1;}
}
} // namespace ribbon_cases
void real_rect(float,float,float,float,uint32_t) asm("__real__ZN6seance2ui4rectEffffj");
extern "C" void __wrap__ZN6seance2ui4rectEffffj(float x,float y,float w,float h,uint32_t color){
    if(ribbon_cases::observe_rects){CHECK(x==std::floor(x)&&y==std::floor(y)&&w==std::floor(w)&&h==std::floor(h));ribbon_cases::rects.push_back({int(x),int(y),int(w),int(h),color});}
    real_rect(x,y,w,h,color);
}
int main(int argc,char** argv){
    const bool capture=argc==4&&std::string(argv[1])=="--capture-only";
    if(!capture&&!(argc==3&&std::string(argv[1])=="--proof-only"))return 2;
    const std::string recordings=argv[capture?3:2];
    CHECK(std::filesystem::is_directory(recordings));
    CHECK(recordings.find("/tmp/seance-bounce-ribbon-wav-")==0);
    RECORDING_DIR=recordings.c_str();
    return ribbon_cases::run(capture,capture?argv[2]:"");
}
