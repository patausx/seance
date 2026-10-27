// New header-only integration catalogue. Actual top_draw/editor_draw, no inherited suites.
// Host synthetic sample + software texture boundary; not hardware/performance/audio proof.
namespace module_headings_cases {
namespace f=fader_lancet_cases;
std::filesystem::path output;
std::ofstream manifest;
int duals=0,peak=0,touches=0;
bool capture_files=false;
uint64_t raster_hash(const std::vector<u32>& p){uint64_t h=1469598103934665603ull;for(u32 v:p)for(int b=0;b<4;++b){h^=(v>>(b*8))&255;h*=1099511628211ull;}return h;}
bool changed(UnitKind k){switch(k){case UnitKind::Bitcrush:case UnitKind::Chorus:case UnitKind::Delay:case UnitKind::Fold:
 case UnitKind::Noise:case UnitKind::Ring:case UnitKind::Strain:case UnitKind::Tilt:case UnitKind::Vca:case UnitKind::Ghost:
 case UnitKind::Reverb:case UnitKind::Stranular:case UnitKind::Bird:case UnitKind::Shift:case UnitKind::Fract:case UnitKind::Filter:
 case UnitKind::Fm:case UnitKind::Motor:return true;default:return false;}}
bool workflow(UnitKind k){return k==UnitKind::SamplePlayer||k==UnitKind::Grain||k==UnitKind::MicIn||k==UnitKind::Recorder;}
struct Scene:f::Scene {
 explicit Scene(UnitKind k):f::Scene(k){
  editor_cancel_input();param_lens_cancel();input::nav_opened=false;host_time_ms=10000;g_ui_frame=100;
  if(slot==1){auto*p=new PlayUnit;p->sample_slot=61;delete engine->replace_unit(0,0,p);p->trigger();}
  g_overlay=Overlay::None;capture();g_overlay=Overlay::Editor;select(f::item(k).param);capture();
 }
 void path(bool holes){
  for(auto*&u:engine->chains[0].units){if(u!=unit)delete u;u=nullptr;}
  const UnitKind kinds[]={UnitKind::SamplePlayer,UnitKind::Strain,UnitKind::Stranular,UnitKind::Chorus,UnitKind::Ghost};
  for(int i=0;i<5;++i){if(holes&&(i==1||i==3))continue;auto*u=make_unit(kinds[i]);CHECK(u);engine->chains[0].units[i]=u;
   u->bypass=i==2||i==4;if(i==0)static_cast<PlayUnit*>(u)->sample_slot=61;}
  slot=5;g_edit_slot=g_sel_slot=slot;engine->chains[0].units[slot]=unit;unit->bypass=holes;capture();
 }
};
void tap(int x,int y){editor_touch_down(x,y);editor_touch_release();++touches;}
void take(Scene&s,const std::string&id,const std::string&variant){
 s.capture();editor_style::capture();editor_gpu::begin();
 const auto clock=s.engine->transport_frame,time=host_time_ms;const int dirty=mutations;
 const auto alloc=editor_gpu::inits,uploads=editor_gpu::flushes;
 int sum=0;
 for(bool top:{true,false}){
  module_inspector_surface(top);
  {f::AudioHidden hidden;if(top)top_draw();else editor_draw();}
  const auto raster=pixels;const int count=objects,rects=outside_rects,lines=outside_lines,overlaps=text_overlaps;
  CHECK(count>0&&count<8192);CHECK(!rects&&!lines);sum+=count;
  for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
  if(capture_files)dump_surface(output,id+(top?"_top":"_bottom"));
  // Same composed route with no recapture: all pixels and object count stay stable.
  module_inspector_surface(top);{f::AudioHidden hidden;if(top)top_draw();else editor_draw();}
  CHECK(pixels==raster&&objects==count);
  manifest<<id<<'\t'<<int(s.kind)<<'\t'<<f::item(s.kind).name<<'\t'<<variant<<'\t'<<(top?"top":"bottom")
    <<'\t'<<count<<'\t'<<rects<<'\t'<<lines<<'\t'<<overlaps<<'\t'<<raster_hash(raster)<<'\n';
 }
 CHECK(sum<=8192);peak=std::max(peak,sum);++duals;
 CHECK(s.engine->transport_frame==clock&&host_time_ms==time&&mutations==dirty&&editor_gpu::inits==alloc&&editor_gpu::flushes==uploads);
 manifest.flush();std::printf("module headings: %s objects=%d\n",id.c_str(),sum);std::fflush(stdout);
}
void state(UnitKind k,const std::string&prefix,const std::string&v){Scene s(k);const int p=f::item(k).param;
 if(v=="bypass")s.unit->bypass=true;
 if(v=="long6")s.path(false);
 if(v=="holes")s.path(true);
 if(v=="live"){
  auto&q=s.unit->params[p];q.base=q.bipolar?0:9000;q.target=q.effective=q.bipolar?-10000:24000;q.src=ModSource::SigilA;q.mod_depth=12000;q.mod_enabled=true;
 }
 if(v=="lens"){s.capture();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());param_lens_touch_release();}
 if(v=="lock"){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
  CHECK(s.engine->set_step_lock(0,4,s.slot,p,14000));}
 take(s,prefix+f::item(k).name+"_"+v,v);
}
void variants(const std::string&prefix){
 {Scene s(UnitKind::Fm);fm_focus_select(FmUnit::P_ATTACK);take(s,prefix+"fm_motion","motion");tap(246,36);take(s,prefix+"fm_operators","operators");}
 {Scene s(UnitKind::Delay);auto&u=*static_cast<DelayUnit*>(s.unit);u.params[DelayUnit::P_SYNC].base=32767;u.params[DelayUnit::P_SYNC].snap();take(s,prefix+"delay_sync","sync");
  auto*old=new DelayUnit(false);delete s.engine->replace_unit(0,s.slot,old);s.unit=old;delay_focus_select(DelayUnit::P_TIME);take(s,prefix+"delay_legacy","legacy");}
 {Scene s(UnitKind::Vca);static_cast<VcaUnit*>(s.unit)->set_flexible(false);take(s,prefix+"vca_legacy","legacy");}
 {Scene s(UnitKind::Reverb);static_cast<ReverbUnit*>(s.unit)->set_modern(false);take(s,prefix+"rev_legacy","legacy");}
 {Scene s(UnitKind::Tilt);auto*u=new TiltUnit(false);delete s.engine->replace_unit(0,s.slot,u);s.unit=u;take(s,prefix+"tilt_legacy","legacy");}
 {Scene s(UnitKind::Osc);auto&u=*static_cast<OscUnit*>(s.unit);u.clean=false;take(s,prefix+"osc_legacy","legacy");s.path(false);take(s,prefix+"osc_legacy_long6","legacy-long6");}
 {Scene s(UnitKind::Stranular);tap(283,214);take(s,prefix+"strn_more","more");}
 {Scene s(UnitKind::Ghost);ghost_focus_select(GhostUnit::P_MIX);take(s,prefix+"ghost_mix","mix");}
 {Scene s(UnitKind::Filter);auto&u=*static_cast<FilterUnit*>(s.unit);for(int i=0;i<5;++i){u.type=trackr::dsp::FilterType(i);take(s,prefix+"filter_mode"+std::to_string(i),"mode");}}
 {Scene s(UnitKind::Grain);tap(243,70);take(s,prefix+"gran_cloud","cloud");tap(284,70);take(s,prefix+"gran_setup","setup");
  static_cast<GrainUnit*>(s.unit)->clean=false;take(s,prefix+"gran_legacy","legacy");}
 {Scene s(UnitKind::SamplePlayer);tap(270,82);take(s,prefix+"play_stretch","stretch");tap(270,146);take(s,prefix+"play_scratch","scratch");
  tap(270,178);take(s,prefix+"play_hold","hold");tap(270,48);tap(113,98);take(s,prefix+"play_cuts","cuts");
  tap(190,98);take(s,prefix+"play_loop","loop");tap(270,222);take(s,prefix+"play_config","config");}
 {Scene s(UnitKind::Recorder);tap(269,10);take(s,prefix+"rec_setup","setup");}
 {Scene s(UnitKind::Comp);comp_focus_select(CompUnit::P_KNEE);take(s,prefix+"comp_detail","detail");}
 {Scene s(UnitKind::Inst);s.set(InstUnit::P_INSTRUMENT,InstUnit::instrument_value(5));s.unit->trigger();s.render(256);take(s,prefix+"inst_drums","drums");}
}
int run(const std::filesystem::path&dir,bool save){
 CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);output=dir;capture_files=save;
 manifest.open(dir/"surfaces.tsv");CHECK(manifest.good());manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
 f::sample_fixture();CHECK(editor_style::init());
 for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);const std::string p=theme==palette::Theme::Graphite?"graphite_":"pink_";
  for(const auto&item:f::catalogue){state(item.kind,p,"default");if(changed(item.kind))for(const char*v:{"bypass","long6","holes","live","lens","lock"})state(item.kind,p,v);
   else if(workflow(item.kind))for(const char*v:{"lens","lock"})state(item.kind,p,v);}
  variants(p);
 }
 editor_style::shutdown();CHECK(editor_gpu::textures.empty());manifest.close();
 std::printf("module headings complete: %d duals; %d touch pairs; peak %d/8192 host objects; 28 modules x 2 themes; no inherited suites; synthetic/native host only\n",duals,touches,peak);
 return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==3);return module_headings_cases::run(argv[2],std::string(argv[1])=="--capture");}
 catch(const std::exception&e){std::fprintf(stderr,"module headings failed: %s\n",e.what());return 1;}}
