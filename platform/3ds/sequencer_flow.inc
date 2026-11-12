// Included inside ui_sequencer.cpp's private namespace, in place of the old flow lane.
// A constant source in the trig layer: the stored value curve IS the row. A tap
// keeps toggling the trig, which is now visible; a stylus stroke draws the curve
// as ordinary step locks of the level target; the selection plate travels along
// the curve; one small solid lancet diamond shows the real effective value at the
// playhead. No format, DSP or scheduler change.
int flow_y(int chain, int step, int y) { return y + 26 - value_percent(chain, step) * 23 / 100; }
// Stored curve between the sixteen visible points, for a moving plate or mark.
float flow_curve_y(int chain, int first, float xc, int y) {
    const float pos = (xc - float(pattern_x(0) + 8)) / float(PITCH);
    if (pos <= 0) return float(flow_y(chain, first, y));
    if (pos >= PAGE_STEPS - 1) return float(flow_y(chain, first + PAGE_STEPS - 1, y));
    const int i = int(pos);
    const float a = float(flow_y(chain, first + i, y)), b = float(flow_y(chain, first + i + 1, y));
    return a + (b - a) * (pos - float(i));
}
// The event plate's 100ms cubic ease-out along x only. Presentation state: the
// logical selection and the playhead are already at their new step.
struct FlowGlide {
    float from = 0, to = 0, shown = 0;
    uint64_t began = 0, last = 0;
    int context = -1;
    void reset() { context = -1; }
    float update(uint64_t now, int next_context, float target, bool snap = false) {
        if (snap || context != next_context || now < last || now - last > 200) { from = to = shown = target; began = now; }
        else if (target != to) { from = shown; to = target; began = now; }
        context = next_context; last = now;
        const uint64_t age = now - began;
        if (age >= step_focus::DURATION_MS) shown = to;
        else {
            const float rest = 1 - float(age) / step_focus::DURATION_MS;
            shown = from + (to - from) * (1 - rest * rest * rest);
        }
        return shown;
    }
};
static_assert(sizeof(FlowGlide) <= 40, "bounded scalar flow motion");
FlowGlide g_flow_focus, g_flow_live;
bool g_flow_touch = false;
int g_touch_origin_y = 0, g_flow_last_value = 0, g_flow_pop_step = -1;
uint64_t g_flow_pop_at = 0;

void draw_flow_lane(int chain, int first, int y) {
    namespace ui = score_ink;
    const u32 color = chain == g_seq_chain ? seq_spot() : seq_ink();
    int px = pattern_x(0) + 8, py = flow_y(chain, first, y);
    for (int local = 0; local < PAGE_STEPS; ++local) {
        const int step = first + local, x = pattern_x(local) + 8;
        const int yy = flow_y(chain, step, y);
        const bool in = step < g_score.sequence_length[chain];
        if (local)
            ui::line(px, py, x, yy, in ? color : seq_ghost(), 1);
        const bool selected = chain == g_seq_chain && step == g_seq_col;
        // A freshly drawn point answers the stylus for 70ms; nothing is queued.
        const bool pop = selected && step == g_flow_pop_step && g_feedback_now >= g_flow_pop_at &&
                         g_feedback_now - g_flow_pop_at < 70;
        score_ink::point(x, yy, pop ? 2 : 1, selected ? seq_spot() : seq_ghost());
        // The trig of a constant source is stored (and restarts RULE/ROT): show it.
        if (meta_trig(g_score.sequence_meta[chain][step]))
            ui::rect(x - 1, y + 28, 3, 2, in ? seq_ink() : seq_ghost());
        px = x;
        py = yy;
    }
}
// Selected flow row only: its level target is already copied every frame.
void draw_flow_live(int first) {
    if (ui::pen.cache) return; // never part of a frozen disclosure background
    const int c = g_seq_chain;
    const int slot = g_score.sequence_value_slot[c], param = g_score.sequence_value_param[c];
    const int head = g_score.sequence_step[c];
    if (!g_feedback_visible || !flow_row() || slot >= UNITS_PER_CHAIN || param >= g_inspector.modules[slot].count ||
        !g_score.transport_playing || !g_score.sequencer_enabled || g_score.pattern_transition.active ||
        g_seq_bank != g_score.active_pattern || head >= g_score.sequence_length[c] || head / PAGE_STEPS != g_seq_page) {
        g_flow_live.reset();
        return;
    }
    int value = g_inspector.modules[slot].params[param].live; // Param::effective, not the stored lock
    if (value < 0) value = -value;
    if (value > 32767) value = 32767;
    const int y = PATTERN_ROWS[c];
    const float target = float(pattern_x(head % PAGE_STEPS) + 8);
    // A wrap or a jump is not a sweep back across the row.
    const bool jump = target - g_flow_live.to > PITCH + 1 || g_flow_live.to - target > PITCH + 1;
    const float xc = g_flow_live.update(g_feedback_now, feedback_context() * NUM_CHAINS + c, target, jump);
    const int x = int(std::floor(xc + .5f));
    const int stored = int(std::floor(flow_curve_y(c, first, xc, y) + .5f));
    const int live = y + 26 - ((value * 100 + 16383) / 32767) * 23 / 100;
    // Lancet grammar: the line is the real displacement from the stored value.
    if (live != stored) ui::rect(x, live < stored ? live : stored, 1, (live < stored ? stored - live : live - stored) + 1, seq_spot());
    module_fader::detail::diamond(module_fader::detail::Pen{x, y + 26, true}, y + 26 - live, true, seq_spot(), true);
}
// Inverse of flow_y: the drawn point sits under the stylus, clamped to the row.
int flow_value_at(int chain, int ty) {
    int height = PATTERN_ROWS[chain] + 26 - ty;
    height = height < 0 ? 0 : height > 23 ? 23 : height;
    return ((height * 100 + 22) / 23) * 32767 / 100;
}
// Continued stylus contact on a flow row in the trig layer.
void flow_draw_hold(int tx, int ty) {
    const int chain = g_pending_chain;
    const int dx = tx > g_touch_origin_x ? tx - g_touch_origin_x : g_touch_origin_x - tx;
    const int dy = ty > g_touch_origin_y ? ty - g_touch_origin_y : g_touch_origin_y - ty;
    // Still a tap (the lift toggles the trig): the trig paint's 9px sideways, or 6px of height.
    if (!g_paint_active && dx < 9 && dy < 6) return;
    const int value = flow_value_at(chain, ty);
    const int step = g_seq_page * PAGE_STEPS + local_from_touch(tx);
    platform::AudioOut::Guard guard(*g_audio);
    const auto signature = g_edit_signature;
    capture_score_locked(); sync_edit_context();
    if (signature != g_edit_signature || !flow_row(chain)) return; // a held stroke cannot retarget a replaced source
    const int slot = g_score.sequence_value_slot[chain], param = g_score.sequence_value_param[chain];
    const Unit* unit = slot < UNITS_PER_CHAIN ? g_eng->chains[chain].units[slot] : nullptr;
    if (!unit || param >= unit->num_params) return;
    if (!g_paint_active) {
        begin_edit(); // one undo entry for the whole stroke
        // The stroke starts where the stylus went down, not where it was first sampled.
        g_paint_active = true; g_paint_last_step = g_pending_step;
        g_flow_last_value = flow_value_at(chain, g_touch_origin_y);
    }
    const q15 base = unit->params[param].base;
    const int from = g_paint_last_step, span = step - from, direction = span >= 0 ? 1 : -1;
    bool changed = false, full = false;
    // Fill skipped columns too, on the straight line between two samples.
    for (int s = from;; s += direction) {
        const int v = span ? g_flow_last_value + (value - g_flow_last_value) * (s - from) / span : value;
        SeqStep& target = edit_pattern().steps[chain][s];
        const int found = seq_edit::find(target, slot, param);
        if ((found >= 0 ? target.locks[found].value : base) != v) {
            if (found >= 0) { target.locks[found].value = q15(v); changed = true; }
            else if (target.lock_count < SEQ_LOCKS) {
                target.locks[target.lock_count++] = {uint8_t(slot), uint8_t(param), q15(v)}; changed = true;
            } else full = true; // that step keeps its four other locks
        }
        if (s == step) break;
    }
    g_paint_last_step = g_seq_col = step; g_flow_last_value = value;
    g_flow_pop_step = step; g_flow_pop_at = osGetTime();
    if (full) { g_seq_lock_flash = 90; edit_status("lock slots full on a step"); }
    if (changed) mark_project_mutated();
}
