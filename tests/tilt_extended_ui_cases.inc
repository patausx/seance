// Current Tilt-only host UI. Historical suites are compiled as support, never run.
#include "../platform/3ds/tilt_editor_visual.h"
#include "../platform/3ds/tilt_readout.h"
#include "../platform/3ds/sequencer_lock_format.h"
#include <complex>
namespace tilt_extended_cases {
namespace h=module_headings_cases;namespace l=tilt_editor_visual;using C=TiltUnit;using R=C::Revision;
int gestures=0,frozen=0,models=0,formats=0,lenses=0,needles=0,owners=0;
constexpr int X[6]={80,80,225,225,80,80},Y[6]={65,119,171,213,171,213};
constexpr int LEFT[6]={14,14,172,172,14,14},RIGHT[6]={306,306,306,306,148,148},LABEL[6]={39,99,154,196,154,196};
struct Scene:h::Scene {
 Scene():h::Scene(UnitKind::Tilt){if(slot+1<6&&!engine->chains[0].units[slot+1])delete engine->replace_unit(0,slot+1,new VcaUnit);
  tilt_focus_input(0,0,false);g_overlay=Overlay::None;module_inspector_capture();g_overlay=Overlay::Editor;capture();tilt_focus_select(C::P_TILT);}
 void capture(){h::Scene::capture();module_inspector_capture();}
 ~Scene(){editor_cancel_input();param_lens_cancel();tilt_focus_input(0,0,false);}
};
C& unit(Scene& s){return *static_cast<C*>(s.unit);}
void set(Scene& s,int p,q15 value){s.set(p,value);}
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++gestures;}
void drag(int p,int dx){editor_touch_down(X[p],Y[p]);editor_touch_drag(X[p]+dx,Y[p]);editor_touch_release();++gestures;}
bool has(const char* s){for(const auto& t:texts)if(t.text==s)return true;return false;}
void draw_top(){editor_style::capture();editor_gpu::begin();module_inspector_surface(true);{fader_lancet_cases::AudioHidden hidden;top_draw();}}
void draw_bottom(){editor_style::capture();editor_gpu::begin();module_inspector_surface(false);{fader_lancet_cases::AudioHidden hidden;editor_draw();}}
void take(Scene& s,const std::string& id,const char* variant){
 s.capture();h::take(s,id,variant);draw_top();CHECK(!outside_lines&&!outside_rects&&!text_overlaps);const auto top=pixels;
 draw_bottom();CHECK(!outside_lines&&!outside_rects&&!text_overlaps);const auto bottom=pixels;
 const auto again=[&]{module_inspector_surface(true);{ModuleInspectorNoEngine hidden;top_draw();}CHECK(pixels==top);
  module_inspector_surface(false);{ModuleInspectorNoEngine hidden;tilt_focus_draw(unit(s));if(param_lens_is_open())param_lens_draw_bottom();}CHECK(pixels==bottom);};
 again();{fader_lancet_cases::PoisonAfterCapture poison(s);again();}++frozen;
}
void air(Scene& s){set(s,C::P_TILT,21845);set(s,C::P_RANGE,21845);set(s,C::P_FREQ,16384);set(s,C::P_DRIVE,0);set(s,C::P_TRIM,tilt_eq::TRIM_UNITY);set(s,C::P_MIX,32767);}
void scenes(const std::string& p){
 {Scene s;CHECK(unit(s).extended());air(s);take(s,p+"tilt_main","main6");}
 {Scene s;air(s);set(s,C::P_TILT,-21845);take(s,p+"tilt_body","negative");}
 {Scene s;air(s);tap(292,39);CHECK(unit(s).params[C::P_TILT].base==0);take(s,p+"tilt_zero","zero-only");}
 {Scene s;air(s);auto& q=unit(s).params[C::P_TILT];q.base=10922;q.effective=q.target=21845;q.src=ModSource::SigilA;q.mod_depth=12000;
  take(s,p+"tilt_live","base-effective");draw_top();CHECK(has("base thin / effective eq"));}
 {Scene s;air(s);set(s,C::P_DRIVE,32767);take(s,p+"tilt_drive","small-signal-overflow");}
 {Scene s;air(s);set(s,C::P_TRIM,0);take(s,p+"tilt_trimlow","underflow");}
 {Scene s;air(s);set(s,C::P_TRIM,32767);take(s,p+"tilt_trimhigh","trim6db");}
 {Scene s;air(s);set(s,C::P_MIX,0);take(s,p+"tilt_mix0","dry");}
 {Scene s;air(s);unit(s).bypass=true;take(s,p+"tilt_bypass","flat-dry");}
 {Scene s;air(s);s.engine->chains[0].mute=true;take(s,p+"tilt_mute","hypothetical-grey");}
 {Scene s;air(s);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  const int b=sequencer_edit_bank();auto& step=s.engine->sequence_pattern(b>=0?b:s.engine->active_pattern).steps[0][5];
  step.lock_count=1;step.locks[0]={uint8_t(s.slot),C::P_TILT,-32767};take(s,p+"tilt_lock","lock-draft");
  for(int i=1;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%6),uint8_t(i),1234};step.lock_count=SEQ_LOCKS;take(s,p+"tilt_full","lock-full");}
 {Scene s;unit(s).set_revision(R::Legacy);set(s,C::P_TILT,21845);take(s,p+"tilt_legacy","legacy5-shelves");
  set(s,C::P_RANGE,0);take(s,p+"tilt_legacy_zero","legacy0-linear");tap(183,14);CHECK(unit(s).extended());take(s,p+"tilt_extend_legacy","explicit-legacy");}
 {Scene s;unit(s).set_revision(R::Clear);air(s);take(s,p+"tilt_clear","clear6");tap(183,14);CHECK(unit(s).extended());take(s,p+"tilt_extend_clear","explicit-clear");}
 {Scene s;s.path(false);take(s,p+"tilt_route","full-route");draw_top();for(const char* name:{"play","strain","swarm","chor","ghost","tilt"})CHECK(has(name));}
 {Scene s;s.path(true);take(s,p+"tilt_holes","route-holes");draw_top();CHECK(has("play")&&has("swarm")&&has("ghost")&&has("tilt"));}
 {Scene s;air(s);CHECK(s.engine->begin_param_temp(0,s.slot,C::P_TILT));CHECK(s.engine->set_param_temp(-23000));s.render(1024);
  CHECK(unit(s).params[C::P_TILT].base==21845);take(s,p+"tilt_temp","temporary-effective");s.engine->cancel_param_temp();}
}
void model_oracles(){
 for(R revision:{R::Legacy,R::Clear,R::Extended})for(int test=0;test<7;++test){
  C u(revision);q15 v[6];for(int p=0;p<6;++p)v[p]=p<u.num_params?u.params[p].base:tilt_eq::TRIM_UNITY;
  v[C::P_TILT]=test%2?-21845:21845;v[C::P_FREQ]=q15(test*32767/6);v[C::P_RANGE]=test==0?0:32767;
  if(test==1)v[C::P_MIX]=0;if(test==2)v[C::P_DRIVE]=21000;if(test==3)v[C::P_TRIM]=0;if(test==4)v[C::P_TRIM]=32767;
  l::Plot plot;l::calculate(v,revision,test==6,plot);bool over=false,under=false;
  for(int i=0;i<l::POINTS;++i){double hz=20*std::pow(800.0,double(i)/(l::POINTS-1)),expected=0;
   if(test!=6){if(revision==R::Extended){const auto c=tilt_extended::coefficients(v[0],v[1],v[4]);const auto z=std::exp(std::complex<double>(0,-2*l::PI*hz/32000));
     const auto h=(double(c.b0)+double(c.b1)*z)/(double(tilt_extended::COEFF_ONE)+double(c.a1)*z);
     const double mix=double(v[3])/32767,gain=double(tilt_extended::drive_gain_q16(v[2]))/65536*double(tilt_eq::trim_gain_q16(v[5]))/65536;
     expected=20*std::log10(std::max(1e-12,std::abs(1-mix+mix*gain*h)));
    }else expected=tilt_response_model::magnitude_db(v,revision==R::Clear,float(hz));}
   CHECK(plot.y[i]==l::response_y(expected));over|=expected>12.00001;under|=expected< -12.00001;}
  CHECK(plot.over==over&&plot.under==under);++models;
 }
 {Scene s;air(s);auto& u=unit(s);u.params[0].base=5000;u.params[0].effective=u.params[0].target=25000;
  s.capture();q15 v[6];for(int p=0;p<6;++p)v[p]=u.control_value(p);l::Plot expected;l::calculate(v,u.revision(),false,expected);draw_top();
  for(int x=0;x<l::POINTS;++x)CHECK(pixels[expected.y[x]*400+l::X0+2*x]==palette::spot());++models;
  g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  const int b=sequencer_edit_bank();auto& step=s.engine->sequence_pattern(b>=0?b:s.engine->active_pattern).steps[0][5];step.lock_count=1;step.locks[0]={uint8_t(s.slot),C::P_TILT,-32767};
  for(int p=0;p<6;++p)v[p]=u.params[p].base;v[0]=-32767;l::calculate(v,u.revision(),false,expected);s.capture();draw_top();
  for(int x=0;x<l::POINTS;++x)CHECK(pixels[expected.y[x]*400+l::X0+2*x]==palette::spot());++models;}
}
void needle_oracles(){Scene s;auto& u=unit(s);
 for(int p=0;p<6;++p)for(int state=0;state<3;++state){const int lo=p==0?-32767:0;auto& q=u.params[p];
  q.base=q15(state==0?lo:state==1?0:32767);q.effective=q.target=q15(state==2?lo:27000);q.src=ModSource::SigilA;q.mod_enabled=true;q.mod_depth=0;
  s.capture();draw_bottom();CHECK(!outside_rects&&!outside_lines&&!text_overlaps);const auto drawn=pixels;
  const auto map=[&](q15 value){return LEFT[p]+(int(value)-lo)*(RIGHT[p]-LEFT[p])/(32767-lo);};
  module_fader::horizontal(LEFT[p],RIGHT[p],Y[p],map(q.base),map(u.control_value(p)),{true,false,false},p==0?map(0):p==5?map(tilt_eq::TRIM_UNITY):-1);
  CHECK(pixels==drawn);++needles;}
}
void geometry_and_frozen_lens(){
 struct Box{int x,y,w,h;};const Box boxes[]={{8,28,260,27},{272,28,40,27},{8,55,304,33},{8,91,304,52},{8,146,146,35},{166,146,146,35},{8,188,146,35},{166,188,146,35}};
 for(int i=0;i<8;++i){const auto& a=boxes[i];CHECK(a.w>=32&&a.h>=24);for(int j=i+1;j<8;++j){const auto& b=boxes[j];CHECK(a.x+a.w<=b.x||b.x+b.w<=a.x||a.y+a.h<=b.y||b.y+b.h<=a.y);}}
 Scene s;s.capture();draw_bottom();const auto ordinary=pixels;u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;tilt_focus_draw(unit(s));}CHECK(pixels==ordinary);
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(tilt_focus_draw_top());}
 s.capture();module_inspector_surface(false);tilt_focus_draw(unit(s));const auto lens=pixels;param_lens_cancel();
 module_inspector_surface(false);{ModuleInspectorNoEngine hidden;tilt_focus_draw(unit(s));}CHECK(pixels==lens);
 module_inspector_surface(true);{ModuleInspectorNoEngine hidden;CHECK(!tilt_focus_draw_top());}++frozen;
}
void interactions(){Scene s;auto& u=unit(s);
 for(R revision:{R::Legacy,R::Clear,R::Extended}){u.set_revision(revision);s.capture();
  for(int p=0;p<u.num_params;++p)for(bool fine:{false,true}){set(s,p,12000);tilt_focus_input(0,fine?KEY_R:0,false);
   editor_touch_down(X[p],Y[p]);CHECK(u.params[p].base==12000);editor_touch_drag(X[p]+24,Y[p]);
   CHECK(u.params[p].base==12000+24*(p==0?65534:32767)/(296*(fine?16:1)));editor_touch_drag(X[p],Y[p]);CHECK(u.params[p].base==12000);editor_touch_release();++gestures;}
  tilt_focus_input(0,0,false);set(s,C::P_TILT,-50);drag(C::P_TILT,1);CHECK(u.params[0].base==0);
  if(u.modern()){set(s,C::P_TRIM,tilt_eq::TRIM_UNITY-30);drag(C::P_TRIM,1);CHECK(u.params[5].base==tilt_eq::TRIM_UNITY);}
 }
 u.set_revision(R::Extended);s.capture();
 for(int test=0;test<15;++test){set(s,0,10000);const auto oldview=g_view;auto* oldeng=g_eng;const int oldslot=g_edit_slot,oldbank=s.engine->active_pattern;
  C other;Unit* original=s.engine->chains[0].units[oldslot];editor_touch_down(80,65);
  if(test==0)tilt_focus_select(1);if(test==1)g_view=View::Sigils;if(test==2)g_edit_slot=(oldslot+1)%6;if(test==3)g_seq_lock_armed=true;
  if(test==4)s.engine->request_pattern((oldbank+1)%8,false);if(test==5)g_overlay=Overlay::None;
  if(test==6)u.set_revision(R::Clear);if(test==7)tilt_focus_input(KEY_R,KEY_R|KEY_TOUCH,false);if(test==8)tilt_focus_input(0,KEY_TOUCH,true);
  if(test==9)g_drag_param=1;if(test==10)g_eng=nullptr;if(test==11)editor_cancel_input();if(test==12)s.engine->chains[0].units[oldslot]=&other;
  if(test==13)tilt_focus_input(KEY_A,KEY_TOUCH,false);if(test==14)g_drag=Drag::None;
  tilt_focus_drag(u,110,65);CHECK(u.params[0].base==10000);
  g_eng=oldeng;s.engine->chains[0].units[oldslot]=original;g_edit_slot=oldslot;g_view=oldview;g_overlay=Overlay::Editor;g_seq_lock_armed=false;
  s.engine->request_pattern(oldbank,false);u.set_revision(R::Extended);tilt_focus_input(0,0,false);editor_touch_release();s.capture();++owners;}
}
void banks(){Scene s;auto& u=unit(s);for(int p=0;p<6;++p)set(s,p,0);
 g_overlay=Overlay::None;g_view=View::Sequencer;sequencer_view_enter();s.frame();
 for(int b=0;b<8;++b){if(b<4){int old=sequencer_edit_bank();extent_test_open(s);for(int i=0;i<(b-old+4)%4;++i)extent_hold_key(s,KEY_DOWN);extent_test_close(s);}
  else{s.engine->request_pattern(b,false);sequencer_forget_pins();sequencer_view_enter();s.frame();}CHECK(sequencer_edit_bank()==b);
  s.engine->request_pattern((b+3)%8,false);auto& step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=0;
  const int active_count=s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count;
  g_overlay=Overlay::Editor;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
  for(int p=0;p<6;++p){drag(p,24);CHECK(step.lock_count==1&&step.locks[0].param==p&&step.locks[0].value==24*(p==0?65534:32767)/296);
   CHECK(u.params[p].base==0);tap(LEFT[p]+8,LABEL[p]);CHECK(step.lock_count==0);}
  step.lock_count=1;step.locks[0]={uint8_t(s.slot),C::P_FREQ,4321};drag(C::P_FREQ,24);
  CHECK(step.lock_count==1&&step.locks[0].value==4321+24*32767/296);tap(22,99);CHECK(step.lock_count==0);
  tap(292,39);CHECK(step.lock_count==1&&step.locks[0].param==0&&step.locks[0].value==0);step.lock_count=0;
  const int previous_chain=g_seq_lock_chain;g_seq_lock_chain=1;
  tap(292,39);drag(0,24);CHECK(step.lock_count==0&&u.params[0].base==0);g_seq_lock_chain=previous_chain;++owners;
  editor_touch_down(80,65);g_seq_lock_step=6;editor_touch_drag(110,65);editor_touch_release();CHECK(step.lock_count==0);g_seq_lock_step=5;++owners;
  for(int i=0;i<SEQ_LOCKS;++i)step.locks[i]={uint8_t((s.slot+1)%6),uint8_t(i),1234};step.lock_count=SEQ_LOCKS;g_seq_lock_flash=0;
  tap(292,39);CHECK(g_seq_lock_flash>0&&step.lock_count==SEQ_LOCKS&&u.params[0].base==0);
  drag(1,24);CHECK(step.lock_count==SEQ_LOCKS&&u.params[1].base==0);CHECK(s.engine->sequence_pattern(s.engine->active_pattern).steps[0][5].lock_count==active_count);
  step.lock_count=1;step.locks[0]={uint8_t(s.slot),0,20000};tap(292,39);CHECK(step.lock_count==1&&step.locks[0].value==0);
  step.lock_count=0;editor_touch_down(80,65);sequencer_forget_pins();editor_touch_drag(110,65);editor_touch_release();
  CHECK(step.lock_count==0&&u.params[0].base==0);++owners;
  s.engine->request_pattern(b,false);sequencer_view_enter();s.engine->request_pattern((b+3)%8,false);CHECK(sequencer_edit_bank()==b);
  g_seq_lock_armed=false;g_overlay=Overlay::None;g_seq_lock_flash=0;
 }
 g_view=View::Chains;g_overlay=Overlay::None;
}
void conversion(){for(R revision:{R::Legacy,R::Clear}){Scene s;auto& u=unit(s);u.set_revision(revision);const int count=u.num_params;
 for(int p=0;p<count;++p){set(s,p,q15(3000+p));u.params[p].src=ModSource::SigilA;u.params[p].mod_enabled=true;}
 auto& sc=s.engine->scenes[0][0];sc.kind[s.slot]=UnitKind::Tilt;for(int p=0;p<12;++p)sc.base[s.slot][p]=q15(4000+p);
 auto& other=s.engine->scenes[0][1];other.kind[s.slot]=UnitKind::Noise;other.base[s.slot][5]=23456;
 for(int b=0;b<8;++b){auto& step=s.engine->sequence_pattern(b).steps[0][5];step.lock_count=2;step.locks[0]={uint8_t(s.slot),0,2000};step.locks[1]={uint8_t(s.slot),5,3000};}
 u.params[5].src=ModSource::SigilA;u.params[5].mod_enabled=true;s.capture();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;
 tap(183,14);CHECK(u.revision()==revision);g_seq_lock_armed=false;tap(183,14);CHECK(u.extended());
 for(int p=0;p<count;++p){CHECK(u.params[p].base==3000+p&&u.params[p].src==ModSource::SigilA);CHECK(sc.base[s.slot][p]==4000+p);}
 if(revision==R::Legacy){CHECK(u.params[5].base==tilt_eq::TRIM_UNITY&&u.params[5].src==ModSource::None&&sc.base[s.slot][5]==tilt_eq::TRIM_UNITY);}
 for(int b=0;b<8;++b)CHECK(s.engine->sequence_pattern(b).steps[0][5].lock_count==(revision==R::Legacy?1:2));
 CHECK(other.base[s.slot][5]==23456);++gestures;}}
void score_formats(){Scene s;char a[32],b[32];for(R revision:{R::Legacy,R::Clear,R::Extended}){unit(s).set_revision(revision);s.capture();CHECK(seq_lock::variant(unit(s))==uint8_t(revision));
 for(int p=0;p<6;++p)for(q15 v:{q15(-32767),q15(-1),q15(0),q15(1),q15(32767)}){
  seq_lock::format_value(UnitKind::Tilt,uint8_t(revision),p,v,a,sizeof a);tilt_readout::value_text(b,sizeof b,p,v,revision,0,false);CHECK(!std::strcmp(a,b));++formats;}
 CHECK(seq_lock::supports_offset(UnitKind::Tilt,uint8_t(revision),C::P_RANGE)==(revision==R::Extended));}
 for(int p:{C::P_TILT,C::P_DRIVE,C::P_TRIM}){const q15 v=p==C::P_TRIM?tilt_eq::TRIM_UNITY+1:1;tilt_readout::value_text(a,sizeof a,p,v,R::Extended,0);
  CHECK(std::strcmp(a,"0.0db")&&std::strcmp(a,"clean")&&std::strcmp(a,"+0.0db"));++formats;}
}
void all_mod(const std::string& prefix){Scene s;for(R revision:{R::Extended,R::Clear,R::Legacy}){unit(s).set_revision(revision);s.capture();
 for(int p=0;p<unit(s).num_params;++p){tilt_focus_select(p);s.capture();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
  take(s,prefix+"tilt_mod_"+std::to_string(int(revision))+"_"+std::to_string(p),"held-l");
  char text[32];tilt_focus_value_text(text,sizeof text,p,unit(s).params[p].base,2);CHECK(text[0]&&std::strcmp(text,"--"));
  param_lens_touch_release();param_lens_touch_down(50,186);param_lens_touch_release();CHECK(unit(s).params[p].src==ModSource::SigilA);
  param_lens_cancel();++lenses;}}}
int run(const std::filesystem::path& dir,bool save){CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);h::output=dir;h::capture_files=save;
 h::manifest.open(dir/"surfaces.tsv");CHECK(h::manifest.good());h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 fader_lancet_cases::sample_fixture();CHECK(editor_style::init());for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
  palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  scenes(prefix);model_oracles();needle_oracles();geometry_and_frozen_lens();interactions();banks();conversion();score_formats();all_mod(prefix);}
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
 std::printf("tilt extended: %d duals %d gestures %d frozen %d model %d format %d mod %d exact needles %d cancellations; native host only\n",h::duals,gestures,frozen,models,formats,lenses,needles,owners);return 0;}
}
int main(int argc,char**argv){try{CHECK(argc==3);return tilt_extended_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"tilt extended: %s\n",e.what());return 1;}}
