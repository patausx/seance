// New focused nonmodule tests. Reuse the real UI/engine and host raster only;
// do not execute inherited test suites or any file/project/audio-export workflow.
#include "platform/3ds/module_fader_painter.h"
#include "platform/3ds/chain_columns.h"
#include "platform/3ds/sigil_surface.h"
#include "platform/3ds/ui_chains.h"
#include "platform/3ds/picker_motion.h"

namespace lancet_nonmodule {
namespace f=module_fader;
namespace sf=sigil_surface;
std::filesystem::path output;
std::ofstream manifest;
int frames=0,rails_checked=0,gestures=0,adapter_cases=0,peak_objects=0;
struct Rail {
 int a,b,y,edit,live,neutral=-1;
 f::State state{};bool vertical=false,track=false;
 u32 background=0;
};
void reset_surface(bool top=false){module_inspector_surface(top);}
u32 blend(u32 a,u32 b,int amount){return sf::Cache::blend(a,b,amount,255);}
void native(const Rail&r){
 if(r.track)f::horizontal_track(r.a,r.b,r.y);
 else if(r.vertical)f::vertical(r.y,r.a,r.b,r.edit,r.live,r.state,r.neutral);
 else f::horizontal(r.a,r.b,r.y,r.edit,r.live,r.state,r.neutral);
}
void verify_rail(const Rail&r,const std::vector<u32>&actual,const std::vector<TextRecord>&labels){
#ifdef LANCET_BASELINE
 (void)r;(void)actual;(void)labels;
#else
 const int wide=width;reset_surface(wide==400);m::paper(wide);
 if(r.background)ui::rect(0,0,wide,240,r.background);
 native(r);
 const int left=r.vertical?r.y-5:r.a-3,right=r.vertical?r.y+5:r.b+3;
 const int top=r.vertical?r.a-3:r.y-5,bottom=r.vertical?r.b+3:r.y+5;
 CHECK(left>=0&&right<wide&&top>=0&&bottom<240);
 for(const auto&t:labels)CHECK(!(t.x<=right&&t.x+t.w>left&&t.y<=bottom&&t.y+t.h>top));
 for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
  if(actual[y*wide+x]!=pixels[y*wide+x]){
   std::fprintf(stderr,"rail mismatch at %d,%d / a=%d,b=%d,y=%d edit=%d live=%d actual=%08x expected=%08x\n",x,y,r.a,r.b,r.y,r.edit,r.live,actual[y*wide+x],pixels[y*wide+x]);CHECK(false);
  }
 }
 ++rails_checked;
#endif
}
void take(const std::string&id,void(*drawfn)(),std::initializer_list<Rail> rails={},bool upper=false){
 reset_surface(upper);fader_lancet_cases::glyph_mask.fill(false);fader_lancet_cases::record_glyphs=true;
 auto*engine=g_eng;auto*audio=g_audio;const auto time=host_time_ms;const int dirty=mutations;
 g_eng=nullptr;g_audio=nullptr;drawfn();g_eng=engine;g_audio=audio;fader_lancet_cases::record_glyphs=false;
 if(outside_rects||outside_lines||text_overlaps)std::fprintf(stderr,"%s bounds %d %d overlaps %d\n",id.c_str(),outside_rects,outside_lines,text_overlaps);
 CHECK(!outside_rects&&!outside_lines&&!text_overlaps);CHECK(objects>0&&objects<8192);
 // Sequencer cell-focus feedback and picker disclosure veils intentionally
 // composite after glyphs. Rail/text intersections are checked separately;
 // the before/after proof covers those complete compositions outside rails.
 if(id.find("score_")==std::string::npos&&id.find("picker_fade")==std::string::npos)
  for(size_t i=0;i<pixels.size();++i)if(fader_lancet_cases::glyph_mask[i])CHECK(pixels[i]==fader_lancet_cases::glyph_ink[i]);
 const auto image=pixels;const auto labels=texts;const int count=objects;const int wide=width;
 dump_surface(output,id);manifest<<id<<'\t'<<wide<<'\t'<<count<<"\t0\t0\t0\n";manifest.flush();
 reset_surface(upper);g_eng=nullptr;g_audio=nullptr;drawfn();g_eng=engine;g_audio=audio;
 CHECK(pixels==image&&objects==count);CHECK(host_time_ms==time&&mutations==dirty);
 for(const auto&r:rails)verify_rail(r,image,labels);
 peak_objects=std::max(peak_objects,count);++frames;
 std::printf("nonmodule %s rails=%zu objects=%d\n",id.c_str(),rails.size(),count);std::fflush(stdout);
}
void painter_equivalence(){
#ifndef LANCET_BASELINE
 // Both routes use the canonical detail::draw. Compare every emitted native
 // pixel, then verify clipping and the real 2x cache/blur and opacity adapters.
 for(bool compact:{false,true})for(bool vertical:{false,true})for(int edit:{0,1,49,50,99,100})
 for(int live:{0,2,48,50,55,99,100})for(int bits=0;bits<8;++bits){
  const f::State s{bool(bits&1),bool(bits&2),bool(bits&4)};
  reset_surface();ui::rect(0,0,320,240,0xff314259u);
  if(vertical)f::vertical(130,40,140,140-edit,140-live,s,90,compact);
  else f::horizontal(70,170,90,70+edit,70+live,s,120,compact);
  const auto expected=pixels;
  reset_surface();ui::rect(0,0,320,240,0xff314259u);sf::Painter p;
  if(vertical)f::vertical(p,130,40,140,140-edit,140-live,s,90,compact);
  else f::horizontal(p,70,170,90,70+edit,70+live,s,120,compact);
  CHECK(pixels==expected);++adapter_cases;
 }
 for(int edit:{70,120,170})for(int live:{70,117,170}){
  reset_surface();sf::Painter p;f::horizontal(p,70,170,90,edit,live,{true,false,false},120);const auto expected=pixels;
  reset_surface();p.clip_left=100;p.clip_right=150;p.clip_top=88;p.clip_bottom=93;
  f::horizontal(p,70,170,90,edit,live,{true,false,false},120);
  for(int y=0;y<240;++y)for(int x=0;x<320;++x)CHECK(pixels[y*320+x]==(x>=100&&x<150&&y>=88&&y<93?expected[y*320+x]:0xff000000u));
  ++adapter_cases;
 }
 sf::Cache cache;cache.clear();sf::Painter p{&cache};f::horizontal(p,70,170,90,80,160,{true,false,false},120);cache.finish();
 const auto copy=cache;reset_surface();CHECK(cache.draw());const auto before=pixels;reset_surface();CHECK(copy.draw());CHECK(before==pixels);
 struct Transformed {sf::Painter p;void rect(int x,int y,int w,int h,u32 c)const{p.rect(x+11,y+7,w,h,(c&0xffffffu)|0x80000000u);}} transformed;
 reset_surface();f::horizontal(transformed,70,170,90,80,160,{true,false,false},120);const auto moved=pixels;
 reset_surface();struct Alpha {void rect(int x,int y,int w,int h,u32 c)const{ui::rect(x,y,w,h,(c&0xffffffu)|0x80000000u);}} alpha;
 f::horizontal(alpha,81,181,97,91,171,{true,false,false},131);CHECK(pixels==moved);++adapter_cases;
 reset_surface();f::horizontal_track(70,170,90);const auto track=pixels;
 reset_surface();f::horizontal_track(sf::Painter{},70,170,90);CHECK(pixels==track);++adapter_cases;
#endif
}
void square(int x,int y,int w,int h){
 for(int j=0;j<h;++j)for(int i=0;i<w;++i)if(!i||!j||i==w-1||j==h-1)CHECK(pixels[(y+j)*width+x+i]==m::spot());
}
void begin_page(){
 // Main samples the shared paper route on every page. Settle its existing
 // 240ms departure fade before a still-page fixture, including theme changes.
 m::patch_capture(false,false,host_time_ms);host_time_ms+=300;m::patch_capture(false,false,host_time_ms);
}
void original_mixer_handle(int cx,int y,u32 ink){
 // User-protected wide handle: exact 31x11 rectangle with the old paper stripe.
 for(int dy=-5;dy<=5;++dy)for(int dx=-15;dx<=15;++dx)
  CHECK(pixels[(y+dy)*width+cx+dx]==(dy==0&&dx>=-11&&dx<=11?m::paper_color(y):ink));
}
void mixer(const std::string&p){
 begin_page();
 PerformanceFixture s;performance_view_buttons(0,0,0);
 const int values[]={0,8192,16384,32767};for(int c=0;c<4;++c)s.engine->chains[c].volume=q15(values[c]);s.refresh();
 take(p+"mixer_extrema",performance_view_draw);
 for(int c=0;c<4;++c){const int y[]={163,122,105,80};original_mixer_handle(35+78*c,y[c],c==0?m::spot():m::ink());}
 s.engine->capture_global_scene(0);s.engine->capture_global_scene(1);performance_view_tick();
 for(int v:{0,16384,32767}){
  for(int c=0;c<4;++c)s.engine->scene_morph[c]=q15(v);
  s.refresh();
  take(p+"mixer_morph_"+std::to_string(v),performance_view_draw);
 }
 s.engine->chains[0].mute=true;s.refresh();take(p+"mixer_muted",performance_view_draw);original_mixer_handle(35,163,m::spot());
 s.engine->chains[0].mute=false;s.engine->chains[0].volume=8192;s.refresh();
 performance_view_buttons(KEY_L,KEY_L,0);performance_view_touch_down(35,120);performance_view_touch_drag(35,160);++gestures;s.refresh();
 const q15 moved=s.engine->chains[0].volume;CHECK(moved<8192);
 take(p+"mixer_return",performance_view_draw);square(53,120,5,5);square(258,224,5,4);
 performance_view_buttons(0,0,KEY_L);CHECK(s.engine->chains[0].volume==8192);++gestures;
 performance_view_buttons(0,0,0);
 for(int v:{-100,500}){performance_view_touch_down(35,120);performance_view_touch_drag(35,v);s.release();CHECK(s.engine->chains[0].volume==(v<0?32767:0));++gestures;}
}
void banks(const std::string&p){
 begin_page();
 PerformanceFixture s;transition::reset();transition::open();transition::buttons(0,0,0);transition::tick();
 take(p+"banks_neutral",transition::draw,{{20,300,134,20,20,160},{16,96,203,56,56,56},{120,200,203,160,160},{224,304,203,277,277}});
 int previous=20;
 for(int x:{300,160,20}){transition::touch_down(previous,134);transition::touch_drag(x,134);transition::touch_release(false);transition::tick();++gestures;previous=x;
  const int at=20+s.engine->pattern_transition().position*280/32767;
  take(p+"banks_blend_"+std::to_string(x),transition::draw,{{20,300,134,at,at,160}});
 }
 const int home=20+(s.engine->pattern_transition().active?s.engine->pattern_transition().position:0)*280/32767;
 transition::buttons(KEY_L,KEY_L,0);transition::touch_down(160,134);transition::touch_drag(220,134);transition::tick();
 take(p+"banks_return",transition::draw);square(home-2,141,5,4);transition::buttons(0,0,KEY_L);transition::buttons(0,0,0);gestures+=2;
 for(int pad=0;pad<3;++pad){for(int dx:{-160,0,160}){
  const int x=40+104*pad;transition::touch_down(x,180);transition::touch_drag(x+dx,180);transition::tick();++gestures;
  const auto fx=s.engine->transition_fx();const int a=16+104*pad;
  const int at=pad==0?a+40+fx.amount*40/32767:pad==1?a+fx.amount*80/32767:a+fx.roll_division*80/3;
  take(p+"banks_fx_"+std::to_string(pad)+"_"+std::to_string(dx),transition::draw,{{a,a+80,203,at,at,pad==0?a+40:-1}});
  transition::touch_release(false);
 }}transition::close();
}
void sigils(const std::string&p){
 begin_page();
 SigilsFixture s;s.capture();
 for(int v:{-32767,0,32767}){
  s.s().depth=q15(v);s.engine->set_sigil_mod(0,ModSource::EnvFollow,q15(v));auto config=s.engine->sigil_config[0];config.phase_degrees=v<0?0:v?359:180;s.engine->set_sigil_config(0,config);s.capture();
  const int phase=110+config.phase_degrees*190/359;
  take(p+"sigil_extrema_"+std::to_string(v),sigils_view_draw,{{208,300,153,254+v*92/65534,254+v*92/65534,254},{110,300,173,phase,phase},{190,300,220,245+v*110/65534,245+v*110/65534,245}});
 }
 s.engine->set_sigil_sync(0,true);s.capture();take(p+"sigil_sync",sigils_view_draw,{{190,300,220,300,300,245,{false,true,false}}});
 s.engine->set_sigil_sync(0,false);s.engine->sigils[0].mode=SigilMode::Follow;s.capture();
 take(p+"sigil_follow",sigils_view_draw,{{110,300,173,110,110,-1,{},false,true},{190,300,220,300,300,245,{false,true,false}}});
 s.engine->sigils[0].mode=SigilMode::Loop;s.s().depth=0;s.capture();
 for(auto xy:{std::pair<int,int>{208,153},{300,153},{110,173},{300,173},{190,220},{300,220}}){s.down(xy.first,xy.second);s.lift();++gestures;}
 host_time_ms+=500;s.capture();take(p+"sigil_edited",sigils_view_draw);
 s.tap(154,24);s.capture();host_time_ms+=180;s.capture();take(p+"sigil_disclosure_cache",sigils_view_draw);s.tap(310,100);s.capture();
}
void cables(const std::string&p){
 begin_page();
 Fixture s;g_view=View::Patchbay;g_overlay=Overlay::None;g_drag=Drag::None;patchbay_forget();patchbay_enter();patchbay_capture();
 take(p+"patchbay_unselected",patchbay_draw,{{80,254,204,167,167,167,{false,true,false}}});
 auto&param=s.engine->chains[0].units[0]->params[0];param.src=ModSource::SigilA;param.mod_depth=0;
 patchbay_capture();patchbay_touch_down(230,72);patchbay_touch_release();++gestures;
 for(int v:{-32767,0,32767}){param.mod_depth=q15(v);patchbay_capture();const int x=int(std::lround(80.f+(float(v)+32767.f)*174.f/65534.f));
  take(p+"patchbay_depth_"+std::to_string(v),patchbay_draw,{{80,254,204,x,x,167}});
 }
 param.mod_enabled=false;patchbay_capture();take(p+"patchbay_disabled",patchbay_draw,{{80,254,204,254,254,167,{false,true,false}}});
 for(int x:{80,167,254}){patchbay_touch_down(x,204);patchbay_touch_release();CHECK(param.mod_depth==(x==80?-32767:x==254?32767:0));++gestures;}
 patchbay_forget();
}
void chains(const std::string&p){
 Fixture s;g_view=View::Chains;g_overlay=Overlay::None;g_drag=Drag::None;g_sel_chain=g_edit_chain=0;g_sel_slot=g_edit_slot=0;
 auto*unit=new RingUnit;delete s.engine->replace_unit(0,0,unit);for(int i=1;i<6;++i)delete s.engine->replace_unit(0,i,new RingUnit);
 chains_close_context_branch();chains_capture();host_time_ms+=300;chains_capture();
 const u32 paper=sf::Cache::blend(m::paper_color(120),m::spot(),7,100);
 for(int v:{0,16384,32767}){unit->params[RingUnit::P_FREQ].base=q15(v);unit->params[RingUnit::P_FREQ].target=unit->params[RingUnit::P_FREQ].effective=q15(v);unit->params[RingUnit::P_MIX].base=q15(v);unit->params[RingUnit::P_MIX].snap();chains_capture();
  const int x=14+v*131/32767;
  take(p+"chains_edit_"+std::to_string(v),chains_view_draw,{{14,145,94,x,x,-1,{},false,false,paper},{14,145,157,x,x,-1,{},false,false,paper}});
 }
 auto&q=unit->params[RingUnit::P_FREQ];q.base=8192;q.effective=q.target=24576;q.src=ModSource::SigilA;q.mod_depth=16000;q.mod_enabled=true;chains_capture();
 take(p+"chains_live",chains_context_draw_top,{{54,185,127,86,152,-1,{true,false,false},false,false,paper}},true);
 q.effective=q.target=q.base;chains_capture();take(p+"chains_link_equal",chains_context_draw_top,{{54,185,127,86,86,-1,{true,false,false},false,false,paper}},true);
 for(int x:{14,145}){chains_view_touch_down(x,94);chains_view_touch_hold(x,94);chains_view_touch_release(true);CHECK(q.base==(x==14?0:32767));++gestures;}
 chains_capture();chains_workspace_buttons(KEY_R);chains_capture();take(p+"chains_disclosure_cache",chains_view_draw);
 chains_context_buttons(KEY_A);CHECK(g_overlay==Overlay::Picker);host_time_ms+=30;picker_motion::sample(host_time_ms);chains_capture();take(p+"chains_picker_fade",chains_view_draw);
 g_overlay=Overlay::None;picker_motion::reset();chains_close_context_branch();chains_capture();
 auto*tilt=new TiltUnit;delete s.engine->replace_unit(0,0,tilt);chains_capture();host_time_ms+=300;chains_capture();
 for(int v:{-32767,0,32767}){tilt->params[TiltUnit::P_TILT].base=q15(v);tilt->params[TiltUnit::P_TILT].snap();chains_capture();const int x=14+(v+32767)*131/65534;
  take(p+"chains_signed_"+std::to_string(v),chains_view_draw,{{14,145,94,x,x,79,{},false,false,paper}});
 }
}
void score(const std::string&p){
 begin_page();
 Fixture s;
 for(int mode:{1,2,5,6}){s.mode(mode);
  if(mode==6){
   sequencer_capture_inspector();take(p+"score_discrete",sequencer_view_draw);
   for(auto xy:{std::pair<int,int>{30,198},{204,216},{204,216},{270,216}}){sequencer_view_touch_down(xy.first,xy.second);sequencer_view_touch_release(false);s.refresh();sequencer_capture_inspector();++gestures;}
   CHECK(sequencer_inspector().param==FmUnit::P_INDEX);
  }
  for(int x:{mode==6?160:layout::VALUE_X0,mode==6?237:160,mode==6?315:layout::VALUE_X1}){
  sequencer_view_touch_down(x,218);sequencer_view_touch_release(false);s.refresh();sequencer_capture_inspector();++gestures;
  const auto name=p+"score_"+std::to_string(mode)+"_"+std::to_string(x);
  if(mode==1){
   const auto kind=UnitKind(g_ui_snapshot.sequence_pitch_kind[0]);const int raw=g_ui_snapshot.sequence_pitch[0][0],param=g_ui_snapshot.sequence_pitch_param[0];
   const int low=sequence_note::minimum(kind),high=sequence_note::maximum(kind);
   const int n=sequence_note::absolute(kind,param)?std::clamp((sequence_note::midi(kind,q15(raw))-low)*32767/(high-low),0,32767):raw;
   const int at=38+(n*244+16383)/32767;take(name,sequencer_view_draw,{{38,282,218,at,at}});
  }else if(mode==2){const int at=38+(int(g_ui_snapshot.sequence_value[0][0])*244+16383)/32767;take(name,sequencer_view_draw,{{38,282,218,at,at}});}
  else if(mode==5){const int at=38+(int(s.engine->pattern.microtiming[0][0])+SEQ_MICROTIMING_MAX)*244/(SEQ_MICROTIMING_MAX*2);take(name,sequencer_view_draw,{{38,282,218,at,at,160}});}
  else {const auto&d=sequencer_inspector();const auto&param=d.modules[d.slot].params[d.param];const int n=param.bipolar?(d.rows[d.param].focused+32767)/2:d.rows[d.param].focused;const int at=160+n*155/32767;
   take(name,sequencer_view_draw,{{160,315,218,at,at,param.bipolar?237:-1}});
  }
 }}
 // No note/level target is a label, not an invented fader.
 for(auto*&u:s.engine->chains[0].units){delete u;u=nullptr;}s.mode(2);s.refresh();sequencer_capture_inspector();take(p+"score_no_target",sequencer_view_draw);
}
int run(const char*dir){
 output=dir;CHECK(!std::filesystem::exists(output));std::filesystem::create_directories(output);
 manifest.open(output/"surfaces.tsv");manifest<<"id\twidth\tobjects\toutside_rects\toutside_lines\ttext_overlaps\n";
 for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){palette::set(theme);
  const std::string p=theme==palette::Theme::Graphite?"graphite_":"pink_";host_time_ms=10000;
  painter_equivalence();mixer(p);banks(p);sigils(p);cables(p);chains(p);score(p);
 }
 manifest.close();std::printf("nonmodule complete: %d native frames, %d exact rails, %d gestures, %d painter cases, peak %d objects; frozen draw/no DSP; no inherited suites/device/FTP\n",frames,rails_checked,gestures,adapter_cases,peak_objects);return 0;
}
}
int main(int argc,char**argv){try{CHECK(argc==2);return lancet_nonmodule::run(argv[1]);}catch(const std::exception&e){std::fprintf(stderr,"nonmodule failed: %s\n",e.what());return 1;}}
