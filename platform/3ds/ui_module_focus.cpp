#include "ui_module_focus_internal.h"

namespace seance::platform::ui3ds {

bool module_focus_touch_header(Unit& unit, int tx, int ty) {
    switch (unit.kind) {
        case UnitKind::SamplePlayer:
            return play_focus_touch_header(static_cast<PlayUnit&>(unit), tx, ty);
        case UnitKind::Grain:
            return grain_focus_touch_header(static_cast<GrainUnit&>(unit), tx, ty);
        case UnitKind::Filter:
            return filter_focus_touch_header(static_cast<FilterUnit&>(unit), tx, ty);
        default:
            return false;
    }
}

bool module_focus_touch_body(Unit& unit, int tx, int ty) {
    switch (unit.kind) {
        case UnitKind::Inst:
            return inst_focus_touch_body(static_cast<InstUnit&>(unit), tx, ty);
        case UnitKind::Glitch:
            return glitch_focus_touch_body(static_cast<GlitchUnit&>(unit), tx, ty);
        case UnitKind::Arp:
            return arp_focus_touch_body(static_cast<ArpUnit&>(unit), tx, ty);
        case UnitKind::Mouth:
            return mouth_focus_touch_body(static_cast<MouthUnit&>(unit), tx, ty);
        case UnitKind::Comp:
            return comp_focus_touch_body(static_cast<CompUnit&>(unit), tx, ty);
        case UnitKind::Fract:
            return fract_focus_touch_body(static_cast<FractUnit&>(unit), tx, ty);
        case UnitKind::Bird:
            return bird_focus_touch_body(static_cast<BirdUnit&>(unit), tx, ty);
        case UnitKind::SamplePlayer:
            return play_focus_touch_body(static_cast<PlayUnit&>(unit), tx, ty);
        case UnitKind::Grain:
            return grain_focus_touch_body(static_cast<GrainUnit&>(unit), tx, ty);
        case UnitKind::Osc:
            return osc_focus_touch_body(static_cast<OscUnit&>(unit), tx, ty);
        case UnitKind::Noise:
            return noise_focus_touch_body(static_cast<NoiseUnit&>(unit), tx, ty);
        case UnitKind::Fm:
            return fm_focus_touch_body(static_cast<FmUnit&>(unit), tx, ty);
        case UnitKind::Motor:
            return motor_focus_touch_body(static_cast<MotorUnit&>(unit), tx, ty);
        case UnitKind::Vca:
            return vca_focus_touch_body(static_cast<VcaUnit&>(unit), tx, ty);
        case UnitKind::Delay:
            return delay_focus_touch_body(static_cast<DelayUnit&>(unit), tx, ty);
        case UnitKind::Filter:
            return filter_focus_touch_body(static_cast<FilterUnit&>(unit), tx, ty);
        case UnitKind::Fold:
            return fold_focus_touch_body(static_cast<FoldUnit&>(unit), tx, ty);
        case UnitKind::Reverb:
            return reverb_focus_touch_body(static_cast<ReverbUnit&>(unit), tx, ty);
        case UnitKind::Chorus:
            return chorus_focus_touch_body(static_cast<ChorusUnit&>(unit), tx, ty);
        case UnitKind::Tilt:
            return tilt_focus_touch_body(static_cast<TiltUnit&>(unit), tx, ty);
        case UnitKind::Shift:
            return shift_focus_touch_body(static_cast<ShiftUnit&>(unit), tx, ty);
        case UnitKind::Ring:
            return ring_focus_touch_body(static_cast<RingUnit&>(unit), tx, ty);
        case UnitKind::Ghost:
            return ghost_focus_touch_body(static_cast<GhostUnit&>(unit), tx, ty);
        case UnitKind::MicIn:
            return mic_focus_touch_body(static_cast<MicUnit&>(unit), tx, ty);
        case UnitKind::Bitcrush:
            return bit_focus_touch_body(static_cast<BitUnit&>(unit), tx, ty);
        case UnitKind::Recorder:
            return recorder_focus_touch_body(static_cast<RecUnit&>(unit), tx, ty);
        case UnitKind::Strain:
            return strain_focus_touch_body(static_cast<StrainUnit&>(unit), tx, ty);
        case UnitKind::Stranular:
            return stranular_focus_touch_body(static_cast<StranularUnit&>(unit), tx, ty);
        default:
            return false;
    }
}

bool module_focus_draw_header(const Unit& unit) {
    switch (unit.kind) {
        case UnitKind::SamplePlayer:
            play_focus_draw_header(static_cast<const PlayUnit&>(unit));
            return true;
        case UnitKind::Filter:
            filter_focus_draw_header(static_cast<const FilterUnit&>(unit));
            return true;
        default:
            return false;
    }
}

void module_focus_draw_body(const Unit& unit) {
    switch (unit.kind) {
        case UnitKind::Inst:
            inst_focus_draw(static_cast<const InstUnit&>(unit));
            break;
        case UnitKind::Glitch:
            glitch_focus_draw(static_cast<const GlitchUnit&>(unit));
            break;
        case UnitKind::Arp:
            arp_focus_draw(static_cast<const ArpUnit&>(unit));
            break;
        case UnitKind::Mouth:
            mouth_focus_draw(static_cast<const MouthUnit&>(unit));
            break;
        case UnitKind::Comp:
            comp_focus_draw(static_cast<const CompUnit&>(unit));
            break;
        case UnitKind::Fract:
            fract_focus_draw(static_cast<const FractUnit&>(unit));
            break;
        case UnitKind::Bird:
            bird_focus_draw(static_cast<const BirdUnit&>(unit));
            break;
        case UnitKind::SamplePlayer:
            play_focus_draw(static_cast<const PlayUnit&>(unit));
            break;
        case UnitKind::Grain:
            grain_focus_draw(static_cast<const GrainUnit&>(unit));
            break;
        case UnitKind::Osc:
            osc_focus_draw(static_cast<const OscUnit&>(unit));
            break;
        case UnitKind::Noise:
            noise_focus_draw(static_cast<const NoiseUnit&>(unit));
            break;
        case UnitKind::Fm:
            fm_focus_draw(static_cast<const FmUnit&>(unit));
            break;
        case UnitKind::Motor:
            motor_focus_draw(static_cast<const MotorUnit&>(unit));
            break;
        case UnitKind::Vca:
            vca_focus_draw(static_cast<const VcaUnit&>(unit));
            break;
        case UnitKind::Delay:
            delay_focus_draw(static_cast<const DelayUnit&>(unit));
            break;
        case UnitKind::Filter:
            filter_focus_draw(static_cast<const FilterUnit&>(unit));
            break;
        case UnitKind::Fold:
            fold_focus_draw(static_cast<const FoldUnit&>(unit));
            break;
        case UnitKind::Reverb:
            reverb_focus_draw(static_cast<const ReverbUnit&>(unit));
            break;
        case UnitKind::Chorus:
            chorus_focus_draw(static_cast<const ChorusUnit&>(unit));
            break;
        case UnitKind::Tilt:
            tilt_focus_draw(static_cast<const TiltUnit&>(unit));
            break;
        case UnitKind::Shift:
            shift_focus_draw(static_cast<const ShiftUnit&>(unit));
            break;
        case UnitKind::Ring:
            ring_focus_draw(static_cast<const RingUnit&>(unit));
            break;
        case UnitKind::Ghost:
            ghost_focus_draw(static_cast<const GhostUnit&>(unit));
            break;
        case UnitKind::MicIn:
            mic_focus_draw(static_cast<const MicUnit&>(unit));
            break;
        case UnitKind::Bitcrush:
            bit_focus_draw(static_cast<const BitUnit&>(unit));
            break;
        case UnitKind::Strain:
            strain_focus_draw(static_cast<const StrainUnit&>(unit));
            break;
        case UnitKind::Stranular:
            stranular_focus_draw_body(static_cast<const StranularUnit&>(unit));
            break;
        default:
            break;
    }
}


} // namespace seance::platform::ui3ds
