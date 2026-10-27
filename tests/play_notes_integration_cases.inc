// Appended to the current real editor/main dispatcher harness. Only these groups run.
#include "../platform/3ds/sequence_note.h"
#include "../core/engine/patch_io.h"
#include <fstream>
namespace play_notes_cases {
using P=PlayUnit;using Mode=P::PitchMode;
int duals=0,gestures=0;
void bounds(){CHECK(!outside_rects&&!outside_lines&&!text_overlaps&&objects<=8192);for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);}
void pair(PlayUiFixture&f){f.capture();module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(play_focus_draw_top());}bounds();int n=objects;
    module_inspector_surface(false);{ModuleInspectorNoEngine hidden;play_focus_draw(*f.play);}bounds();CHECK(n+objects<=8192);++duals;}
void drag(int x,int y,int dx){input::down(x,y);input::move(x,y);input::move(x+dx,y);input::up();++gestures;}
void editor(){
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);PlayUiFixture f;auto&u=*f.play;u.set_pitch_mode(Mode::Normal);f.snap();f.warm();
        const auto before=f.bases();const int dirty=mutations;
        drag(45,46,0);CHECK(f.bases()==before&&mutations==dirty&&play_focus_selected()==P::P_NOTE);
        drag(45,46,4);CHECK(musical_note::decode(u.params[P::P_NOTE].base)==61&&u.params[P::P_SPEED].base==8192);
        f.tap(220,72);CHECK(u.params[P::P_NOTE].base==before[P::P_NOTE]);f.tap(280,72);CHECK(musical_note::decode(u.params[P::P_NOTE].base)==61);
        input::down(220,46);input::up();input::down(30,168);input::up();f.capture();pair(f);CHECK(has_text("normal")&&has_text("stretch")&&has_text("tape"));
        input::down(140,96);input::up();CHECK(u.pitch_mode()==Mode::Stretch);f.capture();pair(f);CHECK(has_text("tempo / drag"));
        const q15 note=u.params[P::P_NOTE].base;
        const auto origin=f.bases();input::down(150,168);input::move(166,168);input::move(150,168);input::up();CHECK(f.bases()==origin);++gestures;
        drag(150,168,16);CHECK(u.params[P::P_SPEED].base==10240&&u.params[P::P_NOTE].base==note);
        const auto moved=f.bases();drag(150,168,0);CHECK(f.bases()==moved);
        f.tap(220,72);CHECK(u.params[P::P_SPEED].base==8192);f.tap(280,72);CHECK(u.params[P::P_SPEED].base==10240);
        f.tap(30,200);CHECK(u.params[P::P_SPEED].base==-10240&&u.params[P::P_NOTE].base==note);
        for(auto mode:{Mode::Normal,Mode::Stretch,Mode::Legacy})for(int midi:{24,25,60,61,95,96}){
            u.set_pitch_mode(mode);u.params[P::P_NOTE].base=musical_note::encode(midi);f.snap();f.capture();pair(f);
            char t[24];play_readout::value_text(t,sizeof t,P::P_NOTE,u.params[P::P_NOTE].base,true,false,mode);
            char expected[24];musical_note::name(expected,sizeof expected,midi);CHECK(std::string(t)==expected);
        }
        u.set_pitch_mode(Mode::Normal);f.capture();auto saved=f.bases();f.engine->pending_rec_start=1;
        drag(45,46,32);CHECK(f.bases()==saved);f.engine->pending_rec_start=0;
        // Mode is part of the gesture/history identity; no stale write after external change.
        input::down(45,46);u.set_pitch_mode(Mode::Stretch);input::move(77,46);input::up();CHECK(f.bases()==saved);
    }
    // A deliberate model switch re-arms only the owning legato ARP, not transport.
    {PlayUiFixture f;auto&u=*f.play;u.set_pitch_mode(Mode::Normal);
        auto*a=new ArpUnit;a->params[ArpUnit::P_PLAY_MODE].base=32767;a->params[ArpUnit::P_PLAY_MODE].snap();
        CHECK(f.engine->replace_unit(0,0,a)==&u);delete f.engine->replace_unit(0,1,&u);g_edit_slot=1;
        u.params[P::P_LOOP].base=32767;f.snap();f.capture();f.engine->set_sequencer_enabled(false);f.engine->transport_play();
        std::array<q15,512> pcm{};f.engine->render(pcm.data(),256);CHECK(u.playback_active());
        f.tap(220,46);f.tap(30,168);f.tap(140,96);CHECK(u.pitch_mode()==Mode::Stretch);
        const auto clock=f.engine->transport_frame;f.engine->render(pcm.data(),256);CHECK(u.playback_active()&&f.engine->transport_frame==clock+256);
        f.tap(220,72);CHECK(u.pitch_mode()==Mode::Normal);f.engine->render(pcm.data(),256);CHECK(u.playback_active());
        const auto events=a->visual().events;drag(45,46,4);f.engine->render(pcm.data(),256);CHECK(u.playback_active());
        f.tap(220,72);CHECK(u.playback_active());f.engine->render(pcm.data(),256);CHECK(a->visual().events==events);
        // Conversion back to the six-parameter model must detect the OLD mode.
        u.set_modern(false);f.capture();f.tap(220,46);f.tap(30,168);f.tap(30,96);CHECK(u.pitch_mode()==Mode::Normal);
        f.engine->render(pcm.data(),256);CHECK(u.playback_active());f.tap(220,72);CHECK(!u.modern()&&u.pitch_mode()==Mode::Legacy);
        f.engine->render(pcm.data(),256);CHECK(u.playback_active());
        // A bypassed PLAY between ARP and OSC is not that ARP's target.
        u.bypass=true;delete f.engine->replace_unit(0,2,new OscUnit);f.engine->render(pcm.data(),256);f.capture();
        const auto other_events=a->visual().events;f.tap(220,46);f.tap(30,168);f.tap(140,96);CHECK(u.pitch_mode()==Mode::Stretch);
        CHECK(a->visual().events==other_events&&a->visual().active);
    }
}
void locks_lens(){
    PlayUiFixture f;auto&u=*f.play;u.set_pitch_mode(Mode::Normal);f.snap();f.capture();
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;f.capture();
    const auto before=f.bases();drag(45,176,12);CHECK(f.bases()==before&&musical_note::decode(seq_display_value(0,P::P_NOTE,0))==63);
    CHECK(f.engine->pattern.steps[0][7].locks[0].param==P::P_NOTE);
    for(int p:{P::P_GAIN,P::P_START,P::P_LEN})CHECK(f.engine->set_step_lock(0,7,0,p,10000));
    f.engine->remove_step_lock(0,7,0,P::P_NOTE);CHECK(f.engine->set_step_lock(0,7,0,P::P_LOOP,32767));
    const auto h=f.hash();drag(45,176,20);CHECK(f.hash()==h&&f.bases()==before&&g_seq_lock_flash>0);
    g_seq_lock_armed=false;g_view=View::Chains;f.capture();play_focus_select(P::P_NOTE);
    input::frame(0,0);input::frame(KEY_L,KEY_L);input::frame(0,KEY_L);CHECK(param_lens_is_open());f.capture();
    const auto n=u.params[P::P_NOTE].base;input::touch={160,102};input::frame(KEY_TOUCH,KEY_L|KEY_TOUCH);
    input::frame(0,KEY_L|KEY_TOUCH);CHECK(u.params[P::P_NOTE].base==n);
    input::touch={180,102};input::frame(0,KEY_L|KEY_TOUCH);input::frame(0,KEY_L,KEY_TOUCH);
    CHECK(musical_note::decode(u.params[P::P_NOTE].base)>musical_note::decode(n));
    f.capture();module_inspector_surface(false);param_lens_draw_bottom();CHECK(has_text("base"));
    input::frame(0,0,KEY_L);input::frame(0,0);CHECK(!param_lens_is_open());
    for(int note=24;note<=96;++note){char t[20];const auto value=musical_note::encode(note);
        seq_lock::format_value(UnitKind::SamplePlayer,5,P::P_NOTE,value,t,sizeof t);char e[20];musical_note::name(e,sizeof e,note);CHECK(std::string(t)==e);
        for(int sign:{-1,1})CHECK(musical_note::decode(seq_lock::nudge_value(UnitKind::SamplePlayer,5,P::P_NOTE,value,sign,false))==std::max(24,std::min(96,note+sign)));
    }
}
void score(){
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);Fixture f;auto*p=new P;p->set_pitch_mode(Mode::Normal);delete f.engine->replace_unit(0,0,p);
        f.refresh();sequencer_view_enter();f.mode(1);f.touch(0,0);
        CHECK(sequencer_snapshot().sequence_pitch_param[0]==P::P_NOTE);
        for(int midi=24;midi<=96;++midi){CHECK(f.engine->set_step_lock(0,0,0,P::P_NOTE,musical_note::encode(midi)));f.frame();
            char t[20],e[20];sequencer_selected_pitch_text(t,sizeof t);musical_note::name(e,sizeof e,midi);CHECK(std::string(t)==e);
            draw();bounds();draw(true);bounds();++duals;
        }
        for(int x:{38,160,282}){f.frame();sequencer_view_touch_down(x,218);f.release();f.frame();
            CHECK(musical_note::decode(sequencer_snapshot().sequence_pitch[0][0])==(x==38?24:x==160?60:96));CHECK(p->params[P::P_SPEED].base==8192);}
        CHECK(musical_note::decode(p->params[P::P_NOTE].base)==60);
        // Slot changes/masked overwrite source cannot write the old PLAY note.
        delete f.engine->replace_unit(0,1,new OscUnit);f.frame();CHECK(sequencer_snapshot().sequence_pitch_slot[0]==1);
        for(auto kind:{UnitKind::Osc,UnitKind::Fm,UnitKind::Inst,UnitKind::Bird,UnitKind::Grain}){
            delete f.engine->replace_unit(0,1,nullptr);delete f.engine->replace_unit(0,0,make_unit(kind));f.engine->clear_track(0);
            const int param=f.engine->chains[0].units[0]->sequencer_properties().pitch_param;
            for(int midi=sequence_note::minimum(kind);midi<=sequence_note::maximum(kind);++midi){
                const q15 v=sequence_note::value(kind,midi);CHECK(sequence_note::midi(kind,v)==midi);
                CHECK(f.engine->set_step_lock(0,0,0,param,v));f.frame();char t[20],e[20];sequencer_selected_pitch_text(t,sizeof t);sequence_note::name(e,sizeof e,midi);CHECK(std::string(t)==e);
            }
            draw();bounds();draw(true);bounds();++duals;
        }
    }
}
void engine_notes(){
    for(auto mode:{Mode::Normal,Mode::Stretch}){
        PlayUiFixture f;auto&u=*f.play;u.set_pitch_mode(mode);u.params[P::P_LOOP].base=32767;f.snap();
        auto*a=new ArpUnit;CHECK(f.engine->replace_unit(0,0,a)==&u);delete f.engine->replace_unit(0,1,&u);g_edit_slot=1;
        for(int p:{ArpUnit::P_NOTE1,ArpUnit::P_NOTE2,ArpUnit::P_NOTE3,ArpUnit::P_NOTE4}){a->params[p].base=ArpUnit::note_value(p==ArpUnit::P_NOTE1?7:ArpUnit::NOTE_OFF);a->params[p].snap();}
        a->params[ArpUnit::P_PLAY_MODE].base=32767;a->params[ArpUnit::P_PLAY_MODE].snap();
        f.engine->pattern.length[0]=4;for(int step=0;step<3;++step)f.engine->pattern.steps[0][step].flags=SeqStep::Trig;
        CHECK(f.engine->set_step_lock(0,0,1,P::P_NOTE,musical_note::encode(60)));
        CHECK(f.engine->set_step_lock(0,1,1,P::P_NOTE,musical_note::encode(62)));
        f.engine->set_sequencer_enabled(true);f.engine->transport_play();
        auto render=[&](int count){std::array<q15,512> pcm{};while(count){const int n=std::min(count,256);f.engine->render(pcm.data(),n);count-=n;}};
        // Transport play schedules the first sequence step at the NEXT sixteenth;
        // the independent ARP already begins at frame zero.
        render(4001);CHECK(u.actual_note()==67&&u.params[P::P_NOTE].step_lock_override);
        render(4000);CHECK(u.actual_note()==69&&musical_note::decode(u.params[P::P_NOTE].base)==60);
        render(4000);CHECK(u.actual_note()==67&&!u.params[P::P_NOTE].step_lock_override);
        f.engine->transport_stop();
        // Private editor voice follows note/tempo but never steals the source.
        u.reset();u.params[P::P_NOTE].base=musical_note::encode(72);f.snap();
        const auto token=f.engine->start_play_preview(0,1,0,64000,false,false,256,false);CHECK(token);
        render(100);const uint32_t pos=f.engine->play_preview_position(token);
        CHECK(pos==(mode==Mode::Normal?198u:99u)&&!u.playback_active());
        u.params[P::P_NOTE].base=musical_note::encode(60);f.snap();render(100);
        CHECK(f.engine->play_preview_position(token)==(mode==Mode::Normal?299u:199u));
        u.set_pitch_mode(mode==Mode::Normal?Mode::Stretch:Mode::Normal);render(1);CHECK(!f.engine->play_preview_active(token));
    }
}
void projection(){
    PlayUiFixture f;auto&u=*f.play;auto&s=trackr::synth::SampleBank::instance().slot(PlayUiFixture::SAMPLE);
    s.chops[0]=40;s.chops[1]=18000;s.slice_reverse_mask=1;
    for(auto mode:{Mode::Normal,Mode::Stretch})for(int note:{24,48,60,61,72,96})for(int speed:{-32767,-1,0,2048,8192,32767})for(int cut:{0,1})for(int rate:{64,256,1024}){
        s.slice_speed_q8[0]=uint16_t(rate);u.set_pitch_mode(mode);u.params[P::P_NOTE].base=musical_note::encode(note);
        u.params[P::P_SPEED].base=q15(speed);u.params[P::P_SLICE].base=cut?seq_slice::encode(1):0;f.snap();
        q15 values[P::P_COUNT];for(int p=0;p<P::P_COUNT;++p)values[p]=u.params[p].base;
        const auto reference=u.projected_window(values[P::P_START],values[P::P_LEN],values[P::P_SLICE]);
        const auto stored=sequence_note::play_window(s,values,u.pitch_quantize,u.modern(),u.edit_state(),mode);
        CHECK(stored.increment==reference.increment&&stored.start==reference.start&&stored.end==reference.end&&stored.traversal_ms()==reference.traversal_ms());
    }
}
void arp(){
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);ArpFixture f;auto*p=new P;p->set_pitch_mode(Mode::Normal);delete f.engine->replace_unit(0,1,p);
        for(int root:{60,62}){p->params[P::P_NOTE].base=musical_note::encode(root);p->params[P::P_NOTE].snap();f.capture();
            module_inspector_surface(false);{ModuleInspectorNoEngine hidden;arp_focus_draw(f.unit());}bounds();
            CHECK(has_text(root==60?"c4":"d4")&&has_text(root==60?"e4":"f#4")&&has_text(root==60?"g4":"a4"));
            module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(arp_focus_draw_top());}bounds();CHECK(has_text("current cycle / notes from source"));++duals;
        }
        const q15 root=p->params[P::P_NOTE].base;drag(35,94,4);CHECK(p->params[P::P_NOTE].base==root&&ArpUnit::note_semitones(f.unit().params[ArpUnit::P_NOTE1].base)==1);
    }
}
struct Temp {
    std::string dir,path;Temp(){char p[]="/tmp/seance-note-storage-XXXXXX";CHECK(mkdtemp(p));dir=p;path=dir+"/patch.snc";}
    ~Temp(){std::filesystem::remove_all(dir);}
};
std::vector<uint8_t> bytes(const std::string&p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void put(const std::string&p,const std::vector<uint8_t>&v){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(v.data()),v.size());CHECK(bool(out));}
uint32_t u32(const std::vector<uint8_t>&v,size_t p){CHECK(p+4<=v.size());return uint32_t(v[p])|uint32_t(v[p+1])<<8|uint32_t(v[p+2])<<16|uint32_t(v[p+3])<<24;}
std::pair<size_t,size_t> chunk(const std::vector<uint8_t>&v,const char*name){for(size_t p=5;p+8<=v.size();){size_t end=p+8+u32(v,p+4);CHECK(end<=v.size());if(!std::memcmp(v.data()+p,name,4))return {p,end};p=end;}return {0,0};}
void storage(){
    Temp t;auto engine=std::make_unique<Engine>();auto*p=new P;delete engine->replace_unit(0,0,p);
    CHECK(patchio::save(*engine,t.path.c_str()));auto original=bytes(t.path);CHECK(!chunk(original,"PNOT").first);
    for(auto mode:{Mode::Normal,Mode::Stretch}){
        p->set_pitch_mode(mode);p->params[P::P_NOTE].base=musical_note::encode(65);p->params[P::P_NOTE].src=ModSource::SigilH;p->params[P::P_NOTE].mod_depth=-4000;p->params[P::P_NOTE].mod_enabled=false;
        CHECK(engine->set_step_lock(0,4,0,P::P_NOTE,musical_note::encode(72)));
        engine->pattern_banks[3].steps[0][9].locks[0]={0,P::P_NOTE,musical_note::encode(24)};engine->pattern_banks[3].steps[0][9].lock_count=1;
        engine->capture_scene(0,0);
        CHECK(patchio::save(*engine,t.path.c_str()));const auto good=bytes(t.path);const auto n=chunk(good,"PNOT");CHECK(n.first&&n.second-n.first==18);
        auto loaded=std::make_unique<Engine>();CHECK(patchio::load(*loaded,t.path.c_str()));auto*q=static_cast<P*>(loaded->chains[0].units[0]);
        CHECK(q&&q->pitch_mode()==mode&&q->num_params==12&&q->params[P::P_NOTE].base==p->params[P::P_NOTE].base);
        CHECK(q->params[P::P_NOTE].src==ModSource::SigilH&&q->params[P::P_NOTE].mod_depth==-4000&&!q->params[P::P_NOTE].mod_enabled);
        CHECK(loaded->pattern.steps[0][4].locks[0].param==P::P_NOTE&&loaded->pattern_banks[3].steps[0][9].locks[0].value==0);
        CHECK(loaded->scenes[0][0].base[0][P::P_NOTE]==musical_note::encode(65));
        // Chunk order is irrelevant. PNOT before CORE must work.
        auto reorder=good;std::vector<uint8_t> note(good.begin()+n.first,good.begin()+n.second);reorder.erase(reorder.begin()+n.first,reorder.begin()+n.second);reorder.insert(reorder.begin()+5,note.begin(),note.end());
        put(t.path,reorder);CHECK(patchio::load(*loaded,t.path.c_str()));CHECK(static_cast<P*>(loaded->chains[0].units[0])->pitch_mode()==mode);
        // Reject unknown mode, negative note, wrong version/count, duplicate/truncated chunks transactionally.
        for(int damage=0;damage<6;++damage){auto bad=good;
            if(damage==0)bad[n.first+12]=3;
            if(damage==1)bad[n.first+14]=0x80;
            if(damage==2)bad[n.first+8]=2;
            if(damage==3)bad[n.first+9]=0;
            if(damage==4)bad.insert(bad.end(),note.begin(),note.end());
            if(damage==5)bad.resize(n.second-1);
            put(t.path,bad);Unit*identity=loaded->chains[0].units[0];CHECK(!patchio::load(*loaded,t.path.c_str()));CHECK(loaded->chains[0].units[0]==identity);
        }
        auto absent=good;absent.erase(absent.begin()+n.first,absent.begin()+n.second);put(t.path,absent);CHECK(patchio::load(*loaded,t.path.c_str()));q=static_cast<P*>(loaded->chains[0].units[0]);
        CHECK(q->pitch_mode()==Mode::Legacy&&q->num_params==11&&musical_note::decode(q->params[P::P_NOTE].base)==60);
        CHECK(!loaded->pattern.steps[0][4].lock_count&&!loaded->pattern_banks[3].steps[0][9].lock_count);
        // Toggling back to tape retains authored note state across a save, but cannot affect tape audio.
        p->set_pitch_mode(Mode::Legacy);CHECK(patchio::save(*engine,t.path.c_str()));CHECK(patchio::load(*loaded,t.path.c_str()));q=static_cast<P*>(loaded->chains[0].units[0]);
        CHECK(q->pitch_mode()==Mode::Legacy&&q->params[P::P_NOTE].base==musical_note::encode(65)&&!q->params[P::P_NOTE].mod_enabled);
    }
    put(t.path,original);auto old=std::make_unique<Engine>();CHECK(patchio::load(*old,t.path.c_str()));CHECK(patchio::save(*old,t.path.c_str()));CHECK(bytes(t.path)==original);
}
}
int main(){theme_test::select();int failed=0,total=0;auto run=[&](const char*name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception&e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("note/stretch editor and frozen two-theme geometry",play_notes_cases::editor);
    run("note locks, capacity, lens and categorical nudges",play_notes_cases::locks_lens);
    run("absolute sequencer notes, every key, both screens",play_notes_cases::score);
    run("actual sequence note edges plus arp and independent preview",play_notes_cases::engine_notes);
    run("stored note/tempo window projections match runtime",play_notes_cases::projection);
    run("arp note names relative to source root",play_notes_cases::arp);
    run("PNOT persistence, compatibility and transactional rejection",play_notes_cases::storage);
    std::printf("play notes integration: %d/%d passed; %d duals; %d gestures; 0 inherited groups\n",total-failed,total,play_notes_cases::duals,play_notes_cases::gestures);return failed?1:0;}
