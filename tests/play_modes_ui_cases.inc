// Appended to current real-main dispatcher harness; no inherited test groups run.
#include <cstdio>
#include <fstream>
#include "../platform/3ds/play_editor_type.h"
#include "../platform/3ds/play_editor_layout.h"
namespace play_modes_cases {
using P=PlayUnit;using Mode=P::PitchMode;
int duals=0,gestures=0,peak=0;
void tap(PlayUiFixture&f,int x,int y){input::down(x,y);input::up();++gestures;f.capture();}
void drag(PlayUiFixture&f,int x,int y,int dx,int dy=0){input::down(x,y);input::move(x,y);input::move(x+dx,y+dy);input::up();++gestures;f.capture();}
void home(PlayUiFixture&f){for(int i=0;i<8;++i){if(!play_focus_back(*f.play))break;}f.capture();}
void mode(PlayUiFixture&f,int i){tap(f,277,50+32*i);}
void tool(PlayUiFixture&f,int i){tap(f,44+72*i,97);}
void config(PlayUiFixture&f,int i){home(f);tap(f,277,223);tap(f,44+72*i,52);}
void row(PlayUiFixture&f,int i,int x=170){tap(f,x,81+24*i);}
void history(PlayUiFixture&f,int i){home(f);tap(f,118,228);row(f,i);home(f);}
void edit(PlayUiFixture&f,int which){home(f);tool(f,which);tap(f,190,202);}
void normal(PlayUiFixture&f){f.play->set_pitch_mode(Mode::Normal);f.play->params[P::P_MODE].base=0;f.snap();f.capture();f.warm();}
void render_audio(PlayUiFixture&f,std::array<q15,4096>&out,int frames=2048){f.engine->render(out.data(),frames);}
void bounds(){
    if(outside_rects||outside_lines||text_overlaps){std::fprintf(stderr,"play bounds w%d rect%d lines%d overlap%d\n",width,outside_rects,outside_lines,text_overlaps);
        for(const auto&t:texts)std::fprintf(stderr," %d,%d %dx%d %s\n",t.x,t.y,t.w,t.h,t.text.c_str());}
    CHECK(!outside_rects&&!outside_lines&&!text_overlaps&&objects<=8192);
    for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
}
void surface(PlayUiFixture&f,bool top){module_inspector_surface(top);{ModuleInspectorNoEngine hidden;if(top)CHECK(play_focus_draw_top());else play_focus_draw(*f.play);}bounds();}
void pair(PlayUiFixture&f){f.capture();surface(f,true);const int top=objects;const auto up=pixels;surface(f,false);const int bottom=objects;const auto lo=pixels;
    CHECK(top+bottom<=8192);peak=std::max(peak,top+bottom);++duals;
    // Frozen draws cannot dereference engine/audio or advance controller state.
    surface(f,true);CHECK(pixels==up);surface(f,false);CHECK(pixels==lo);
}
bool text_contains(const char*s){for(const auto&t:texts)if(t.text.find(s)!=std::string::npos)return true;return false;}

void engines(){
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);PlayUiFixture f;normal(f);auto&u=*f.play;
        const auto original=f.bases();const int dirty=mutations;
        drag(f,40,60,0);CHECK(f.bases()==original&&mutations==dirty&&play_focus_selected()==P::P_NOTE);
        drag(f,40,60,4);CHECK(musical_note::decode(u.params[P::P_NOTE].base)==61&&u.params[P::P_SPEED].base==8192);
        history(f,0);CHECK(u.params[P::P_NOTE].base==original[P::P_NOTE]);history(f,1);CHECK(musical_note::decode(u.params[P::P_NOTE].base)==61);
        mode(f,1);CHECK(u.pitch_mode()==Mode::Stretch);pair(f);CHECK(has_text("tempo"));
        const q15 note=u.params[P::P_NOTE].base;const auto origin=f.bases();
        input::down(150,60);input::move(166,60);input::move(150,60);input::up();f.capture();CHECK(f.bases()==origin);
        drag(f,150,60,16);CHECK(u.params[P::P_SPEED].base==10240&&u.params[P::P_NOTE].base==note);
        history(f,0);CHECK(u.params[P::P_SPEED].base==8192);history(f,1);CHECK(u.params[P::P_SPEED].base==10240);
        const auto edit_before=u.edit_state();const auto pcm=trackr::synth::SampleBank::instance().slot(61).data;
        for(int m:{2,0,1,2,1}){mode(f,m);CHECK(u.pitch_mode()==(m==0?Mode::Normal:m==1?Mode::Stretch:Mode::Legacy));
            CHECK(play_edit_equal(u.edit_state(),edit_before));pair(f);}
        CHECK(trackr::synth::SampleBank::instance().slot(61).data==pcm);
        const auto before_gap=f.bases();for(auto point:{std::pair<int,int>{232,80},{277,65},{277,98},{277,195}})tap(f,point.first,point.second);
        CHECK(u.pitch_mode()==Mode::Stretch&&f.bases()==before_gap);
        input::down(277,114);CHECK(u.pitch_mode()==Mode::Stretch);input::up();f.capture();CHECK(u.pitch_mode()==Mode::Legacy);
        input::down(277,82);input::move(230,82);input::move(277,82);input::up();f.capture();CHECK(u.pitch_mode()==Mode::Legacy);
        // Legacy conversion is explicit; undo cannot strand the UI in a position
        // workspace whose parameters do not exist in the six-entry legacy bank.
        u.set_modern(false);f.capture();mode(f,3);CHECK(!u.modern()&&u.pitch_mode()==Mode::Legacy);
        config(f,0);row(f,0);CHECK(u.modern());mode(f,4);CHECK(u.params[P::P_HOLD].base>16383);
        history(f,0);history(f,0);CHECK(!u.modern()&&u.pitch_mode()==Mode::Legacy&&play_focus_selected()<6);pair(f);CHECK(has_text("speed"));
    }
}

void audition(){
    PlayUiFixture f;normal(f);auto&u=*f.play;std::array<q15,4096> out{};const auto score=f.hash();
    mode(f,1);const auto original=f.bases();
    mode(f,3);CHECK(u.pitch_mode()==Mode::Stretch&&u.params[P::P_SCRUB].base==0&&u.params[P::P_HOLD].base==0);
    input::down(95,145);render_audio(f,out);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v==0;}));
    input::move(140,145);render_audio(f,out);CHECK(std::any_of(out.begin(),out.end(),[](q15 v){return v!=0;}));
    // The retained preview release uses a short source fade, not an abrupt mute.
    input::up();render_audio(f,out);render_audio(f,out);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v==0;}));
    CHECK(f.bases()==original&&!u.playback_active()&&f.hash()==score);f.capture();
    mode(f,4);CHECK(u.pitch_mode()==Mode::Stretch&&u.params[P::P_HOLD].base>16383&&u.params[P::P_SCRUB].base==0);
    const auto held=f.bases();input::down(95,145);render_audio(f,out);CHECK(std::any_of(out.begin(),out.end(),[](q15 v){return v!=0;}));
    input::up();render_audio(f,out);render_audio(f,out);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v==0;}));CHECK(f.bases()==held);
    // Sequenced scrub is explicit, never a side effect of merely opening the workspace.
    tap(f,190,202);row(f,0);CHECK(u.params[P::P_SCRUB].base>16383);row(f,0);CHECK(u.params[P::P_SCRUB].base==0);home(f);
    mode(f,1);CHECK(u.pitch_mode()==Mode::Stretch&&u.params[P::P_HOLD].base==held[P::P_HOLD]);
    tap(f,44,228);render_audio(f,out);CHECK(std::any_of(out.begin(),out.end(),[](q15 v){return v!=0;}));CHECK(!u.playback_active());
    tap(f,44,228);render_audio(f,out);render_audio(f,out);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v==0;}));
    config(f,1);const q15 loop=u.params[P::P_LOOP].base;row(f,1);CHECK(u.params[P::P_LOOP].base==loop);
    row(f,0);home(f);tap(f,44,228);CHECK(u.playback_active());tap(f,44,228);render_audio(f,out,128);CHECK(!u.playback_active());
    CHECK(f.hash()==score);
}

void configuration(){
    PlayUiFixture f;normal(f);auto&u=*f.play;
    config(f,0);row(f,0);CHECK(u.params[P::P_MODE].base>16383);row(f,1);CHECK(u.params[P::P_LOOP].base>16383);
    const auto gate=u.params[P::P_GATE].base;drag(f,150,129,45);CHECK(u.params[P::P_GATE].base!=gate);
    const auto speed=u.params[P::P_SPEED].base;row(f,3);CHECK(u.params[P::P_SPEED].base==-speed);
    drag(f,150,177,-30);CHECK(u.params[P::P_GAIN].base<32767);row(f,5);CHECK(u.bypass);row(f,5);CHECK(!u.bypass);pair(f);
    mode(f,2);config(f,0);const bool quant=u.pitch_quantize;tap(f,190,228);CHECK(u.pitch_quantize!=quant);
    config(f,1);pair(f);CHECK(has_text("route")&&has_text("repeat"));
    config(f,2);const auto stored=f.bases();const auto pcm=trackr::synth::SampleBank::instance().slot(61).data;
    for(int i=0;i<4;++i)row(f,i);
    CHECK(f.bases()==stored&&trackr::synth::SampleBank::instance().slot(61).data==pcm);pair(f);
}

void editing(){
    PlayUiFixture f;normal(f);auto&u=*f.play;
    // Absolute trim remains a retained non-destructive source edit.
    edit(f,0);row(f,2,180);CHECK(u.edit_state().enabled&&u.edit_state().start==1);
    const auto first=u.edit_state();history(f,0);CHECK(!u.edit_state().enabled);history(f,1);CHECK(play_edit_equal(first,u.edit_state()));
    edit(f,0);drag(f,150,105,10);CHECK(u.edit_state().start>first.start);home(f);
    tap(f,44,202);drag(f,150,81,8);CHECK(u.edit_state().fade_in>0);pair(f);home(f);
    const auto musical_loop=u.params[P::P_LOOP].base;tool(f,2);CHECK(u.params[P::P_LOOP].base==musical_loop);
    tap(f,190,202);drag(f,150,81,4);drag(f,150,129,3);CHECK(u.edit_state().loop_start>u.edit_state().start&&u.edit_state().crossfade>0);pair(f);
    home(f);tool(f,1);tap(f,118,202);row(f,0,95);pair(f);tap(f,190,228);
    auto&s=trackr::synth::SampleBank::instance().slot(61);CHECK(s.chops[0]==0&&s.chops[3]!=UINT32_MAX&&s.chops[4]==UINT32_MAX);
    // Slice tap selects and auditions, but does not edit the seam.
    const auto seam=s.chops[1];tap(f,85,145);CHECK(s.chops[1]==seam);tap(f,190,202);
    row(f,3);CHECK(s.slice_reverse_mask!=0);const auto reverse=s.slice_reverse_mask;
    mode(f,1);tool(f,1);tap(f,190,202);drag(f,150,177,10);CHECK(s.slice_reverse_mask==reverse);
    CHECK(std::any_of(std::begin(s.slice_speed_q8),std::end(s.slice_speed_q8),[](uint16_t v){return v!=256;}));
    const auto count_before=std::count_if(std::begin(s.chops),std::end(s.chops),[](uint32_t v){return v!=UINT32_MAX;});row(f,5);
    CHECK(std::count_if(std::begin(s.chops),std::end(s.chops),[](uint32_t v){return v!=UINT32_MAX;})==count_before-1);
    history(f,0);CHECK(std::count_if(std::begin(s.chops),std::end(s.chops),[](uint32_t v){return v!=UINT32_MAX;})==count_before);
    home(f);tool(f,0);tap(f,118,202);row(f,1);pair(f);const auto edit_before=u.edit_state();drag(f,140,177,12);CHECK(play_edit_equal(u.edit_state(),edit_before));
}

struct Files {
    std::string path;const char*old=nullptr;
    Files(){char dir[]="/tmp/seance-play-modes-files-XXXXXX";CHECK(mkdtemp(dir));path=dir;old=RECORDING_DIR;RECORDING_DIR=path.c_str();}
    ~Files(){RECORDING_DIR=old;auto&b=trackr::synth::SampleBank::instance();
        for(int i=0;i<trackr::synth::SAMPLE_BANK_SIZE;++i){
            if(std::string(b.provenance(i).path).rfind(path,0)==0)b.clear_slot(i);
        }
        std::filesystem::remove_all(path);
    }
    int count()const{int n=0;for(const auto&p:std::filesystem::directory_iterator(path)){(void)p;++n;}return n;}
};
void files(){
    PlayUiFixture f;normal(f);auto&u=*f.play;Files files;auto&b=trackr::synth::SampleBank::instance();const auto pcm=b.slot(61).data;
    config(f,2);row(f,4);CHECK(files.count()==0);pair(f); // first click only opens confirmation
    input::down(190,228);input::move(230,200);input::move(190,228);input::up();f.capture();CHECK(files.count()==0);
    input::down(190,228);play_editor_release(false);input::up();f.capture();CHECK(files.count()==0);
    // Cancellation cannot be revived by a later release.
    tap(f,44,228);CHECK(files.count()==0);config(f,2);row(f,4);tap(f,190,228);CHECK(files.count()==1&&u.sample_slot==61&&b.slot(61).data==pcm);
    config(f,2);row(f,5);CHECK(files.count()==1);tap(f,190,228);CHECK(files.count()==2&&u.sample_slot!=61);
    CHECK(b.slot(u.sample_slot).data==pcm&&b.slot(61).data==pcm);
    // Protect source during active/pending recording even at confirmation boundary.
    config(f,2);row(f,4);f.engine->pending_rec_start=1;tap(f,190,228);CHECK(files.count()==2);f.engine->pending_rec_start=0;f.capture();
    // Crop is a new file/source switch, with source undo/redo and the raw source retained.
    edit(f,0);row(f,2,190);const int source=u.sample_slot;const auto trimmed=u.edit_state();CHECK(trimmed.start==1);
    tap(f,190,228);row(f,2);CHECK(files.count()==2);tap(f,190,228);const int cropped=u.sample_slot;
    CHECK(files.count()==3&&cropped!=source&&b.slot(source).data==pcm&&b.slot(61).data==pcm);
    CHECK(b.slot(cropped).playable_frames()==trimmed.end-trimmed.start&&u.edit_state().start==0);
    history(f,0);CHECK(u.sample_slot==source&&play_edit_equal(u.edit_state(),trimmed));
    history(f,1);CHECK(u.sample_slot==cropped&&u.edit_source_matches());
    // A changed document between press and release cannot adopt an old confirmation.
    config(f,2);row(f,4);input::down(190,228);u.params[P::P_GAIN].base-=1;input::up();f.capture();CHECK(files.count()==3);
}

void cancellation_and_lens(){
    PlayUiFixture f;normal(f);auto&u=*f.play;auto before=f.bases();
    f.engine->pending_rec_start=1;mode(f,1);drag(f,40,60,20);CHECK(u.pitch_mode()==Mode::Normal&&f.bases()==before);pair(f);
    CHECK(pixels[147*320+215]==pixels[147*320+225]); // warning replaces wave; no axis crossing its text
    f.engine->pending_rec_start=0;f.capture();
    input::down(40,60);u.set_pitch_mode(Mode::Stretch);input::move(64,60);input::up();CHECK(f.bases()==before);f.capture();
    mode(f,0);input::down(40,60);g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;input::move(64,60);input::up();CHECK(f.bases()==before);f.capture();
    input::down(40,60);trackr::synth::Sample replacement;replacement.data.assign(32000,1000);
    CHECK(trackr::synth::SampleBank::instance().replace_sample(61,std::move(replacement),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
    input::move(64,60);input::up();CHECK(f.bases()==before);f.capture();
    mode(f,0);tap(f,40,60);input::frame(0,0);input::frame(KEY_L,KEY_L);input::frame(0,KEY_L);CHECK(param_lens_is_open());
    f.capture();const auto held=f.bases();input::touch={277,82};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);input::frame(0,KEY_L,KEY_TOUCH);CHECK(f.bases()==held&&u.pitch_mode()==Mode::Normal);
    input::frame(0,0,KEY_L);input::frame(0,0);CHECK(!param_lens_is_open());
    // P-lock remains owned by its existing separate editor, never these mode keys.
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;f.capture();before=f.bases();drag(f,45,176,12);
    CHECK(f.bases()==before&&musical_note::decode(seq_display_value(0,P::P_NOTE,0))==63);pair(f);
    g_seq_lock_armed=false;g_view=View::Chains;f.capture();
    gran_browser_enabled=true;const int scans=gran_browser_scans;tap(f,100,16);CHECK(g_overlay==Overlay::SampleBrowser&&gran_browser_scans==scans+1);
    gran_browser_enabled=false;g_overlay=Overlay::Editor;f.capture();
}

void layout(){
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);PlayUiFixture f;normal(f);auto&u=*f.play;
        for(int m=0;m<5;++m){mode(f,m);pair(f);if(m<3)for(int t=0;t<3;++t){tool(f,t);pair(f);}}
        for(int i=0;i<3;++i){config(f,i);pair(f);}
        for(int note=24;note<=96;++note){mode(f,0);u.params[P::P_NOTE].base=musical_note::encode(note);f.snap();f.capture();pair(f);}
        mode(f,1);for(int speed:{-32767,-2048,0,2048,32767}){u.params[P::P_SPEED].base=q15(speed);f.snap();f.capture();pair(f);}
        auto&s=trackr::synth::SampleBank::instance().slot(61);for(int i=0;i<16;++i)s.chops[i]=i*128001u/16;
        f.capture();tool(f,1);for(int i=0;i<16;++i){tap(f,14+i*13+4,145);pair(f);}
        edit(f,1);pair(f);tap(f,190,228);pair(f);home(f);edit(f,0);pair(f);tap(f,190,228);pair(f);
        home(f);tool(f,0);tap(f,44,202);pair(f);home(f);tap(f,118,202);pair(f);home(f);tap(f,118,228);pair(f);
        mode(f,4);tap(f,190,202);pair(f);
        home(f);u.set_modern(false);f.capture();pair(f);config(f,0);pair(f);home(f);
        // Long names and unloaded slots must not collide with the rail or upper header.
        trackr::synth::Sample long_source;long_source.data.assign(256,0);
        CHECK(trackr::synth::SampleBank::instance().replace_sample(61,std::move(long_source),trackr::synth::SampleOrigin::SdWav,
            "test/abcdefghijklmnopqrstuvwxyz0123456789_long_file.wav")==trackr::synth::SampleEditResult::Ok);f.capture();f.warm();pair(f);
        trackr::synth::Sample empty;CHECK(trackr::synth::SampleBank::instance().replace_sample(61,std::move(empty),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
        f.capture();pair(f);for(int i=0;i<3;++i){config(f,i);pair(f);}
    }
}

void owners(){
    PlayUiFixture f;normal(f);auto&u=*f.play;auto*a=new ArpUnit;
    a->params[ArpUnit::P_PLAY_MODE].base=32767;a->params[ArpUnit::P_PLAY_MODE].snap();
    CHECK(f.engine->replace_unit(0,0,a)==&u);delete f.engine->replace_unit(0,1,&u);g_edit_slot=1;u.params[P::P_LOOP].base=32767;f.snap();f.capture();
    f.engine->set_sequencer_enabled(false);f.engine->transport_play();std::array<q15,4096> out{};render_audio(f,out,256);CHECK(u.playback_active());
    mode(f,1);CHECK(u.pitch_mode()==Mode::Stretch);render_audio(f,out,256);CHECK(u.playback_active());
    history(f,0);CHECK(u.pitch_mode()==Mode::Normal);render_audio(f,out,256);CHECK(u.playback_active());
    u.bypass=true;delete f.engine->replace_unit(0,2,new OscUnit);render_audio(f,out,256);f.capture();const auto other=a->visual().events;
    mode(f,1);CHECK(a->visual().events==other&&a->visual().active);
    // Sequence-origin back goes to the original sequence without source/step mutations.
    delete f.engine->replace_unit(0,2,nullptr);u.bypass=false;f.engine->transport_stop();play_editor_open_cuts(0,1,true);f.capture();
    const auto score=f.hash();tap(f,277,223);CHECK(play_focus_back(u));CHECK(g_view==View::Chains);CHECK(play_focus_back(u));
    CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None&&f.hash()==score);
}

void type_and_targets(){
    namespace pt=play_editor_type;namespace pl=play_editor_layout;
    for(const auto&l:pt::labels){module_inspector_surface(false);pt::word(20,70,l.text,l.size,menu_style::ink());bounds();
        const int stride=(l.width+7)/8;
        for(int y=0;y<l.height;++y)for(int x=0;x<l.width;++x){const bool on=pt::bitmap[l.offset+y*stride+x/8]&(0x80u>>(x&7));
            CHECK((pixels[(70+l.top+y)*320+20+x]==menu_style::ink())==on);}}
    auto good=[](pl::Box b){CHECK(b.x0>=0&&b.y0>=0&&b.x1<=320&&b.y1<=240&&b.x1-b.x0>=32&&b.y1-b.y0>=24);};
    for(auto b:{pl::source,pl::primary,pl::secondary,pl::wave,pl::config,pl::foot,pl::context_left,pl::context_right})good(b);
    for(int i=0;i<5;++i)good(pl::mode(i));
    for(int i=0;i<3;++i){good(pl::tool(i));good(pl::tab(i));good(pl::context(i));good(pl::footer(i));}
    for(int i=0;i<6;++i)good(pl::row(i));
    for(int midi=24;midi<=96;++midi){char label[16];musical_note::name(label,sizeof label,midi);CHECK(pt::value_width(label,40)<100);
        module_inspector_surface(false);pt::value(14,77,label,40,menu_style::ink());bounds();CHECK(objects>0);}
    CHECK(pt::find('#',40)&&pt::find('#',28));
}

int proof(){int total=0,failed=0;auto run=[&](const char*name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception&e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("native gothic masks and bounded targets",type_and_targets);
    run("mode rail, note/tempo and undo/redo",engines);
    run("scratch/hold, prior engine and private audition",audition);
    run("bounded voice/listen/file config",configuration);
    run("trim/fades/loop/cuts/zoom retained editing",editing);
    run("copy confirmations and out/back cancellation",files);
    run("record/stale/modal/lens/p-lock/source guards",cancellation_and_lens);
    run("frozen two-theme geometry and all note/cut ranges",layout);
    run("legato ARP owner and sequence-origin return",owners);
    std::printf("play modes UI: %d/%d groups; %d frozen duals; %d gestures; peak %d/8192; 0 inherited groups\n",total-failed,total,duals,gestures,peak);
    return failed?1:0;
}

void capture(const std::filesystem::path&dir){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    std::ofstream manifest(dir/"manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\n";int pairs=0;
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        palette::set(theme);PlayUiFixture f;normal(f);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        auto shot=[&](const char*name){f.warm();f.capture();surface(f,true);const int top=objects;dump_surface(dir,prefix+name+"_top");
            surface(f,false);CHECK(top+objects<=8192);dump_surface(dir,prefix+name+"_bottom");manifest<<prefix+name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';++pairs;};
        for(int i=0;i<5;++i){mode(f,i);shot((const char*[]){"normal","stretch","tape","scratch","hold"}[i]);}
        mode(f,1);auto&s=trackr::synth::SampleBank::instance().slot(61);for(int i=0;i<8;++i)s.chops[i]=i*128001u/8;f.capture();
        tool(f,1);tap(f,82,145);shot("stretch_slices");tool(f,2);shot("loop");
        for(int i=0;i<3;++i){config(f,i);shot((const char*[]){"config_voice","config_listen","config_file"}[i]);}
        edit(f,0);shot("trim_edit");tap(f,190,228);shot("trim_more");home(f);tool(f,0);tap(f,44,202);shot("fades");
        home(f);tap(f,118,202);shot("zoom");home(f);tap(f,118,228);shot("history");
        mode(f,1);edit(f,1);shot("cut_edit");tap(f,190,228);shot("cut_tools");home(f);tool(f,1);tap(f,118,202);shot("auto_cuts");
        mode(f,4);tap(f,190,202);shot("sequence_scrub");mode(f,0);f.engine->pending_rec_start=1;shot("protected");f.engine->pending_rec_start=0;
    }
    manifest.close();std::ofstream receipt(dir/"capture.json");receipt<<"{\"pairs\":"<<pairs<<",\"regression_groups_run\":0,\"origin\":\"actual C++ main/editor raster, engine synthetic PCM fixture, host static paper; not hardware\"}\n";
    std::printf("play modes capture: %d pairs; 0 regression groups\n",pairs);
}
}
int main(int argc,char**argv){theme_test::select();if(argc==3&&std::string(argv[1])=="--capture-only"){play_modes_cases::capture(argv[2]);return 0;}
    if(argc==2&&std::string(argv[1])=="--proof-only")return play_modes_cases::proof();
    return 2;
}
