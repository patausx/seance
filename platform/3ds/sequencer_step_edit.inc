// Included inside ui_sequencer.cpp's private namespace; no audio-path work.
void edit_status(const char* text) { std::snprintf(g_edit_status,sizeof g_edit_status,"%s",text); }
uint64_t chain_signature() {
    uint64_t hash=1469598103934665603ull;
    const auto add=[&](uint64_t v) { hash^=v; hash*=1099511628211ull; };
    const auto& bank=trackr::synth::SampleBank::instance();
    for (int s=0;s<UNITS_PER_CHAIN;++s) {
        const Unit* u=g_eng->chains[g_seq_chain].units[s];
        add(reinterpret_cast<uintptr_t>(u));
        if(!u) continue;
        add((int)u->kind); add(u->num_params); add(seq_lock::variant(*u));
        for(int p=0;p<u->num_params;++p) add(u->params[p].bipolar);
        int sample=-1;
        if(u->kind==UnitKind::SamplePlayer) {
            const auto& play=*static_cast<const PlayUnit*>(u);
            sample=play.sample_slot; add(play.pitch_quantize);
        } else if(u->kind==UnitKind::Grain) sample=static_cast<const GrainUnit*>(u)->sample_slot;
        add(sample+1);
        if(sample>=0 && sample<trackr::synth::SAMPLE_BANK_SIZE) {
            const auto& src=bank.slot(sample);
            add(bank.generation(sample)); add(src.playable_frames());
            add(src.slice_reverse_mask);
            for(int i=0;i<16;++i) { add(src.chops[i]); add(src.slice_speed_q8[i]); }
        }
    }
    return hash;
}
void sync_edit_context() { // caller holds Guard
    const uint64_t signature=chain_signature();
    if(g_target_signature[g_seq_chain]!=signature) {
        for(auto& p:g_param_memory[g_seq_chain]) p=0;
        g_target_signature[g_seq_chain]=signature;
        g_lock_param=0;
    }
    if(g_edit_bank==g_seq_bank && g_history_chain==g_seq_chain && g_edit_signature==signature) return;
    g_selection.clear(); g_history.clear();
    g_value_drag=false; g_select_chain=-1; g_target_picker=false;
    g_offset=false; g_edit_status[0]=0;
    g_edit_bank=g_seq_bank; g_history_chain=g_seq_chain; g_edit_signature=signature;
}
void begin_edit() {
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context();
    g_edit_before.read(edit_pattern(),g_seq_chain); g_history.begin(g_edit_before);
}
void finish_edit() {
    if(!g_history.active || !g_eng || !g_audio) return;
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context();
    g_edit_draft.read(edit_pattern(),g_seq_chain); g_history.finish(g_edit_draft);
}
void cancel_edit() {
    if(!g_history.active || !g_eng || !g_audio) return;
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context();
    if(g_history.active) {
        g_history.before.write(edit_pattern(),g_seq_chain); g_history.active=false;
        g_eng->cancel_sequence_audition();
    }
}
bool commit_draft() { // caller holds Guard; atomic row publication
    g_edit_before.read(edit_pattern(),g_seq_chain);
    if(seq_edit::equal(g_edit_before,g_edit_draft)) return false;
    g_history.begin(g_edit_before);
    g_edit_draft.write(edit_pattern(),g_seq_chain);
    g_eng->cancel_sequence_audition();
    mark_project_mutated(); note_value_changed(); g_edit_status[0]=0; return true;
}
void edit_group_lock(int value, bool relative=false, bool remove=false) {
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context();
    if(g_lock_slot<0 || g_lock_slot>=UNITS_PER_CHAIN) return;
    const Unit* unit=g_eng->chains[g_seq_chain].units[g_lock_slot];
    if(!unit || g_lock_param<0 || g_lock_param>=unit->num_params) return;
    const auto& param=unit->params[g_lock_param];
    const bool offset=g_offset && seq_lock::supports_offset(unit->kind,seq_lock::variant(*unit),g_lock_param);
    g_edit_draft.read(edit_pattern(),g_seq_chain);
    const auto targets=g_selection.targets(g_seq_col);
    const int focus=seq_edit::find(g_edit_draft.steps[g_seq_col],g_lock_slot,g_lock_param);
    const q15 current=focus<0 ? param.base : g_edit_draft.steps[g_seq_col].locks[focus].value;
    const bool comp_release=unit->kind==UnitKind::Comp && g_lock_param==CompUnit::P_RELEASE;
    const auto stored_sync=[&](int s) {
        const auto& step=g_edit_draft.steps[s];
        const int i=seq_edit::find(step,g_lock_slot,CompUnit::P_CLOCK);
        return (i<0?unit->params[CompUnit::P_CLOCK].base:step.locks[i].value)>16383;
    };
    const bool release_sync=comp_release && stored_sync(g_seq_col);
    if(comp_release && !remove)for(int s=0;s<SEQ_STEPS;++s)if(targets&seq_edit::bit(s)) {
        if(offset && stored_sync(s)) {edit_status("sync release: use set, not raw offset");return;}
        if(relative && !offset && stored_sync(s)!=release_sync) {edit_status("release: mixed clocks / choose one mode");return;}
    }
    const q15 common=relative ? (comp_release?seq_lock::comp_release_nudge(current,value,release_sync):
        seq_lock::nudge_value(unit->kind,seq_lock::variant(*unit),g_lock_param,current,value,param.bipolar)) : (q15)value;
    for(int s=0;s<SEQ_STEPS;++s) if(targets&seq_edit::bit(s)) {
        auto& step=g_edit_draft.steps[s];
        if(remove) { seq_edit::remove(step,g_lock_slot,g_lock_param); continue; }
        const int found=seq_edit::find(step,g_lock_slot,g_lock_param);
        int next=common;
        if(offset) {
            const q15 old=found<0 ? param.base : step.locks[found].value;
            // Keyboard uses 1% q15 full-scale; touch uses a relative delta from
            // the gesture's frozen starting row (never accumulates sample jitter).
            if(relative) next=seq_lock::nudge_value(unit->kind,seq_lock::variant(*unit),g_lock_param,old,value,param.bipolar);
            else {
                const auto& original=g_history.before.steps[s];
                const int i=seq_edit::find(original,g_lock_slot,g_lock_param);
                next=(i<0?param.base:original.locks[i].value)+value;
            }
        }
        const int low=param.bipolar?-32767:0;
        next=next<low?low:next>32767?32767:next;
        const bool pitch_target=g_lock_slot==g_score.sequence_pitch_slot[g_seq_chain] &&
                                g_lock_param==g_score.sequence_pitch_param[g_seq_chain];
        if(!seq_edit::put(step,g_lock_slot,g_lock_param,(q15)next,param.base,pitch_target)) {
            if(g_value_drag) { cancel_edit(); g_value_drag=false; }
            edit_status("full: needs more than 4 locks"); g_seq_lock_flash=90; return;
        }
    }
    commit_draft();
}
void copy_steps() {
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context();
    if(g_copy_scope==seq_edit::Scope::Parameter && (g_lock_slot<0 || g_lock_param<0 ||
        g_lock_slot>=UNITS_PER_CHAIN || !g_eng->chains[g_seq_chain].units[g_lock_slot] ||
        g_lock_param>=g_eng->chains[g_seq_chain].units[g_lock_slot]->num_params)) { edit_status("no parameter"); return; }
    g_clipboard.data.read(edit_pattern(),g_seq_chain);
    g_clipboard.mask=g_selection.targets(g_seq_col); g_clipboard.chain=g_seq_chain;
    g_clipboard.signature=g_edit_signature; g_clipboard.scope=g_copy_scope;
    g_clipboard.slot=g_lock_slot; g_clipboard.param=g_lock_param;
    char text[48]; std::snprintf(text,sizeof text,"copied %d %s",seq_edit::count(g_clipboard.mask),seq_edit::scope_name(g_clipboard.scope)); edit_status(text);
}
void paste_steps() {
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context();
    if(!g_clipboard.mask) { edit_status("clipboard empty"); return; }
    if(g_clipboard.chain!=g_seq_chain || g_clipboard.signature!=g_edit_signature) { edit_status("paste: incompatible chain/source"); return; }
    if(g_clipboard.scope==seq_edit::Scope::Parameter && (g_clipboard.slot!=g_lock_slot || g_clipboard.param!=g_lock_param)) {
        edit_status("paste: select the copied parameter"); return;
    }
    uint64_t destinations=0;
    if(!g_clipboard.destinations(g_selection.mask,g_seq_col,destinations)) { edit_status("paste: count or end beyond 64"); return; }
    g_edit_draft.read(edit_pattern(),g_seq_chain);
    if(!g_clipboard.paste(g_edit_draft,g_selection.mask,g_seq_col,destinations)) { edit_status("paste: lock slots full"); return; }
    commit_draft(); finish_edit(); edit_status("pasted");
}
void history_step(bool redo) {
    finish_edit();
    platform::AudioOut::Guard guard(*g_audio);
    sync_edit_context(); g_edit_draft.read(edit_pattern(),g_seq_chain);
    const bool changed=redo ? g_history.redo(g_edit_draft) : g_history.undo(g_edit_draft);
    if(changed) {
        g_edit_draft.write(edit_pattern(),g_seq_chain); mark_project_mutated();
        g_eng->cancel_sequence_audition(); note_value_changed();
    }
    edit_status(changed ? (redo?"redo":"undo") : "no history / context changed");
}
