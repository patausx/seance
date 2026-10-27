// Focused legacy GRAN telemetry proof; clean DSP has its own focused suite.
// Build: g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore
//   tests/gran_visual_core_test.cpp core/engine/units.cpp
//   -Wl,--gc-sections -o /tmp/seance_gran_visual_core_test (join the three lines).
#include "engine/units.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <sys/mman.h>
#include <unistd.h>

using seance::GrainUnit;
using seance::q15;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
using Visual = GrainUnit::VisualGrain;
static uint64_t allocations = 0;
void* operator new(std::size_t n) {
    ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
uint64_t checks = 0, pcm_samples = 0, captures = 0, envelope_checks = 0;
void require(bool good, const char* expression, int line) {
    ++checks;
    if (!good) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        std::exit(1);
    }
}
#define CHECK(x) require((x), #x, __LINE__)
void set(GrainUnit& g, int p, int v) {
    g.params[p].effective = g.params[p].base = (q15)v;
}
void sample(uint32_t frames, int channels = 1) {
    Sample& s = SampleBank::instance().slot(0);
    s = Sample{};
    s.channels = (uint8_t)channels;
    s.data.resize((size_t)frames * channels);
    for (size_t i = 0; i < s.data.size(); ++i)
        s.data[i] = (q15)((int)((i * 1931u + i / 37u * 173u) & 65535u) - 32768);
}
void capture(const GrainUnit& g, Visual (&out)[GrainUnit::MAX_GRAINS]) {
    unsigned char before[sizeof g];
    std::memcpy(before, &g, sizeof g);
    const uint64_t allocations_before = allocations;
    g.capture_visual_grains(out);
    CHECK(std::memcmp(before, &g, sizeof g) == 0);
    CHECK(allocations == allocations_before);
    ++captures;
}
bool equal(const Visual& a, const Visual& b) {
    return a.active == b.active && a.position_q16 == b.position_q16 &&
        a.start_q16 == b.start_q16 && a.end_q16 == b.end_q16 &&
        a.age == b.age && a.length == b.length && a.attack == b.attack &&
        a.release == b.release && a.envelope == b.envelope;
}
int active(const Visual (&v)[GrainUnit::MAX_GRAINS]) {
    int n = 0;
    for (const auto& g : v) if (g.active) ++n;
    return n;
}
void empty(const Visual (&v)[GrainUnit::MAX_GRAINS]) {
    for (const auto& g : v) CHECK(equal(g, Visual{}));
}
q15 expected_envelope(uint32_t age, uint32_t length, uint32_t attack, uint32_t release) {
    int64_t amp = 32767;
    if (attack && age < attack) amp = std::min<int64_t>(amp, age * (32767u / attack));
    const uint32_t left = age < length ? length - age - 1 : 0;
    if (release && left < release) amp = std::min<int64_t>(amp, left * (32767u / release));
    ++envelope_checks;
    return (q15)std::max<int64_t>(0, amp);
}
int32_t expected_speed(int pitch) {
    const int speed = std::max(1024, std::abs(pitch) * 8);
    return pitch < 0 ? -speed : speed;
}
uint32_t expected_length(int size, uint32_t nf, int32_t inc) {
    uint32_t n = 960u + ((uint64_t)(uint16_t)size * 15040u + 16383u) / 32767u;
    n = std::min<uint32_t>(n, ((uint64_t)(nf - 3) * 65536u) / std::abs(inc));
    return std::max<uint32_t>(2, n);
}
int64_t expected_start(int pos, uint32_t nf, uint32_t length, int32_t inc) {
    const uint64_t span = std::min<uint64_t>((uint64_t)length * std::abs(inc) / 65536, nf - 3);
    int64_t start = (uint64_t)(uint16_t)pos * nf / 32768;
    if (start + (int64_t)span + 2 >= nf) start = nf - span - 3;
    return std::max<int64_t>(0, start) * 65536;
}
void quiet_config(GrainUnit& g, int pitch, int size, int pos, int atk, int rel) {
    g.clean = false; // historical trajectories/envelopes remain frozen
    set(g, GrainUnit::P_SPRAY, 0); set(g, GrainUnit::P_PITCH_DEV, 0);
    set(g, GrainUnit::P_POS, pos); set(g, GrainUnit::P_PITCH, pitch);
    set(g, GrainUnit::P_SIZE, size); set(g, GrainUnit::P_ATTACK, atk);
    set(g, GrainUnit::P_RELEASE, rel);
    g.clock_sync = true; g.sync_interval_frames = 200000;
    g.control_tick();
}

void defaults_and_empty() {
    sample(0);
    GrainUnit g;
    static_assert(std::is_trivially_copyable<Visual>::value, "frozen scalar DTO");
    static_assert(GrainUnit::MAX_GRAINS == 8, "eight primary heads only");
    const char* names[] = {"POS", "SIZE", "SPRY", "PTCH", "GAIN", "DENS", "JIT", "ATK", "REL", "PDEV", "VOX"};
    const int defaults[] = {0, 8192, 4096, 16384, 32767, 16384, 0, 4096, 8192, 0, 32767};
    CHECK(g.num_params == GrainUnit::P_COUNT && g.clean); CHECK(g.sample_slot == 0);
    CHECK(g.params[GrainUnit::P_SCAN].base == 0 && g.params[GrainUnit::P_SCAN].bipolar);
    CHECK(!g.clock_sync && g.sync_division == 0 && g.sync_interval_frames == 8000);
    for (int i = 0; i < 11; ++i) {
        CHECK(std::strcmp(g.params[i].name, names[i]) == 0);
        CHECK(g.params[i].base == defaults[i]);
        CHECK(g.params[i].effective == defaults[i]);
        CHECK(g.params[i].bipolar == (i == GrainUnit::P_PITCH));
    }
    Visual v[8];
    capture(g, v); empty(v);
    g.sample_slot = -1; capture(g, v); empty(v);
    g.sample_slot = trackr::synth::SAMPLE_BANK_SIZE; capture(g, v); empty(v);
    g.sample_slot = 0;
    q15 pcm[32]; g.process(pcm, 32); capture(g, v); empty(v);
    for (auto x : pcm) CHECK(x == 0);
    std::puts("PASS defaults/order/empty/invalid slots");
}

void trajectories_and_latches() {
    const int pitches[] = {0, 1, 128, 4097, 8192, 16384, 32767, -1, -4097, -8192, -16384, -32767};
    const int sizes[] = {0, 8192, 32767};
    const int ramps[][2] = {{0,0}, {1,1}, {4096,8192}, {32767,32767}};
    uint32_t cases = 0;
    for (int pitch : pitches) for (int size : sizes) for (const auto& ramp : ramps) {
        sample(65536, cases % 2 + 1);
        GrainUnit g;
        quiet_config(g, pitch, size, 16384, ramp[0], ramp[1]);
        const int32_t inc = expected_speed(pitch);
        const uint32_t length = expected_length(size, 65536, inc);
        const int64_t start = expected_start(16384, 65536, length, inc);
        const uint32_t atk = (uint64_t)ramp[0] * length / 65536;
        const uint32_t rel = (uint64_t)ramp[1] * length / 65536;
        for (uint32_t age = 1; age <= length + 1; ++age) {
            q15 pcm; g.process(&pcm, 1);
            if (age == 20) {
                // Live knob changes must not rewrite the older grain's trajectory or ramps.
                set(g, GrainUnit::P_ATTACK, 12345); set(g, GrainUnit::P_RELEASE, 0);
                set(g, GrainUnit::P_SIZE, 13); set(g, GrainUnit::P_POS, 0);
                set(g, GrainUnit::P_PITCH, -1234); set(g, GrainUnit::P_PITCH_DEV, 32767);
                g.control_tick();
            }
            Visual v[8]; capture(g, v);
            const int64_t cursor = start + (int64_t)age * inc;
            const bool valid = age < length && cursor >= 0 && cursor / 65536 + 1 < 65536;
            CHECK(active(v) == (valid ? 1 : 0));
            if (valid) {
                CHECK(v[0].active && v[0].age == age && v[0].length == length);
                CHECK(v[0].position_q16 == cursor && v[0].start_q16 == start);
                CHECK(v[0].end_q16 == start + (int64_t)length * inc);
                CHECK(v[0].attack == atk && v[0].release == rel);
                CHECK(v[0].envelope == expected_envelope(age, length, atk, rel));
            } else empty(v);
        }
        g.trigger(); q15 pcm; g.process(&pcm, 1);
        Visual v[8]; capture(g, v);
        // New reverse grain at source zero reads age zero then dies immediately;
        // never repair this old DSP behavior in telemetry.
        empty(v);
        g.reset(); capture(g, v); empty(v);
        ++cases;
    }
    CHECK(cases == 144);
    std::printf("PASS %u forward/reverse/fractional speed/window/latched-ramp traces\n", cases);
}

int32_t expected_pdev(int32_t inc, int deviation) {
    uint32_t seed = 0xDEADBEEFu * 1664525u + 1013904223u;
    int spread = (int64_t)deviation * 16383 / 32767;
    int offset = (int)(seed % (uint32_t)(2 * spread + 1)) - spread;
    const uint32_t scaled = (uint32_t)(offset + 32767) * 256;
    const uint32_t index = scaled / 65534;
    const uint32_t rem = scaled % 65534;
    const auto knot = [](uint32_t i) { return (int32_t)std::floor(std::exp2(-2.0 + (double)i / 64.0) * 65536 + .5); };
    const int64_t ratio = knot(index) + (int64_t)(knot(index + 1) - knot(index)) * rem / 65534;
    int32_t speed = (int32_t)std::clamp<int64_t>((int64_t)inc * ratio / 65536, -262136, 262136);
    if (std::abs(speed) < 1024) speed = speed < 0 ? -1024 : 1024;
    return speed;
}
void pitch_deviation() {
    sample(65536);
    for (int pitch : {128, 8192, 32767, -128, -8192, -32767}) {
        for (int deviation : {1, 4096, 16384, 32767}) {
            GrainUnit g; quiet_config(g, pitch, 8192, 16384, 4096, 8192);
            set(g, GrainUnit::P_PITCH_DEV, deviation);
            const int32_t inc = expected_pdev(expected_speed(pitch), deviation);
            const uint32_t length = expected_length(8192, 65536, inc);
            const int64_t start = expected_start(16384, 65536, length, inc);
            q15 pcm[73]; g.process(pcm, 73);
            Visual v[8]; capture(g, v);
            CHECK(active(v) == 1);
            CHECK(v[0].position_q16 == start + (int64_t)73 * inc);
            CHECK(v[0].start_q16 == start && v[0].end_q16 == start + (int64_t)length * inc);
        }
    }
    std::puts("PASS 24 exact independent pdev/LUT trajectory cases");
}

void short_bounds_and_no_pcm_reads() {
    for (int nf : {0, 1, 2, 3, 255, 256, 257, 512}) for (int pitch : {128, 8192, 32767, -4097, -32767}) {
        sample((uint32_t)nf, (nf & 1) + 1);
        GrainUnit g; quiet_config(g, pitch, 32767, 16384, 32767, 32767);
        for (int i = 0; i < 514; ++i) {
            q15 pcm; g.process(&pcm, 1); Visual v[8]; capture(g, v);
            if (nf < 256) empty(v);
            for (const auto& x : v) if (x.active) {
                CHECK(x.position_q16 >= 0 && x.position_q16 / 65536 + 1 < nf);
                CHECK(x.age < x.length && x.length <= 16000);
            }
        }
    }
    sample(65536);
    GrainUnit g; quiet_config(g, 8192, 8192, 16384, 4096, 8192);
    q15 pcm; g.process(&pcm, 1);
    Sample& s = SampleBank::instance().slot(0);
    const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    const uintptr_t first = ((uintptr_t)s.data.data() + page - 1) / page * page;
    const uintptr_t last = ((uintptr_t)(s.data.data() + s.data.size())) / page * page;
    CHECK(last > first);
    CHECK(mprotect((void*)first, last - first, PROT_NONE) == 0);
    Visual v[8]; capture(g, v); CHECK(active(v) == 1);
    // All interior PCM pages are unreadable; only metadata may be inspected.
    s.logical_frames = 1; capture(g, v); empty(v);
    s.logical_frames = 0; capture(g, v); empty(v);
    s.logical_frames = 65536; capture(g, v); CHECK(active(v) == 1);
    CHECK(mprotect((void*)first, last - first, PROT_READ | PROT_WRITE) == 0);
    s.data.clear(); capture(g, v); empty(v);
    std::puts("PASS 40 short-source bounds cases/logical frames/inaccessible PCM pages");
}

uint32_t random(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
void exact_pcm_and_primary_heads() {
    uint32_t seed = 0xBEA70F11u;
    for (int fixture = 0; fixture < 24; ++fixture) {
        sample(fixture % 3 == 0 ? 1024 : 65536, fixture % 2 + 1);
        GrainUnit observed, untouched;
        observed.clean = untouched.clean = false;
        for (int block = 0; block < 640; ++block) {
            if (block % 13 == 0) {
                for (int p = 0; p < 11; ++p) {
                    int value = random(seed) & 32767;
                    if (p == GrainUnit::P_PITCH && (fixture & 1)) value = -value;
                    set(observed, p, value); set(untouched, p, value);
                }
                observed.clock_sync = untouched.clock_sync = (fixture % 4 == 0);
                observed.sync_division = untouched.sync_division = fixture % 4;
                observed.set_transport_bpm_q16((20 + block % 281) * 65536 + 123);
                untouched.set_transport_bpm_q16((20 + block % 281) * 65536 + 123);
                observed.control_tick(); untouched.control_tick();
            }
            if (block % 29 == 0) { observed.trigger(); untouched.trigger(); }
            if (block == 319) { observed.reset(); untouched.reset(); }
            Visual a[8], b[8];
            capture(observed, a); capture(observed, b);
            for (int i = 0; i < 8; ++i) CHECK(equal(a[i], b[i]));
            const int frames = 1 + random(seed) % 127;
            q15 audio_a[127], audio_b[127];
            observed.process(audio_a, frames); untouched.process(audio_b, frames);
            CHECK(std::memcmp(audio_a, audio_b, frames * sizeof(q15)) == 0);
            pcm_samples += frames;
            CHECK(observed.visual_spawned() == untouched.visual_spawned());
            CHECK(observed.visual_grains() == untouched.visual_grains());
            capture(observed, a);
            for (const auto& x : a) if (x.active) {
                CHECK(x.envelope == expected_envelope(x.age, x.length, x.attack, x.release));
                CHECK((x.end_q16 - x.start_q16) % x.length == 0);
                const int64_t inc = (x.end_q16 - x.start_q16) / x.length;
                CHECK(x.position_q16 == x.start_q16 + x.age * inc);
            }
        }
    }
    sample(65536);
    GrainUnit g; quiet_config(g, 8192, 32767, 16384, 0, 0);
    set(g, GrainUnit::P_VOICES, 0);
    q15 pcm[120]; g.process(pcm, 120); g.trigger(); g.process(pcm, 1);
    Visual v[8]; capture(g, v);
    CHECK(active(v) == 1 && v[0].age == 1);
    CHECK(g.visual_spawned() == 2); // old primary now has a declick tail, excluded.
    std::puts("PASS 24 deterministic twins/repeated captures/primary-only stolen-tail case");
}
} // namespace
int main() {
    defaults_and_empty();
    trajectories_and_latches();
    pitch_deviation();
    short_bounds_and_no_pcm_reads();
    exact_pcm_and_primary_heads();
    std::printf("gran visual core: 5/5 passed; checks=%llu captures=%llu exact-envelope=%llu twin-pcm=%llu; VisualGrain=%zu bytes; GrainUnit=%zu bytes\n",
        (unsigned long long)checks, (unsigned long long)captures,
        (unsigned long long)envelope_checks, (unsigned long long)pcm_samples,
        sizeof(Visual), sizeof(GrainUnit));
}
