#pragma once
// UI-only scalar lifetime of one shared field. No engine, allocation or GPU.
#include <cstdint>

namespace seance::platform::ui3ds::paper_presence {
enum class Scene { Still, Startup, Patch };
constexpr unsigned FadeMs = 240, UpdateMs = 100;
constexpr unsigned target(Scene scene) {
    return scene == Scene::Patch ? 255 : scene == Scene::Startup ? 102 : 0;
}
struct Motion {
    Scene scene = Scene::Still;
    bool frozen = false;
    uint64_t last = 0;
    unsigned amount = 0, from = 0, to = 0, age = FadeMs;
    // Common phase units: patch takes 18000ms (5 units/ms), startup 30000 (3).
    // Only shown_units is presented; hide/modal discards sub-bucket drift.
    unsigned units = 0, shown_units = 0, pending = 0;
    unsigned phase() const { return shown_units / 5; }
    void capture(Scene next, bool freeze, uint64_t now, unsigned startup_amount = 102) {
        const bool changed = scene != next;
        const unsigned destination = next == Scene::Startup
            ? (startup_amount > 255 ? 255 : startup_amount) : target(next);
        freeze = freeze && next != Scene::Still;
        if (changed || to != destination) {
            scene = next; from = amount; to = destination;
            age = from == to ? FadeMs : 0;
        } else if (!freeze && !frozen && now >= last) {
            const uint64_t dt = now - last;
            const bool settled = age == FadeMs;
            if (!settled) {
                age += unsigned(dt > FadeMs - age ? FadeMs - age : dt);
                // Smoothstep; retarget always starts at the last captured level,
                // not an extrapolated unshown frame. Text/input never waits.
                constexpr int64_t d = FadeMs;
                const int64_t t = age;
                const int64_t eased = (65536*t*t*(3*d-2*t) + d*d*d/2)/(d*d*d);
                amount = unsigned((int64_t(from)*(65536-eased) + int64_t(to)*eased + 32768)/65536);
            }
            if (settled && next != Scene::Still) {
                const unsigned period = next == Scene::Patch ? 18000 : 30000;
                const unsigned rate = next == Scene::Patch ? 5 : 3;
                units = (units + unsigned(dt % period)*rate) % 90000;
                if (dt >= UpdateMs - pending) shown_units = units;
                pending = (pending + unsigned(dt % UpdateMs)) % UpdateMs;
            }
        }
        if (changed || freeze) { units = shown_units; pending = 0; }
        frozen = freeze; last = now;
    }
};
} // namespace seance::platform::ui3ds::paper_presence
