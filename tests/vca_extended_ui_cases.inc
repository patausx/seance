// Focused current native VCA UI; does not execute inherited historical suites.
#include "../core/engine/vca_timing.h"
#include "../platform/3ds/module_heading.h"
#include "../platform/3ds/sequencer_lock_format.h"
namespace vca_extended_cases {
namespace h=module_headings_cases;
namespace vt=seance::vca_timing;
using V=VcaUnit;
int gestures=0,frozen=0,models=0,formats=0,lenses=0,needles=0,owners=0;
constexpr int main_params[7]={V::P_ATTACK,V::P_HOLD,V::P_DECAY,V::P_GAIN,V::P_A_CURVE,V::P_D_CURVE,V::P_FLOOR};
constexpr int ys[4]={44,90,136,182};
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Vca){vca_focus_input(0,0,false);g_overlay=Overlay::None;module_inspector_capture();g_overlay=Overlay::Editor;module_inspector_capture();vca_focus_select(V::P_DECAY);capture();}
 ~Scene(){vca_focus_input(0,0,false);editor_cancel_input();param_lens_cancel();}
};
V& unit(Scene& s){return *static_cast<V*>(s.unit);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void drag(int x,int y,int dx){editor_touch_down(x,y);editor_touch_drag(x+dx,y);editor_touch_release();++gestures;}
void advance(V& u,int n){q15 block[128];while(n){const int k=std::min(n,128);std::fill(std::begin(block),std::end(block),10000);u.process(block,k);n-=k;}}
bool has(const char* word){for(const auto& t:texts)if(t.text==word)return true;return false;}
void seed(Scene& s,bool duck=false){
 s.set(V::P_GAIN,26214);s.set(V::P_ATTACK,vca_envelope::time_value(240*32));s.set(V::P_HOLD,vca_envelope::time_value(180*32,true));
 s.set(V::P_DECAY,vca_envelope::time_value(780*32));s.set(V::P_FLOOR,6553);s.set(V::P_A_CURVE,8192);s.set(V::P_D_CURVE,-18022);s.set(V::P_DUCK,duck?32767:0);
 unit(s).reset();unit(s).trigger();advance(unit(s),650*32);
}
void take(Scene& s,const std::string& id,const char* variant){
 s.capture();module_inspector_capture();h::take(s,id,variant);
 s.capture();module_inspector_capture();editor_style::capture();editor_gpu::begin();
 module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;
 module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;
 auto repeat=[&]{module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
  module_inspector_surface(false);{ModuleInspectorNoEngine hidden;vca_focus_draw(unit(s));if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);};
 repeat();{fader_lancet_cases::PoisonAfterCapture poison(s);repeat();}++frozen;
}
void captures(const std::string& prefix){
 {Scene s;CHECK(unit(s).modern()&&unit(s).num_params==11);take(s,prefix+"vca_default","default11");}
 {Scene s;seed(s);take(s,prefix+"vca_main","from-rest-actual-dot");tap(218,14);take(s,prefix+"vca_setup","setup-free");CHECK(editor_back());CHECK(!editor_back());}
 {Scene s;seed(s,true);unit(s).set_clock_bpm_q16(120*65536);s.set(V::P_SYNC,32767);
  s.set(V::P_ATTACK,vt::division_value(10));s.set(V::P_HOLD,vt::division_value(10,true));s.set(V::P_DECAY,vt::division_value(13));s.set(V::P_RUN,32767);
  unit(s).reset();unit(s).trigger();advance(unit(s),375*32);take(s,prefix+"vca_sync_duck","sync-duck-cycle");tap(218,14);take(s,prefix+"vca_sync_setup","setup-sync-cycle");}
 {Scene s;seed(s);s.set(V::P_HOLD,0);take(s,prefix+"vca_hold0","no-hold");}
 for(int edge=0;edge<3;++edge){Scene s;seed(s);for(int p=0;p<8;++p)s.set(p,edge==0?0:edge==1?32767:((p==V::P_A_CURVE||p==V::P_D_CURVE)?-32767:(p%2?0:32767)));
  unit(s).trigger();advance(unit(s),113);take(s,prefix+"vca_extreme"+std::to_string(edge),"extreme");}
 {Scene s;seed(s);unit(s).bypass=true;take(s,prefix+"vca_bypass","bypassed");}
 {Scene s;seed(s);s.engine->chains[0].mute=true;take(s,prefix+"vca_mute","muted");}
 {Scene s;seed(s);s.set(V::P_RUN,32767);unit(s).reset();advance(unit(s),1);tap(218,14);take(s,prefix+"vca_wait","waiting-for-trigger");}
 {Scene s;seed(s);s.set(V::P_RETRIGGER,vt::retrigger_value(1));tap(218,14);take(s,prefix+"vca_reset","reset-crossfade");s.set(V::P_RETRIGGER,vt::retrigger_value(2));take(s,prefix+"vca_ignore","ignore-active");}
 {Scene s;seed(s);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
  CHECK(s.engine->set_step_lock(0,4,s.slot,V::P_DECAY,0));take(s,prefix+"vca_lock","lock-shape-actual-dot");}
 {Scene s;seed(s);unit(s).set_flexible(true);s.capture();CHECK(!unit(s).modern()&&unit(s).num_params==8);
  take(s,prefix+"vca_ahd","preserved-ahd8");tap(218,14);take(s,prefix+"vca_ahd_setup","explicit-extend");
  auto& sc=s.engine->scenes[0][0];sc.kind[s.slot]=UnitKind::Vca;for(int p=0;p<11;++p)sc.base[s.slot][p]=q15(7000+p);
  const int neighbor=(s.slot+1)%UNITS_PER_CHAIN;sc.kind[neighbor]=UnitKind::Vca;sc.base[neighbor][V::P_RUN]=12000;
  auto& other=s.engine->scenes[0][1];other.kind[s.slot]=UnitKind::Noise;other.base[s.slot][V::P_RUN]=22000;
  std::array<Param,8> original;for(int p=0;p<8;++p)original[p]=unit(s).params[p];const auto opening=unit(s).visual_envelope();const auto stage=unit(s).visual_stage();
  for(int p=8;p<11;++p){unit(s).params[p].base=30000;unit(s).params[p].src=ModSource::SigilB;unit(s).params[p].mod_depth=15000;}
  g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;tap(245,136);CHECK(!unit(s).modern());g_seq_lock_armed=false;
  tap(245,136);CHECK(unit(s).modern()&&unit(s).num_params==11);CHECK(unit(s).visual_stage()==stage&&unit(s).visual_envelope()==opening);
  for(int p=0;p<8;++p){CHECK(unit(s).params[p].base==original[p].base);CHECK(sc.base[s.slot][p]==7000+p);}
  V defaults;for(int p=8;p<11;++p){CHECK(unit(s).params[p].base==defaults.params[p].base&&unit(s).params[p].src==ModSource::None);CHECK(sc.base[s.slot][p]==defaults.params[p].base);}
  CHECK(sc.base[neighbor][V::P_RUN]==12000&&other.base[s.slot][V::P_RUN]==22000);take(s,prefix+"vca_extended","explicit-upgrade");}
 {Scene s;unit(s).set_flexible(false);s.capture();take(s,prefix+"vca_ad","preserved-ad3");tap(218,14);CHECK(unit(s).num_params==3);}
 {Scene s;seed(s);s.path(false);take(s,prefix+"vca_route","route-window");}
 {Scene s;seed(s);s.path(true);take(s,prefix+"vca_holes","route-holes");}
}
void controls(){
 Scene s;auto& u=unit(s);
 for(int k=0;k<7;++k){const int p=main_params[k],x=k%2?230:70,y=ys[k/2]+23;const bool bi=p==V::P_A_CURVE||p==V::P_D_CURVE;
  for(int edge:{bi?-32767:0,0,32767}){s.set(p,q15(edge));editor_touch_down(x,y);CHECK(u.params[p].base==edge);editor_touch_drag(x,y);CHECK(u.params[p].base==edge);
   editor_touch_drag(x+12,y);CHECK(u.params[p].base==std::min(32767,edge+12*(bi?65534:32767)/296));editor_touch_release();++gestures;}
  s.set(p,0);drag(x,y,296);CHECK(u.params[p].base==32767);drag(x,y,-592);CHECK(u.params[p].base==(bi?-32767:0));
  s.set(p,10000);vca_focus_input(KEY_R,KEY_R,false);drag(x,y,7);CHECK(u.params[p].base==10007);vca_focus_input(0,0,false);
 }
 for(int p:{V::P_A_CURVE,V::P_D_CURVE}){const int x=p==V::P_A_CURVE?70:230;s.set(p,-100);drag(x,159,1);CHECK(u.params[p].base==0);drag(x,159,1);CHECK(u.params[p].base>0);}
 for(int p:{V::P_A_CURVE,V::P_D_CURVE}){const int x=p==V::P_A_CURVE?70:230;
  for(int sign:{-1,1}){s.set(p,q15(sign*3));vca_focus_input(KEY_R,KEY_R,false);editor_touch_down(x,159);
   for(int dx=1;dx<=6;++dx){editor_touch_drag(x-sign*dx,159);CHECK(u.params[p].base==sign*(3-dx));}
   editor_touch_release();vca_focus_input(0,0,false);++gestures;
  }
 }
 tap(280,205);CHECK(u.params[V::P_DUCK].base==32767);tap(180,205);CHECK(u.params[V::P_DUCK].base==0);
 const q15 a=u.params[V::P_ATTACK].base,hld=u.params[V::P_HOLD].base,d=u.params[V::P_DECAY].base;
 tap(218,14);tap(282,45);CHECK(u.params[V::P_SYNC].base==32767);CHECK(u.params[V::P_ATTACK].base==a&&u.params[V::P_HOLD].base==hld&&u.params[V::P_DECAY].base==d);
 tap(190,105);CHECK(vt::retrigger(u.params[V::P_RETRIGGER].base)==1);tap(278,105);CHECK(vt::retrigger(u.params[V::P_RETRIGGER].base)==2);
 tap(280,165);CHECK(u.params[V::P_RUN].base==32767);CHECK(editor_back());
 for(int reason=0;reason<13;++reason){s.set(V::P_ATTACK,10000);s.set(V::P_SYNC,0);s.capture();editor_touch_down(70,67);
  Engine* old_engine=g_eng;const auto view=g_view;const int slot=g_edit_slot,bank=s.engine->active_pattern;Unit* orig=s.engine->chains[0].units[slot];
  if(reason==0)g_eng=nullptr;
  if(reason==1)g_view=View::Performance;
  if(reason==2)++g_edit_slot;
  if(reason==3)s.engine->request_pattern((bank+1)%8,false);
  if(reason==4)tap(218,14);
  if(reason==5)vca_focus_select(V::P_GAIN);
  if(reason==6)vca_focus_input(KEY_R,KEY_R,false);
  if(reason==7)g_seq_lock_armed=true;
  if(reason==8)s.engine->chains[0].units[slot]=nullptr;
  if(reason==9)u.set_flexible(true);
  if(reason==10)s.set(V::P_SYNC,32767);
  if(reason==11)g_overlay=Overlay::None;
  if(reason==12)vca_focus_input(0,0,true);
  vca_focus_drag(u,100,67);CHECK(u.params[V::P_ATTACK].base==10000);++owners;
  g_eng=old_engine;g_view=view;g_edit_slot=slot;s.engine->chains[0].units[slot]=orig;s.engine->request_pattern(bank,false);g_overlay=Overlay::Editor;
  g_seq_lock_armed=false;vca_focus_input(0,0,false);u.set_modern(true);editor_touch_release();if(reason==4)CHECK(editor_back());s.capture();
 }
 // Old AD remains quadratic/vertical-gain editor, without an extend action.
 u.set_flexible(false);s.capture();for(int p=0;p<3;++p){s.set(p,12000);const int x=p==0?295:p==1?30:175,y=p==0?110:184;
  editor_touch_down(x,y);CHECK(u.params[p].base==12000);editor_touch_drag(x+5,y-5);CHECK(u.params[p].base==12000+5*32767/(p==0?56:160));editor_touch_release();++gestures;}
}
void banks(){
 Scene s;auto& u=unit(s);g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<NUM_PATTERNS;++b){
  if(b<4){const int old=sequencer_edit_bank();extent_test_open(s);for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}
  CHECK(sequencer_edit_bank()==b);s.engine->request_pattern((b+3)%NUM_PATTERNS,false);
  auto& step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
  const int active=s.engine->active_pattern,active_count=s.engine->sequence_pattern(active).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  for(int k=0;k<7;++k){const int p=main_params[k],x=k%2?230:70,y=ys[k/2]+23;const bool bi=p==V::P_A_CURVE||p==V::P_D_CURVE;
   s.set(p,0);drag(x,y,0);CHECK(step.lock_count==1&&step.locks[0].param==p&&step.locks[0].value==0&&u.params[p].base==0);
   drag(x,y,20);CHECK(step.locks[0].value==20*(bi?65534:32767)/296);tap(k%2?176:18,ys[k/2]+2);CHECK(step.lock_count==0);
  }
  for(int p:{V::P_DUCK,V::P_SYNC,V::P_RETRIGGER,V::P_RUN}){if(p==V::P_SYNC)tap(218,14);
   const int x=p==V::P_DUCK?180:p==V::P_SYNC?184:p==V::P_RETRIGGER?118:170,y=p==V::P_DUCK?205:p==V::P_SYNC?45:p==V::P_RETRIGGER?105:165;
   const q15 raw=p==V::P_RETRIGGER?vt::retrigger_value(0):0;s.set(p,raw);tap(x,y);
   CHECK(step.lock_count==1&&step.locks[0].param==p&&step.locks[0].value==raw&&u.params[p].base==raw);
   tap(p==V::P_DUCK?178:18,p==V::P_DUCK?184:y);CHECK(step.lock_count==0);
  }
  CHECK(editor_back());s.set(V::P_FLOOR,0);drag(70,205,25);CHECK(step.lock_count==1);editor_touch_down(70,205);g_seq_lock_step=6;editor_touch_drag(90,205);editor_touch_release();g_seq_lock_step=5;++owners;
  CHECK(step.locks[0].value==25*32767/296);tap(18,184);CHECK(step.lock_count==0);
  step.lock_count=SEQ_LOCKS;for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%UNITS_PER_CHAIN),uint8_t(i),2222};
  const auto before=step;s.set(V::P_GAIN,10000);drag(230,113,20);CHECK(std::memcmp(&before,&step,sizeof step)==0&&u.params[V::P_GAIN].base==10000&&g_seq_lock_flash>0);
  const bool byp=u.bypass;tap(260,14);CHECK(u.bypass==byp);tap(280,205);CHECK(step.lock_count==SEQ_LOCKS&&u.params[V::P_DUCK].base==0);
  CHECK(s.engine->sequence_pattern(active).steps[0][5].lock_count==active_count);
  step.lock_count=0;g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
 }
 g_view=View::Chains;g_overlay=Overlay::None;
}
std::vector<u32> host_region(int x0,int y0,int x1,int y1){std::vector<u32> out;for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)out.push_back(pixels[y*width+x]);return out;}
void needle_oracles(){
 Scene s;auto& u=unit(s);
 for(int k=0;k<7;++k)for(int mode=0;mode<3;++mode){const int p=main_params[k],left=k%2?173:15,end=k%2?305:147,y=ys[k/2]+23;const bool bi=p==V::P_A_CURVE||p==V::P_D_CURVE;
  const int lo=bi?-32767:0;auto& q=u.params[p];q.base=q15(mode==0?lo:mode==1?0:32767);q.target=q.effective=q15(mode==2?lo:27000);q.src=ModSource::SigilA;q.mod_depth=0;q.mod_enabled=true;
  s.capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(false);vca_focus_draw(u);CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto actual=host_region(left-3,y-6,end+3,y+6);
  module_inspector_surface(false);editor_style::paper(320);auto px=[&](q15 v){return left+(int(v)+(bi?32767:0))*(end-left)/(bi?65534:32767);};
  const q15 live=u.visual_ready()?u.visual_effective(p):q.effective;
  module_fader::horizontal(left,end,y,px(q.base),px(live),{true,false,false},bi?px(0):-1);
  CHECK(host_region(left-3,y-6,end+3,y+6)==actual);++needles;
 }
}
void dot_oracle(Scene& s){
 auto& u=unit(s);CHECK(u.visual_ready()&&u.visual_stage()>0);
 const bool sync=u.modern()&&vt::enabled(u.params[V::P_SYNC].base);
 const uint32_t total=vt::frames(u.params[V::P_ATTACK].base,false,sync,u.clock_bpm_q16())+
  vt::frames(u.params[V::P_HOLD].base,true,sync,u.clock_bpm_q16())+vt::frames(u.params[V::P_DECAY].base,false,sync,u.clock_bpm_q16());
 const int ps[]={V::P_ATTACK,V::P_HOLD,V::P_DECAY};uint64_t elapsed=0;
 for(int i=0;i<u.visual_stage()-1;++i)elapsed+=u.visual_stage_frames(ps[i]);
 elapsed+=uint64_t(u.visual_stage_frames(ps[u.visual_stage()-1]))*u.visual_progress_q16()/65536;
 const int x=34+int(std::min<uint64_t>(elapsed,total)*352/total),y=178-(int(u.visual_envelope())*90+16383)/32767;
 s.capture();module_inspector_surface(true);CHECK(vca_focus_draw_top());
 for(auto xy:{std::pair<int,int>{x-4,y},{x+4,y},{x,y-4},{x,y+4}})CHECK(pixels[xy.second*400+xy.first]==palette::spot());
 ++models;
}
void model_oracles(){
 Scene s;seed(s);auto& u=unit(s);s.capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(true);CHECK(vca_focus_draw_top());
 CHECK(has((std::string("opening ")+std::to_string((u.visual_envelope()*100u+16383)/32767)+"%").c_str()));dot_oracle(s);
 // Isolate the authored curve from its intentionally independent actual-dot overlay.
 u.reset();s.capture();module_inspector_surface(true);CHECK(vca_focus_draw_top());
 // The shape has one sampled opening per column on an honest linear axis.
 const uint32_t a=vca_envelope::time_frames(u.params[V::P_ATTACK].base),hld=vca_envelope::time_frames(u.params[V::P_HOLD].base,true),d=vca_envelope::time_frames(u.params[V::P_DECAY].base),total=a+hld+d;
 for(int i=1;i<352;++i){const uint32_t t=uint64_t(i)*total/352;q15 e=0;
  if(t<a)e=vca_envelope::curve(q15(uint64_t(t)*32767/a),u.params[V::P_A_CURVE].base);
  else if(t<a+hld)e=32767;else e=vca_envelope::curve(q15(uint64_t(total-t)*32767/d),q15(-u.params[V::P_D_CURVE].base));
  const q15 opening=vca_envelope::opening(e,u.params[V::P_FLOOR].base,false);const int y=178-(int(opening)*90+16383)/32767;
  const auto ink=t<a+hld?palette::ink():palette::spot();bool found=false;for(int dy=-2;dy<=2;++dy)if(pixels[(y+dy)*400+34+i]==ink)found=true;CHECK(found);++models;
 }
 // A changed time target during a stage does not move the actual dot using a
 // freshly computed target duration; its x uses latched elapsed sample times.
 u.trigger();advance(u,650*32);const auto length=u.visual_stage_frames(V::P_DECAY);s.set(V::P_DECAY,0);CHECK(u.visual_stage_frames(V::P_DECAY)==length);dot_oracle(s);
 s.set(V::P_ATTACK,0);s.set(V::P_HOLD,0);dot_oracle(s); // clamped on the edit axis, not re-timed by new targets
 seed(s);u.reset();u.trigger();advance(u,330*32);u.trigger();advance(u,1);dot_oracle(s); // soft restart from nonzero opening
 s.set(V::P_SYNC,32767);for(int p:{V::P_ATTACK,V::P_HOLD,V::P_DECAY})for(int div=0;div<vt::COUNT;++div){
  const q15 raw=vt::division_value(div,p==V::P_HOLD);s.set(p,raw);s.capture();char text[32];vca_focus_value_text(text,sizeof text,p,raw);
  CHECK(std::strcmp(text,vt::division_name(div))==0);++formats;
 }
 s.set(V::P_HOLD,0);s.capture();char text[16];vca_focus_value_text(text,sizeof text,V::P_HOLD,0);CHECK(std::strcmp(text,"off")==0);++formats;
}
void all_mod(const std::string& prefix){
 Scene s;auto& u=unit(s);
 for(int version:{11,8,3}){if(version==11)u.set_modern(true);else u.set_flexible(version==8);s.capture();
  for(int p=0;p<u.num_params;++p){vca_focus_select(p);s.capture();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
   take(s,prefix+"vca_mod"+std::to_string(version)+"_"+std::to_string(p),"all-version-mod");
   for(int raw:{-32768,0,1,16384,32767}){char text[32],tiny[3]={'x','x','z'};vca_focus_value_text(text,sizeof text,p,q15(raw));CHECK(text[0]&&std::strcmp(text,"--"));
    vca_focus_value_text(tiny,2,p,q15(raw));CHECK(tiny[1]==0&&tiny[2]=='z');++formats;}
   param_lens_touch_release();param_lens_touch_down(50,186);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);param_lens_cancel();++lenses;
  }
 }
 // Frozen frame contains its lens state; changing live global lens state must
 // not make captured body/graph appear/disappear without a new capture.
 u.set_modern(true);s.capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(true);CHECK(vca_focus_draw_top());const auto top=pixels;
 module_inspector_surface(false);vca_focus_draw(u);const auto lower=pixels;u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(vca_focus_draw_top());}CHECK(pixels==top);
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;vca_focus_draw(u);}CHECK(pixels==lower);
 s.capture();module_inspector_surface(false);vca_focus_draw(u);const auto shell=pixels;param_lens_cancel();
 module_inspector_surface(true);CHECK(vca_focus_draw_top());const auto modtop=pixels;
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;vca_focus_draw(u);}CHECK(pixels==shell);
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(vca_focus_draw_top());}CHECK(pixels==modtop);++frozen;
}
void score_formats(){
 Scene s;auto& shown=unit(s);char text[32],expected[32];
 for(int count:{3,8,11}){V u;if(count!=11)u.set_flexible(count==8);
  const uint8_t version=seq_lock::variant(u);CHECK(version==(count==3?0:count==8?1:3));
  for(q15 sync:{q15(0),q15(32767)}){u.params[V::P_SYNC].base=sync;CHECK(seq_lock::variant(u)==version);++formats;}
  // Score identity/value law is independent of another open editor or its SYNC.
  shown.set_modern(true);s.set(V::P_SYNC,count==11?0:32767);s.capture();
  for(int p:{V::P_ATTACK,V::P_DECAY,V::P_HOLD}){if(p>=u.num_params)continue;
   for(int raw:{0,1,12345,32767}){
    seq_lock::format_value(UnitKind::Vca,version,p,q15(raw),text,sizeof text);
    if(count==11){if(p==V::P_HOLD&&!raw)std::snprintf(expected,sizeof expected,"off");else std::snprintf(expected,sizeof expected,"q15 %d",raw);
     CHECK(std::strcmp(text,expected)==0&&std::strstr(text,"ms")==nullptr);}
    else {module_inspector_value_text(expected,sizeof expected,UnitKind::Vca,p,q15(raw),count==8);CHECK(std::strcmp(text,expected)==0);}
    ++formats;
   }
  }
 }
 for(int p:{V::P_SYNC,V::P_RETRIGGER,V::P_RUN}){
  CHECK(!seq_lock::supports_offset(UnitKind::Vca,3,p));
  for(int raw:{0,10922,10923,16383,16384,21845,21846,32767})for(int direction:{-1,0,1}){
   const q15 value=q15(raw),next=seq_lock::nudge_value(UnitKind::Vca,3,p,value,direction,false);
   const int count=p==V::P_RETRIGGER?3:2,before=p==V::P_RETRIGGER?vt::retrigger(value):vt::enabled(value);
   const int after=p==V::P_RETRIGGER?vt::retrigger(next):vt::enabled(next);
   CHECK(after==std::max(0,std::min(count-1,before+direction)));
   if(after==before)CHECK(next==value);
   seq_lock::format_value(UnitKind::Vca,3,p,next,text,sizeof text);
   const char* retrigger[]={"soft","reset","ignore"};const char* name=p==V::P_RETRIGGER?retrigger[after]:p==V::P_SYNC?(after?"sync":"free"):(after?"cycle":"one shot");
   CHECK(std::strcmp(text,name)==0);++formats;
  }
 }
 CHECK(seq_lock::supports_offset(UnitKind::Vca,3,V::P_ATTACK));
}
int run(const std::filesystem::path& dir,bool save){
 CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=save;h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());
 h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 fader_lancet_cases::sample_fixture();CHECK(editor_style::init());
 for(const auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  captures(prefix);controls();banks();needle_oracles();model_oracles();all_mod(prefix);score_formats();}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("vca extended: %d duals %d gestures %d frozen %d model points %d formats %d mod lenses %d exact needles %d owner cancellations; native host only\n",h::duals,gestures,frozen,models,formats,lenses,needles,owners);return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==3);return vca_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"vca extended: %s\n",e.what());return 1;}}
