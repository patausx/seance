// Current strain-only native host proof; never executes historical run() suites.
#include "../platform/3ds/strain_editor_visual.h"
#include "../platform/3ds/module_heading.h"
#include "../platform/3ds/strain_reference.h"
#include "../platform/3ds/strain_readout.h"
#include "../platform/3ds/sequencer_lock_format.h"
namespace strain_extended_cases {
namespace h=module_headings_cases;
namespace l=strain_editor_visual;
using C=StrainUnit;
int gestures=0,frozen=0,models=0,format_checks=0,lens_checks=0,needles=0,owners=0,lens_frozen=0;
constexpr int MAIN[8]={C::P_DRIVE,C::P_TEETH,C::P_BODY,C::P_BIAS,C::P_FEEDBACK,C::P_MOTION,C::P_TRIM,C::P_MIX};
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Strain){
  if(slot+1<UNITS_PER_CHAIN&&!engine->chains[0].units[slot+1])delete engine->replace_unit(0,slot+1,new VcaUnit);
  strain_focus_input(0,0,false);g_overlay=Overlay::None;module_inspector_capture();
  g_overlay=Overlay::Editor;module_inspector_capture();strain_focus_select(C::P_DRIVE);capture();}
 ~Scene(){strain_focus_input(0,0,false);editor_cancel_input();param_lens_cancel();}
};
C& unit(Scene& s){return *static_cast<C*>(s.unit);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void drag(int x,int y,int delta){editor_touch_down(x,y);editor_touch_drag(x+delta,y);editor_touch_release();++gestures;}
bool has(const char* word){for(const auto& t:texts)if(t.text==word)return true;return false;}
void take(Scene& s,const std::string& id,const char* variant){
 s.capture();module_inspector_capture();h::take(s,id,variant);
 s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();const auto calls=strain_process_calls;
 module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;
 module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;
 const auto again=[&]{
  module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
  module_inspector_surface(false);{ModuleInspectorNoEngine hidden;strain_focus_draw(unit(s));
   if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);
 };
 again();{fader_lancet_cases::PoisonAfterCapture poison(s);again();}
 CHECK(strain_process_calls==calls);++frozen;
}
void set(Scene& s,int p,q15 v){s.set(p,v);}
void rounded(Scene& s){const int pp[6]={15000,0,15000,12000,9000,24500};for(int p=0;p<6;++p)set(s,p,pp[p]);}
void scenes(const std::string& prefix){
 {Scene s;CHECK(unit(s).modern()&&unit(s).num_params==C::P_COUNT);take(s,prefix+"strain_default","default9");}
 {Scene s;rounded(s);take(s,prefix+"strain_main","main8");set(s,C::P_TEETH,32767);strain_focus_select(C::P_TEETH);take(s,prefix+"strain_teeth","teeth");}
 {Scene s;rounded(s);set(s,C::P_MOTION,0);strain_focus_select(C::P_MOTION);take(s,prefix+"strain_motion0","motion-off");}
 {Scene s;rounded(s);set(s,C::P_TRIM,0);strain_focus_select(C::P_TRIM);take(s,prefix+"strain_trim","minus18db");}
 {Scene s;rounded(s);tap(218,14);strain_focus_select(C::P_RATE);take(s,prefix+"strain_setup","setup1");
  set(s,C::P_RATE,32767);take(s,prefix+"strain_fast_setup","8hz");CHECK(editor_back());CHECK(!editor_back());}
 {Scene s;auto& u=unit(s);u.set_modern(false);s.capture();CHECK(u.num_params==C::LEGACY_COUNT);rounded(s);
  take(s,prefix+"strain_legacy","legacy6");
  auto& scene=s.engine->scenes[0][0];scene.kind[s.slot]=UnitKind::Strain;
  for(int p=0;p<C::P_COUNT;++p)scene.base[s.slot][p]=q15(4000+p);
  const int neighbor=(s.slot+1)%UNITS_PER_CHAIN;scene.kind[neighbor]=UnitKind::Strain;scene.base[neighbor][C::P_RATE]=12345;
  auto& other=s.engine->scenes[0][1];other.kind[s.slot]=UnitKind::Noise;other.base[s.slot][C::P_RATE]=23456;
  std::array<q15,C::LEGACY_COUNT> before;for(int p=0;p<C::LEGACY_COUNT;++p)before[p]=u.params[p].base;
  g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;tap(183,14);CHECK(!u.modern());
  g_seq_lock_armed=false;tap(183,14);CHECK(u.modern()&&u.num_params==C::P_COUNT);
  C defaults;for(int p=0;p<C::LEGACY_COUNT;++p){CHECK(u.params[p].base==before[p]);CHECK(scene.base[s.slot][p]==q15(4000+p));}
  for(int p=C::LEGACY_COUNT;p<C::P_COUNT;++p){CHECK(u.params[p].base==defaults.params[p].base);CHECK(scene.base[s.slot][p]==defaults.params[p].base);}
  CHECK(scene.base[neighbor][C::P_RATE]==12345&&other.base[s.slot][C::P_RATE]==23456);
  take(s,prefix+"strain_extended","explicit-extend");}
 {Scene s;rounded(s);unit(s).bypass=true;take(s,prefix+"strain_bypass","bypass-dry");}
 {Scene s;rounded(s);s.engine->chains[0].mute=true;take(s,prefix+"strain_mute","mute-reference");}
 {Scene s;rounded(s);set(s,C::P_MIX,0);take(s,prefix+"strain_dry","exact-dry");}
 {Scene s;rounded(s);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
  CHECK(s.engine->set_step_lock(0,4,s.slot,C::P_TEETH,32767));take(s,prefix+"strain_lock","lock-reference");}
 {Scene s;rounded(s);s.path(false);take(s,prefix+"strain_route","six-slot-route");}
 for(bool holes:{false,true}){Scene s;rounded(s);s.path(holes);
  UnitKind longest=UnitKind::Strain;
  for(const auto& entry:fader_lancet_cases::catalogue)
   if(ui::text_w(editor_style::module_name(entry.kind))>ui::text_w(editor_style::module_name(longest)))longest=entry.kind;
  for(int slot=0;slot<UNITS_PER_CHAIN;++slot)if(slot!=s.slot&&s.engine->chains[0].units[slot]){
   auto* replacement=make_unit(longest);replacement->bypass=slot%2==0;delete s.engine->replace_unit(0,slot,replacement);}
  take(s,prefix+(holes?"strain_holes_route":"strain_long_route"),holes?"long-route-holes":"longest-six");
  module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}
  std::vector<TextRecord> route;for(const auto& t:texts)if(t.call_y==module_heading::kRouteY)route.push_back(t);
  int x=14;size_t at=0;
  for(int slot=0;slot<UNITS_PER_CHAIN;++slot)if(auto* current=s.engine->chains[0].units[slot]){
   if(at){CHECK(at<route.size()&&route[at].text=="/");++at;x+=14;}
   const char* name=editor_style::module_name(current->kind);const int w=ui::text_w(name);
   CHECK(at<route.size()&&route[at].text==name&&route[at].call_x==x);++at;
   if(slot==s.slot)for(int xx=x;xx<=x+w-2;++xx)CHECK(pixels[module_heading::kRouteUnderlineY*400+xx]==palette::spot());
   x+=w+8;
  }
  CHECK(at==route.size()||(at+1==route.size()&&route[at].text=="drive 4.20x"&&route[at].call_x>=x));
 }
}
void frozen_lens(){
 Scene s;rounded(s);s.capture();editor_style::capture();editor_gpu::begin();
 module_inspector_surface(true);CHECK(strain_focus_draw_top());const auto top=pixels;
 module_inspector_surface(false);strain_focus_draw(unit(s));const auto bottom=pixels;
 u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)); // no new frame yet
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(strain_focus_draw_top());}CHECK(pixels==top);
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;strain_focus_draw(unit(s));}CHECK(pixels==bottom);
 const auto calls=strain_process_calls;set(s,C::P_TEETH,32767);s.capture();CHECK(strain_process_calls==calls);
 module_inspector_surface(false);strain_focus_draw(unit(s));const auto shell=pixels;
 param_lens_cancel(); // still no new captured frame: owned surface remains a MOD shell
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;strain_focus_draw(unit(s));}CHECK(pixels==shell);
 CHECK(!strain_focus_draw_top());s.capture();CHECK(strain_process_calls>calls);++lens_frozen;
}
q15 independent_source(int n){
 if(n>=512)return 0;
 const double pi=std::acos(-1.0);
 const double envelope=n<64?.5-.5*std::cos(pi*n/64):.5+.5*std::cos(pi*(n-64)/447);
 return q15(std::lround(24575*envelope*std::sin(2*pi*n/128)));
}
int independent_y(q15 v){return int(std::lround(double(v)*34/32768));}
void model_oracles(){
 for(int test=0;test<9;++test){Scene s;rounded(s);
  if(test==1)set(s,C::P_TEETH,32767);
  if(test==2)unit(s).bypass=true;
  if(test==3)set(s,C::P_MOTION,0);
  if(test==4)set(s,C::P_TRIM,0);
  if(test==5)set(s,C::P_MIX,0);
  if(test==6){set(s,C::P_RATE,32767);set(s,C::P_BIAS,-32767);}if(test==7)set(s,C::P_BODY,0);
  if(test==8){set(s,C::P_BODY,32767);set(s,C::P_FEEDBACK,32767);set(s,C::P_TRIM,32767);}
  q15 values[C::P_COUNT];for(int p=0;p<C::P_COUNT;++p)values[p]=unit(s).control_value(p);
  l::Plot actual;l::calculate(values,unit(s).bypass,actual);
  C reference(true);for(int p=0;p<C::P_COUNT;++p){reference.params[p].base=values[p];reference.params[p].snap();}reference.reset();
  std::array<q15,1024> output{};
  // Independently regenerate approved input; preserve 1khz ticks, vary process partitions.
  for(int n=0;n<1024;++n){output[n]=independent_source(n);CHECK(output[n]==l::source_sample(n));}
  if(!unit(s).bypass)for(int n=0;n<1024;n+=32){reference.control_tick();for(int k=0;k<32;k+=7)reference.process(output.data()+n+k,std::min(7,32-k));}
  for(int x=0;x<353;++x){int first=x*1024/353,last=(x+1)*1024/353;int lo=32767,hi=-32768;
   for(int n=first;n<last;++n){lo=std::min(lo,int(output[n]));hi=std::max(hi,int(output[n]));}
   const auto& c=actual.out[x];CHECK(c.first==independent_y(output[first])&&c.last==independent_y(output[last-1]));
   CHECK(c.low==independent_y(q15(lo))&&c.high==independent_y(q15(hi)));
  }
  if(test==2||test==5)for(int n=0;n<1024;++n)CHECK(output[n]==independent_source(n));
  for(int i=0;i<177;++i){const uint32_t phase=uint32_t((uint64_t(i)<<32)/176);
   const int motion=C::model_motion(values[C::P_BODY],values[C::P_MOTION],phase,0);
   const int coefficient=std::max(0,std::min(32767,int(values[C::P_BODY])+motion));
   CHECK(actual.motion[i]==213-int(std::lround(double(coefficient)*27/32767)));
   if(test==3)CHECK(actual.motion[i]==actual.body);
  }
  s.capture();const auto calls=strain_process_calls;s.capture();CHECK(strain_process_calls==calls);
  editor_style::capture();editor_gpu::begin();module_inspector_surface(true);
  {fader_lancet_cases::AudioHidden hidden;top_draw();}CHECK(!outside_rects&&!outside_lines&&!text_overlaps);
  CHECK(has("body motion"));for(int x=0;x<353;++x)for(int y=109-actual.out[x].high;y<=109-actual.out[x].low;++y)
   CHECK(pixels[y*400+34+x]==palette::spot());
  ++models;
 }
 // Explicit legacy constructor is required even though the factory now defaults modern.
 {C legacy(false);q15 values[6];for(int p=0;p<6;++p)values[p]=legacy.params[p].effective;
  strain_reference::Plot plot;strain_reference::plot(values,false,plot);legacy.reset();
  std::array<q15,1024> out{};for(int n=0;n<1024;n+=32){for(int i=0;i<32;++i)out[n+i]=independent_source(n+i);legacy.control_tick();legacy.process(out.data()+n,32);}
  for(int x=0;x<243;++x){int lo=255,hi=0,first=-1,last=-1;for(int n=0;n<1024;++n)if(n*242/1023==x){
   const int y=strain_reference::y(out[n]);if(first<0)first=y;last=y;lo=std::min(lo,y);hi=std::max(hi,y);}
   CHECK(plot.shaped.first[x]==first&&plot.shaped.last[x]==last&&plot.shaped.lo[x]==lo&&plot.shaped.hi[x]==hi);}
  ++models;}
}
void needle_oracles(){
 Scene s;rounded(s);for(int page=0;page<2;++page){if(page)tap(218,14);
  for(int k=0;k<(page?1:8);++k){const int p=page?C::P_RATE:MAIN[k],x=page?14:l::kControlLeft[k%2],end=page?306:l::kControlRight[k%2];
   const int y=(page?l::kSetupY:l::kMainY[k/2])+23;
   for(int mode=0;mode<3;++mode){const int lo=p==C::P_BIAS?-32767:0;auto& q=unit(s).params[p];
    q.base=q15(mode==0?lo:mode==1?0:32767);q.target=q.effective=q15(mode==2?lo:27000);
    q.src=ModSource::SigilA;q.mod_depth=0;q.mod_enabled=true;s.capture();editor_style::capture();editor_gpu::begin();
    module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}CHECK(!text_overlaps&&!outside_rects&&!outside_lines);
    const auto drawn=pixels;const auto map=[&](q15 v){return x+(int(v)-lo)*(end-x)/(32767-lo);};
    module_fader::horizontal(x,end,y,map(q.base),map(unit(s).control_value(p)),{true,false,false},p==C::P_BIAS?map(0):p==C::P_TRIM?map(C::DEFAULT_TRIM):-1);
    CHECK(pixels==drawn);++needles;
   }
  }
 }
}
void interactions(){
 Scene s;auto& u=unit(s);
 for(int k=0;k<8;++k){const int p=MAIN[k],x=k%2?222:64,y=l::kMainY[k/2]+23;set(s,p,16000);
  editor_touch_down(x,y);CHECK(u.params[p].base==16000&&strain_focus_selected()==p);
  editor_touch_drag(x+23,y);CHECK(u.params[p].base==q15(16000+23*(p==C::P_BIAS?65534:32767)/296));editor_touch_release();++gestures;}
 tap(218,14);set(s,C::P_RATE,16000);drag(160,102,-23);CHECK(u.params[C::P_RATE].base==q15(16000-23*32767/296));CHECK(editor_back());
 set(s,C::P_BIAS,11);strain_focus_input(KEY_R,KEY_R,false);drag(230,112,-11);CHECK(u.params[C::P_BIAS].base==0);
 drag(230,112,-1);CHECK(u.params[C::P_BIAS].base==-1);strain_focus_input(0,0,false);
 set(s,C::P_BIAS,100);drag(230,112,-1);CHECK(u.params[C::P_BIAS].base==0);
 // Engine, selected unit/param/view/page/bank/mode and blocked/fine contexts all own input.
 for(int which=0;which<11;++which){set(s,C::P_DRIVE,16000);editor_touch_down(65,66);CHECK(g_drag==Drag::StrainParam);
  const View view=g_view;const int slot=g_edit_slot,b=s.engine->active_pattern;auto* eng=g_eng;C other;Unit* original=s.engine->chains[0].units[slot];
  if(which==0)g_view=View::Sequencer;
  if(which==1)g_edit_slot=(slot+1)%UNITS_PER_CHAIN;
  if(which==2)s.engine->request_pattern((b+1)%NUM_PATTERNS,false);
  if(which==3)strain_focus_select(C::P_RATE);
  if(which==4)tap(218,14);
  if(which==5)s.engine->chains[0].units[slot]=&other;
  if(which==6)g_eng=nullptr;
  if(which==7)u.set_modern(false);
  if(which==8)strain_focus_input(0,0,true);
  if(which==9)strain_focus_input(KEY_R,KEY_R,false);
  if(which==10)g_seq_lock_armed=true;
  strain_focus_drag(u,95,66);CHECK(u.params[C::P_DRIVE].base==16000);++owners;
  g_eng=eng;g_view=view;g_edit_slot=slot;s.engine->chains[0].units[slot]=original;s.engine->request_pattern(b,false);
  g_seq_lock_armed=false;strain_focus_input(0,0,false);u.set_modern(true);editor_touch_release();if(which==4)CHECK(editor_back());s.capture();
 }
 u.set_modern(false);s.capture();for(int p=0;p<6;++p){constexpr int xs[6]={60,115,40,215,192,268},ys[6]={65,214,214,65,214,214};
  set(s,p,p==C::P_BIAS?0:13000);const q15 before=u.params[p].base;editor_touch_down(xs[p],ys[p]);CHECK(u.params[p].base==before);
  editor_touch_drag(xs[p]+7,ys[p]);CHECK(u.params[p].base!=before&&strain_focus_selected()==p);editor_touch_release();++gestures;}
}
void banks(){
 Scene s;auto&u=unit(s);set(s,C::P_MOTION,0);set(s,C::P_BIAS,0);set(s,C::P_RATE,0);
 g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<NUM_PATTERNS;++b){
  if(b<4){const int old=sequencer_edit_bank();extent_test_open(s);for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}
  CHECK(sequencer_edit_bank()==b);s.engine->request_pattern((b+3)%NUM_PATTERNS,false);
  auto&step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
  const int active=s.engine->active_pattern,active_count=s.engine->sequence_pattern(active).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  // Every stored index, including setup RATE and bipolar BIAS, in every edited bank.
  for(int k=0;k<9;++k){const int p=k==8?C::P_RATE:MAIN[k];const q15 base=u.params[p].base;
   set(s,p,1234);if(k==8)tap(218,14);
   const int x=k==8?160:k%2?230:65,y=k==8?102:l::kMainY[k/2]+23;
   drag(x,y,20);CHECK(step.lock_count==1&&step.locks[0].param==p&&step.locks[0].slot==s.slot);
   CHECK(step.locks[0].value==q15(1234+20*(p==C::P_BIAS?65534:32767)/296)&&u.params[p].base==1234);
   tap(k==8?20:k%2?180:20,k==8?81:l::kMainY[k/2]+2);CHECK(step.lock_count==0);
   if(k==8)CHECK(editor_back());
   set(s,p,base);
  }
  drag(230,158,30);CHECK(step.lock_count==1&&step.locks[0].slot==s.slot&&step.locks[0].param==C::P_MOTION);
  CHECK(step.locks[0].value==q15(30*32767/296)&&u.params[C::P_MOTION].base==0);tap(180,137);CHECK(step.lock_count==0);
  editor_touch_down(230,158);editor_touch_drag(260,158);editor_touch_drag(230,158);editor_touch_release();++gestures;
  CHECK(step.lock_count==1&&step.locks[0].value==0);tap(180,137);CHECK(step.lock_count==0);
  drag(230,112,-30);CHECK(step.lock_count==1&&step.locks[0].param==C::P_BIAS&&step.locks[0].value==q15(-30*65534/296));
  drag(230,112,10);CHECK(step.locks[0].value==q15(-30*65534/296+10*65534/296));tap(180,91);CHECK(step.lock_count==0);
  tap(218,14);drag(160,102,30);CHECK(step.lock_count==1&&step.locks[0].param==C::P_RATE);
  tap(20,81);CHECK(step.lock_count==0);CHECK(editor_back());
  editor_touch_down(230,158);g_seq_lock_step=6;editor_touch_drag(260,158);editor_touch_release();CHECK(step.lock_count==0);g_seq_lock_step=5;++owners;
  for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%UNITS_PER_CHAIN),uint8_t(i),1234};
  step.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;drag(230,158,30);
  CHECK(g_seq_lock_flash>0&&step.lock_count==SEQ_LOCKS&&u.params[C::P_MOTION].base==0);
  step.locks[0]={uint8_t(s.slot),uint8_t(C::P_MOTION),1234};
  drag(230,158,30);CHECK(step.lock_count==SEQ_LOCKS&&step.locks[0].value==q15(1234+30*32767/296));
  CHECK(s.engine->sequence_pattern(active).steps[0][5].lock_count==active_count);
  g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
 }
 g_view=View::Chains;g_overlay=Overlay::None;
}
void formats(){
 Scene s;char text[32],expected[32];
 for(bool modern:{false,true}){C target(modern);unit(s).set_modern(!modern);s.capture();
  const auto version=seq_lock::variant(target);CHECK(version==unsigned(modern));
  for(int p=0;p<target.num_params;++p)for(q15 raw:{q15(-32767),q15(0),q15(16384),q15(32767)}){
   strain_readout::value_text(expected,sizeof expected,p,raw,modern);seq_lock::format_value(UnitKind::Strain,version,p,raw,text,sizeof text);
   CHECK(std::strcmp(text,expected)==0);++format_checks;}
 }
 strain_readout::value_text(text,sizeof text,C::P_RATE,C::DEFAULT_RATE,true);CHECK(std::strcmp(text,"0.032hz")==0);
 strain_readout::cycle_text(text,sizeof text,C::DEFAULT_RATE);CHECK(std::strcmp(text,"31.25s")==0);
 strain_readout::cycle_text(text,sizeof text,32767);CHECK(std::strcmp(text,"0.125s")==0);++format_checks;
 strain_readout::value_text(text,sizeof text,C::P_TRIM,C::DEFAULT_TRIM,true);CHECK(std::strcmp(text,"0.0db")==0);
 strain_readout::value_text(text,sizeof text,C::P_TRIM,0,true);CHECK(std::strcmp(text,"-18.0db")==0);
 strain_readout::value_text(text,sizeof text,C::P_TRIM,32767,true);CHECK(std::strcmp(text,"+6.0db")==0);format_checks+=5;
}
void all_mod(const std::string& prefix){
 Scene s;set(s,C::P_BIAS,-16384);
 for(bool modern:{true,false}){unit(s).set_modern(modern);s.capture();
  for(int p=0;p<unit(s).num_params;++p){strain_focus_select(p);s.capture();u32 down=KEY_L;
   CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
   take(s,prefix+(modern?"strain_mod_":"strain_legacy_mod_")+std::to_string(p),modern?"modern-mod":"legacy-mod");
   char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Strain,p,unit(s).params[p].base);CHECK(text[0]&&std::strcmp(text,"--"));++format_checks;
   if(p==C::P_BIAS){strain_focus_value_text(text,sizeof text,p,-32767);CHECK(text[0]=='-');}
   param_lens_touch_release();const auto before=unit(s).params[p].base;
   param_lens_touch_down(144,99);param_lens_touch_drag(146,99);param_lens_touch_release();CHECK(unit(s).params[p].base>=before);++gestures;
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
 for(const auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  scenes(prefix);model_oracles();needle_oracles();interactions();banks();formats();all_mod(prefix);frozen_lens();}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("strain extended: %d duals %d gestures %d frozen %d models %d formatter %d mod-lenses %d exact needles %d owner cancellations %d frozen-lens transitions; native host only\n",
  h::duals,gestures,frozen,models,format_checks,lens_checks,needles,owners,lens_frozen);return 0;
}
} // namespace strain_extended_cases
int main(int argc,char**argv){try{CHECK(argc==3);return strain_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"strain extended: %s\n",e.what());return 1;}}
