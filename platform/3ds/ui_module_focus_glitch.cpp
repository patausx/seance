#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using G = GlitchUnit;
using Op = G::Op;
constexpr const char* NAMES[G::P_COUNT] = {"density","jump","repeat","drop","reverse","pitch","mix","keep"};
constexpr int ORDER[G::P_COUNT] = {G::P_DENSITY,G::P_MIX,G::P_JUMP,G::P_REPEAT,G::P_DROP,G::P_REVERSE,G::P_PITCH,G::P_KEEP};
struct Frame {
    bool valid=false, bypass=false, mute=false;
    int chain=0, slot=0, selected=0, inspected=-1;
    G::Visual visual{};
    q15 edit[G::P_COUNT]={};
    ParamInspector routes[G::P_COUNT]={};
    char values[G::P_COUNT][12]={}, resolved[12]={}, route[64]={};
    uint8_t arrival=0; // short ink emphasis on an accepted pattern, not invented audio
} frame;
static_assert(sizeof(Frame)<=2048 && std::is_trivially_copyable<Frame>::value,
              "glitch bounded pre-GPU copy; never draw engine or PCM pointers");
int selected=0, inspected=-1;
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    int chain=-1, slot=-1, bank=-1, param=-1, x=0;
    View view=View::Chains;
    q15 value=0;
} gesture;
Engine* captured_engine=nullptr;
uintptr_t captured_unit=0;
uint32_t captured_revision=0;
uint64_t accepted_at=0;
int clamp(int v,int lo,int hi) {return std::max(lo,std::min(hi,v));}
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        g_view==View::Chains &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
q15 edit_value(const G& unit,int p) {
    return unit.params[p].base;
}
bool owns(const G& unit) {
    return gesture.engine==g_eng && gesture.unit==uintptr_t(&unit) && gesture.view==g_view &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&unit &&
        gesture.bank==g_eng->active_pattern;
}
void remember(G& unit,int x,int p) {
    gesture.engine=g_eng;gesture.unit=uintptr_t(&unit);gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.x=x;gesture.value=p>=0?edit_value(unit,p):0;
    gesture.view=g_view;
}
void change(G& unit,int p,q15 value) {
    if(value==edit_value(unit,p))return;
    unit.params[p].base=value;mark_project_mutated();
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
u32 blend(u32 a,u32 b,int n,int d) {
    u32 c=0xff000000u;
    for(int shift=0;shift<24;shift+=8)c|=u32((int((a>>shift)&255)*(d-n)+int((b>>shift)&255)*n)/d)<<shift;
    return c;
}
int cell_at(int x,int y) {
    if(x<12 || x>=308 || y<39 || y>=105)return -1;
    const int row=(y-39)/33,col=(x-12)/37;
    return row*8+col;
}
int op_param(Op op) {
    switch(op) {
        case Op::Jump:return G::P_JUMP;
        case Op::Repeat:return G::P_REPEAT;
        case Op::Drop:return G::P_DROP;
        case Op::Reverse:return G::P_REVERSE;
        case Op::Pitch:return G::P_PITCH;
        default:return G::P_DENSITY;
    }
}
const char* op_name(Op op) {
    switch(op) {
        case Op::Jump:return "jump";
        case Op::Repeat:return "repeat";
        case Op::Drop:return "drop";
        case Op::Reverse:return "reverse";
        case Op::Pitch:return "pitch";
        default:return "live";
    }
}
bool audible() {return frame.visual.running && frame.visual.ready && !frame.bypass && !frame.mute;}
int focus_cell() {return frame.inspected>=0?frame.inspected:clamp(frame.visual.step,0,15);}
// Native 13x11 marks: direction, cut, subdivided roll, gap, stepped pitch.
// Stable glyphs, not shaking/flickering text. Every mark comes from an actual Cell.
void glyph(const G::Cell& cell,int x,int y,u32 color) {
    switch(cell.op) {
        case Op::Jump:
            ui::line(x-6,y+3,x-2,y+3,color);ui::line(x-2,y+3,x+2,y-3,color);ui::line(x+2,y-3,x+6,y-3,color);
            ui::line(x+3,y-5,x+6,y-3,color);ui::line(x+6,y-3,x+3,y-1,color);break;
        case Op::Repeat:
            for(int i=0;i<(cell.repeats>=4?4:2);++i) {
                const int xx=x-6+i*(cell.repeats>=4?4:8);
                ui::line(xx,y+4,xx,y-4,color);ui::line(xx,y-4,xx+2,y-4,color);
            } break;
        case Op::Drop:
            ui::line(x-6,y,x-3,y,color);ui::line(x+3,y,x+6,y,color);
            ui::line(x-2,y+4,x+2,y-4,color);break;
        case Op::Reverse:
            ui::line(x-6,y,x+6,y,color);ui::line(x-6,y,x-2,y-4,color);ui::line(x-6,y,x-2,y+4,color);break;
        case Op::Pitch: {
            const int dir=cell.semitones<0?1:-1;
            ui::line(x-6,y-dir*3,x,y-dir*3,color);ui::line(x,y-dir*3,x,y+dir*3,color);ui::line(x,y+dir*3,x+6,y+dir*3,color);
            ui::line(x+6,y+dir*3,x+3,y+dir*3-2,color);ui::line(x+6,y+dir*3,x+3,y+dir*3+2,color);break;
        }
        default:ui::line(x-6,y,x+6,y,color);ui::rect(x-1,y-1,3,3,color);break;
    }
}
void rail(int p,int x,int y) {
    const auto& d=frame.routes[p];
    const auto px=[&](int v){return x+clamp(v,0,32767)*126/32767;};
    const int base=px(frame.edit[p]),live=px(d.target);
    module_fader::horizontal(x,x+126,y,base,live,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute});
}
void route_text() {
    const auto& d=frame.routes[frame.selected];
    static const char* layers[]={"base","step lock","audition","perform","temp"};
    const char* layer=layers[clamp(int(d.layer),0,4)];
    if(d.source==ModSource::None)std::snprintf(frame.route,sizeof frame.route,"%s / mod off",layer);
    else if(mod_source_sigil(d.source)>=0)
        std::snprintf(frame.route,sizeof frame.route,"%s / sigil %c / %s %+d%%",layer,'a'+mod_source_sigil(d.source),d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
    else std::snprintf(frame.route,sizeof frame.route,"%s / env%d / %s %+d%%",layer,d.env_chain+1,d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
}
// A short three-segment thread. Dot traverses the current ACTUAL cell phase;
// it describes time through the routing map, not a waveform/read-head position.
void thread(int source,int dest,u32 color,bool flowing) {
    const int x0=24+22*source,x1=24+22*dest;
    ui::line(x0,128,x0,136,color);ui::line(x0,136,x1,154,color);ui::line(x1,154,x1,161,color);
    if(flowing) {
        int x,y;const int t=int(frame.visual.phase)*33/32767;
        if(t<8) {x=x0;y=128+t;}
        else if(t<26) {x=x0+(x1-x0)*(t-8)/18;y=136+t-8;}
        else {x=x1;y=154+t-26;}
        ui::rect(x-1,y-1,3,3,m::spot());
    }
}
void describe_cell(char* out,size_t n,int i) {
    const auto& c=frame.visual.cells[i];
    if(c.op==Op::Dry)std::snprintf(out,n,"%02d / live input",i+1);
    else if(c.op==Op::Drop)std::snprintf(out,n,"%02d / drop / live input muted",i+1);
    else if(c.op==Op::Repeat)std::snprintf(out,n,"%02d / slice %02d / repeat x%d",i+1,c.source+1,c.repeats);
    else if(c.op==Op::Pitch)std::snprintf(out,n,"%02d / slice %02d / pitch %+dst",i+1,c.source+1,c.semitones);
    else std::snprintf(out,n,"%02d / slice %02d / %s",i+1,c.source+1,op_name(c.op));
}
} // namespace

int glitch_focus_selected() {return selected;}
void glitch_focus_select(int p) {if(p>=0 && p<G::P_COUNT)selected=p;}
void glitch_focus_release() {gesture={};}
void glitch_focus_value_text(char* out,size_t n,int p,q15 value) {
    if(!out || !n)return;
    if(p<0 || p>=G::P_COUNT)std::snprintf(out,n,"--");
    else if(p==G::P_KEEP)std::snprintf(out,n,"%s",value>16383?"on":"off");
    else std::snprintf(out,n,"%d%%",(clamp(value,0,32767)*100+16383)/32767);
}
bool glitch_focus_touch_body(G& unit,int tx,int ty) {
    if(!context() || param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    glitch_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&unit)return false;
    if(hit(tx,ty,262,5,50,29)) {
        unit.bypass=!unit.bypass;mark_project_mutated();return true;
    }
    if(hit(tx,ty,193,5,66,29)) {
        if(!unit.bypass && !g_eng->chains[g_edit_chain].mute) {
            unit.request_reroll();mark_project_mutated();
        }return true;
    }
    if(hit(tx,ty,8,224,100,16)) {g_overlay=Overlay::None;g_drag=Drag::None;return true;}
    const int cell=cell_at(tx,ty);
    if(cell>=0) {
        inspected=inspected==cell?-1:cell;selected=op_param(unit.visual().cells[cell].op);
        remember(unit,tx,-1);g_drag=Drag::GlitchCell;return true;
    }
    for(int i=0;i<G::P_COUNT;++i) {
        const int x=14+156*(i%2),y=114+27*(i/2),p=ORDER[i];
        if(!hit(tx,ty,x-6,y-4,144,27))continue;
        selected=p;
        if(p==G::P_KEEP) {change(unit,p,edit_value(unit,p)>16383?0:32767);return true;}
        remember(unit,tx,p);g_drag=Drag::GlitchParam;return true;
    }
    return true;
}
void glitch_focus_drag(G& unit,int tx,int ty) {
    if(!context() || param_lens_is_open()) {glitch_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!gesture.unit || !owns(unit)) {glitch_focus_release();return;}
    if(g_drag==Drag::GlitchCell) {
        const int cell=cell_at(tx,ty);
        if(cell>=0) {inspected=cell;selected=op_param(unit.visual().cells[cell].op);}return;
    }
    if(g_drag!=Drag::GlitchParam || gesture.param<0) {glitch_focus_release();return;}
    change(unit,gesture.param,q15(clamp(int(gesture.value)+(tx-gesture.x)*32767/192,0,32767)));
}
void glitch_focus_capture() {
    frame={};
    if(!context()) {glitch_focus_release();captured_unit=0;captured_engine=nullptr;return;}
    uintptr_t identity=0;
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Glitch) {glitch_focus_release();captured_unit=0;captured_engine=nullptr;return;}
        const auto& glitch=*static_cast<const G*>(unit);identity=uintptr_t(unit);
        if(gesture.unit && (!owns(glitch) || param_lens_is_open() ||
            (g_drag!=Drag::GlitchParam && g_drag!=Drag::GlitchCell)))glitch_focus_release();
        frame.valid=true;frame.bypass=unit->bypass;frame.mute=chain.mute;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.visual=glitch.visual();
        for(int p=0;p<G::P_COUNT;++p) {
            frame.edit[p]=edit_value(glitch,p);
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
    }
    const uint64_t now=osGetTime();
    if(captured_engine!=g_eng || captured_unit!=identity) {
        captured_engine=g_eng;captured_unit=identity;captured_revision=frame.visual.revision;inspected=-1;accepted_at=0;
    } else if(captured_revision!=frame.visual.revision) {captured_revision=frame.visual.revision;accepted_at=now;}
    frame.inspected=inspected;
    if(accepted_at && now>=accepted_at && now-accepted_at<160)frame.arrival=uint8_t(160-(now-accepted_at));
    for(int p=0;p<G::P_COUNT;++p)glitch_focus_value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
    glitch_focus_value_text(frame.resolved,sizeof frame.resolved,selected,frame.routes[selected].target);
    route_text();
}
void glitch_focus_draw(const G&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Glitch,m::spot());ui::text(94,m::kHeaderTextY,"16 x 1/16",m::ghost());
    ui::text(202,m::kHeaderTextY,frame.visual.pending?"queued":"reroll",m::spot());
    right(m::right_edge(320),m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(m::right_edge(320)-18,m::kHeaderMarkY,m::right_edge(320)-1,m::kHeaderMarkY,m::spot());
    for(int i=0;i<16;++i) {
        const int x=14+37*(i%8),y=42+33*(i/8);const auto& cell=frame.visual.cells[i];
        const bool live=audible() && frame.visual.step==i,focus=frame.inspected==i;
        const u32 ink=live?m::spot():cell.op==Op::Dry?m::ghost():m::ink();
        if(live)ui::rect(x-2,y-3,33,27,(m::spot()&0xffffffu)|(26u<<24));
        char index[12];std::snprintf(index,sizeof index,"%02d",i+1);ui::text(x,y,index,live?m::spot():m::ghost());
        glyph(cell,x+21,y+11,ink);
        ui::line(x,y+24,x+29,y+24,blend(m::branch(),m::spot(),frame.arrival,200));
        if(focus) {ui::line(x-2,y-3,x+8,y-3,m::spot());ui::line(x-2,y-3,x-2,y+6,m::spot());}
        if(live) {
            const int pos=x+int(frame.visual.phase)*28/32767;
            ui::line(std::max(x,pos-5),y+24,pos,y+24,m::spot());ui::rect(pos,y+22,1,3,m::spot());
        }
    }
    for(int i=0;i<G::P_COUNT;++i) {
        const int x=14+156*(i%2),y=114+27*(i/2),p=ORDER[i];
        ui::text(x,y,NAMES[p],frame.selected==p?m::spot():m::ghost());right(x+126,y,frame.values[p],m::ink());
        const auto& d=frame.routes[p];
        if(d.source!=ModSource::None) {
            char tag[6];const int sigil=mod_source_sigil(d.source);
            if(sigil>=0)std::snprintf(tag,sizeof tag,"%c",'a'+sigil);else std::snprintf(tag,sizeof tag,"e%d",d.env_chain+1);
            ui::text(x+65,y,tag,d.mod_enabled?m::spot():m::ghost());
        }
        if(p==G::P_KEEP) {
            ui::line(x,y+22,x+126,y+22,m::branch());
            ui::text(x,y+11,frame.visual.kept?"pattern fixed":"new each bar",m::ghost());
        } else rail(p,x,y+15);
    }
    m::footer(320,"b back","l: mod >");
}
bool glitch_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Glitch,m::spot());
    char text[80];std::snprintf(text,sizeof text,"ch%02d / %02d / 16 x 1/16",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(16,46,NAMES[frame.selected],m::spot());
    std::snprintf(text,sizeof text,"base %s / %s %s",frame.values[frame.selected],
        frame.selected==G::P_MIX?"target":frame.selected==G::P_KEEP?"live":"next",frame.resolved);
    right(384,46,text,m::ink());ui::text(16,62,frame.route,m::ghost());
    ui::text(16,82,"previous bar / source slices",m::ghost());
    right(384,82,frame.visual.ready?"recording new input":"warming / dry",frame.visual.ready?m::ghost():m::spot());
    const int focus=focus_cell(),current=clamp(frame.visual.step,0,15);
    for(int i=0;i<16;++i) {
        const int x=24+22*i;const auto& cell=frame.visual.cells[i];
        char index[12];std::snprintf(index,sizeof index,"%02d",i+1);ui::text(x-6,99,index,m::ghost());
        const auto& chosen=frame.visual.cells[focus];
        const bool from=chosen.op!=Op::Dry && chosen.op!=Op::Drop && chosen.source==i;
        const u32 ink=frame.visual.ready?(from?m::spot():m::ink()):m::branch();
        ui::line(x-8,126,x+8,126,m::branch());
        if(frame.visual.ready)for(int k=0;k<3;++k)ui::line(x-8+k*5,115-int(frame.visual.trace[i*4+k])*7/32768,
            x-3+k*5,115-int(frame.visual.trace[i*4+k+1])*7/32768,ink);
        else ui::line(x-6,115,x+6,115,m::branch());
        // No false connection from the old bar for live or muted-current cells.
        if(cell.op==Op::Dry || cell.op==Op::Drop) {
            ui::line(x,153,x,160,m::branch());
            if(cell.op==Op::Dry)ui::rect(x-1,151,3,2,m::ghost());
        } else if(i!=focus && (!audible() || i!=current))thread(clamp(cell.source,0,15),i,m::branch(),false);
    }
    const auto connect=[&](int i,u32 color,bool flow) {
        const auto& cell=frame.visual.cells[i];
        if(cell.op!=Op::Dry && cell.op!=Op::Drop)thread(clamp(cell.source,0,15),i,color,flow);
    };
    connect(focus,m::ink(),false);
    if(audible())connect(current,m::spot(),true);
    for(int i=0;i<16;++i) {
        const int x=24+22*i;const bool live=audible() && i==current;
        if(live)ui::rect(x-9,165,19,19,(m::spot()&0xffffffu)|(26u<<24));
        glyph(frame.visual.cells[i],x,174,live?m::spot():i==focus?m::ink():m::ghost());
        char index[12];std::snprintf(index,sizeof index,"%02d",i+1);ui::text(x-6,188,index,live?m::spot():m::ghost());
        if(i==focus)ui::line(x-7,183,x+7,183,m::ink());
        if(live)ui::rect(x-8+int(frame.visual.phase)*16/32767,184,1,3,m::spot());
    }
    describe_cell(text,sizeof text,focus);ui::text(16,202,text,m::ink());
    const char* status=frame.bypass?"bypassed / live input":frame.mute?"chain muted":
        !frame.visual.running?"start transport / warm one complete bar":
        !frame.visual.ready?"filling history / live input passes unchanged":
        frame.visual.pending?"new figure queued for next bar":
        frame.visual.kept?"keep fixes the pattern, not the incoming sound":"new figure each bar / tap keep to retain";
    ui::text(16,214,status,m::ghost());ui::line(16,225,384,225,m::branch());
    ui::text(m::kLeft,m::kTextY,frame.visual.kept?"weights prepare reroll / mix stays live":"density + weights prepare next bar",m::ghost());
    right(m::right_edge(400),m::kTextY,frame.inspected>=0?"inspect":"follow",m::spot());return true;
}
} // namespace seance::platform::ui3ds
