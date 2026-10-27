#include "platform/3ds/ui_module_rand.h"
#include "platform/3ds/editor_style.h"
namespace seance::platform::ui3ds {void rand_baseline_editor_draw();}
namespace module_rand_cases {
namespace f=fader_lancet_cases;
namespace m=editor_style;
unsigned cases=0,surfaces=0;int peak=0;
std::filesystem::path output;
std::string current_case;
std::array<q15,MAX_PARAMS> bases(const Unit& u) {std::array<q15,MAX_PARAMS>a{};for(int p=0;p<MAX_PARAMS;++p)a[p]=u.params[p].base;return a;}
void keys(u32 down=0,u32 held=0,u32 up=0,bool blocked=false) {param_lens_input(down,held,up,blocked);}
void tap(int x,int y) {keys();editor_touch_down(x,y);editor_touch_release();}
struct Scene:f::Scene {
 explicit Scene(UnitKind kind):f::Scene(kind) {
  editor_cancel_input();param_lens_cancel();input::nav_opened=false;
  host_time_ms=10000;g_ui_frame=100;
  g_overlay=Overlay::None;capture();g_overlay=Overlay::Editor;capture();
  module_rand_poll();
 }
 void lens() {keys(KEY_L,KEY_L);CHECK(param_lens_is_open());keys(0,KEY_L);capture();}
 void random_lens() {module_rand_cancel();lens();param_lens_touch_down(225,55);param_lens_touch_release();capture();}
 void leave() {keys(0,0,KEY_L);keys();CHECK(!param_lens_is_open());capture();}
};
int header_x() {for(const auto&t:texts)if(t.text=="rand"&&t.y==14)return t.x;return -1;}
void geometry() {
 if(outside_rects||outside_lines||text_overlaps) {
  std::fprintf(stderr,"rand %s: rect%d line%d overlaps%d\n",current_case.c_str(),outside_rects,outside_lines,text_overlaps);
  dump_surface(output,"failure_"+current_case);
  for(size_t i=0;i<texts.size();++i)for(size_t j=i+1;j<texts.size();++j) {
   const auto&a=texts[i];const auto&b=texts[j];
   if(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h)
    std::fprintf(stderr,"overlap %s (%d,%d,%d,%d) / %s (%d,%d,%d,%d)\n",a.text.c_str(),a.x,a.y,a.w,a.h,b.text.c_str(),b.x,b.y,b.w,b.h);
  }
 }
 CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
}
void render_surface(Scene&s,bool top) {
 module_inspector_surface(top);
 {f::AudioHidden hidden;if(top)top_draw();else editor_draw();}
}
void capture(Scene&s,const std::string&id,bool compare) {
 current_case=id;s.capture();m::capture();editor_gpu::begin();CHECK(m::active());
 const auto oldbases=bases(*s.unit);const auto rev=s.engine->module_random_revision();const int dirty=mutations;
 const auto clock=s.engine->transport_frame;int sum=0;
 for(bool top:{true,false}) {
  std::vector<u32> baseline;decltype(texts) oldtext;
  if(compare&&!top) {
   module_inspector_surface(false);{f::AudioHidden h;rand_baseline_editor_draw();}
   baseline=pixels;oldtext=texts;
  }
  render_surface(s,top);
  // The approved MOD veil deliberately preserves faint underlying controls.
  // Check actual full composition bounds now; check both text layers separately
  // below and require that independently composed pixels/commands are exact.
  if(top||!param_lens_is_open())geometry();
  else CHECK(objects>0&&objects<=8192&&!outside_rects&&!outside_lines);
  sum+=objects;
  const int anchor=top?-1:header_x();const auto image=pixels;const int count=objects;
  dump_surface(output,id+(top?"_top":"_bottom"));++surfaces;
  if(compare&&!top) {
   CHECK(baseline.size()==pixels.size());
   for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
    if(anchor>=0&&x>=anchor&&x<anchor+24&&y>=14&&y<22)continue;
    CHECK(pixels[y*320+x]==baseline[y*320+x]);
   }
   auto now=texts;now.erase(std::remove_if(now.begin(),now.end(),[](const auto&t){return t.text=="rand"&&t.y==14;}),now.end());
   CHECK(now.size()==oldtext.size());
   for(size_t i=0;i<now.size();++i)CHECK(now[i].text==oldtext[i].text&&now[i].x==oldtext[i].x&&now[i].y==oldtext[i].y&&now[i].w==oldtext[i].w&&now[i].h==oldtext[i].h);
   if(s.kind==UnitKind::Grain)CHECK(anchor<0&&pixels==baseline);else if(!g_seq_lock_armed)CHECK(anchor>=0);
  }
  module_inspector_surface(top);
  {ModuleInspectorNoEngine detached;if(top)top_draw();else {
   f::lower(*s.unit);module_rand_draw();geometry();
   if(param_lens_is_open()){texts.clear();text_overlaps=0;param_lens_draw_bottom();}
  }}
  geometry();CHECK(pixels==image&&objects==count);
 }
 CHECK(sum<=8192);peak=std::max(peak,sum);
 CHECK(bases(*s.unit)==oldbases&&s.engine->module_random_revision()==rev&&mutations==dirty&&s.engine->transport_frame==clock);++cases;
}
void input_contract(UnitKind kind) {
 Scene s(kind);current_case=std::string(f::item(kind).name)+" input";
 render_surface(s,false);const int x=header_x();
 const auto original=bases(*s.unit);const auto initial=s.engine->module_random_revision();
 if(x>=0) {
  editor_touch_down(x+10,16);CHECK(bases(*s.unit)==original); // release-only
  for(int i=0;i<30;++i){keys(0,KEY_TOUCH);editor_touch_drag(x+10,16);}
  CHECK(bases(*s.unit)==original);editor_touch_release();
  CHECK(bases(*s.unit)!=original&&s.engine->module_random_revision()==initial+1);
  const auto changed=bases(*s.unit);const int dirty=mutations;module_rand_poll();CHECK(mutations==dirty);
  editor_touch_release();CHECK(bases(*s.unit)==changed); // no repeat
  editor_touch_down(x+10,16);editor_touch_drag(x-1,16);editor_touch_drag(x+10,16);editor_touch_release();CHECK(bases(*s.unit)==changed);
  editor_touch_down(x+10,16);editor_cancel_input();editor_touch_release();CHECK(bases(*s.unit)==changed);
  // Hold rand, then L: assign without performing an accidental reroll on lift.
  editor_touch_down(x+10,16);keys(KEY_L,KEY_L|KEY_TOUCH);CHECK(param_lens_is_open());
  editor_touch_release();CHECK(bases(*s.unit)==changed);keys(0,KEY_L);s.capture();
  render_surface(s,false);bool found=false;for(const auto&t:texts)found|=t.text=="rand / module";CHECK(found);
  s.leave();
 }
 s.random_lens();const auto before=bases(*s.unit);std::array<ModSource,MAX_PARAMS> sources{};
 for(int p=0;p<MAX_PARAMS;++p)sources[p]=s.unit->params[p].src;
 for(int i=1;i<=8;++i) {
  param_lens_touch_down(14+i*33,187);param_lens_touch_release();s.capture();
  CHECK(s.unit->random_source==sigil_mod_source(i-1)&&bases(*s.unit)==before);
 }
 param_lens_touch_down(16,187);param_lens_touch_release();CHECK(s.unit->random_source==ModSource::None);
 for(int p=0;p<MAX_PARAMS;++p)CHECK(s.unit->params[p].src==sources[p]);
 param_lens_touch_down(40,146);param_lens_touch_release();CHECK(bases(*s.unit)!=before);
 param_lens_touch_down(260,146);param_lens_touch_release();CHECK(bases(*s.unit)==before);
 // Switch back to real parameter lens: old source cells keep their exact meaning.
 param_lens_touch_down(260,55);param_lens_touch_release();s.capture();
 render_surface(s,false);bool base=false;for(const auto&t:texts)base|=t.text=="base";CHECK(base);
 s.leave();
 if(x>=0) {
  const auto old=bases(*s.unit);editor_touch_down(x+10,16);g_overlay=Overlay::None;editor_touch_release();CHECK(bases(*s.unit)==old);g_overlay=Overlay::Editor;
  s.capture();g_seq_lock_armed=true;const auto rev=s.engine->module_random_revision();s.capture();render_surface(s,false);CHECK(header_x()<0);
  CHECK(!module_rand_touch_down(x+10,16));CHECK(s.engine->module_random_revision()==rev);g_seq_lock_armed=false;
 }
 ++cases;
}
void automatic_dirty() {
 Scene s(UnitKind::Filter);s.random_lens();param_lens_touch_down(47,187);param_lens_touch_release();s.leave();
 auto level=[&](int value){for(int n=0;n<SIGIL_POINTS;++n)s.engine->sigils[0].set_point(n,q15(value));auto c=s.engine->sigil_config[0];c.enabled=true;c.legacy=true;s.engine->set_sigil_config(0,c);s.render(64);};
 level(0);module_rand_poll();const auto old=bases(*s.unit);const int dirty=mutations;
 level(32767);CHECK(bases(*s.unit)!=old);CHECK(mutations==dirty);module_rand_poll();CHECK(mutations==dirty+1);
 const auto now=bases(*s.unit);s.render(2048);module_rand_poll();CHECK(mutations==dirty+1&&bases(*s.unit)==now);
 g_overlay=Overlay::None;g_view=View::Sigils;level(0);level(32767);module_rand_poll();CHECK(mutations==dirty+2);++cases;
}
void busy_and_neighbors() {
 for(const auto&i:f::catalogue) {
  Scene s(i.kind);render_surface(s,false);
  int x=-1;for(const auto&t:texts)if(t.text=="byp"&&t.y==14)x=t.x+8;
  if(x>=0) {
   const bool old=s.unit->bypass;const auto values=bases(*s.unit);
   tap(x,16);CHECK(s.unit->bypass!=old&&bases(*s.unit)==values);
   tap(x,16);CHECK(s.unit->bypass==old&&bases(*s.unit)==values);++cases;
  }
 }
 Scene s(UnitKind::Filter);auto*rec=new RecUnit;rec->sample_slot=60;
 CHECK(rec->prepare_slot()==trackr::synth::SampleEditResult::Ok);
 delete s.engine->replace_unit(1,0,rec);
 CHECK(s.engine->request_recorder_command(1,0,RecorderCommand::Start,LaunchQuantize::Immediate)==trackr::synth::SampleEditResult::Ok);
 const auto before=bases(*s.unit);const auto rev=s.engine->module_random_revision();
 for(bool paused:{false,true}) {
  if(paused)CHECK(rec->pause());s.capture();tap(222,16);CHECK(bases(*s.unit)==before);
  s.random_lens();param_lens_touch_down(47,187);param_lens_touch_release();
  CHECK(s.unit->random_source==ModSource::None);
  param_lens_touch_down(40,146);param_lens_touch_release();CHECK(bases(*s.unit)==before);
  render_surface(s,false);bool busy=false;for(const auto&t:texts)busy|=t.text=="recording / busy";CHECK(busy);s.leave();
 }
 rec->stop();CHECK(s.engine->module_random_revision()==rev);
 editor_touch_down(222,16);keys(0,KEY_TOUCH,0,true);editor_touch_release();CHECK(bases(*s.unit)==before);
 keys();s.capture();tap(222,16);CHECK(bases(*s.unit)!=before);++cases;
}
namespace save_commit_probe {
struct Session {int saved=0;void mark_saved(int){++saved;}} g_project_session;
const char* g_patch_save_error=nullptr;
bool commit(uint64_t saved_random_revision,int slot) {
#include "rand_save_commit.inc"
return true;
}
}
void dirty_save_load() {
 Scene s(UnitKind::Filter);const auto captured=s.engine->module_random_revision();
 CHECK(s.engine->randomize_module(0,s.slot));const int dirty=mutations;
 CHECK(!save_commit_probe::commit(captured,0));CHECK(save_commit_probe::g_project_session.saved==0&&mutations==dirty+1);
 CHECK(std::string(save_commit_probe::g_patch_save_error)=="rand changed / retry save");
 CHECK(save_commit_probe::commit(s.engine->module_random_revision(),0));CHECK(save_commit_probe::g_project_session.saved==1);
 const int saved_dirty=mutations;module_rand_poll();CHECK(mutations==saved_dirty);
 CHECK(s.engine->randomize_module(0,s.slot));module_rand_rebase();module_rand_poll();CHECK(mutations==saved_dirty); // discarded old-project revision
 s.capture();render_surface(s,false);CHECK(header_x()>=0);++cases;
}
void legacy(Scene&s) {
 switch(s.kind) {
  case UnitKind::Osc:static_cast<OscUnit*>(s.unit)->clean=false;break;
  case UnitKind::Grain:static_cast<GrainUnit*>(s.unit)->clean=false;break;
  case UnitKind::Noise:static_cast<NoiseUnit*>(s.unit)->set_modern(false);break;
  case UnitKind::Motor:static_cast<MotorUnit*>(s.unit)->set_modern(false);break;
  case UnitKind::Fold:static_cast<FoldUnit*>(s.unit)->set_modern(false);break;
  case UnitKind::Bitcrush:static_cast<BitUnit*>(s.unit)->set_modern(false);break;
  case UnitKind::Ring:static_cast<RingUnit*>(s.unit)->set_modern(false);break;
  case UnitKind::Strain:static_cast<StrainUnit*>(s.unit)->set_modern(false);break;
  default:break;
 }
}
int run(const std::filesystem::path& dir) {
 CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);output=dir;
 CHECK(m::init());f::sample_fixture();
 for(const auto&i:f::catalogue)input_contract(i.kind);
 automatic_dirty();dirty_save_load();busy_and_neighbors();
 for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
  palette::set(theme);host_time_ms=10000;lancet_nonmodule::begin_page();m::capture();
  const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
  for(const auto&i:f::catalogue) {
   Scene s(i.kind);capture(s,prefix+i.name+"_default",true);
   s.unit->bypass=true;capture(s,prefix+i.name+"_bypass",true);s.unit->bypass=false;
   legacy(s);capture(s,prefix+i.name+"_legacy",true);
   s.random_lens();s.engine->set_module_random_source(0,s.slot,ModSource::SigilH);
   capture(s,prefix+i.name+"_rand",false);s.leave();
  }
 }
 m::shutdown();CHECK(editor_gpu::textures.empty());
 std::printf("module rand UI: %u cases, %u native surfaces; peak %d/8192 host objects, not fps\n",cases,surfaces,peak);
 return 0;
}
}
int main(int argc,char**argv) {
 try {CHECK(argc==2);return module_rand_cases::run(argv[1]);}
 catch(const std::exception&e){std::fprintf(stderr,"%s: %s\n",module_rand_cases::current_case.c_str(),e.what());return 1;}
}
