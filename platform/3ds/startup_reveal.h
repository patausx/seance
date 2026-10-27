#pragma once
// Shared presentation duration for boot brightness and the independent drone.
namespace seance::platform::startup_reveal {
constexpr unsigned kDurationMs = 3000;
constexpr unsigned veil_alpha(unsigned elapsed_ms) {
    if (elapsed_ms >= kDurationMs) return 0;
    const unsigned long long t = elapsed_ms, d = kDurationMs;
    return 255 - unsigned((255*t*t*(3*d-2*t) + d*d*d/2)/(d*d*d));
}
} // namespace seance::platform::startup_reveal
