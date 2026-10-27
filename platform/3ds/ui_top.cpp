#include "ui_state.h"
#include "ui_sample_library.h"
#include "ui_transition.h"
#include "ui_performance.h"
#include "ui_sigils.h"
#include "ui_patchbay.h"
#include "ui_chains.h"
#include "ui_param_lens.h"
#include "fm_context.h"
#include "strn_focus.h"
#include "ui_module_focus_internal.h"
#include <citro2d.h>
#include "ui_theme.h"
#include "ui_palette.h"
#include "audio_out.h"
#include "ui_workspace.h"
#include "ui_sequencer.h"
#include "sequencer_inspector.h"
#include "sequencer_lock_format.h"
#include "sequencer_slices.h"
#include "sequencer_cuts.h"
#include "slice_boundaries.h"
#include "inspector_gothic.h"
#include "module_inspector.h"
#include "sequencer_profile.h"
#include "menu_style.h"
#include "note_display.h"
#include "../../core/engine/units.h"
#include "../../core/ui/draw.h"
#include <cstdio>
#include <algorithm>
#include <cstring>

using namespace seance;
namespace seance::platform::ui3ds {

namespace {
namespace m = menu_style;

const char* short_unit_name(UnitKind kind) {
    switch (kind) {
        case UnitKind::Empty:        return "----";
        case UnitKind::SamplePlayer: return "PLY";
        case UnitKind::Grain:        return "GRN";
        case UnitKind::Osc:          return "OSC";
        case UnitKind::Filter:       return "FLT";
        case UnitKind::Vca:          return "VCA";
        case UnitKind::Delay:        return "DLY";
        case UnitKind::Fold:         return "FLD";
        case UnitKind::MicIn:        return "MIC";
        case UnitKind::Recorder:     return "REC";
        case UnitKind::Bitcrush:     return "BIT";
        case UnitKind::Chorus:       return "CHR";
        case UnitKind::Reverb:       return "REV";
        case UnitKind::Stranular:    return "SWM";
        case UnitKind::Ghost:        return "GHO";
        case UnitKind::Inst:         return "INS";
        case UnitKind::Glitch:       return "GLT";
        case UnitKind::Arp:          return "ARP";
        case UnitKind::Mouth:        return "MTH";
        case UnitKind::Comp:         return "CMP";
        case UnitKind::Fract:        return "FRC";
        case UnitKind::Bird:         return "BRD";
        case UnitKind::Shift:        return "SHF";
        case UnitKind::Ring:         return "RNG";
        case UnitKind::Tilt:         return "TLT";
        case UnitKind::Noise:        return "NOI";
        case UnitKind::Fm:           return "FM";
        case UnitKind::Motor:        return "MTR";
        case UnitKind::Strain:       return "STAI";
        default:                     return "---";
    }
}

void snapshot_route(int chain, char* out, int capacity) {
    if (!out || capacity < 1) return;
    int cursor = 0;
    out[0] = 0;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        const UnitKind kind = (UnitKind)g_ui_snapshot.unit_kind[chain][slot];
        if (kind == UnitKind::Empty) continue;
        const char* name = short_unit_name(kind);
        if (cursor && cursor < capacity - 2) out[cursor++] = '>';
        for (const char* p = name; *p && cursor < capacity - 1; ++p) out[cursor++] = *p;
    }
    out[cursor] = 0;
}

void draw_sigils_top() { sigils_top_draw(); }

const char* top_condition_human(SeqStep::Condition condition) {
    static const char* names[] = {"all", "50%", "25%", "75%", "1:2", "2:2", "fill", "!fill"};
    const int i = (int)condition;
    return i >= 0 && i < (int)SeqStep::Condition::Count ? names[i] : "unknown";
}

const char* top_sequence_source_name(int chain) {
    const UnitKind kind = (UnitKind)sequencer_snapshot().sequence_source_kind[chain];
    if (kind == UnitKind::Inst) return "inst";
    if (kind == UnitKind::Bird) return "bird";
    if (kind == UnitKind::Osc) return "osc";
    if (kind == UnitKind::Fm) return "fm";
    if (kind == UnitKind::SamplePlayer) return "play";
    if (kind == UnitKind::Grain) return "gran";
    if (kind == UnitKind::Motor) return "motor";
    if (kind == UnitKind::Noise) return "noise";
    if (kind == UnitKind::MicIn) return "mic";
    if (kind == UnitKind::Stranular) return m::module_name(kind);
    return "chain";
}

const char* sequence_route_name(UnitKind kind) {
    switch (kind) {
        case UnitKind::SamplePlayer: return "play";
        case UnitKind::Stranular: return m::module_name(kind);
        case UnitKind::Recorder: return "rec";
        case UnitKind::Bitcrush: return "bit";
        case UnitKind::Chorus: return "chor";
        case UnitKind::Reverb: return "rev";
        case UnitKind::Ghost: return "ghost";
        case UnitKind::Strain: return "strain";
        default: return m::module_name(kind);
    }
}

void draw_sequencer_path(const SequencerInspector& d, int layer) {
    const auto& score = sequencer_snapshot();
    char context[40];
    std::snprintf(context, sizeof context, "chain %02d", d.chain + 1);
    ui::text(24, 31, context, m::ink());
    const auto direction = (SequenceDirection)score.sequence_direction[d.chain];
    const char* direction_name = direction == SequenceDirection::Reverse ? "reverse" :
                                 direction == SequenceDirection::PingPong ? "ping-pong" : "forward";
    std::snprintf(context, sizeof context, "%d steps / %s", score.sequence_length[d.chain], direction_name);
    ui::text(376 - ui::text_w(context), 31, context, m::ghost());
    // The full six-slot route gets its own compact row. No framing/divider lines;
    // only the existing functional target underline and bypass strike remain.
    const int selected_slot = layer == 1 ? score.sequence_pitch_slot[d.chain] :
                              layer == 2 ? score.sequence_value_slot[d.chain] :
                              layer == 6 ? d.slot : layer == seq_slice::MODE ? sequencer_slices().rows[d.chain].slot : -1;
    int x = 24;
    for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        const auto& module = d.modules[s];
        if (module.kind == UnitKind::Empty) continue;
        const char* name = sequence_route_name(module.kind);
        const int w = ui::text_w(name);
        const bool selected = s == selected_slot || (selected_slot < 0 && x == 24);
        ui::text(x, 45, name, selected ? m::spot() : module.bypass ? m::ghost() : m::ink());
        if (selected) ui::line(x, 56, x + w - 1, 56, m::spot(), 1);
        if (module.bypass) ui::line(x, 53, x + w - 1, 53, selected ? m::spot() : m::ghost(), 1);
        x += w;
        ui::text(x + 5, 45, "/", m::ghost()); x += 17;
    }
    ui::text(x, 45, "out", selected_slot == UNITS_PER_CHAIN ? m::spot() : m::ink());
}
void draw_sequencer_extent() {
    const auto& score = sequencer_snapshot();
    const int chain = sequencer_selected_chain(), selected = sequencer_selected_step();
    const int length = std::max(1, std::min(64, int(score.sequence_length[chain])));
    const int page = selected / 16;
    const bool muted = score.chain_mute[chain];
    // Read-only overview in the previously empty lower-left inspector area.
    // Bare marks and page labels, without the proposal's framing/bracket rules.
    for (int step = 0; step < 64; ++step) {
        const int x = 16 + (step / 16) * 48 + (step % 16) * 2;
        const u32 color = step >= length ? palette::subdued() :
                          step / 16 == page && !muted ? m::ink() : m::ghost();
        if (step == selected) ui::rect(x, 185, 2, 9, m::spot());
        else ui::rect(x, 187, 1, 6, color);
    }
    const int head = score.sequence_step[chain];
    if (score.transport_playing && score.sequencer_enabled && !muted &&
        !score.pattern_transition.active && sequencer_edit_bank() == score.active_pattern && head < length) {
        const int x = 16 + (head / 16) * 48 + (head % 16) * 2;
        ui::rect(x, 181, 2, 2, palette::playhead());
    }
    for (int group = 0; group < 4; ++group) {
        char label[4]; std::snprintf(label, sizeof label, "%02d", 1 + group * 16);
        ui::text(16 + group * 48, 200, label, group == page ? m::spot() : m::ghost());
    }
}
const char* lock_module_name(UnitKind kind) {
    // Six 62px slots, including index and bypass suffix. Never shrink/clip names
    // opportunistically: repeated modules keep a stable, distinguishable address.
    switch (kind) {
        case UnitKind::Empty: return "--";
        case UnitKind::SamplePlayer: return "play";
        case UnitKind::Grain: return "gran";
        case UnitKind::Filter: return "filter";
        case UnitKind::Bitcrush: return "bit";
        case UnitKind::Chorus: return "chorus";
        case UnitKind::Reverb: return "rev";
        case UnitKind::Stranular: return m::module_name(kind);
        case UnitKind::Recorder: return "rec";
        case UnitKind::Strain: return "strain";
        default: return m::module_name(kind);
    }
}
void draw_lock_inspector(const SequencerInspector& d) {
    // Only the frozen controller DTO is read here, including all group ranges.
    // Neither a profile, module graph nor live effective value shares this table.
    for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        const auto& module = d.modules[s];
        const int x = 14 + s * 62;
        char label[16];
        std::snprintf(label, sizeof label, "%02d %s%s", s + 1, lock_module_name(module.kind), module.bypass ? "~" : "");
        ui::text(x, 37, label, s == d.slot ? m::spot() : module.kind == UnitKind::Empty || module.bypass ? m::ghost() : m::ink());
        if (s == d.slot) ui::line(x, 48, x + 59, 48, m::spot());
    }
    const bool valid = d.slot >= 0 && d.slot < UNITS_PER_CHAIN && d.param >= 0 &&
        d.param < std::min(PARAM_BANK_SIZE, d.modules[d.slot].count);
    int first_step = -1, last_step = -1, paste_first = -1, paste_last = -1, paste_count = 0;
    for (int s = 0; s < SEQ_STEPS; ++s) {
        if (d.selection & (uint64_t(1) << s)) { if (first_step < 0) first_step = s; last_step = s; }
        if (d.paste_mask & (uint64_t(1) << s)) { if (paste_first < 0) paste_first = s; paste_last = s; ++paste_count; }
    }
    char text[80];
    if (first_step >= 0) std::snprintf(text, sizeof text, "lock ch%02d / s%02d / group %d (%02d-%02d)", d.chain + 1, d.step + 1, d.selected_count, first_step + 1, last_step + 1);
    else std::snprintf(text, sizeof text, "lock ch%02d / step %02d / single", d.chain + 1, d.step + 1);
    ui::text(14, 55, text, m::spot());
    const int count = valid ? std::min(PARAM_BANK_SIZE, d.modules[d.slot].count) : 0;
    const int first = valid ? (d.param / 6) * 6 : 0;
    std::snprintf(text, sizeof text, "%d-%d / %d", count ? first + 1 : 0, std::min(first + 6, count), count);
    ui::text(386 - ui::text_w(text), 55, text, m::ghost());
    const char* copy_scope = d.copy_scope == 1 ? "locks" : d.copy_scope == 2 ? "param" : "steps";
    const char* scope = d.clipboard_scope == 1 ? "locks" : d.clipboard_scope == 2 ? "param" : "steps";
    std::snprintf(text, sizeof text, "touch %s / copy %s / clip %s %d / undo%c redo%c", d.offset ? "offset" : "set", copy_scope, scope, d.clipboard_count, d.can_undo ? '+' : '-', d.can_redo ? '+' : '-');
    ui::text(14, 69, text, m::ghost());
    ui::text(18, 83, "parameter", m::ghost());
    ui::text(118, 83, "stored locks", m::ghost());
    ui::text(356, 83, "cover", m::ghost());
    ui::line(14, 93, 386, 93, m::branch());
    if (!valid) {
        ui::text(18, 110, "no module parameters / no lock target", m::ghost());
    } else {
        const auto& module = d.modules[d.slot];
        for (int p = first; p < std::min(first + 6, count); ++p) {
            const auto& row = d.rows[p];
            const bool selected = p == d.param;
            const int y = 98 + (p - first) * 13;
            if (selected) ui::rect(14, y - 2, 372, 12, m::spot());
            const u32 ink = selected ? palette::inverse() : m::ink();
            const u32 muted = selected ? palette::inverse() : m::ghost();
            ui::text(18, y, module.params[p].name, ink);
            char low[40], high[40], range[88];
            if (!row.covered) std::snprintf(range, sizeof range, "base");
            else {
                seq_lock::format_value(module.kind, module.variant, p, row.minimum, low, sizeof low);
                seq_lock::format_value(module.kind, module.variant, p, row.maximum, high, sizeof high);
                if (row.minimum == row.maximum) std::snprintf(range, sizeof range, "%s", low);
                else std::snprintf(range, sizeof range, "%s .. %s", low, high);
                // Never truncate a number or let it hide coverage. Equal rounded
                // labels can conceal a genuine raw range; expose exact q15 then.
                if (ui::text_w(range) > 228 || (row.minimum != row.maximum && !std::strcmp(low, high)))
                    std::snprintf(range, sizeof range, "q15 %d .. %d", int(row.minimum), int(row.maximum));
            }
            ui::text(118, y, range, row.covered ? ink : muted);
            char coverage[20]; std::snprintf(coverage, sizeof coverage, "%u/%d", unsigned(row.covered), std::max(1, std::min(64, d.selected_count)));
            ui::text(382 - ui::text_w(coverage), y, coverage, muted);
        }
        ui::line(14, 175, 386, 175, m::branch());
        const auto& row = d.rows[d.param];
        const auto& target = module.params[d.param];
        char value[40]; seq_lock::format_value(module.kind, module.variant, d.param, row.focused, value, sizeof value);
        std::snprintf(text, sizeof text, "s%02d %s %s / q15 %d", d.step + 1, row.focused_locked ? "lock" : "base", value, int(row.focused));
        ui::text(14, 181, text, m::ink());
        seq_lock::format_value(module.kind, module.variant, d.param, target.base, value, sizeof value);
        std::snprintf(text, sizeof text, "base %s / %02d %s / %.8s", value, d.slot + 1, lock_module_name(module.kind), target.name);
        ui::text(14, 194, text, m::ghost());
    }
    if (d.status[0]) ui::text(14, 207, d.status, m::spot());
    else if (paste_count) {
        std::snprintf(text, sizeof text, "paste %d -> %02d-%02d%s / %s", paste_count, paste_first + 1, paste_last + 1, paste_count != paste_last - paste_first + 1 ? " sparse" : "", scope);
        ui::text(14, 207, text, m::ghost());
    } else if (valid && d.modules[d.slot].kind == UnitKind::Comp && d.param == CompUnit::P_RELEASE) {
        ui::text(14, 207, "f free ms / s sync tau / clock per stored step", m::ghost());
    } else if (valid && d.modules[d.slot].kind == UnitKind::Fract && d.param == FractUnit::P_SIZE) {
        ui::text(14, 207, "f free ms / s nominal sync / 1s capture limit", m::ghost());
    } else ui::text(14, 207, "stored only / base is not live modulation", m::ghost());
    if (d.show_l_help) {
        ui::text(14, 219, "l: <> range / ^v group values / tap group", m::spot());
        ui::text(14, 231, "l+x copy / l+a paste / l+y undo / l+b deselect", m::ink());
    } else {
        ui::text(14, 219, "cpad <> module / ^v param / dpad steps / ~=bypass", m::ghost());
        ui::text(14, 231, "a+ b- / y listen / l tools / r mode", m::ghost());
    }
}
void draw_l_help(int mode) {
    // Other modes keep their profile/help footprint. LOCK retains its complete
    // table and uses only the two footer lines for the same shoulder grammar.
    (void)mode;
    ui::line(198, 54, 198, 202, m::branch());
    ui::text(212, 54, "l / hold shortcuts", m::spot());
    ui::line(212, 68, 386, 68, m::branch());
    auto row = [](int y, const char* key, const char* action) {
        ui::text(212, y, key, m::spot()); ui::text(272, y, action, m::ink());
    };
    row(76, "< >", "range");
    row(91, "^ v", "group value");
    row(106, "x", "copy");
    row(121, "a", "paste");
    row(136, "y", "undo");
    row(151, "b", "deselect");
    row(166, "touch", "group");
    row(186, "select", "go to");
}
void draw_play_window(const SequencerInspector::Play& play) {
    char text[40];
    if (!play.valid) {
        ui::text(208, 174, "play / no sample", m::ghost()); return;
    }
    std::snprintf(text, sizeof text, "s%02d / %s %s", play.sample, play.increment < 0 ? "rev" : "fwd",
                  play.loop ? "loop" : "once");
    ui::text(208, 167, text, m::ink());
    const int a = 212 + (int)play.start * 173 / 32767;
    const int b = 212 + (int)play.end * 173 / 32767;
    ui::line(212, 184, 385, 184, m::branch(), 1);
    ui::line(a, 184, b, 184, m::spot(), 1);
    ui::line(a, 180, a, 188, m::spot(), 1);
    ui::line(b, 180, b, 188, m::spot(), 1);
    // Window/slice projection uses stored locks + bases, never a fake live cursor.
    if (play.milliseconds < 1000)
        std::snprintf(text, sizeof text, "~%lums / pass%s", (unsigned long)play.milliseconds,
                      play.slice ? " / slice" : "");
    else
        std::snprintf(text, sizeof text, "~%lu.%02lus / pass%s", (unsigned long)(play.milliseconds / 1000),
                      (unsigned long)(play.milliseconds % 1000 / 10), play.slice ? " / slice" : "");
    ui::text(208, 197, text, m::ghost());
    ui::text(208, 208, play.modulated ? "stored / mod may change" : "stored window / not gate", m::ghost());
}
void draw_sequence_profile(const SequencerInspector& d, int mode) {
    const auto profile = seq_profile::capture(sequencer_snapshot(), d, mode);
    const int c = d.chain;
    if (c < 0 || c >= NUM_CHAINS) return;
    const auto kind = (UnitKind)sequencer_snapshot().sequence_pitch_kind[c];
    const bool notes=sequence_note::absolute(kind,sequencer_snapshot().sequence_pitch_param[c]);
    const bool speed = kind == UnitKind::SamplePlayer && !notes;
    const char* title = mode == 1 ? (notes?"notes":speed ? "tape speed" : "transpose") : mode == 2 ? "level" :
                        mode == 3 ? "retrig" : mode == 4 ? "when" : mode == 5 ? "time" : "events";
    char range[32]; std::snprintf(range, sizeof range, "%02d-%02d", profile.first + 1, profile.first + 16);
    ui::text(208, 67, title, m::ink());
    ui::text(390 - ui::text_w(range), 67, range, m::ghost());
    if (!profile.available) {
        ui::text(208, 102, mode == 1 ? "no pitch parameter" : "no level parameter", m::ghost());
    } else {
        const int base_y = profile.base_y;
        for (int x = 212; x <= 385; x += 5) ui::rect(x, base_y, 2, 1, m::branch());
        for (int i = 0; i < 16; ++i) {
            const auto& p = profile.points[i];
            const bool selected = i == profile.selected;
            const u32 color = !p.active ? m::branch() : selected ? m::spot() : m::ink();
            if (profile.pitch || profile.level) {
                // Piecewise-constant stored steps, NOT an interpolated glide or
                // a prediction that a conditional lock will necessarily fire.
                const u32 value_color = !p.active ? m::branch() : p.locked ? color : m::ghost();
                ui::line(p.x, p.y, p.x + 8, p.y, value_color, 1);
                if (p.locked) {
                    if (p.conditional) ui::frame(p.x + 2, p.y - 2, 5, 5, value_color);
                    else ui::rect(p.x + 3, p.y - 1, 3, 3, value_color);
                }
                if (i && p.active && profile.points[i - 1].active)
                    ui::line(p.x - 2, profile.points[i - 1].y, p.x - 2, p.y, m::branch(), 1);
            } else if (mode == 4) {
                const auto meta = sequencer_snapshot().sequence_meta[c][profile.first + i];
                const auto condition = (SeqStep::Condition)((meta & SeqStep::ConditionMask) >> SeqStep::ConditionShift);
                if (condition == SeqStep::Condition::FirstOfTwo || condition == SeqStep::Condition::SecondOfTwo) {
                    ui::rect(p.x + 1, 103, 3, 3, condition == SeqStep::Condition::FirstOfTwo ? color : m::branch());
                    ui::rect(p.x + 6, 114, 3, 3, condition == SeqStep::Condition::SecondOfTwo ? color : m::branch());
                    ui::line(p.x + 3, 114, p.x + 6, 107, color, 1);
                } else if (condition == SeqStep::Condition::Fill || condition == SeqStep::Condition::NotFill) {
                    ui::text(p.x + 2, 107, condition == SeqStep::Condition::Fill ? "f" : "!", color);
                } else {
                    const int pct = condition == SeqStep::Condition::Half ? 50 : condition == SeqStep::Condition::Quarter ? 25 :
                                    condition == SeqStep::Condition::ThreeQuarter ? 75 : 100;
                    ui::line(p.x + 4, 131, p.x + 4, 131 - pct * 38 / 100, color, 2);
                }
            } else {
                if (mode == 0) {
                    if (p.trigger) ui::rect(p.x + 2, 105, 5, 5, color);
                    else if ((sequencer_snapshot().sequence_meta[c][profile.first + i] >> 6) & 7)
                        ui::frame(p.x + 2, 105, 5, 5, color);
                    else ui::rect(p.x + 4, 108, 1, 1, m::ghost());
                } else {
                    ui::line(p.x + 4, base_y, p.x + 4, p.y, color, 1);
                    ui::rect(p.x + 2, p.y - 1, 5, 2, color);
                }
            }
            if (selected) ui::line(p.x + 1, 142, p.x + 7, 142, m::spot(), 1);
            if (i == profile.playhead) ui::rect(p.x + 3, 148, 3, 3, palette::playhead());
        }
    }
    if (d.play.present) { draw_play_window(d.play); return; }
    if (!profile.available) {
        ui::text(208, 167, "source has no target", m::ghost());
        ui::text(208, 197, "l / button shortcuts", m::ghost());
    } else if (mode == 1 || mode == 2) {
        ui::text(208, 167, "dot lock / pale base", m::ghost());
        ui::text(208, 182, "hollow = conditional", m::ghost());
        ui::text(208, 197, "one step / then base", m::ghost());
        if (mode == 1 && profile.available)
            ui::text(208, 206, "note mode / edit to hear", m::spot());
    } else {
        ui::text(208, 167, mode == 4 ? "stored / not outcome" : "selected track / stored", m::ghost());
        ui::text(208, 182, "line select / dot play", m::ghost());
        ui::text(208, 197, "l / button shortcuts", m::ghost());
    }
}
void draw_sequencer_slices(const SequencerInspector& inspector) {
    const auto& d = sequencer_slices();
    const bool cuts=sequencer_cuts().active;
    if (d.chain < 0 || d.chain >= NUM_CHAINS) return;
    const auto& row = d.rows[d.chain];
    const int count = seq_slice::count(row.valid);
    const uint16_t meta = sequencer_snapshot().sequence_meta[d.chain][d.step];
    ui::text(24, 60, "slice", m::spot());
    if (row.slot < 0 || !d.loaded) {
        ui::text(24, 87, row.slot < 0 ? "no play source" : "no sample", m::ink(), 2);
        ui::text(24, 122, row.slot < 0 ? "select a chain whose source is play" :
                 "load a sample in play", m::ghost());
        ui::text(24, 144, "slice > cuts / edit boundaries", m::ghost());
        m::footer(400, "cuts shared / locks per step", "r mode");
        return;
    }
    char label[64];
    if (d.applied) {
        std::snprintf(label, sizeof label, "%02u", d.applied);
        inspector_gothic::draw_numeric(24, 113, label, m::ink(), m::spot());
    } else {
        ui::text(24, 84, "--", m::ink(), 3);
        ui::text(24, 111, "window", m::ghost());
    }
    std::snprintf(label, sizeof label, "s%02d / %d cuts / %s", d.sample, count, cuts ? "preview" : d.locked ? "step lock" : "base");
    ui::text(118, 65, label, m::ink());
    if (!inspector.show_l_help) {
    const char* state = sequencer_snapshot().chain_mute[d.chain] ? "muted" :
        d.step >= sequencer_snapshot().sequence_length[d.chain] ? "inactive" : (meta & SeqStep::Trig) ? "on" : "off";
    const auto condition = (SeqStep::Condition)((meta & SeqStep::ConditionMask) >> SeqStep::ConditionShift);
    std::snprintf(label, sizeof label, "step %02d / %s / %s", d.step + 1, state, top_condition_human(condition));
    ui::text(118, 81, cuts ? "shared sample / stable cut ids" : label, m::ghost());
    if (d.milliseconds < 1000)
        std::snprintf(label, sizeof label, "%s / %s / ~%lums per pass", d.reverse ? "rev" : "fwd",
            d.loop ? "loop" : "once", (unsigned long)d.milliseconds);
    else
        std::snprintf(label, sizeof label, "%s / %s / ~%lu.%02lus per pass", d.reverse ? "rev" : "fwd",
            d.loop ? "loop" : "once", (unsigned long)(d.milliseconds / 1000),
            (unsigned long)(d.milliseconds % 1000 / 10));
    ui::text(118, 97, d.window_valid ? label : "no playable window", m::ghost());
    if (d.requested && !d.applied) {
        std::snprintf(label, sizeof label, "slice %02u missing / window fallback", d.requested);
        ui::text(118, 113, label, m::spot());
    } else ui::text(118, 113, !count ? "open cuts / add or auto divide" :
                   d.modulated ? "stored / modulation may change it" : "stored window / not a live playhead", m::ghost());
    }

    constexpr int x0 = 24, y0 = 160;
    auto wave_x = [](uint16_t n) { return x0 + (int)n * (seq_slice::WAVE_COLUMNS - 1) / 32767; };
    const int begin = wave_x(d.window_begin), end = wave_x(d.window_end);
    // Delicate guides share half-open boundaries with the selected interval.
    ui::rect(x0,y0,352,1,m::branch());
    for (int i = 0; i < seq_slice::WAVE_COLUMNS; ++i) {
        const int x = x0 + i;
        const int a = y0 - (int)d.high[i] * 27 / 128;
        const int b = y0 - (int)d.low[i] * 27 / 128;
        const bool selected = d.window_valid && x >= begin && x < end;
        ui::rect(x, a, 1, b - a + 1, selected ? m::spot() : m::ink());
    }
    for (int i=0;i<16;++i) {
        if(!(row.valid&(1u<<i)))continue;
        ui::rect(wave_x(d.begin[i]),132,1,57,m::branch());
    }
    int label_left[16] = {}, label_right[16] = {}, labels = 0;
    for (int i = 0; i < 16; ++i) {
        if (!(row.valid & (1u << i))) continue;
        const int a = wave_x(d.begin[i]), b = wave_x(d.end[i]);
        const bool selected = d.applied == i + 1;
        ui::line(a, 190, b, 190, selected ? m::spot() : m::branch(), 1);
        // Narrow/duplicate cuts get a notch, never overlapping numbers or
        // renumbered IDs. Even unsorted legacy tables must have disjoint labels.
        const int left = (a + b) / 2 - 6, right = left + 12;
        bool fits = b - a >= 15;
        for (int n = 0; n < labels; ++n)
            if (left < label_right[n] + 2 && right + 2 > label_left[n]) fits = false;
        if (fits) {
            label_left[labels] = left; label_right[labels++] = right;
            std::snprintf(label, sizeof label, "%02d", i + 1);
            ui::text((a + b) / 2 - ui::text_w(label) / 2, 196, label, selected ? m::spot() : m::ghost());
            if (d.reversed & (1u << i)) {
                const int mid = (a + b) / 2;
                ui::line(mid + 3, 210, mid - 3, 210, m::ghost(), 1);
                ui::line(mid - 3, 210, mid, 207, m::ghost(), 1);
            }
        }
    }
    if (d.window_valid) slice_boundaries::selected(begin,end,129,188,m::spot());
    std::snprintf(label,sizeof label,"wave x%u.%u / visual only",d.visual_gain_q8/256,d.visual_gain_q8%256*10/256);
    ui::text(24,214,label,m::ghost());
    // Keep the wave visible while holding L. Slice-specific help replaces only
    // the metadata block; old seven modes retain their original helper surface.
    if (inspector.show_l_help) {
        if (cuts) {
            ui::text(118, 81, "<> cut / a audition", m::spot());
            ui::text(118, 97, "b return to score", m::ink());
        } else {
            ui::text(118, 81, "l: <> range / ^v value / tap group", m::spot());
            ui::text(118, 97, "l+x copy / l+a paste", m::ink());
            ui::text(118, 113, "l+y undo / l+b deselect", m::ink());
        }
    }
    m::footer(400, cuts ? "shared cuts / pattern untouched" : "a next / l value / cuts to edit", cuts ? "b score" : "-- window");
}
void draw_sequencer_top() {
    const int chain = sequencer_selected_chain();
    const int step = sequencer_selected_step();
    const uint16_t meta = sequencer_snapshot().sequence_meta[chain][step];
    const uint8_t flags = (uint8_t)(meta & 0x3fu);
    const int locks = (meta >> 6) & 7;
    const int timing = (int)((meta >> 9) & 0x3f) - SEQ_MICROTIMING_MAX;
    const int retrig = ((flags & SeqStep::RetrigMask) >> SeqStep::RetrigShift) + 1;
    const auto condition = (SeqStep::Condition)((flags & SeqStep::ConditionMask) >> SeqStep::ConditionShift);
    const bool trig = (flags & SeqStep::Trig) != 0;
    char pitch[12]; sequencer_selected_pitch_text(pitch, sizeof pitch);
    int value = sequencer_snapshot().sequence_value[chain][step];
    if (value < 0) value = -value;
    if (value > 32767) value = 32767;
    const int level = (value * 100 + 16383) / 32767;
    const auto& inspector = sequencer_inspector();
    const int layer = sequencer_selected_layer();
    const int length = sequencer_snapshot().sequence_length[chain];
    const SequenceDirection direction = (SequenceDirection)sequencer_snapshot().sequence_direction[chain];
    const char* direction_name = direction == SequenceDirection::Reverse ? "reverse" :
                                 direction == SequenceDirection::PingPong ? "ping-pong" : "forward";

    m::paper(400);
    char bank[32];
    const auto& transition=sequencer_snapshot().pattern_transition;
    if(transition.active)std::snprintf(bank,sizeof bank,"edit %d / %d>%d %d%%",sequencer_edit_bank()+1,
        transition.source+1,transition.destination+1,transition::position_percent(transition.position));
    else std::snprintf(bank,sizeof bank,"edit %d / play %d%s",sequencer_edit_bank()+1,
        sequencer_snapshot().active_pattern+1,project_is_dirty()?" *":"");
    ui::text(24,16,bank,m::spot());
    const unsigned beats_per_bar = sequencer_snapshot().beats_per_bar ? sequencer_snapshot().beats_per_bar : 4;
    char transport[64]; std::snprintf(transport, sizeof transport, "%d bpm / %lu.%lu / %s",
        sequencer_snapshot().bpm,
        (unsigned long)(sequencer_snapshot().beat_count / beats_per_bar + 1),
        (unsigned long)(sequencer_snapshot().beat_count % beats_per_bar + 1),
        sequencer_snapshot().transport_playing ? "play" : "stop");
    ui::text(376 - ui::text_w(transport), 16, transport, m::ghost());

    if (layer == 6) {
        draw_lock_inspector(inspector);
        return;
    }
    draw_sequencer_path(inspector, layer);
    if (layer == seq_slice::MODE) {
        draw_sequencer_slices(inspector);
        return;
    }

    const UnitKind pitch_kind = (UnitKind)sequencer_snapshot().sequence_pitch_kind[chain];
    const bool melodic = sequence_note::absolute(pitch_kind,sequencer_snapshot().sequence_pitch_param[chain]);
    if (melodic) {
        if (!note_display::melodic(pitch, m::ink(), m::spot(), 14, 18))
            ui::text(24, 77, pitch, m::ink(), 3);
        const q15 pitch_value = sequencer_snapshot().sequence_pitch[chain][step];
        const int midi = sequence_note::midi(pitch_kind,pitch_value);
        char midi_text[16]; std::snprintf(midi_text, sizeof midi_text, "midi %d", midi);
        ui::text(24, 133, midi_text, m::ghost());
    } else if (sequencer_snapshot().sequence_pitch_slot[chain] >= 0) {
        if (!note_display::semitones(pitch, m::ink(), m::spot(), 14, 18))
            ui::text(24, 77, pitch, m::ink(), 3);
        ui::text(24, 118, pitch_kind == UnitKind::Stranular ? "semitones" : "st / r reverse", m::ghost());
    } else {
        ui::text(24, 82, top_sequence_source_name(chain), m::ink(), 2);
    }
    ui::text(24, 60, sequencer_selected_layer_name(), m::spot());
    char info[32];
    std::snprintf(info, sizeof info, "%02d/%02d %s", step + 1, length, trig ? "on" : "off");
    ui::text(112, 76, info, m::spot());
    std::snprintf(info, sizeof info, "lev %02d x%d", level, retrig);
    ui::text(112, 92, info, layer == 2 || layer == 3 ? m::spot() : m::ghost());
    ui::text(112, 108, top_condition_human(condition), layer == 4 ? m::spot() : m::ghost());
    std::snprintf(info, sizeof info, "%+d/96", timing);
    ui::text(112, 124, info, layer == 5 ? m::spot() : m::ghost());
    std::snprintf(info, sizeof info, "locks %d/%d", locks, SEQ_LOCKS);
    ui::text(112, 140, info, layer == 6 ? m::spot() : m::ghost());
    {
        if (inspector.show_l_help) draw_l_help(layer);
        else draw_sequence_profile(inspector, layer);
        const int slot = sequencer_snapshot().sequence_value_slot[chain];
        const int param = sequencer_snapshot().sequence_value_param[chain];
        char target[48];
        if (slot >= 0 && slot < UNITS_PER_CHAIN && param >= 0 && param < inspector.modules[slot].count)
            std::snprintf(target, sizeof target, "level > %d %s / %s", slot + 1,
                          short_unit_name(inspector.modules[slot].kind), inspector.modules[slot].params[param].name);
        else std::snprintf(target, sizeof target, "level > no target");
        ui::text(8, 150, target, layer == 2 ? m::spot() : m::ghost());
        const auto lifecycle = (SequenceLifecycle)sequencer_snapshot().sequence_lifecycle[chain];
        const char* life = lifecycle == SequenceLifecycle::Flow ? "flow" : lifecycle == SequenceLifecycle::Gate ? "gate" :
                           lifecycle == SequenceLifecycle::Loop ? "loop" : "strike";
        const char* state = sequencer_snapshot().chain_mute[chain] ? " / muted" : step >= length ? " / inactive" : "";
        std::snprintf(target, sizeof target, "%s / %s%s", life, direction_name, state);
        ui::text(8, 167, target, m::ghost());
        draw_sequencer_extent();
        ui::text(8, 224, layer == 1 ? "a next note / l range+value / slide to hear" : "l+dpad range/value / r mode", m::ghost());
        ui::text(392 - ui::text_w(sequencer_selected_layer_name()), 224, sequencer_selected_layer_name(), m::spot());
    }
    if (sequencer_snapshot().xrun_count) {
        char alert[24]; std::snprintf(alert, sizeof alert, "xrun %lu", (unsigned long)sequencer_snapshot().xrun_count);
        ui::text(392 - ui::text_w(alert), 205, alert, m::spot());
    }
}

void draw_performance_top() {
    if(!transition::active()){performance_view_draw_top();return;}
    m::paper(400);m::marker(9,10,m::spot());
    char text[64];
    if(g_ui_snapshot.pending_pattern<NUM_PATTERNS)std::snprintf(text,sizeof text,"banks / play %d / queued %d",g_ui_snapshot.active_pattern+1,g_ui_snapshot.pending_pattern+1);
    else std::snprintf(text,sizeof text,"banks / play %d",g_ui_snapshot.active_pattern+1);
    ui::text(19,7,text,m::ink());
    std::snprintf(text,sizeof text,"%d bpm / %s",g_ui_snapshot.bpm,g_ui_snapshot.transport_playing?"play":"stop");
    ui::text(391-ui::text_w(text),7,text,m::ghost());ui::line(8,23,392,23,m::branch());
    for(int c=0;c<NUM_CHAINS;++c) {
        const int y=33+c*41;
        const bool selected=c==g_sel_chain,muted=g_ui_snapshot.chain_mute[c];
        const u32 ink=muted?m::ghost():m::ink();
        char number[2]={(char)('1'+c),0};
        inspector_gothic::draw_numeric(15,y+32,number,selected?m::spot():ink,m::spot());
        if(selected)ui::line(8,y,8,y+31,m::spot(),2);
        char route[48];snapshot_route(c,route,sizeof route);
        ui::text(53,y,route[0]?route:"empty",ink);
        const auto direction=(SequenceDirection)g_ui_snapshot.sequence_direction[c];
        const char* mode=direction==SequenceDirection::Reverse?"reverse":direction==SequenceDirection::PingPong?"ping pong":"forward";
        const auto& transition=g_ui_snapshot.pattern_transition;
        if(transition.active)std::snprintf(text,sizeof text,"bank %d>%d / %d%% / %s",transition.source+1,
            transition.destination+1,transition::position_percent(transition.position),muted?"mute":"blend");
        else std::snprintf(text,sizeof text,"%02d/%02d %s / %s",g_ui_snapshot.sequence_step[c]+1,g_ui_snapshot.sequence_length[c],mode,muted?"mute":"live");
        ui::text(53,y+12,text,muted?m::spot():m::ghost());
        int peak=(int)((int32_t)g_ui_snapshot.chain_peak[c]*80/32767);if(peak<0)peak=0;if(peak>80)peak=80;
        ui::line(308,y+15,388,y+15,m::branch());if(!muted&&peak)ui::rect(308,y+13,peak,4,m::spot());
        // Fixed 64-step ruler: end post never follows the selected editor page.
        const int length=std::max(1,std::min(64,(int)g_ui_snapshot.sequence_length[c]));
        for(int s=0;s<64;++s)ui::rect(54+s*5,y+28,2,s<length?2:1,s<length?m::ghost():m::branch());
        for(int s=16;s<64;s+=16)ui::line(52+s*5,y+25,52+s*5,y+32,m::branch());
        ui::line(52+length*5,y+25,52+length*5,y+32,m::spot());
        if(g_ui_snapshot.sequencer_enabled&&g_ui_snapshot.transport_playing&&!muted&&g_ui_snapshot.sequence_step[c]<64) {
            const int head=54+std::min(63,(int)g_ui_snapshot.sequence_step[c])*5;
            ui::rect(head-1,y+24,4,7,m::spot());
        }
        ui::line(53,y+37,392,y+37,m::branch());
    }
    ui::text(10,205,g_ui_snapshot.pattern_transition.active?"runtime blend / keep at an end before launch":
        g_ui_snapshot.pending_pattern<NUM_PATTERNS?"queued launch / cancel in banks":"swipe destination / tap launch below",m::ink());
    ui::text(10,217,"track length + direction: sequencer hold b",m::ghost());
    if(transition::active())m::footer(400,"banks: rhythm / a,b: separate sound","l return");
    else m::footer(400,"tap number: trigger / x: mute","<> chain");
}

void draw_patch_table_top() {
    const u32 paper = palette::bg();
    const u32 paper_hi = palette::cell();
    const u32 ink = palette::ink();
    const u32 spot = palette::spot();
    const u32 ghost = palette::ghost();

    ui::rect(0, 0, 400, 240, paper);
    ui::rect(0, 0, 400, 20, paper_hi);
    ui::line(0, 19, 399, 19, ink, 1);
    ui::rect(5, 5, 3, 10, spot);
    ui::text(12, 6, "patch table", ink);
    char transport[48];
    std::snprintf(transport, sizeof transport, "p.%03d %s %03d bpm %s",
                  g_patch_slot + 1, g_ui_snapshot.transport_playing ? ">" : "|",
                  g_ui_snapshot.bpm, project_is_dirty() ? "unsaved" : "saved");
    ui::text(395 - ui::text_w(transport), 6, transport,
             project_is_dirty() ? spot : ink);

    ui::text(8, 26, "chains", ghost);
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const int x = 50 + chain * 31;
        char label[4]; std::snprintf(label, sizeof label, "%02d", chain + 1);
        if (chain == g_sel_chain) {
            ui::rect(x, 22, 24, 13, ink);
            ui::text(x + 6, 25, label, paper_hi);
        } else {
            ui::line(x, 34, x + 23, 34, ghost, 1);
            ui::text(x + 6, 25, label, ghost);
        }
    }

    ui::text(8, 43, "workspace overview", ink);
    constexpr int ox = 8, oy = 51, ow = 232, oh = 153;
    ui::frame(ox, oy, ow, oh, ghost);
    int previous = -1;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        if ((UnitKind)g_ui_snapshot.unit_kind[g_sel_chain][slot] == UnitKind::Empty) continue;
        const WorkspacePoint& point = workspace_point(g_sel_chain, slot);
        const int x = ox + point.x * (ow - 24) / WORKSPACE_W;
        const int y = oy + point.y * (oh - 18) / WORKSPACE_H;
        if (previous >= 0) {
            const WorkspacePoint& pp = workspace_point(g_sel_chain, previous);
            const int px = ox + pp.x * (ow - 24) / WORKSPACE_W + 20;
            const int py = oy + pp.y * (oh - 18) / WORKSPACE_H + 7;
            ui::line(px, py, x, y + 7, ink, 1);
        }
        previous = slot;
    }
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        const UnitKind kind = (UnitKind)g_ui_snapshot.unit_kind[g_sel_chain][slot];
        if (kind == UnitKind::Empty) continue;
        const WorkspacePoint& point = workspace_point(g_sel_chain, slot);
        const int x = ox + point.x * (ow - 24) / WORKSPACE_W;
        const int y = oy + point.y * (oh - 18) / WORKSPACE_H;
        ui::rect(x, y, 22, 15, paper_hi);
        ui::frame(x, y, 22, 15, slot == g_sel_slot ? spot : ink);
        ui::text(x + 2, y + 4, short_unit_name(kind), slot == g_sel_slot ? spot : ink);
    }

    const int vx = ox + workspace_camera_x(g_sel_chain) * (ow - 24) / WORKSPACE_W;
    const int vy = oy + workspace_camera_y(g_sel_chain) * (oh - 18) / WORKSPACE_H;
    const int vw = 320 * (ow - 24) / WORKSPACE_W;
    const int vh = WORKSPACE_VIEW_H * (oh - 18) / WORKSPACE_H;
    ui::frame(vx, vy, vw, vh, spot);
    ui::rect(vx + 2, vy + 2, 3, 3, spot);
    ui::text(8, 208, "lower viewport", spot);

    ui::line(249, 22, 249, 224, ghost, 1);
    ui::text(260, 29, "selected", ghost);
    const UnitKind selected_kind = (UnitKind)g_ui_snapshot.unit_kind[g_sel_chain][g_sel_slot];
    char selected[32];
    std::snprintf(selected, sizeof selected, "%s / slot %d", short_unit_name(selected_kind), g_sel_slot + 1);
    ui::text(260, 43, selected, ink);
    const q15 effect = g_ui_snapshot.unit_mod_effect[g_sel_chain][g_sel_slot];
    char modulation[32];
    std::snprintf(modulation, sizeof modulation, "mod effect %+d",
                  (int)((int32_t)effect * 99 / 32767));
    ui::text(260, 56, modulation, effect ? spot : ghost);

    ui::line(260, 128, 390, 128, ghost, 1);
    int previous_y = 101;
    for (int i = 0; i < Engine::SCOPE_LEN; i += 2) {
        const q15 value = g_ui_snapshot.scope[(g_ui_snapshot.scope_pos + i) % Engine::SCOPE_LEN];
        const int x = 260 + i * 130 / Engine::SCOPE_LEN;
        const int y = 101 - (int)((int32_t)value * 27 / 32767);
        if (i) ui::line(x - 1, previous_y, x, y, spot, 1);
        previous_y = y;
    }
    ui::text(260, 137, "live audio", ghost);
    int meter_h = (int)((int32_t)g_ui_snapshot.chain_peak[g_sel_chain] * 48 / 32767);
    if (meter_h < 0) meter_h = 0;
    if (meter_h > 48) meter_h = 48;
    ui::frame(382, 78, 6, 52, ink);
    if (meter_h > 0) ui::rect(384, 128 - meter_h, 2, meter_h, spot);

    ui::line(260, 158, 390, 158, ink, 1);
    char route[64] = {};
    int cursor = 0;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        const UnitKind kind = (UnitKind)g_ui_snapshot.unit_kind[g_sel_chain][slot];
        if (kind == UnitKind::Empty) continue;
        const char* name = short_unit_name(kind);
        if (cursor && cursor < (int)sizeof(route) - 2) route[cursor++] = '>';
        for (const char* p = name; *p && cursor < (int)sizeof(route) - 1; ++p) route[cursor++] = *p;
    }
    ui::text(260, 166, route[0] ? route : "empty chain", ink);
    char mix[40];
    std::snprintf(mix, sizeof mix, "vol %02d pan %+03d",
        (int)((int32_t)g_ui_snapshot.chain_volume[g_sel_chain] * 99 / 32767),
        (int)((int32_t)g_ui_snapshot.chain_pan[g_sel_chain] * 99 / 32767));
    ui::text(260, 181, mix, ghost);
    ui::text(260, 196, g_ui_snapshot.chain_mute[g_sel_chain] ? "chain muted" : "chain live",
             g_ui_snapshot.chain_mute[g_sel_chain] ? spot : ink);

    char load[32], peak[24];
    std::snprintf(load, sizeof load, "dsp %u.%u%%  xrun %lu",
                  (unsigned)(g_ui_snapshot.dsp_load_permille / 10),
                  (unsigned)(g_ui_snapshot.dsp_load_permille % 10),
                  (unsigned long)g_ui_snapshot.xrun_count);
    std::snprintf(peak, sizeof peak, "peak %luus",
                  (unsigned long)g_ui_snapshot.render_peak_us);
    ui::text(260, 207, load, g_ui_snapshot.xrun_count ? spot : ghost);
    ui::text(260, 218, peak, ghost);

    ui::rect(0, 232, 400, 8, ink);
    ui::text(5, 233, "pan empty space / hold node move / tap edit", paper_hi);
}

} // namespace

void top_draw() {
    if (g_view == View::Chains && g_overlay == Overlay::SampleBrowser) {
        sample_browser_draw_top();
        return;
    }
    if (g_overlay == Overlay::Editor && fm_context_draw_top()) return;
    if (g_view == View::Chains && g_overlay == Overlay::Editor && param_lens_draw_top()) return;
    if (g_overlay == Overlay::Editor && module_inspector_draw_top()) return;
    if (g_overlay == Overlay::Editor && stranular_focus_draw_top()) return;
    if (g_overlay == Overlay::Editor && reverb_focus_draw_top()) return;
    if (g_overlay == Overlay::Editor && ghost_focus_draw_top()) return;
    if (g_view == View::Chains) {
        if (g_overlay == Overlay::None || g_overlay == Overlay::Picker) chains_context_draw_top();
        else draw_patch_table_top(); // editor and other screens migrate in later stages
        return;
    }
    // Each surface gets its own context lens. A generic telemetry dashboard made
    // the two LCDs look like unrelated applications and hid the active workflow.
    switch (g_view) {
        case View::Sigils:      draw_sigils_top(); break;
        case View::Patchbay:    patchbay_draw_top(); break;
        case View::Sequencer:   draw_sequencer_top(); break;
        case View::Performance: draw_performance_top(); break;
        case View::Chains:      break;
        case View::Bounce:      break; // dedicated frozen page is dispatched by main
    }
}

} // namespace seance::platform::ui3ds
