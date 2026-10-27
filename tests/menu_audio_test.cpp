#include "../platform/3ds/menu_sound_player.h"
#include "../platform/3ds/menu_audio.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
using namespace seance::platform;
using namespace seance::platform::menu_sound;
#include "menu_drone_bin.h"
#include "menu_entry_bin.h"
static Clip drone() { return {reinterpret_cast<const int16_t*>(menu_drone_bin), menu_drone_bin_size / 4}; }
static Clip entry() { return {reinterpret_cast<const int16_t*>(menu_entry_bin), menu_entry_bin_size / 4}; }
static std::array<int16_t, 1024> block;
static void advance(Player& p, size_t frames) {
    while (frames) { const auto n = std::min<size_t>(512, frames); p.render(block.data(), n); frames -= n; }
}
static int peak() { int p = 0; for (int v : block) p = std::max(p, std::abs(v)); return p; }
static void player_tests() {
    Player p(drone(), entry());
    advance(p, 512); assert(!peak() && !p.active());
    p.command(3); advance(p, 1); assert(peak() <= 1);
    advance(p, kRate * startup_reveal::kDurationMs / 1000);
    // Stable gain/phase repeats bit-exactly through multiple cyclic boundaries.
    std::vector<int16_t> a(drone().frames * 2), b(a.size());
    p.render(a.data(), drone().frames); p.render(b.data(), drone().frames); assert(a == b);
    p.render(b.data(), drone().frames); assert(a == b);
    p.command(2); advance(p, kRate); assert(!p.active() && !peak());
    p.command(3); advance(p, kRate * 2); assert(p.active() && peak());
    p.command(9); advance(p, kRate * 5); assert(!p.active()); // fade drone + one chime
    p.command(9); advance(p, kRate); assert(!peak()); // stable serial never repeats
    p.command(16); p.command(17); advance(p, kRate); assert(!peak()); // disabled event consumed
    p.command(25); advance(p, kRate / 2); assert(peak());
    p.command(24); advance(p, kRate); assert(!p.active() && !peak());
    p.command(25); advance(p, kRate); assert(!peak()); // enabling doesn't resurrect tail
    Player missing({}, {}); missing.command(3); advance(missing, 512); assert(!peak() && !missing.active());
    missing.command(9); advance(missing, 512); assert(!peak());

    // A second success during the one-shot consumes its event without overlap/restart.
    Player one(drone(), entry()), two(drone(), entry());
    one.command(9); two.command(9); advance(one, 4096); advance(two, 4096);
    two.command(17);
    std::array<int16_t, 1024> ref;
    for (int i = 0; i < 220; ++i) {
        one.render(ref.data(), 512); two.render(block.data(), 512); assert(ref == block);
    }
    two.command(17); advance(two, 512); assert(!peak() && !two.active());

    // Constant stereo isolates sample-ramp continuity, duck level and channel order.
    const int16_t constant[] = {20000, -10000};
    std::vector<int16_t> tone(kRate * 4 * 2);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = constant[i % 2];
    Player ramp({constant, 1}, {tone.data(), tone.size()/2});
    ramp.command(3);
    const unsigned fade_frames = kRate * startup_reveal::kDurationMs / 1000;
    int previous = 0;
    for (unsigned i = 1; i <= fade_frames; ++i) {
        int16_t sample[2]; ramp.render(sample,1);
        assert(sample[0] >= previous && sample[0] - previous <= 1);
        assert(sample[1] <= 0 && sample[1] >= -2200);
        if (i == 1) assert(sample[0] == 0);
        if (i == fade_frames/4) assert(std::abs(int(sample[0])-688) <= 3);
        if (i == fade_frames/2) assert(std::abs(int(sample[0])-2200) <= 3);
        previous = sample[0];
    }
    advance(ramp,512);
    assert(block[0] == 4400 && block[1] == -2200);
    ramp.command(9); advance(ramp, kRate);
    assert(block[0] == 5600 && block[1] == -2800);
    ramp.command(13); advance(ramp, kRate / 10); assert(block[0] == 700 && block[1] == -350);
    ramp.command(9); advance(ramp, kRate / 10); assert(block[0] == 700); // latched duck
    std::puts("pass menu player: cyclic stereo/3s smoothstep entrance/mute/events/duck/missing assets");
}
static void wait_for(const std::function<bool()>& predicate) {
    for (int i = 0; i < 2000; ++i) {
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "worker timed out");
}
static void backend_tests() {
    namespace f = fake_ndsp;
    // Simulate the instrument's separate reference; menu must not own its exit.
    f::refs = 1;
    {
        MenuAudio a; assert(a.init(false)); assert(f::refs == 2 && f::allocations == 4);
        a.open_menu();
        wait_for([] { return fake_ndsp::waits > 0; });
        assert(f::queued == 0);
        a.set_enabled(true);
        wait_for([] { return fake_ndsp::nonzero >= 8; });
        f::hook(APTHOOK_ONSLEEP, f::hook_arg);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const int asleep = f::queued;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(f::queued == asleep);
        f::hook(APTHOOK_ONWAKEUP, f::hook_arg);
        wait_for([&] { return f::queued > asleep; });
        a.enter_patch(); a.set_patch_audible(true);
        // Drain the output; idle worker must stop polling, not endlessly enqueue zeroes.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const int ended = f::queued, waits = f::waits;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        assert(f::queued == ended && f::waits == waits);
        a.enter_patch(); wait_for([&] { return f::queued > ended; });
        a.set_enabled(false);
        a.shutdown(); a.shutdown();
    }
    assert(f::refs == 1 && !f::allocations && !f::hook && f::pending.empty());
    f::init_fail = true; { MenuAudio a; assert(!a.init(true)); }
    f::init_fail = false;
    for (int i = 0; i < 4; ++i) {
        f::alloc_calls = 0; f::allocation_fail_at = i;
        MenuAudio a; assert(!a.init(true));
        assert(f::refs == 1 && !f::allocations && !f::hook);
    }
    f::allocation_fail_at = -1; f::thread_fail = true;
    { MenuAudio a; assert(!a.init(true)); }
    f::thread_fail = false;
    assert(f::refs == 1 && !f::allocations && !f::hook);
    f::refs = 0;
    std::puts("pass menu backend: independent channel/refcount, ring, idle, mute, sleep, failure cleanup");
}
int main() { player_tests(); backend_tests(); }
