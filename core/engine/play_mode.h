// Modern PLAY musical gate controls. Stored indices are append-only.
#pragma once
#include "../audio/fixed.h"
#include <cstdint>
namespace seance { namespace play_mode {
constexpr int GATE_COUNT = 6;
constexpr int DEFAULT_GATE = 2; // one native event interval
inline bool is_gate(trackr::fx::q15 value) { return value > 16383; }
inline int gate_index(trackr::fx::q15 value) {
    const int v = value < 0 ? 0 : value;
    return (v * (GATE_COUNT - 1) + 16383) / 32767;
}
inline trackr::fx::q15 gate_value(int index) {
    if (index < 0) index = 0;
    if (index >= GATE_COUNT) index = GATE_COUNT - 1;
    return (trackr::fx::q15)((index * 32767 + 2) / (GATE_COUNT - 1));
}
inline uint8_t gate_quarters(trackr::fx::q15 value) {
    return (uint8_t)(1u << gate_index(value));
}
inline const char* gate_label(trackr::fx::q15 value) {
    static const char* const labels[GATE_COUNT] = {"1/4", "1/2", "1", "2", "4", "8"};
    return labels[gate_index(value)];
}
} } // namespace seance::play_mode
