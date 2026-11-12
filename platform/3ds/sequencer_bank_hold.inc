// Included inside ui_sequencer.cpp's private namespace, after draw_wave().
// Held B. One panel: the bank candidate (L/R or its number), four staged track
// lengths and directions (up/down track, left/right length, A direction) and
// whole-bank copy / paste / clear. Lengths and directions stay staged until B is
// released; navigation discards them. Paste and clear are explicit commands:
// they write the bank at once, ask again when the target holds anything, and
// never replace the bank that is sounding.
namespace bank_panel {
constexpr int DIGIT_X=34, DIGIT_PITCH=36, DIGIT_BASE=84, DIGIT_TOP=44, DIGIT_BOTTOM=104;
constexpr int RAIL_X0=76, RAIL_X1=202, ROW_Y=126, ROW_PITCH=22, FOOT_Y=228;
constexpr int COPY_X=16, PASTE_X=64, CLEAR_X=118, RIGHT=304;
constexpr int row_y(int track) { return ROW_Y+track*ROW_PITCH; }
constexpr int rail_x(int length) { return RAIL_X0+(length-1)*2; }
static_assert(rail_x(SEQ_STEPS)==RAIL_X1, "two pixels per step on the shared length scale");
}
void bank_status(const char* text) { std::snprintf(g_b_status,sizeof g_b_status,"%s",text); }
// Caller holds Guard: staged values of one bank follow the engine again.
void bank_restage_locked(int bank) {
    const auto& p=g_eng->sequence_pattern(bank);
    for(int c=0;c<NUM_CHAINS;++c) {
        g_b_length[bank][c]=p.length[c];g_b_direction[bank][c]=(uint8_t)p.direction[c];
        g_b_length_edits[c]&=uint8_t(~(1u<<bank));g_b_direction_edits[c]&=uint8_t(~(1u<<bank));
    }
}
bool bank_empty_locked(int bank) {
    const auto& p=g_eng->sequence_pattern(bank);
    for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<SEQ_STEPS;++s)
        if((p.steps[c][s].flags&SeqStep::Trig)||p.steps[c][s].lock_count)return false;
    return true;
}
// Engine contract: replacing the active bank restarts its clock and random
// sequence. Refuse that while it is audible instead of jumping the music.
bool bank_sounding_locked(int bank) {
    if(!g_eng->transport_playing||!g_eng->sequencer_enabled)return false;
    const auto blend=g_eng->pattern_transition(); // a rhythm blend sounds both of its banks
    return bank==g_eng->active_pattern||(blend.active&&(bank==blend.source||bank==blend.destination));
}
void bank_written() {
    // Row history and group belonged to the previous content of that bank.
    if(g_b_bank==g_edit_bank) { g_history.clear();g_selection.clear();g_edit_bank=-1; }
    g_b_dirty=true;mark_project_mutated();
}
void bank_set_length(int track,int length) {
    length=length<1?1:length>SEQ_STEPS?SEQ_STEPS:length;
    if(length==g_b_length[g_b_bank][track])return;
    g_b_length[g_b_bank][track]=(uint8_t)length;g_b_length_edits[track]|=uint8_t(1u<<g_b_bank);
}
void bank_cycle_direction(int track) {
    g_b_direction[g_b_bank][track]=uint8_t((g_b_direction[g_b_bank][track]+1)%(int)SequenceDirection::Count);
    g_b_direction_edits[track]|=uint8_t(1u<<g_b_bank);
}
void bank_copy() {
    char text[48];g_b_copy=g_b_bank;g_b_confirm=0;
    std::snprintf(text,sizeof text,"copied bank %d",g_b_bank+1);bank_status(text);
}
// The source is the copied bank as it is now, including its staged lengths.
void bank_paste() {
    char text[48];
    if(g_b_copy<0) { g_b_confirm=0;bank_status("copy a bank first");return; }
    if(g_b_copy==g_b_bank) { g_b_confirm=0;bank_status("same bank");return; }
    platform::AudioOut::Guard guard(*g_audio);
    if(bank_sounding_locked(g_b_bank)) { g_b_confirm=0;bank_status("bank plays / stop first");return; }
    if(g_b_confirm!=1 && !bank_empty_locked(g_b_bank)) {
        g_b_confirm=1;std::snprintf(text,sizeof text,"y again / replace bank %d",g_b_bank+1);bank_status(text);return;
    }
    g_b_confirm=0;
    if(!g_eng->copy_pattern_bank(g_b_copy,g_b_bank))return;
    const uint8_t from=uint8_t(1u<<g_b_copy),to=uint8_t(1u<<g_b_bank);
    bank_restage_locked(g_b_bank);
    for(int c=0;c<NUM_CHAINS;++c) {
        if(g_b_length_edits[c]&from) { g_b_length[g_b_bank][c]=g_b_length[g_b_copy][c];g_b_length_edits[c]|=to; }
        if(g_b_direction_edits[c]&from) { g_b_direction[g_b_bank][c]=g_b_direction[g_b_copy][c];g_b_direction_edits[c]|=to; }
    }
    bank_written();
    std::snprintf(text,sizeof text,"bank %d = copy of %d",g_b_bank+1,g_b_copy+1);bank_status(text);
}
void bank_clear() {
    char text[48];
    platform::AudioOut::Guard guard(*g_audio);
    if(bank_sounding_locked(g_b_bank)) { g_b_confirm=0;bank_status("bank plays / stop first");return; }
    if(g_b_confirm!=2) {
        g_b_confirm=2;std::snprintf(text,sizeof text,"tap clear again / bank %d",g_b_bank+1);bank_status(text);return;
    }
    g_b_confirm=0;g_eng->clear_pattern_bank(g_b_bank);
    bank_restage_locked(g_b_bank);bank_written();
    std::snprintf(text,sizeof text,"bank %d cleared",g_b_bank+1);bank_status(text);
}
void begin_bank_hold() {
    sequencer_view_touch_release(false);
    platform::AudioOut::Guard guard(*g_audio);
    capture_score_locked();
    g_eng->cancel_sequence_audition();
    g_b_pressed=true;g_b_open=false;g_b_start=g_ui_frame;g_b_repeat_key=0;
    g_b_bank=g_seq_bank;g_b_track=g_seq_chain;g_b_play=g_score.active_pattern;
    g_b_confirm=0;g_b_rail=-1;g_b_status[0]=0;g_b_dirty=true;
    for(int c=0;c<NUM_CHAINS;++c)g_b_length_edits[c]=g_b_direction_edits[c]=0;
    for(int bank=0;bank<NUM_PATTERNS;++bank)bank_restage_locked(bank);
}
void finish_bank_hold(u32 held) {
    if(g_b_open) {
        platform::AudioOut::Guard guard(*g_audio);
        bool changed=false;
        for(int bank=0;bank<NUM_PATTERNS;++bank)for(int c=0;c<NUM_CHAINS;++c) {
            const auto& p=g_eng->sequence_pattern(bank);
            if((g_b_length_edits[c]&(1u<<bank)) && p.length[c]!=g_b_length[bank][c]) {
                g_eng->set_sequence_length(c,g_b_length[bank][c],bank);changed=true;
            }
            if((g_b_direction_edits[c]&(1u<<bank)) && (int)p.direction[c]!=g_b_direction[bank][c]) {
                g_eng->set_sequence_direction(c,(SequenceDirection)g_b_direction[bank][c],bank);changed=true;
            }
        }
        g_seq_bank=g_b_bank;
        if(changed)mark_project_mutated();
    } else nudge_step_values(-1, false);
    g_b_pressed=g_b_open=false;g_b_rail=-1;g_b_confirm=0;g_bank_panel.open=false;
    g_b_drain=(held&(B_OWNED_KEYS&~KEY_B))!=0;
}
// B owns every score input until all participating controls have been released.
// The initial press is provisional: only a short release decreases the focused value.
bool bank_hold_buttons(u32 down,u32 held,u32 up) {
    if(g_b_drain) {
        g_touch_blocked_frame=true;
        if(!(held&B_OWNED_KEYS))g_b_drain=false;
        return true;
    }
    if(!g_b_pressed)return false;
    g_touch_blocked_frame=true;g_l_held=false;
    if(!g_b_open && g_ui_frame-g_b_start>=B_HOLD_FRAMES) { g_b_open=true;g_b_dirty=true; }
    if((up&KEY_B)||!(held&KEY_B)) { finish_bank_hold(held);return true; }
    if(!g_b_open)return true;
    const u32 keys=down&(B_OWNED_KEYS&~u32(KEY_B|KEY_TOUCH));
    if(keys) {
        // Any other key withdraws an armed replace; a status lives until the next key.
        if(!(g_b_confirm==1 && keys==KEY_Y))g_b_confirm=0;
        g_b_status[0]=0;
    }
    if(bool(down&KEY_L)!=bool(down&KEY_R)) {
        g_b_bank=(g_b_bank+NUM_PATTERNS+((down&KEY_L)?-1:1))%NUM_PATTERNS;g_b_dirty=true;
    }
    u32 key=held&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT);
    // Opposing/simultaneous arrows do not produce an accidental extra operation.
    if(key && (key&(key-1)))key=0;
    bool repeat=false;
    if(key!=g_b_repeat_key) { g_b_repeat_key=key;g_b_repeat_start=g_ui_frame; }
    else if((key&(KEY_DLEFT|KEY_DRIGHT)) && g_ui_frame-g_b_repeat_start>=18 &&
            (g_ui_frame-g_b_repeat_start-18)%5==0)repeat=true;
    if(key && ((down&key)||repeat)) {
        if(key==KEY_DUP)g_b_track=(g_b_track+NUM_CHAINS-1)%NUM_CHAINS;
        else if(key==KEY_DDOWN)g_b_track=(g_b_track+1)%NUM_CHAINS;
        else bank_set_length(g_b_track,g_b_length[g_b_bank][g_b_track]+(key==KEY_DLEFT?-1:1));
    }
    if(down&KEY_A)bank_cycle_direction(g_b_track);
    if(down&KEY_X)bank_copy();
    if(down&KEY_Y)bank_paste();
    return true;
}
void bank_touch_hold(int tx,int) {
    using namespace bank_panel;
    if(g_b_rail<0)return;
    const int x=tx<RAIL_X0?RAIL_X0:tx>RAIL_X1?RAIL_X1:tx;
    bank_set_length(g_b_rail,1+(x-RAIL_X0+1)/2);
}
// The panel is a real control surface while B is held: numbers, rails and words.
void bank_touch_down(int tx,int ty) {
    using namespace bank_panel;
    const int armed=g_b_confirm;
    g_b_confirm=0;g_b_status[0]=0;g_b_rail=-1;
    if(ty>=DIGIT_TOP && ty<DIGIT_BOTTOM) {
        const int left=DIGIT_X-DIGIT_PITCH/2;
        const int bank=tx>=left?(tx-left)/DIGIT_PITCH:-1;
        if(bank>=0 && bank<NUM_PATTERNS && bank!=g_b_bank) { g_b_bank=bank;g_b_dirty=true; }
        return;
    }
    for(int c=0;c<NUM_CHAINS;++c) {
        const int y=row_y(c);
        if(ty<y-7 || ty>=y+15)continue;
        g_b_track=c;
        if(tx>=RAIL_X0-6 && tx<=RAIL_X1+6) { g_b_rail=c;bank_touch_hold(tx,ty); }
        else if(tx>=232)bank_cycle_direction(c);
        return;
    }
    if(ty<FOOT_Y-6)return;
    if(tx>=COPY_X-6 && tx<PASTE_X-6)bank_copy();
    else if(tx>=PASTE_X-6 && tx<CLEAR_X-6) { g_b_confirm=armed==1?1:0;bank_paste(); }
    else if(tx>=CLEAR_X-6 && tx<CLEAR_X+36) { g_b_confirm=armed==2?2:0;bank_clear(); }
}
// Before drawing, outside the GPU frame. Stored steps are read only when the
// candidate or the content changed; staged values need no engine access.
void bank_capture() {
    auto& d=g_bank_panel;
    d.open=g_b_open;
    if(!g_b_open)return;
    if(g_b_dirty && g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);
        d.empty=0;
        for(int bank=0;bank<NUM_PATTERNS;++bank) {
            const Pattern& p=g_eng->sequence_pattern(bank);bool used=false;
            for(int c=0;c<NUM_CHAINS;++c) {
                uint64_t trig=0,lock=0;
                for(int s=0;s<SEQ_STEPS;++s) {
                    if(p.steps[c][s].flags&SeqStep::Trig)trig|=seq_edit::bit(s);
                    if(p.steps[c][s].lock_count)lock|=seq_edit::bit(s);
                }
                used=used||trig||lock;
                if(bank==g_b_bank) { d.trig[c]=trig;d.lock[c]=lock; }
            }
            if(!used)d.empty|=uint8_t(1u<<bank);
        }
        g_b_dirty=false;
    }
    d.bank=uint8_t(g_b_bank);d.play=uint8_t(g_b_play);d.track=uint8_t(g_b_track);
    for(int c=0;c<NUM_CHAINS;++c) { d.length[c]=g_b_length[g_b_bank][c];d.direction[c]=g_b_direction[g_b_bank][c]; }
}
void draw_bank_hold() {
    using namespace bank_panel;
    const auto& d=g_bank_panel;
    ui::paper(320);
    // Only the dimmed frozen trace stays behind the panel: the upper screen
    // shows what the candidate bank really stores.
    draw_wave(true);
    m::heading(16,18,"bank",seq_ink());
    ui::text(RIGHT-ui::text_w("hold b"),22,"hold b",seq_spot());
    for(int b=0;b<NUM_PATTERNS;++b) {
        const int center=DIGIT_X+b*DIGIT_PITCH;
        const char text[2]={(char)('1'+b),0};
        const auto* glyph=inspector_gothic::digit(text[0]);
        // Approved native 1x numerals; only color marks candidate and empty banks.
        const u32 color=b==d.bank?seq_spot():((d.empty>>b)&1)?palette::subdued():seq_ghost();
        inspector_gothic::draw_numeric(center-glyph->width/2,DIGIT_BASE,text,color,seq_spot(),1);
        if(b==d.play)ui::text(center-11,92,"play",seq_ghost());
    }
    static const int marks[]={1,16,32,48,64};
    for(int n:marks) {
        char t[4];std::snprintf(t,sizeof t,"%d",n);
        ui::text(rail_x(n)-ui::text_w(t)/2+1,110,t,seq_ghost());
    }
    static const char* names[]={"forward","reverse","ping-pong"};
    for(int c=0;c<NUM_CHAINS;++c) {
        const int y=row_y(c);const bool on=c==d.track;
        char t[8];
        if(on)m::marker(10,y+3,seq_spot());
        std::snprintf(t,sizeof t,"%d",c+1);ui::text(16,y,t,on?seq_spot():seq_ink());
        ui::text(28,y,source_name(c),seq_ink());
        const int x=rail_x(d.length[c]<1?1:d.length[c]>SEQ_STEPS?SEQ_STEPS:d.length[c]);
        ui::horizontal(RAIL_X0,RAIL_X1,y+4,x,x);
        std::snprintf(t,sizeof t,"%02u",unsigned(d.length[c]));ui::text(214,y,t,on?seq_spot():seq_ink());
        ui::text(238,y,names[d.direction[c]%3],on?seq_spot():seq_ink());
    }
    ui::text(COPY_X,FOOT_Y,"x copy",seq_ink());
    ui::text(PASTE_X,FOOT_Y,"y paste",g_b_confirm==1?seq_spot():g_b_copy>=0?seq_ink():seq_ghost());
    ui::text(CLEAR_X,FOOT_Y,"clear",g_b_confirm==2?seq_spot():seq_ink());
    const char* right=g_b_status[0]?g_b_status:"release keeps";
    ui::text(RIGHT-ui::text_w(right),FOOT_Y,right,g_b_status[0]?seq_spot():seq_ghost());
}
