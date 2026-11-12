#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "audio_out.h"
#include "ui_sample_library.h"
#include "ui_workspace.h"
#include "ui_editor.h"
#include "ui_param_lens.h"
#include "module_inspector.h"
#include "editor_style.h"
#include "module_heading.h"
#include "rec_ui.h"
#include "sample_ownership.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;namespace geo=rec_ui;
using C=RecUnit;using Bank=trackr::synth::SampleBank;using Result=trackr::synth::SampleEditResult;
using Action=RecorderFocusAction;using Command=RecorderCommand;
constexpr int POINTS=EngineUiSnapshot::REC_WAVEFORM_POINTS;
enum class Modal:uint8_t {None,Retake,Clear,Slot,Destination};
struct Token {
    Engine* engine=nullptr;platform::AudioOut* audio=nullptr;uintptr_t unit=0;
    int chain=-1,slot=-1,sample=-1;uint64_t session=0,state=0;uint32_t take=0,generation=0;
};
struct Gesture {
    Token owner{};Action action=Action::None;geo::Box box{};int x=0;
    View view=View::Chains;bool setup=false,drag=false;q15 value=0;
} gesture;
struct Choice {
    Modal kind=Modal::None;Token owner{};int target=-1,pressed=-1;bool touching=false;
    UnitKind destination=UnitKind::Empty;
} choice;
struct Visit {Token rec{};uintptr_t mic=0;int slot=-1;bool visible=false,pressed=false;} visit;
struct Frame {
    bool valid=false,lens=false,setup=false,active=false,paused=false,pending=false;
    bool unsaved=false,wave=false,bypass=false,mute=false,dirty=false,return_mic=false;
    uint16_t links=0;int chain=0,slot=0,sample=0,selected=C::P_LEN,mic=-1,target_chain=-1;
    q15 edit[3]={},live[3]={};uint32_t frames=0,wave_frames=0,target=0;int8_t wave_y[POINTS]={};uint8_t gain=1;
    uint8_t channels=1;
    Modal modal=Modal::None;Command command=Command::None;LaunchQuantize clock=LaunchQuantize::Immediate;
    char duration[16]={},timer[16]={},length[16]={},status[64]={},notice[64]={},destination[64]={},timing[48]={};
} frame;
static_assert(sizeof(Frame)<768&&std::is_trivially_copyable<Frame>::value,"REC draw is a bounded frozen DTO");
bool setup=false,quarantine=false,blocked_frame=false;int selected=C::P_LEN;
Engine* page_engine=nullptr;uintptr_t page_unit=0;int page_chain=-1,page_slot=-1;
const char* notice=nullptr;
bool context(){return g_eng&&g_audio&&g_overlay==Overlay::Editor&&
    g_view==View::Chains&&
    g_edit_chain>=0&&g_edit_chain<NUM_CHAINS&&g_edit_slot>=0&&g_edit_slot<UNITS_PER_CHAIN;}
C* current(){if(!context())return nullptr;Unit*u=g_eng->chains[g_edit_chain].units[g_edit_slot];return u&&u->kind==UnitKind::Recorder?static_cast<C*>(u):nullptr;}
bool pending(){return g_eng->recorder_record_pending(g_edit_chain,g_edit_slot)||g_eng->chain_record_pending(g_edit_chain);}
bool busy(const C&u){return u.writer_open()||pending();}
void sync_clip(){g_eng->set_external_sample_slot(sample_ownership::unit_slot(g_clip_unit));}
Token token(const C&u){return {g_eng,g_audio,uintptr_t(&u),g_edit_chain,g_edit_slot,u.sample_slot,u.session_token(),u.state_token(),u.take_generation(),Bank::instance().generation(u.sample_slot)};}
C* owner(const Token&t){C*u=current();if(!u||t.engine!=g_eng||t.audio!=g_audio||t.unit!=uintptr_t(u)||
    t.chain!=g_edit_chain||t.slot!=g_edit_slot||t.sample!=u->sample_slot||t.session!=u->session_token()||
    t.state!=u->state_token()||t.take!=u->take_generation()||t.generation!=Bank::instance().generation(u->sample_slot)||
    param_lens_is_open())return nullptr;
    return u;}
void page_context(const C&u){if(page_engine!=g_eng||page_unit!=uintptr_t(&u)||page_chain!=g_edit_chain||page_slot!=g_edit_slot){
    setup=false;notice=nullptr;page_engine=g_eng;page_unit=uintptr_t(&u);page_chain=g_edit_chain;page_slot=g_edit_slot;}}
q15 edit_value(const C&u,int p){return u.params[p].base;}
void change(C&u,int p,q15 v){if(v==edit_value(u,p))return;u.params[p].base=v;mark_project_mutated();}
void result(Result r){module_focus_show_sample_result(r);if(r==Result::Ok)notice=nullptr;}
void modal(Modal kind,C&u,int target=-1,UnitKind dest=UnitKind::Empty){choice={};choice.kind=kind;choice.owner=token(u);choice.target=target;choice.destination=dest;gesture={};g_drag=Drag::None;notice=nullptr;}
int input_mic(int chain,int rec_slot){
    // Only a single direct MIC lineage is advertised. Effects/another source make
    // this processed chain input, not a raw microphone shortcut.
    int mic=-1;
    for(int s=0;s<rec_slot;++s){const Unit*u=g_eng->chains[chain].units[s];if(!u)continue;
        if(u->kind==UnitKind::MicIn&&!u->bypass&&mic<0)mic=s;
        else return -1;}
    return mic;
}
void open_editor(int chain,int slot){editor_cancel_input();param_lens_cancel();g_edit_chain=g_sel_chain=chain;g_edit_slot=g_sel_slot=slot;
    g_overlay=Overlay::Editor;g_view=View::Chains;g_drag=Drag::None;g_last_px=-1;g_smooth_active=false;}
// UI-only half of valid_visit(): false means the visit is invalid without reading the engine.
bool visit_reachable(){
    return context()&&!param_lens_is_open()&&visit.rec.engine==g_eng&&visit.rec.audio==g_audio&&
       g_edit_chain==visit.rec.chain&&g_edit_slot==visit.slot&&visit.rec.slot>=0;
}
bool valid_visit(){
    if(!visit_reachable())return false;
    Unit*mic=g_eng->chains[g_edit_chain].units[g_edit_slot];Unit*rec=g_eng->chains[g_edit_chain].units[visit.rec.slot];
    return mic&&mic->kind==UnitKind::MicIn&&uintptr_t(mic)==visit.mic&&rec&&rec->kind==UnitKind::Recorder&&
        uintptr_t(rec)==visit.rec.unit&&static_cast<C*>(rec)->session_token()==visit.rec.session&&input_mic(g_edit_chain,visit.rec.slot)==visit.slot;
}
struct Destination {int chain=-1,at=-1;uintptr_t units[UNITS_PER_CHAIN]={};UnitKind kinds[UNITS_PER_CHAIN]={};};
bool empty_chain(int c){for(const Unit*u:g_eng->chains[c].units)if(u)return false;return true;}
int empty_destination(){for(int c=0;c<NUM_CHAINS;++c)if(c!=g_edit_chain&&empty_chain(c))return c;return -1;}
bool same_chain_safe(){if(g_edit_slot+1>=UNITS_PER_CHAIN)return false;bool space=false;
    for(int s=g_edit_slot+1;s<UNITS_PER_CHAIN;++s){const Unit*u=g_eng->chains[g_edit_chain].units[s];
        if(!u){space=true;continue;}if(u->sequencer_properties().source||u->kind==UnitKind::MicIn||u->kind==UnitKind::Recorder)return false;}
    return space;}
bool destination(int c,Destination&d){if(c==g_edit_chain){if(!same_chain_safe())return false;d.at=g_edit_slot+1;}
    else {if(c<0||c>=NUM_CHAINS||!empty_chain(c)||g_eng->chain_record_pending(c))return false;d.at=0;}
    d.chain=c;for(int s=0;s<UNITS_PER_CHAIN;++s){const Unit*u=g_eng->chains[c].units[s];d.units[s]=uintptr_t(u);d.kinds[s]=u?u->kind:UnitKind::Empty;}return true;}
bool destination_current(const Destination&d){Destination now;return destination(d.chain,now)&&now.at==d.at&&
    !std::memcmp(now.units,d.units,sizeof d.units)&&!std::memcmp(now.kinds,d.kinds,sizeof d.kinds);}
Result send_to(UnitKind kind,int target){
    Token source;Destination dest;bool save=false;
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();
        if(!u||param_lens_is_open()||busy(*u)||!u->recorded_frames()||g_eng->play_editor_busy())return Result::Busy;
        if(!destination(target,dest))return Result::NoFreeSlot;
        source=token(*u);save=u->take_needs_save();}
    Unit*created=make_unit(kind);if(!created)return Result::MemoryBudget;
    if(kind==UnitKind::SamplePlayer){auto*p=static_cast<PlayUnit*>(created);p->sample_slot=source.sample;p->set_pitch_mode(PlayUnit::PitchMode::Normal);}
    else{auto*g=static_cast<GrainUnit*>(created);g->sample_slot=source.sample;g->apply_new_card_start();}
    if(save){export_current_sample();if(!g_export_ok){delete created;notice="wav save failed / take kept";return Result::Busy;}}
    int inserted=-1;
    {platform::AudioOut::Guard guard(*g_audio);C*u=owner(source);if(u&&!busy(*u)&&!u->take_needs_save()&&!g_eng->play_editor_busy()&&destination_current(dest)){
        inserted=g_eng->insert_unit(dest.chain,dest.at,created);if(inserted>=0){created->control_tick();created->trigger();}}}
    if(inserted<0){delete created;notice=save?"wav saved / send not completed":"send cancelled / take kept";return Result::Busy;}
    workspace_shift_slots_right(dest.chain,dest.at,inserted);mark_project_mutated();choice={};visit={};open_editor(dest.chain,dest.at);
    return Result::Ok;
}
Result fresh(C&rec,bool restart,bool discard,int target=-1){
    RecWorkspacePlan plan;Token source;Result r;
    {platform::AudioOut::Guard guard(*g_audio);if(current()!=&rec||param_lens_is_open()||busy(rec))return Result::Busy;source=token(rec);sync_clip();
        r=g_eng->plan_recorder(rec,true,plan,target);plan.discard_unsaved=discard;}
    if(r==Result::Ok)r=Engine::stage_recorder(plan);
    if(r==Result::Ok){platform::AudioOut::Guard guard(*g_audio);if(!owner(source)||busy(rec))return Result::Busy;sync_clip();
        if(restart)r=g_eng->commit_recorder_restart(plan,g_edit_chain,g_edit_slot,g_eng->launch_quantize);
        else {r=g_eng->commit_recorder(plan);if(r==Result::Ok)rec.clear_take();}}
    if(r==Result::Ok){mark_project_mutated();setup=false;choice={};}return r;
}
int choice_at(int x,int y){
    if(choice.kind==Modal::Destination){if(hit(x,y,14,145,194,40)&&choice.target>=0)return 0;if(hit(x,y,218,145,94,40))return 2;}
    else if(choice.kind==Modal::Clear){if(hit(x,y,14,145,142,40))return 1;if(hit(x,y,166,145,142,40))return 2;}
    else {if(hit(x,y,8,145,100,50))return 0;if(hit(x,y,110,145,100,50))return 1;if(hit(x,y,212,145,100,50))return 2;}
    return -1;
}
void right(int x,int y,const char*s,u32 c){ui::text(x-ui::text_w(s),y,s,c);}
void center(int x,int y,const char*s,u32 c){ui::text(x-ui::text_w(s)/2,y,s,c);}
void seconds(char*out,size_t n,uint32_t f){const unsigned cs=unsigned((uint64_t(f)*100+ENGINE_SR/2)/ENGINE_SR);std::snprintf(out,n,"%u.%02us",cs/100,cs%100);}
const char* clock_name(LaunchQuantize q){return q==LaunchQuantize::Sixteenth?"1/16":q==LaunchQuantize::Beat?"beat":q==LaunchQuantize::Bar?"bar":"immediate";}
const char* queued_name(Command c){switch(c){case Command::Start:return "start queued";case Command::Pause:return "pause queued";case Command::Resume:return "resume queued";case Command::Stop:return "stop queued";default:return "clock queued";}}
}

RecorderFocusAction recorder_focus_action_at(int x,int y){
    if(setup){if(geo::back.has(x,y))return Action::Back;
        if(geo::row(0).has(x,y))return Action::Input;
        if(geo::row(1).has(x,y))return Action::Monitor;
        if(geo::row(3).has(x,y))return Action::Clock;
        if(geo::row(4).has(x,y))return Action::SaveWav;
        if(geo::row(5).has(x,y))return Action::Clear;
        return Action::None;}
    if(geo::setup.has(x,y))return Action::Setup;
    if(geo::stop.has(x,y))return Action::Stop;
    if(geo::dot.has(x,y))return Action::Record;
    // Before finalization there is neither a SEND label nor a hidden target.
    C*u=current();if(u&&!busy(*u)&&u->recorded_frames()){if(geo::gran.has(x,y))return Action::SendGrain;if(geo::play.has(x,y))return Action::SendPlay;}
    return Action::None;
}
Result recorder_focus_send_recording_to(UnitKind destination_kind){
    if(destination_kind!=UnitKind::SamplePlayer&&destination_kind!=UnitKind::Grain)return Result::InvalidSlot;
    int target=-1;
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u||busy(*u)||!u->recorded_frames())return Result::Busy;
        if(same_chain_safe())target=g_edit_chain;else{modal(Modal::Destination,*u,empty_destination(),destination_kind);return Result::Ok;}}
    return send_to(destination_kind,target);
}
void recorder_focus_command(C&rec,Action a){
    bool open=false,waiting=false,unsaved=false,paused=false;uint32_t frames=0;int sample=0;
    {platform::AudioOut::Guard guard(*g_audio);if(current()!=&rec||param_lens_is_open())return;
        open=rec.writer_open();waiting=pending();unsaved=rec.take_needs_save();paused=rec.is_paused();frames=rec.recorded_frames();sample=rec.sample_slot;}
    if(a==Action::Setup){setup=true;return;}if(a==Action::Back){setup=false;return;}
    if(a==Action::Record||a==Action::Stop){
        if(waiting&&a!=Action::Stop)return;
        if(a==Action::Record&&!open){
            if(unsaved){platform::AudioOut::Guard guard(*g_audio);modal(Modal::Retake,rec);return;}
            result(fresh(rec,true,false));return;}
        Command command=a==Action::Stop?Command::Stop:paused?Command::Resume:Command::Pause;Result r;
        {platform::AudioOut::Guard guard(*g_audio);r=g_eng->request_recorder_command(g_edit_chain,g_edit_slot,command,g_eng->launch_quantize);}
        result(r);if(r==Result::Ok)mark_project_mutated();return;}
    if(open||waiting)return;
    if(a==Action::Clear){platform::AudioOut::Guard guard(*g_audio);if(!busy(rec)&&frames)modal(Modal::Clear,rec);return;}
    if(a==Action::SaveWav){result(Result::Ok);if(frames){export_current_sample();notice=g_export_ok?"wav saved / project save separate":"wav save failed / take kept";}return;}
    if(a==Action::SendPlay||a==Action::SendGrain){const auto r=recorder_focus_send_recording_to(a==Action::SendPlay?UnitKind::SamplePlayer:UnitKind::Grain);module_focus_show_sample_result(r);return;}
    if(a==Action::Input){int at;{platform::AudioOut::Guard guard(*g_audio);if(busy(rec))return;at=input_mic(g_edit_chain,g_edit_slot);if(at<0)return;
            visit={};visit.rec=token(rec);visit.slot=at;visit.mic=uintptr_t(g_eng->chains[g_edit_chain].units[at]);}
        open_editor(g_edit_chain,at);return;}
    if(a==Action::Monitor){platform::AudioOut::Guard guard(*g_audio);if(busy(rec))return;selected=C::P_MON;change(rec,selected,edit_value(rec,selected)>16383?0:32767);return;}
    if(a==Action::Clock){platform::AudioOut::Guard guard(*g_audio);if(busy(rec))return;g_eng->launch_quantize=LaunchQuantize((int(g_eng->launch_quantize)+1)%4);mark_project_mutated();return;}
    if(a==Action::PreviousSlot||a==Action::NextSlot){const int target=(sample+trackr::synth::SAMPLE_BANK_SIZE+(a==Action::PreviousSlot?-1:1))%trackr::synth::SAMPLE_BANK_SIZE;
        if(unsaved){platform::AudioOut::Guard guard(*g_audio);modal(Modal::Slot,rec,target);}
        else result(fresh(rec,false,false,target));}
}
int recorder_focus_selected(){return selected;}
void recorder_focus_select(int p){if(p!=C::P_LEN&&p!=C::P_MON)return;if(selected!=p)recorder_focus_release();selected=p;}
void recorder_focus_release(){gesture={};visit.pressed=false;if(g_drag==Drag::RecLength)g_drag=Drag::None;}
void recorder_focus_cancel_lifecycle(){recorder_focus_release();choice={};quarantine=false;blocked_frame=false;}
bool recorder_focus_back(){if(setup&&current()){setup=false;recorder_focus_release();return true;}return false;}
bool recorder_focus_confirming(){return choice.kind!=Modal::None;}
bool recorder_focus_input(u32&down,u32 held,bool blocked){
    blocked_frame=blocked;if(!(held&KEY_TOUCH))quarantine=false;
    // No gesture owner and no reachable visit: same result as the guarded branch, without its entry.
    if(g_audio&&!gesture.owner.unit&&!visit_reachable())visit={};
    else if(g_audio){platform::AudioOut::Guard guard(*g_audio);visit.visible=valid_visit();if(!visit.visible)visit={};
        if(gesture.owner.unit&&(!owner(gesture.owner)||gesture.view!=g_view||blocked||(down&~(KEY_TOUCH|KEY_ZL)))){
            recorder_focus_release();quarantine=(held&KEY_TOUCH)!=0;}}
    if(!recorder_focus_confirming())return quarantine;
    bool valid=false;if(g_audio){platform::AudioOut::Guard guard(*g_audio);C*u=owner(choice.owner);valid=u&&!busy(*u);}
    if(!valid||blocked||(down&~(KEY_TOUCH|KEY_ZL))){choice={};recorder_focus_release();quarantine=(held&KEY_TOUCH)!=0;down&=KEY_SELECT|KEY_START|KEY_Y|KEY_ZL;return true;}
    return quarantine;
}
void recorder_focus_confirm_touch(bool down,bool held,int x,int y){
    if(!recorder_focus_confirming()||quarantine||blocked_frame)return;
    C*u;bool unsaved=false;
    {platform::AudioOut::Guard guard(*g_audio);u=owner(choice.owner);if(!u||busy(*u)){choice={};quarantine=held;return;}unsaved=u->take_needs_save();}
    if(held){int at=choice_at(x,y);if(down){choice.pressed=at;choice.touching=true;}else if(at!=choice.pressed)choice.pressed=-1;return;}
    if(!choice.touching)return;
    const int selected_choice=choice.pressed;choice.pressed=-1;choice.touching=false;if(selected_choice<0)return;
    if(selected_choice==2){choice={};return;}
    if(choice.kind==Modal::Destination){const auto r=send_to(choice.destination,choice.target);module_focus_show_sample_result(r);return;}
    bool saved=false;if(selected_choice==0&&unsaved){export_current_sample();if(!g_export_ok){notice="wav save failed / take kept";return;}saved=true;}
    {platform::AudioOut::Guard guard(*g_audio);u=owner(choice.owner);if(!u||busy(*u)){choice={};notice=saved?"wav saved / take not changed":"take changed / cancelled";return;}}
    const auto r=fresh(*u,choice.kind==Modal::Retake,selected_choice==1,choice.kind==Modal::Slot?choice.target:-1);
    result(r);if(r!=Result::Ok&&saved)notice="wav saved / new take not started";
}
bool recorder_focus_touch_body(C&u,int x,int y){
    if(!context()||param_lens_is_open()||blocked_frame)return false;
    if(recorder_focus_confirming()){recorder_focus_confirm_touch(true,true,x,y);return true;}
    recorder_focus_release();platform::AudioOut::Guard guard(*g_audio);if(current()!=&u)return false;page_context(u);
    const bool params=setup;
    const geo::Box len=geo::row(2);
    const geo::Box mon=geo::row(1);
    if(params&&(len.has(x,y)||mon.has(x,y))){const bool length=len.has(x,y);selected=length?C::P_LEN:C::P_MON;
        if(busy(u))return true;
        gesture.owner=token(u);gesture.x=x;gesture.value=edit_value(u,selected);gesture.view=g_view;
        gesture.setup=setup;g_drag=Drag::RecLength;
        gesture.drag=length;gesture.action=length?Action::None:Action::Monitor;gesture.box=mon;return true;}
    const Action a=recorder_focus_action_at(x,y);if(a==Action::None)return true;
    if(a!=Action::Setup&&a!=Action::Back){if(a==Action::Stop){if(!busy(u))return true;}
        else if(a==Action::Record){if(pending())return true;}else if(busy(u))return true;}
    gesture.owner=token(u);gesture.action=a;gesture.setup=setup;gesture.view=g_view;
    gesture.box=setup?(a==Action::Back?geo::back:a==Action::Input?geo::row(0):a==Action::Clock?geo::row(3):a==Action::SaveWav?geo::row(4):geo::row(5)):
        a==Action::Setup?geo::setup:a==Action::Stop?geo::stop:a==Action::Record?geo::dot:a==Action::SendGrain?geo::gran:geo::play;
    g_drag=Drag::RecLength;return true;
}
void recorder_focus_drag(C&u,int x,int y){
    if(!gesture.owner.unit)return;
    platform::AudioOut::Guard guard(*g_audio);
    if(!owner(gesture.owner)||current()!=&u||g_drag!=Drag::RecLength||gesture.setup!=setup||gesture.view!=g_view){recorder_focus_release();return;}
    if(!gesture.drag){if(!gesture.box.has(x,y))recorder_focus_release();return;}
    if(busy(u)){recorder_focus_release();return;}
    change(u,C::P_LEN,q15(std::max(0,std::min(32767,int(gesture.value)+(x-gesture.x)*32767/280))));
}
void recorder_focus_touch_release(){
    if(visit.pressed){bool valid;{platform::AudioOut::Guard guard(*g_audio);valid=valid_visit();}const int c=visit.rec.chain,s=visit.rec.slot;visit={};if(valid)open_editor(c,s);return;}
    Action a=Action::None;C*u=nullptr;
    if(g_audio){platform::AudioOut::Guard guard(*g_audio);u=owner(gesture.owner);
        if(u&&!blocked_frame&&g_drag==Drag::RecLength&&gesture.setup==setup&&gesture.view==g_view)a=gesture.action;}
    recorder_focus_release();if(u&&a!=Action::None)recorder_focus_command(*u,a);
}
bool recorder_focus_return_touch(int x,int y,bool down){if(!visit.visible)return false;
    const bool at=hit(x,y,174,0,64,26);if(down&&at){visit.pressed=true;g_drag=Drag::RecLength;return true;}
    if(!down&&visit.pressed){if(!at)visit.pressed=false;return true;}return false;}
void recorder_focus_draw_return(){if(visit.visible)ui::text(184,m::kHeaderTextY,"rec >",m::spot());}
const char* recorder_focus_status(){return frame.valid?frame.status:"";}
void recorder_focus_capture(){
    frame={};if(!context()){recorder_focus_release();choice={};page_unit=0;return;}
    q15 wave[POINTS]={};
    {platform::AudioOut::Guard guard(*g_audio);C*u=current();if(!u){if(gesture.owner.unit)recorder_focus_release();choice={};page_unit=0;return;}page_context(*u);
        if(choice.kind!=Modal::None&&!owner(choice.owner))choice={};
        frame.valid=true;frame.lens=param_lens_is_open();frame.setup=setup;
        frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.sample=u->sample_slot;frame.mic=input_mic(g_edit_chain,g_edit_slot);frame.selected=selected;
        frame.bypass=u->bypass;frame.mute=g_eng->chains[g_edit_chain].mute;frame.links=focus_mod_links(*u);frame.dirty=project_is_dirty();
        for(int p=0;p<3;++p){frame.edit[p]=edit_value(*u,p);frame.live[p]=u->params[p].effective;}
        frame.active=u->is_recording();frame.paused=u->is_paused();frame.pending=pending();frame.command=g_eng->recorder_pending_command(g_edit_chain,g_edit_slot);
        if(frame.command==Command::None&&frame.pending)frame.command=g_eng->chain_record_pending_start(g_edit_chain)?Command::Start:Command::Stop;
        frame.unsaved=u->take_needs_save();frame.frames=u->recorded_frames();frame.target=u->writer_open()?u->active_target_frames():u->target_frames();frame.clock=g_eng->launch_quantize;
        frame.channels=frame.frames?Bank::instance().slot(u->sample_slot).channels:g_eng->chain_audio_channels(g_edit_chain,g_edit_slot);
        frame.modal=choice.kind;frame.target_chain=choice.target;
        if(choice.kind==Modal::Destination&&choice.target>=0){const auto&ch=g_eng->chains[choice.target];
            std::snprintf(frame.destination,sizeof frame.destination,"ch%02d / %s / level %u%%",choice.target+1,ch.mute?"muted":"unmuted",unsigned(std::max(0,int(ch.volume)))*100/32767);
            std::snprintf(frame.timing,sizeof frame.timing,"clock %s / track %u / seq %s",clock_name(g_eng->launch_quantize),unsigned(g_eng->pattern.length[choice.target]),g_eng->sequencer_enabled?"on":"off");}
        // Published trace paired to current take identity, never audio rendering in draw.
        const auto&s=g_ui_snapshot;
        if(s.rec_waveform_chain==g_edit_chain&&s.rec_waveform_slot==g_edit_slot&&s.rec_slot[g_edit_chain][g_edit_slot]==u->sample_slot&&
           (s.rec_waveform_valid&1u)&&s.rec_waveform_session==u->session_token()&&s.rec_frames[g_edit_chain][g_edit_slot]<=u->recorded_frames()){
            frame.wave_frames=s.rec_frames[g_edit_chain][g_edit_slot];frame.wave=frame.wave_frames>1;std::copy(std::begin(s.rec_waveform),std::end(s.rec_waveform),wave);}
    }
    int peak=0;for(q15 v:wave)peak=std::max(peak,std::abs(int(v)));frame.gain=peak>0&&peak<8192?std::min(8,8192/peak):1;
    for(int i=0;i<POINTS;++i)frame.wave_y[i]=frame.wave?std::max(-32767,std::min(32767,int(wave[i])*frame.gain))*21/32767:0;
    seconds(frame.duration,sizeof frame.duration,frame.frames);seconds(frame.length,sizeof frame.length,frame.target);
    const unsigned cs=unsigned(uint64_t(frame.frames)*100/ENGINE_SR);std::snprintf(frame.timer,sizeof frame.timer,"%02u:%02u.%02u",cs/6000,(cs/100)%60,cs%100);
    std::snprintf(frame.status,sizeof frame.status,"%s",frame.pending?queued_name(frame.command):frame.paused?"paused":frame.active?"recording":frame.frames?"take ready":"ready");
    const char*msg=notice?notice:g_export_flash>0&&!g_export_is_load?(g_export_ok?"wav saved":"wav save failed"):module_focus_sample_result_notice();
    if(msg)std::snprintf(frame.notice,sizeof frame.notice,"%.63s",msg);
}
bool recorder_focus_draw_top(){
    if(!frame.valid||frame.lens)return false;
    m::paper(400);char text[64];
    module_heading::header(UnitKind::Recorder,m::spot());
    std::snprintf(text,sizeof text,"%s / ch%02d / %02d",frame.mic>=0?"mic":frame.channels==2?"stereo chain":!frame.channels?"unsupported":"chain input",frame.chain+1,frame.slot+1);right(m::right_edge(400),m::kHeaderTextY,text,m::ghost());
    center(200,62,frame.status,frame.active||frame.paused||frame.pending?m::spot():m::ghost());geo::timer(200,119,frame.timer,m::ink());center(200,139,"recorded time",m::ghost());
    if(frame.wave)for(int i=1;i<POINTS;++i)ui::line(20+(i-1)*359/(POINTS-1),181-frame.wave_y[i-1],20+i*359/(POINTS-1),181-frame.wave_y[i],m::ink());
    else ui::line(20,181,379,181,m::branch());
    std::snprintf(text,sizeof text,"s%02d / %s",frame.sample+1,frame.frames?(frame.unsaved?"unsaved take":"wav saved"):"empty");ui::text(20,208,text,m::ghost());
    std::snprintf(text,sizeof text,"limit %s / x%u",frame.length,unsigned(frame.gain));right(380,208,text,m::ghost());
    const char*hint=frame.notice[0]?frame.notice:frame.paused?"resume appends / pause adds no silence":frame.active?"stop finalizes the take":frame.pending?"waiting for clock":frame.frames?
        (frame.dirty?"send saves wav / project unsaved":"send saves wav / project saved"):"record / pause / stop";
    ui::text(m::kLeft,m::kTextY,hint,m::ghost());return true;
}
void recorder_focus_draw(const C&,const char*){
    m::paper(320);if(!frame.valid)return;const bool busy=frame.active||frame.paused||frame.pending;
    module_heading::header(UnitKind::Recorder,m::spot());
    if(frame.modal!=Modal::None){
        if(frame.modal==Modal::Destination){ui::text(14,64,"keep the existing chain intact",m::ink());ui::text(14,84,"send to a completely empty chain",m::ghost());
            ui::text(14,110,frame.target_chain>=0?frame.destination:"no empty chain / take kept",m::spot());
            ui::text(14,126,frame.target_chain>=0?frame.timing:"no routing changes",m::ghost());
            center(111,161,frame.target_chain>=0?"send here":"unavailable",frame.target_chain>=0?m::spot():m::ghost());center(265,161,"cancel",m::ink());}
        else {ui::text(14,62,frame.modal==Modal::Clear?"clear this take?":frame.unsaved?"unsaved take":"saved take",m::spot());
            ui::text(14,86,frame.modal==Modal::Clear?"saved wavs and aliases are kept":"keep a wav before the new take?",m::ink());
            if(frame.modal==Modal::Clear){center(85,163,"clear",m::spot());center(237,163,"cancel",m::ink());}
            else{center(58,156,"save then",m::spot());center(58,176,frame.modal==Modal::Slot?"switch":"record",m::spot());center(160,156,"discard then",m::ink());
                center(160,176,frame.modal==Modal::Slot?"switch":"record",m::ink());center(262,166,"cancel",m::ink());}}
        if(frame.notice[0])ui::text(14,204,frame.notice,m::spot());
        ui::text(m::kLeft,m::kTextY,frame.modal==Modal::Destination?"b cancel / fx not copied":"b cancel / original files kept",m::ghost());return;}
    if(frame.setup){geo::word(64,34,"setup",m::spot());
        const char*names[]={"input","monitor","max length","clock","save wav","clear take"};
        for(int i=0;i<6;++i){const int y=55+i*25;ui::text(14,y,names[i],busy?m::ghost():m::ink());
            const char*v=i==0?(frame.mic>=0?"mic >":"chain input"):i==1?(frame.edit[C::P_MON]>16383?"on":"off"):i==2?frame.length:i==3?clock_name(frame.clock):i==4?(busy?"stop first":frame.frames?(frame.unsaved?"unsaved take":"saved / export copy"):"empty"):"confirm >";
            right(304,y,v,m::ghost());}
        center(160,m::kTextY,"back",m::ink());return;}
    ui::text(54,m::kHeaderTextY,frame.mic>=0?"mic":frame.channels==2?"stereo chain":!frame.channels?"unsupported":"chain input",m::ghost());center(269,m::kHeaderTextY,"setup",m::ink());
    const bool stop_enabled=(frame.active||frame.paused||frame.pending)&&(!frame.pending||frame.command==Command::Start);
    const u32 sc=stop_enabled?m::ink():m::ghost(),dc=frame.pending?m::ghost():m::spot();
    ui::rect(70,69,27,27,sc);for(int dy=-15;dy<=15;++dy){int half=0;while((half+1)*(half+1)+dy*dy<=225)++half;ui::rect(237-half,82+dy,2*half+1,1,dc);}
    geo::word(83,134,"stop",sc,true);geo::word(237,134,frame.paused?"resume":frame.active?"pause":frame.frames?"new take":"record",dc,true);
    if(!busy&&frame.frames){center(160,168,"send to",m::ghost());geo::word(83,206,"gran",m::spot(),true);geo::word(237,206,"play",m::spot(),true);}
    else center(160,188,frame.pending?"waiting for clock":frame.active||frame.paused?"stop first, then send":"tap the dot to record",m::ghost());
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
}
} // namespace seance::platform::ui3ds
