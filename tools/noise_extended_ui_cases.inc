// Current NOISE-only native host proof. Imports the old module-headings driver as infrastructure, never its run().
namespace noise_extended_cases {
namespace h=module_headings_cases;
using N=NoiseUnit;
int gestures=0,frozen=0,models=0,format_checks=0,lens_checks=0;
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Noise){g_overlay=Overlay::None;module_inspector_capture();
  g_overlay=Overlay::Editor;module_inspector_capture();noise_focus_select(N::P_DENSITY);capture();}
 ~Scene(){editor_cancel_input();param_lens_cancel();}
};
N& unit(Scene& s){return *static_cast<N*>(s.unit);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void take(Scene& s,const std::string& id,const char* variant){
 s.capture();module_inspector_capture();h::take(s,id,variant);
 s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();
 module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;
 module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;noise_focus_draw(unit(s));
  if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);++frozen;
}
void value(Scene& s,int p,q15 v){s.set(p,v);}
void scenes(const std::string& prefix){
 {Scene s;CHECK(unit(s).modern());value(s,N::P_MODE,16384);value(s,N::P_COLOUR,0);
  value(s,N::P_SWEEP,15000);value(s,N::P_RESONANCE,21000);
  noise_focus_select(N::P_SWEEP);take(s,prefix+"noise_main_hit","white-hit");}
 {Scene s;value(s,N::P_COLOUR,10922);value(s,N::P_MODE,32767);
  value(s,N::P_TONE,23000);take(s,prefix+"noise_pink_drone","pink-drone");}
 {Scene s;value(s,N::P_COLOUR,21845);value(s,N::P_MODE,16384);
  value(s,N::P_DENSITY,9000);tap(213,13);CHECK(noise_focus_modern());
  noise_focus_select(N::P_EVENT_LENGTH);take(s,prefix+"noise_crackle_setup","crackle-setup");}
 {Scene s;value(s,N::P_COLOUR,32767);value(s,N::P_MODE,16384);
  value(s,N::P_SWEEP,-32767);take(s,prefix+"noise_metal_negative","metal-signed");}
 {Scene s;auto& u=unit(s);u.set_modern(false);s.capture();CHECK(u.num_params==N::LEGACY_COUNT);
  take(s,prefix+"noise_legacy","legacy");auto& scene=s.engine->scenes[0][0];
  scene.kind[s.slot]=UnitKind::Noise;for(int p=0;p<N::P_COUNT;++p)scene.base[s.slot][p]=q15(4000+p);
  const int neighbor=(s.slot+1)%UNITS_PER_CHAIN;scene.kind[neighbor]=UnitKind::Noise;
  scene.base[neighbor][N::P_RESONANCE]=12345;
  const q15 old=u.params[N::P_DENSITY].base;tap(184,13);CHECK(u.modern()&&u.params[N::P_DENSITY].base==old);
  N defaults;for(int p=0;p<N::LEGACY_COUNT;++p)CHECK(scene.base[s.slot][p]==q15(4000+p));
  for(int p=N::LEGACY_COUNT;p<N::P_COUNT;++p){CHECK(scene.base[s.slot][p]==defaults.params[p].base);
   CHECK(u.params[p].base==defaults.params[p].base);}
  CHECK(scene.base[neighbor][N::P_RESONANCE]==12345);
  take(s,prefix+"noise_extended","explicit-extend");}
 {Scene s;unit(s).bypass=true;take(s,prefix+"noise_bypass","unavailable-peak");}
 {Scene s;g_ui_snapshot.effect_focus_valid=true;g_ui_snapshot.effect_focus_kind=uint8_t(UnitKind::Motor);
  noise_focus_capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(true);
  {fader_lancet_cases::AudioHidden hidden;top_draw();}
  bool unavailable=false;for(const auto& item:texts)if(item.text=="out --")unavailable=true;
  CHECK(unavailable&&!outside_rects&&!outside_lines);++models;}
 {Scene s;value(s,N::P_SWEEP,-16384);noise_focus_select(N::P_SWEEP);s.capture();u32 down=KEY_L;
  CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
  take(s,prefix+"noise_mod","mod-lens");param_lens_cancel();}
}
void interactions(){
 {Scene s;auto&u=unit(s);value(s,N::P_SWEEP,0);value(s,N::P_MODE,16384);
  editor_touch_down(215,172);CHECK(u.params[N::P_SWEEP].base==0);
  editor_touch_drag(250,172);CHECK(u.params[N::P_SWEEP].base>0);
  editor_touch_drag(180,172);CHECK(u.params[N::P_SWEEP].base<0);editor_touch_release();++gestures;
  tap(86,66);CHECK(N::drone_mode(u.params[N::P_MODE].base,u.params[N::P_DECAY].base));
  const auto before=u.params[N::P_SWEEP].base;editor_touch_down(215,172);
  editor_touch_drag(256,172);editor_touch_release();CHECK(u.params[N::P_SWEEP].base==before);++gestures;}
 {Scene s;auto&u=unit(s);value(s,N::P_MODE,16384);tap(210,13);CHECK(u.modern());
  tap(85,89);const q15 first=u.params[N::P_EVENT_LENGTH].base;
  tap(210,13);tap(190,40);tap(210,13);
  editor_touch_down(85,89);editor_touch_drag(125,89);editor_touch_release();
  CHECK(u.params[N::P_EVENT_LENGTH].base!=first);++gestures;
  const q15 second=u.params[N::P_SWEEP_TIME].base;
  editor_touch_down(88,164);editor_touch_drag(126,164);editor_touch_release();
  CHECK(u.params[N::P_SWEEP_TIME].base!=second);++gestures;}
 {Scene s;value(s,N::P_MODE,16384);value(s,N::P_TONE,4000);s.capture();module_inspector_capture();
  editor_style::capture();editor_gpu::begin();module_inspector_surface(true);
  {fader_lancet_cases::AudioHidden hidden;top_draw();}auto before=pixels;
  value(s,N::P_TONE,30000);s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();
  module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}
  CHECK(pixels!=before);int response_pixels=0;
  for(int y=72;y<136;++y)for(int x=34;x<386;++x)response_pixels+=pixels[y*400+x]!=before[y*400+x];
  CHECK(response_pixels>20);++models;}
 {Scene s;char text[32];for(int p=0;p<N::P_COUNT;++p){noise_focus_select(p);s.capture();
   module_inspector_value_text(text,sizeof text,UnitKind::Noise,p,unit(s).params[p].base);
   CHECK(text[0]&&std::strcmp(text,"--"));++format_checks;}
  noise_focus_select(N::P_SWEEP);noise_focus_value_text(text,sizeof text,N::P_SWEEP,-32767);
  CHECK(std::string(text).find("-24.00st")!=std::string::npos);++format_checks;}
}
void banks(){
 Scene s;auto&u=unit(s);value(s,N::P_MODE,16384);value(s,N::P_COLOUR,0);const q15 base=u.params[N::P_SWEEP].base;
 g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<NUM_PATTERNS;++b){
  if(b<4){const int old=sequencer_edit_bank();extent_test_open(s);
   for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);
   extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}
  CHECK(sequencer_edit_bank()==b);s.engine->request_pattern((b+3)%NUM_PATTERNS,false);
  auto&step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
  const int active_count=s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  editor_touch_down(210,172);editor_touch_drag(240,172);editor_touch_release();
  CHECK(step.lock_count==1&&step.locks[0].slot==s.slot&&step.locks[0].param==N::P_SWEEP);
  CHECK(step.locks[0].value==q15(base+30*65534/296));CHECK(u.params[N::P_SWEEP].base==base);++gestures;
  tap(180,159);CHECK(step.lock_count==0&&u.params[N::P_SWEEP].base==base);
  tap(20,41);CHECK(step.lock_count==1&&step.locks[0].param==N::P_COLOUR&&step.locks[0].value==0);
  step.lock_count=0;
  editor_touch_down(210,172);g_seq_lock_step=6;editor_touch_drag(240,172);editor_touch_release();
  CHECK(step.lock_count==0);g_seq_lock_step=5;++gestures;
  step.lock_count=1;step.locks[0]={uint8_t(s.slot),N::P_COLOUR,32767};
  tap(20,41);CHECK(step.lock_count==1&&step.locks[0].value==0);step.lock_count=0;
  for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%UNITS_PER_CHAIN),uint8_t(i),1234};
  step.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;
  editor_touch_down(210,172);editor_touch_drag(240,172);editor_touch_release();
  CHECK(g_seq_lock_flash>0&&step.lock_count==SEQ_LOCKS&&u.params[N::P_SWEEP].base==base);
  CHECK(s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count==active_count);++gestures;
  g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
 }
 g_view=View::Chains;g_overlay=Overlay::None;
}
void all_mod(){
 Scene s;value(s,N::P_SWEEP,-16384);
 for(int p=0;p<N::P_COUNT;++p){
  noise_focus_select(p);s.capture();u32 down=KEY_L;
  CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
  module_inspector_capture();editor_style::capture();editor_gpu::begin();
  for(bool top:{true,false}){module_inspector_surface(top);
   {fader_lancet_cases::AudioHidden hidden;if(top)top_draw();else editor_draw();}
   CHECK(!outside_rects&&!outside_lines&&!text_overlaps);
  }
  if(p==N::P_SWEEP){char text[32];noise_focus_value_text(text,sizeof text,p,-32767);CHECK(text[0]=='-');}
  param_lens_cancel();++lens_checks;
 }
}
int run(const std::filesystem::path& dir,bool save){
 CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
 h::output=dir;h::capture_files=save;h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());
 h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 fader_lancet_cases::sample_fixture();CHECK(editor_style::init());
 for(const auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
  palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  scenes(prefix);interactions();banks();all_mod();}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("noise extended: %d duals %d gestures %d frozen %d model %d formatter %d mod-lenses, native host only\n",
  h::duals,gestures,frozen,models,format_checks,lens_checks);return 0;
}
} // namespace noise_extended_cases
int main(int argc,char** argv){try{CHECK(argc==3);return noise_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"noise extended: %s\n",e.what());return 1;}}
