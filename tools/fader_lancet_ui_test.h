// Focused drawing-only proof. Included after the real editor/main host fixtures.
// No inherited regression group is called. Captures never replay these tests.
#include "../platform/3ds/module_fader.h"
#include "../platform/3ds/sample_ownership.h"
#include <set>

namespace fader_lancet_cases {
namespace fader = seance::platform::ui3ds::module_fader;
// Additional linker probe records the REAL native glyph submissions. This is
// not an alternative font or UI renderer; the existing raster still draws them.
static bool record_glyphs=false;
static std::array<u32,400*240> glyph_ink{};
static std::array<bool,400*240> glyph_mask{};
static uint64_t primitive_cases=0, dual_cases=0, frozen_cases=0, glyph_cases=0;
static uint64_t routed_cases=0, target_cases=0, rendered_frames=0;
static int peak_objects=0;
static std::string case_name;
void glyph_rect(float x,float y,float w,float h) {
    if(!record_glyphs || native_text_depth==0)return;
    for(int yy=std::max(0,int(std::ceil(y)));yy<std::min(240,int(std::ceil(y+h)));++yy)
        for(int xx=std::max(0,int(std::ceil(x)));xx<std::min(width,int(std::ceil(x+w)));++xx) {
            const int at=yy*width+xx;glyph_mask[at]=true;glyph_ink[at]=pixels[at];
        }
}
}
// C++ symbol of the established host C2D boundary (five floats, unsigned color).
// The runner adds this one wrap flag; other harness builds discard this unused
// section through their existing --gc-sections, without changing their raster.
extern "C" bool __real__Z17C2D_DrawRectSolidfffffj(float,float,float,float,float,u32);
extern "C" bool __wrap__Z17C2D_DrawRectSolidfffffj(float x,float y,float z,float w,float h,u32 c) {
    const bool result=__real__Z17C2D_DrawRectSolidfffffj(x,y,z,w,h,c);
    fader_lancet_cases::glyph_rect(x,y,w,h);return result;
}

// Replace only the narrow historical sample-slot host allowlist with the exact
// current production ownership helper. No sample browser/export IO is linked.
extern "C" int __wrap__ZN6seance8platform5ui3ds20sample_slot_for_unitEPKNS_4UnitE(const seance::Unit* u) {
    return seance::platform::ui3ds::sample_ownership::unit_slot(u);
}
namespace fader_lancet_cases {
namespace {
struct Item { UnitKind kind; const char* name; int param; };
constexpr Item catalogue[]={
    {UnitKind::Osc,"osc",OscUnit::P_SHAPE},{UnitKind::Noise,"noise",NoiseUnit::P_DENSITY},
    {UnitKind::Fm,"fm",FmUnit::P_INDEX},{UnitKind::Motor,"motor",MotorUnit::P_RPM},
    {UnitKind::SamplePlayer,"play",PlayUnit::P_SPEED},{UnitKind::Grain,"gran",GrainUnit::P_SIZE},
    {UnitKind::MicIn,"mic",MicUnit::P_GAIN},{UnitKind::Recorder,"rec",RecUnit::P_LEN},
    {UnitKind::Filter,"filter",FilterUnit::P_DRIVE},{UnitKind::Vca,"vca",VcaUnit::P_GAIN},
    {UnitKind::Delay,"delay",DelayUnit::P_TIME},{UnitKind::Fold,"fold",FoldUnit::P_DRIVE},
    {UnitKind::Bitcrush,"bit",BitUnit::P_BITS},{UnitKind::Chorus,"chorus",ChorusUnit::P_RATE},
    {UnitKind::Stranular,"strn",StranularUnit::P_PITCH},{UnitKind::Reverb,"rev",ReverbUnit::P_SIZE},
    {UnitKind::Ghost,"ghost",GhostUnit::P_TONE},{UnitKind::Ring,"ring",RingUnit::P_FREQ},
    {UnitKind::Tilt,"tilt",TiltUnit::P_TILT},{UnitKind::Strain,"strain",StrainUnit::P_DRIVE},
    {UnitKind::Shift,"shift",ShiftUnit::P_SHIFT},{UnitKind::Bird,"bird",BirdUnit::P_PITCH},
    {UnitKind::Fract,"fract",FractUnit::P_SIZE},{UnitKind::Comp,"comp",CompUnit::P_THRESHOLD},
    {UnitKind::Mouth,"mouth",MouthUnit::P_VOWEL},{UnitKind::Arp,"arp",ArpUnit::P_GATE},
    {UnitKind::Glitch,"glitch",GlitchUnit::P_DENSITY},{UnitKind::Inst,"inst",InstUnit::P_TONE}
};
static_assert(sizeof catalogue/sizeof catalogue[0]==size_t(UnitKind::Count)-1,"all current nonempty modules");
const Item& item(UnitKind kind) {for(const auto& c:catalogue)if(c.kind==kind)return c;throw std::runtime_error("missing lancet module");}
void geometry() {
    if(outside_rects || outside_lines || text_overlaps)
        std::fprintf(stderr,"lancet %s: width=%d outside rectangles=%d lines=%d text overlaps=%d\n",
                     case_name.c_str(),width,outside_rects,outside_lines,text_overlaps);
    CHECK(objects>0 && objects<=8192 && outside_rects==0 && outside_lines==0 && text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0 && t.y>=0 && t.x+t.w<=width && t.y+t.h<=240);
}
void surface(bool upper,bool glyphs=false) {
    record_glyphs=false;module_inspector_surface(upper);
    if(glyphs){glyph_mask.fill(false);glyph_ink.fill(0);record_glyphs=true;}
}
void lower(const Unit& u) {
#define L(K,T,F) case UnitKind::K:F(static_cast<const T&>(u));break
    switch(u.kind) {
        L(Osc,OscUnit,osc_focus_draw);L(Noise,NoiseUnit,noise_focus_draw);L(Fm,FmUnit,fm_focus_draw);
        L(Motor,MotorUnit,motor_focus_draw);L(SamplePlayer,PlayUnit,play_focus_draw);L(Grain,GrainUnit,grain_focus_draw);
        L(MicIn,MicUnit,mic_focus_draw);case UnitKind::Recorder:recorder_focus_draw(static_cast<const RecUnit&>(u),"");break;
        L(Filter,FilterUnit,filter_focus_draw);L(Vca,VcaUnit,vca_focus_draw);L(Delay,DelayUnit,delay_focus_draw);
        L(Fold,FoldUnit,fold_focus_draw);L(Bitcrush,BitUnit,bit_focus_draw);L(Chorus,ChorusUnit,chorus_focus_draw);
        case UnitKind::Stranular:CHECK(stranular_focus_draw_bottom());break;
        L(Reverb,ReverbUnit,reverb_focus_draw);L(Ghost,GhostUnit,ghost_focus_draw);L(Ring,RingUnit,ring_focus_draw);
        L(Tilt,TiltUnit,tilt_focus_draw);L(Strain,StrainUnit,strain_focus_draw);L(Shift,ShiftUnit,shift_focus_draw);
        L(Bird,BirdUnit,bird_focus_draw);L(Fract,FractUnit,fract_focus_draw);L(Comp,CompUnit,comp_focus_draw);
        L(Mouth,MouthUnit,mouth_focus_draw);L(Arp,ArpUnit,arp_focus_draw);L(Glitch,GlitchUnit,glitch_focus_draw);
        L(Inst,InstUnit,inst_focus_draw);default:CHECK(false);
    }
#undef L
}
void sample_fixture() {
    // Private process memory only. No WAV read/write, project or user sample IO.
    trackr::synth::Sample sample;sample.channels=1;sample.data.resize(16000);
    for(int n=0;n<16000;++n)sample.data[n]=q15(((n%128)-64)*80);
    CHECK(trackr::synth::SampleBank::instance().replace_sample(61,std::move(sample),
          trackr::synth::SampleOrigin::Runtime,"lancet_fixture.wav")==trackr::synth::SampleEditResult::Ok);
}
struct Scene : Fixture {
    Unit* unit=nullptr;UnitKind kind;int slot=0;
    std::array<Param,MAX_PARAMS> defaults{};
    explicit Scene(UnitKind k):Fixture(),kind(k) {
        param_lens_cancel();stranular_focus_cancel();ghost_focus_cancel();small_press::cancel();
        for(auto& chain:engine->chains)for(auto*& u:chain.units){delete u;u=nullptr;}
        unit=make_unit(kind);CHECK(unit && unit->ready());
        if(kind!=UnitKind::Arp && !unit->sequencer_properties().source) {
            auto* source=new OscUnit;source->params[OscUnit::P_GAIN].base=5000;source->params[OscUnit::P_GAIN].snap();
            engine->chains[0].units[0]=source;slot=1;
        }
        engine->chains[0].units[slot]=unit;
        if(kind==UnitKind::Arp)engine->chains[0].units[1]=new OscUnit;
        if(kind==UnitKind::SamplePlayer)static_cast<PlayUnit*>(unit)->sample_slot=61;
        if(kind==UnitKind::Grain)static_cast<GrainUnit*>(unit)->sample_slot=61;
        g_edit_chain=g_sel_chain=0;g_edit_slot=g_sel_slot=slot;g_view=View::Chains;g_overlay=Overlay::Editor;
        g_drag=Drag::None;g_seq_lock_armed=false;g_seq_lock_flash=0;g_mic_ok=false;
        engine->set_sequencer_enabled(false);engine->set_bpm(120);engine->transport_play();
        for(int p=0;p<unit->num_params;++p)unit->params[p].snap();
        unit->control_tick();
        if(kind!=UnitKind::Recorder)unit->trigger();
        std::copy(std::begin(unit->params),std::end(unit->params),defaults.begin());
        capture();select(item(kind).param);render(1024);capture();
    }
    ~Scene(){record_glyphs=false;g_eng=engine.get();g_audio=&audio;param_lens_cancel();small_press::cancel();g_seq_lock_armed=false;}
    void capture() {
        engine->capture_ui_snapshot(g_ui_snapshot,g_edit_chain,g_edit_slot);
        // Exact module capture order in real main, including FM's own lower DTO.
        param_lens_capture();fm_context_capture(fm_focus_selected());fm_focus_capture();
        stranular_focus_capture();play_focus_capture();
    }
    void render(int n) {q15 buffer[512];while(n>0){const int count=std::min(n,256);engine->render(buffer,count);n-=count;rendered_frames+=count;}}
    void set(int p,q15 value) {CHECK(p>=0 && p<unit->num_params);unit->params[p].base=value;unit->params[p].snap();unit->control_tick();}
    void restore() {
        param_lens_cancel();g_seq_lock_armed=false;g_view=View::Chains;g_overlay=Overlay::Editor;
        std::copy(defaults.begin(),defaults.end(),std::begin(unit->params));unit->bypass=false;engine->chains[0].mute=false;
        unit->control_tick();select(item(kind).param);capture();
    }
    void select(int p) {
        if(kind==UnitKind::Fm)fm_focus_select(p);
        else if(kind==UnitKind::Reverb) {
            if(p<4){const int xs[]={24,182,182,24},ys[]={42,42,200,200};
                reverb_focus_touch_body(*static_cast<ReverbUnit*>(unit),xs[p],ys[p]);reverb_focus_release();}
        } else if(kind==UnitKind::Ghost) {
            auto& u=*static_cast<GhostUnit*>(unit);
            if(p==GhostUnit::P_FINE)ghost_focus_touch_body(u,150,76);
            else if(p==GhostUnit::P_RATE)ghost_focus_touch_body(u,150,220);
            else if(p==GhostUnit::P_DECAY)ghost_focus_touch_body(u,61,174-int(u.params[p].base)*82/32767);
            else ghost_focus_touch_body(u,82+int(u.params[0].base)*150/32767,54);
            ghost_focus_release();
        } else if(kind==UnitKind::Stranular) {
            stranular_focus_touch_body(*static_cast<StranularUnit*>(unit),70,192);stranular_focus_release();
        } else module_inspector_select(kind,p);
        g_drag=Drag::None;small_press::cancel();
    }
    void signal(int amplitude,int depth=10000,bool enabled=true) {
        const int p=item(kind).param;auto& parameter=unit->params[p];
        parameter.base=parameter.bipolar?0:14000;parameter.src=ModSource::SigilA;
        parameter.mod_depth=q15(depth);parameter.mod_enabled=enabled;parameter.snap();
        for(int i=0;i<SIGIL_POINTS;++i)engine->sigils[0].set_point(i,q15(amplitude));
        auto config=engine->sigil_config[0];config.enabled=true;config.legacy=true;
        engine->set_sigil_config(0,config);engine->set_sigil_sync(0,false);engine->reset_sigil(0);
        render(1024);capture();
        ParamInspector d{};engine->capture_param_inspector(0,slot,p,d);
        CHECK(d.valid && d.source==ModSource::SigilA && d.mod_enabled==enabled);
        if(enabled && depth)CHECK(amplitude>0?d.target>d.base:d.target<d.base);
        ++routed_cases;
    }
};
struct Pair {std::vector<u32> top,bottom;int upper=0,lower=0;};
struct AudioHidden {
    platform::AudioOut* saved=g_audio;
    AudioHidden(){g_audio=nullptr;}~AudioHidden(){g_audio=saved;}
};
struct PoisonAfterCapture {
    Scene& scene;std::array<Param,MAX_PARAMS> params{};EngineUiSnapshot snapshot=g_ui_snapshot;
    bool bypass,mute;uint64_t clock=host_time_ms;decltype(g_ui_frame) ui_frame=g_ui_frame;
    explicit PoisonAfterCapture(Scene& s):scene(s),bypass(s.unit->bypass),mute(s.engine->chains[0].mute) {
        std::copy(std::begin(s.unit->params),std::end(s.unit->params),params.begin());
        for(int p=0;p<s.unit->num_params;++p){auto& q=s.unit->params[p];q.base=1234;q.effective=4567;q.target=8901;q.src=ModSource::None;q.mod_enabled=!q.mod_enabled;}
        s.unit->bypass=!bypass;s.engine->chains[0].mute=!mute;g_ui_snapshot={};host_time_ms+=77777;g_ui_frame+=222;
    }
    ~PoisonAfterCapture(){std::copy(params.begin(),params.end(),std::begin(scene.unit->params));scene.unit->bypass=bypass;
        scene.engine->chains[0].mute=mute;g_ui_snapshot=snapshot;host_time_ms=clock;g_ui_frame=ui_frame;}
};
Pair pair(Scene& scene,bool detached=true) {
    Pair result;const int dirty=mutations;const uint64_t clock=scene.engine->transport_frame;
    const auto ring=ring_process_calls,strain=strain_process_calls,rev=rev_process_calls;
    surface(true);{AudioHidden hidden;top_draw();}geometry();result.top=pixels;result.upper=objects;
    surface(false);{AudioHidden hidden;editor_draw();}geometry();result.bottom=pixels;result.lower=objects;
    CHECK(result.upper+result.lower<=8192);peak_objects=std::max(peak_objects,result.upper+result.lower);++dual_cases;
    if(detached) {
        const auto draw_again=[&]() {
            surface(true);{ModuleInspectorNoEngine hidden;top_draw();}geometry();CHECK(pixels==result.top && objects==result.upper);
            surface(false);{ModuleInspectorNoEngine hidden;if(param_lens_is_open())param_lens_draw_bottom();else lower(*scene.unit);}
            geometry();CHECK(pixels==result.bottom && objects==result.lower);
        };
        draw_again();{PoisonAfterCapture poison(scene);draw_again();}++frozen_cases;
    }
    CHECK(mutations==dirty && scene.engine->transport_frame==clock);
    CHECK(ring_process_calls==ring && strain_process_calls==strain && rev_process_calls==rev);
    return result;
}

// Independent declarative stencil oracle. It does not call detail::draw,
// half_width, diamond, pixel:: helpers, or a historical marker implementation.
int half(int row,bool small){static const int full[]={3,2,2,1,1,0},compact[]={2,1,1,0};return small?compact[std::abs(row)]:full[std::abs(row)];}
void primitive_case(int length,int edit,int live,fader::State state,int neutral,bool small,bool vertical) {
    surface(true);const int ox=vertical?180:14,oy=vertical?210:110;
    const auto background=[](int x,int y){return 0xff000000u|u32(17+(x%13))|u32(32+y%17)<<8|u32(51+(x+y)%11)<<16;};
    for(int y=0;y<240;++y)for(int x=0;x<400;++x)pixels[y*400+x]=background(x,y);
    auto expected=pixels;
    const auto point=[&](int u,int v,u32 color){const int x=vertical?ox+v:ox+u,y=vertical?oy-u:oy+v;
        CHECK(x>=0 && x<400 && y>=0 && y<240);expected[y*400+x]=color;};
    edit=std::max(0,std::min(length,edit));live=std::max(0,std::min(length,live));
    const int radius=small?3:5,hole=small?1:2;
    const auto track=palette::subdued(),ink=state.disabled?palette::menu_ghost():palette::ink();
    const auto accent=state.disabled||state.quiet?palette::menu_ghost():palette::spot();
    const auto axis=[&](int first,int last,u32 color){for(int at=std::min(first,last);at<=std::max(first,last);++at)if(std::abs(at-edit)>hole)point(at,0,color);};
    const auto tick=[&](int at,int row){if(std::abs(row)<=radius && std::abs(at-edit)<=half(row,small))return;point(at,row,track);};
    axis(0,length,track);
    for(int side:{-1,1})for(int v=-2;v<=2;++v)tick((side<0?0:length)+side*(2-std::abs(v)),v);
    if(neutral>=0 && neutral<=length)for(int v=-3;v<=3;++v)tick(neutral,v);
    const bool visible=state.linked || edit!=live;
    if(visible && edit!=live)axis(edit,live,accent);
    for(int v=-radius;v<=radius;++v){point(edit-half(v,small),v,ink);point(edit+half(v,small),v,ink);}
    if(visible) {
        if(std::abs(live-edit)<=4)for(int v=-2;v<=2;++v)point(live,v,accent);
        else for(int v=-3;v<=3;++v)for(int u=-half(v,true);u<=half(v,true);++u)point(live+u,v,accent);
    }
    {ModuleInspectorNoEngine hidden;
        if(vertical)fader::vertical(ox,oy-length,oy,oy-edit,oy-live,state,neutral<0?-1:oy-neutral,small);
        else fader::horizontal(ox,ox+length,oy,ox+edit,ox+live,state,neutral<0?-1:ox+neutral,small);
    }
    CHECK(objects>0 && objects<=48 && outside_rects==0 && outside_lines==0);
    CHECK(pixels==expected); // includes every untouched interior and outside pixel
    ++primitive_cases;
}
void primitives() {
    constexpr int lengths[]={34,52,56,60,62,65,70,82,84,102,106,114,118,120,121,126,127,128,129,130,132,138,150,170,186,210,256,271,277,278,284,295,359,372};
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);
        for(bool small:{false,true})for(int n:lengths) {
            const int mid=n/2;
            struct Pose{int edit,live;fader::State state;};
            const Pose poses[]={{0,0,{}},{n,n,{}},{mid,mid,{}},{mid,mid,{true,false,false}},
                {mid,mid+1,{true,false,false}},{mid,mid-4,{true,false,false}},{mid,mid+4,{true,false,false}},
                {mid,mid+5,{true,false,false}},{mid,mid-5,{true,false,false}},
                {0,n,{true,false,false}},{n,0,{false,false,false}},
                {0,n,{true,false,true}},{n,0,{true,true,false}}};
            for(const auto& p:poses)primitive_case(n,p.edit,p.live,p.state,mid,small,false);
            if(n==34||n==52||n==56||n==65||n==70||n==82)
                for(const auto& p:poses)primitive_case(n,p.edit,p.live,p.state,mid,small,true);
        }
        surface(false);const auto empty=pixels;
        fader::horizontal(10,10,80,10,10);fader::vertical(90,90,90,90,90);fader::horizontal_track(100,90,80);
        CHECK(objects==0 && pixels==empty);
        fader::horizontal_track(20,280,90);CHECK(objects<=12 && outside_rects==0 && outside_lines==0);
        for(int x=20;x<=280;++x)CHECK(pixels[90*320+x]==palette::subdued());
        for(auto color:pixels)CHECK(color==0xff000000u || color==palette::subdued());
    }
}

struct Probe {int x0,x1,y;bool bi=false,vertical=false,compact=false;};
Probe probe(UnitKind kind) {
    switch(kind) {
        case UnitKind::Osc:return {14,292,75};case UnitKind::Noise:return {14,152,92};
        case UnitKind::Fm:return {14,150,156};case UnitKind::Motor:return {14,152,169};
        case UnitKind::SamplePlayer:return {16,144,183,true};case UnitKind::Grain:return {118,202,190};
        case UnitKind::MicIn:return {14,152,53};case UnitKind::Recorder:return {14,184,54};
        case UnitKind::Filter:return {18,144,197};case UnitKind::Vca:return {92,148,295,false,true};
        case UnitKind::Delay:return {14,143,63};case UnitKind::Fold:return {14,142,64};
        case UnitKind::Bitcrush:return {14,96,65};case UnitKind::Chorus:return {109,230,43};
        case UnitKind::Stranular:return {12,150,192,true};case UnitKind::Reverb:return {12,150,48};
        case UnitKind::Ghost:return {82,232,54,false,false,true};case UnitKind::Ring:return {14,142,65};
        case UnitKind::Tilt:return {14,132,65,true};case UnitKind::Strain:return {14,142,65};
        case UnitKind::Shift:return {18,302,106,true};case UnitKind::Bird:return {24,152,115};
        case UnitKind::Fract:return {14,146,155};case UnitKind::Comp:return {12,307,97};
        case UnitKind::Mouth:return {32,288,103};case UnitKind::Arp:return {118,200,164};
        case UnitKind::Glitch:return {14,140,129};case UnitKind::Inst:return {20,146,203};
        default:throw std::runtime_error("missing lancet probe");
    }
}
int mapped(q15 value,const Probe& p) {
    const int raw=std::max(p.bi?-32767:0,std::min(32767,int(value)));
    const int v=(raw+(p.bi?32767:0))*(p.x1-p.x0)/(p.bi?65534:32767);
    return p.vertical?p.x1-v:p.x0+v;
}
q15 live_value(Scene& s) {
    const int p=item(s.kind).param;
    if(s.kind==UnitKind::Inst || s.kind==UnitKind::Glitch)return s.unit->params[p].target;
    if(s.kind==UnitKind::Vca){const auto& u=*static_cast<VcaUnit*>(s.unit);return u.visual_ready()?u.visual_effective(p):u.params[p].effective;}
    if(s.kind==UnitKind::Chorus){const auto& u=*static_cast<ChorusUnit*>(s.unit);return u.visual_ready()?u.visual_effective(p):u.params[p].effective;}
    return s.unit->params[p].effective;
}
void probe_live(Scene& s,const Pair& images,bool quiet=false) {
    if(s.kind==UnitKind::SamplePlayer)return; // normal PLAY has text scrubs, no new rail
    const auto p=probe(s.kind);const auto& q=s.unit->params[item(s.kind).param];
    const int base=mapped(q.base,p),live=mapped(live_value(s),p);
    if(base==live && !(q.src!=ModSource::None && q.mod_enabled))return;
    const int at=p.vertical?live*320+p.y:p.y*320+live;
    if(images.bottom[at]!=(quiet?palette::menu_ghost():palette::spot()))
        std::fprintf(stderr,"lancet %s expected live centre %d base %d at %d\n",case_name.c_str(),live,base,at);
    CHECK(images.bottom[at]==(quiet?palette::menu_ghost():palette::spot()));
}
void compositions() {
    std::set<UnitKind> seen;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite})for(const auto& c:catalogue) {
        palette::set(theme);case_name=std::string(c.name)+" / "+(theme==palette::Theme::Pink?"pink":"graphite");
        Scene s(c.kind);seen.insert(c.kind);pair(s);
        for(int extreme:{0,1,2}) {
            for(int p=0;p<s.unit->num_params;++p) {
                if(c.kind==UnitKind::Recorder && p==RecUnit::P_REC)continue;
                s.set(p,q15(extreme==0?(s.unit->params[p].bipolar?-32767:0):extreme==1?(s.unit->params[p].bipolar?0:16384):32767));
            }
            s.render(64);s.capture();pair(s,false);
        }
        s.restore();s.signal(24000);const q15 positive=s.unit->params[c.param].effective;
        auto active=pair(s);probe_live(s,active);
        s.signal(-24000);CHECK(s.unit->params[c.param].effective<positive);probe_live(s,pair(s,false));
        s.signal(24000,0);probe_live(s,pair(s,false)); // enabled zero-depth equality still has live tick
        s.signal(24000,10000,false);pair(s,false); // muted source, preserved depth; no forged range
        s.signal(24000);s.unit->bypass=true;s.capture();probe_live(s,pair(s,false),true);s.unit->bypass=false;
        s.engine->chains[0].mute=true;s.capture();pair(s,false);s.engine->chains[0].mute=false;
        s.restore();s.set(c.param,s.unit->params[c.param].bipolar?q15(-7000):q15(9000));
        const q15 stored=s.unit->params[c.param].base;
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
        CHECK(s.engine->set_step_lock(0,4,s.slot,c.param,24000));s.capture();pair(s);
        CHECK(s.unit->params[c.param].base==stored);g_seq_lock_armed=false;g_view=View::Chains;
        s.restore();s.select(c.param);u32 down=KEY_L;
        CHECK(param_lens_input(down,KEY_L,0,false) && param_lens_is_open());param_lens_touch_release();s.capture();pair(s);
        param_lens_cancel();
    }
    CHECK(seen.size()==28);
}
void alternate_pages() {
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);
        {Scene s(UnitKind::Osc);auto& u=*static_cast<OscUnit*>(s.unit);u.clean=false;
            for(bool morph:{false,true}){u.shape_morph=morph;s.capture();case_name="osc legacy/morph";pair(s);}}
        {Scene s(UnitKind::Fm);auto& u=*static_cast<FmUnit*>(s.unit);
            for(bool ext:{false,true})for(bool drone:{false,true}) {u.set_extended(ext);u.drone=drone;
                for(int p:{FmUnit::P_RATIO_FINE,FmUnit::P_ATTACK,FmUnit::P_DECAY,FmUnit::P_INDEX_FALL,FmUnit::P_FEEDBACK_FALL,FmUnit::P_PITCH_ENV,FmUnit::P_PITCH_TIME}){fm_focus_select(p);s.capture();case_name="fm voice/motion legacy/drone";pair(s);}
                fm_focus_touch_body(u,270,34);fm_focus_release();s.capture();case_name="fm operators";pair(s);}}
        {Scene s(UnitKind::Grain);auto& u=*static_cast<GrainUnit*>(s.unit);
            for(bool clean:{false,true})for(bool sync:{false,true})for(int p:{GrainUnit::P_PITCH,GrainUnit::P_DENS}) {
                u.clean=clean;u.clock_sync=sync;grain_focus_select(p);s.capture();case_name="gran pages legacy/sync";pair(s);}}
        {Scene s(UnitKind::Stranular);stranular_focus_touch_body(*static_cast<StranularUnit*>(s.unit),280,212);stranular_focus_release();
            s.set(StranularUnit::P_SYNC,32767);s.capture();case_name="strn more/sync";pair(s);}
        {Scene s(UnitKind::Comp);for(int p:{CompUnit::P_KNEE,CompUnit::P_HP,CompUnit::P_CLOCK}){comp_focus_select(p);s.capture();case_name="comp detail";pair(s);}}
        {Scene s(UnitKind::Vca);static_cast<VcaUnit*>(s.unit)->set_flexible(false);s.capture();case_name="vca legacy";pair(s);}
        {Scene s(UnitKind::Tilt);static_cast<TiltUnit*>(s.unit)->set_modern(false);s.capture();case_name="tilt legacy rail-only trim";pair(s);}
        {Scene s(UnitKind::Delay);auto& u=*static_cast<DelayUnit*>(s.unit);CHECK(u.set_modern(false) || !u.modern());s.capture();case_name="delay legacy";pair(s);
            CHECK(u.set_modern(true) || u.modern());s.set(DelayUnit::P_SYNC,32767);s.capture();case_name="delay sync division";pair(s);}
        {Scene s(UnitKind::Reverb);for(bool modern:{false,true}) {auto& u=*static_cast<ReverbUnit*>(s.unit);u.set_modern(modern);
                s.set(ReverbUnit::P_FREEZE,32767);s.render(768);s.capture();case_name="rev held legacy/modern";pair(s);}}
        {Scene s(UnitKind::Ghost);s.set(GhostUnit::P_FREEZE,32767);s.render(128);s.capture();case_name="ghost held";pair(s);
            s.set(GhostUnit::P_FREEZE,0);s.select(GhostUnit::P_FINE);s.capture();
            ghost_focus_touch_body(*static_cast<GhostUnit*>(s.unit),288,210);ghost_focus_release();s.capture();case_name="ghost existing fine/depth routing";pair(s);}
        {Scene s(UnitKind::Inst);s.set(InstUnit::P_INSTRUMENT,InstUnit::instrument_value(5));
            for(int n:{36,38,42,45,46,49}){s.set(InstUnit::P_NOTE,InstUnit::note_value(n));s.capture();case_name="inst drums discrete sound / rail-only length";pair(s,false);}}
        {Scene s(UnitKind::Noise);s.set(NoiseUnit::P_MODE,32767);s.capture();case_name="noise drone decay dormant";pair(s);}
    }
}
void future_targets() {
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite})for(UnitKind kind:{UnitKind::Arp,UnitKind::Inst,UnitKind::Glitch}) {
        palette::set(theme);Scene s(kind);case_name=std::string(item(kind).name)+" future target fixture";
        const int p=kind==UnitKind::Arp?int(ArpUnit::P_RATE):kind==UnitKind::Inst?int(InstUnit::P_TONE):int(GlitchUnit::P_DENSITY);
        s.signal(24000);
        // The inspector's future target is recomputed from control + the real
        // accepted sigil value, NOT copied from Param::target. Deliberately keep
        // all three distinct so using either old raw field fails this oracle.
        auto& value=s.unit->params[p];value.base=16000;value.target=7000;value.effective=4000;
        value.src=ModSource::SigilA;value.mod_enabled=true;value.mod_depth=16000;
        ParamInspector next{};s.engine->capture_param_inspector(0,s.slot,p,next);
        CHECK(next.valid && next.target>24000 && next.target!=value.target && next.effective==4000);
        s.select(p);s.capture();const auto images=pair(s);
        const int x=kind==UnitKind::Inst?20:14,y=kind==UnitKind::Arp?164:kind==UnitKind::Inst?203:129;
        const int extent=kind==UnitKind::Arp?82:126;
        CHECK(images.bottom[y*320+x+int(next.target)*extent/32767]==palette::spot());
        CHECK(images.bottom[y*320+x+4000*extent/32767]!=palette::spot());++target_cases;
        if(kind==UnitKind::Arp) {
            auto& gate=s.unit->params[ArpUnit::P_GATE];gate.base=16000;gate.target=7000;gate.effective=4000;
            gate.src=ModSource::SigilA;gate.mod_enabled=true;gate.mod_depth=16000;
            ParamInspector gate_next{};s.engine->capture_param_inspector(0,s.slot,ArpUnit::P_GATE,gate_next);
            CHECK(gate_next.valid && gate_next.target>24000 && gate_next.effective==4000);
            s.select(ArpUnit::P_GATE);s.capture();const auto gate_images=pair(s);
            CHECK(gate_images.bottom[164*320+118+4000*82/32767]==palette::spot());
            CHECK(gate_images.bottom[164*320+118+int(gate_next.target)*82/32767]!=palette::spot());++target_cases;
        }
    }
}
void glyph_preservation() {
    // Saved actual text pixels are checked after every later graphic submission.
    // This catches fader-over-text collisions that text bounding boxes miss.
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite})for(UnitKind kind:{UnitKind::Grain,UnitKind::Osc,UnitKind::Ghost}) {
        palette::set(theme);Scene s(kind);
        if(kind==UnitKind::Osc)static_cast<OscUnit*>(s.unit)->clean=false;
        const int steps=kind==UnitKind::Grain?42:kind==UnitKind::Osc?70:38;
        for(int i=0;i<=steps;++i) {
            const q15 raw=q15(i*32767/steps);
            if(kind==UnitKind::Grain)for(int p:{GrainUnit::P_PITCH,GrainUnit::P_GAIN,GrainUnit::P_SCAN})s.set(p,raw);
            else if(kind==UnitKind::Osc)s.set(OscUnit::P_SHAPE,raw);
            else {s.set(GhostUnit::P_TONE,raw);s.set(GhostUnit::P_FINE,q15(-32767+i*65534/steps));}
            s.capture();case_name=std::string(item(kind).name)+" glyph isolation "+std::to_string(i);
            surface(false,true);{AudioHidden hidden;editor_draw();}record_glyphs=false;geometry();
            int checked=0;
            for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
                const bool region=kind==UnitKind::Grain?y>=211:kind==UnitKind::Osc?y>=145&&y<=163:y>=40&&y<=81;
                const int at=y*320+x;if(!region || !glyph_mask[at])continue;
                if(pixels[at]!=glyph_ink[at])std::fprintf(stderr,"lancet glyph overwritten: %s x%d y%d\n",case_name.c_str(),x,y);
                CHECK(pixels[at]==glyph_ink[at]);++checked;
            }
            CHECK(checked>0);++glyph_cases;
        }
    }
}
int run() {
    primitive_cases=dual_cases=frozen_cases=glyph_cases=routed_cases=target_cases=rendered_frames=0;peak_objects=0;
    int total=0,failed=0;sample_fixture();
    const auto group=[&](const char* name,auto test){++total;try{test();std::printf("ok: lancet %s\n",name);}
        catch(const std::exception& e){++failed;record_glyphs=false;std::fprintf(stderr,"FAIL: lancet %s [%s]: %s\n",name,case_name.c_str(),e.what());}};
    group("native stencil / transparent holes / endpoints / equal-near / disabled-quiet / both themes",primitives);
    group("all 28 real editor/top / actual sigil resolve / locks / shared lens / detached frozen frames",compositions);
    group("alternate frozen pages / legacy-held-dormant / ARP effective vs INST-GLITCH targets",[]{alternate_pages();future_targets();});
    group("native glyph pixel isolation at compact rail boundaries",glyph_preservation);
    theme_test::select();
    std::printf("lancet UI: %d/%d groups; %llu primitive cases; %llu duals; %llu detached frozen pairs; %llu routed captures; %llu target fixtures; %llu glyph poses; %llu engine frames; shared peak %d/8192\n",
        total-failed,total,(unsigned long long)primitive_cases,(unsigned long long)dual_cases,(unsigned long long)frozen_cases,
        (unsigned long long)routed_cases,(unsigned long long)target_cases,(unsigned long long)glyph_cases,(unsigned long long)rendered_frames,peak_objects);
    std::puts("host native C++ draw proof only; not hardware coverage/FPS/sound; no unrelated regression groups or audio/project exports");
    return failed?1:0;
}
int capture(const char* directory) {
    try {
        const std::filesystem::path out(directory);CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
        sample_fixture();palette::set(palette::Theme::Graphite);host_time_ms=1000;peak_objects=0;
        std::ofstream manifest(out/"manifest.tsv");manifest<<"frame\tmodule\ttop\tbottom\tshared\n";
        unsigned count=0;
        const auto save=[&](Scene& scene,const std::string& name) {
            scene.capture();case_name=name;const auto images=pair(scene,false);
            width=400;pixels=images.top;dump_surface(out,name+"_top");
            width=320;pixels=images.bottom;dump_surface(out,name+"_bottom");
            manifest<<name<<'\t'<<item(scene.kind).name<<'\t'<<images.upper<<'\t'<<images.lower<<'\t'<<images.upper+images.lower<<'\n';++count;
        };
        for(size_t i=0;i<sizeof catalogue/sizeof catalogue[0];++i) {
            const auto& c=catalogue[i];Scene scene(c.kind);scene.signal(24000);
            char name[64];std::snprintf(name,sizeof name,"%02u_%s",unsigned(i+1),c.name);save(scene,name);
            if(c.kind==UnitKind::Fm){fm_focus_select(FmUnit::P_PITCH_ENV);save(scene,"03_fm_motion");}
            if(c.kind==UnitKind::Grain){grain_focus_select(GrainUnit::P_DENS);save(scene,"06_gran_cloud");}
            if(c.kind==UnitKind::Stranular){stranular_focus_touch_body(*static_cast<StranularUnit*>(scene.unit),280,212);stranular_focus_release();save(scene,"15_strn_more");}
            if(c.kind==UnitKind::Comp){comp_focus_select(CompUnit::P_KNEE);save(scene,"24_comp_detail");}
            if(c.kind==UnitKind::Inst){scene.set(InstUnit::P_INSTRUMENT,InstUnit::instrument_value(5));save(scene,"28_inst_drums");}
        }
        {Scene scene(UnitKind::SamplePlayer);g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=4;
            CHECK(scene.engine->set_step_lock(0,4,scene.slot,PlayUnit::P_SPEED,13000));save(scene,"05_play_lock");}
        {Scene scene(UnitKind::Shift);scene.signal(-24000);u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false));
            param_lens_touch_release();save(scene,"21_shift_lens");}
        manifest.close();std::ofstream receipt(out/"capture.json");
        receipt<<"{\n  \"provenance\": \"fresh actual current C++ editor/top/engine through native host raster; no authored UI pixels\",\n"
               <<"  \"modules\": 28, \"pairs\": "<<count<<", \"regression_groups_run\": 0,\n"
               <<"  \"max_shared_objects\": "<<peak_objects<<", \"budget\": 8192,\n"
               <<"  \"theme\": \"graphite\", \"top\": [400,240], \"bottom\": [320,240],\n"
               <<"  \"audio\": \"private engine frames feed UI scalars; discarded, no sound claim or audio export\",\n"
               <<"  \"hardware\": false\n}\n";
        std::printf("lancet captures: %u fresh native C++ duals / 28 modules; 0 regression groups; shared peak %d/8192\n",count,peak_objects);return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"lancet capture [%s]: %s\n",case_name.c_str(),e.what());return 1;}
}
} // namespace
} // namespace fader_lancet_cases
