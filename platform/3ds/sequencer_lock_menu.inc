// Included inside ui_sequencer.cpp's private namespace, after sequencer_scale_menu.inc.
// LOCK target chooser: the shared sigil word disclosure (word_menu + the existing
// sigil_surface cache), two lists over the frozen real score. It only selects the
// module/parameter the lock layer edits: no step, base or history is written, so
// every way of closing keeps what is underlined.
struct LockMenu {
    bool open=false,dirty=false,close_on_lift=false;
    bool press=false,armed=false;         // address press: opens on its lift, like the scale word
    int group=1;                          // focused list: 0 module, 1 parameter
    int shown_slot=-1;                    // module whose parameters the second list shows
    int press_row=-1;                     // parameter word under the stylus
    Engine* engine=nullptr;
    int chain=0,bank=0;
    uint64_t signature=0;
    uint32_t ink=0,spot=0,branch=0,paper=0;
    int modules=0,params=0;
    int8_t slot[UNITS_PER_CHAIN]={};      // module row -> chain slot
    char module[UNITS_PER_CHAIN][12]={};
    char param[PARAM_BANK_SIZE][10]={};
    word_menu::State motion[2]{};
    word_menu::Frame frame[2]{};
    int selected[2]={};
} g_lock_menu;
static_assert(sizeof(LockMenu)<=448,"bounded lock-target menu; the shared cache is its only pixel storage");
constexpr word_menu::Layout LOCK_MODULE_LAYOUT{22,36,34,16,22,120};
constexpr word_menu::Layout LOCK_PARAM_LAYOUT{142,156,34,14,142,306};
void lock_menu_cancel() {
    auto& s=g_lock_menu;
    s.open=s.dirty=s.close_on_lift=s.press=s.armed=false;s.shown_slot=-1;
    s.engine=nullptr;s.motion[0]={};s.motion[1]={};
}
bool lock_menu_context_locked() {
    const auto& s=g_lock_menu;
    return g_eng&&g_audio&&s.engine==g_eng&&g_view==View::Sequencer&&g_overlay==Overlay::None&&
        g_seq_mode==SeqMode::Lock&&!g_l_held&&!g_tools&&!g_r_pressed&&!g_b_pressed&&!g_cuts_open&&
        s.chain==g_seq_chain&&s.bank==g_seq_bank&&s.signature==chain_signature()&&
        s.ink==m::ink()&&s.spot==m::spot()&&s.branch==m::branch()&&s.paper==m::paper_color(0);
}
// Rows come from the frozen inspector DTO of this frame, never from live units.
void lock_menu_labels() {
    auto& s=g_lock_menu;const auto& d=g_inspector;
    s.modules=s.params=0;s.selected[0]=s.selected[1]=0;
    for(int slot=0;slot<UNITS_PER_CHAIN;++slot) {
        const auto& module=d.modules[slot];
        if(module.kind==UnitKind::Empty||module.count<=0)continue;
        if(slot==d.slot)s.selected[0]=s.modules;
        s.slot[s.modules]=int8_t(slot);
        std::snprintf(s.module[s.modules],sizeof s.module[0],"%02d %.6s",slot+1,m::module_name(module.kind));
        ++s.modules;
    }
    for(auto& row:s.param)row[0]=0;
    if(d.slot<0)return;
    const auto& module=d.modules[d.slot];
    // Another module: its list is disclosed anew instead of morphing old words.
    if(s.shown_slot!=d.slot) { s.shown_slot=d.slot;s.motion[1]={}; }
    s.params=module.count;
    s.selected[1]=d.param<0?0:d.param>=module.count?module.count-1:d.param;
    for(int p=0;p<module.count;++p)std::snprintf(s.param[p],sizeof s.param[0],"%.8s",module.params[p].name);
}
bool lock_address_hit(int x,int y) { return y>=194 && y<211 && x<258; }
void lock_menu_open() {
    finish_edit();
    auto& s=g_lock_menu;
    platform::AudioOut::Guard guard(*g_audio);
    normalize_lock_target();
    if(!valid_lock_target(g_lock_slot,g_lock_param))return;
    s.engine=g_eng;s.chain=g_seq_chain;s.bank=g_seq_bank;s.signature=chain_signature();
    s.ink=m::ink();s.spot=m::spot();s.branch=m::branch();s.paper=m::paper_color(0);
    s.open=s.dirty=true;s.close_on_lift=false;s.group=1;
}
// true: the touch belonged to the menu (including the one that closes it).
bool lock_menu_touch(int x,int y) {
    auto& s=g_lock_menu;
    if(!s.open)return false;
    platform::AudioOut::Guard guard(*g_audio);
    if(!lock_menu_context_locked()) { lock_menu_cancel();return true; }
    for(int group=0;group<2;++group) {
        const auto& l=group?LOCK_PARAM_LAYOUT:LOCK_MODULE_LAYOUT;
        const int count=group?s.params:s.modules;
        if(hit(x,y,l.left,8,l.right-l.left,18)) { s.group=group;return true; } // heading: focus only
        if(x<l.left||x>=l.right||y<l.y-4||y>=l.y-4+count*l.step)continue;
        const int row=(y-l.y+4)/l.step;
        s.group=group;
        if(group==0) {
            const int slot=s.slot[row];
            if(valid_lock_target(slot,0)) {
                g_lock_slot=slot;g_lock_param=g_param_memory[g_seq_chain][slot];
                if(!valid_lock_target(g_lock_slot,g_lock_param))g_lock_param=0;
                g_offset=false;g_edit_status[0]=0;
            }
        } else if(valid_lock_target(g_lock_slot,row)) {
            g_lock_param=row;g_param_memory[g_seq_chain][g_lock_slot]=row;
            g_offset=false;g_edit_status[0]=0;
            s.close_on_lift=true;s.press_row=row; // the parameter word is the last choice
        }
        return true;
    }
    lock_menu_cancel();return true;
}
void lock_menu_hold(int x,int y) {
    auto& s=g_lock_menu;
    if(!s.close_on_lift)return;
    const auto& l=LOCK_PARAM_LAYOUT;
    const bool inside=x>=l.left&&x<l.right&&y>=l.y-4&&(y-l.y+4)/l.step==s.press_row;
    if(!inside)s.close_on_lift=false; // dragged off the word: the lists stay open
}
// true: the menu owned this frame's buttons. L, R, START and SELECT close it and
// keep their own meaning in the same frame.
bool lock_menu_buttons(u32 down,u32 held) {
    auto& s=g_lock_menu;
    if(!s.open)return false;
    if(down&(KEY_L|KEY_R|KEY_START|KEY_SELECT)) { lock_menu_cancel();return false; }
    if(!g_eng||!g_audio) { lock_menu_cancel();return true; }
    { platform::AudioOut::Guard guard(*g_audio);
      if(!lock_menu_context_locked()) { lock_menu_cancel();g_touch_blocked_frame=true;return true; } }
    g_touch_blocked_frame=true;
    if(down&(KEY_A|KEY_B)) { lock_menu_cancel();return true; }
    if(down&(KEY_DLEFT|KEY_DRIGHT))s.group=(down&KEY_DLEFT)?0:1;
    if(bool(down&KEY_DUP)!=bool(down&KEY_DDOWN)) {
        const int delta=(down&KEY_DUP)?-1:1;
        if(s.group==0)browse_lock_module(delta);else browse_lock_param(delta);
    }
    if(!(held&KEY_TOUCH))pad_lock_navigation(); // circle pad as in the closed lock layer
    return true;
}
void lock_menu_capture() {
    auto& s=g_lock_menu;
    if(s.open) {
        if(!g_eng||!g_audio) lock_menu_cancel();
        else {
            bool valid;
            { platform::AudioOut::Guard guard(*g_audio);valid=lock_menu_context_locked(); }
            if(valid)lock_menu_labels();
            if(!valid||s.modules<=0||s.params<=0)lock_menu_cancel();
        }
    }
    if(s.open&&s.dirty) {
        // Entire real score stream; only the invoking address is left out, so the
        // sharp one can follow the selection without a stale blurred copy under it.
        g_freeze_without_address=true;freeze_score();g_freeze_without_address=false;
    }
    for(int group=0;group<2;++group)
        s.frame[group]=word_menu::capture(s.motion[group],osGetTime(),s.open?unsigned(group+1):0,s.selected[group],
            group?LOCK_PARAM_LAYOUT:LOCK_MODULE_LAYOUT,
            [&s,group](int i)->const char*{return group?s.param[i]:s.module[i];},s.dirty);
    s.dirty=false;
}
void lock_menu_draw() {
    const auto& s=g_lock_menu;
    if(!sigil_surface::bottom_cache.draw())m::paper(320);
    char label[64];lock_address(label,sizeof label);
    ui::text(8,198,label,m::spot());
    const sigil_surface::Painter p{};
    for(int group=0;group<2;++group) {
        const auto& l=group?LOCK_PARAM_LAYOUT:LOCK_MODULE_LAYOUT;
        p.text(l.x,14,group?"parameter":"module",group==s.group?m::spot():m::ghost());
        word_menu::draw(p,l,s.frame[group],group?s.params:s.modules,s.selected[group],
            [&s,group](int i)->const char*{return group?s.param[i]:s.module[i];},
            [](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());
    }
}
