#include "ui_module_focus_internal.h"
#include "module_footer_layout.h"
#include "module_frame_layout.h"
#include "editor_style.h"
#include "ui_state.h"
#include "ui_sample_library.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "small_button_motion.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include <cstdio>
#include <cstring>

namespace seance::platform::ui3ds {

namespace {
trackr::synth::SampleEditResult g_sample_result =
    trackr::synth::SampleEditResult::Ok;
int g_sample_result_frames = 0;

const char* sample_result_name(trackr::synth::SampleEditResult result) {
    using trackr::synth::SampleEditResult;
    switch (result) {
        case SampleEditResult::NoFreeSlot: return "no free slot";
        case SampleEditResult::MemoryBudget: return "sample memory full";
        case SampleEditResult::SharedSlot: return "slot is shared";
        case SampleEditResult::Busy: return "save/clear take first";
        case SampleEditResult::InvalidSlot: return "bad slot";
        default: return "";
    }
}
} // namespace

void module_focus_show_sample_result(trackr::synth::SampleEditResult result) {
    g_sample_result = result;
    g_sample_result_frames = result == trackr::synth::SampleEditResult::Ok ? 0 : 120;
}

const char* module_focus_sample_result_notice() {
    return g_sample_result_frames > 0 ? sample_result_name(g_sample_result) : nullptr;
}

void module_focus_tick() {
    if (g_sample_result_frames > 0) --g_sample_result_frames;
}

bool module_focus_touch_common_header(Unit& unit, int tx, int ty) {
    if ((unit.kind == UnitKind::SamplePlayer || unit.kind == UnitKind::Osc) &&
        hit(tx, ty, 284, 3, 32, 18)) {
        {
            platform::AudioOut::Guard guard(*g_audio);
            if (unit.kind == UnitKind::SamplePlayer) {
                auto& play = static_cast<PlayUnit&>(unit);
                play.pitch_quantize = !play.pitch_quantize;
            } else if (unit.kind == UnitKind::Osc) {
                auto& osc = static_cast<OscUnit&>(unit);
                osc.pitch_quantize = !osc.pitch_quantize;
            }
        }
        mark_project_mutated();
        return true;
    }
    if (hit(tx, ty, 200, 3, 40, 18)) {
        {
            platform::AudioOut::Guard guard(*g_audio);
            unit.bypass = !unit.bypass;
        }
        mark_project_mutated();
        return true;
    }
    const int sample_slot = sample_slot_for_unit(&unit);
    if (sample_slot < 0) return false;
    if (hit(tx, ty, 140, 3, 38, 18)) {
        // The browser owns source/folder/scroll/selection across editor visits.
        scan_sample_files();
        g_overlay = Overlay::SampleBrowser;
        return true;
    }
    if (unit.kind != UnitKind::SamplePlayer && hit(tx, ty, 244, 3, 38, 18)) {
        export_current_sample();
        return true;
    }
    if (unit.kind != UnitKind::Recorder && hit(tx, ty, 114, 3, 24, 18)) {
        platform::AudioOut::Guard guard(*g_audio);
        set_sample_slot_for_unit(&unit, sample_slot - 1);
        return true;
    }
    if (unit.kind != UnitKind::Recorder && hit(tx, ty, 180, 3, 18, 18)) {
        platform::AudioOut::Guard guard(*g_audio);
        set_sample_slot_for_unit(&unit, sample_slot + 1);
        return true;
    }
    return false;
}

void module_focus_draw_shell(const Unit& unit) {
    ui::rect(0, 0, 320, 240, paper());
    ui::rect(0, 0, 320, 22, paper_hi());
    ui::line(0, 21, 319, 21, ink(), 1);
    ui::rect(5, 5, 3, 11, spot());
    ui::text(12, 6, unit_kind_name(unit.kind), ink());
    char info[32];
    std::snprintf(info, sizeof info, "ch%02u / slot %02u",
                  (unsigned)(g_edit_chain + 1), (unsigned)(g_edit_slot + 1));
    ui::text(74, 7, info, ghost());
    paper_button(200, 3, 40, 18, "byp", unit.bypass);

    int sample_slot = sample_slot_for_unit(&unit);
    uint32_t frames = 0;
    const bool recorder = unit.kind == UnitKind::Recorder;
    if (recorder) {
        sample_slot = g_ui_snapshot.rec_slot[g_edit_chain][g_edit_slot];
        if (sample_slot == 0xff) sample_slot = -1;
        frames = g_ui_snapshot.rec_frames[g_edit_chain][g_edit_slot];
    }
    if (sample_slot >= 0) {
        // REC provenance/logical length are audio-worker state while a take is
        // active. Its shell is therefore rendered entirely from the published
        // snapshot; ordinary sample units retain their immutable bank metadata.
        if (!recorder) {
            const auto& provenance =
                trackr::synth::SampleBank::instance().provenance(sample_slot);
            if (provenance.missing) {
                const char* name = std::strrchr(provenance.path, '/');
                name = name ? name + 1 : provenance.path;
                char missing[44];
                std::snprintf(missing, sizeof missing, "missing %.30s", name);
                ui::text(180, 25, missing, spot());
            } else if (provenance.origin == trackr::synth::SampleOrigin::Recording) {
                ui::text(270, 25, "REC WAV", ink());
            }
        }
        paper_button(114, 3, 24, 18, "s-");
        char slot_label[12];
        std::snprintf(slot_label, sizeof slot_label, "S%02d", sample_slot);
        paper_button(140, 3, 38, 18, slot_label);
        paper_button(180, 3, 18, 18, "+");
        if (!recorder) frames = sample_slot_frames(sample_slot);
        char status[32];
        if (recorder) {
            const uint8_t chain_bit = (uint8_t)(1u << g_edit_chain);
            const bool recording = (g_ui_snapshot.rec_recording & chain_bit) != 0;
            const uint32_t recorded = g_ui_snapshot.rec_frames[g_edit_chain][g_edit_slot];
            const uint32_t capacity = g_ui_snapshot.rec_capacity[g_edit_chain][g_edit_slot];
            if (recording) {
                const unsigned percent = capacity
                    ? (unsigned)((uint64_t)recorded * 100 / capacity) : 0;
                std::snprintf(status, sizeof status, "REC %u%%", percent > 100 ? 100 : percent);
                ui::text(6, 25, status, spot());
                ui::rect(62, 26, 112, 5, faint());
                ui::rect(62, 26, (int)(112 * (percent > 100 ? 100 : percent) / 100), 5, spot());
            } else if (frames) {
                std::snprintf(status, sizeof status, "TAKE %.2fs", (double)frames / ENGINE_SR);
                ui::text(6, 25, status, ink());
            } else ui::text(6, 25, "empty", ghost());
        } else if (frames) {
            std::snprintf(status, sizeof status, "loaded %.2fs", (double)frames / ENGINE_SR);
            ui::text(6, 25, status, ink());
        } else ui::text(6, 25, "empty", ghost());
    }
    if (unit.kind == UnitKind::SamplePlayer || unit.kind == UnitKind::Osc) {
        const bool quantized = unit.kind == UnitKind::SamplePlayer
            ? static_cast<const PlayUnit&>(unit).pitch_quantize
            : static_cast<const OscUnit&>(unit).pitch_quantize;
        paper_button(284, 3, 32, 18, "qnt", quantized);
    }
    if (!module_focus_draw_header(unit)) {
        if (sample_slot >= 0) {
            paper_button(244, 3, 38, 18, "WAV");
            ui::text(288, 8, "X:del", ink());
        } else ui::text(252, 8, "X:del", ink());
    }
}

const char* module_focus_footer(const Unit& unit, int sample_slot) {
    if (unit.kind == UnitKind::SamplePlayer)
        return "PLAY: qnt  tap wave=chop  tap Sxx=browse";
    if (unit.kind == UnitKind::Recorder)
        return "REC > stop > send PLAY/GRAN  b back";
    return sample_slot >= 0
        ? "tap Sxx=browse  WAV=export  b back"
        : "drag value/depth  l: mod >  b back";
}

u32 paper() { return palette::bg(); }
u32 paper_hi() { return palette::cell(); }
u32 ink() { return palette::ink(); }
u32 spot() { return palette::spot(); }
u32 ghost() { return palette::ghost(); }
u32 faint() { return palette::subdued(); }

void paper_button(int x, int y, int w, int h, const char* label,
                  bool active, bool dim) {
    const auto press = small_press::pose(x, y, w, h, !dim && !(g_seq_lock_armed && y < 22));
    y += press.dy;
    ui::rect(x, y, w, h, small_press::tone(active ? spot() : paper_hi(), press.weight));
    ui::frame(x, y, w, h, dim ? ghost() : (active ? spot() : ink()));
    const u32 color = active ? palette::inverse() : (dim ? ghost() : ink());
    ui::text(x + (w - ui::text_w(label)) / 2, y + (h - 8) / 2, label, color);
}

void paper_footer(const char* label) {
    ui::rect(0, module_footer_layout::kLegacyBandTop, 320,
             module_footer_layout::kScreenHeight - module_footer_layout::kLegacyBandTop, ink());
    editor_style::caption(module_frame_layout::kLeft, module_footer_layout::kTextY,
                          module_frame_layout::frame(320).w, label, paper_hi());
}


} // namespace seance::platform::ui3ds
