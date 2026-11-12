// Private controller state for the shared rand action. Included inside the
// parameter lens namespace after context()/unit(); no draw-time engine access.
bool random_target = false;
struct RandomOwner {
    Engine* engine = nullptr;
    uintptr_t unit = 0;
    int chain = -1, slot = -1;
    bool matches(const Unit* u) const {
        return u && engine == g_eng && unit == uintptr_t(u) &&
            chain == g_edit_chain && slot == g_edit_slot;
    }
};
struct RandomFrame {
    bool valid=false, available=false, undo=false, focused=false, pressed=false;
    int x=-1;
    ModSource source=ModSource::None;
} random_frame;
RandomOwner random_focus, random_press;
bool random_touch=false, random_armed=false;
int random_press_x=-1;
Engine* random_seen_engine=nullptr;
uint64_t random_seen_revision=0;

int random_anchor(UnitKind kind) {
    // Existing captions are never moved or resized. The four approved anchors
    // are exact. GRAN has no free 24px header cell in fine mode: use L > rand.
    switch(kind) {
        case UnitKind::Empty: case UnitKind::Count: case UnitKind::Grain: return -1;
        // Source/setup fill their headers; both keep the shared L > rand panel.
        case UnitKind::Rule: case UnitKind::Pent: case UnitKind::Rot: case UnitKind::Relic: case UnitKind::Scratch: case UnitKind::Paul: return -1;
        case UnitKind::Filter: case UnitKind::Reverb: case UnitKind::Chorus: return 212;
        case UnitKind::SamplePlayer: return 228; // full PLAY: after source arrow, before mode
        case UnitKind::Delay: return 150;
        case UnitKind::Osc: return 134;
        case UnitKind::Fm: return 274;
        case UnitKind::Ghost: return 172;
        case UnitKind::Stranular: return 142;
        case UnitKind::Comp: return 174;
        case UnitKind::Mouth: return 214;
        case UnitKind::Fract: return 146;
        case UnitKind::Glitch: return 252;
        case UnitKind::Inst: case UnitKind::Arp: return 250;
        case UnitKind::Bird: case UnitKind::Shift: return 218;
        case UnitKind::Recorder: return 206;
        case UnitKind::MicIn: return 142; // retains REC return at 184
        case UnitKind::Noise: case UnitKind::Strain: return 138;
        case UnitKind::Motor: return 210;
        case UnitKind::Vca: case UnitKind::Bitcrush: case UnitKind::Fold:
        case UnitKind::Tilt: case UnitKind::Ring: return 132;
    }
    return -1;
}
bool random_hit(int x,int y,int anchor) {
    // Tight native cell, not a hidden large button stealing neighbouring actions.
    return anchor>=0 && x>=anchor && x<anchor+24 && y>=8 && y<24;
}
RandomOwner random_owner(const Unit& u) { return {g_eng,uintptr_t(&u),g_edit_chain,g_edit_slot}; }
void random_clear_press() { random_touch=random_armed=false; random_press={}; random_press_x=-1; }
bool random_available(const Unit&) {
    return g_eng->module_random_available(g_edit_chain,g_edit_slot);
}
void random_capture() {
    random_frame={};
    if(!context() || recorder_focus_confirming()) {
        random_focus={};random_clear_press();random_target=false;return;
    }
    platform::AudioOut::Guard guard(*g_audio);
    const Unit* u=unit();
    if(!u || !u->num_params) {random_focus={};random_clear_press();return;}
    if(!random_focus.matches(u))random_focus={};
    if(random_touch&&!random_press.matches(u))random_clear_press();
    random_frame.valid=true;
    random_frame.x=random_anchor(u->kind);
    random_frame.source=u->random_source;
    random_frame.available=random_available(*u);
    random_frame.undo=g_eng->can_undo_module_randomize(g_edit_chain,g_edit_slot);
    random_frame.focused=random_focus.matches(u);
    random_frame.pressed=random_touch&&random_armed;
}
void random_acknowledge_change() {
    // Called under the same guard as the base edit. Prevent polling the same
    // manual action twice; automatic edits are acknowledged by the UI poll.
    random_seen_engine=g_eng;
    random_seen_revision=g_eng->module_random_revision();
    mark_project_mutated();
}
void random_draw_panel() {
    namespace m=menu_style;
    m::paper(320,0,0);
    m::paper_rect(0,0,320,44,170);
    m::paper_rect(0,44,8,161,170);
    m::paper_rect(312,44,8,161,170);
    m::paper_rect(0,205,320,35,170);
    m::paper_rect(8,44,304,161,234);
    ui::text(24,55,"rand / module",m::spot());
    ui::text(248,55,"params",m::ghost());
    ui::text(24,82,"off > on: one randomize",m::ink());
    ui::text(24,100,"held on: no repeats",m::ghost());
    ui::text(24,118,"off: rearm, keep values",m::ghost());
    ui::text(24,146,random_frame.available?"rand now":"recording / busy",
             random_frame.available?m::ink():m::ghost());
    ui::text(248,146,"undo",random_frame.undo?m::ink():m::ghost());
    static const char* names[]={"off","a","b","c","d","e","f","g","h"};
    for(int i=0;i<9;++i) {
        const int x=14+i*33;
        const auto src=i?sigil_mod_source(i-1):ModSource::None;
        const bool on=random_frame.source==src;
        ui::text(x,184,names[i],on?m::spot():m::ghost());
        if(on)ui::rect(x,199,ui::text_w(names[i])-1,1,m::spot());
    }
}
