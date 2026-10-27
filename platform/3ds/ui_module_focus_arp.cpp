#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_param_lens.h"
#include "editor_style.h"
#include "module_heading.h"
#include "audio_out.h"
#include "sequence_note.h"
#include "../../core/engine/musical_note.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
using A = ArpUnit;
constexpr const char* NAMES[A::P_COUNT] = {
    "rate", "gate", "order", "octaves", "reset", "play", "note 1", "note 2", "note 3", "note 4"
};
struct Frame {
    bool valid=false, bypass=false, mute=false, lock=false, full=false;
    int chain=0, slot=0, selected=0;
    UnitKind target=UnitKind::Empty;
    bool note_names=false;
    int root_edit=60, root_live=60;
    A::Visual visual{};
    q15 edit[A::P_COUNT]={};
    ParamInspector routes[A::P_COUNT]={};
    char values[A::P_COUNT][16]={}, live[A::P_COUNT][16]={};
    char route[64]={}, destination[40]={};
} frame;
static_assert(sizeof(Frame)<3072 && std::is_trivially_copyable<Frame>::value,
              "ARP draws bounded frozen note state, never live engine/PCM");
int selected=0;
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    int chain=-1, slot=-1, step=-1, lock_chain=-1, bank=-1, param=-1, x=0;
    View view=View::Chains;
    q15 value=0;
    bool lock=false;
} gesture;
// Legacy injected lock editing has separate, transient recall; it must never
// borrow or overwrite the persistent card's four disabled-note values.
struct LockRecall {
    Engine* engine=nullptr; uintptr_t unit=0;
    int chain=-1,slot=-1,bank=-1,step=-1;
    A::NoteRecall notes;
} lock_recall;
int clamp(int v,int lo,int hi) { return std::max(lo,std::min(hi,v)); }
bool context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain && g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
q15 edit_value(const A& unit,int p) {
    return lock_context()?seq_display_value(g_edit_slot,p,unit.params[p].base):unit.params[p].base;
}
bool owns(const A& unit) {
    return gesture.engine==g_eng && gesture.unit==uintptr_t(&unit) && gesture.view==g_view &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&unit && gesture.lock==g_seq_lock_armed &&
        gesture.bank==g_eng->active_pattern && (!gesture.lock || (lock_context() &&
        gesture.step==g_seq_lock_step && gesture.lock_chain==g_seq_lock_chain));
}
void change(A& unit,int p,q15 value) {
    if(value==edit_value(unit,p))return;
    if(g_seq_lock_armed) { if(lock_context())seq_record_lock(g_edit_slot,p,value); }
    else { unit.params[p].base=value; mark_project_mutated(); }
}
void toggle_note(A& unit,int p) {
    A::NoteRecall* recall=&unit.note_recall;
    if(lock_context()) {
        if(lock_recall.engine!=g_eng || lock_recall.unit!=uintptr_t(&unit) ||
           lock_recall.chain!=g_edit_chain || lock_recall.slot!=g_edit_slot ||
           lock_recall.bank!=g_eng->active_pattern || lock_recall.step!=g_seq_lock_step) {
            lock_recall={};lock_recall.engine=g_eng;lock_recall.unit=uintptr_t(&unit);
            lock_recall.chain=g_edit_chain;lock_recall.slot=g_edit_slot;
            lock_recall.bank=g_eng->active_pattern;lock_recall.step=g_seq_lock_step;
        }
        recall=&lock_recall.notes;
    }
    const int i=p-A::P_NOTE1;const uint8_t bit=uint8_t(1u<<i);
    const q15 before=edit_value(unit,p);const bool off=A::note_semitones(before)==A::NOTE_OFF;
    const q15 next=off ? ((recall->mask&bit) && A::note_semitones(recall->values[i])!=A::NOTE_OFF ?
        recall->values[i] : A::note_value(0)) : A::note_value(A::NOTE_OFF);
    change(unit,p,next);
    if(edit_value(unit,p)!=next)return; // full lock bank: no phantom recall update
    if(off) {recall->mask&=uint8_t(~bit);recall->values[i]=0;}
    else {recall->values[i]=before;recall->mask|=bit;}
}
void right(int x,int y,const char* text,u32 color) { ui::text(x-ui::text_w(text),y,text,color); }
int px(q15 value,int x,int extent) { return x+clamp(value,0,32767)*extent/32767; }
void rail(int p,int x,int y,int extent) {
    const auto& d=frame.routes[p];
    const int base=px(frame.edit[p],x,extent),live=px(p==A::P_GATE?d.effective:d.target,x,extent);
    module_fader::horizontal(x,x+extent,y,base,live,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypass||frame.mute});
}
void source_tag(int p,int x,int y) {
    const auto& d=frame.routes[p];
    if(d.source==ModSource::None)return;
    char text[8]; const int sigil=mod_source_sigil(d.source);
    if(sigil>=0)std::snprintf(text,sizeof text,"%c",'a'+sigil);
    else std::snprintf(text,sizeof text,"env%d",d.env_chain+1);
    right(x,y,text,d.mod_enabled?m::spot():m::ghost());
}
// Discrete movement is in musical units, never dozens of raw-Q15 no-op drags.
q15 drag_value(int p,q15 origin,int dx) {
    if(!dx)return origin;
    if(p>=A::P_NOTE1) {
        if(dx/4==0)return origin;
        const int note=A::note_semitones(origin);
        return A::note_value(clamp((note==A::NOTE_OFF?0:note)+dx/4,-24,24));
    }
    const int step=dx/12;
    switch(p) {
        case A::P_RATE:return step?A::rate_value(clamp(A::rate_index(origin)+step,0,A::RATE_COUNT-1)):origin;
        case A::P_GATE:return q15(clamp(int(origin)+dx*32767/192,0,32767));
        case A::P_ORDER:return step?A::order_value(clamp(A::order_index(origin)+step,0,A::ORDER_COUNT-1)):origin;
        case A::P_OCTAVES:return step?A::octaves_value(clamp(A::octaves(origin)+step,1,3)):origin;
        case A::P_RESET:return step?A::reset_value(step>0):origin;
        case A::P_PLAY_MODE:return step?A::play_mode_value(step>0):origin;
        default:return origin;
    }
}
void figure_note(char* text,size_t size,int offset,bool live=false) {
    if(!frame.note_names){std::snprintf(text,size,"%+d",offset);return;}
    int midi=(live?frame.root_live:frame.root_edit)+offset;
    if(frame.target==UnitKind::SamplePlayer)midi=clamp(midi,0,120);
    else if(frame.target==UnitKind::Inst)midi=clamp(midi,24,96);
    // Other synths display requested notes; their high-frequency safety cap stays DSP-owned.
    static const char* names[]={"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    int octave=midi/12-1, pitch=midi%12;if(pitch<0){pitch+=12;--octave;}
    std::snprintf(text,size,"%s%d",names[pitch],octave);
}
const char* status() {
    if(frame.bypass)return "bypassed / source independent";
    if(frame.mute)return "chain muted";
    if(frame.target==UnitKind::Empty)return "arp target: osc / fm / bird / play / inst / note motor";
    switch(frame.visual.state) {
        case A::State::Conflict:return "nearer arp owns this source";
        case A::State::NoTarget:return "waiting for target";
        case A::State::AllOff:return "all notes off / source gated";
        case A::State::Running:return frame.visual.gate?"note on":"gate gap / effects continue";
        default:return "start transport to play";
    }
}
void describe_route() {
    const auto& d=frame.routes[frame.selected];
    static const char* layers[]={"base","step lock","audition","perform","temp"};
    const char* layer=layers[clamp(int(d.layer),0,4)];
    if(d.source==ModSource::None)std::snprintf(frame.route,sizeof frame.route,"%s / mod off",layer);
    else if(mod_source_sigil(d.source)>=0)
        std::snprintf(frame.route,sizeof frame.route,"%s / sigil %c / %s %+d%%",layer,'a'+mod_source_sigil(d.source),d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
    else std::snprintf(frame.route,sizeof frame.route,"%s / env%d / %s %+d%%",layer,d.env_chain+1,d.mod_enabled?"depth":"muted",int(d.depth)*100/32767);
}
} // namespace

int arp_focus_selected() { return selected; }
void arp_focus_select(int p) { if(p>=0 && p<A::P_COUNT)selected=p; }
void arp_focus_release() { gesture={}; }
void arp_focus_value_text(char* out,size_t n,int p,q15 raw) {
    if(!out || !n)return;
    switch(p) {
        case A::P_RATE:std::snprintf(out,n,"%s",A::rate_name(A::rate_index(raw)));break;
        case A::P_GATE:std::snprintf(out,n,"%d%%",A::gate_percent(raw));break;
        case A::P_ORDER:std::snprintf(out,n,"%s",A::order_name(A::order_index(raw)));break;
        case A::P_OCTAVES:std::snprintf(out,n,"%d",A::octaves(raw));break;
        case A::P_RESET:std::snprintf(out,n,"%s",A::reset_on_trigger(raw)?"trigger":"free");break;
        case A::P_PLAY_MODE:std::snprintf(out,n,"%s",A::play_legato(raw)?"legato":"restart");break;
        default:
            if(p>=A::P_NOTE1 && p<=A::P_NOTE4) {
                const int note=A::note_semitones(raw);
                if(note==A::NOTE_OFF)std::snprintf(out,n,"off");
                else std::snprintf(out,n,"%+dst",note);
            } else std::snprintf(out,n,"--");
            break;
    }
}
bool arp_focus_touch_body(A& unit,int tx,int ty) {
    if(!context() || param_lens_is_open())return false;
    platform::AudioOut::Guard guard(*g_audio);
    arp_focus_release();
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&unit)return false;
    if(g_seq_lock_armed && !lock_context())return true;
    if(hit(tx,ty,262,5,50,29)) {
        if(!g_seq_lock_armed) {unit.bypass=!unit.bypass;mark_project_mutated();}
        return true;
    }
    if(hit(tx,ty,8,224,100,16)) {g_overlay=Overlay::None;g_seq_lock_armed=false;g_drag=Drag::None;return true;}
    int p=-1,x=0,y=0;
    for(int i=0;i<4;++i)if(hit(tx,ty,8+76*i,41,72,82)) {p=A::P_NOTE1+i;x=14+76*i;y=46;break;}
    for(int i=0;i<6;++i)if(hit(tx,ty,8+104*(i%3),131+43*(i/3),96,43)) {p=i;x=14+104*(i%3);y=135+43*(i/3);break;}
    if(p<0)return true;
    selected=p;
    if(lock_context() && hit(tx,ty,x-3,y-3,ui::text_w(NAMES[p])+6,14)) {
        if(g_eng->remove_step_lock(g_seq_lock_chain,g_seq_lock_step,g_edit_slot,p))mark_project_mutated();
        return true;
    }
    if(p>=A::P_NOTE1 && hit(tx,ty,x-3,105,68,18)) {
        toggle_note(unit,p);
        return true;
    }
    gesture.engine=g_eng;gesture.unit=uintptr_t(&unit);gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.step=g_seq_lock_step;gesture.lock_chain=g_seq_lock_chain;gesture.bank=g_eng->active_pattern;
    gesture.param=p;gesture.x=tx;gesture.value=edit_value(unit,p);gesture.lock=g_seq_lock_armed;gesture.view=g_view;
    g_drag=Drag::ArpParam;
    return true;
}
void arp_focus_drag(A& unit,int tx,int) {
    if(!context() || param_lens_is_open()) {arp_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(gesture.param<0 || g_drag!=Drag::ArpParam || !owns(unit)) {arp_focus_release();return;}
    change(unit,gesture.param,drag_value(gesture.param,gesture.value,tx-gesture.x));
}
void arp_focus_capture() {
    frame={};
    if(!context()) {arp_focus_release();lock_recall={};return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto& chain=g_eng->chains[g_edit_chain];const Unit* unit=chain.units[g_edit_slot];
        if(!unit || unit->kind!=UnitKind::Arp) {arp_focus_release();lock_recall={};return;}
        if(!lock_context() || lock_recall.engine!=g_eng || lock_recall.unit!=uintptr_t(unit) ||
           lock_recall.chain!=g_edit_chain || lock_recall.slot!=g_edit_slot ||
           lock_recall.bank!=g_eng->active_pattern || lock_recall.step!=g_seq_lock_step)lock_recall={};
        const auto& arp=*static_cast<const A*>(unit);
        if(gesture.unit && (!owns(arp) || param_lens_is_open() || g_drag!=Drag::ArpParam))arp_focus_release();
        frame.valid=true;frame.bypass=arp.bypass;frame.mute=chain.mute;frame.lock=lock_context();
        frame.full=frame.lock && g_seq_lock_flash>0;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.visual=arp.visual();
        // Resolve the destination even while stopped, before the first audio tick.
        for(int i=g_edit_slot+1;i<UNITS_PER_CHAIN;++i)if(chain.units[i] && !chain.units[i]->bypass && A::accepts(chain.units[i])) {
            frame.target=chain.units[i]->kind;
            const Unit& source=*chain.units[i];const int pitch=source.sequencer_properties().pitch_param;
            frame.note_names=(source.kind==UnitKind::Motor) || sequence_note::absolute(source.kind,pitch);
            if(frame.note_names){
                const q15 edit=frame.lock?seq_display_value(i,pitch,source.params[pitch].base):source.params[pitch].base;
                frame.root_edit=source.kind==UnitKind::Motor?musical_note::decode(edit):sequence_note::midi(source.kind,edit);
                frame.root_live=source.kind==UnitKind::Motor?musical_note::decode(source.params[pitch].target):sequence_note::midi(source.kind,source.params[pitch].target);
            }
            std::snprintf(frame.destination,sizeof frame.destination,"to %s / %02d",m::module_name(frame.target),i+1);
            break;
        }
        for(int p=0;p<A::P_COUNT;++p) {
            frame.edit[p]=edit_value(arp,p);
            g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,p,frame.routes[p]);
        }
    }
    if(frame.target==UnitKind::Empty)std::snprintf(frame.destination,sizeof frame.destination,"no next source");
    for(int p=0;p<A::P_COUNT;++p) {
        arp_focus_value_text(frame.values[p],sizeof frame.values[p],p,frame.edit[p]);
        if(frame.note_names&&p>=A::P_NOTE1&&A::note_semitones(frame.edit[p])!=A::NOTE_OFF)
            figure_note(frame.values[p],sizeof frame.values[p],A::note_semitones(frame.edit[p]));
        arp_focus_value_text(frame.live[p],sizeof frame.live[p],p,p==A::P_GATE?frame.routes[p].effective:frame.routes[p].target);
        if(frame.note_names&&p>=A::P_NOTE1&&A::note_semitones(frame.routes[p].target)!=A::NOTE_OFF)
            figure_note(frame.live[p],sizeof frame.live[p],A::note_semitones(frame.routes[p].target),true);
    }
    describe_route();
}
void arp_focus_draw(const A&) {
    m::paper(320);if(!frame.valid)return;
    module_heading::header(UnitKind::Arp,m::spot());
    ui::text(71,m::kHeaderTextY,frame.destination,m::ghost());right(m::right_edge(320),m::kHeaderTextY,"byp",frame.bypass?m::spot():m::ghost());
    if(frame.bypass)ui::line(m::right_edge(320)-18,m::kHeaderMarkY,m::right_edge(320)-1,m::kHeaderMarkY,m::spot());
    for(int i=0;i<4;++i) {
        const int p=A::P_NOTE1+i,x=14+76*i,note=A::note_semitones(frame.edit[p]);
        ui::text(x,46,NAMES[p],frame.selected==p?m::spot():m::ghost());source_tag(p,x+62,46);
        char text[12];if(note==A::NOTE_OFF)std::snprintf(text,sizeof text,"off");else figure_note(text,sizeof text,note);
        ui::text(x,64,text,note==A::NOTE_OFF?m::ghost():m::ink(),2);
        rail(p,x,94,62);
        ui::text(x,110,note==A::NOTE_OFF?"enable":"disable",m::ghost());
    }
    ui::line(14,126,304,126,m::branch());
    for(int p=0;p<6;++p) {
        const int x=14+104*(p%3),y=135+43*(p/3);
        ui::text(x,y,NAMES[p],frame.selected==p?m::spot():m::ghost());source_tag(p,x+82,y);
        ui::text(x,y+13,frame.values[p],m::ink());rail(p,x,y+29,82);
    }
    ui::line(14,220,304,220,m::branch());
    m::footer(320,frame.full?"lock full":frame.lock?"name: remove lock":"b back",frame.lock?"b back":"l: mod >");
}
bool arp_focus_draw_top() {
    if(!frame.valid)return false;
    m::paper(400);module_heading::header(UnitKind::Arp,m::spot());
    char text[80];std::snprintf(text,sizeof text,"ch%02d / %02d / %s",frame.chain+1,frame.slot+1,frame.destination);
    right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    ui::text(16,46,NAMES[frame.selected],m::spot());
    std::snprintf(text,sizeof text,"%s %s / next %s",frame.lock?"lock":"base",frame.values[frame.selected],frame.live[frame.selected]);
    right(384,46,text,m::ink());ui::text(16,62,frame.route,m::ghost());
    ui::text(16,84,frame.lock?"playing figure / lock edit":frame.note_names?"current cycle / notes from source":"current cycle / semitones from source",m::ghost());
    const int count=std::min(int(frame.visual.count),A::MAX_NOTES);
    if(count) {
        int low=0,high=12;
        for(int i=0;i<count;++i) {low=std::min(low,int(frame.visual.notes[i]));high=std::max(high,int(frame.visual.notes[i]));}
        const auto y=[&](int note) {return 167-(note-low)*61/std::max(12,high-low);};
        const auto x=[&](int i) {return count==1?208:45+i*328/(count-1);};
        const int zero=y(0);
        ui::line(40,zero,379,zero,m::branch());ui::text(16,zero-3,"0",m::ghost());
        for(int i=0;i<count;++i) {
            const int xx=x(i),yy=y(frame.visual.notes[i]);
            const bool current=frame.visual.active && frame.visual.current==i && !frame.bypass && !frame.mute;
            if(i) {ui::line(x(i-1),y(frame.visual.notes[i-1]),xx,y(frame.visual.notes[i-1]),m::branch());ui::line(xx,y(frame.visual.notes[i-1]),xx,yy,m::branch());}
            ui::rect(xx-2,yy-2,5,5,current?m::spot():m::ink());
            if(current)ui::frame(xx-4,yy-4,9,9,frame.visual.gate?m::spot():m::ghost());
            if(count<=12) {figure_note(text,sizeof text,frame.visual.notes[i],true);ui::text(xx-ui::text_w(text)/2,179,text,current?m::spot():m::ghost());}
        }
    } else ui::text(40,135,"no active figure",m::ghost());
    ui::text(16,197,status(),frame.visual.active?m::spot():m::ghost());
    ui::text(16,212,frame.selected==A::P_PLAY_MODE?"play only / restart or keep sample position":
        frame.selected==A::P_RESET?"trigger: restart figure / free: keep phase":"root follows source / tune it in source editor",m::ghost());
    ui::line(16,225,384,225,m::branch());
    ui::text(m::kLeft,m::kTextY,"source gate / effects keep tails",m::ghost());
    right(m::right_edge(400),m::kTextY,frame.full?"lock full":frame.lock?"lock edit":"clock synced",m::spot());
    return true;
}
} // namespace seance::platform::ui3ds
