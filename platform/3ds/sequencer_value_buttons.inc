// Included only inside ui_sequencer.cpp's private namespace.
// Buttons always nudge each target's own stored/base value. The existing touch
// strip's explicit set/offset choice is independent of this relative operation.
void nudge_step_values(int direction, bool group) {
    if (!direction || !g_eng || !g_audio) return;
    finish_edit();
    platform::AudioOut::Guard guard(*g_audio);
    capture_score_locked();
    sync_edit_context();
    if (group && !g_selection.mask) {
        edit_status("select a group first");
        return;
    }
    direction = direction > 0 ? 1 : -1;
    const uint64_t targets = group ? g_selection.mask : seq_edit::bit(g_seq_col);
    int slot = -1, param = -1;
    uint16_t slices = 0;
    const bool parameter_mode = g_seq_mode == SeqMode::Note || g_seq_mode == SeqMode::Level ||
                                g_seq_mode == SeqMode::Lock || g_seq_mode == SeqMode::Slice;
    if (g_seq_mode == SeqMode::Note) {
        slot = g_score.sequence_pitch_slot[g_seq_chain];
        param = g_score.sequence_pitch_param[g_seq_chain];
    } else if (g_seq_mode == SeqMode::Level) {
        slot = g_score.sequence_value_slot[g_seq_chain];
        param = g_score.sequence_value_param[g_seq_chain];
    } else if (g_seq_mode == SeqMode::Lock) {
        normalize_lock_target();
        slot = g_lock_slot; param = g_lock_param;
    } else if (g_seq_mode == SeqMode::Slice) {
        slot = slice_source_slot(g_seq_chain); param = PlayUnit::P_SLICE;
        if (slot >= 0) slices = slice_source_mask(slot);
        if (!slices) { edit_status("no playable cuts"); return; }
    }
    if (parameter_mode && !valid_lock_target(slot, param)) {
        edit_status("no parameter target");
        return;
    }
    const Unit* unit = parameter_mode ? g_eng->chains[g_seq_chain].units[slot] : nullptr;
    g_edit_draft.read(edit_pattern(), g_seq_chain);
    for (int s = 0; s < SEQ_STEPS; ++s) {
        if (!(targets & seq_edit::bit(s))) continue;
        auto& step = g_edit_draft.steps[s];
        if (g_seq_mode == SeqMode::Trig) {
            if (direction > 0) step.flags |= SeqStep::Trig;
            else step.flags &= ~SeqStep::Trig;
        } else if (g_seq_mode == SeqMode::Retrig) {
            step.set_retrig_count(uint8_t(std::max(1, std::min(4, int(step.retrig_count()) + direction))));
        } else if (g_seq_mode == SeqMode::When) {
            const int count = int(SeqStep::Condition::Count);
            step.set_condition(SeqStep::Condition((int(step.condition()) + direction + count) % count));
        } else if (g_seq_mode == SeqMode::Time) {
            g_edit_draft.timing[s] = int8_t(std::max(-SEQ_MICROTIMING_MAX,
                std::min(SEQ_MICROTIMING_MAX, int(g_edit_draft.timing[s]) + direction)));
        } else if (unit) {
            const auto& target = unit->params[param];
            const int found = seq_edit::find(step, slot, param);
            const q15 current = found >= 0 ? step.locks[found].value : target.base;
            int next = current;
            if (g_seq_mode == SeqMode::Note) next = nudged_pitch(current, direction);
            else if (g_seq_mode == SeqMode::Level) next = int(current) + direction * 331;
            else if (g_seq_mode == SeqMode::Slice) {
                const int from = seq_slice::decode(current);
                const int to = nudged_slice(slices, from, direction);
                if (from == to) continue; // retain legacy raw values at boundaries
                next = seq_slice::encode(to);
            } else if (unit->kind == UnitKind::Comp && param == CompUnit::P_RELEASE) {
                const int clock = seq_edit::find(step, slot, CompUnit::P_CLOCK);
                const bool sync = (clock >= 0 ? step.locks[clock].value :
                                   unit->params[CompUnit::P_CLOCK].base) > 16383;
                next = seq_lock::comp_release_nudge(current, direction, sync);
            } else {
                next = seq_lock::nudge_value(unit->kind, seq_lock::variant(*unit),
                                             param, current, direction, target.bipolar);
            }
            next = std::max(target.bipolar ? -32767 : 0, std::min(32767, next));
            if (next == current) continue;
            const bool pitch_target = slot == g_score.sequence_pitch_slot[g_seq_chain] &&
                                      param == g_score.sequence_pitch_param[g_seq_chain];
            if (!seq_edit::put(step, slot, param, q15(next), target.base, pitch_target)) {
                edit_status("full: needs more than 4 locks");
                g_seq_lock_flash = 90;
                return; // only the draft changed: never publish a partial group
            }
        }
    }
    const bool changed = commit_draft();
    finish_edit();
    if (changed && !group && (g_seq_mode == SeqMode::Note || g_seq_mode == SeqMode::Slice))
        g_eng->audition_sequence_step(g_seq_chain, g_seq_col, g_seq_bank);
    edit_status(changed ? (group ? (direction > 0 ? "group values +" : "group values -") : "") : "unchanged / at limit");
}

void audition_focused_step() {
    if (!g_eng || !g_audio) return;
    platform::AudioOut::Guard guard(*g_audio);
    const bool heard = g_eng->audition_sequence_step(g_seq_chain, g_seq_col, g_seq_bank);
    if (heard) note_value_changed(); // presentation only; no dirty/history entry
    edit_status(heard ? "listen" : "listen unavailable");
}
