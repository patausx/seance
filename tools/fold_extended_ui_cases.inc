// Focused Fold-only actual native UI. Historical run() suites are never invoked.
#include "../platform/3ds/fold_editor_visual.h"
#include "../platform/3ds/fold_wave_model.h"
#include "../platform/3ds/module_heading.h"
#include "../platform/3ds/sequencer_lock_format.h"
static uint64_t fold_test_samples=0,fold_test_calls=0,fold_test_live_calls=0,fold_test_curves=0;
static seance::FoldUnit* fold_test_live=nullptr;
extern "C" void __real__ZN6seance8FoldUnit7processEPsi(seance::FoldUnit*,seance::q15*,int);
extern "C" void __wrap__ZN6seance8FoldUnit7processEPsi(seance::FoldUnit* u,seance::q15* out,int count){
 ++fold_test_calls;fold_test_samples+=std::max(0,count);if(u==fold_test_live)++fold_test_live_calls;
 __real__ZN6seance8FoldUnit7processEPsi(u,out,count);
}
extern "C" float __real__ZN6seance8FoldUnit8transferEfPKs(float,const seance::q15*);
extern "C" float __wrap__ZN6seance8FoldUnit8transferEfPKs(float in,const seance::q15* values){
 ++fold_test_curves;return __real__ZN6seance8FoldUnit8transferEfPKs(in,values);
}
namespace fold_extended_cases {
namespace h=module_headings_cases;namespace l=fold_editor_visual;using C=FoldUnit;
int gestures=0,frozen=0,models=0,formats=0,lenses=0,needles=0,owners=0,lens_frozen=0;
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Fold){fold_focus_input(0,0,false);g_overlay=Overlay::None;module_inspector_capture();
  g_overlay=Overlay::Editor;module_inspector_capture();fold_focus_select(C::P_DRIVE);capture();fold_test_live=static_cast<C*>(unit);}
 ~Scene(){fold_test_live=nullptr;editor_cancel_input();param_lens_cancel();fold_focus_input(0,0,false);}
};
C& unit(Scene& s){return *static_cast<C*>(s.unit);}
void set(Scene& s,int p,q15 v){s.set(p,v);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void drag(int x,int y,int dx){editor_touch_down(x,y);editor_touch_drag(x+dx,y);editor_touch_release();++gestures;}
bool has(const char* s){for(const auto& t:texts)if(t.text==s)return true;return false;}
void settle(Scene& s){for(int i=0;i<9;++i)s.capture();}
void draw_top(){editor_style::capture();editor_gpu::begin();module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}}
void take(Scene& s,const std::string& id,const char* variant,bool ready=true){
 if(ready)settle(s);h::take(s,id,variant);
 const auto calls=fold_test_calls,curves=fold_test_curves;
 editor_style::capture();editor_gpu::begin();module_inspector_surface(true);
 {fader_lancet_cases::AudioHidden hidden;top_draw();}CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;
 module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;
 const auto again=[&]{module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
  module_inspector_surface(false);{ModuleInspectorNoEngine hidden;fold_focus_draw(unit(s));if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);};
 again();{fader_lancet_cases::PoisonAfterCapture poison(s);again();}
 CHECK(calls==fold_test_calls&&curves==fold_test_curves&&!fold_test_live_calls);++frozen;
}
void rounded(Scene& s){set(s,C::P_DRIVE,8192);set(s,C::P_MODE,0);set(s,C::P_BIAS,4000);set(s,C::P_SMOOTH,22937);set(s,C::P_TONE,20500);set(s,C::P_TRIM,16423);set(s,C::P_MIX,27852);}
void scenes(const std::string& p){
 {Scene s;CHECK(unit(s).modern());take(s,p+"fold_default","default7");rounded(s);take(s,p+"fold_wave","wave7");tap(218,14);take(s,p+"fold_curve","curve7");}
 {Scene s;rounded(s);set(s,C::P_SMOOTH,0);set(s,C::P_TONE,32767);take(s,p+"fold_sharp","sharp");}
 {Scene s;rounded(s);tap(68,124);take(s,p+"fold_wrap","wrap-anchor");tap(116,124);take(s,p+"fold_diode","diode-anchor");tap(218,14);take(s,p+"fold_offscale","curve-offscale");}
 {Scene s;set(s,C::P_DRIVE,-32767);fold_focus_select(C::P_DRIVE);take(s,p+"fold_attenuate","quarter-gain");}
 {Scene s;set(s,C::P_BIAS,-1);fold_focus_select(C::P_BIAS);take(s,p+"fold_fine","negative-fine-bias");}
 {Scene s;rounded(s);unit(s).bypass=true;take(s,p+"fold_bypass","bypass-dry");}
 {Scene s;rounded(s);s.engine->chains[0].mute=true;take(s,p+"fold_mute","muted-reference");}
 {Scene s;rounded(s);set(s,C::P_MIX,0);take(s,p+"fold_mix0","exact-dry");}
 {Scene s;rounded(s);set(s,C::P_TRIM,0);take(s,p+"fold_trim0","wet-mute");}
 {Scene s;rounded(s);take(s,p+"fold_updating","key-change-clears",false);draw_top();CHECK(has("updating"));
  settle(s);draw_top();CHECK(!has("updating"));set(s,C::P_SMOOTH,0);s.capture();draw_top();CHECK(has("updating"));
  for(int y=77;y<=187;++y)for(int x=34;x<=386;++x)CHECK(pixels[y*400+x]!=palette::spot());++models;}
 {Scene s;rounded(s);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  const int b=sequencer_edit_bank();auto& st=s.engine->sequence_pattern(b>=0?b:s.engine->active_pattern).steps[0][5];
  st.lock_count=1;st.locks[0]={uint8_t(s.slot),C::P_DRIVE,-32767};take(s,p+"fold_lock","lock-reference");tap(218,14);take(s,p+"fold_curve_lock","curve-lock");}
 {Scene s;unit(s).set_modern(false);s.capture();take(s,p+"fold_legacy","legacy5");
  auto& sc=s.engine->scenes[0][0];sc.kind[s.slot]=UnitKind::Fold;for(int i=0;i<C::P_COUNT;++i)sc.base[s.slot][i]=q15(4000+i);
  const int n=(s.slot+1)%UNITS_PER_CHAIN;sc.kind[n]=UnitKind::Fold;sc.base[n][C::P_TONE]=12345;
  auto& other=s.engine->scenes[0][1];other.kind[s.slot]=UnitKind::Noise;other.base[s.slot][C::P_TONE]=22222;
  unit(s).params[C::P_DRIVE].src=ModSource::SigilA;unit(s).params[C::P_DRIVE].mod_depth=7777;
  std::array<q15,5> old;for(int i=0;i<5;++i)old[i]=unit(s).params[i].base;
  auto& st=s.engine->sequence_pattern(3).steps[0][7];st.lock_count=1;st.locks[0]={uint8_t(s.slot),C::P_TRIM,12000};const auto before=st;
  g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;tap(182,14);CHECK(!unit(s).modern());take(s,p+"fold_legacy_lock","legacy-no-convert");
  g_seq_lock_armed=false;editor_touch_down(182,14);CHECK(!unit(s).modern());editor_touch_release();++gestures;
  CHECK(unit(s).modern());C defaults;
  for(int i=0;i<5;++i){CHECK(unit(s).params[i].base==old[i]);CHECK(sc.base[s.slot][i]==4000+i);}
  for(int i=5;i<7;++i){CHECK(unit(s).params[i].base==defaults.params[i].base);CHECK(sc.base[s.slot][i]==defaults.params[i].base);}
  CHECK(unit(s).params[C::P_DRIVE].src==ModSource::SigilA&&unit(s).params[C::P_DRIVE].mod_depth==7777&&unit(s).params[C::P_DRIVE].bipolar);
  CHECK(sc.base[n][C::P_TONE]==12345&&other.base[s.slot][C::P_TONE]==22222&&std::memcmp(&st,&before,sizeof st)==0);
  take(s,p+"fold_extended","owned-explicit-only");}
 {Scene s;s.path(false);take(s,p+"fold_route","full-route");draw_top();for(const char* name:{"play","strain","swarm","chor","ghost","fold"})CHECK(has(name));}
 {Scene s;s.path(true);take(s,p+"fold_holes","holes-bypassed");draw_top();CHECK(has("play")&&has("swarm")&&has("ghost")&&has("fold")&&!has("strain")&&!has("chor"));}
}
q15 independent_source(int n){return q15(std::lround(24575*std::sin((n%128)*6.283185307179586/128)));}
int ordinate(double v){return int(std::lround(std::max(-1.0,std::min(1.0,v))*55));}
void check_projection(const std::vector<q15>& audio,const l::Column* points){
 for(int x=0;x<353;++x){const double a=x*256.0/352,b=std::min(352,x+1)*256.0/352;
  const auto at=[&](double n){int i=int(std::floor(n));return (audio[i]*(1-(n-i))+audio[std::min(256,i+1)]*(n-i))/32768.0;};
  const double first=at(a),last=at(b);double low=std::min(first,last),high=std::max(first,last);
  for(int n=int(std::ceil(a));n<=int(std::floor(b));++n){low=std::min(low,audio[n]/32768.0);high=std::max(high,audio[n]/32768.0);}
  const auto& c=points[x];CHECK(c.first==ordinate(first)&&c.last==ordinate(last)&&c.low==ordinate(low)&&c.high==ordinate(high)&&!c.clipped);
 }
}
void model_oracles(){
 // Retain the full original exhaustive memoryless oracle (including all 65536
 // sample codes at seams); no historical controller/geometry run() is invoked.
 test_fold_wave_model();++models;
 // Preserved legacy helper is independently checked against an EXPLICIT old voice;
 // no default-new constructor is used as a historical oracle.
 for(int mode:{0,8192,16384,24576,32767})for(bool bypass:{false,true}){
  C old(false);q15 values[5]={32767,24576,q15(mode),-12345,27000};for(int p=0;p<5;++p)old.params[p].effective=values[p];
  std::vector<q15> audio(2225);for(int n=0;n<2225;++n)audio[n]=q15(std::lround(24575*std::sin(4*3.14159265358979323846*n/2224)));
  if(!bypass)old.process(audio.data(),int(audio.size()));fold_wave_model::Plot actual;fold_wave_model::plot(values,bypass,actual);
  const auto y=[](q15 v){return 132-int(std::lround(double(v)*36/32768));};
  for(int x=0;x<279;++x){const int first=std::max(0,x*8-4),last=std::min(2224,x*8+4);int low=255,high=0;
   for(int n=first;n<=last;++n){low=std::min(low,y(audio[n]));high=std::max(high,y(audio[n]));}
   const auto& c=actual.shaped[x];CHECK(c.first==y(audio[first])&&c.last==y(audio[last])&&c.low==low&&c.high==high);}
  ++models;
 }
 for(int test=0;test<10;++test){C u;l::Key key;for(int p=0;p<7;++p)key.values[p]=u.params[p].base;
  key.values[C::P_DRIVE]=test==8?-32767:27000;key.values[C::P_MODE]=q15((test%3)*32767/2);key.values[C::P_BIAS]=test%2?-20000:12000;
  if(test==2)key.bypass=true;if(test==3)key.values[C::P_MIX]=0;if(test==4)key.values[C::P_TRIM]=0;
  if(test==5)key.values[C::P_TONE]=0;if(test==6)key.values[C::P_SMOOTH]=32767;if(test==7)key.values[C::P_SMOOTH]=15000;
  l::Model m;int calls=0;do{const int before=m.done;m.advance(key);CHECK(m.work<=2048&&m.done-before==m.work);++calls;}while(!m.ready());
  CHECK(calls==8);const auto plot=m.plot;const auto old_calls=fold_test_calls;m.advance(key);CHECK(!m.work&&fold_test_calls==old_calls&&std::memcmp(&plot,&m.plot,sizeof plot)==0);
  std::vector<q15> all(l::kTotal),source(257),out(257);
  for(int i=0;i<l::kTotal;++i){all[i]=independent_source(i);CHECK(all[i]==l::source_sample(i));}
  for(int p=0;p<7;++p){u.params[p].base=key.values[p];u.params[p].snap();}u.reset();
  if(!key.bypass)for(int n=0;n<l::kTotal;n+=32){u.control_tick();for(int i=0;i<std::min(32,l::kTotal-n);i+=7)u.process(all.data()+n+i,std::min(7,std::min(32,l::kTotal-n)-i));}
  for(int i=0;i<=256;++i){source[i]=independent_source(16000+i);out[i]=all[16000+i];}
  if(key.bypass||test==3)CHECK(out==source);check_projection(source,plot.source);check_projection(out,plot.output);++models;
  key.values[C::P_DRIVE]=20000;m.advance(key);CHECK(!m.ready()&&m.done==2048);key.values[C::P_DRIVE]=10000;m.advance(key);CHECK(!m.ready()&&m.done==2048);++models;
  key.curve=true;const auto before=fold_test_calls;m.advance(key);CHECK(m.ready()&&!m.work&&before==fold_test_calls);
  for(int x=0;x<353;++x){const double in=-1.0+2.0*x/352;const float shaped=C::transfer(float(in),key.values);const auto& c=m.plot.output[x];
   CHECK(c.first==ordinate(shaped)&&c.last==c.first&&c.high==c.first&&c.low==c.first);
   CHECK(c.clipped==unsigned((shaped>1?1:0)|(shaped< -1?2:0)));}++models;
 }
 // Actual resolved controls, not unsmoothed base; selected-bank lock is a draft.
 {Scene s;auto& u=unit(s);u.params[C::P_DRIVE].base=-32767;u.params[C::P_DRIVE].target=u.params[C::P_DRIVE].effective=26000;
  settle(s);l::Key key;for(int p=0;p<7;++p)key.values[p]=u.control_value(p);l::Model expected;while(!expected.ready())expected.advance(key);
  draw_top();for(int x=0;x<353;++x)for(int y=132-expected.plot.output[x].high;y<=132-expected.plot.output[x].low;++y)CHECK(pixels[y*400+34+x]==palette::spot());++models;
  const auto calls=fold_test_calls;s.capture();CHECK(calls==fold_test_calls);
  g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;int b=sequencer_edit_bank();if(b<0)b=s.engine->active_pattern;
  auto& st=s.engine->sequence_pattern(b).steps[0][4];st.lock_count=1;st.locks[0]={uint8_t(s.slot),C::P_MODE,32767};
  for(int p=0;p<7;++p)key.values[p]=u.params[p].base;key.values[C::P_MODE]=32767;expected.invalidate();while(!expected.ready())expected.advance(key);settle(s);draw_top();
  for(int x=0;x<353;++x)for(int y=132-expected.plot.output[x].high;y<=132-expected.plot.output[x].low;++y)CHECK(pixels[y*400+34+x]==palette::spot());++models;}
}
void needle_oracles(){
 Scene s;for(int view=0;view<2;++view){if(view)tap(218,14);
  for(const auto& c:l::controls)for(int mode=0;mode<3;++mode){const int p=c.p,lo=p==C::P_DRIVE||p==C::P_BIAS?-32767:0;auto& q=unit(s).params[p];
   q.base=q15(mode==0?lo:mode==1?0:32767);q.target=q.effective=q15(mode==2?lo:27000);q.src=ModSource::SigilA;q.mod_depth=0;q.mod_enabled=true;s.capture();
   editor_style::capture();editor_gpu::begin();module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}CHECK(!text_overlaps&&!outside_rects&&!outside_lines);
   const auto drawn=pixels;const int a=c.left+1,b=c.right-1;
   const auto map=[&](q15 v){if(p==C::P_DRIVE){const double gain=double(C::drive_q16(v))/65536;return a+int(std::lround(std::log(gain/.25)/std::log(32)*(b-a)));}return a+(int(v)-lo)*(b-a)/(32767-lo);};
   module_fader::horizontal(a,b,c.rail,map(q.base),map(unit(s).control_value(p)),{true,false,false},p==C::P_BIAS||p==C::P_DRIVE?map(0):p==C::P_MODE?map(16384):-1);CHECK(pixels==drawn);++needles;
  }
 }
}
void interactions(){
 Scene s;auto& u=unit(s);
 for(int view=0;view<2;++view){if(view)tap(218,14);for(const auto& c:l::controls){const int p=c.p,x=(c.left+c.right)/2;set(s,p,16000);
  editor_touch_down(x,c.rail);CHECK(u.params[p].base==16000&&fold_focus_selected()==p);editor_touch_drag(x+23,c.rail);
  const q15 expected=p==C::P_DRIVE?l::drive_raw(l::drive_fraction(16000)+23.0/296):q15(16000+23*(p==C::P_BIAS?65534:32767)/296);
  CHECK(u.params[p].base==expected);editor_touch_release();++gestures;}}
 for(int i=0;i<3;++i){tap(20+i*48,124);CHECK(u.params[C::P_MODE].base==(i==0?0:i==1?16384:32767)&&fold_focus_selected()==C::P_MODE);}
 set(s,C::P_BIAS,1200);tap(284,124);CHECK(u.params[C::P_BIAS].base==0&&fold_focus_selected()==C::P_BIAS);
 fold_focus_input(KEY_R,KEY_R,false);set(s,C::P_BIAS,11);drag(230,103,-11);CHECK(u.params[C::P_BIAS].base==0);drag(230,103,-1);CHECK(u.params[C::P_BIAS].base==-1);
 set(s,C::P_BIAS,11);drag(230,103,-12);CHECK(u.params[C::P_BIAS].base==-1);
 set(s,C::P_SMOOTH,10000);drag(75,171,16);CHECK(u.params[C::P_SMOOTH].base==10000+32767/296);fold_focus_input(0,0,false);
 set(s,C::P_BIAS,100);drag(230,103,-1);CHECK(u.params[C::P_BIAS].base==0);
 for(int which=0;which<12;++which){set(s,C::P_DRIVE,16000);editor_touch_down(160,60);
  const View view=g_view;const int slot=g_edit_slot,b=s.engine->active_pattern;auto* eng=g_eng;C other;Unit* original=s.engine->chains[0].units[slot];
  if(which==0)g_view=View::Sequencer;if(which==1)g_edit_slot=(slot+1)%UNITS_PER_CHAIN;if(which==2)s.engine->request_pattern((b+1)%NUM_PATTERNS,false);
  if(which==3)fold_focus_select(C::P_BIAS);if(which==4)tap(218,14);if(which==5)s.engine->chains[0].units[slot]=&other;if(which==6)g_eng=nullptr;
  if(which==7)u.set_modern(false);if(which==8)fold_focus_input(0,0,true);if(which==9)fold_focus_input(KEY_R,KEY_R,false);if(which==10)g_seq_lock_armed=true;if(which==11)g_overlay=Overlay::None;
  fold_focus_drag(u,190,60);CHECK(u.params[C::P_DRIVE].base==16000);++owners;
  g_eng=eng;g_view=view;g_edit_slot=slot;s.engine->chains[0].units[slot]=original;s.engine->request_pattern(b,false);g_seq_lock_armed=false;g_overlay=Overlay::Editor;
  fold_focus_input(0,0,false);u.set_modern(true);editor_touch_release();s.capture();}
 // Legacy actual original rails remain usable, but no conversion may occur on cancellation.
 u.set_modern(false);s.capture();for(int p=0;p<5;++p){constexpr int xs[5]={60,140,230,40,268},ys[5]={64,213,48,213,213};set(s,p,13000);
  drag(xs[p],ys[p],7);CHECK(u.params[p].base!=13000&&fold_focus_selected()==p);}
 for(int which=0;which<5;++which){editor_touch_down(182,14);CHECK(!u.modern());
  if(which==0)editor_cancel_input();if(which==1){editor_touch_drag(182,40);}if(which==2)fold_focus_input(KEY_R,KEY_R,false);
  if(which==3)fold_focus_select(C::P_BIAS);if(which==4)g_seq_lock_armed=true;
  editor_touch_release();CHECK(!u.modern());g_seq_lock_armed=false;fold_focus_input(0,0,false);s.capture();++owners;}
}
void banks(){
 Scene s;auto& u=unit(s);g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<NUM_PATTERNS;++b){
  if(b<4){const int old=sequencer_edit_bank();extent_test_open(s);for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}
  CHECK(sequencer_edit_bank()==b);s.engine->request_pattern((b+3)%NUM_PATTERNS,false);
  auto& st=s.engine->sequence_pattern(b).steps[0][5];st.lock_count=0;const int active=s.engine->active_pattern,active_count=s.engine->sequence_pattern(active).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  for(const auto& c:l::controls){const int p=c.p,x=(c.left+c.right)/2;set(s,p,1234);drag(x,c.rail,20);
   CHECK(st.lock_count==1&&st.locks[0].param==p&&st.locks[0].slot==s.slot&&u.params[p].base==1234);
   const q15 expected=p==C::P_DRIVE?l::drive_raw(l::drive_fraction(1234)+20.0/296):q15(1234+20*(p==C::P_BIAS?65534:32767)/296);
   CHECK(st.locks[0].value==expected);tap(c.left+4,c.y+2);CHECK(!st.lock_count);}
  set(s,C::P_BIAS,0);tap(284,124);CHECK(st.lock_count==1&&st.locks[0].param==C::P_BIAS&&st.locks[0].value==0);tap(180,83);CHECK(!st.lock_count);
  set(s,C::P_MODE,0);tap(20,124);CHECK(st.lock_count==1&&st.locks[0].value==0);tap(20,83);CHECK(!st.lock_count);
  for(int i=0;i<3;++i){tap(20+i*48,124);CHECK(st.lock_count==1&&st.locks[0].value==(i==0?0:i==1?16384:32767));}tap(20,83);CHECK(!st.lock_count);
  set(s,C::P_SMOOTH,0);editor_touch_down(75,171);editor_touch_drag(95,171);editor_touch_drag(75,171);editor_touch_release();++gestures;CHECK(st.lock_count==1&&st.locks[0].value==0);tap(20,151);CHECK(!st.lock_count);
  editor_touch_down(75,171);g_seq_lock_step=6;editor_touch_drag(95,171);editor_touch_release();CHECK(!st.lock_count);g_seq_lock_step=5;++owners;
  editor_touch_down(75,171);g_seq_lock_chain=1;editor_touch_drag(95,171);editor_touch_release();CHECK(!st.lock_count);g_seq_lock_chain=0;++owners;
  for(int i=0;i<SEQ_LOCKS;++i)st.locks[i]={uint8_t((s.slot+1)%UNITS_PER_CHAIN),uint8_t(i),1234};st.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;
  drag(75,171,30);CHECK(g_seq_lock_flash>0&&st.lock_count==SEQ_LOCKS&&u.params[C::P_SMOOTH].base==0);
  st.locks[0]={uint8_t(s.slot),C::P_SMOOTH,1234};drag(75,171,30);CHECK(st.lock_count==SEQ_LOCKS&&st.locks[0].value==q15(1234+30*32767/296));
  CHECK(s.engine->sequence_pattern(active).steps[0][5].lock_count==active_count);g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
 }
 g_view=View::Chains;g_overlay=Overlay::None;
}
void all_mod(const std::string& prefix){
 Scene s;set(s,C::P_DRIVE,-16384);set(s,C::P_BIAS,-16384);
 for(bool modern:{true,false}){unit(s).set_modern(modern);s.capture();
  for(int p=0;p<unit(s).num_params;++p){fold_focus_select(p);s.capture();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
   const auto calls=fold_test_calls,curves=fold_test_curves;take(s,prefix+(modern?"fold_mod_":"fold_legacy_mod_")+std::to_string(p),modern?"modern-mod":"legacy-mod");
   CHECK(calls==fold_test_calls&&curves==fold_test_curves);
   char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Fold,p,unit(s).params[p].base);CHECK(text[0]&&std::strcmp(text,"--"));++formats;
   if(modern&&p==C::P_DRIVE){fold_focus_value_text(text,sizeof text,p,-32767);CHECK(std::strcmp(text,"0.25x")==0);}
   param_lens_touch_release();const auto before=unit(s).params[p].base;param_lens_touch_down(144,99);param_lens_touch_drag(146,99);param_lens_touch_release();CHECK(unit(s).params[p].base>=before);++gestures;
   param_lens_touch_down(50,186);param_lens_touch_release();CHECK(unit(s).params[p].src==ModSource::SigilA);param_lens_cancel();++lenses;
  }
 }
 for(bool modern:{false,true}){C target(modern);unit(s).set_modern(!modern);s.capture();const auto revision=seq_lock::variant(target);CHECK(revision==unsigned(modern));
  for(int p=0;p<(modern?7:5);++p)for(q15 v:{q15(-32767),q15(-1),q15(0),q15(1),q15(16384),q15(32767)}){
   char out[32],score[32];l::value_text(out,sizeof out,p,v,modern);seq_lock::format_value(UnitKind::Fold,revision,p,v,score,sizeof score);
   CHECK(out[0]&&std::strcmp(out,"--")&&!std::strcmp(out,score));++formats;}}
 for(int v:{-3,-2,-1,1,2,3}){char text[32];l::value_text(text,sizeof text,C::P_BIAS,q15(v),true);
  CHECK(std::strtod(text,nullptr)!=0&&ui::text_w(text)<=76);++formats;}
 CHECK(l::position(0,C::P_DRIVE,0,1000,true)==400);CHECK(l::drive_raw(0)==-32767&&l::drive_raw(.4)==0&&l::drive_raw(1)==32767);
}
void frozen_lens(){
 Scene s;rounded(s);settle(s);editor_style::capture();editor_gpu::begin();module_inspector_surface(true);CHECK(fold_focus_draw_top());const auto top=pixels;
 module_inspector_surface(false);fold_focus_draw(unit(s));const auto bottom=pixels;u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(fold_focus_draw_top());}CHECK(pixels==top);
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;fold_focus_draw(unit(s));}CHECK(pixels==bottom);
 const auto calls=fold_test_calls,curves=fold_test_curves;set(s,C::P_SMOOTH,32767);s.capture();CHECK(calls==fold_test_calls&&curves==fold_test_curves);
 module_inspector_surface(false);fold_focus_draw(unit(s));const auto shell=pixels;param_lens_cancel();
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;fold_focus_draw(unit(s));}CHECK(pixels==shell&&!fold_focus_draw_top());
 s.capture();CHECK(fold_test_calls>calls);++lens_frozen;
}
int run(const std::filesystem::path& dir,bool save){
 CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=save;h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());
 h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 fader_lancet_cases::sample_fixture();CHECK(editor_style::init());
 for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  scenes(prefix);model_oracles();needle_oracles();interactions();banks();all_mod(prefix);frozen_lens();}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("fold extended: %d duals %d gestures %d frozen %d models %d formats %d mod-lenses %d exact needles %d owner cancellations %d frozen-lens transitions; no device/FPS/listening\n",h::duals,gestures,frozen,models,formats,lenses,needles,owners,lens_frozen);return 0;
}
} // namespace fold_extended_cases
int main(int argc,char**argv){try{CHECK(argc==3);return fold_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}catch(const std::exception&e){std::fprintf(stderr,"fold extended: %s\n",e.what());return 1;}}
