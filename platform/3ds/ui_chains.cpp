#include "ui_state.h"
#include "ui_chains.h"
#include "menu_style.h"
#include "module_fader_painter.h"
#include "ui_palette.h"
#include "audio_out.h"
#include "sample_ownership.h"
#include "ui_picker.h"
#include "ui_nav.h"
#include "ui_module_focus_internal.h"
#include "chain_columns.h"
#include "picker_motion.h"
#include "editor_transition.h"
#include "editor_style.h"
#include "module_heading.h"
#include "sigil_surface.h"
#include "ring_reference.h"
#include "play_readout.h"
#include "inst_readout.h"
#include "scratch_readout.h"
#include "paul_readout.h"
#include "pent_readout.h"
#include "rule_readout.h"
#include "rot_readout.h"
#include "../../core/engine/units.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m = menu_style;
namespace cc = chain_columns;
constexpr int BODY_END=198, GRIP_Y=188;
struct Tile {
    UnitKind kind=UnitKind::Empty;
    bool bypass=false;
    bool play_modern=false, play_quantize=false;
    PlayUnit::PitchMode play_pitch_mode=PlayUnit::PitchMode::Legacy;
    ParamInspector control[2];
};
struct Frame {
    MusicalScale scale{};
    Tile tiles[UNITS_PER_CHAIN];
    ParamInspector route;
    cc::Layout layout;
    int selected=cc::Add, chain=0, count=0, gap=0, dragged=-1;
    int menu_x=0, anchor_x=0, menu_cursor=0;
    bool valid=false, modal=false, picker=false, add=false, paste=false, confirm=false;
    char patch[24]{}, status[48]{};
};
static_assert(sizeof(Frame)<=1536,"bounded pointer-free chains capture");
static_assert(std::is_trivially_copyable<Frame>::value,"draw never follows a Unit");
Frame frame;
cc::Motion motion;
Engine* context_engine=nullptr;
Unit* identities[UNITS_PER_CHAIN]{};
int context_chain=-1, order[UNITS_PER_CHAIN]{}, count=0, selected=cc::Add;
WorkspaceLayout undo_layout;
bool undo_layout_valid=false;
const char* notice=nullptr;
u64 notice_until=0;

// Input tokens, never part of the draw DTO. Revalidate before any mutation.
enum class Gesture { None, Tap, Grip, Move, Slider, Menu, Chain, Add };
enum class Action { Select, Edit, Bypass, More };
struct Touch {
    Gesture kind=Gesture::None;
    Action action=Action::Select;
    Engine* engine=nullptr;
    platform::AudioOut* audio=nullptr;
    Unit* identity=nullptr;
    int chain=-1, slot=-1, item=-1, param=-1;
    int x=0,y=0,rail_x=0,rail_w=0,offset=80,float_x=0,gap=0;
    u64 began=0;
} touch;
bool menu_open=false, blur_refresh=true;
bool picker_origin_ready=false;
int menu_cursor=0, menu_chain=-1, menu_slot=-1;
Unit* menu_identity=nullptr;
Engine* menu_engine=nullptr;
int delete_chain=-1,delete_slot=-1;
u64 delete_until=0;
uint32_t blur_colors[4]{};

// Only controller state holds addresses. The existing chain Frame stays frozen
// while the editor is open; the compositor never dereferences these identities.
Engine* editor_engine=nullptr;
platform::AudioOut* editor_audio=nullptr;
Unit* editor_units[NUM_CHAINS][UNITS_PER_CHAIN]{};
UnitKind editor_kinds[NUM_CHAINS][UNITS_PER_CHAIN]{};
int editor_chain=-1,editor_slot=-1;
u32 editor_drain=0;
bool editor_visible=false;

bool editor_context() {
    if(!editor_transition::state.active||!g_eng||!g_audio||g_eng!=editor_engine||g_audio!=editor_audio||
       nav_is_open()||g_view!=View::Chains||g_overlay!=Overlay::Editor||
       g_sel_chain!=editor_chain||g_edit_chain!=editor_chain||g_sel_slot!=editor_slot||g_edit_slot!=editor_slot)return false;
    platform::AudioOut::Guard guard(*g_audio);
    for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s){
        const Unit* u=g_eng->chains[c].units[s];
        if(u!=editor_units[c][s]||(u&&u->kind!=editor_kinds[c][s]))return false;
    }
    return frame.valid&&frame.chain==editor_chain&&frame.selected==editor_slot;
}
void editor_forget() {
    editor_transition::reset();editor_visible=false;
    editor_engine=nullptr;editor_audio=nullptr;editor_chain=editor_slot=-1;
}
void editor_finish() {
    // Validated by the caller. Restore the SAME frozen geometry; the normal
    // column controller may then continue its own interrupted selection slide.
    g_overlay=Overlay::None;g_drag=Drag::None;g_last_px=-1;g_smooth_active=false;
    editor_forget();
}

void notify(const char* text){notice=text;notice_until=osGetTime()+1600;}
bool structure_ready_locked(){if(g_eng->recording_active_or_pending()){notify("stop rec before changing chain");return false;}return true;}
int empty_slot(){for(int i=0;i<UNITS_PER_CHAIN;++i)if(!g_eng->chains[g_sel_chain].units[i])return i;return -1;}
bool available(){return g_eng&&g_audio&&g_view==View::Chains&&g_sel_chain>=0&&g_sel_chain<NUM_CHAINS;}
void cancel_touch(){
    const bool preview=touch.kind==Gesture::Move||touch.kind==Gesture::Grip;
    touch={};
    if(preview)motion.aim(cc::layout(order,count,selected),osGetTime());
}
void close_menu(){menu_open=false;menu_identity=nullptr;menu_engine=nullptr;delete_slot=-1;cancel_touch();}
void select_slot(int slot){
    selected=slot;
    g_edit_chain=g_sel_chain;
    const int physical=slot==cc::Add?empty_slot():slot;
    if(physical>=0)g_sel_slot=g_edit_slot=physical;
}
bool sync_context(){
    if(!available())return false;
    bool changed=context_engine!=g_eng||context_chain!=g_sel_chain;
    auto& c=g_eng->chains[g_sel_chain];
    for(int i=0;i<UNITS_PER_CHAIN;++i)changed|=identities[i]!=c.units[i];
    if(changed){
        cancel_touch();close_menu();count=0;
        for(int i=0;i<UNITS_PER_CHAIN;++i){identities[i]=c.units[i];if(c.units[i])order[count++]=i;}
        context_engine=g_eng;context_chain=g_sel_chain;blur_refresh=true;
        if(g_sel_slot>=0&&g_sel_slot<UNITS_PER_CHAIN&&c.units[g_sel_slot])selected=g_sel_slot;
        else if(count<UNITS_PER_CHAIN)select_slot(cc::Add);
        else select_slot(order[0]);
    } else {
        const int next=g_sel_slot>=0&&g_sel_slot<UNITS_PER_CHAIN&&c.units[g_sel_slot]?g_sel_slot:cc::Add;
        if(next!=selected){cancel_touch();close_menu();selected=next;}
    }
    int preview[UNITS_PER_CHAIN];
    const int* active_order=order;
    if(touch.kind==Gesture::Move){cc::preview_order(order,count,touch.slot,touch.gap,preview);active_order=preview;}
    motion.aim(cc::layout(active_order,count,selected),osGetTime(),changed);
    motion.sample(osGetTime());
    return true;
}
bool touch_valid(){
    if(!available()||nav_is_open()||g_overlay!=Overlay::None||touch.engine!=g_eng||touch.audio!=g_audio||touch.chain!=g_sel_chain)return false;
    for(int s=0;s<UNITS_PER_CHAIN;++s)if(identities[s]!=g_eng->chains[g_sel_chain].units[s])return false;
    return touch.slot<0||(touch.slot<UNITS_PER_CHAIN&&touch.slot==g_sel_slot&&
        g_eng->chains[g_sel_chain].units[touch.slot]==touch.identity);
}
int menu_x(){return std::clamp(motion.shown.box[selected].x+146,24,236);}
int menu_hit(int x,int y){
    const int mx=menu_x();
    if(x<mx-16||x>=mx+68||y<46||y>=146)return -1;
    return (y-46)/25;
}
void open_menu(){
    cancel_touch();if(!available()||selected==cc::Add)return;
    menu_open=true;menu_cursor=0;menu_engine=g_eng;menu_chain=g_sel_chain;menu_slot=g_sel_slot;
    menu_identity=g_eng->chains[g_sel_chain].units[g_sel_slot];blur_refresh=true;
    motion.aim(cc::layout(order,count,selected),osGetTime(),true);
}
void open_picker(bool replace){
    {platform::AudioOut::Guard guard(*g_audio);if(!structure_ready_locked())return;}
    const int slot=replace?g_sel_slot:empty_slot();
    if(slot<0)return;
    // Destination and visual origin are deliberately independent: adding grows
    // the currently visible tile but still appends at the actual chain tail.
    close_menu();g_edit_chain=g_sel_chain;g_edit_slot=slot;
    picker_open();g_overlay=Overlay::Picker;blur_refresh=true;
}
void copy_unit(){
    Unit* copy=nullptr;Engine* owner=g_eng;const int ci=g_sel_chain,si=g_sel_slot;
    {platform::AudioOut::Guard guard(*g_audio);if(!structure_ready_locked())return;
        copy=clone_unit(g_eng->chains[ci].units[si]); // retained ordinary copy path
    }
    if(copy){Unit* retired=copy;
        {platform::AudioOut::Guard guard(*g_audio);
            const bool same=owner==g_eng&&ci==g_sel_chain&&si==g_sel_slot;
            if(same&&structure_ready_locked()){retired=g_clip_unit;g_clip_unit=copy;g_eng->set_external_sample_slot(sample_ownership::unit_slot(copy));notify("copied module");}}
        delete retired;}
    else notify("copy failed");
}
void paste_unit(){
    {platform::AudioOut::Guard guard(*g_audio);if(!structure_ready_locked())return;}
    const int empty=empty_slot();
    if(empty<0||!g_clip_unit)return;
    Unit* copy=clone_unit(g_clip_unit);
    if(!copy){notify("paste failed");return;}
    RecWorkspacePlan plan;
    auto result=trackr::synth::SampleEditResult::Ok;
    if(copy->kind==UnitKind::Recorder){
        {platform::AudioOut::Guard guard(*g_audio);result=g_eng->plan_recorder(*static_cast<RecUnit*>(copy),true,plan);}
        if(result==trackr::synth::SampleEditResult::Ok)result=Engine::stage_recorder(plan);
    }
    Unit* displaced=nullptr;
    if(result==trackr::synth::SampleEditResult::Ok){
        platform::AudioOut::Guard guard(*g_audio);
        if(!structure_ready_locked())result=trackr::synth::SampleEditResult::Busy;
        else if(!g_eng->can_install_unit(copy,g_eng->chains[g_sel_chain].units[empty]))result=trackr::synth::SampleEditResult::MemoryBudget;
        else if(copy->kind==UnitKind::Recorder)result=g_eng->commit_recorder(plan);
        if(result==trackr::synth::SampleEditResult::Ok){
            displaced=g_eng->replace_unit(g_sel_chain,empty,copy);copy=nullptr;
            const int final_slot=g_eng->move_unit_before(g_sel_chain,empty,UNITS_PER_CHAIN);
            if(final_slot>=0&&final_slot!=empty)workspace_move_slot(g_sel_chain,empty,final_slot);
            g_sel_slot=g_edit_slot=final_slot>=0?final_slot:empty;g_edit_chain=g_sel_chain;
            mark_project_structural_mutated();notify("pasted module");
        }
    }
    delete displaced;delete copy;
    if(result!=trackr::synth::SampleEditResult::Ok)notify("paste refused / reader or sample budget");
}
void menu_action(int item){
    if(!menu_open||!available()||menu_engine!=g_eng||menu_chain!=g_sel_chain||menu_slot!=g_sel_slot||
       g_eng->chains[g_sel_chain].units[g_sel_slot]!=menu_identity){close_menu();return;}
    if(item==0){open_picker(true);return;}
    if(item==1){copy_unit();close_menu();return;}
    if(item==2){if(!g_clip_unit||empty_slot()<0)return;paste_unit();close_menu();return;}
    if(item!=3)return;
    {platform::AudioOut::Guard guard(*g_audio);if(!structure_ready_locked())return;}
    if(delete_chain!=g_sel_chain||delete_slot!=g_sel_slot||osGetTime()>delete_until){
        delete_chain=g_sel_chain;delete_slot=g_sel_slot;delete_until=osGetTime()+1500;return;
    }
    WorkspaceLayout before;workspace_get_layout(before);
    {platform::AudioOut::Guard guard(*g_audio);
        if(structure_ready_locked()&&g_eng->remove_unit_with_undo(g_sel_chain,g_sel_slot)){
            undo_layout=before;undo_layout_valid=true;
            workspace_remove_slot(g_sel_chain,g_sel_slot);
            if(g_sel_slot>0&&!g_eng->chains[g_sel_chain].units[g_sel_slot])--g_sel_slot;
            g_edit_slot=g_sel_slot;mark_project_structural_mutated();
        }
    }
    delete_slot=-1;close_menu();
}
void perform(Action action){
    if(!available()||selected==cc::Add)return;
    if(action==Action::More){open_menu();return;}
    if(action==Action::Edit){close_menu();g_edit_chain=g_sel_chain;g_edit_slot=g_sel_slot;chains_editor_begin();g_overlay=Overlay::Editor;g_drag=Drag::None;return;}
    if(action==Action::Bypass){platform::AudioOut::Guard guard(*g_audio);
        auto* u=g_eng->chains[g_sel_chain].units[g_sel_slot];
        if(u){u->bypass=!u->bypass;mark_project_mutated();}}
}
int primary(const Unit& unit,int which){
    const UnitKind k=unit.kind;
    // Deliberate frequent controls; REC command and destructive/sample operations
    // remain in their editor. All ids are the existing persistent ParamBank ids.
    switch(k){
    case UnitKind::SamplePlayer:return which?PlayUnit::P_GAIN:
        static_cast<const PlayUnit&>(unit).pitch_mode()==PlayUnit::PitchMode::Legacy?PlayUnit::P_SPEED:PlayUnit::P_NOTE;
    case UnitKind::Pent:return which?PentUnit::P_BOND:PentUnit::P_ROOT;
    case UnitKind::Paul:return which?PaulUnit::P_WINDOW:PaulUnit::P_STRETCH;
    case UnitKind::Scratch:{const auto& s=static_cast<const ScratchUnit&>(unit);int x,y;s.axis_params(x,y);return which?y:x;}
    case UnitKind::Osc:return which?OscUnit::P_GAIN:OscUnit::P_PITCH;
    case UnitKind::Noise:return which?NoiseUnit::P_GAIN:NoiseUnit::P_TONE;
    case UnitKind::Fm:return which?FmUnit::P_INDEX:FmUnit::P_PITCH;
    case UnitKind::Motor:return which?MotorUnit::P_LOAD:MotorUnit::P_RPM;
    case UnitKind::Bird:return which?BirdUnit::P_TIME:BirdUnit::P_PITCH;
    case UnitKind::Inst:return which?InstUnit::P_LEVEL:InstUnit::P_NOTE;
    case UnitKind::Recorder:return which?RecUnit::P_MON:RecUnit::P_LEN;
    case UnitKind::Delay:return which?DelayUnit::P_MIX:DelayUnit::P_TIME;
    case UnitKind::Chorus:return which?ChorusUnit::P_MIX:ChorusUnit::P_RATE;
    case UnitKind::Reverb:return which?ReverbUnit::P_MIX:ReverbUnit::P_SIZE;
    case UnitKind::Ring:return which?RingUnit::P_MIX:RingUnit::P_FREQ;
    case UnitKind::Fold:return which?FoldUnit::P_MIX:FoldUnit::P_DRIVE;
    case UnitKind::Bitcrush:return which?BitUnit::P_MIX:BitUnit::P_BITS;
    case UnitKind::Strain:return which?StrainUnit::P_MIX:StrainUnit::P_DRIVE;
    case UnitKind::Stranular:return which?StranularUnit::P_MIX:StranularUnit::P_SIZE;
    case UnitKind::Tilt:return which?TiltUnit::P_MIX:TiltUnit::P_TILT;
    case UnitKind::Ghost:return which?GhostUnit::P_MIX:GhostUnit::P_TONE;
    case UnitKind::Shift:return which?ShiftUnit::P_MIX:ShiftUnit::P_SHIFT;
    case UnitKind::Glitch:return which?GlitchUnit::P_MIX:GlitchUnit::P_DENSITY;
    case UnitKind::Arp:return which?ArpUnit::P_GATE:ArpUnit::P_RATE;
    case UnitKind::Fract:return which?FractUnit::P_MIX:FractUnit::P_SIZE;
    default:return which; // GRAN pos/size, FILT cutoff/res, VCA gain/attack, MIC gain/input, COMP threshold/ratio
    }
}
void slider(int x){
    if(!touch_valid()||touch.param<0)return;
    platform::AudioOut::Guard guard(*g_audio);
    Unit* u=g_eng->chains[touch.chain].units[touch.slot];
    if(touch.param>=u->num_params)return;
    if(u->kind==UnitKind::Recorder&&(static_cast<RecUnit*>(u)->writer_open()||g_eng->chain_record_pending(touch.chain)))return;
    Param& p=u->params[touch.param];
    const int v=std::clamp(x-touch.rail_x,0,touch.rail_w)*32767/touch.rail_w;
    const q15 raw=q15(p.bipolar?v*2-32767:v);
    const q15 value=u->kind==UnitKind::SamplePlayer&&touch.param==PlayUnit::P_NOTE?
        musical_note::encode(musical_note::decode(raw)):raw;
    if(p.base!=value){p.base=value;mark_project_mutated();}
}
void drop(){
    if(!touch_valid()||touch.kind!=Gesture::Move)return;
    int preview[UNITS_PER_CHAIN];cc::preview_order(order,count,touch.slot,touch.gap,preview);
    int from_index=0;while(from_index<count&&order[from_index]!=touch.slot)++from_index;
    if(from_index==touch.gap)return;
    const int before=touch.gap+1<count?preview[touch.gap+1]:UNITS_PER_CHAIN;
    WorkspaceLayout previous;workspace_get_layout(previous);
    platform::AudioOut::Guard guard(*g_audio);
    const int slot=structure_ready_locked()?g_eng->move_unit_before_with_undo(g_sel_chain,touch.slot,before):-1;
    if(slot>=0&&slot!=touch.slot){
        undo_layout=previous;undo_layout_valid=true;
        workspace_move_slot(g_sel_chain,touch.slot,slot);
        g_sel_slot=g_edit_slot=slot;mark_project_structural_mutated();
    }
}
void lower_name(char* out,size_t size,const char* name){
    size_t i=0;for(;name[i]&&i+1<size;++i)out[i]=name[i]>='A'&&name[i]<='Z'?char(name[i]+'a'-'A'):name[i];out[i]=0;
}
void value_text(char* out,size_t n,const ParamInspector& p,q15 value,const Tile& tile){
    // Percent is normalized control position unless a shared, mode-independent
    // physical law is available. Never present parameter state as audio peaks.
    const auto k=UnitKind(p.kind);
    if(k==UnitKind::SamplePlayer){
        play_readout::value_text(out,n,p.param,value,tile.play_modern,tile.play_quantize,tile.play_pitch_mode,frame.scale);return;
    }
    if(k==UnitKind::Pent)pent_readout::value_text(out,n,p.param,value);
    else if(k==UnitKind::Paul)paul_readout::value_text(out,n,p.param,value);
    else if(k==UnitKind::Scratch)scratch_readout::value_text(out,n,p.param,value);
    else if(k==UnitKind::Rule)rule_readout::value_text(out,n,p.param,value);
    else if(k==UnitKind::Rot)rot_readout::value_text(out,n,p.param,value);
    else if(k==UnitKind::Inst)inst_readout::value_text(out,n,p.param,value);
    else if(k==UnitKind::Arp&&p.param==ArpUnit::P_RATE)std::snprintf(out,n,"%s",ArpUnit::rate_name(ArpUnit::rate_index(value)));
    else if(k==UnitKind::Arp&&p.param==ArpUnit::P_GATE)std::snprintf(out,n,"%d%%",ArpUnit::gate_percent(value));
    else if(k==UnitKind::Ring&&p.param==RingUnit::P_FREQ)std::snprintf(out,n,"%.0fhz",ring_reference::hz(value));
    else if(k==UnitKind::Bird&&p.param==BirdUnit::P_PITCH)std::snprintf(out,n,"%uhz",unsigned((BirdUnit::pitch_millihz(value)+500)/1000));
    else if(k==UnitKind::Bird&&p.param==BirdUnit::P_TIME)std::snprintf(out,n,"%ums",unsigned((BirdUnit::duration_samples(value)+16)/32));
    else if(k==UnitKind::Shift&&p.param==ShiftUnit::P_SHIFT)std::snprintf(out,n,"%+.1fhz",double(ShiftUnit::shift_millihz(value))/1000);
    else if(k==UnitKind::Grain&&p.param==GrainUnit::P_SIZE)std::snprintf(out,n,"%ums",unsigned((GrainUnit::grain_length_frames(value)+16)/32));
    else std::snprintf(out,n,p.bipolar?"%+d%%":"%d%%",int(value)*100/32767);
}
int position(q15 value,bool bipolar,int left,int span){return left+(std::clamp(int(value),bipolar?-32767:0,32767)+(bipolar?32767:0))*span/(bipolar?65534:32767);}
uint32_t active_paper(){return sigil_surface::Cache::blend(m::paper_color(120),m::spot(),7,100);}

template<class P> void param_rail(const P& p,const ParamInspector& v,int left,int right,int y,bool live){
    const int base=position(v.base,v.bipolar,left,right-left);
    const int effective=position(live?v.effective:v.base,v.bipolar,left,right-left);
    // The upper selected card has a real captured effective value. Lower card
    // controls are base-only; no duplicated live marker is invented for them.
    module_fader::horizontal(p,left,right,y,base,effective,
        {live&&v.mod_enabled&&(v.source!=ModSource::None||v.relic_source),false,false},v.bipolar?(left+right)/2:-1);
}
// New card titles reuse the registered private masks at the existing card
// anchor. Older menu masks stay byte-identical; the painter keeps native/cache
// clipping and disclosure opacity, so no shared atlas/layout change is needed.
template<class P> void card_heading(const P& p,int x,int y,UnitKind kind,uint32_t color){
    if(kind!=UnitKind::Pent&&kind!=UnitKind::Scratch&&kind!=UnitKind::Paul&&kind!=UnitKind::Rule&&kind!=UnitKind::Rot){
        p.heading(x,y,m::module_name(kind),color);return;}
    const auto* at=module_heading::label(kind);
    if(!at)return;
    const int stride=(at->width+7)/8;
    const auto* bitmap=module_heading_data::bitmap+at->offset;
    for(int row=0;row<at->height;++row){int run=-1;
        for(int col=0;col<=at->width;++col){const bool on=col<at->width&&(bitmap[row*stride+col/8]&(0x80u>>(col&7)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){p.rect(x+run,y+row,col-run,1,color);run=-1;}}}
}
template<class P> void tile(P p,int slot,bool top){
    const auto b=frame.layout.box[slot];if(!b.w)return;
    const int x=b.x+(top?40:0),right=x+b.w-3,y0=top?40:0,y1=top?239:197;
    const bool chosen=frame.selected==slot;
    const auto& t=frame.tiles[slot];
    p.clip_left=x+2;p.clip_right=right+1;p.clip_top=y0;p.clip_bottom=y1+1;
    // Keep controls calm and the floating selected tile opaque. Narrow tiles
    // expose the same live/frozen paper already painted beneath both screens.
    if(chosen)p.rect(x+2,y0,b.w-4,y1-y0+1,active_paper());
    const auto color=chosen?m::spot():m::branch();
    p.line(x+2,y0,x+2,y1,color);p.line(right,y0,right,y1,color);
    p.line(x+2,top?y0:y1,right,top?y0:y1,color);
    if(top){
        const char* name=m::module_name(t.kind);
        if(chosen&&b.w>=78)card_heading(p,x+14,47,t.kind,m::spot());
        else {char short_name[5];std::snprintf(short_name,sizeof short_name,"%.4s",name);
            p.text(x+(b.w-ui::text_w(short_name))/2,55,short_name,t.bypass?m::ghost():m::ink());}
        if(chosen&&b.w>=132){
            const auto& v=t.control[0];char value[24],label[24],param[16];
            lower_name(param,sizeof param,v.name);p.text(x+14,81,"live control",m::ghost());
            value_text(value,sizeof value,v,v.effective,t);p.text(x+14,101,value,m::ink());
            p.right(right-12,101,param,m::ghost());
            param_rail(p,v,x+14,right-12,127,true);
            value_text(value,sizeof value,v,v.target,t);std::snprintf(label,sizeof label,"target %.14s",value);
            p.text(x+14,149,label,m::ghost());
            static const char* layers[]={"base","step lock","audition","performance","temp"};
            p.text(x+14,163,t.bypass?"bypassed":layers[std::min(4,int(v.layer))],t.bypass?m::ghost():m::ink());
            p.line(x+14,176,right-12,176,m::branch());
            if(frame.route.valid){
                const auto& r=frame.route;char route[24];lower_name(param,sizeof param,r.name);
                const int sigil=mod_source_sigil(r.source);
                if(r.relic_source){constexpr const char* ports[]={"pull","comp","bend","wave"};std::snprintf(route,sizeof route,"r.%s > %.8s",ports[std::min(3,int(r.relic_port))],param);}
                else if(sigil>=0)std::snprintf(route,sizeof route,"%c > %.10s",'a'+sigil,param);
                else std::snprintf(route,sizeof route,"env > %.8s",param);
                p.text(x+14,185,route,r.mod_enabled?m::spot():m::ghost());
                p.line(x+14,205,right-12,205,m::branch());
                const int mid=(x+14+right-12)/2;
                p.line(mid,202,mid,208,m::branch());
                const int mark=position(r.source_value,true,x+14,right-12-(x+14));
                p.line(mid,205,mark,205,m::spot());p.rect(mark-1,203,3,5,m::spot());
                std::snprintf(label,sizeof label,r.mod_enabled?"depth %+d%%":"muted %+d%%",int(r.depth)*100/32767);
                p.text(x+14,222,label,r.mod_enabled?m::spot():m::ghost());
            } else {p.text(x+14,185,"no modulation",m::ghost());p.text(x+14,222,"base controls below",m::ghost());}
        } else if(!chosen||b.w<78){
            // Two true effective-value rails; no decorative pretend waveform.
            for(int i=0;i<2;++i){const auto& v=t.control[i];if(!v.valid)continue;
                const int xx=x+b.w/2-5+i*10;
                p.line(xx,91,xx,151,m::branch());
                const int yy=151-position(v.effective,v.bipolar,0,60);
                p.line(xx-2,yy,xx+2,yy,t.bypass?m::ghost():m::ink());}
            if(t.bypass)p.text(x+(b.w-18)/2,170,"off",m::ghost());
            const char index[]={'0',char('1'+slot),0};p.text(x+(b.w-12)/2,222,index,m::ghost());
        }
    } else {
        if(chosen&&b.w>=132){
            p.text(x+12,22,"edit",m::spot());p.line(x+12,35,x+35,35,m::spot());
            p.text(x+54,22,"bypass",t.bypass?m::spot():m::ghost());
            p.text(x+b.w-40,22,"more",m::ghost());
            for(int i=0;i<2;++i){const auto& v=t.control[i];if(!v.valid)continue;
                const int y=70+i*63;char name[16],value[24];lower_name(name,sizeof name,v.name);
                value_text(value,sizeof value,v,v.base,t);p.text(x+14,y,name,m::ghost());
                p.right(right-12,y,value,m::ink());param_rail(p,v,x+14,right-12,y+24,false);}
        }
        for(int dx:{-5,0,5})p.rect(x+b.w/2+dx,GRIP_Y-1,2,2,chosen?m::spot():m::ghost());
    }
}
template<class P> void selection_glow(P p,bool top){
    if(frame.selected>=UNITS_PER_CHAIN)return; // no halo around the add target
    const auto b=frame.layout.box[frame.selected];
    if(!b.w||frame.tiles[frame.selected].kind==UnitKind::Empty)return;
    const int x=b.x+(top?40:0),y=top?40:0,h=top?200:198;
    p.clip_top=top?36:0;p.clip_bottom=top?240:206;
    // Eight disjoint 1px falloff shells, at most 6.7% accent. Behind every tile
    // and label; same native/cache command stream, no texture or live pulse.
    constexpr unsigned alpha[]={17,13,10,7,5,3,2,1};
    for(int d=8;d>=1;--d)
        p.frame(x+2-d,y-d,b.w-4+2*d,h+2*d,(m::spot()&0x00ffffffu)|(alpha[d-1]<<24));
}
template<class P> void add_label(const P& p){
    const auto b=frame.layout.box[cc::Add];if(!b.w)return;
    const int center=b.x+b.w/2;
    const auto color=frame.selected==cc::Add?m::spot():m::ghost();
    // Retain the header lane: the sharp add anchor must clear picker rows.
    p.text(center-ui::text_w("+")/2,22,"+",color);
    p.text(center-ui::text_w("add")/2,40,"add",color);
}
template<class P> void background(const P& p,bool top){
    if(top)p.heading(40,8,"patch table",m::ink());
    if(!frame.valid)return;
    selection_glow(p,top);
    if(frame.dragged>=0){
        const int x=cc::group_left(frame.count)+frame.gap*cc::Narrow+(top?40:0);
        for(int y=top?40:0;y<(top?240:198);y+=8){p.line(x+2,y,x+2,y+3,m::branch());p.line(x+157,y,x+157,y+3,m::branch());}
    }
    for(int s=0;s<UNITS_PER_CHAIN;++s)if(frame.tiles[s].kind!=UnitKind::Empty&&s!=frame.dragged)tile(p,s,top);
    if(frame.dragged>=0)tile(p,frame.dragged,top);
    if(frame.layout.box[cc::Add].w){
        if(top){if(!frame.count){const char* text="empty chain / add a module below";p.text(200-ui::text_w(text)/2,136,text,m::ghost());}}
        else add_label(p);
    }
    if(!top){
        if(frame.dragged>=0){const int x=cc::group_left(frame.count)+frame.gap*cc::Narrow+80;p.line(x-4,206,x,202,m::spot());p.line(x,202,x+4,206,m::spot());}
        for(int i=0;i<NUM_CHAINS;++i){const int x=18+i*36;char num[2]={char('1'+i),0};
            p.text(x,219,num,i==frame.chain?m::spot():m::ghost());if(i==frame.chain)p.line(x-5,233,x+10,233,m::spot());}
        p.right(304,219,frame.patch,m::ghost());
        if(frame.status[0])p.text(16,202,frame.status,m::ghost());
    }
}
template<class Cache> void capture_background(Cache& cache,bool top){
    // Sample the same captured paper phase/UVs as the ambient texture. No live
    // engine/GPU reads, and no per-frame blur: only disclosure/theme refresh.
    for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<Cache::W;++x)
        cache.pixels[y*Cache::W+x]=m::patch_sample(top,float(x*2+1),float(y*2+1));
    background(sigil_surface::BasicPainter<Cache::W>{&cache},top);
    cache.finish();
}
void sharp_anchor(bool top){
    if(top){if(frame.selected<UNITS_PER_CHAIN){const auto b=frame.layout.box[frame.selected];
        sigil_surface::TopPainter p;p.clip_left=40+b.x+2;p.clip_right=40+b.x+b.w-2;
        card_heading(p,40+b.x+14,47,frame.tiles[frame.selected].kind,m::spot());}}
    else if(frame.add)add_label(sigil_surface::Painter{});
    else ui::text(frame.anchor_x,22,"more",m::spot());
}
void draw_modal_background(bool top){
    if(top){m::paper(400);if(!sigil_surface::top_cache.draw()){background(sigil_surface::TopPainter{},true);m::paper_rect(0,0,400,240,185);}}
    else {m::paper(320);if(!sigil_surface::bottom_cache.draw()){background(sigil_surface::Painter{},false);m::paper_rect(0,0,320,240,185);}}
    sharp_anchor(top);
}
// Snapshot once before disclosure. Drawing consumes only this pointer-free DTO.
void capture_frame(bool modal,bool picker){
    frame={};frame.valid=true;frame.chain=g_sel_chain;frame.count=count;frame.selected=selected;
    frame.layout=motion.shown;frame.modal=modal;frame.picker=picker;frame.add=selected==cc::Add;
    if(touch.kind==Gesture::Move){frame.dragged=touch.slot;frame.gap=touch.gap;frame.layout.box[touch.slot]={touch.float_x,cc::Wide};}
    frame.anchor_x=frame.layout.box[selected].x+frame.layout.box[selected].w-40;
    frame.menu_x=std::clamp(frame.anchor_x+26,24,236);
    {
        platform::AudioOut::Guard guard(*g_audio);
        frame.scale=g_eng->project_scale();
        for(int s=0;s<UNITS_PER_CHAIN;++s){const Unit* u=g_eng->chains[g_sel_chain].units[s];if(!u)continue;
            auto& t=frame.tiles[s];t.kind=u->kind;t.bypass=u->bypass;
            if(u->kind==UnitKind::SamplePlayer){const auto& play=*static_cast<const PlayUnit*>(u);
                t.play_modern=play.modern();t.play_quantize=play.pitch_quantize;t.play_pitch_mode=play.pitch_mode();}
            for(int i=0;i<2;++i)g_eng->capture_param_inspector(g_sel_chain,s,primary(*u,i),t.control[i]);
            if(s==selected)for(int p=0;p<u->num_params;++p)if(u->params[p].src!=ModSource::None||u->params[p].relic_source){g_eng->capture_param_inspector(g_sel_chain,s,p,frame.route);break;}
        }
        if(g_eng->chains[g_sel_chain].mute)std::snprintf(frame.status,sizeof frame.status,"chain muted");
    }
    const int patch=project_active_slot();
    if(patch>=0)std::snprintf(frame.patch,sizeof frame.patch,"patch%02d%s",patch+1,project_is_dirty()?" *":"");
    else std::snprintf(frame.patch,sizeof frame.patch,"new *");
    if(notice&&osGetTime()<notice_until)std::snprintf(frame.status,sizeof frame.status,"%s",notice);
    if(frame.dragged>=0)frame.status[0]=0;
}

// Local disclosure opacity/clipping adapter; shared painters/atlases unchanged.
template<int W> struct FadedPainter : sigil_surface::BasicPainter<W> {
    using Base=sigil_surface::BasicPainter<W>;
    unsigned opacity=255;
    int mask_left=0,mask_top=0,mask_right=W*2,mask_bottom=240;
    uint32_t tint(uint32_t c) const {return (c&0xffffffu)|((((c>>24)*opacity+127)/255)<<24);}
    Base clipped() const {
        Base p=*this;
        p.clip_left=std::max(p.clip_left,mask_left);p.clip_right=std::min(p.clip_right,mask_right);
        p.clip_top=std::max(p.clip_top,mask_top);p.clip_bottom=std::min(p.clip_bottom,mask_bottom);
        return p;
    }
    void rect(int x,int y,int w,int h,uint32_t c) const {clipped().rect(x,y,w,h,tint(c));}
    void line(int x,int y,int r,int b,uint32_t c) const {clipped().line(x,y,r,b,tint(c));}
    void frame(int x,int y,int w,int h,uint32_t c) const {clipped().frame(x,y,w,h,tint(c));}
    void text(int x,int y,const char* s,uint32_t c) const {clipped().text(x,y,s,tint(c));}
    void right(int x,int y,const char* s,uint32_t c) const {clipped().right(x,y,s,tint(c));}
    int heading(int x,int y,const char* s,uint32_t c) const {return clipped().heading(x,y,s,tint(c));}
};

// The existing frozen lower cache, one alpha-tinted GPU quad. No new texture,
// readback, blur per tick or full-resolution framebuffer allocation.
bool picker_cache(unsigned opacity){
    const auto& cache=sigil_surface::bottom_cache;
    if(!cache.valid)return false;
#ifdef __3DS__
    if(!cache.ready)return false;
    C2D_ImageTint tint;C2D_AlphaImageTint(&tint,float(opacity)/255);
    const C2D_Image image{const_cast<C3D_Tex*>(&cache.texture),&cache.sub};
    C2D_DrawImageAt(image,0,0,0,&tint,2,2);
#else
    for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;){
        int end=x+1;while(end<sigil_surface::W&&cache.pixels[y*sigil_surface::W+end]==cache.pixels[y*sigil_surface::W+x])++end;
        ui::rect(x*2,y*2,(end-x)*2,2,(cache.pixels[y*sigil_surface::W+x]&0xffffffu)|(opacity<<24));x=end;
    }
#endif
    return true;
}

template<int W> void picker_surface(bool top){
    using namespace picker_motion;
    const float t=state.progress;
    const int width=top?400:320;
    m::paper(width);
    FadedPainter<W> p;
    if(t<=0){background(p,top);return;}
    const bool active=frame.selected<UNITS_PER_CHAIN&&frame.tiles[frame.selected].kind!=UnitKind::Empty;
    const auto tile_box=frame.layout.box[active?frame.selected:cc::Add];
    const int center=tile_box.w?tile_box.x+tile_box.w/2:160;
    const Bounds origin=active?Bounds{tile_box.x+2,0,tile_box.x+tile_box.w-3,197}:Bounds{center-1,22,center+1,48};
    const auto box=bounds(origin,t,top);
    if(top){
        p.opacity=unsigned(std::lround(255*(1-smooth(t/.30f))));
        if(p.opacity)background(p,true);
    }else{
        const unsigned wash=unsigned(std::lround(255*smooth(t/.8f)));
        if(wash<255)background(p,false);
        if(!picker_cache(wash))m::paper_rect(0,0,320,240,int(wash*185/255));
        // Same absolute paper UVs; the blur's quiet veil never covers the tile.
        m::paper_rect(0,0,320,240,int(wash*42/100));
        m::paper_rect(0,217,320,23,int(wash));
    }
    p.opacity=255;
    p.rect(box.left,box.top,box.right-box.left+1,box.bottom-box.top+1,active_paper());
    const float old=1-smooth(t/(top?.34f:.42f));
    if(active&&old>0){
        auto old_p=p;old_p.opacity=unsigned(std::lround(255*old));
        old_p.mask_left=tile_box.x+(top?40:0)+3;
        old_p.mask_right=tile_box.x+(top?40:0)+tile_box.w-3;
        old_p.mask_top=top?41:0;old_p.mask_bottom=top?240:197;
        tile(old_p,frame.selected,top);
    }
    const float strength=(1-.4f*smooth(t))*(active?1:smooth(t/.45f));
    constexpr unsigned alpha[]={17,13,10,7,5,3,2,1};
    for(int d=8;d>=1;--d){
        const auto color=(m::spot()&0xffffffu)|(unsigned(std::lround(alpha[d-1]*strength))<<24);
        p.frame(box.left-d,box.top-d,box.right-box.left+1+2*d,box.bottom-box.top+1+2*d,color);
    }
    const auto edge=(m::spot()&0xffffffu)|(unsigned(std::lround(255*strength))<<24);
    p.line(box.left,box.top,box.left,box.bottom,edge);
    p.line(box.right,box.top,box.right,box.bottom,edge);
    p.line(box.left,top?box.top:box.bottom,box.right,top?box.top:box.bottom,edge);
    picker_draw_content(top,box.left,box.top,box.right,box.bottom,t);
}
template<int W> void editor_surface(bool top){
    using namespace editor_transition;
    const float t=state.progress;
    const int width=top?400:320;
    FadedPainter<W> p;
    if(t<=0){m::paper(width);background(p,top);return;}
    const auto tile_box=frame.layout.box[frame.selected];
    const auto box=bounds({tile_box.x+2,0,tile_box.x+tile_box.w-3,197},t,top);
    // The already-drawn native editor is covered, not transformed. Outside the
    // growing frame restore the frozen patch sheet at its original UVs.
    m::paper(width,0,0);
    const int l=std::clamp(box.left,0,width),r=std::clamp(box.right+1,0,width);
    const int y=box.top,b=box.bottom+1;
    const int strips[4][4]={{0,0,width,y},{0,b,width,240-b},{0,y,l,b-y},{r,y,width-r,b-y}};
    p.opacity=unsigned(std::lround(255*(1-smooth(t/.32f))));
    for(const auto& s:strips){
        if(s[2]<=0||s[3]<=0)continue;
        m::paper_rect(s[0],s[1],s[2],s[3]);
        if(p.opacity){p.mask_left=s[0];p.mask_top=s[1];p.mask_right=s[0]+s[2];p.mask_bottom=s[1]+s[3];background(p,top);}
    }
    const int cover=int(std::lround(255*(1-smooth((t-.55f)/.45f))));
    editor_style::paper(width,0,0);
    editor_style::paper_rect(box.left,box.top,box.right-box.left+1,box.bottom-box.top+1,cover);
    p=FadedPainter<W>{};
    p.opacity=unsigned(std::lround(255*(1-smooth(t/.65f))));
    if(p.opacity)p.rect(box.left,box.top,box.right-box.left+1,box.bottom-box.top+1,active_paper());
    // Existing native text/rails dissolve at their old positions, never stretch.
    p.opacity=unsigned(std::lround(255*(1-smooth(t/.30f))));
    if(p.opacity){
        p.mask_left=tile_box.x+(top?40:0)+3;p.mask_right=tile_box.x+(top?40:0)+tile_box.w-3;
        p.mask_top=top?41:0;p.mask_bottom=top?240:197;
        tile(p,frame.selected,top);
    }
    p=FadedPainter<W>{};p.opacity=unsigned(std::lround(255*(1-smooth((t-.40f)/.60f))));
    if(p.opacity){
        constexpr unsigned halo[]={17,13,10,7,5,3,2,1};
        for(int d=8;d>=1;--d)p.frame(box.left-d,box.top-d,box.right-box.left+1+2*d,box.bottom-box.top+1+2*d,(m::spot()&0xffffffu)|(halo[d-1]<<24));
        p.line(box.left,box.top,box.left,box.bottom,m::spot());
        p.line(box.right,box.top,box.right,box.bottom,m::spot());
        p.line(box.left,top?box.top:box.bottom,box.right,top?box.top:box.bottom,m::spot());
    }
}
} // namespace

void chains_editor_begin(){
    editor_forget();
    if(!available()||nav_is_open()||g_overlay!=Overlay::None||
       !frame.valid||frame.modal||frame.picker||frame.dragged>=0||frame.chain!=g_sel_chain||
       frame.selected!=g_sel_slot||g_sel_slot<0||g_sel_slot>=UNITS_PER_CHAIN||
       context_engine!=g_eng||context_chain!=g_sel_chain)return;
    {
        platform::AudioOut::Guard guard(*g_audio);
        for(int s=0;s<UNITS_PER_CHAIN;++s)if(identities[s]!=g_eng->chains[g_sel_chain].units[s])return;
        if(!g_eng->chains[g_sel_chain].units[g_sel_slot])return;
        for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s){
            const Unit* u=g_eng->chains[c].units[s];editor_units[c][s]=g_eng->chains[c].units[s];
            editor_kinds[c][s]=u?u->kind:UnitKind::Empty;
        }
    }
    editor_engine=g_eng;editor_audio=g_audio;editor_chain=g_sel_chain;editor_slot=g_sel_slot;
    motion.from=motion.target=motion.shown=frame.layout;motion.began=osGetTime();motion.ready=true;
    editor_transition::begin(osGetTime());editor_visible=true;
    m::patch_capture(true,true,osGetTime());
}
bool chains_editor_back(){
    if(!editor_context()){editor_forget();return false;}
    // Refresh values/bypass/mute/status, never restore a screenshot from entry.
    capture_frame(false,false);
    editor_transition::close(osGetTime());return true;
}
void chains_editor_cancel_input(){editor_forget();editor_drain=0;}
bool chains_editor_input(u32& down,u32& held,u32& up){
    const u32 blocked=editor_drain;
    editor_drain&=held;
    down&=~blocked;up&=~blocked;
    const u32 physical_held=held;held&=~blocked;
    const bool skip=(blocked&KEY_TOUCH)!=0;
    if(!editor_transition::state.active)return skip;
    if(!editor_context()){editor_forget();return skip;}
    if(editor_transition::state.closing){
        constexpr u32 global=KEY_SELECT|KEY_START|KEY_Y|KEY_ZL;
        const bool global_edge=(down&(KEY_SELECT|KEY_START|KEY_Y))!=0;
        const u32 pass=global|(global_edge?(KEY_L|KEY_R):0);
        // Preserve the physical shoulder chord on global actions: L+START must
        // stay unassigned, and R+START must stop/reset rather than toggle play.
        editor_drain=(editor_drain|physical_held)&~pass;
        if(down&~KEY_ZL)editor_finish();
        down&=global;held=physical_held&pass;up&=pass;
        return true;
    }
    // A fresh input needs no second tap. B retains the current phase so a quick
    // back can reverse it; other actions (including MOD) see final hit geometry.
    if(down&~(KEY_B|KEY_ZL))editor_transition::snap();
    return skip;
}
void chains_editor_capture(){
    editor_visible=false;
    if(!editor_transition::state.active)return;
    if(!editor_context()){editor_forget();return;}
    if(editor_transition::sample(osGetTime())){editor_finish();return;}
    if(editor_transition::state.closing)capture_frame(false,false);
    editor_visible=editor_transition::state.progress<1;
}
bool chains_editor_covers(){return editor_visible&&editor_transition::state.progress<=.55f;}
void chains_editor_draw(bool top){
    if(!editor_visible)return;
    if(top)editor_surface<sigil_surface::TopW>(true);else editor_surface<sigil_surface::W>(false);
}

void chains_picker_begin(){
    // Prefer the LAST PRESENTED geometry, including an interrupted tile slide.
    // Never select Add/snap before taking this copy. Fallback only on first entry
    // or after another owner (e.g. GO TO) invalidated the shared disclosure DTO.
    if(!available())return;
    bool same=frame.valid&&frame.chain==g_sel_chain&&context_engine==g_eng&&context_chain==g_sel_chain;
    if(same)for(int s=0;s<UNITS_PER_CHAIN;++s)same&=identities[s]==g_eng->chains[g_sel_chain].units[s];
    if(!same){if(!sync_context())return;capture_frame(true,true);}
    frame.modal=true;frame.picker=true;frame.dragged=-1;
    motion.from=motion.target=motion.shown=frame.layout;
    motion.began=osGetTime();motion.ready=true;
    picker_origin_ready=true;blur_refresh=true;
    m::patch_capture(true,true,osGetTime());
}

bool chains_context_is_open(){return menu_open;}
void chains_close_context_branch(){close_menu();delete_slot=-1;blur_refresh=true;}
bool chains_confirm_editor_delete(){
    if(!available()||nav_is_open()||g_overlay!=Overlay::Editor||
       g_edit_chain<0||g_edit_chain>=NUM_CHAINS||g_edit_slot<0||g_edit_slot>=UNITS_PER_CHAIN||
       !g_eng->chains[g_edit_chain].units[g_edit_slot])return false;
    {platform::AudioOut::Guard guard(*g_audio);if(!structure_ready_locked())return false;}
    g_sel_chain=g_edit_chain;g_sel_slot=g_edit_slot;
    g_overlay=Overlay::None;g_drag=Drag::None;
    close_menu();sync_context();open_menu();menu_cursor=3;
    menu_action(3); // arm only; open_menu/close_menu discarded any old approval
    return menu_open;
}
bool chains_context_buttons(u32 down){
    if(!menu_open)return false;
    if(down)cancel_touch();
    if(down&(KEY_B|KEY_SELECT|KEY_START|KEY_Y)){close_menu();return true;}
    if(down&(KEY_UP|KEY_LEFT))menu_cursor=(menu_cursor+3)%4;
    if(down&(KEY_DOWN|KEY_RIGHT))menu_cursor=(menu_cursor+1)%4;
    if(down&(KEY_UP|KEY_LEFT|KEY_DOWN|KEY_RIGHT))delete_slot=-1;
    if(down&KEY_A)menu_action(menu_cursor);
    return true;
}
void chains_view_touch_down(int x,int y){
    cancel_touch();if(!sync_context()||g_overlay!=Overlay::None)return;
    touch.engine=g_eng;touch.audio=g_audio;touch.chain=g_sel_chain;touch.x=x;touch.y=y;touch.began=osGetTime();
    if(menu_open){
        touch.item=menu_hit(x,y);if(touch.item<0){close_menu();return;}
        if(touch.item!=3)delete_slot=-1;
        touch.kind=Gesture::Menu;menu_cursor=touch.item;return;
    }
    if(y>=211){
        if(x>=4&&x<148){touch.kind=Gesture::Chain;touch.item=std::clamp((x-4)/36,0,3);}return;
    }
    if(y<0||y>=BODY_END)return;
    const int s=cc::hit(motion.shown,x);if(s<0)return;
    if(s==cc::Add){touch.kind=Gesture::Add;return;}
    const bool was_selected=selected==s;
    const auto box=motion.shown.box[s];
    select_slot(s);motion.aim(cc::layout(order,count,selected),osGetTime());
    touch.slot=s;touch.identity=g_eng->chains[g_sel_chain].units[s];touch.kind=Gesture::Tap;
    if(y>=174&&std::abs(x-(box.x+box.w/2))<=14){
        touch.kind=Gesture::Grip;touch.offset=was_selected?x-box.x:80;
        for(int i=0;i<count;++i)if(order[i]==s)touch.gap=i;
        return;
    }
    // First tap selects only: a collapsing/expanding neighbour cannot steal a
    // button/slider gesture intended for the previous geometry.
    if(!was_selected||box.w<152)return;
    if(y>=10&&y<44){
        const int local=x-box.x;
        touch.action=local<48?Action::Edit:local<112?Action::Bypass:Action::More;
    } else for(int i=0;i<2;++i)if(y>=58+i*63&&y<110+i*63){
        touch.kind=Gesture::Slider;touch.param=primary(*touch.identity,i);
        touch.rail_x=box.x+14;touch.rail_w=box.w-29;slider(x);break;
    }
}
void chains_view_touch_hold(int x,int y){
    if(touch.kind==Gesture::None)return;
    if(!touch_valid()){cancel_touch();return;}
    const int dx=x-touch.x,dy=y-touch.y;
    if(touch.kind==Gesture::Slider){slider(x);return;}
    if(touch.kind==Gesture::Grip&&(std::abs(dx)>=5||osGetTime()-touch.began>=180)){
        touch.kind=Gesture::Move;motion.aim(cc::layout(order,count,selected),osGetTime(),true);
    }
    if(touch.kind==Gesture::Move){
        if(x<0||x>=cc::Width||y<0||y>=211){cancel_touch();return;}
        const int left=cc::group_left(count);
        touch.float_x=std::clamp(x-touch.offset,left,left+(count-1)*cc::Narrow);
        touch.gap=std::clamp((touch.float_x-left+16)/cc::Narrow,0,count-1);
        int preview[UNITS_PER_CHAIN];cc::preview_order(order,count,touch.slot,touch.gap,preview);
        motion.aim(cc::layout(preview,count,selected),osGetTime());return;
    }
    if(std::abs(dx)>=5||std::abs(dy)>=5||(touch.kind==Gesture::Menu&&menu_hit(x,y)!=touch.item))cancel_touch();
}
void chains_view_touch_release(bool commit){
    if(touch.kind==Gesture::None)return;
    if(!commit||!touch_valid()){cancel_touch();return;}
    const auto saved=touch;
    if(touch.kind==Gesture::Move)drop();
    cancel_touch();
    if(saved.kind==Gesture::Tap)perform(saved.action);
    else if(saved.kind==Gesture::Menu)menu_action(saved.item);
    else if(saved.kind==Gesture::Add)open_picker(false);
    else if(saved.kind==Gesture::Chain){g_sel_chain=g_edit_chain=saved.item;close_menu();}
}
void chains_workspace_buttons(u32 down){
    if(!sync_context()||g_overlay!=Overlay::None)return;
    if(down&(KEY_A|KEY_B|KEY_X|KEY_ZR|KEY_LEFT|KEY_RIGHT|KEY_UP|KEY_DOWN|KEY_R))cancel_touch();
    if(down&KEY_B){close_menu();return;}
    if(down&KEY_R){if(selected==cc::Add)open_picker(false);else open_menu();return;}
    if(down&KEY_X){if(selected==cc::Add)open_picker(false);else perform(Action::Edit);return;}
    if(down&KEY_ZR){
        int chain=-1,slot=-1;
        platform::AudioOut::Guard guard(*g_audio);
        if(structure_ready_locked()&&g_eng->undo_structural_edit(&chain,&slot)){
            if(undo_layout_valid)for(int i=0;i<UNITS_PER_CHAIN;++i){const auto p=undo_layout.node[chain][i];workspace_set_point(chain,i,p.x,p.y);}
            undo_layout_valid=false;g_sel_chain=g_edit_chain=chain;g_sel_slot=g_edit_slot=slot;
            mark_project_structural_mutated();notify("structure restored");
        }return;
    }
    if(down&(KEY_UP|KEY_DOWN)){
        g_sel_chain=(g_sel_chain+NUM_CHAINS+((down&KEY_UP)?-1:1))%NUM_CHAINS;g_edit_chain=g_sel_chain;
        close_menu();sync_context();return;
    }
    if(down&(KEY_LEFT|KEY_RIGHT)){
        int index=count;for(int i=0;i<count;++i)if(order[i]==selected)index=i;
        const int total=count+(count<UNITS_PER_CHAIN);
        index=(index+total+((down&KEY_LEFT)?-1:1))%total;
        select_slot(index<count?order[index]:cc::Add);close_menu();
        motion.aim(cc::layout(order,count,selected),osGetTime());
    }
    // A remains the existing manual trigger; X / edit opens the full editor.
}

void chains_capture(){
    if(editor_transition::state.active&&g_overlay==Overlay::Editor){
        // Retain the origin/geometry and the last presented patch phase. No blur,
        // hidden main DTO capture, noise work or new texture during the editor.
        m::patch_capture(true,true,osGetTime());return;
    }
    if(!available()||nav_is_open()||(g_overlay!=Overlay::None&&g_overlay!=Overlay::Picker)){
        // Only Chains opts into live patch paper. Perform, transition and other
        // work pages return to quiet paper through the existing 240ms fade.
        m::patch_capture(false,false,osGetTime());
        close_menu();frame.valid=false;context_chain=-1;picker_origin_ready=false;blur_refresh=true;return;
    }
    const bool picker=g_overlay==Overlay::Picker;
    if(picker){
        if(!picker_origin_ready)chains_picker_begin();
    }else{
        picker_origin_ready=false;
        if(!sync_context()){m::patch_capture(false,false,osGetTime());frame.valid=false;return;}
    }
    const bool modal=menu_open||picker;
    // CPU surface capture is outside the audio guard, before fenced GPU upload.
    m::patch_capture(true,modal,osGetTime());
    const uint32_t colors[]={m::ink(),m::spot(),m::paper_color(0),m::paper_color(239)};
    const bool refresh=modal&&(blur_refresh||!frame.modal||frame.picker!=picker);
    // Modal backdrop and geometry stay frozen. A theme change recolors that same
    // DTO, it does not sample a new live background underneath the menu.
    if(!picker&&(!modal||refresh))capture_frame(modal,false);
    frame.menu_cursor=menu_cursor;frame.paste=g_clip_unit&&count<UNITS_PER_CHAIN;
    frame.confirm=delete_chain==g_sel_chain&&delete_slot==g_sel_slot&&osGetTime()<=delete_until;
    if(modal&&(refresh||std::memcmp(colors,blur_colors,sizeof colors))){
        capture_background(sigil_surface::bottom_cache,false);
        if(!picker)capture_background(sigil_surface::top_cache,true);
        std::memcpy(blur_colors,colors,sizeof colors);blur_refresh=false;
    }
}
void chains_draw_surface(bool top){
    if(!frame.valid){m::paper(top?400:320);return;}
    if(frame.picker){
        if(top)picker_surface<sigil_surface::TopW>(true);else picker_surface<sigil_surface::W>(false);
        return;
    }
    if(frame.modal){draw_modal_background(top);if(top)return;
        const int x=frame.menu_x;
        ui::line(frame.anchor_x+12,36,x-10,36,m::spot());ui::line(x-10,36,x-10,132,m::spot());
        const char* labels[]={"replace","copy","paste",frame.confirm?"sure?":"delete"};
        for(int i=0;i<4;++i){const int y=53+i*25;const auto c=i==2&&!frame.paste?m::ghost():i==frame.menu_cursor?m::spot():m::ink();
            ui::line(x-10,y+4,x-2,y+4,c);ui::text(x+6,y,labels[i],c);
            if(i==frame.menu_cursor)ui::line(x+6,y+12,x+ui::text_w(labels[i])+4,y+12,c);}
        ui::text(16,219,"b close",m::ghost());return;
    }
    m::paper(top?400:320);
    if(top)background(sigil_surface::TopPainter{},true);else background(sigil_surface::Painter{},false);
}
bool chains_picker_background(){if(!frame.valid||!frame.picker)return false;chains_draw_surface(false);return true;}
void chains_view_draw(){chains_draw_surface(false);}
} // namespace seance::platform::ui3ds
