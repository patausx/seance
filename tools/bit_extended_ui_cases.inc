// Current BIT-only native host proof. Reuses the host boundary, never an inherited run().
#include "../platform/3ds/bit_editor_visual.h"
#include "../platform/3ds/bit_reference.h"
#include "../platform/3ds/sequencer_lock_format.h"
namespace bit_extended_cases {
namespace h=module_headings_cases;
namespace l=bit_editor_visual;
using C=BitUnit;
int gestures=0,frozen=0,models=0,format_checks=0,lens_checks=0,needles=0,owners=0;
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Bitcrush){g_overlay=Overlay::None;module_inspector_capture();
  g_overlay=Overlay::Editor;module_inspector_capture();bit_focus_select(C::P_BITS);capture();}
 ~Scene(){editor_cancel_input();param_lens_cancel();}
};
C& unit(Scene& s){return *static_cast<C*>(s.unit);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void drag(int x,int y,int delta){editor_touch_down(x,y);editor_touch_drag(x+delta,y);editor_touch_release();++gestures;}
bool has(const char* word){for(const auto& t:texts)if(t.text==word)return true;return false;}
void take(Scene& s,const std::string& id,const char* variant){
 s.capture();module_inspector_capture();h::take(s,id,variant);
 s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();
 module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;
 module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;
 const auto again=[&]{
  module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
  module_inspector_surface(false);{ModuleInspectorNoEngine hidden;bit_focus_draw(unit(s));
   if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);
 };
 again();{fader_lancet_cases::PoisonAfterCapture poison(s);again();}++frozen;
}
void set(Scene& s,int p,q15 v){s.set(p,v);}
void crunch(Scene& s){set(s,C::P_BITS,6554);set(s,C::P_RATE,18000);set(s,C::P_DRIVE,8192);
 set(s,C::P_TONE,28500);set(s,C::P_JITTER,3932);set(s,C::P_COMP,3277);
 set(s,C::P_DITHER,6554);set(s,C::P_MIX,27852);set(s,C::P_GAIN,19660);}
void broken(Scene& s){crunch(s);set(s,C::P_BITS,4369);set(s,C::P_RATE,16384);set(s,C::P_JITTER,12452);
 set(s,C::P_DRIFT,-1843);set(s,C::P_DROPOUT,3277);set(s,C::P_MIX,32767);set(s,C::P_DRIVE,16384);set(s,C::P_TONE,25000);}
void scenes(const std::string& prefix){
 {Scene s;CHECK(unit(s).modern()&&unit(s).num_params==C::P_COUNT);crunch(s);
  take(s,prefix+"bit_main","main8");}
 {Scene s;broken(s);bit_focus_select(C::P_DROPOUT);take(s,prefix+"bit_broken","clock-dropout");
  tap(218,14);bit_focus_select(C::P_DROP_TIME);take(s,prefix+"bit_setup","setup4");
  CHECK(editor_back());CHECK(!editor_back());}
 {Scene s;set(s,C::P_BITS,32767);set(s,C::P_RATE,32767);take(s,prefix+"bit_clean","neutral16");}
 {Scene s;auto& u=unit(s);u.set_modern(false);s.capture();CHECK(u.num_params==C::LEGACY_COUNT);
  take(s,prefix+"bit_legacy","legacy7");
  auto& scene=s.engine->scenes[0][0];scene.kind[s.slot]=UnitKind::Bitcrush;
  for(int p=0;p<C::P_COUNT;++p)scene.base[s.slot][p]=q15(4000+p);
  const int neighbor=(s.slot+1)%UNITS_PER_CHAIN;scene.kind[neighbor]=UnitKind::Bitcrush;
  scene.base[neighbor][C::P_GAIN]=12345;
  auto& other=s.engine->scenes[0][1];other.kind[s.slot]=UnitKind::Noise;other.base[s.slot][C::P_GAIN]=23456;
  std::array<q15,C::LEGACY_COUNT> before;for(int p=0;p<C::LEGACY_COUNT;++p)before[p]=u.params[p].base;
  g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;tap(183,14);CHECK(!u.modern());
  g_seq_lock_armed=false;tap(183,14);CHECK(u.modern()&&u.num_params==C::P_COUNT);
  C defaults;for(int p=0;p<C::LEGACY_COUNT;++p){CHECK(u.params[p].base==before[p]);CHECK(scene.base[s.slot][p]==q15(4000+p));}
  for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p){CHECK(u.params[p].base==defaults.params[p].base);
   CHECK(scene.base[s.slot][p]==defaults.params[p].base);}
  CHECK(scene.base[neighbor][C::P_GAIN]==12345&&other.base[s.slot][C::P_GAIN]==23456);
  take(s,prefix+"bit_extended","explicit-extend");}
 {Scene s;broken(s);unit(s).bypass=true;take(s,prefix+"bit_bypass","bypass-dry");}
 {Scene s;broken(s);s.engine->chains[0].mute=true;take(s,prefix+"bit_mute","mute-reference");}
 {Scene s;broken(s);set(s,C::P_MIX,0);take(s,prefix+"bit_dry","exact-dry");}
 {Scene s;broken(s);set(s,C::P_DROPOUT,32767);take(s,prefix+"bit_full_drop","full-wet-drop");}
 {Scene s;broken(s);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
  CHECK(s.engine->set_step_lock(0,4,s.slot,C::P_BITS,32767));take(s,prefix+"bit_lock","lock-reference");}
 {Scene s;crunch(s);s.path(false);take(s,prefix+"bit_route","six-slot-route");}
}
void model_oracles(){
 for(int test=0;test<8;++test){
  Scene s;crunch(s);if(test==1)broken(s);if(test==2)unit(s).bypass=true;
  if(test==3){set(s,C::P_BITS,32767);set(s,C::P_RATE,32767);set(s,C::P_DRIVE,0);set(s,C::P_TONE,32767);
   set(s,C::P_JITTER,0);set(s,C::P_COMP,0);set(s,C::P_DITHER,0);set(s,C::P_MIX,32767);set(s,C::P_GAIN,26214);}
  if(test==4){broken(s);set(s,C::P_MIX,0);}if(test==5){broken(s);set(s,C::P_DROPOUT,32767);}
  if(test==6){set(s,C::P_BITS,0);set(s,C::P_RATE,0);}if(test==7){broken(s);set(s,C::P_DRIFT,-32767);set(s,C::P_DROP_TIME,0);}
  q15 values[C::P_COUNT];for(int p=0;p<C::P_COUNT;++p)values[p]=unit(s).control_value(p);
  l::Plot actual;l::calculate(values,unit(s).bypass,actual);
  C reference;for(int p=0;p<C::P_COUNT;++p){reference.params[p].base=values[p];reference.params[p].snap();}
  reference.reset();reference.control_tick();
  std::array<q15,3072> output{};std::array<uint8_t,3072> events{};
  // Independent source expression and differently partitioned actual DSP calls.
  for(int n=0;n<3072;++n)output[n]=q15((128-std::abs((n%256)-128)*2)*160);
  if(!unit(s).bypass)for(int n=0;n<3072;n+=17)reference.process_with_events(output.data()+n,events.data()+n,std::min(17,3072-n));
  int captures=0,drops=0;
  for(int x=0;x<353;++x){int first=2048+x*1024/353,last=2048+(x+1)*1024/353;
   int lo=32767,hi=-32768;unsigned flags=0;for(int n=first;n<last;++n){lo=std::min(lo,int(output[n]));hi=std::max(hi,int(output[n]));flags|=events[n];}
   const auto& c=actual.out[x];CHECK(c.first==int(output[first])*36/32768&&c.last==int(output[last-1])*36/32768);
   CHECK(c.low==lo*36/32768&&c.high==hi*36/32768);
   CHECK(l::event(actual.capture,x)==bool(flags&1)&&l::event(actual.drop,x)==bool(flags&2));
   captures+=bool(flags&1);drops+=bool(flags&2);
  }
  if(test==2)CHECK(captures==0&&drops==0);else CHECK(captures>0);
  if(test==5){CHECK(drops==353);for(int n=2048;n<3072;++n)CHECK(output[n]==0);}
  if(test==2||test==3||test==4)for(int n=2048;n<3072;++n)CHECK(output[n]==l::source_sample(n));
  s.capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(true);
  {fader_lancet_cases::AudioHidden hidden;top_draw();}CHECK(!outside_rects&&!outside_lines&&!text_overlaps);
  CHECK(has("out --"));
  for(int x=0;x<353;++x){
   if(l::event(actual.capture,x))CHECK(pixels[190*400+34+x]==palette::menu_ghost());
   if(l::event(actual.drop,x))CHECK(pixels[209*400+34+x]==palette::spot());
   for(int y=113-actual.out[x].high;y<=113-actual.out[x].low;++y)CHECK(pixels[y*400+34+x]==palette::spot());
  }
  ++models;
 }
 // Legacy lower reference must still run the old7 DSP despite new default modern construction.
 {C legacy(false);q15 values[7];for(int p=0;p<7;++p)values[p]=legacy.params[p].effective;
  bit_reference::Plot plot;bit_reference::plot(values,false,plot);q15 samples[288];
  for(int n=0;n<288;++n)samples[n]=bit_reference::source_sample(n);
  legacy.process(samples,288);
  for(int n=0;n<288;++n)CHECK(plot.shaped[n]==bit_reference::y(samples[n]));
  ++models;}
}
void needle_oracles(){
 Scene s;crunch(s);constexpr int main[8]={C::P_BITS,C::P_RATE,C::P_DRIVE,C::P_TONE,C::P_JITTER,C::P_DROPOUT,C::P_MIX,C::P_GAIN};
 constexpr int extra[4]={C::P_DRIFT,C::P_COMP,C::P_DITHER,C::P_DROP_TIME};
 for(int page=0;page<2;++page){if(page)tap(218,14);
  for(int k=0;k<(page?4:8);++k){int p=page?extra[k]:main[k],x=page?14:l::kControlLeft[k%2],end=page?306:l::kControlRight[k%2];
   const int y=(page?l::kSetupY[k]:l::kMainY[k/2])+20;
   for(int mode=0;mode<3;++mode){const int lo=p==C::P_DRIFT?-32767:0;
    auto& q=unit(s).params[p];q.base=q15(mode==0?lo:mode==1?0:32767);q.target=q.effective=q15(mode==2?lo:27000);
    q.src=ModSource::SigilA;q.mod_depth=0;q.mod_enabled=true;s.capture();editor_style::capture();editor_gpu::begin();
    module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}CHECK(!text_overlaps&&!outside_rects&&!outside_lines);
    auto drawn=pixels;const auto map=[&](q15 v){return x+1+(int(v)-lo)*(end-x-2)/(32767-lo);};
    // Replaying EXACT shared helper over captured paper must change no pixel of the native lancet region.
    module_fader::horizontal(x+1,end-1,y,map(q.base),map(unit(s).control_value(p)),{true,false,false},p==C::P_DRIFT?map(0):-1);
    CHECK(pixels==drawn);++needles;
   }
  }
 }
}
void interactions(){
 Scene s;auto& u=unit(s);
 constexpr int pp[8]={C::P_BITS,C::P_RATE,C::P_DRIVE,C::P_TONE,C::P_JITTER,C::P_DROPOUT,C::P_MIX,C::P_GAIN};
 for(int k=0;k<8;++k){const int p=pp[k],x=(k%2)?222:64,y=l::kMainY[k/2]+20;set(s,p,16000);
  editor_touch_down(x,y);CHECK(u.params[p].base==16000&&bit_focus_selected()==p);
  editor_touch_drag(x+23,y);CHECK(u.params[p].base==q15(16000+23*32767/296));editor_touch_release();++gestures;
 }
 tap(218,14);constexpr int extra[4]={C::P_DRIFT,C::P_COMP,C::P_DITHER,C::P_DROP_TIME};
 for(int k=0;k<4;++k){const int p=extra[k],y=l::kSetupY[k]+20;set(s,p,16000);
  editor_touch_down(160,y);CHECK(u.params[p].base==16000&&bit_focus_selected()==p);
  editor_touch_drag(137,y);CHECK(u.params[p].base==q15(16000-23*(p==C::P_DRIFT?65534:32767)/296));editor_touch_release();++gestures;}
 CHECK(editor_back());
 // Each owner invalidation is tested without recapture and again after a recapture.
 for(int which=0;which<7;++which){set(s,C::P_BITS,16000);editor_touch_down(65,68);CHECK(g_drag==Drag::BitBits);
  const View view=g_view;const int slot=g_edit_slot,bank=s.engine->active_pattern;auto* eng=g_eng;
  C other;Unit* original=s.engine->chains[0].units[slot];
  if(which==0)g_view=View::Sequencer;
  if(which==1)g_edit_slot=(slot+1)%UNITS_PER_CHAIN;
  if(which==2)s.engine->request_pattern((bank+1)%NUM_PATTERNS,false);
  if(which==3)bit_focus_select(C::P_RATE);
  if(which==4)tap(218,14);
  if(which==5)s.engine->chains[0].units[slot]=&other;
  if(which==6)g_eng=nullptr;
  bit_focus_drag(u,95,68);CHECK(u.params[C::P_BITS].base==16000);++owners;
  g_eng=eng;g_view=view;g_edit_slot=slot;s.engine->chains[0].units[slot]=original;s.engine->request_pattern(bank,false);
  editor_touch_release();if(which==4)CHECK(editor_back());s.capture();
 }
 u.set_modern(false);s.capture();for(int p=0;p<7;++p){
  constexpr int xs[7]={50,152,256,45,118,193,272},ys[7]={65,65,65,213,213,213,213};
  set(s,p,p==C::P_DRIFT?0:13000);const q15 before=u.params[p].base;
  editor_touch_down(xs[p],ys[p]);CHECK(u.params[p].base==before);editor_touch_drag(xs[p]+7,ys[p]);
  CHECK(u.params[p].base!=before&&bit_focus_selected()==p);editor_touch_release();++gestures;}
}
void banks(){
 Scene s;auto&u=unit(s);set(s,C::P_DRIVE,0);set(s,C::P_DRIFT,0);
 g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<NUM_PATTERNS;++b){
  if(b<4){const int old=sequencer_edit_bank();extent_test_open(s);for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}
  CHECK(sequencer_edit_bank()==b);s.engine->request_pattern((b+3)%NUM_PATTERNS,false);
  auto&step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
  const int active=s.engine->active_pattern,active_count=s.engine->sequence_pattern(active).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  drag(65,114,30);CHECK(step.lock_count==1&&step.locks[0].slot==s.slot&&step.locks[0].param==C::P_DRIVE);
  CHECK(step.locks[0].value==q15(30*32767/296)&&u.params[C::P_DRIVE].base==0);
  tap(20,95);CHECK(step.lock_count==0);
  // Returning to the stored zero is an explicit source, never lock deletion.
  editor_touch_down(65,114);editor_touch_drag(95,114);editor_touch_drag(65,114);editor_touch_release();++gestures;
  CHECK(step.lock_count==1&&step.locks[0].value==0);tap(20,95);CHECK(step.lock_count==0);
  tap(218,14);drag(160,60,-30);CHECK(step.lock_count==1&&step.locks[0].param==C::P_DRIFT&&step.locks[0].value==q15(-30*65534/296));
  // Draft read: next relative edit starts from selected bank's lock, not base or active bank.
  drag(160,60,10);CHECK(step.locks[0].value==q15(-30*65534/296+10*65534/296));tap(20,42);CHECK(step.lock_count==0);
  CHECK(editor_back());
  editor_touch_down(65,114);g_seq_lock_step=6;editor_touch_drag(95,114);editor_touch_release();
  CHECK(step.lock_count==0);g_seq_lock_step=5;++owners;
  for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%UNITS_PER_CHAIN),uint8_t(i),1234};
  step.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;drag(65,114,30);
  CHECK(g_seq_lock_flash>0&&step.lock_count==SEQ_LOCKS&&u.params[C::P_DRIVE].base==0);
  CHECK(s.engine->sequence_pattern(active).steps[0][5].lock_count==active_count);
  g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
 }
 g_view=View::Chains;g_overlay=Overlay::None;
}
void score_formats(){
 Scene s;char text[32],expected[32];
 for(bool modern:{false,true}){
  C target(modern);unit(s).set_modern(!modern);s.capture(); // deliberately opposite visible version
  const auto version=seq_lock::variant(target);CHECK(version==unsigned(modern));
  for(int p=0;p<target.num_params;++p)for(q15 raw:{q15(-32767),q15(0),q15(16384),q15(32767)}){
   bit_readout::value_text(expected,sizeof expected,p,raw,modern);
   seq_lock::format_value(UnitKind::Bitcrush,version,p,raw,text,sizeof text);
   CHECK(std::strcmp(text,expected)==0);++format_checks;
  }
  if(modern){for(q15 raw:{q15(-1),q15(0),q15(1)}){
   bit_readout::value_text(text,sizeof text,C::P_DRIFT,raw,true);
   CHECK(raw?std::strstr(text,"<0.01hz")!=nullptr:std::strcmp(text,"0 / off")==0);++format_checks;}}
  CHECK(seq_lock::supports_offset(UnitKind::Bitcrush,version,C::P_RATE)==modern);
  CHECK(!seq_lock::supports_offset(UnitKind::Bitcrush,version,C::P_BITS));
  q15 raw=0;const int buckets=modern?16:13;
  for(int i=1;i<buckets;++i){raw=seq_lock::nudge_value(UnitKind::Bitcrush,version,C::P_BITS,raw,1,false);
   CHECK(seq_lock::detail::domain(UnitKind::Bitcrush,version,C::P_BITS,raw).index==i);++format_checks;}
  CHECK(raw==32767);
 }
}
void all_mod(const std::string& prefix){
 Scene s;set(s,C::P_DRIFT,-16384);
 for(bool modern:{true,false}){
  unit(s).set_modern(modern);s.capture();
  for(int p=0;p<unit(s).num_params;++p){
   bit_focus_select(p);s.capture();u32 down=KEY_L;
   CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
   take(s,prefix+(modern?"bit_mod_":"bit_legacy_mod_")+std::to_string(p),modern?"modern-mod":"legacy-mod");
   char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Bitcrush,p,unit(s).params[p].base);
   CHECK(text[0]&&std::strcmp(text,"--"));++format_checks;
   if(p==C::P_DRIFT){bit_focus_value_text(text,sizeof text,p,-32767);CHECK(text[0]=='-');}
   param_lens_touch_release();const auto before=unit(s).params[p].base;
   param_lens_touch_down(144,99);param_lens_touch_drag(146,99);param_lens_touch_release();
   CHECK(unit(s).params[p].base>=before);++gestures;
   param_lens_touch_down(50,186);param_lens_touch_release();CHECK(unit(s).params[p].src==ModSource::SigilA);
   param_lens_cancel();++lens_checks;
  }
 }
}
int run(const std::filesystem::path& dir,bool save){
 CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
 h::output=dir;h::capture_files=save;h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());
 h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 fader_lancet_cases::sample_fixture();CHECK(editor_style::init());
 for(const auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
  palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  scenes(prefix);model_oracles();needle_oracles();interactions();banks();score_formats();all_mod(prefix);}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("bit extended: %d duals %d gestures %d frozen %d model %d formatter %d mod-lenses %d exact needles %d owner cancellations; native host only\n",
  h::duals,gestures,frozen,models,format_checks,lens_checks,needles,owners);return 0;
}
} // namespace bit_extended_cases
int main(int argc,char** argv){try{CHECK(argc==3);return bit_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"bit extended: %s\n",e.what());return 1;}}
