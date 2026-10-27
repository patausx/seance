// Focused standalone ChorusUnit DSP check; no engine/UI harness or device needed.
// g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore
//   tools/chorus_dsp_test.cpp core/engine/units.cpp -Wl,--gc-sections -o /tmp/chorus_dsp_test
// /tmp/chorus_dsp_test
// Sanitized: replace -O2 with -O1 -g -fsanitize=address,undefined
// -fno-omit-frame-pointer (same sources and section-GC flags).
#include "engine/units.h"
#include "engine/chorus_motion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using seance::ChorusUnit;
using seance::q15;
namespace motion = seance::chorus_motion;
namespace {
constexpr double TAU = 6.283185307179586476925286766559;
int assertions = 0;
void require(bool good, const char* expression, int line) {
    ++assertions;
    if (!good) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        std::exit(1);
    }
}
#define CHECK(x) require((x), #x, __LINE__)

void parameters(ChorusUnit& u, int rate, int depth, int mix) {
    const int raw[] = {rate, depth, mix};
    for (int i = 0; i < 3; ++i)
        u.params[i].effective = u.params[i].base = (q15)raw[i];
}
uint32_t random_word(uint32_t& seed) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
q15 stimulus(int i, uint32_t& seed) {
    if (i % 11 == 0) return -32768;
    if (i % 11 == 1) return 32767;
    return (q15)((int)(random_word(seed) & 65535u) - 32768);
}

// Independent floating-point oracle: construct sine knots analytically, then
// model quantization with floor, NOT production table/bit shifts/lerp helpers.
uint32_t reference_sine(uint32_t phase) {
    const double p = (double)phase / 16777216.0;
    const int index = (int)std::floor(p);
    const double fraction = std::floor((p - index) * 65536.0) / 65536.0;
    const double a = std::floor(32768.0 * (1.0 + std::sin(TAU * index / 256.0)) + .5);
    const double b = std::floor(32768.0 * (1.0 + std::sin(TAU * (index + 1) / 256.0)) + .5);
    return (uint32_t)std::floor(a + (b - a) * fraction);
}
struct Reference {
    uint32_t phase = 0;
    uint32_t delays[2] = {96u * 65536u, 96u * 65536u};
    std::vector<q15> history;
    int at(int64_t i) const { return i < 0 ? 0 : history[(size_t)i]; }
    q15 sample(q15 dry, int rate, int depth, int mix) {
        rate = std::max(0, std::min(32767, rate));
        depth = std::max(0, std::min(32767, depth));
        mix = std::max(0, std::min(32767, mix));
        phase = (uint32_t)((uint64_t)phase + 5000u + (uint32_t)rate * 20u);
        const uint32_t excursion = 16u + (uint32_t)(depth * 300 / 32768);
        int voices[2];
        for (int voice = 0; voice < 2; ++voice) {
            const uint32_t p = (uint32_t)((uint64_t)phase + (uint64_t)voice * 1073741824u);
            delays[voice] = 96u * 65536u + excursion * reference_sine(p);
            const double position = (double)history.size() - (double)delays[voice] / 65536.0;
            const int64_t lower = (int64_t)std::floor(position);
            const double fraction = position - (double)lower;
            voices[voice] = (int)std::floor(at(lower) * (1.0 - fraction) + at(lower + 1) * fraction);
        }
        const int wet = (voices[0] + voices[1]) / 2; // deliberate truncation to zero
        const double weight = mix == 32767 ? 1.0 : (double)mix / 32768.0;
        history.push_back(dry);
        return (q15)(dry + (int)std::floor((wet - dry) * weight));
    }
};

void mappings_and_defaults() {
    static_assert(ChorusUnit::P_RATE == 0 && ChorusUnit::P_DEPTH == 1 && ChorusUnit::P_MIX == 2);
    ChorusUnit u;
    CHECK(u.ready()); CHECK(u.num_params == 3);
    CHECK(u.params[0].base == 7000 && u.params[1].base == 12000 && u.params[2].base == 12000);
    CHECK(std::strcmp(u.params[0].name, "RATE") == 0);
    CHECK(std::strcmp(u.params[1].name, "DPTH") == 0);
    CHECK(std::strcmp(u.params[2].name, "MIX") == 0);
    CHECK(!u.visual_ready());
    for (int raw = 0; raw <= 32767; ++raw) {
        CHECK(motion::rate_increment(raw) == 5000u + (uint32_t)raw * 20u);
        CHECK(motion::depth_samples(raw) == 16u + (uint32_t)(raw * 300 / 32768));
    }
    CHECK(motion::rate_increment(32767) == 660340u);
    CHECK(motion::depth_samples(32767) == 315u);
    CHECK(motion::rate_increment(-32768) == 5000u);
    CHECK(motion::depth_samples(-32768) == 16u);
    CHECK(motion::unipolar_raw(65535) == 32767u);
    std::puts("ok mappings/defaults/indices");
}

void sine_and_quarter_cycle() {
    CHECK(motion::sine_unipolar_q16(0) == 32768u);
    CHECK(motion::sine_unipolar_q16(0x40000000u) == 65536u);
    CHECK(motion::sine_unipolar_q16(0x80000000u) == 32768u);
    CHECK(motion::sine_unipolar_q16(0xc0000000u) == 0u);
    double worst = 0;
    uint32_t seed = 0x984293u;
    for (int i = 0; i < 262144; ++i) {
        // Uniform full-cycle coverage with low phase bits varied independently.
        const uint32_t p = ((uint32_t)i << 14) | (random_word(seed) & 16383u);
        for (int voice = 0; voice < 2; ++voice) {
            const uint32_t phase = p + (voice ? 0x40000000u : 0u);
            const uint32_t sine = motion::sine_unipolar_q16(phase);
            CHECK(sine <= 65536u); CHECK(sine == reference_sine(phase));
            const double exact = 32768.0 * (1.0 + std::sin(TAU * (double)phase / 4294967296.0));
            worst = std::max(worst, std::fabs(exact - (double)sine));
            CHECK(std::fabs(exact - (double)sine) <= 4.0);
            for (uint32_t depth : {16u, 125u, 315u}) {
                const uint32_t delay = motion::delay_q16(phase, depth);
                CHECK(delay >= 96u * 65536u && delay <= (96u + depth) * 65536u);
            }
        }
    }
    CHECK(motion::sine_unipolar_q16(UINT32_MAX) == reference_sine(UINT32_MAX));
    std::printf("ok sine/quarter-cycle/bounds; worst normalized q16 error %.6f (<0.020 sample at max depth)\n", worst);
}

void reference_pcm_and_extrema() {
    ChorusUnit u;
    Reference ref;
    uint32_t seed = 0x736A31u;
    int count = 0;
    for (int rate : {0, 7000, 32767}) for (int depth : {0, 12000, 32767})
    for (int mix : {0, 1, 12000, 16384, 32766, 32767}) {
        parameters(u, rate, depth, mix);
        for (int i = 0; i < 1024; ++i) {
            const q15 input = stimulus(count++, seed);
            q15 output = input;
            u.process(&output, 1);
            CHECK(output == ref.sample(input, rate, depth, mix));
            CHECK(u.visual_ready());
            CHECK(u.visual_phase() == (ref.phase / 65536u));
            CHECK(u.visual_delay_q16(0) == ref.delays[0]);
            CHECK(u.visual_delay_q16(1) == ref.delays[1]);
            CHECK(u.visual_delay() == ref.delays[0] / 65536u);
            if (mix == 0) CHECK(output == input);
        }
    }
    // Defensive unipolar handling, including the most negative q15 value.
    parameters(u, -32768, -32768, -32768);
    q15 sample = -32768;
    u.process(&sample, 1);
    CHECK(sample == ref.sample(-32768, -32768, -32768, -32768));
    CHECK(u.visual_delay_q16(-100) == u.visual_delay_q16(0));
    CHECK(u.visual_delay_q16(100) == u.visual_delay_q16(1));
    std::printf("ok independent PCM/fractional reads/mix extrema/telemetry; %d samples\n", count + 1);
}

void wet_normalization() {
    // Fully filled constant buffers must preserve both full-scale polarities.
    for (q15 value : {(q15)-32768, (q15)-1, (q15)0, (q15)1, (q15)32767}) {
        ChorusUnit u;
        parameters(u, 32767, 32767, 32767);
        std::vector<q15> pcm(10000, value);
        u.process(pcm.data(), (int)pcm.size());
        for (size_t i = 412; i < pcm.size(); ++i) CHECK(pcm[i] == value);
    }
    // Opposite dry/wet extremes exercise the maximum signed mix difference.
    ChorusUnit u;
    parameters(u, 0, 0, 32767);
    std::vector<q15> filled(2048, -32768);
    u.process(filled.data(), (int)filled.size());
    q15 sample = 32767; u.process(&sample, 1); CHECK(sample == -32768);
    filled.assign(2048, 32767); u.process(filled.data(), (int)filled.size());
    sample = -32768; u.process(&sample, 1); CHECK(sample == 32767);
    std::puts("ok exact normalized full-wet/full-scale endpoints");
}

void splitting_ring_phase_and_zero_frames() {
    ChorusUnit whole, split;
    parameters(whole, 32767, 32767, 12000);
    parameters(split, 32767, 32767, 12000);
    std::vector<q15> a(50000), b;
    uint32_t seed = 0x181FF71u;
    for (size_t i = 0; i < a.size(); ++i) a[i] = stimulus((int)i, seed);
    b = a;
    whole.process(a.data(), (int)a.size());
    size_t offset = 0;
    while (offset < b.size()) {
        const int n = (int)std::min(b.size() - offset, (size_t)(1u + random_word(seed) % 4097u));
        split.process(b.data() + offset, n);
        const uint16_t phase = split.visual_phase();
        const uint32_t d0 = split.visual_delay_q16(0), d1 = split.visual_delay_q16(1);
        q15 sentinel = 27123;
        split.process(nullptr, 0); split.process(&sentinel, -1);
        CHECK(sentinel == 27123); CHECK(split.visual_ready());
        CHECK(split.visual_phase() == phase);
        CHECK(split.visual_delay_q16(0) == d0 && split.visual_delay_q16(1) == d1);
        offset += (size_t)n;
    }
    CHECK(a == b);
    CHECK(whole.visual_phase() == split.visual_phase());
    CHECK(whole.visual_delay_q16(0) == split.visual_delay_q16(0));
    CHECK(whole.visual_delay_q16(1) == split.visual_delay_q16(1));
    const uint32_t expected_phase = (uint32_t)(50000ull * 660340u);
    CHECK(whole.visual_phase() == expected_phase / 65536u);
    CHECK(whole.visual_delay_q16(0) == motion::delay_q16(expected_phase, 315));
    CHECK(whole.visual_delay_q16(1) == motion::delay_q16(expected_phase + 0x40000000u, 315));
    std::puts("ok block splitting/24 ring wraps/7 phase wraps/zero-frame no-op");
}

void silence_and_reset() {
    ChorusUnit u;
    u.process(nullptr, 0); CHECK(!u.visual_ready());
    parameters(u, 32767, 32767, 32767);
    std::vector<q15> pcm(8192, 0);
    u.process(pcm.data(), (int)pcm.size());
    CHECK(u.visual_ready());
    for (q15 sample : pcm) CHECK(sample == 0);
    pcm.assign(8192, 32767); u.process(pcm.data(), (int)pcm.size());
    u.reset();
    CHECK(!u.visual_ready()); CHECK(u.visual_phase() == 0); CHECK(u.visual_delay() == 96);
    CHECK(u.visual_delay_q16(0) == 96u * 65536u && u.visual_delay_q16(1) == 96u * 65536u);
    for (int p = 0; p < 3; ++p) CHECK(u.visual_effective(p) == 0);
    parameters(u, 12345, 23456, 32767);
    u.process(nullptr, 0); CHECK(!u.visual_ready());
    ChorusUnit fresh; parameters(fresh, 12345, 23456, 32767);
    pcm.assign(8192, 0);
    std::vector<q15> other = pcm;
    u.process(pcm.data(), (int)pcm.size()); fresh.process(other.data(), (int)other.size());
    CHECK(pcm == other);
    for (q15 sample : pcm) CHECK(sample == 0);
    CHECK(u.visual_phase() == fresh.visual_phase());
    CHECK(u.visual_delay_q16(0) == fresh.visual_delay_q16(0));
    CHECK(u.visual_delay_q16(1) == fresh.visual_delay_q16(1));
    CHECK(u.visual_effective(0) == 12345);
    CHECK(u.visual_effective(1) == 23456);
    CHECK(u.visual_effective(2) == 32767);
    CHECK(u.visual_effective(-100) == 12345 && u.visual_effective(100) == 32767);
    // Changing effective params cannot fabricate a new last-processed delay.
    const uint32_t delay = u.visual_delay_q16(0);
    parameters(u, 0, 0, 0); u.process(nullptr, 0);
    CHECK(u.visual_delay_q16(0) == delay);
    u.process(nullptr, -1);
    CHECK(u.visual_effective(0) == 12345);
    CHECK(u.visual_effective(1) == 23456);
    CHECK(u.visual_effective(2) == 32767);
    parameters(u, -32768, -1, -32768);
    q15 sample = 0; u.process(&sample, 1);
    for (int p = 0; p < 3; ++p) CHECK(u.visual_effective(p) == 0);
    std::puts("ok silence/reset/not-yet-processed telemetry");
}
} // namespace

int main() {
    mappings_and_defaults();
    sine_and_quarter_cycle();
    reference_pcm_and_extrema();
    wet_normalization();
    splitting_ring_phase_and_zero_frames();
    silence_and_reset();
    std::printf("chorus dsp: 6/6 passed (%d assertions)\n", assertions);
}
