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
constexpr const char* NAMES[I::P_COUNT] = {"instrument","note","length","attack","release","tone","level"};
constexpr const char* FAMILIES[I::INSTRUMENT_COUNT] = {
    "impulse / trace", "tension / drift", "pressure / flare",
    "air / current", "weight / recoil", "impact / break"
};
constexpr int STROKES = 448;
enum Ink : uint8_t { Rail, Ghost, Solid, Accent };
// x is relative to 128: the figure never reaches the note/parameter column.
struct Stroke { uint8_t x0,y0,x1,y1,ink; };
struct Frame {
    bool valid=false, bypass=false, mute=false, shadowed=false, gated=false;
    bool lock=false, full=false, lens=false, queued=false, audible=false;
    uint8_t chain=0, slot=0, selected=I::P_RELEASE, instrument=0, hero=0, note=60;
    int8_t voice=-1;
    uint8_t kinds[UNITS_PER_CHAIN]={};
    q15 edit[I::P_COUNT]={};
    ParamInspector routes[I::P_COUNT]={};
    I::Visual visual{};
    char values[I::P_COUNT][16]={}, route[64]={}, status[24]={};
    uint16_t strokes=0;
    Stroke ink[STROKES]={};
} frame;
static_assert(sizeof(Frame)<=4096 && std::is_trivially_copyable<Frame>::value,
              "INST bounded UI-only voice/geometry DTO; no engine or PCM in draw");
int selected=I::P_RELEASE;
bool blocked_frame=false;
enum class Action : uint8_t { None, Previous, Next, Bypass, Back, Play, Mod, Remove };
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    int chain=-1,slot=-1,step=-1,bank=-1,lock_chain=-1,param=-1;
    int x=0,y=0,x0=0,y0=0,w=0,h=0;
    q15 value=0,instrument=0;
    View view=View::Chains;
    Action action=Action::None;
    bool lock=false,bypass=false;
} gesture;
static_assert(sizeof(Gesture)<=96,"small INST input identity/capture");
int clamp(int v,int lo,int hi) {return std::max(lo,std::min(hi,v));}
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
int edit_bank() {
    const int bank=sequencer_edit_bank();
    return bank>=0 && bank<NUM_PATTERNS ? bank : g_eng->active_pattern;
}
// Explicit edit-bank lookup. Never let a queued audio bank replace the bank
// whose step the user opened, and never use a playing step's effective value.
q15 edit_value(const I& u,int p) {
    if(lock_context()) {
        const auto& step=g_eng->sequence_pattern(edit_bank()).steps[g_edit_chain][g_seq_lock_step];
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot && step.locks[i].param==p)return step.locks[i].value;
    }
    return u.params[p].base;
}
void change(I& u,int p,q15 value) {
    if(edit_value(u,p)==value)return;
    if(g_seq_lock_armed) {
        if(!lock_context())return;
        // Same bounded, guarded selected-bank write as the score controller.
        // Engine::set_step_lock is deliberately active-bank-only.
        auto& step=g_eng->sequence_pattern(edit_bank()).steps[g_edit_chain][g_seq_lock_step];
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);
        int found=-1;
        for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot && step.locks[i].param==p) {found=i;break;}
        if(found>=0)step.locks[found].value=value;
        else {
            if(count>=SEQ_LOCKS) {g_seq_lock_flash=90;return;}
            step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),value};
            step.lock_count=uint8_t(count+1);
        }
    } else u.params[p].base=value;
    mark_project_mutated();
}
bool owns(const I& u) {
    return context() && gesture.unit==uintptr_t(&u) && gesture.engine==g_eng &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot && gesture.view==g_view &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u && gesture.lock==g_seq_lock_armed &&
        gesture.bypass==u.bypass && (gesture.param==I::P_INSTRUMENT ||
            gesture.instrument==edit_value(u,I::P_INSTRUMENT)) &&
        (!gesture.lock || (lock_context() && gesture.step==g_seq_lock_step &&
            gesture.lock_chain==g_seq_lock_chain && gesture.bank==edit_bank()));
}
void start(I& u,int p,int tx,int ty,Action action=Action::None,int x=0,int y=0,int w=0,int h=0) {
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);
    gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;gesture.view=g_view;
    gesture.lock=g_seq_lock_armed;gesture.lock_chain=g_seq_lock_chain;
    gesture.step=g_seq_lock_step;gesture.bank=edit_bank();gesture.param=p;
    gesture.x=tx;gesture.y=ty;gesture.value=p>=0?edit_value(u,p):0;
    gesture.instrument=edit_value(u,I::P_INSTRUMENT);gesture.bypass=u.bypass;
    gesture.action=action;gesture.x0=x;gesture.y0=y;gesture.w=w;gesture.h=h;
    if(p>=0)selected=p;
    g_drag=Drag::InstParam;
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
int px(q15 v,int x) {return x+clamp(v,0,32767)*126/32767;}
int note_px(q15 v,int x) {
    if(frame.instrument==5)return x+I::drum_index(I::note_midi(v))*126/(I::DRUM_COUNT-1);
    return px(v,x);
}
void field(int i) {
    const int p=i+1,x=16+(i%2)*152,y=108+(i/2)*38,ry=y+19;
    const bool disabled=frame.instrument==5 && p==I::P_LENGTH;
    const auto& d=frame.routes[p];
    const u32 color=disabled?m::ghost():frame.selected==p?m::spot():m::ink();
    ui::text(x,y,p==I::P_NOTE && frame.instrument==5?"sound":NAMES[p],color);
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
constexpr float PI=3.14159265358979323846f,TAU=2*PI;
void impulse(float energy,float drift,bool bass) {
    const float scale=.35f+.65f*energy;
    float fall=1;
    for(int i=0;i<7;++i) {
        const float cx=169+i*29,cy=bass?151:143;
        const float height=(bass?47:62)*fall*scale;
        const float width=(bass?20:11)*fall;
        const float lean=(bass?-15:19)*fall+drift*2;
        const auto point=[&](float t){return Point{cx+lean*std::sin(t)+width*std::cos(t),cy+height*std::sin(t)};};
        Point previous=point(0);
        for(int j=1;j<=32;++j) {
            Point next=point(TAU*j/32);
            if(j<=4 || j>=14)line(previous,next,i==1?Accent:i==0?Solid:i<4?Ghost:Rail);
            previous=next;
        }
        if(i<3) {
            previous=point(PI*.25f);
            for(int j=1;j<=16;++j) {
                const float t=PI*(.25f+j*1.4f/16);
                Point next{cx+lean*std::sin(t)-width*.4f*std::cos(t),cy+height*.87f*std::sin(t)};
                line(previous,next,i==1?Accent:Rail);previous=next;
            }
        }
        fall*=.74f;
    }
    // Compact onset slash grows from the voice envelope, never from wall time.
    for(int j=0;j<6;++j)line({float(145+j),141-10*energy},{float(144+j),143+13*energy},Accent);
}
Point ribbon(float t,float u,float energy,float drift) {
    const float s=std::sin(PI*t),width=35*s*(.45f+.55f*energy);
    const float angle=(-30+210*t)*PI/180+drift*.14f;
    return {143+232*t+u*width*std::sin(angle),
        142+31*std::sin(TAU*t+.4f+drift*.12f)*std::sqrt(std::max(0.f,s))+u*width*std::cos(angle)};
}
void tension(float energy,float drift) {
    for(int i=0;i<11;++i) {
        const float u=(i-5)/5.f;Point previous=ribbon(0,u,energy,drift);
        for(int j=1;j<=32;++j) {
            const auto next=ribbon(j/32.f,u,energy,drift);
            line(previous,next,i==0||i==10?Solid:i==4||i==5?Accent:Ghost);previous=next;
        }
    }
    for(float t:{.16f,.23f,.30f,.37f,.64f,.71f,.78f,.85f})
        line(ribbon(t,-1,energy,drift),ribbon(t,1,energy,drift),Rail);
}
void pressure(float energy,float drift) {
    for(int i=0;i<7;++i) {
        const float u=(i-3)/3.f;Point previous{143,143};
        for(int j=1;j<=36;++j) {
            const float t=j/36.f,s=std::sin(PI*t);
            Point next{143+231*t,143+u*(15+42*energy)*s*(.35f+t)+
                10*std::sin(TAU*t+drift*.18f)*s};
            if(j<26 || j>30)line(previous,next,i==0||i==6?Solid:i==3?Accent:i&1?Rail:Ghost);
            previous=next;
        }
    }
}
void current(float energy,float drift) {
    for(int i=0;i<7;++i) {
        Point previous{143,141};
        for(int j=1;j<=36;++j) {
            const float t=j/36.f,s=std::sin(PI*t),u=(i-3)/3.f;
            Point next{143+232*t,142+s*(u*(10+18*energy)+
                (11+9*energy)*std::sin(TAU*(1.2f*t)+i*.23f+drift*.22f))};
            if(j<32 || (i&1))line(previous,next,i==3?Accent:i==0||i==6?Solid:Ghost);
            previous=next;
        }
    }
}
void impact(float energy,float drift) {
    static constexpr int radii[]={25,18,28,16,23,19,29,17,26,19,23,15};
    const auto point=[&](int i,float scale,float angle){
        i%=12;const float t=TAU*i/12+.12f+angle;
        const float r=radii[i]*scale*(.6f+.4f*energy);
        return Point{253+r*std::cos(t)*1.32f,140+r*std::sin(t)};
    };
    for(int ring=0;ring<3;++ring) {
        const float scale=2.5f-ring*.55f,offset=(ring-1)*.05f+drift*.015f;
        for(int i=0;i<12;++i)if(i!=2 && i!=4 && i!=7 && i!=9 && i!=11)
            line(point(i,scale,offset),point(i+1,scale,offset),ring==0?Rail:ring==1?Ghost:Solid);
    }
    for(int i=0;i<12;++i) {
        line(point(i,.92f,0),point(i+1,.92f,0),Solid);
        if(i%3==0)line(point(i,.92f,0),{253,140},Ghost);
    }
    for(int y=-10;y<=10;++y) {
        const float half=(10-std::abs(y))*(.5f+.5f*energy);
        if(std::abs(y)<4) {
            line({253-half,float(140+y)},{250,float(140+y)},Accent);
            line({256,float(140+y)},{253+half,float(140+y)},Accent);
        } else line({253-half,float(140+y)},{253+half,float(140+y)},Accent);
    }
    for(int i:{0,2,5,7,10})line(point(i,2.8f,0),point(i,2.93f,0),Ghost);
}
void prepare_figure() {
    frame.strokes=0;
    const auto* voice=frame.voice<0?nullptr:&frame.visual.voices[frame.voice];
    const float energy=voice?voice->envelope/32767.f:0;
    const float age=voice?voice->age/float(ENGINE_SR):0;
    const float drift=voice?std::sin(age*1.7f+(int(voice->note)-60)*.05f):0;
    switch(frame.hero) {
        case 0:impulse(energy,drift,false);break;
        case 1:tension(energy,drift);break;
        case 2:pressure(energy,drift);break;
        case 3:current(energy,drift);break;
        case 4:impulse(energy,drift,true);break;
        case 5:impact(energy,drift);break;
    }
    for(const auto& v:frame.visual.voices)if(v.stage!=uint8_t(I::Stage::Off)) {
        const float x=135+(clamp(v.note,24,96)-24)*3.3f,y=216-v.envelope*6.f/32767;
        line({x,y},{x+5,y},v.instrument==frame.hero?Accent:Ghost);
    }
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
void inst_focus_select(int p) {if(p>=0 && p<I::P_COUNT)selected=p;}
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
    if(g_seq_lock_armed && !lock_context())return true;
    const auto button=[&](Action a,int x,int y,int w,int h,int p=-1){
        if(!hit(tx,ty,x,y,w,h))return false;
        start(u,p,tx,ty,a,x,y,w,h);return true;
    };
    if(hit(tx,ty,266,5,48,31)) {
        if(!g_seq_lock_armed)button(Action::Bypass,266,5,48,31);
        return true;
    }
    if(button(Action::Previous,8,58,28,38,I::P_INSTRUMENT) ||
       button(Action::Next,280,58,32,38,I::P_INSTRUMENT)) {selected=I::P_INSTRUMENT;return true;}
    if(hit(tx,ty,36,58,244,38)) {selected=I::P_INSTRUMENT;start(u,selected,tx,ty);return true;}
    // Tapping a label removes only this parameter's stored lock, on real lift.
    if(lock_context() && button(Action::Remove,12,42,74,14,I::P_INSTRUMENT))return true;
    for(int i=0;i<6;++i) {
        const int p=i+1,x=16+(i%2)*152,y=108+(i/2)*38;
        if(!hit(tx,ty,x-4,y-4,142,35))continue;
        selected=p;
        if(lock_context() && button(Action::Remove,x-4,y-4,56,14,p))return true;
        if(p==I::P_LENGTH && I::instrument_index(edit_value(u,I::P_INSTRUMENT))==5)return true;
        start(u,p,tx,ty);return true;
    }
    if(button(Action::Back,8,222,80,18) || button(Action::Play,112,222,90,18))return true;
    if(!g_seq_lock_armed)button(Action::Mod,218,222,96,18);
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
    q15 value=gesture.value;
    if(p==I::P_INSTRUMENT) {
        if(dx/40)value=I::instrument_value(clamp(I::instrument_index(value)+dx/40,0,I::INSTRUMENT_COUNT-1));
    } else if(p==I::P_NOTE) {
        if(I::instrument_index(gesture.instrument)==5) {
            if(dx/20)value=I::note_value(I::drum_note(clamp(I::drum_index(I::note_midi(value))+dx/20,0,I::DRUM_COUNT-1)));
        } else if(dx/4)value=I::note_value(clamp(I::note_midi(value)+dx/4,24,96));
    } else if(p>=0)value=q15(clamp(int(value)+dx*32767/126,0,32767));
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
            if(owns(u) && (gesture.param<0 || edit_value(u,gesture.param)==gesture.value)) {
                action=gesture.action;
                if(action==Action::Previous || action==Action::Next) {
                    const int next=(I::instrument_index(gesture.value)+I::INSTRUMENT_COUNT+
                        (action==Action::Next?1:-1))%I::INSTRUMENT_COUNT;
                    selected=I::P_INSTRUMENT;change(u,I::P_INSTRUMENT,I::instrument_value(next));
                } else if(action==Action::Bypass && !g_seq_lock_armed) {
                    u.bypass=!u.bypass;mark_project_mutated();
                } else if(action==Action::Remove && lock_context()) {
                    if(g_eng->remove_step_lock(g_edit_chain,g_seq_lock_step,g_edit_slot,gesture.param,edit_bank()))mark_project_mutated();
                } else if(action==Action::Play && !u.bypass && !g_eng->chains[g_edit_chain].mute) {
                    if(lock_context())g_eng->audition_sequence_step(g_edit_chain,g_seq_lock_step,edit_bank());
                    else g_eng->request_chain_trigger(g_edit_chain,g_eng->launch_quantize);
                }
            }
        }
    }
    inst_focus_release();
    if(action==Action::Back) {
        g_overlay=Overlay::None;g_seq_lock_armed=false;g_seq_lock_flash=0;g_drag=Drag::None;
    } else if(action==Action::Mod) {
        // Deliberately outside our guard; the shared controller owns its own guard.
        param_lens_touch_down(250,232);
    }
}
void inst_focus_capture() {
    frame={};
    if(!context()) {inst_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Inst) {inst_focus_release();return;}
        const auto& u=*static_cast<const I*>(unit);
        if(gesture.unit && (!owns(u) || param_lens_is_open() || g_drag!=Drag::InstParam))inst_focus_release();
        frame.valid=true;frame.bypass=u.bypass;frame.mute=chain.mute;frame.lock=lock_context();
        frame.full=frame.lock && g_seq_lock_flash>0;frame.lens=false;
        frame.chain=uint8_t(g_edit_chain);frame.slot=uint8_t(g_edit_slot);frame.selected=uint8_t(selected);
        frame.queued=(g_eng->pending_triggers&(1u<<g_edit_chain))!=0;
        frame.gated=u.arp_voice.owner>=0 && !u.arp_voice.gate;
        frame.visual=u.visual();
        for(int p=0;p<I::P_COUNT;++p) {
            frame.edit[p]=edit_value(u,p);
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
        for(int s=0;s<UNITS_PER_CHAIN;++s) {
            const auto* next=chain.units[s];frame.kinds[s]=uint8_t(next?next->kind:UnitKind::Empty);
            if(s>g_edit_slot && next && !next->bypass && next->sequencer_properties().source)frame.shadowed=true;
        }
    }
    // All formatting/projection is outside the audio guard; no render/PCM/clock calls.
    frame.instrument=uint8_t(I::instrument_index(frame.edit[I::P_INSTRUMENT]));
    frame.hero=uint8_t(I::instrument_index(frame.routes[I::P_INSTRUMENT].target));
    frame.note=uint8_t(I::note_midi(frame.routes[I::P_NOTE].target));
    for(int i=0;i<I::VOICE_COUNT;++i) {
        const auto& v=frame.visual.voices[i];
        if(v.stage!=uint8_t(I::Stage::Off) && (frame.voice<0 || v.age<frame.visual.voices[frame.voice].age))frame.voice=int8_t(i);
    }
    if(frame.voice>=0) {frame.hero=frame.visual.voices[frame.voice].instrument;frame.note=frame.visual.voices[frame.voice].note;}
    frame.audible=frame.voice>=0 && !frame.bypass && !frame.mute && !frame.shadowed && !frame.gated;
    for(int p=0;p<I::P_COUNT;++p) {
        inst_readout::value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
        const unsigned ms=p==I::P_LENGTH?I::length_ms(frame.edit[p]):p==I::P_ATTACK?I::attack_ms(frame.edit[p]):
            p==I::P_RELEASE?I::release_ms(frame.edit[p]):0;
        // Four full-size digits plus 'ms' would enter the figure column.
        if(ms>=1000)std::snprintf(frame.values[p],sizeof frame.values[p],"%u.%02us",ms/1000,(ms%1000)/10);
    }
    if(frame.instrument==5)std::snprintf(frame.values[I::P_NOTE],sizeof frame.values[0],"%s",I::drum_name(I::drum_index(I::note_midi(frame.edit[I::P_NOTE]))));
    const char* state=frame.bypass?"bypassed":frame.mute?"chain muted":frame.shadowed?"later source":frame.gated?"arp gap":nullptr;
    if(state)std::snprintf(frame.status,sizeof frame.status,"%s",state);
    else {
        const uint8_t stage=frame.voice<0?uint8_t(I::Stage::Off):frame.visual.voices[frame.voice].stage;
        std::snprintf(frame.status,sizeof frame.status,"%s / %02u",stage==uint8_t(I::Stage::Off)?"ready":
            stage==uint8_t(I::Stage::Attack)?"attack":stage==uint8_t(I::Stage::Release)?"tail":frame.hero==5?"hit":"sustain",unsigned(frame.visual.active));
    }
    int at=0;
    for(uint8_t kind:frame.kinds)if(kind!=uint8_t(UnitKind::Empty)) {
        const int n=std::snprintf(frame.route+at,sizeof frame.route-size_t(at),"%s%s",at?" / ":"",m::module_name(UnitKind(kind)));
        at=std::min(int(sizeof frame.route)-1,at+std::max(0,n));
    }
    prepare_figure();
}
void inst_focus_draw(const I&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Inst,m::spot());
    char text[32];std::snprintf(text,sizeof text,"ch%02u / %02u",unsigned(frame.chain+1),unsigned(frame.slot+1));
    ui::text(76,m::kHeaderTextY,text,m::ghost());right(m::right_edge(320),m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(m::right_edge(320)-18,m::kHeaderMarkY,m::right_edge(320)-1,m::kHeaderMarkY,m::spot());
    ui::text(16,46,"instrument",frame.selected==I::P_INSTRUMENT?m::spot():m::ghost());
    std::snprintf(text,sizeof text,"%02u / 06",unsigned(frame.instrument+1));right(304,46,text,m::ghost());
    ui::line(25,69,20,74,m::ghost());ui::line(20,74,25,79,m::ghost());
    ui::line(295,69,300,74,m::ghost());ui::line(300,74,295,79,m::ghost());
    m::heading(42,64,I::instrument_name(frame.instrument),m::spot());
    const int next=I::instrument_index(frame.routes[I::P_INSTRUMENT].target);
    if(next!=frame.instrument) {std::snprintf(text,sizeof text,"next %s",I::instrument_name(next));right(274,87,text,m::ghost());}
    for(int i=0;i<6;++i)field(i);
    if(frame.full)ui::text(16,215,"lock full / 4 per step",m::ghost());
    else if(frame.lock)ui::text(16,215,"label: remove lock",m::ghost());
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());
    ui::text(124,m::kTextY,frame.lock?"listen":frame.queued?"queued":"a play",m::ghost());
    right(m::right_edge(320),m::kTextY,frame.lock?"lock edit":"l: mod >",m::ghost());
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
    ui::text(16,165,p==I::P_NOTE && frame.instrument==5?"sound":NAMES[p],m::spot());
    right(120,165,frame.lock?"lock":"edit",m::ghost());
    if(p==I::P_LENGTH && frame.instrument==5)ui::text(16,193,"one-shot",m::ghost());
    else {
        const auto number=inspector_gothic::draw_numeric(16,213,frame.values[p],m::ink(),m::spot());
        if(number.drawn)ui::text(number.end_x+2,202,number.suffix,m::ghost());
        else if(p==I::P_INSTRUMENT)m::heading(16,186,frame.values[p],m::ink());
        else ui::text(16,190,frame.values[p],m::ink(),2);
    }
    const u32 colors[]={m::branch(),m::ghost(),frame.audible?m::ink():m::ghost(),frame.audible?m::spot():m::branch()};
    for(int i=0;i<frame.strokes;++i) {
        const auto& s=frame.ink[i];ui::line(128+s.x0,s.y0,128+s.x1,s.y1,colors[s.ink]);
    }
    ui::text(m::kLeft,m::kTextY,frame.route,m::ghost());right(m::right_edge(400),m::kTextY,"voice view",m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
