#pragma once
// UI-only stored/control-value readouts. No engine, sample, voice or captured-frame
// dependency: callers supply the frozen PLAY mode, quantize flag and scale.
// Musical Normal/Stretch note/time laws stay unchanged; only Legacy qnt opts in.
#include "../../core/engine/units.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace seance::platform::ui3ds::play_readout {
inline void value_text(char* out, size_t size, int param, q15 raw, bool modern, bool quantize,
                       PlayUnit::PitchMode mode = PlayUnit::PitchMode::Legacy, MusicalScale scale = {}) {
    if (!out || !size) return;
    using P = PlayUnit;
    if (param < 0 || param >= (modern ? P::P_COUNT : 6)) {
        std::snprintf(out, size, "--"); return;
    }
    const int value = std::max(0, int(raw));
    if (param == P::P_NOTE) {
        musical_note::name(out, size, musical_note::decode(raw));
    } else if (param == P::P_SPEED && mode == P::PitchMode::Normal) {
        std::snprintf(out, size, "%s", raw < 0 ? "reverse" : "forward");
    } else if (param == P::P_SPEED && mode == P::PitchMode::Stretch) {
        const int speed = std::max(2048, std::abs(int(raw)));
        std::snprintf(out, size, "%s%.2fx", raw < 0 ? "r " : "", double(speed) / 8192.0);
    } else if (param == P::P_SPEED) {
        // Legacy and modern PLAY both use signed varispeed, not percent or an
        // independent pitch shifter. Use the actual integer law, including qnt,
        // the nonzero floor and its retained negative-near-zero ordering.
        // This is global speed only: cut multiplier/reverse, arp and scrub motion
        // belong to the voice, not to this one parameter's nominal readout.
        const auto w = play_window(2, nullptr, 0, raw, 0, 32767, 0, quantize, modern, scale);
        std::snprintf(out, size, "%+.2fx", double(w.increment) / 65536.0);
    } else if (param == P::P_HOLD) std::snprintf(out, size, "%s", raw > 16383 ? "hold" : "scratch");
    else if (param == P::P_LOOP || param == P::P_SCRUB) std::snprintf(out, size, "%s", raw > 16383 ? "on" : "off");
    else if (param == P::P_MODE) std::snprintf(out, size, "%s", play_mode::is_gate(raw) ? "gate" : "one shot");
    else if (param == P::P_GATE) std::snprintf(out, size, "%s step", play_mode::gate_label(raw));
    else if (param == P::P_SLICE) {
        const int cut = value * 17 >> 15;
        if (cut) std::snprintf(out, size, "cut %02d", cut);
        else std::snprintf(out, size, "window");
    } else std::snprintf(out, size, "%.2f%%", double(value) * 100.0 / 32767.0);
}
} // namespace seance::platform::ui3ds::play_readout
