#include "ui_editor.h"
#include "ui_param_lens.h"
#include "ui_module_rand.h"
#include "strn_focus.h"
#include "ui_sample_library.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_module_focus_internal.h"
#include "small_button_motion.h"
#include <cstdio>
using namespace seance;
namespace seance::platform::ui3ds {


// ===================== param editor overlay =====================
namespace {
uint64_t button_context() {
    return uint64_t(g_view) | (uint64_t(g_edit_chain) << 8) |
        (uint64_t(g_edit_slot) << 12) | (uint64_t(g_seq_lock_armed) << 16) |
        (uint64_t(g_seq_lock_armed ? g_seq_lock_step : 0) << 17) |
        (uint64_t(g_eng ? g_eng->active_pattern : 0) << 24);
}
}
void editor_capture_button_motion(bool visible) {
    small_press::capture(osGetTime(), visible && g_eng ? edited_unit() : nullptr,
                         button_context(), visible);
}


Unit* edited_unit() { return g_eng->chains[g_edit_chain].units[g_edit_slot]; }

bool editor_back() {
    if (param_lens_is_open()) { param_lens_cancel(); return true; }
    Unit* unit = edited_unit();
    if (unit && unit->kind == UnitKind::Recorder) return recorder_focus_back();
    if (unit && unit->kind == UnitKind::Comp) return comp_focus_back();
    if (unit && unit->kind == UnitKind::Bitcrush) return bit_focus_back();
    if (unit && unit->kind == UnitKind::Strain) return strain_focus_back();
    if (unit && unit->kind == UnitKind::Vca) return vca_focus_back();
    return unit && unit->kind == UnitKind::SamplePlayer &&
           play_focus_back(*static_cast<PlayUnit*>(unit));
}

void editor_touch_down(int tx, int ty) {
    if (module_rand_touch_down(tx, ty)) return;
    if (recorder_focus_return_touch(tx, ty, true)) return;
    // INST owns release-only footer buttons too; an already-open lens keeps its
    // existing controller. Do not open MOD on the press that only arms its label.
    if (!param_lens_is_open()) {
        Unit* candidate = edited_unit();
        if (candidate && candidate->kind == UnitKind::Inst) {
            small_press::begin(tx, ty, osGetTime(), candidate, button_context());
            module_focus_touch_body(*candidate, tx, ty);
            return;
        }
    }
    if (param_lens_touch_down(tx, ty)) return;
    Unit* u = edited_unit();
    if (!u) { g_overlay = Overlay::None; return; }
    small_press::begin(tx, ty, osGetTime(), u, button_context());
    if (u->kind == UnitKind::Glitch) { module_focus_touch_body(*u, tx, ty); return; }
    if (u->kind == UnitKind::Arp) { module_focus_touch_body(*u, tx, ty); return; }
    if (u->kind == UnitKind::Mouth) { module_focus_touch_body(*u, tx, ty); return; }
    if (u->kind == UnitKind::Comp) { module_focus_touch_body(*u, tx, ty); return; }
    if (u->kind == UnitKind::Fract) { module_focus_touch_body(*u, tx, ty); return; }
    if (u->kind == UnitKind::Bird) { module_focus_touch_body(*u, tx, ty); return; }
    // SHIFT owns its complete lock-safe surface; never enter legacy shell hits.
    if (u->kind == UnitKind::Shift) { module_focus_touch_body(*u, tx, ty); return; }

    // Lock mode exposes only semantic base gestures. Header topology/sample tools
    // stay inert everywhere; legacy lower-right routing tethers remain guarded.
    // FM owns an explicit two-page map and gates its routing/mode controls itself,
    // so its formerly blocked gain/page region remains available for p-locks.
    // OSC owns its complete header and lock-safe map; old shell hit regions must not intercept it.
    if (g_seq_lock_armed && u->kind != UnitKind::Osc && u->kind != UnitKind::Grain && u->kind != UnitKind::SamplePlayer && u->kind != UnitKind::Reverb && u->kind != UnitKind::Chorus && u->kind != UnitKind::Vca && u->kind != UnitKind::Delay && u->kind != UnitKind::Filter && u->kind != UnitKind::Fold && u->kind != UnitKind::Bitcrush && u->kind != UnitKind::Tilt && u->kind != UnitKind::Ring && u->kind != UnitKind::Strain && u->kind != UnitKind::MicIn && u->kind != UnitKind::Recorder && u->kind != UnitKind::Noise &&
        ((ty < 22 && u->kind != UnitKind::Fm && u->kind != UnitKind::Stranular) || (u->kind != UnitKind::Fm && u->kind != UnitKind::Stranular && tx >= 258 && ty >= 190))) return;

    if (module_focus_touch_header(*u, tx, ty)) return;
    if (u->kind != UnitKind::Osc && u->kind != UnitKind::Grain && u->kind != UnitKind::SamplePlayer &&
        u->kind != UnitKind::Filter && u->kind != UnitKind::Fold &&
        u->kind != UnitKind::Reverb && u->kind != UnitKind::Chorus && u->kind != UnitKind::Vca && u->kind != UnitKind::Delay &&
        u->kind != UnitKind::Tilt && u->kind != UnitKind::Ring &&
        u->kind != UnitKind::Noise && u->kind != UnitKind::Fm && u->kind != UnitKind::Motor &&
        u->kind != UnitKind::Ghost && u->kind != UnitKind::MicIn && u->kind != UnitKind::Recorder &&
        u->kind != UnitKind::Bitcrush && u->kind != UnitKind::Strain && u->kind != UnitKind::Stranular &&
        module_focus_touch_common_header(*u, tx, ty)) return;

    if (module_focus_touch_body(*u, tx, ty)) return;

}

void editor_touch_drag(int tx, int ty) {
    if (module_rand_touch_drag(tx, ty)) return;
    if (recorder_focus_return_touch(tx, ty, false)) return;
    small_press::drag(tx, ty);
    if (param_lens_touch_drag(tx, ty)) return;
    Unit* unit = edited_unit();
    if (!unit) return;
    if (unit->kind == UnitKind::SamplePlayer)
        play_focus_drag(*static_cast<PlayUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Osc)
        osc_focus_drag(*static_cast<OscUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Noise)
        noise_focus_drag(*static_cast<NoiseUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Fm)
        fm_focus_drag(*static_cast<FmUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Motor)
        motor_focus_drag(*static_cast<MotorUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Grain)
        grain_focus_drag(*static_cast<GrainUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Vca)
        vca_focus_drag(*static_cast<VcaUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Delay)
        delay_focus_drag(*static_cast<DelayUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Filter)
        filter_focus_drag(*static_cast<FilterUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Fold)
        fold_focus_drag(*static_cast<FoldUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Reverb)
        reverb_focus_drag(*static_cast<ReverbUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Chorus)
        chorus_focus_drag(*static_cast<ChorusUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Tilt)
        tilt_focus_drag(*static_cast<TiltUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Inst)
        inst_focus_drag(*static_cast<InstUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Glitch)
        glitch_focus_drag(*static_cast<GlitchUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Arp)
        arp_focus_drag(*static_cast<ArpUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Mouth)
        mouth_focus_drag(*static_cast<MouthUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Comp)
        comp_focus_drag(*static_cast<CompUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Fract)
        fract_focus_drag(*static_cast<FractUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Bird)
        bird_focus_drag(*static_cast<BirdUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Shift)
        shift_focus_drag(*static_cast<ShiftUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Ring)
        ring_focus_drag(*static_cast<RingUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Ghost)
        ghost_focus_drag(*static_cast<GhostUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::MicIn)
        mic_focus_drag(*static_cast<MicUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Recorder)
        recorder_focus_drag(*static_cast<RecUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Bitcrush)
        bit_focus_drag(*static_cast<BitUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Strain)
        strain_focus_drag(*static_cast<StrainUnit*>(unit), tx, ty);
    else if (unit->kind == UnitKind::Stranular)
        stranular_focus_drag(*static_cast<StranularUnit*>(unit), tx, ty);
}

void editor_touch_release() {
    if (module_rand_touch_release()) return;
    reverb_focus_touch_release();
    inst_focus_touch_release();
    glitch_focus_release();
    arp_focus_release();
    mouth_focus_release();
    comp_focus_release();
    fract_focus_release();
    osc_focus_release();
    fm_focus_release();
    chorus_focus_release();
    delay_focus_touch_release();
    filter_focus_release();
    fold_focus_touch_release();
    bit_focus_release();
    tilt_focus_release();
    ring_focus_release();
    bird_focus_release();
    shift_focus_release();
    strain_focus_release();
    grain_focus_touch_release();
    mic_focus_touch_release();
    recorder_focus_touch_release();
    noise_focus_release();
    vca_focus_release();
    ghost_focus_release();
    stranular_focus_release();
    param_lens_touch_release();
    // A PLAY lift must commit its owned button before any cancellation. The
    // navigation/lens path still cancels instead of activating a hidden control.
    if (param_lens_is_open()) { play_focus_release(); return; }
    Unit* unit = edited_unit();
    if (unit && unit->kind == UnitKind::SamplePlayer)
        play_focus_touch_release(*static_cast<PlayUnit*>(unit));
    else
        play_focus_release();
}

void editor_cancel_input() {
    module_rand_cancel();
    reverb_focus_release(); inst_focus_release(); delay_focus_release();
    play_focus_release(); glitch_focus_release(); arp_focus_release();
    mouth_focus_release(); comp_focus_release(); fract_focus_release();
    osc_focus_release(); fm_focus_release(); motor_focus_release();
    chorus_focus_release(); filter_focus_release(); fold_focus_release();
    bit_focus_release(); tilt_focus_release(); ring_focus_release();
    bird_focus_release(); shift_focus_release(); strain_focus_release();
    grain_focus_release(); mic_focus_release(); recorder_focus_release();
    noise_focus_release(); vca_focus_release(); ghost_focus_release();
    stranular_focus_release(); small_press::cancel();
    g_drag=Drag::None; g_last_px=-1; g_smooth_active=false;
}

namespace {
void draw_module_editor() {
    if (stranular_focus_draw_bottom()) return;
    Unit* u = edited_unit();
    if (!u) return;
    if (u->kind == UnitKind::Inst) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Glitch) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Arp) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Mouth) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Comp) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Fract) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Bird) { module_focus_draw_body(*u); return; }
    if (u->kind == UnitKind::Shift) { module_focus_draw_body(*u); return; }

    // Migrated effects own complete menu-paper surfaces; do not paint the old shell first.
    if (u->kind != UnitKind::Grain && u->kind != UnitKind::SamplePlayer && u->kind != UnitKind::Osc && u->kind != UnitKind::Fm && u->kind != UnitKind::Reverb && u->kind != UnitKind::Ghost && u->kind != UnitKind::Chorus && u->kind != UnitKind::Vca && u->kind != UnitKind::Delay && u->kind != UnitKind::Filter && u->kind != UnitKind::Fold && u->kind != UnitKind::Bitcrush && u->kind != UnitKind::Tilt && u->kind != UnitKind::Ring && u->kind != UnitKind::Strain && u->kind != UnitKind::MicIn && u->kind != UnitKind::Recorder && u->kind != UnitKind::Noise && u->kind != UnitKind::Motor) module_focus_draw_shell(*u);
    const int smp_slot = sample_slot_for_unit(u);

    module_focus_draw_body(*u);

    if (u->kind == UnitKind::Recorder) {
        recorder_focus_draw(*static_cast<const RecUnit*>(u),
                            module_focus_sample_result_notice());
    }
    if (u->kind != UnitKind::SamplePlayer && u->kind != UnitKind::Osc &&
        u->kind != UnitKind::Noise && u->kind != UnitKind::Fm && u->kind != UnitKind::Motor &&
        u->kind != UnitKind::Grain && u->kind != UnitKind::Vca &&
        u->kind != UnitKind::Delay && u->kind != UnitKind::Filter &&
        u->kind != UnitKind::Fold && u->kind != UnitKind::Reverb &&
        u->kind != UnitKind::Chorus && u->kind != UnitKind::Tilt &&
        u->kind != UnitKind::Ring && u->kind != UnitKind::Ghost &&
        u->kind != UnitKind::MicIn && u->kind != UnitKind::Recorder &&
        u->kind != UnitKind::Bitcrush && u->kind != UnitKind::Strain && u->kind != UnitKind::Stranular) {
        if (!g_seq_lock_armed) paper_footer(module_focus_footer(*u, smp_slot));
    }
    if (u->kind == UnitKind::Grain || u->kind == UnitKind::SamplePlayer || u->kind == UnitKind::Osc || u->kind == UnitKind::Fm || u->kind == UnitKind::Reverb || u->kind == UnitKind::Ghost || u->kind == UnitKind::Chorus || u->kind == UnitKind::Vca || u->kind == UnitKind::Delay || u->kind == UnitKind::Filter || u->kind == UnitKind::Fold || u->kind == UnitKind::Bitcrush || u->kind == UnitKind::Tilt || u->kind == UnitKind::Ring || u->kind == UnitKind::Strain || u->kind == UnitKind::Recorder || u->kind == UnitKind::Noise || u->kind == UnitKind::Motor || u->kind == UnitKind::MicIn) return; // module already drew the contextual plain footer.
    if (!g_seq_lock_armed && g_view == View::Chains && u->kind != UnitKind::Recorder)
        param_lens_draw_entry();
    if (g_seq_lock_armed && u->kind == UnitKind::Reverb) {
        paper_footer(g_seq_lock_flash > 0 ? "lock full / B back" : "p-lock / drag labelled controls / B back");
    } else if (g_seq_lock_armed) {
        char lock_help[64];
        snprintf(lock_help, sizeof lock_help, g_seq_lock_flash > 0
                 ? "lock full C%d:S%02d / tap param name to delete"
                 : "locking C%d:S%02d / base gestures only / B return",
                 g_seq_lock_chain + 1, g_seq_lock_step + 1);
        paper_footer(lock_help);
    }
}
} // namespace

void editor_draw() {
    // One ordinary module surface and one frozen modulation layer. Keeping the
    // existing facade avoids a duplicate module dispatch or cached screenshot.
    draw_module_editor();
    module_rand_draw();
    recorder_focus_draw_return();
    if (param_lens_is_open()) param_lens_draw_bottom();
}

void editor_ui_tick() {
    module_focus_tick();
}

}
