#include "ui_module_focus_internal.h"
#include "ui_state.h"
#include "ui_param_lens.h"
#include "ui_sequencer.h"
#include "ui_theme.h"
#include "editor_style.h"
#include "module_heading.h"
#include "rev_readout.h"
#include "rev_tail_visual.h"
#include "reverb_field.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <cstdio>
#include <algorithm>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace m=editor_style;
using R=ReverbUnit;
constexpr const char* NAMES[]={"size","decay","mix","damp","freeze","predelay","low-cut","duck"};
constexpr int MAIN_ORDER[]={R::P_SIZE,R::P_DECAY,R::P_DAMP,R::P_MIX};
constexpr int EXTRA_ORDER[]={R::P_PREDELAY,R::P_LOW_CUT,R::P_DUCK};
constexpr int RAIL=118,RAIL_END=303,SPAN=240;
struct Frame {
    ParamInspector detail{};
    reverb_field::Plot plot{};
    q15 base[R::P_COUNT]{},edit[R::P_COUNT]{},live[R::P_COUNT]{};
    q15 energy=0,freeze=0,size=0,damp=0,returns[4]{},duck_gain=32767;
    uint16_t mod_links=0;
    uint8_t source[R::P_COUNT]{},kinds[UNITS_PER_CHAIN]{};
    bool crossed[UNITS_PER_CHAIN]{};
    bool valid=false,bypass=false,mute=false,locked=false,full=false,held=false;
    bool modern=true,settling=false,fine=false,ready=false,lens=false,more=false;
    bool requested=false,effective=false;
    int8_t chain=-1,slot=-1,selected=R::P_DECAY;
} frame;
static_assert(sizeof(Frame)<=2944 && std::is_trivially_copyable<Frame>::value,
              "bounded REV scalar capture and field; no engine/PCM/clock in draw");
int selected=R::P_DECAY;
bool more=false,blocked_frame=false,fine_held=false;
uintptr_t bound_unit=0;
Engine* bound_engine=nullptr;
int bound_chain=-1,bound_slot=-1;
enum class Action:uint8_t { None,Freeze,Bypass,Page,ClearLock };
struct Gesture {
    Engine* engine=nullptr;
    uintptr_t unit=0;
    int chain=-1,slot=-1,step=-1,bank=-1,lock_chain=-1,param=-1,selection=-1;
    int x=0,y=0,last_x=0,divisor=1,row=-1;
    q15 value=0;
    View view=View::Chains;
    bool locked=false,bypass=false,modern=true,more=false;
    Action action=Action::None;
} gesture;
static_assert(sizeof(Gesture)<=96,"bounded REV gesture ownership");
int clamp(int v) {return std::max(0,std::min(v,32767));}
bool capture_context() {
    return g_eng && g_audio && g_overlay==Overlay::Editor &&
        (g_view==View::Chains || (g_view==View::Sequencer && g_seq_lock_armed)) &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool context() {return !param_lens_is_open() && capture_context();}
bool lock_context() {
    return g_seq_lock_armed && g_seq_lock_chain==g_edit_chain &&
        g_seq_lock_step>=0 && g_seq_lock_step<SEQ_STEPS;
}
int edit_bank() {
    const int bank=sequencer_edit_bank();
    return bank>=0 && bank<NUM_PATTERNS?bank:g_eng->active_pattern;
}
q15 edit_value(const R& u,int p) {
    if(lock_context()) {
        const auto& step=g_eng->sequence_pattern(edit_bank()).steps[g_edit_chain][g_seq_lock_step];
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot && step.locks[i].param==p)return step.locks[i].value;
    }
    return u.params[p].base;
}
void set_value(R& u,int p,q15 value) {
    if(p<0 || p>=u.num_params || edit_value(u,p)==value)return;
    if(g_seq_lock_armed) {
        if(!lock_context() || u.bypass)return;
        auto& step=g_eng->sequence_pattern(edit_bank()).steps[g_edit_chain][g_seq_lock_step];
        const int count=std::min(int(step.lock_count),SEQ_LOCKS);
        int found=-1;
        for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot && step.locks[i].param==p){found=i;break;}
        if(found>=0)step.locks[found].value=value;
        else {
            if(count>=SEQ_LOCKS){g_seq_lock_flash=90;return;}
            step.locks[count]={uint8_t(g_edit_slot),uint8_t(p),value};step.lock_count=uint8_t(count+1);
        }
    } else u.params[p].base=value;
    mark_project_mutated();
}
void clear_lock(const R& u,int p) {
    if(!lock_context() || u.bypass)return;
    auto& step=g_eng->sequence_pattern(edit_bank()).steps[g_edit_chain][g_seq_lock_step];
    const int count=std::min(int(step.lock_count),SEQ_LOCKS);
    for(int i=0;i<count;++i)if(step.locks[i].slot==g_edit_slot && step.locks[i].param==p) {
        for(int j=i+1;j<count;++j)step.locks[j-1]=step.locks[j];
        step.locks[count-1]={};step.lock_count=uint8_t(count-1);mark_project_mutated();return;
    }
}
bool held(const R& u) {
    return u.params[R::P_FREEZE].base>16384 || u.params[R::P_FREEZE].effective>16384 || u.visual_freeze()>0;
}
bool captured_param(int p) {return p==R::P_SIZE || p==R::P_DECAY || p==R::P_DAMP;}
void bind(const R& u) {
    if(bound_engine!=g_eng || bound_unit!=uintptr_t(&u) || bound_chain!=g_edit_chain || bound_slot!=g_edit_slot) {
        reverb_focus_release();selected=R::P_DECAY;more=false;
        bound_engine=g_eng;bound_unit=uintptr_t(&u);bound_chain=g_edit_chain;bound_slot=g_edit_slot;
    }
    if(!u.modern() && (more || selected>=R::P_PREDELAY)) {
        more=false;
        if(selected>=R::P_PREDELAY)selected=R::P_DECAY;
        reverb_focus_release();
    }
}
bool owns(const R& u) {
    return context() && gesture.engine==g_eng && gesture.unit==uintptr_t(&u) &&
        gesture.chain==g_edit_chain && gesture.slot==g_edit_slot && gesture.view==g_view &&
        gesture.locked==g_seq_lock_armed && gesture.bypass==u.bypass && gesture.modern==u.modern() &&
        gesture.more==more && gesture.selection==selected &&
        g_eng->chains[g_edit_chain].units[g_edit_slot]==&u &&
        (!gesture.locked || (lock_context() && gesture.step==g_seq_lock_step &&
            gesture.lock_chain==g_seq_lock_chain && gesture.bank==edit_bank()));
}
void start(R& u,int p,int tx,int ty,Action action=Action::None,int row=-1) {
    gesture={};gesture.engine=g_eng;gesture.unit=uintptr_t(&u);gesture.chain=g_edit_chain;gesture.slot=g_edit_slot;
    gesture.view=g_view;gesture.locked=g_seq_lock_armed;gesture.lock_chain=g_seq_lock_chain;
    gesture.step=g_seq_lock_step;gesture.bank=edit_bank();gesture.param=p;gesture.selection=selected;
    gesture.x=gesture.last_x=tx;gesture.y=ty;gesture.value=p>=0?edit_value(u,p):0;
    gesture.divisor=fine_held?8:1;gesture.bypass=u.bypass;gesture.modern=u.modern();gesture.more=more;
    gesture.action=action;gesture.row=row;g_drag=Drag::ReverbSize;
}
bool action_hit(int tx,int ty) {
    switch(gesture.action) {
        case Action::Freeze:return hit(tx,ty,16,194,100,24);
        case Action::Bypass:return hit(tx,ty,244,0,37,24);
        case Action::Page:return hit(tx,ty,232,194,80,24);
        case Action::ClearLock:return hit(tx,ty,12,43+37*gesture.row,72,16);
        default:return false;
    }
}
void right(int x,int y,const char* text,u32 color) {ui::text(x-ui::text_w(text),y,text,color);}
void value_text(int p,q15 value,char* out,size_t size,bool modern) {rev_readout::value(out,size,p,value,modern);}
u32 blend(u32 a,u32 b,unsigned amount) {
    u32 out=0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8)
        out|=((((a>>shift)&255)*(255-amount)+((b>>shift)&255)*amount+127)/255)<<shift;
    return out;
}
u32 quiet_ink(unsigned amount=76) {return blend(m::paper_color(109),m::ghost(),amount);}
const char* status(const Frame& f) {
    if(f.bypass)return "bypassed";
    if(f.mute)return "chain muted";
    if(!f.ready)return "await audio";
    if(f.settling)return "settling";
    const bool returns_active=f.energy || (f.modern &&
        (f.returns[0] || f.returns[1] || f.returns[2] || f.returns[3]));
    switch(rev_tail_visual::freeze_state(f.requested,f.effective,f.freeze)) {
        case rev_tail_visual::State::Requested:return "requested";
        case rev_tail_visual::State::Effective:return "pending";
        case rev_tail_visual::State::Entering:return "freezing";
        case rev_tail_visual::State::Held:return returns_active?"held":"held / empty";
        case rev_tail_visual::State::Leaving:return "releasing";
        default:return !f.live[R::P_MIX]?"dry mix":f.energy?"tail active":returns_active?"returns active":"quiet";
    }
}
void source_text(char* out,size_t size,int source) {
    const int s=mod_source_sigil(ModSource(source));
    if(s>=0)std::snprintf(out,size,"%c",'a'+s);else std::snprintf(out,size,"env");
}
void draw_control(const Frame& f,int p,int row) {
    const int y=47+row*37;
    const bool disabled=captured_param(p) && f.held;
    const bool quiet=f.bypass || f.mute || !f.ready;
    const u32 color=disabled||quiet?m::ghost():f.selected==p?m::spot():m::ink();
    char a[32],label[48];
    ui::text(16,y,p==R::P_DECAY&&!f.modern?"dec fb":NAMES[p],color);
    if(disabled)ui::text(208,y,"held",m::ghost());
    else if(f.source[p]) {
        source_text(a,sizeof a,f.source[p]);right(224,y,a,f.mod_links&(1u<<p)?m::spot():m::ghost());
        if(!(f.mod_links&(1u<<p)))ui::line(224-ui::text_w(a),y+4,223,y+4,m::ghost());
    }
    value_text(p,f.locked?f.edit[p]:f.live[p],a,sizeof a,f.modern);right(304,y,a,color);
    value_text(p,f.locked?f.live[p]:f.base[p],a,sizeof a,f.modern);
    std::snprintf(label,sizeof label,"%s %s",f.locked?"live":"base",a);ui::text(16,y+16,label,m::ghost());
    const int edit=RAIL+clamp(f.edit[p])*(RAIL_END-RAIL)/32767;
    const int live=RAIL+clamp(f.live[p])*(RAIL_END-RAIL)/32767;
    module_fader::horizontal(RAIL,RAIL_END,y+19,edit,live,
        {bool(f.mod_links&(1u<<p)),disabled,quiet});
    if(f.selected==p)ui::line(8,y,8,y+7,m::spot());
}
void field(const Frame& f) {
    const u32 faint=quiet_ink(48);
    for(int row=0;row<reverb_field::ROWS;++row) {
        const unsigned strength=f.plot.row_strength[row];
        const u32 ink=blend(faint,row%3==1?m::spot():m::ghost(),strength*145/255);
        const auto* points=f.plot.points[row];
        for(int col=1;col<reverb_field::COLS;++col)
            ui::line(points[col-1].x,points[col-1].y,points[col].x,points[col].y,ink);
    }
    ui::line(45,169,52,169,quiet_ink());ui::line(348,169,355,169,quiet_ink());
}
} // namespace

int reverb_focus_selected() {return selected;}
void reverb_focus_select(int p) {
    if(p<0 || p>=R::P_COUNT)return;
    if(p!=selected)reverb_focus_release();
    selected=p;
    if(p!=R::P_FREEZE)more=p>=R::P_PREDELAY;
}
void reverb_focus_release() {
    gesture={};
    if(g_drag==Drag::ReverbSize)g_drag=Drag::None;
}
void reverb_focus_input(u32 down,u32 held_keys,bool blocked) {
    fine_held=(held_keys&KEY_R)!=0;
    const u32 buttons=down&~u32(KEY_TOUCH|KEY_ZL);
    blocked_frame=blocked || (buttons&~u32(KEY_R)) || (buttons && gesture.action!=Action::None);
    if(blocked_frame || !context())reverb_focus_release();
}
void reverb_focus_value_text(char* out,size_t size,int param,q15 value,bool modern) {
    value_text(param,value,out,size,modern);
}
void reverb_focus_capture() {
    frame={};
    if(!capture_context()){reverb_focus_release();return;}
    {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* at=g_eng->chains[g_edit_chain].units[g_edit_slot];
        if(!at || at->kind!=UnitKind::Reverb){reverb_focus_release();return;}
        const auto& u=*static_cast<R*>(at);bind(u);
        if(gesture.unit && !owns(u))reverb_focus_release();
        frame.valid=true;frame.chain=g_edit_chain;frame.slot=g_edit_slot;frame.selected=selected;
        frame.bypass=u.bypass;frame.mute=g_eng->chains[g_edit_chain].mute;
        frame.locked=g_seq_lock_armed;frame.full=g_seq_lock_flash>0;frame.held=held(u);
        frame.modern=u.modern();frame.fine=fine_held;frame.settling=u.visual_transition();
        frame.ready=u.visual_ready();frame.lens=param_lens_is_open();frame.more=more&&u.modern();
        frame.mod_links=focus_mod_links(u);
        for(int p=0;p<R::P_COUNT;++p) {
            frame.base[p]=u.params[p].base;frame.edit[p]=edit_value(u,p);
            frame.live[p]=frame.ready?u.visual_effective(p):u.params[p].base;
            frame.source[p]=uint8_t(u.params[p].src);
        }
        frame.energy=u.visual_energy();frame.freeze=u.visual_freeze();
        frame.requested=u.params[R::P_FREEZE].base>16384;
        frame.effective=u.params[R::P_FREEZE].effective>16384;
        frame.size=u.visual_size();frame.damp=u.visual_damping();frame.duck_gain=u.visual_duck_gain();
        for(int lane=0;lane<4;++lane)frame.returns[lane]=u.visual_return(lane);
        g_eng->capture_param_inspector(g_edit_chain,g_edit_slot,selected,frame.detail);
        for(int i=0;i<UNITS_PER_CHAIN;++i) {
            const Unit* unit=g_eng->chains[g_edit_chain].units[i];
            frame.kinds[i]=uint8_t(unit?unit->kind:UnitKind::Empty);frame.crossed[i]=unit&&unit->bypass;
        }
    }
    reverb_field::build(frame.size,frame.damp,frame.energy,frame.returns,frame.modern,
                       frame.bypass||frame.mute||!frame.ready,frame.plot);
}
bool reverb_focus_touch_body(R& unit,int tx,int ty) {
    reverb_focus_release();
    if(!context() || blocked_frame)return true;
    platform::AudioOut::Guard guard(*g_audio);
    if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&unit)return true;
    bind(unit);
    if(g_seq_lock_armed && (!lock_context() || unit.bypass))return true;
    if(hit(tx,ty,244,0,37,24)) {
        if(!g_seq_lock_armed)start(unit,-1,tx,ty,Action::Bypass);
        return true;
    }
    if(hit(tx,ty,16,194,100,24)) {
        selected=R::P_FREEZE;start(unit,selected,tx,ty,Action::Freeze);return true;
    }
    if(hit(tx,ty,232,194,80,24)) {
        if(unit.modern())start(unit,-1,tx,ty,Action::Page);
        return true;
    }
    const bool extra_page=more && unit.modern();
    const int rows=extra_page?3:4;
    for(int row=0;row<rows;++row) {
        if(!hit(tx,ty,8,39+37*row,304,37))continue;
        const int p=extra_page?EXTRA_ORDER[row]:MAIN_ORDER[row];selected=p;
        if(captured_param(p) && held(unit))return true;
        const bool clear=lock_context() && hit(tx,ty,12,43+37*row,72,16);
        start(unit,p,tx,ty,clear?Action::ClearLock:Action::None,row);return true;
    }
    return true;
}
void reverb_focus_drag(R& unit,int tx,int ty) {
    if(!context() || blocked_frame){reverb_focus_release();return;}
    platform::AudioOut::Guard guard(*g_audio);
    if(!gesture.unit || g_drag!=Drag::ReverbSize || !owns(unit)){reverb_focus_release();return;}
    if(gesture.action!=Action::None) {
        if(!action_hit(tx,ty) || std::abs(tx-gesture.x)>8 || std::abs(ty-gesture.y)>8)reverb_focus_release();
        return;
    }
    if(captured_param(gesture.param) && held(unit)){reverb_focus_release();return;}
    tx=std::max(0,std::min(tx,319));
    const int divisor=fine_held?8:1;
    if(divisor!=gesture.divisor) {
        gesture.x=gesture.last_x;gesture.value=edit_value(unit,gesture.param);gesture.divisor=divisor;
    }
    if(tx==gesture.last_x)return;
    gesture.last_x=tx;
    set_value(unit,gesture.param,q15(clamp(int(gesture.value)+(tx-gesture.x)*32767/(SPAN*divisor))));
}
void reverb_focus_touch_release() {
    if(gesture.unit && context() && !blocked_frame) {
        platform::AudioOut::Guard guard(*g_audio);
        Unit* at=g_eng->chains[g_edit_chain].units[g_edit_slot];
        if(at && at->kind==UnitKind::Reverb && g_drag==Drag::ReverbSize) {
            auto& u=*static_cast<R*>(at);
            if(owns(u)) {
                if(gesture.action==Action::Freeze && edit_value(u,R::P_FREEZE)==gesture.value)
                    set_value(u,R::P_FREEZE,gesture.value>16384?0:32767);
                else if(gesture.action==Action::Bypass && !g_seq_lock_armed){u.bypass=!u.bypass;mark_project_mutated();}
                else if(gesture.action==Action::ClearLock)clear_lock(u,gesture.param);
                else if(gesture.action==Action::Page){more=!more;selected=more?R::P_PREDELAY:R::P_DECAY;}
            }
        }
    }
    reverb_focus_release();
}
void reverb_focus_draw(const R&) {
    const auto& f=frame;if(!f.valid)return;
    m::paper(320);char label[64];
    module_heading::header(UnitKind::Reverb,m::spot());
    std::snprintf(label,sizeof label,"ch%02d / %02d",f.chain+1,f.slot+1);ui::text(70,m::kHeaderTextY,label,m::ghost());
    ui::text(251,m::kHeaderTextY,"byp",f.bypass?m::spot():m::ghost());right(m::right_edge(320),m::kHeaderTextY,"x del",m::ghost());
    if(f.lens) {ui::text(16,32,NAMES[f.selected],m::spot());ui::text(m::kLeft,m::kTextY,"b / return",m::ghost());return;}
    ui::text(16,32,f.more?"wet settings":"parameter",m::ghost());
    right(304,32,f.locked?"lock draft":!f.ready?"base":f.bypass||f.mute?"last":"live",m::ghost());
    const int rows=f.more?3:4;
    for(int row=0;row<rows;++row)draw_control(f,f.more?EXTRA_ORDER[row]:MAIN_ORDER[row],row);
    if(f.more) {
        ui::text(16,160,"dry stays unchanged",m::ghost());
        std::snprintf(label,sizeof label,"wet gain %d%%",(clamp(f.duck_gain)*100+16383)/32767);
        const char* duck_state=f.freeze==32767 && f.effective && f.duck_gain==32767?"freeze: open":
            f.effective||f.requested?"freeze pending":f.freeze?"returning":"input duck";
        ui::text(16,175,label,m::ghost());right(304,175,duck_state,m::ghost());
    }
    const bool on=f.edit[R::P_FREEZE]>16384;
    ui::text(20,202,on?"freeze on":"freeze",on?m::spot():m::ink());
    if(on)ui::line(20,215,73,215,m::spot());
    ui::text(134,202,f.full?"lock full":f.locked?"step lock":on?"tap release":"tap capture",m::ghost());
    if(f.modern)right(304,202,f.more?"< main":"more >",m::ghost());
    ui::text(m::kLeft,m::kTextY,"b back",m::ghost());ui::text(121,m::kTextY,f.fine?"fine /8":"r fine",m::ghost());
    if(!f.locked)right(m::right_edge(320),m::kTextY,"l: mod >",m::ghost());
}
bool reverb_focus_draw_top() {
    const auto& f=frame;if(!f.valid)return false;
    m::paper(400);char label[96],a[32],b[32];
    module_heading::header(UnitKind::Reverb,m::spot());
    ui::text(64,m::kHeaderTextY,f.modern?"dense reverb":"legacy reverb",m::ghost());
    std::snprintf(label,sizeof label,"ch%02d / %02d",f.chain+1,f.slot+1);right(m::right_edge(400),m::kHeaderTextY,label,m::ghost());
    int x=14;
    for(int i=0;i<UNITS_PER_CHAIN;++i) {
        const auto kind=UnitKind(f.kinds[i]);if(kind==UnitKind::Empty)continue;
        if(x>14){ui::text(x,module_heading::kRouteY,"/",m::ghost());x+=12;}
        // Full distinct names (strain vs strn); six current names fit400px.
        const char* n=m::module_name(kind);const int w=ui::text_w(n);
        ui::text(x,module_heading::kRouteY,n,i==f.slot?m::spot():m::ghost());
        if(i==f.slot)ui::line(x,module_heading::kRouteUnderlineY,x+w-2,module_heading::kRouteUnderlineY,m::spot());
        if(f.crossed[i])ui::line(x,module_heading::kRouteCrossY,x+w-2,module_heading::kRouteCrossY,m::ghost());
        x+=w+8;
    }
    field(f);
    ui::text(16,178,"wet return",m::ghost());ui::line(90,181,236,181,quiet_ink());
    if(f.plot.wet_strength)ui::line(90,181,90+146*f.plot.wet_strength/255,181,m::spot());
    right(384,178,status(f),m::ghost());ui::line(16,196,384,196,quiet_ink());
    const int p=f.selected;ui::text(16,204,p==R::P_DECAY&&!f.modern?"dec fb":NAMES[p],m::spot());
    value_text(p,f.locked?f.edit[p]:f.live[p],a,sizeof a,f.modern);
    std::snprintf(label,sizeof label,"%s%s",f.locked?"draft ":"",a);right(220,204,label,m::ink());
    value_text(p,f.locked?f.live[p]:f.base[p],a,sizeof a,f.modern);
    std::snprintf(label,sizeof label,"%s %s",f.locked?"live":"base",a);right(384,204,label,m::ghost());
    const auto& d=f.detail;
    if(d.source!=ModSource::None) {
        source_text(label,sizeof label,uint8_t(d.source));value_text(p,d.range_min,a,sizeof a,f.modern);value_text(p,d.range_max,b,sizeof b,f.modern);
        char src[8];std::snprintf(src,sizeof src,"%.7s",label);
        std::snprintf(label,sizeof label,"%s %s..%s%s",src,a,b,!d.mod_enabled?" off":d.clip_low||d.clip_high?" !":"");
    } else if(f.ready && d.target!=f.live[p]) {
        value_text(p,d.target,a,sizeof a,f.modern);std::snprintf(label,sizeof label,"target %s",a);
    } else {
        const char* meaning=p==R::P_DECAY?(f.modern?"nominal tail":"feedback"):
            p==R::P_PREDELAY?"send target":p==R::P_LOW_CUT?"send highpass":p==R::P_DUCK?"wet duck":
            p==R::P_MIX?"dry / wet":p==R::P_FREEZE?"capture":"space control";
        std::snprintf(label,sizeof label,"%s / mod off",meaning);
    }
    ui::text(m::kLeft,m::kTextY,label,m::ghost());
    static const char* LAYERS[]={"base layer","step lock","audition","performance","temp"};
    right(m::right_edge(400),m::kTextY,f.locked?"lock draft":f.held?"space held":LAYERS[std::min(int(d.layer),4)],m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
