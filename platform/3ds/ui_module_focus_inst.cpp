#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_sequencer.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "note_display.h"
#include "inspector_gothic.h"
#include "inst_readout.h"
#include "inst_model_heading.h"
#include "../../core/engine/inst_scope.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using I = InstUnit;
constexpr const char* NAMES[I::P_COUNT] = {"instrument","note","length","attack","release","body","level",
    "decay","hardness","position","damping","strength"};
constexpr const char* FAMILIES[I::INSTRUMENT_COUNT] = {
    "sample / keys", "sample / bowed", "sample / brass",
    "sample / wind", "sample / bass", "sample / drums", "struck / metal"
};
constexpr int STROKES = 544;
enum Ink : uint8_t { Rail, Ghost, Solid, Accent };
// x is relative to 128: the figure never reaches the note/parameter column.
struct Stroke { uint8_t x0,y0,x1,y1,ink; };
struct Frame {
    bool valid=false, bypass=false, mute=false, shadowed=false, gated=false;
    bool lens=false, queued=false, audible=false;
    uint8_t chain=0, slot=0, selected=I::P_RELEASE, instrument=0, hero=0, note=60;
    int8_t voice=-1;
    uint8_t kinds[UNITS_PER_CHAIN]={};
    q15 edit[I::P_COUNT]={};
    ParamInspector routes[I::P_COUNT]={};
    I::Visual visual{};
    I::ModelVisual model{};
    int8_t model_voice=-1;
    bool env=false;
    uint16_t horizon_ms=2000;
    uint16_t progress=0;
    uint32_t scope_age=0;
    char values[I::P_COUNT][16]={}, route[64]={}, status[24]={};
    uint16_t strokes=0;
    Stroke ink[STROKES]={};
} frame;
// One UI scratch copy only; draw consumes projected strokes, never live PCM.
q15 scope_pcm[inst_scope::SAMPLE_COUNT]{};
static_assert(sizeof(Frame)<=4096 && std::is_trivially_copyable<Frame>::value,
              "INST bounded UI-only voice/geometry DTO; no engine or PCM in draw");
int selected=I::P_HARDNESS;
bool envelope_page=false;
int last_bank=0;
bool blocked_frame=false;
enum class Action : uint8_t { None, Previous, Next, Bypass, Back, Play, Mod, Bank, Model, Sound, Env };
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    int chain=-1,slot=-1,param=-1;
    int x=0,y=0,x0=0,y0=0,w=0,h=0;
    q15 value=0,value2=0,instrument=0;
    View view=View::Chains;
    Action action=Action::None;
    bool bypass=false,env=false,surface=false;
} gesture;
static_assert(sizeof(Gesture)<=112,"small INST input identity/capture");
int clamp(int v,int lo,int hi) {return std::max(lo,std::min(hi,v));}
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        g_view==View::Chains &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
q15 edit_value(const I& u,int p) {
    return u.params[p].base;
}
void change(I& u,int p,q15 value) {
    if(edit_value(u,p)==value)return;
    u.params[p].base=value;
    mark_project_mutated();
}
bool owns(const I& u) {
    return context() && gesture.unit==uintptr_t(&u) && gesture.engine==g_eng &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot && gesture.view==g_view &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u &&
        gesture.bypass==u.bypass && gesture.env==envelope_page && (gesture.param==I::P_INSTRUMENT ||
            gesture.instrument==edit_value(u,I::P_INSTRUMENT));
}
void start(I& u,int p,int tx,int ty,Action action=Action::None,int x=0,int y=0,int w=0,int h=0) {
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.view=g_view;
    gesture.param=p;
    gesture.x=tx;gesture.y=ty;gesture.value=p>=0?edit_value(u,p):0;
    gesture.instrument=edit_value(u,I::P_INSTRUMENT);gesture.bypass=u.bypass;gesture.env=envelope_page;
    gesture.action=action;gesture.x0=x;gesture.y0=y;gesture.w=w;gesture.h=h;
    if(p>=0)selected=p;
    g_drag=Drag::InstParam;
}
bool model_edit(const I& u) {return I::instrument_index(edit_value(u,I::P_INSTRUMENT))==I::MODEL_INSTRUMENT;}
void change_surface(I& u,q15 position,q15 hardness) {
    change(u,I::P_POSITION,position);change(u,I::P_HARDNESS,hardness);
}
struct ModelField {int param,x,y,width;};
constexpr ModelField sound_fields[]={{I::P_NOTE,16,92,128},{I::P_DECAY,176,92,128},
    {I::P_BODY,16,194,128},{I::P_LEVEL,176,194,128}};
constexpr ModelField env_fields[]={{I::P_DECAY,16,92,128},{I::P_DAMPING,176,92,128},
    {I::P_STRENGTH,16,128,128},{I::P_LEVEL,176,128,128},
    {I::P_ATTACK,16,182,88},{I::P_LENGTH,116,182,88},{I::P_RELEASE,216,182,88}};
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
int px(q15 v,int x) {return x+clamp(v,0,32767)*126/32767;}
int note_px(q15 v,int x) {
    if(frame.instrument==5)return x+I::drum_index(I::note_midi(v))*126/(I::DRUM_COUNT-1);
    return px(v,x);
}
void model_field(const ModelField& f) {
    const int p=f.param;
    const auto& route=frame.routes[p];
    const u32 color=frame.selected==p?m::spot():m::ink();
    ui::text(f.x,f.y,NAMES[p],color);
    const bool compact=f.width<100;
    if(compact)ui::text(f.x,f.y+13,frame.values[p],m::ghost());
    else right(f.x+f.width,f.y,frame.values[p],color);
    const int a=f.x+4,b=f.x+f.width-4,rail=compact?213:f.y+17;
    const auto position=[&](q15 v){return a+clamp(v,0,32767)*(b-a)/32767;};
    module_fader::horizontal(a,b,rail,position(frame.edit[p]),position(route.target),
        {route.source!=ModSource::None && route.mod_enabled,false,frame.bypass||frame.mute},-1,compact);
}
void field(int i) {
    const int p=i+1,x=16+(i%2)*152,y=108+(i/2)*38,ry=y+19;
    const bool disabled=frame.instrument==5 && p==I::P_LENGTH;
    const auto& d=frame.routes[p];
    const u32 color=disabled?m::ghost():frame.selected==p?m::spot():m::ink();
    ui::text(x,y,p==I::P_NOTE && frame.instrument==5?"sound":p==I::P_TONE?"tone":NAMES[p],color);
    right(x+134,y,disabled?"one-shot":frame.values[p],color);
    if(d.source!=ModSource::None) {
        char source[2]={'e',0};const int s=mod_source_sigil(d.source);
        if(s>=0)source[0]=char('a'+s);
        ui::text(x+74,y,source,d.mod_enabled&&!disabled?m::spot():m::ghost());
    }
    const int rx=x+4;
    const auto pos=[&](q15 v){return p==I::P_NOTE?note_px(v,rx):px(v,rx);};
    if(disabled){module_fader::horizontal_track(rx,rx+126,ry);return;}
    const int stored=pos(frame.edit[p]),next=pos(d.target);
    module_fader::horizontal(rx,rx+126,ry,stored,next,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute});
}
// Bounded geometric metaphors derived only from the copied accepted voice.
// Traces are NOT additional voices, PCM samples, harmonics or a spectrum.
struct Point {float x,y;};
void line(Point a,Point b,Ink role) {
    if(frame.strokes>=STROKES)return;
    auto x=[](float v){return uint8_t(clamp(int(std::lround(v)),128,383)-128);};
    auto y=[](float v){return uint8_t(clamp(int(std::lround(v)),60,217));};
    frame.ink[frame.strokes++]={x(a.x),y(a.y),x(b.x),y(b.y),uint8_t(role)};
}
constexpr float PI=3.14159265358979323846f;
void prepare_model_figure() {
    // Predictive natural resonance envelopes, NOT PCM, spectrum or extra voices.
    // Active traits come from the accepted strike, never later control edits.
    const auto* voice=frame.model_voice<0?nullptr:&frame.model.voices[frame.model_voice];
    const float decay=voice?voice->decay_ms:I::decay_ms(frame.edit[I::P_DECAY]);
    const float damping=(voice?voice->damping:frame.edit[I::P_DAMPING])/32767.f;
    const float body=(voice?voice->body:frame.edit[I::P_BODY])/32767.f;
    const float hardness=(voice?voice->hardness:frame.edit[I::P_HARDNESS])/32767.f;
    const float pos=.06f+.88f*(voice?voice->position:frame.edit[I::P_POSITION])/32767.f;
    const float strength=(voice?voice->strength:frame.edit[I::P_STRENGTH])/32767.f;
    const float f0=440*std::pow(2.f,(frame.note-69)/12.f),cutoff=950+12500*hardness*hardness;
    constexpr float bar[]={1,4,10,18.5f,28.5f,40},bell[]={1,2.706f,3.84f,5.31f,6.74f,8.82f};
    constexpr float gains[]={1,.48f,.31f,.22f,.16f,.12f};
    frame.horizon_ms=uint16_t(clamp(int(decay),120,6000));
    frame.progress=voice?uint16_t(clamp(int(uint64_t(voice->age)*1000/ENGINE_SR*246/frame.horizon_ms),0,246)):0;
    for(int k=5;k>=0;--k) {
        const float ratio=bar[k]+(bell[k]-bar[k])*body,hz=f0*ratio;
        if(hz>=ENGINE_SR*.45f)continue;
        const float weight=std::abs(std::sin(PI*(k+1)*pos))*gains[k]*strength/
            (1+(hz/cutoff)*(hz/cutoff))*std::min(1.f,(ENGINE_SR*.45f-hz)/(ENGINE_SR*.05f));
        const float t60=decay*.001f/(1+damping*.23f*(ratio-1));
        Point previous{137,207};
        for(int j=1;j<=48;++j) {
            const float u=j/48.f,t=frame.horizon_ms*.001f*u;
            const float amplitude=weight*(1-std::exp(-70*t))*std::exp(-std::log(1000.f)*t/t60);
            const Point next{137+246*u,207-126*std::cbrt(amplitude)*std::pow(1-u,.55f)};
            line(previous,next,k==0?Solid:k==1?Accent:k==2?Ghost:Rail);previous=next;
        }
    }
}
void prepare_scope_figure() {
    // 64ms of this unit's actual pre-FX mono output, not a synthetic waveform.
    // One fixed linear gain across all families; min/max preserves column peaks.
    constexpr int left=137,cy=139,columns=246;
    const auto y=[](int v){return float(clamp(139-v*256/32768,85,193));};
    int previous=cy;
    for(int x=0;x<columns;++x) {
        int low=0,high=0,sum=0,count=0;
        for(int i=x*inst_scope::SAMPLE_COUNT/columns;i<(x+1)*inst_scope::SAMPLE_COUNT/columns;++i) {
            const int v=scope_pcm[i];
            if(!count)low=high=v;else {low=std::min(low,v);high=std::max(high,v);}
            sum+=v;++count;
        }
        line({float(left+x),y(high)},{float(left+x),y(low)},Ghost);
        const int mean=int(y(sum/count));
        if(x)line({float(left+x-1),float(previous)},{float(left+x),float(mean)},Solid);
        previous=mean;
    }
    // Accent is the most recent 8ms of the SAME signal, not another voice.
    for(int x=215;x<columns;++x) {
        const int i=x*inst_scope::SAMPLE_COUNT/columns;
        const int j=(x+1)*inst_scope::SAMPLE_COUNT/columns;
        line({float(left+x),y(scope_pcm[i])},{float(left+x+1),y(scope_pcm[std::min(j,inst_scope::SAMPLE_COUNT-1)])},Accent);
    }
}
void prepare_figure() {
    frame.strokes=0;
    if(frame.instrument==I::MODEL_INSTRUMENT)prepare_model_figure();
    else prepare_scope_figure();
}
void glyph(int x,int baseline,const note_display::Glyph& g,int scale,u32 color) {
    const int stride=(g.width+7)/8;
    for(int y=0;y<g.height;++y) {
        int run=-1;
        for(int col=0;col<=g.width;++col) {
            const bool on=col<g.width && (note_display::bitmap[g.offset+y*stride+col/8]&(0x80u>>(col&7)));
            if(on && run<0)run=col;
            if(!on && run>=0) {ui::rect(x+run*scale,baseline+(g.top+y)*scale,(col-run)*scale,scale,color);run=-1;}
        }
    }
}
void hero_note() {
    if(frame.hero==5) {
        const char* name=I::drum_name(I::drum_index(frame.note));
        char hero[24];std::snprintf(hero,sizeof hero,"inst-%s",name);
        if(m::heading_width(hero)<=108)m::heading(16,80,hero,m::ink());
        else m::heading(16,86,name,m::ink());
        return;
    }
    char note[8];inst_readout::note_text(note,sizeof note,frame.note);
    const auto* letter=note_display::find(note[0]);const bool sharp=note[1]=='#';
    const auto* octave=note_display::find(note[sharp?2:1],true);
    if(!letter || !octave)return;
    // Tall b/f/g and sharps must not cross the note label or the state caption.
    const int scale=!sharp && letter->height<=30?2:1;
    glyph(16,72-letter->top*scale,*letter,scale,m::ink());
    int x=16+letter->width*scale;
    if(sharp){note_display::sharp(x+5,87,m::spot());x+=19;}
    glyph(x+6*scale,122,*octave,scale,m::spot());
}
} // namespace

int inst_focus_selected() {return selected;}
void inst_focus_select(int p) {
    if(p<0 || p>=I::P_COUNT)return;
    selected=p;
    if(p==I::P_ATTACK || p==I::P_LENGTH || p==I::P_RELEASE || p==I::P_DAMPING || p==I::P_STRENGTH)envelope_page=true;
    else if(p==I::P_NOTE || p==I::P_HARDNESS || p==I::P_POSITION || p==I::P_BODY)envelope_page=false;
}
void inst_focus_release() {gesture={};}
void inst_focus_input(u32 down,bool blocked) {
    blocked_frame=blocked || (down&~(KEY_TOUCH|KEY_ZL));
    if(blocked_frame || !context())inst_focus_release();
}
bool inst_focus_touch_body(I& u,int tx,int ty) {
    if(!context() || param_lens_is_open() || blocked_frame)return false;
    platform::AudioOut::Guard guard(*g_audio);
    inst_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&u)return false;
    const auto button=[&](Action a,int x,int y,int w,int h,int p=-1){
        if(!hit(tx,ty,x,y,w,h))return false;
        start(u,p,tx,ty,a,x,y,w,h);return true;
    };
    if(hit(tx,ty,266,5,48,31)) {
        button(Action::Bypass,266,5,48,31);
        return true;
    }
    if(button(Action::Bank,12,39,42,18,I::P_INSTRUMENT) ||
       button(Action::Model,54,39,60,18,I::P_INSTRUMENT))return true;
    if(model_edit(u)) {
        if(button(Action::Sound,196,39,52,18) || button(Action::Env,266,39,44,18))return true;
        const auto field_touch=[&](const ModelField& f) {
            if(!hit(tx,ty,f.x-4,f.y-4,f.width+8,f.width<100?38:30))return false;
            selected=f.param;
            start(u,f.param,tx,ty);return true;
        };
        if(envelope_page) {for(const auto& f:env_fields)if(field_touch(f))return true;}
        else {
            for(const auto& f:sound_fields)if(field_touch(f))return true;
            for(int p:{I::P_HARDNESS,I::P_POSITION}) {
                const int x=p==I::P_HARDNESS?12:172;
                if(hit(tx,ty,x,120,136,15)) {
                    selected=p;
                    start(u,p,tx,ty);return true;
                }
            }
            if(hit(tx,ty,16,138,288,47)) {
                start(u,I::P_POSITION,tx,ty);gesture.surface=true;
                gesture.value2=edit_value(u,I::P_HARDNESS);selected=I::P_HARDNESS;return true;
            }
        }
        if(button(Action::Back,8,222,80,18) || button(Action::Play,112,222,90,18))return true;
        button(Action::Mod,218,222,96,18);
        return true;
    }
    if(button(Action::Previous,8,58,28,38,I::P_INSTRUMENT) ||
       button(Action::Next,280,58,32,38,I::P_INSTRUMENT)) {selected=I::P_INSTRUMENT;return true;}
    if(hit(tx,ty,36,58,244,38)) {selected=I::P_INSTRUMENT;start(u,selected,tx,ty);return true;}
    for(int i=0;i<6;++i) {
        const int p=i+1,x=16+(i%2)*152,y=108+(i/2)*38;
        if(!hit(tx,ty,x-4,y-4,142,35))continue;
        selected=p;
        if(p==I::P_LENGTH && I::instrument_index(edit_value(u,I::P_INSTRUMENT))==5)return true;
        start(u,p,tx,ty);return true;
    }
    if(button(Action::Back,8,222,80,18) || button(Action::Play,112,222,90,18))return true;
    button(Action::Mod,218,222,96,18);
    return true;
}
void inst_focus_drag(I& u,int tx,int ty) {
    if(!context() || param_lens_is_open() || blocked_frame) {inst_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!gesture.unit || g_drag!=Drag::InstParam || !owns(u)) {inst_focus_release();return;}
    if(gesture.action!=Action::None) {
        if(!hit(tx,ty,gesture.x0,gesture.y0,gesture.w,gesture.h) ||
           std::abs(tx-gesture.x)>6 || std::abs(ty-gesture.y)>6)inst_focus_release();
        return;
    }
    const int p=gesture.param,dx=tx-gesture.x;
    if(gesture.surface) {
        const int dy=ty-gesture.y;
        change_surface(u,q15(clamp(int(gesture.value)+dx*32767/272,0,32767)),
            q15(clamp(int(gesture.value2)-dy*32767/34,0,32767)));
        selected=std::abs(dx)>std::abs(dy)*4?I::P_POSITION:I::P_HARDNESS;
        return;
    }
    q15 value=gesture.value;
    if(p==I::P_INSTRUMENT) {
        if(dx/40)value=I::instrument_value(clamp(I::instrument_index(value)+dx/40,0,I::SAMPLE_INSTRUMENT_COUNT-1));
    } else if(p==I::P_NOTE) {
        if(I::instrument_index(gesture.instrument)==5) {
            if(dx/20)value=I::note_value(I::drum_note(clamp(I::drum_index(I::note_midi(value))+dx/20,0,I::DRUM_COUNT-1)));
        } else if(dx/4)value=I::note_value(clamp(I::note_midi(value)+dx/4,24,96));
    } else if(p>=0) {
        const int rail=model_edit(u)?(envelope_page && (p==I::P_ATTACK || p==I::P_LENGTH || p==I::P_RELEASE)?80:120):126;
        value=q15(clamp(int(value)+dx*32767/rail,0,32767));
    }
    if(p>=0)change(u,p,value);
}
void inst_focus_touch_release() {
    if(!gesture.unit)return;
    Action action=Action::None;
    if(context() && !blocked_frame && !param_lens_is_open()) {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* unit=g_eng->chains[g_edit_chain].units[g_edit_slot];
        if(unit && unit->kind==UnitKind::Inst && g_drag==Drag::InstParam) {
            auto& u=*static_cast<I*>(unit);
            if(owns(u) && (gesture.action==Action::None || gesture.param<0 || edit_value(u,gesture.param)==gesture.value)) {
                action=gesture.action;
                if(action==Action::Previous || action==Action::Next) {
                    const int next=(I::instrument_index(gesture.value)+I::SAMPLE_INSTRUMENT_COUNT+
                        (action==Action::Next?1:-1))%I::SAMPLE_INSTRUMENT_COUNT;
                    selected=I::P_INSTRUMENT;change(u,I::P_INSTRUMENT,I::instrument_value(next));
                } else if(action==Action::Bank || action==Action::Model) {
                    const int source=action==Action::Model?I::MODEL_INSTRUMENT:last_bank;
                    change(u,I::P_INSTRUMENT,I::instrument_value(source));
                    selected=source==I::MODEL_INSTRUMENT?I::P_HARDNESS:I::P_INSTRUMENT;
                    envelope_page=false;
                } else if(action==Action::Sound || action==Action::Env) {
                    envelope_page=action==Action::Env;
                    selected=envelope_page?I::P_DECAY:I::P_HARDNESS;
                } else if(action==Action::Bypass) {
                    u.bypass=!u.bypass;mark_project_mutated();
                } else if(action==Action::Play && !u.bypass && !g_eng->chains[g_edit_chain].mute) {
                    g_eng->request_chain_trigger(g_edit_chain,g_eng->launch_quantize);
                }
            }
        }
    }
    inst_focus_release();
    if(action==Action::Back) {
        g_overlay=Overlay::None;g_seq_lock_flash=0;g_drag=Drag::None;
    } else if(action==Action::Mod) {
        // Deliberately outside our guard; the shared controller owns its own guard.
        param_lens_touch_down(250,232);
    }
}
void inst_focus_capture() {
    frame={};
    if(!context()) {
        if(g_audio) {platform::AudioOut::Guard guard(*g_audio);inst_scope::recorder.select(nullptr);}
        inst_focus_release();return;
    }
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Inst) {inst_scope::recorder.select(nullptr);inst_focus_release();return;}
        const auto& u=*static_cast<const I*>(unit);
        if(gesture.unit && (!owns(u) || param_lens_is_open() || g_drag!=Drag::InstParam))inst_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;
        frame.lens=false;
        frame.chain=uint8_t(g_edit_chain);frame.slot=uint8_t(g_edit_slot);frame.selected=uint8_t(selected);
        frame.queued=(g_eng->pending_triggers&(1u<<g_edit_chain))!=0;
        frame.gated=u.arp_voice.owner>=0 && !u.arp_voice.gate;
        frame.visual=u.visual();frame.model=u.model_visual();frame.env=envelope_page;
        for(int p=0;p<I::P_COUNT;++p) {
            frame.edit[p]=edit_value(u,p);
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
        const bool sample=I::instrument_index(frame.edit[I::P_INSTRUMENT])!=I::MODEL_INSTRUMENT;
        const bool observe=sample && !frame.bypass && !frame.mute && !frame.gated;
        inst_scope::recorder.select(observe?&u:nullptr);
        frame.scope_age=inst_scope::recorder.copy(observe?&u:nullptr,scope_pcm);
        for(int s=0;s<UNITS_PER_CHAIN;++s) {
            const auto* next=chain.units[s];frame.kinds[s]=uint8_t(next?next->kind:UnitKind::Empty);
            if(s>g_edit_slot && next && !next->bypass && next->sequencer_properties().source)frame.shadowed=true;
        }
    }
    // All formatting/PCM reduction is outside the audio guard; no engine advancement.
    frame.instrument=uint8_t(I::instrument_index(frame.edit[I::P_INSTRUMENT]));
    frame.hero=uint8_t(I::instrument_index(frame.routes[I::P_INSTRUMENT].target));
    frame.note=uint8_t(I::note_midi(frame.routes[I::P_NOTE].target));
    if(frame.instrument<I::SAMPLE_INSTRUMENT_COUNT)last_bank=frame.instrument;
    for(int i=0;i<I::MODEL_VOICE_COUNT;++i) {
        const auto& v=frame.model.voices[i];
        if(v.active && (frame.model_voice<0 || v.age<frame.model.voices[frame.model_voice].age))frame.model_voice=int8_t(i);
    }
    for(int i=0;i<I::VOICE_COUNT;++i) {
        const auto& v=frame.visual.voices[i];
        if(v.stage!=uint8_t(I::Stage::Off) && (frame.voice<0 || v.age<frame.visual.voices[frame.voice].age))frame.voice=int8_t(i);
    }
    if(frame.instrument==I::MODEL_INSTRUMENT) {
        frame.hero=I::MODEL_INSTRUMENT;
        if(frame.model_voice>=0)frame.note=frame.model.voices[frame.model_voice].note;
    } else if(frame.voice>=0) {frame.hero=frame.visual.voices[frame.voice].instrument;frame.note=frame.visual.voices[frame.voice].note;}
    frame.audible=(frame.instrument==I::MODEL_INSTRUMENT?frame.model_voice>=0:frame.voice>=0) && !frame.bypass && !frame.mute && !frame.shadowed && !frame.gated;
    for(int p=0;p<I::P_COUNT;++p) {
        inst_readout::value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
        const unsigned ms=p==I::P_LENGTH?I::length_ms(frame.edit[p]):p==I::P_ATTACK?I::attack_ms(frame.edit[p]):
            p==I::P_RELEASE?I::release_ms(frame.edit[p]):p==I::P_DECAY?I::decay_ms(frame.edit[p]):0;
        // Four full-size digits plus 'ms' would enter the figure column.
        if(ms>=1000)std::snprintf(frame.values[p],sizeof frame.values[p],"%u.%02us",ms/1000,(ms%1000)/10);
    }
    if(frame.instrument==5)std::snprintf(frame.values[I::P_NOTE],sizeof frame.values[0],"%s",I::drum_name(I::drum_index(I::note_midi(frame.edit[I::P_NOTE]))));
    const char* state=frame.bypass?"bypassed":frame.mute?"chain muted":frame.shadowed?"later source":frame.gated?"arp gap":nullptr;
    if(state)std::snprintf(frame.status,sizeof frame.status,"%s",state);
    else {
        const bool model=frame.instrument==I::MODEL_INSTRUMENT;
        const uint8_t stage=model?(frame.model_voice<0?0:frame.model.voices[frame.model_voice].stage):
            (frame.voice<0?uint8_t(I::Stage::Off):frame.visual.voices[frame.voice].stage);
        std::snprintf(frame.status,sizeof frame.status,"%s / %02u",stage==uint8_t(I::Stage::Off)?"ready":
            stage==uint8_t(I::Stage::Attack)?"attack":stage==uint8_t(I::Stage::Release)?"tail":frame.hero==5?"hit":model?"ring":"sustain",unsigned(model?frame.model.active:frame.visual.active));
        if(model && !stage)std::snprintf(frame.status,sizeof frame.status,"ready / model");
    }
    int at=0;
    for(uint8_t kind:frame.kinds)if(kind!=uint8_t(UnitKind::Empty)) {
        const int n=std::snprintf(frame.route+at,sizeof frame.route-size_t(at),"%s%s",at?" / ":"",m::module_name(UnitKind(kind)));
        at=std::min(int(sizeof frame.route)-1,at+std::max(0,n));
    }
    prepare_figure();
}
namespace {
void source_tabs() {
    const bool model=frame.instrument==I::MODEL_INSTRUMENT;
    ui::text(16,43,"bank",model?m::ghost():m::spot());ui::text(58,43,"model",model?m::spot():m::ghost());
    ui::line(model?58:16,54,model?86:38,54,m::spot());
    if(model) {
        ui::text(202,43,"sound",frame.env?m::ghost():m::spot());
        ui::text(274,43,"env",frame.env?m::spot():m::ghost());
        ui::line(frame.env?274:202,54,frame.env?290:230,54,m::spot());
    }
}
void model_surface() {
    ui::text(16,124,"hardness",frame.selected==I::P_HARDNESS?m::spot():m::ink());
    right(144,124,frame.values[I::P_HARDNESS],m::spot());
    ui::text(176,124,"position",frame.selected==I::P_POSITION?m::spot():m::ink());
    right(304,124,frame.values[I::P_POSITION],m::ink());
    const int cx=24+clamp(frame.edit[I::P_POSITION],0,32767)*272/32767;
    const int cy=172-clamp(frame.edit[I::P_HARDNESS],0,32767)*32/32767;
    ui::line(20,140,20,172,m::branch());
    ui::line(18,140,22,140,m::ghost());ui::line(18,172,22,172,m::ghost());ui::line(18,cy,22,cy,m::spot());
    const Point body[]={{20,176},{23,174},{297,174},{300,176},{297,179},{23,179},{20,176}};
    for(int i=1;i<7;++i)ui::line(body[i-1].x,body[i-1].y,body[i].x,body[i].y,m::ghost());
    for(int x:{80,240}){ui::line(x-3,183,x,180,m::branch());ui::line(x,180,x+3,183,m::branch());}
    ui::line(cx+4,cy-4,cx+13,std::max(136,cy-13),m::ghost());
    const Point head[]={{float(cx-4),float(cy-2)},{float(cx-2),float(cy-4)},{float(cx+2),float(cy-4)},
        {float(cx+4),float(cy-2)},{float(cx+4),float(cy+2)},{float(cx+2),float(cy+4)},
        {float(cx-2),float(cy+4)},{float(cx-4),float(cy+2)},{float(cx-4),float(cy-2)}};
    for(int i=1;i<9;++i)ui::line(head[i-1].x,head[i-1].y,head[i].x,head[i].y,m::spot());
    for(int y=cy+7;y<174;y+=3)ui::rect(cx,y,1,1,m::branch());
    ui::line(cx-4,174,cx+4,174,m::spot());
    const auto& route=frame.routes[I::P_POSITION];
    if(route.source!=ModSource::None && route.mod_enabled) {
        const int live=24+clamp(route.target,0,32767)*272/32767;ui::line(live,180,live,183,m::spot());
    }
}
}
void inst_focus_draw(const I&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Inst,m::spot());
    char text[32];std::snprintf(text,sizeof text,"ch%02u / %02u",unsigned(frame.chain+1),unsigned(frame.slot+1));
    ui::text(76,m::kHeaderTextY,text,m::ghost());right(m::right_edge(320),m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(m::right_edge(320)-18,m::kHeaderMarkY,m::right_edge(320)-1,m::kHeaderMarkY,m::spot());
    source_tabs();
    if(frame.instrument==I::MODEL_INSTRUMENT) {
        inst_model_heading::draw_vibes(160-inst_model_heading::VIBES_W/2,62,m::spot());
        if(frame.env) {
            for(const auto& f:env_fields)model_field(f);
            ui::text(16,163,"amp envelope",m::ghost());
        } else {
            for(const auto& f:sound_fields)model_field(f);
            model_surface();
        }
        ui::text(m::kLeft,m::kTextY,"b back",m::ghost());
        ui::text(124,m::kTextY,frame.queued?"queued":"a play",m::ghost());
        right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
        return;
    }
    ui::text(16,98,"instrument",frame.selected==I::P_INSTRUMENT?m::spot():m::ghost());
    std::snprintf(text,sizeof text,"%02u / 06",unsigned(frame.instrument+1));right(304,46,text,m::ghost());
    ui::line(25,69,20,74,m::ghost());ui::line(20,74,25,79,m::ghost());
    ui::line(295,69,300,74,m::ghost());ui::line(300,74,295,79,m::ghost());
    m::heading(42,64,I::instrument_name(frame.instrument),m::spot());
    const int next=I::instrument_index(frame.routes[I::P_INSTRUMENT].target);
    if(next!=frame.instrument) {std::snprintf(text,sizeof text,"next %s",I::instrument_name(next));right(274,87,text,m::ghost());}
    for(int i=0;i<6;++i)field(i);
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());
    ui::text(124,m::kTextY,frame.queued?"queued":"a play",m::ghost());
    right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
}
bool inst_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Inst,m::spot());
    ui::text(70,m::kHeaderTextY,I::instrument_name(frame.hero),m::ink());
    char text[64];std::snprintf(text,sizeof text,"inst / ch%02u / %02u",unsigned(frame.chain+1),unsigned(frame.slot+1));
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());ui::text(16,43,FAMILIES[frame.hero],m::ghost());
    ui::text(16,59,frame.hero==5?"sound":"note",m::spot());hero_note();
    ui::text(16,139,frame.status,m::ghost());
    const int p=frame.selected;
    ui::text(16,165,p==I::P_NOTE && frame.instrument==5?"sound":p==I::P_TONE && frame.instrument!=I::MODEL_INSTRUMENT?"tone":NAMES[p],m::spot());
    right(120,165,"edit",m::ghost());
    if(p==I::P_LENGTH && frame.instrument==5)ui::text(16,193,"one-shot",m::ghost());
    else {
        const auto number=inspector_gothic::draw_numeric(16,213,frame.values[p],m::ink(),m::spot());
        if(number.drawn)ui::text(number.end_x+2,202,number.suffix,m::ghost());
        else if(p==I::P_INSTRUMENT)m::heading(16,186,frame.values[p],m::ink());
        else ui::text(16,190,frame.values[p],m::ink(),2);
    }
    if(frame.instrument==I::MODEL_INSTRUMENT) {
        ui::text(137,59,"resonance",m::ghost());
        char horizon[16];std::snprintf(horizon,sizeof horizon,"%u.%u s",frame.horizon_ms/1000,(frame.horizon_ms%1000)/100);
        right(384,59,horizon,m::ghost());
        for(int x=137;x<=383;x+=4)ui::rect(x,207,1,1,m::branch());
        ui::line(137+frame.progress,203,137+frame.progress,211,m::spot());
    }
    if(frame.instrument!=I::MODEL_INSTRUMENT) {
        ui::text(137,59,"sound",m::ghost());right(384,59,"64 ms",m::ghost());
        for(int x=137;x<=383;x+=5)ui::rect(x,139,1,1,m::branch());
        ui::text(137,205,"pre-fx / mono",m::ghost());
        char stamp[24];std::snprintf(stamp,sizeof stamp,"%lu ms",static_cast<unsigned long>(frame.scope_age/32));
        right(384,205,stamp,m::ghost());
    }
    const bool lit=frame.audible || (frame.instrument==I::MODEL_INSTRUMENT && !frame.bypass && !frame.mute && !frame.shadowed && !frame.gated);
    const u32 colors[]={m::branch(),m::ghost(),lit?m::ink():m::ghost(),lit?m::spot():m::branch()};
    for(int i=0;i<frame.strokes;++i) {
        const auto& s=frame.ink[i];ui::line(128+s.x0,s.y0,128+s.x1,s.y1,colors[s.ink]);
    }
    ui::text(m::kLeft,m::kTextY,frame.route,m::ghost());right(m::right_edge(400),m::kTextY,frame.instrument==I::MODEL_INSTRUMENT?"model view":"audio view",m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
