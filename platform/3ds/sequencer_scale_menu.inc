// Included in the score controller, after its actual closed-state drawing.
struct ScaleMenu {
    bool open=false,dirty=false,drain=false,press=false,armed=false,keys_pass=false;
    int group=1,row=-1,press_group=-1;
    Engine* engine=nullptr;
    int chain=0,bank=0,step=0;
    uint64_t signature=0;
    uint32_t ink=0,spot=0,branch=0,paper=0;
    MusicalScale scale{};
    char anchor[32]={};
    word_menu::State motion[2]{};
    word_menu::Frame frame[2]{};
    int selected[2]={};
} g_scale_menu;
static_assert(sizeof(ScaleMenu)<=256,"bounded scale menu; shared cache is its only pixel storage");
constexpr u32 SCALE_KEYS=B_OWNED_KEYS|KEY_START;
void scale_cancel(bool tail=true) {
    auto& s=g_scale_menu;
    s.drain=s.drain||(tail&&(s.press||s.open));
    s.open=s.dirty=s.press=s.armed=s.keys_pass=false;s.row=s.press_group=-1;
    s.engine=nullptr;s.motion[0]={};s.motion[1]={};
}
bool scale_context_locked(bool allow_l=false) {
    const auto& s=g_scale_menu;
    return g_eng&&g_audio&&s.engine==g_eng&&g_view==View::Sequencer&&g_overlay==Overlay::None&&
        g_seq_mode==SeqMode::Note&&(!g_l_held||allow_l)&&!g_tools&&!g_r_pressed&&!g_b_pressed&&
        s.chain==g_seq_chain&&s.bank==g_seq_bank&&s.step==g_seq_col&&s.signature==chain_signature()&&
        musical_scale::equal(s.scale,g_eng->project_scale())&&
        s.ink==m::ink()&&s.spot==m::spot()&&s.branch==m::branch()&&s.paper==m::paper_color(0);
}
void scale_begin_press(int group,int row) {
    auto& s=g_scale_menu;s.press=s.armed=true;s.press_group=group;s.row=row;
    if(group>=0){s.group=group;if(row>=0)s.selected[group]=row;}
}
bool scale_anchor_hit(int x,int y) {
    char word[32];sequencer_scale::label(word,sizeof word,g_score.project_scale);
    return hit(x,y,sequencer_scale::kX-4,194,ui::text_w(word)+8,16);
}
int scale_row(int group,int x,int y) {
    const auto l=group?sequencer_scale::scale_layout:sequencer_scale::tonic_layout;
    const int count=group?int(ScaleMode::Count):12;
    if(x<l.left||x>=l.right||y<l.y-4||y>=l.y-4+count*l.step)return -1;
    return (y-l.y+4)/l.step;
}
bool scale_touch(int x,int y) {
    auto& s=g_scale_menu;
    if(s.drain)return true;
    if(s.open) {
        if(!g_eng||!g_audio){scale_cancel();return true;}
        {platform::AudioOut::Guard guard(*g_audio);if(!scale_context_locked()){scale_cancel();return true;}}
        // Both branches are live immediately; headings navigate without edits.
        for(int group=0;group<2;++group){
            const auto l=group?sequencer_scale::scale_layout:sequencer_scale::tonic_layout;
            if(hit(x,y,l.left,8,l.right-l.left,18)){scale_begin_press(group,-1);return true;}
            const int row=scale_row(group,x,y);
            if(row>=0){scale_begin_press(group,row);return true;}
        }
        scale_cancel();return true;
    }
    if(g_seq_mode!=SeqMode::Note||g_tools||g_l_held||!scale_anchor_hit(x,y))return false;
    finish_edit();
    {platform::AudioOut::Guard guard(*g_audio);
        s.engine=g_eng;s.chain=g_seq_chain;s.bank=g_seq_bank;s.step=g_seq_col;
        s.signature=chain_signature();s.scale=g_eng->project_scale();
        s.ink=m::ink();s.spot=m::spot();s.branch=m::branch();s.paper=m::paper_color(0);
        sequencer_scale::label(s.anchor,sizeof s.anchor,s.scale);
    }
    scale_begin_press(-1,-1);return true;
}
void scale_hold(int x,int y) {
    auto& s=g_scale_menu;if(!s.press)return;
    const bool inside=s.press_group<0?scale_anchor_hit(x,y):s.row<0?
        hit(x,y,s.press_group?142:22,8,s.press_group?164:86,18):scale_row(s.press_group,x,y)==s.row;
    if(!inside)s.armed=false; // dragging out is irreversible until a fresh down
}
bool scale_release(bool commit) {
    auto& s=g_scale_menu;
    if(s.drain){s.press=s.armed=false;return true;}
    if(!s.press)return s.open;
    const bool apply=commit&&s.armed;const int group=s.press_group,row=s.row;
    s.press=s.armed=false;
    if(!apply){
        if(!s.open)scale_cancel(false);
        else {s.selected[0]=s.scale.tonic;s.selected[1]=int(s.scale.mode);}
        return true;
    }
    platform::AudioOut::Guard guard(*g_audio);
    if(!scale_context_locked()){scale_cancel();return true;}
    if(group<0){
        s.open=s.dirty=true;s.group=1;
        s.selected[0]=s.scale.tonic;s.selected[1]=int(s.scale.mode);
    }else if(row>=0){
        auto next=s.scale;
        if(group)next.mode=ScaleMode(row);else next.tonic=uint8_t(row);
        if(g_eng->set_project_scale(next))mark_project_mutated();
        scale_cancel(false);g_touch_blocked_frame=true;
    }
    return true;
}
bool scale_buttons(u32 down,u32 held,u32 up) {
    auto& s=g_scale_menu;
    if(s.drain){
        g_touch_blocked_frame=true;
        if((down&KEY_TOUCH)&&!(held&(SCALE_KEYS&~KEY_TOUCH))){s.drain=s.keys_pass=false;g_touch_blocked_frame=false;return false;}
        if(!(held&SCALE_KEYS)&&!(up&SCALE_KEYS)){s.drain=s.keys_pass=false;return true;}
        return !s.keys_pass;
    }
    if(!s.open&&!s.press)return false;
    if(!g_eng||!g_audio){scale_cancel();g_touch_blocked_frame=true;return true;}
    {platform::AudioOut::Guard guard(*g_audio);if(!scale_context_locked((down&KEY_L)!=0)){scale_cancel();g_touch_blocked_frame=true;return true;}}
    if(down&(KEY_R|KEY_L|KEY_START)){
        scale_cancel();s.keys_pass=true; // fresh original chord/transport keeps its own authority
        return false;
    }
    g_touch_blocked_frame=true;
    if(down&KEY_B)scale_cancel();
    return true;
}
void scale_capture() {
    auto& s=g_scale_menu;
    if(s.open||s.press){
        if(!g_eng||!g_audio){scale_cancel();return;}
        {platform::AudioOut::Guard guard(*g_audio);if(!scale_context_locked()){scale_cancel();return;}}
    }
    if(s.open&&s.dirty){
        // Entire REAL sequencer stream: wave, cells, inverse focus, selection,
        // playhead, editor lancet and footer. No simplified copied background.
        auto& cache=sigil_surface::bottom_cache;cache.clear();
        sequencer_scale::drawing::pen={&cache};draw_score();sequencer_scale::drawing::pen={};cache.finish();
    }
    for(int group=0;group<2;++group){
        const auto l=group?sequencer_scale::scale_layout:sequencer_scale::tonic_layout;
        s.frame[group]=word_menu::capture(s.motion[group],osGetTime(),s.open?unsigned(group+1):0,s.selected[group],l,
            [group](int i){return group?musical_scale::name(ScaleMode(i)):musical_scale::tonic_name(i);},s.dirty);
    }
    s.dirty=false;
}
void scale_draw() {
    const auto& s=g_scale_menu;
    if(!sigil_surface::bottom_cache.draw())m::paper(320);
    ui::text(sequencer_scale::kX,sequencer_scale::kY,s.anchor,m::spot());
    const sigil_surface::Painter p{};
    for(int group=0;group<2;++group){
        const auto l=group?sequencer_scale::scale_layout:sequencer_scale::tonic_layout;
        p.text(l.x,14,group?"scale":"tonic",group==s.group?m::spot():m::ghost());
        word_menu::draw(p,l,s.frame[group],group?int(ScaleMode::Count):12,s.selected[group],
            [group](int i){return group?musical_scale::name(ScaleMode(i)):musical_scale::tonic_name(i);},
            [](int,int,unsigned,bool){},m::ink(),m::spot(),m::branch());
    }
}
