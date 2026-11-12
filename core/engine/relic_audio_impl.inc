// Production adaptation of the selected frozen shape_runtime DSP; no model/UI state.
// Compile in exactly one TU. One construction allocation, readonly generated tables.
#include "relic_audio.h"
#include "relic_audio_tables.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace seance::relic {
namespace audio_detail {
static_assert(SAMPLE_RATE == 32000 && MECHANICS_FRAMES == 320, "selected relic table/control clock");
constexpr int kSampleRate = SAMPLE_RATE, kControlFrames = MECHANICS_FRAMES;
constexpr int kMaxHeads = MAX_AUDIO_HEADS, kHannSegments = 2048, kPanSegments = 256;
constexpr int kRampFrames = kSampleRate / 25, kDeclickFrames = 128;
constexpr int kSpaceCells = 41436, kClearCells = 64;
constexpr std::array<int, 4> kApLengths{{431, 811, 593, 1061}};
constexpr std::array<int, 8> kDelayLengths{{2473, 3167, 3761, 4481, 5233, 5801, 6521, 7103}};
constexpr std::array<float, 8> kLeft{{1, 1, 1, 1, -1, -1, -1, -1}};
constexpr std::array<float, 8> kRight{{1, -1, 1, -1, 1, -1, 1, -1}};
using Stereo = std::array<float, 2>;
float bounded(float x, float lo, float hi, float fallback = 0.f) noexcept {
    return std::isfinite(x) ? std::max(lo, std::min(hi, x)) : fallback;
}
float smooth(float x) noexcept { return x * x * (3.f - 2.f * x); }
void increment(uint32_t& x) noexcept { if (x != std::numeric_limits<uint32_t>::max()) ++x; }
AudioMap interpolate(const AudioMap& a, const AudioMap& b, float f) noexcept {
    return {a.body + f * (b.body - a.body), a.memory + f * (b.memory - a.memory),
        a.detail + f * (b.detail - a.detail), a.memory_window + f * (b.memory_window - a.memory_window),
        a.detail_window + f * (b.detail_window - a.detail_window),
        a.space_send + f * (b.space_send - a.space_send), a.space_return + f * (b.space_return - a.space_return)};
}
AudioMap shape_map(const Features& f, float depth) noexcept {
    const float c = .85f * depth, p = f.pull, k = f.compress, w = f.wave;
    AudioMap m{1.f - .35f * c * std::max(p, k),
        .015f + 4.f * c * p, .008f + 3.4f * c * k,
        4.8f + c * p, .18f - .095f * c * k,
        .20f + .48f * c * p - .14f * c * k,
        .16f + .12f * c * p - .13f * c * k};
    m.memory *= 1.f + .06f * c * w;
    m.detail *= 1.f - .06f * c * w;
    m.space_send = bounded(m.space_send + .025f * c * w, .06f, .68f);
    m.space_return = bounded(m.space_return + .008f * c * w, .03f, .28f);
    return m;
}
struct Tables {
    float envelope(float phase) const noexcept {
        const float p = bounded(phase, 0.f, 1.f) * kHannSegments;
        const int i = std::min(static_cast<int>(p), kHannSegments - 1);
        return audio_tables::hann[i] + (p - i) * (audio_tables::hann[i + 1] - audio_tables::hann[i]);
    }
    Stereo gains(float position) const noexcept {
        const float p = bounded((position + 1.f) * .5f, 0.f, 1.f) * kPanSegments;
        const int i = std::min(static_cast<int>(p), kPanSegments - 1);
        return {audio_tables::pan_left[i] + (p - i) * (audio_tables::pan_left[i + 1] - audio_tables::pan_left[i]),
            audio_tables::pan_right[i] + (p - i) * (audio_tables::pan_right[i + 1] - audio_tables::pan_right[i])};
    }
};
struct Filter {
    std::array<float, 4> state{};
    float tick(float x, float alpha, bool high) noexcept {
        for (float& z : state) { z += alpha * (x - z); x = high ? x - z : z; }
        return x;
    }
};
struct Space {
    // Exactly four allpasses and eight damped Hadamard FDN lines, 2.2-second decay.
    std::array<float, kSpaceCells> data{};
    std::array<int, 12> offset{}, length{}, at{};
    std::array<float, 8> damp{};
    int clear_at = kSpaceCells;
    Space() noexcept {
        int cursor = 0;
        for (int j = 0; j < 12; ++j) {
            offset[j] = cursor; length[j] = j < 4 ? kApLengths[j] : kDelayLengths[j - 4];
            cursor += length[j];
        }
    }
    void begin_clear() noexcept { clear_at = 0; at.fill(0); damp.fill(0.f); }
    bool clear_tick() noexcept {
        if (clear_at == kSpaceCells) return false;
        const int end = std::min(kSpaceCells, clear_at + kClearCells);
        for (; clear_at < end; ++clear_at) data[clear_at] = 0.f;
        return true;
    }
    void advance(int j) noexcept { if (++at[j] == length[j]) at[j] = 0; }
    float allpass(int j, float x) noexcept {
        float& cell = data[offset[j] + at[j]];
        const float out = cell - .6f * x;
        cell = x + .6f * out; advance(j); return out;
    }
    Stereo tick(Stereo x, bool audible) noexcept {
        if (clear_tick() || !audible) return {};
        x[0] = allpass(1, allpass(0, x[0]));
        x[1] = allpass(3, allpass(2, x[1]));
        constexpr float inv = .3535533905932737622f;
        std::array<float, 8> mixed{};
        Stereo out{};
        for (int j = 0; j < 8; ++j) {
            damp[j] += .8f * (data[offset[j + 4] + at[j + 4]] - damp[j]);
            mixed[j] = damp[j];
            out[0] += damp[j] * kLeft[j] * inv; out[1] += damp[j] * kRight[j] * inv;
        }
        for (int w = 1; w < 8; w *= 2) for (int k = 0; k < 8; k += 2 * w) for (int j = 0; j < w; ++j) {
            const float a = mixed[k + j], b = mixed[k + j + w];
            mixed[k + j] = a + b; mixed[k + j + w] = a - b;
        }
        for (int j = 0; j < 8; ++j) {
            data[offset[j + 4] + at[j + 4]] = audio_tables::delay_gain[j] * mixed[j] * inv
                + .32f * (x[0] * kLeft[j] + x[1] * kRight[j]) * inv;
            advance(j + 4);
        }
        return out;
    }
};
struct Meter {
    std::array<float, 7> sum{};
    Filter low{}, high{};
    void tick(Stereo out, Stereo dry, Stereo wet) noexcept {
        const float mid = (out[0] + out[1]) * .5f, side = (out[0] - out[1]) * .5f;
        const float l = low.tick(mid, audio_tables::meter_alpha[0], false);
        const float h = high.tick(mid, audio_tables::meter_alpha[1], true);
        const std::array<float, 7> e{{(out[0] * out[0] + out[1] * out[1]) * .5f, mid * mid, side * side,
            l * l, h * h, (dry[0] * dry[0] + dry[1] * dry[1]) * .5f, (wet[0] * wet[0] + wet[1] * wet[1]) * .5f}};
        for (int j = 0; j < 7; ++j) sum[j] += e[j];
    }
    Energy finish() noexcept {
        // Seven square roots per completed tick, not per sample/head; selected meter.
        Energy e{std::sqrt(sum[0] / kControlFrames), std::sqrt(sum[1] / kControlFrames),
            std::sqrt(sum[2] / kControlFrames), std::sqrt(sum[3] / kControlFrames),
            std::sqrt(sum[4] / kControlFrames), std::sqrt(sum[5] / kControlFrames),
            std::sqrt(sum[6] / kControlFrames)};
        sum.fill(0.f); return e;
    }
};
struct RNG {
    uint64_t state = 1;
    float uniform() noexcept {
        uint64_t x = state; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; state = x;
        return static_cast<float>((x * UINT64_C(2685821657736338717)) >> 40) * (1.f / 16777216.f);
    }
};
struct Head {
    uint32_t id = 0, origin = 0;
    uint8_t layer = 0;
    int age = 0, window = 0;
    float inverse_window = 0, start = 0, rate = 1, pan = 0;
    Stereo gains{};
    Filter filter{}, right_filter{}; // Mono executes the historical single filter arithmetic.
    float tick_energy = 0, energy = 0;
};
int window_frames(int layer, const AudioMap& map) noexcept {
    const float seconds = layer == 0 ? 2.4f : (layer == 1 ? map.memory_window : map.detail_window);
    return static_cast<int>(seconds * kSampleRate + .5f);
}
int hop_frames(int window) noexcept { return std::max(1, static_cast<int>(window * .125f + .5f)); }
q15 quantize(float value, uint32_t& clipped) noexcept {
    if (!std::isfinite(value)) { increment(clipped); return 0; }
    const float scaled = value * 32768.f;
    if (scaled >= 32767.5f) { increment(clipped); return 32767; }
    if (scaled <= -32768.5f) { increment(clipped); return -32768; }
    return static_cast<q15>(static_cast<int>(scaled + (scaled < 0.f ? -.5f : .5f)));
}
bool valid_source(const Source& s) noexcept {
    if (!s.data) return s.elements == 0 && s.frames == 0 && s.first == 0 && s.end == 0 && s.channels <= 2;
    if (s.channels != 1 && s.channels != 2) return false;
    if (s.first > s.end || s.end > s.frames) return false;
    constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (s.elements > maximum / sizeof(q15) || s.frames > maximum / s.channels / sizeof(q15)) return false;
    return s.elements >= static_cast<std::size_t>(s.frames) * s.channels;
}
bool same_source(const Source& a, const Source& b) noexcept {
    return a.data == b.data && a.elements == b.elements && a.frames == b.frames && a.first == b.first
        && a.end == b.end && a.generation == b.generation && a.channels == b.channels;
}
struct Publication { Features features{}; float depth = 1; bool playing = false, active = true; };
} // namespace audio_detail
using namespace audio_detail;

struct Audio::Impl {
    Tables tables{};
    Space space{};
    Meter meter{};
    Source source{};
    Publication control{}, pending{};
    bool pending_valid = false, installed_at_boundary = false;
    float old_bend = 0, current_bend = 0, old_wave = 0, current_wave = 0;
    AudioMap old_map{}, current_map{};
    Energy energy{};
    std::array<Head, kMaxHeads> heads{};
    std::array<RNG, 3> rng{{RNG{0x5ea11ceULL ^ 11}, RNG{0x5ea11ceULL ^ 29}, RNG{0x5ea11ceULL ^ 47}}};
    std::array<uint32_t, 3> center_phase{}, pan_phase{};
    std::array<uint64_t, 3> next{};
    uint64_t clock = 0;
    uint32_t next_id = 1, births = 0, retirements = 0, refused = 0, clipped = 0;
    int tick_position = 0, ramp = 0, count = 0, declick = 0;
    bool requested = false;
    Stereo last{}, held_last{};

    bool available() const noexcept { return source.data && source.end - source.first >= 2; }
    bool retained_region() const noexcept { return source.first == 0 && source.end == 10 * SAMPLE_RATE; }
    int source_window(int layer, const AudioMap& map) const noexcept {
        const int wanted = window_frames(layer, map);
        if (retained_region()) return wanted;
        const uint32_t span = source.end - source.first - 2;
        const float rate = layer == 0 ? 1.f : (layer == 1 ? .5f : 2.f);
        // Avoid converting huge logical capacities to float: only short regions clamp.
        if (span >= static_cast<uint32_t>(wanted * 2)) return wanted;
        return std::min(wanted, std::max(2, static_cast<int>(span / rate) + 1));
    }
    void install(Publication p) noexcept {
        control = p;
        const Features effective = p.active ? p.features : Features{};
        // A queued publication may have just been installed by finish_tick().
        // A 1kHz caller then publishes AGAIN at this same phase-zero boundary.
        // Replace the target, NOT the interpolation origin: no sample has reached
        // that queued target yet. This also coalesces repeated phase-zero setters.
        if (!installed_at_boundary) {
            old_map = current_map; old_bend = current_bend; old_wave = current_wave;
        }
        current_map = shape_map(effective, p.depth);
        current_bend = .18f * .85f * p.depth * effective.bend;
        current_wave = .040f * .85f * p.depth * effective.wave;
        installed_at_boundary = true;
    }
    void retire_source() noexcept {
        // No head owns a pointer. Invalidate every id BEFORE clearing the borrowed view.
        for (Head& h : heads) if (h.id) { h.id = 0; increment(retirements); }
        count = 0; requested = false; ramp = 0;
        held_last = last; declick = (last[0] != 0 || last[1] != 0) ? kDeclickFrames : 0;
        space.begin_clear();
        source = Source{};
    }
    bool birth(int layer, const AudioMap& map, int age = 0) noexcept {
        const int window = source_window(layer, map);
        next[layer] = clock + static_cast<uint64_t>(hop_frames(window));
        Head* head = nullptr;
        for (Head& candidate : heads) if (candidate.id == 0) { head = &candidate; break; }
        if (!head) { increment(refused); return false; } // Never steal a live head.
        Head g{};
        g.id = next_id++; if (next_id == 0) next_id = 1;
        g.layer = static_cast<uint8_t>(layer); g.window = window;
        g.age = age; g.inverse_window = 1.f / (window - 1);
        g.rate = layer == 0 ? 1.f : (layer == 1 ? .5f : 2.f);
        center_phase[layer] += UINT32_C(2654435769);
        pan_phase[layer] += UINT32_C(1779033703);
        const float center_u = static_cast<float>(center_phase[layer] >> 8) * (1.f / 16777216.f);
        const float pan_u = static_cast<float>(pan_phase[layer] >> 8) * (1.f / 16777216.f);
        const float uniform = rng[layer].uniform();
        const float shift = old_wave + (current_wave - old_wave) *
            (tick_position * (1.f / kControlFrames)) + (2.f * center_u - 1.f) * (layer == 1 ? .55f : .10f)
            + .006f * (2.f * uniform - 1.f);
        const float travel = g.rate * (window - 1);
        if (retained_region()) {
            // EXPLICIT retention route: same arithmetic grouping, 5s center / .8..9.2s.
            const float center = 5.f + old_wave + (current_wave - old_wave) *
                (tick_position * (1.f / kControlFrames)) + (2.f * center_u - 1.f) * (layer == 1 ? .55f : .10f)
                + .006f * (2.f * uniform - 1.f);
            g.start = bounded(center * kSampleRate - travel * .5f,
                .8f * kSampleRate, 9.2f * kSampleRate - 2.f - travel);
        } else {
            const uint32_t length = source.end - source.first;
            // Keep all per-sample float positions near <=320k, even with >2^24 frames
            // or a huge first offset. Integer origin preserves exact original PCM taps.
            const uint32_t origin_offset = length > 10 * SAMPLE_RATE ? length / 2 - 5 * SAMPLE_RATE : 0;
            g.origin = source.first + origin_offset;
            const float center = static_cast<float>(length / 2 - origin_offset) + .5f * (length & 1U);
            const float upper = static_cast<float>(length - 2 - origin_offset);
            g.start = bounded(center + shift * kSampleRate - travel * .5f, 0.f, std::max(0.f, upper - travel));
        }
        g.pan = (layer == 1 ? .66f : (layer == 2 ? .08f : .16f)) * (2.f * pan_u - 1.f);
        g.gains = tables.gains(g.pan);
        *head = g; ++count; increment(births); return true;
    }
    void start() noexcept {
        requested = true;
        const AudioMap map = interpolate(old_map, current_map, tick_position * (1.f / kControlFrames));
        if (count == 0) {
            for (int layer = 0; layer < 3; ++layer) {
                const int window = source_window(layer, map);
                for (int j = 0; j < 8; ++j) birth(layer, map, (window * j) / 8);
            }
        } else for (auto& deadline : next) if (deadline < clock) deadline = clock;
    }
    void finish_tick() noexcept {
        energy = meter.finish();
        for (Head& g : heads) if (g.id) { g.energy = g.tick_energy / kControlFrames; g.tick_energy = 0.f; }
        old_map = current_map; old_bend = current_bend; old_wave = current_wave;
        tick_position = 0; installed_at_boundary = false;
        if (pending_valid) { install(pending); pending_valid = false; }
    }
    // Returns two REAL source taps. Generic min-two-frame windows may reach the
    // endpoint (e.g. 2-frame 2x); clamp travel there, never fabricate/pad a sample.
    uint32_t position(const Head& g, float& fraction) const noexcept {
        const float p = g.start + g.rate * g.age;
        uint32_t local = static_cast<uint32_t>(p);
        fraction = p - local;
        const uint32_t last_local = source.end - 2 - g.origin;
        if (local > last_local) { local = last_local; fraction = 0; }
        return g.origin + local;
    }
    Stereo sample() noexcept {
        const bool play = available() && control.playing;
        if (play && !requested) start();
        if (!play) requested = false;
        if (play) { if (ramp < kRampFrames) ++ramp; }
        else if (ramp > 0 && --ramp == 0) space.begin_clear();
        const float gate = smooth(ramp * (1.f / kRampFrames));
        const AudioMap map = interpolate(old_map, current_map, tick_position * (1.f / kControlFrames));
        const std::array<float, 3> mix{{map.body, map.memory, map.detail}};
        if (play) for (int layer = 0; layer < 3; ++layer) if (clock >= next[layer]) birth(layer, map);
        Stereo dry{};
        for (Head& g : heads) if (g.id) {
            float fraction = 0;
            const uint32_t frame = position(g, fraction);
            const std::size_t index = static_cast<std::size_t>(frame) * source.channels;
            const float a = static_cast<float>(source.data[index]) * (1.f / 32768.f);
            const float b = static_cast<float>(source.data[index + source.channels]) * (1.f / 32768.f);
            const float envelope = tables.envelope(g.age * g.inverse_window);
            const float filtered = g.filter.tick((a + fraction * (b - a)) * envelope * .25f,
                audio_tables::filter_alpha[g.layer], g.layer == 2);
            const float amplitude = .80f * mix[g.layer] * filtered;
            float right_amplitude = amplitude;
            if (source.channels == 2) {
                const float ra = static_cast<float>(source.data[index + 1]) * (1.f / 32768.f);
                const float rb = static_cast<float>(source.data[index + source.channels + 1]) * (1.f / 32768.f);
                const float rf = g.right_filter.tick((ra + fraction * (rb - ra)) * envelope * .25f,
                    audio_tables::filter_alpha[g.layer], g.layer == 2);
                right_amplitude = .80f * mix[g.layer] * rf;
            }
            const float bend = old_bend + (current_bend - old_bend) * (tick_position * (1.f / kControlFrames));
            const Stereo gains = tables.gains(g.pan + bend);
            const Stereo isolated{{amplitude * gains[0], right_amplitude * gains[1]}};
            g.tick_energy += (isolated[0] * isolated[0] + isolated[1] * isolated[1]) * .5f * gate * gate;
            dry[0] += isolated[0]; dry[1] += isolated[1];
            if (++g.age == g.window) { g.id = 0; --count; increment(retirements); }
        }
        Stereo wet = space.tick({dry[0] * map.space_send, dry[1] * map.space_send}, ramp != 0);
        Stereo value{};
        for (int j = 0; j < 2; ++j) {
            dry[j] *= gate; wet[j] *= map.space_return * gate; value[j] = dry[j] + wet[j];
            if (declick) { const float tail = held_last[j] * (declick * (1.f / kDeclickFrames)); dry[j] += tail; value[j] += tail; }
        }
        if (declick) --declick;
        const Stereo physical{{bounded(value[0], -1.f, 32767.f / 32768.f), bounded(value[1], -1.f, 32767.f / 32768.f)}};
        last = physical; meter.tick(physical, dry, wet);
        ++clock;
        if (++tick_position == kControlFrames) finish_tick();
        return value;
    }
};

Audio::Audio() noexcept : impl_(new (std::nothrow) Impl{}) {}
Audio::~Audio() noexcept { delete impl_; }
bool Audio::ready() const noexcept { return impl_ != nullptr; }
bool Audio::set_source(const Source& source) noexcept {
    if (!impl_ || !valid_source(source)) return false;
    if (same_source(impl_->source, source)) return true;
    // Initial install needs no clear; construction already owns an empty tank.
    if (impl_->source.data || impl_->count || impl_->ramp || impl_->declick) impl_->retire_source();
    impl_->source = source;
    return true;
}
void Audio::detach_source() noexcept { if (impl_) impl_->retire_source(); }
bool Audio::set_shape(Features f, float depth, bool playing, bool active) noexcept {
    if (!impl_ || !std::isfinite(depth) || depth <= 0.f || depth > 1.f || !std::isfinite(f.pull)
        || !std::isfinite(f.compress) || !std::isfinite(f.bend) || !std::isfinite(f.wave)) return false;
    f.pull = bounded(f.pull, 0.f, 1.f); f.compress = bounded(f.compress, 0.f, 1.f);
    f.bend = bounded(f.bend, -1.f, 1.f); f.wave = bounded(f.wave, -1.f, 1.f);
    const Publication p{f, depth, playing, active};
    if (impl_->tick_position == 0) { impl_->install(p); impl_->pending_valid = false; }
    else { impl_->pending = p; impl_->pending_valid = true; }
    return true;
}
void Audio::render(q15* stereo, int frames) noexcept {
    if (!stereo || frames <= 0) return;
    for (int i = 0; i < frames; ++i) {
        Stereo value{};
        if (impl_) value = impl_->sample();
        const std::size_t at = static_cast<std::size_t>(i) * 2;
        if (impl_) { stereo[at] = quantize(value[0], impl_->clipped); stereo[at + 1] = quantize(value[1], impl_->clipped); }
        else stereo[at] = stereo[at + 1] = 0;
    }
}
void Audio::snapshot(Visual& out) const noexcept {
    // Direct value initialization of caller storage: no 3KiB snapshot temporary on ARM.
    ::new (static_cast<void*>(&out)) Visual{};
    if (!impl_) return;
    out.ready = true; out.sample_clock = impl_->clock; out.features = impl_->control.features;
    out.map = impl_->current_map; out.energy = impl_->energy; out.depth = impl_->control.depth;
    out.playing = impl_->control.playing; out.active = impl_->control.active;
    out.control_pending = impl_->pending_valid; out.control_phase = static_cast<uint16_t>(impl_->tick_position);
    out.ramp_frames = static_cast<uint16_t>(impl_->ramp); out.declick_frames = static_cast<uint16_t>(impl_->declick);
    out.births = impl_->births; out.retirements = impl_->retirements;
    out.refused_births = impl_->refused; out.clipped_samples = impl_->clipped;
    out.source_frames = impl_->source.frames; out.source_first = impl_->source.first; out.source_end = impl_->source.end;
    out.source_channels = impl_->source.channels; out.source_generation = impl_->source.generation;
    out.audio_available = impl_->available(); out.running = impl_->ramp != 0 || impl_->declick != 0;
    for (const Head& g : impl_->heads) if (g.id) {
        Reader& r = out.readers[out.reader_count++];
        r.id = g.id; r.source_generation = impl_->source.generation; r.layer = g.layer;
        r.phase = g.age * g.inverse_window; r.envelope = impl_->tables.envelope(r.phase); r.energy = g.energy;
        r.pan = g.pan + impl_->old_bend + (impl_->current_bend - impl_->old_bend) * (impl_->tick_position * (1.f / kControlFrames));
        r.rate = g.rate;
        float fraction = 0; r.frame = impl_->position(g, fraction);
        // Historical float seconds exactly, integer frame also remains precise for huge sources.
        r.position = impl_->retained_region() ? (g.start + g.rate * g.age) / kSampleRate
            : (static_cast<float>(r.frame) + fraction) / kSampleRate;
        r.window = static_cast<float>(g.window) / kSampleRate;
    }
}
void Audio::reset() noexcept {
    if (!impl_) return;
    const Source source = impl_->source;
    impl_->retire_source(); impl_->source = source;
    impl_->control = Publication{}; impl_->pending = Publication{};
    impl_->pending_valid = impl_->installed_at_boundary = false;
    impl_->old_map = impl_->current_map = AudioMap{};
    impl_->old_bend = impl_->current_bend = impl_->old_wave = impl_->current_wave = 0;
    impl_->meter = Meter{}; impl_->energy = Energy{};
    impl_->rng = {{RNG{0x5ea11ceULL ^ 11}, RNG{0x5ea11ceULL ^ 29}, RNG{0x5ea11ceULL ^ 47}}};
    impl_->center_phase.fill(0); impl_->pan_phase.fill(0); impl_->next.fill(0);
    impl_->clock = 0; impl_->next_id = 1; impl_->births = impl_->retirements = impl_->refused = impl_->clipped = 0;
    impl_->tick_position = 0; impl_->declick = 0; impl_->last = impl_->held_last = Stereo{};
}
} // namespace seance::relic
