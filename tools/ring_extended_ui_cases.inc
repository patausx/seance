// Current Ring-only native host proof; no inherited run() and no historical edits.
#include "../platform/3ds/ring_editor_visual.h"
#include "../platform/3ds/ring_readout.h"
#include "../platform/3ds/sequencer_lock_format.h"
static uint64_t ring_test_carrier_samples=0;
extern "C" void __real__ZN6seance8RingUnit20process_with_carrierEPsS1_i(seance::RingUnit*,seance::q15*,seance::q15*,int);
extern "C" void __wrap__ZN6seance8RingUnit20process_with_carrierEPsS1_i(seance::RingUnit* unit,seance::q15* out,seance::q15* carrier,int frames){
 ring_test_carrier_samples+=uint64_t(std::max(0,frames));
 __real__ZN6seance8RingUnit20process_with_carrierEPsS1_i(unit,out,carrier,frames);
}
namespace ring_extended_cases {
namespace h=module_headings_cases;namespace l=ring_editor_visual;using C=RingUnit;
int gestures=0,frozen=0,models=0,formats=0,lenses=0,needles=0,owners=0;
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Ring){g_overlay=Overlay::None;module_inspector_capture();g_overlay=Overlay::Editor;
  module_inspector_capture();ring_focus_input(0,0,false);ring_focus_select(C::P_FREQ);capture();}
 ~Scene(){editor_cancel_input();param_lens_cancel();ring_focus_input(0,0,false);}
};
C& unit(Scene& s){return *static_cast<C*>(s.unit);}
void set(Scene& s,int p,q15 v){s.set(p,v);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void drag(int x,int y,int dx,int dy=0){editor_touch_down(x,y);editor_touch_drag(x+dx,y+dy);editor_touch_release();++gestures;}
bool has(const char* s){for(const auto& t:texts)if(t.text==s)return true;return false;}
void draw_top(){editor_style::capture();editor_gpu::begin();module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}}
void take(Scene& s,const std::string& id,const char* variant){
 s.capture();module_inspector_capture();h::take(s,id,variant);
 // Do not recapture an incremental reference between the compared frozen draws.
 editor_style::capture();editor_gpu::begin();module_inspector_surface(true);
 {fader_lancet_cases::AudioHidden hidden;top_draw();}CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto top=pixels;
 module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto bottom=pixels;
 const auto again=[&]{module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
  module_inspector_surface(false);{ModuleInspectorNoEngine hidden;ring_focus_draw(unit(s));if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);};
 again();{fader_lancet_cases::PoisonAfterCapture poison(s);again();}++frozen;
}
void bridge(Scene& s){set(s,C::P_FREQ,18500);set(s,C::P_DEPTH,27000);set(s,C::P_FEEDBACK,-11000);
 set(s,C::P_DIODE,29000);set(s,C::P_DRIVE,12500);set(s,C::P_FOLLOW,14000);set(s,C::P_MIX,28500);}
void scenes(const std::string& p){
 {Scene s;CHECK(unit(s).modern());take(s,p+"ring_main","main8");}
 {Scene s;bridge(s);set(s,C::P_WAVE,32767);take(s,p+"ring_bridge","pulse-bridge");}
 {Scene s;bridge(s);set(s,C::P_FOLLOW,-26000);set(s,C::P_WAVE,16383);ring_focus_select(C::P_FOLLOW);take(s,p+"ring_signed","negative-follow");}
 {Scene s;set(s,C::P_FINE,-13000);ring_focus_select(C::P_FINE);take(s,p+"ring_fine","fine-signed");}
 {Scene s;set(s,C::P_MODE,32767);set(s,C::P_NOTE,musical_note::encode(45));take(s,p+"ring_note","note-manual");}
 {Scene s;set(s,C::P_FREQ,0);set(s,C::P_FOLLOW,24000);set(s,C::P_FOLLOW_TIME,32767);tap(216,14);
  take(s,p+"ring_updating","slow-unavailable");draw_top();CHECK(has("updating"));
  for(int i=0;i<64;++i)s.capture();take(s,p+"ring_slow","4s-tone-envelope");draw_top();CHECK(has("ready"));
  set(s,C::P_FREQ,10000);s.capture();draw_top();CHECK(has("updating"));
  for(int y=140;y<=196;++y)for(int x=34;x<=386;++x)CHECK(pixels[y*400+x]!=palette::spot());++models;}
 {Scene s;bridge(s);unit(s).bypass=true;take(s,p+"ring_bypass","bypass-paused");}
 {Scene s;bridge(s);s.engine->chains[0].mute=true;take(s,p+"ring_mute","hypothetical-muted");}
 {Scene s;bridge(s);set(s,C::P_DEPTH,0);take(s,p+"ring_depth0","depth0-paused");}
 {Scene s;bridge(s);set(s,C::P_MIX,0);take(s,p+"ring_mix0","mix0-advances");}
 {Scene s;bridge(s);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  const int b=sequencer_edit_bank();auto& step=s.engine->sequence_pattern(b>=0?b:s.engine->active_pattern).steps[0][5];step.lock_count=1;step.locks[0]={uint8_t(s.slot),C::P_FOLLOW,-32767};take(s,p+"ring_lock","lock-reference");}
 {Scene s;unit(s).set_modern(false);s.capture();take(s,p+"ring_legacy","legacy6");
  auto& sc=s.engine->scenes[0][0];sc.kind[s.slot]=UnitKind::Ring;for(int i=0;i<C::P_COUNT;++i)sc.base[s.slot][i]=q15(4000+i);
  const int n=(s.slot+1)%UNITS_PER_CHAIN;sc.kind[n]=UnitKind::Ring;sc.base[n][C::P_FINE]=12345;
  auto& other=s.engine->scenes[0][1];other.kind[s.slot]=UnitKind::Noise;other.base[s.slot][C::P_FINE]=22222;
  std::array<q15,6> old;for(int i=0;i<6;++i)old[i]=unit(s).params[i].base;
  g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;tap(182,14);CHECK(!unit(s).modern());
  g_seq_lock_armed=false;tap(182,14);CHECK(unit(s).modern());C defaults;
  for(int i=0;i<6;++i){CHECK(unit(s).params[i].base==old[i]);CHECK(sc.base[s.slot][i]==4000+i);}
  for(int i=6;i<12;++i){CHECK(unit(s).params[i].base==defaults.params[i].base);CHECK(sc.base[s.slot][i]==defaults.params[i].base);}
  CHECK(sc.base[n][C::P_FINE]==12345&&other.base[s.slot][C::P_FINE]==22222);take(s,p+"ring_extended","explicit-only");}
 {Scene s;s.path(false);take(s,p+"ring_route","full-route");draw_top();for(const char* name:{"play","strain","swarm","chor","ghost","ring"})CHECK(has(name));}
 {Scene s;s.path(true);take(s,p+"ring_holes","holes-bypassed");draw_top();CHECK(has("play")&&has("swarm")&&has("ghost")&&has("ring")&&!has("strain")&&!has("chor"));}
}
void model_oracles(){
 {Scene s;auto& u=unit(s);u.params[C::P_FREQ].base=1000;u.params[C::P_FREQ].effective=u.params[C::P_FREQ].target=26000;
  u.params[C::P_DEPTH].base=0;u.params[C::P_DEPTH].effective=u.params[C::P_DEPTH].target=32767;
  s.capture();l::Key key;for(int p=0;p<12;++p)key.values[p]=u.control_value(p);l::Model expected;expected.advance(key);CHECK(expected.ready());
  draw_top();for(int x=0;x<353;++x){const auto& c=expected.plot.trace[2][x];for(int y=168-c.high;y<=168-c.low;++y)CHECK(pixels[y*400+34+x]==palette::spot());}++models;
  g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  const int b=sequencer_edit_bank();auto& step=s.engine->sequence_pattern(b>=0?b:s.engine->active_pattern).steps[0][5];
  step.lock_count=1;step.locks[0]={uint8_t(s.slot),C::P_DEPTH,32767};
  for(int p=0;p<12;++p)key.values[p]=u.params[p].base;key.values[C::P_DEPTH]=32767;expected.advance(key);s.capture();draw_top();
  for(int x=0;x<353;++x){const auto& c=expected.plot.trace[2][x];for(int y=168-c.high;y<=168-c.low;++y)CHECK(pixels[y*400+34+x]==palette::spot());}++models;}
 for(int test=0;test<8;++test){C u;l::Key key;for(int p=0;p<12;++p)key.values[p]=u.params[p].base;
  key.values[C::P_FREQ]=test==6?0:16000;key.values[C::P_FOLLOW]=test%2?-24000:18000;
  key.values[C::P_DIODE]=test%2?32767:0;key.values[C::P_WAVE]=q15((test%3)*32767/2);key.values[C::P_FINE]=-13000;
  if(test==2)key.bypass=true;if(test==3)key.values[C::P_DEPTH]=0;if(test==4)key.values[C::P_MIX]=0;
  key.slow=test>=6;l::Model model;int calls=0;do{const int before=model.done;model.advance(key);
   CHECK(model.work<=2048&&model.done-before==model.work);++calls;}while(!model.ready());
  CHECK(calls==(key.slow?63:1));const auto result=model.plot;const int starts=model.starts;model.advance(key);
  CHECK(model.work==0&&model.starts==starts&&std::memcmp(&result,&model.plot,sizeof result)==0);
  const int total=key.slow?128000:1024;std::vector<q15> input(total),out(total),carrier(total,0);
  for(int i=0;i<total;++i){if(!key.slow)input[i]=ring_reference::data::BURST[i];else{
    const int tone=int(std::lround(std::sin((i%128)*6.283185307179586/128)*24575));
    const int denominator=i<512?512:48000,numerator=i<512?i:i<16000?48000:std::max(0,64000-i);
    input[i]=q15(std::lround(double(tone*numerator)/denominator));}out[i]=input[i];}
  for(int p=0;p<12;++p){u.params[p].base=key.values[p];u.params[p].snap();}u.reset();u.control_tick();
  if(!key.bypass)for(int i=0;i<total;i+=17)u.process_with_carrier(out.data()+i,carrier.data()+i,std::min(17,total-i));
  for(int x=0;x<353;++x){const int first=(x*total+352)/353,last=((x+1)*total+352)/353;
   for(int t=0;t<3;++t){const auto& source=t==0?input:t==1?carrier:out;int lo=32767,hi=-32768;
    for(int i=first;i<last;++i){lo=std::min(lo,int(source[i]));hi=std::max(hi,int(source[i]));}
    const auto& col=result.trace[t][x];const int height=t==2?28:12;
    CHECK(col.first==int(source[first])*height/32768&&col.last==int(source[last-1])*height/32768);
    CHECK(col.low==lo*height/32768&&col.high==hi*height/32768);}}
  if(key.bypass||test==3||test==4)CHECK(out==input);
  if(key.bypass||test==3)for(q15 c:carrier)CHECK(c==0);
  ++models;
  // New keys invalidate a completed slow plot, restart within budget, and coalesce latest values.
  key.slow=true;key.values[C::P_FINE]=22000;model.advance(key);CHECK(!model.ready()&&model.done==2048&&model.starts==starts+1);
  key.values[C::P_FINE]=15000;model.advance(key);CHECK(!model.ready()&&model.done==2048&&model.starts==starts+2);++models;
 }
 {C old(false);q15 values[6];for(int p=0;p<6;++p)values[p]=old.params[p].effective;ring_reference::Plot actual;
  ring_reference::plot(values,false,actual);ring_reference::Plot expected;int previous=-1;
  for(int i=0;i<1024;++i){q15 input=ring_reference::source_sample(i),out=input;old.process(&out,1);int x=i*242/1023;bool first=x!=previous;
   ring_reference::append(expected.shaped,x,ring_reference::y(out),first);previous=x;}
  CHECK(std::memcmp(&actual.shaped,&expected.shaped,sizeof actual.shaped)==0);++models;}
}
void needle_oracles(){Scene s;constexpr int ps[]={C::P_FREQ,C::P_DEPTH,C::P_FEEDBACK,C::P_DIODE,C::P_FOLLOW,C::P_FOLLOW_TIME,C::P_DRIVE,C::P_MIX};
 set(s,C::P_FOLLOW,12000);
 for(int k=0;k<8;++k)for(int state=0;state<3;++state){int p=ps[k],lo=p==C::P_FEEDBACK||p==C::P_FOLLOW?-32767:0;
  auto& q=unit(s).params[p];q.base=q15(state==0?lo:state==1?0:32767);q.effective=q.target=q15(state==2?lo:27000);
  q.src=ModSource::SigilA;q.mod_enabled=true;q.mod_depth=0;s.capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(false);
  {fader_lancet_cases::AudioHidden hidden;editor_draw();}CHECK(!outside_lines&&!outside_rects&&!text_overlaps);const auto drawn=pixels;
  const int x=l::kControlLeft[k%2],end=l::kControlRight[k%2],y=l::kRows[k/2]+20;
  const auto map=[&](q15 v){return x+1+(int(v)-lo)*(end-x-2)/(32767-lo);};
  const bool dim=p==C::P_FOLLOW_TIME&&unit(s).params[C::P_FOLLOW].base==0;
  module_fader::horizontal(x+1,end-1,y,map(q.base),map(unit(s).control_value(p)),{true,dim,false},lo<0?map(0):-1);
  CHECK(pixels==drawn);++needles;}
}
void geometry_and_frozen_lens(){
 struct Box{int x,y,w,h;};const Box boxes[]={{8,27,50,24},{64,27,40,24},{108,27,58,24},{214,27,46,24},{264,27,42,24},
  {8,65,146,35},{166,65,146,35},{8,105,146,35},{166,105,146,35},{8,145,146,35},{166,145,146,35},{8,185,146,35},{166,185,146,35},{208,51,104,14}};
 for(int i=0;i<14;++i){const auto& a=boxes[i];if(i<13)CHECK(a.w>=32&&a.h>=24);
  for(int j=i+1;j<14;++j){const auto& b=boxes[j];CHECK(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y);}}
 Scene s;
 for(int i=0;i<5;++i)for(int y:{27,50}){tap(boxes[i].x+boxes[i].w/2,y);const int p=i<3?C::P_WAVE:C::P_MODE;
  CHECK(ring_focus_selected()==p);CHECK(unit(s).params[p].base==(i<3?q15(i*32767/2):i==3?0:32767));}
 s.capture();editor_style::capture();editor_gpu::begin();module_inspector_surface(false);ring_focus_draw(unit(s));const auto ordinary=pixels;
 u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));module_inspector_surface(false);{ModuleInspectorNoEngine hidden;ring_focus_draw(unit(s));}CHECK(pixels==ordinary);
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(ring_focus_draw_top());}
 s.capture();module_inspector_surface(false);ring_focus_draw(unit(s));const auto lens=pixels;param_lens_cancel();
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;ring_focus_draw(unit(s));}CHECK(pixels==lens);
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(!ring_focus_draw_top());}++frozen;
}
void real_fine_mod_and_note_binding(){
 Scene s;set(s,C::P_FREQ,10000);set(s,C::P_FINE,0);
 editor_touch_down(80,90);editor_touch_drag(80,70);CHECK(ring_focus_selected()==C::P_FINE);
 editor_touch_drag(80,90);CHECK(unit(s).params[C::P_FINE].base==0);
 editor_touch_drag(80,110);CHECK(unit(s).params[C::P_FINE].base==-20*32767/100);
 editor_touch_release();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
 const auto count=ring_test_carrier_samples;s.capture();CHECK(ring_test_carrier_samples==count);
 draw_top();CHECK(has("fine / carrier cents"));CHECK(ring_focus_selected()==C::P_FINE);param_lens_cancel();++lenses;++gestures;
 set(s,C::P_MODE,32767);g_overlay=Overlay::None;s.capture();g_overlay=Overlay::Editor;s.capture();
 CHECK(ring_focus_selected()==C::P_NOTE);down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
 s.capture();draw_top();CHECK(has("pitch / manual carrier note"));param_lens_cancel();++lenses;
 // Fresh slow model spends the strict budget once, then none beneath MOD.
 tap(218,14);const auto before=ring_test_carrier_samples;s.capture();CHECK(ring_test_carrier_samples-before==2048);
 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));const auto paused=ring_test_carrier_samples;
 for(int i=0;i<5;++i)s.capture();CHECK(ring_test_carrier_samples==paused);
 param_lens_cancel();s.capture();CHECK(ring_test_carrier_samples-paused==2048);++models;
}
void interactions(){Scene s;auto& u=unit(s);constexpr int ps[]={C::P_FREQ,C::P_DEPTH,C::P_FEEDBACK,C::P_DIODE,C::P_FOLLOW,C::P_FOLLOW_TIME,C::P_DRIVE,C::P_MIX};
 for(int k=0;k<8;++k){int p=ps[k],x=k%2?225:80,y=l::kRows[k/2]+20;set(s,p,12000);
  editor_touch_down(x,y);CHECK(u.params[p].base==12000);editor_touch_drag(x+24,y);
  CHECK(u.params[p].base==q15(12000+24*(p==C::P_FEEDBACK||p==C::P_FOLLOW?65534:32767)/296));
  editor_touch_drag(x,y);CHECK(u.params[p].base==12000);editor_touch_release();++gestures;}
 set(s,C::P_FREQ,10000);set(s,C::P_FINE,0);editor_touch_down(80,90);editor_touch_drag(82,88);
 CHECK(u.params[C::P_FREQ].base==10000&&u.params[C::P_FINE].base==0);editor_touch_drag(81,70);
 CHECK(u.params[C::P_FREQ].base==10000&&u.params[C::P_FINE].base==20*32767/100);
 editor_touch_drag(130,70);CHECK(u.params[C::P_FREQ].base==10000);editor_touch_drag(80,90);
 CHECK(u.params[C::P_FINE].base==0);editor_touch_drag(80,110);CHECK(u.params[C::P_FINE].base==-20*32767/100);editor_touch_release();++gestures;
 tap(279,39);CHECK(u.params[C::P_MODE].base==32767);const q15 note=u.params[C::P_NOTE].base;
 editor_touch_down(80,90);editor_touch_drag(100,90);CHECK(musical_note::decode(u.params[C::P_NOTE].base)==musical_note::decode(note)+5);
 editor_touch_drag(100,70);CHECK(u.params[C::P_FINE].base==-20*32767/100);editor_touch_drag(80,90);CHECK(u.params[C::P_NOTE].base==note);editor_touch_release();++gestures;
 for(int test=0;test<14;++test){tap(229,39);set(s,C::P_FREQ,10000);set(s,C::P_FINE,0);
  const auto oldview=g_view;auto* oldeng=g_eng;const int oldslot=g_edit_slot,oldbank=s.engine->active_pattern;
  C other;Unit* original=s.engine->chains[0].units[oldslot];
  editor_touch_down(80,90);
  if(test==0)ring_focus_select(C::P_DEPTH);if(test==1)g_view=View::Sigils;if(test==2)g_edit_slot=(oldslot+1)%6;
  if(test==3)g_seq_lock_armed=true;if(test==4)s.engine->request_pattern((oldbank+1)%8,false);
  if(test==5)g_overlay=Overlay::None;if(test==6)u.params[C::P_MODE].base=32767;
  if(test==7)ring_focus_input(KEY_R,KEY_R|KEY_TOUCH,false);if(test==8)ring_focus_input(0,KEY_TOUCH,true);
  if(test==9)g_drag_param=C::P_DEPTH;if(test==10)u.set_modern(false);
  if(test==11)g_eng=nullptr;if(test==12)editor_cancel_input();if(test==13)s.engine->chains[0].units[oldslot]=&other;
  ring_focus_drag(u,100,70);CHECK(u.params[C::P_FREQ].base==10000&&u.params[C::P_FINE].base==0);
  g_eng=oldeng;s.engine->chains[0].units[oldslot]=original;g_edit_slot=oldslot;g_view=oldview;g_overlay=Overlay::Editor;g_seq_lock_armed=false;
  s.engine->request_pattern(oldbank,false);u.set_modern(true);ring_focus_input(0,0,false);editor_touch_release();s.capture();++owners;}
}
void banks(){Scene s;auto& u=unit(s);set(s,C::P_FOLLOW,0);set(s,C::P_FINE,0);
 g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<8;++b){if(b<4){int old=sequencer_edit_bank();extent_test_open(s);for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}CHECK(sequencer_edit_bank()==b);
  s.engine->request_pattern((b+3)%8,false);auto& step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
  const int active_count=s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  drag(80,170,-24);CHECK(step.lock_count==1&&step.locks[0].param==C::P_FOLLOW&&step.locks[0].value==-24*65534/296);
  CHECK(u.params[C::P_FOLLOW].base==0);tap(25,152);CHECK(step.lock_count==0);
  drag(225,170,12);CHECK(step.lock_count==1&&step.locks[0].param==C::P_FOLLOW_TIME);tap(200,152);CHECK(step.lock_count==0);
  tap(20,39);CHECK(step.lock_count==1&&step.locks[0].param==C::P_WAVE&&step.locks[0].value==0);step.lock_count=0;
  drag(80,90,0,-20);CHECK(step.lock_count==1&&step.locks[0].param==C::P_FINE&&step.locks[0].value==20*32767/100);
  CHECK(u.params[C::P_FINE].base==0);tap(251,56);CHECK(step.lock_count==0);
  editor_touch_down(80,170);g_seq_lock_step=6;editor_touch_drag(100,170);editor_touch_release();CHECK(step.lock_count==0);g_seq_lock_step=5;++owners;
  for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%6),uint8_t(i),1234};step.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;
  drag(80,170,24);CHECK(g_seq_lock_flash>0&&step.lock_count==SEQ_LOCKS&&u.params[C::P_FOLLOW].base==0);
  CHECK(s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count==active_count);
  step.lock_count=1;step.locks[0]={uint8_t(s.slot),C::P_WAVE,32767};tap(20,39);CHECK(step.lock_count==1&&step.locks[0].value==0);
  g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;}
 g_view=View::Chains;g_overlay=Overlay::None;
}
void score_formats(){Scene s;char a[32],b[32];for(bool modern:{true,false}){unit(s).set_modern(modern);s.capture();const auto v=seq_lock::variant(unit(s));CHECK(v==modern);
 for(int p=0;p<12;++p)for(q15 q:{q15(-32767),q15(0),q15(32767)}){seq_lock::format_value(UnitKind::Ring,v,p,q,a,sizeof a);
  ring_readout::value_text(b,sizeof b,p,q,modern);CHECK(!std::strcmp(a,b));if(p<unit(s).num_params)CHECK(std::strcmp(a,"--"));++formats;}
 if(modern){for(int p:{C::P_MODE,C::P_WAVE,C::P_NOTE})CHECK(!seq_lock::supports_offset(UnitKind::Ring,v,p));
  for(int p:{C::P_FINE,C::P_FOLLOW}){ring_readout::value_text(a,sizeof a,p,-32767,true);CHECK(a[0]=='-');}}}}
void all_mod(const std::string& prefix){Scene s;for(bool modern:{true,false}){unit(s).set_modern(modern);s.capture();
 for(int p=0;p<unit(s).num_params;++p){ring_focus_select(p);s.capture();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
  take(s,prefix+(modern?"ring_mod_":"ring_legacy_mod_")+std::to_string(p),"held-l");
  char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Ring,p,unit(s).params[p].base);CHECK(text[0]&&std::strcmp(text,"--"));
  param_lens_touch_release();param_lens_touch_down(50,186);param_lens_touch_release();CHECK(unit(s).params[p].src==ModSource::SigilA);
  param_lens_cancel();++lenses;}}}
int run(const std::filesystem::path& dir,bool save){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=save;
 h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 fader_lancet_cases::sample_fixture();CHECK(editor_style::init());for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
  palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  scenes(prefix);model_oracles();needle_oracles();geometry_and_frozen_lens();real_fine_mod_and_note_binding();interactions();banks();score_formats();all_mod(prefix);}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("ring extended: %d duals %d gestures %d frozen %d model %d format %d mod %d exact needles %d cancellations; native host only\n",h::duals,gestures,frozen,models,formats,lenses,needles,owners);return 0;}
}
int main(int argc,char**argv){try{CHECK(argc==3);return ring_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"ring extended: %s\n",e.what());return 1;}}
