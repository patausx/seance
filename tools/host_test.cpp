// host-side smoke test: build a chain, drive it with sigils, render, sanity-check.
//   g++ -O2 -std=gnu++17 -I core tools/host_test.cpp core/engine/*.cpp \
//       core/synth/sampler.cpp core/synth/wav_loader.cpp \
//       -o /tmp/seance_test && /tmp/seance_test
#include "ghost_tuning_test.h"
#include "../core/engine/engine.h"
#include "../core/engine/units.h"
#include "../core/engine/play_window.h"
#include "../core/engine/patch_io.h"
#include "../core/engine/project_session.h"
#include "../core/engine/sigil_transform.h"
#include "../core/engine/pad_io.h"
#include "../core/synth/wav_loader.h"
#include "../core/synth/wav_writer.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace seance;
using trackr::synth::SampleBank;

class TriggerCounterUnit final : public Unit {
public:
    int triggers = 0;
    uint64_t* transport_frame = nullptr;
    uint64_t trigger_frames[32] = {};
    TriggerCounterUnit() { kind = UnitKind::Osc; }
    void process(q15* buf, int frames) override { std::memset(buf, 0, (size_t)frames * sizeof(q15)); }
    void trigger() override {
        if (triggers < 32 && transport_frame) trigger_frames[triggers] = *transport_frame;
        ++triggers;
    }
};

// A deterministic unit isolates audition expiry from oscillator phase, metering,
// envelopes and the existing block-dependent master limiter.
class AuditionProbeUnit final : public Unit {
public:
    int triggers = 0;
    q15 triggered_pitch = 0, triggered_level = 0;
    AuditionProbeUnit() {
        kind = UnitKind::Osc;
        add_param("PITCH", 3000);
        add_param("GAIN", 6000);
    }
    void trigger() override {
        ++triggers;
        triggered_pitch = params[0].effective;
        triggered_level = params[1].effective;
    }
    void process(q15* out, int frames) override {
        for (int i = 0; i < frames; ++i) out[i] = params[0].effective / 8;
    }
};

static bool test_sequence_audition() {
#define AUDITION_CHECK(x, message) do { if (!(x)) { printf("FAIL: audition %s\n", message); return false; } } while (0)
    static_assert(sizeof(EngineUiSnapshot) <= 3840, "hot snapshot must remain bounded");
    static q15 audio[20000 * 2], chunked[20000 * 2];
    {
        Engine e;
        auto* probe = new AuditionProbeUnit();
        e.replace_unit(0, 0, probe);
        e.set_step_lock(0, 20, 0, 0, 21000);
        e.set_step_lock(0, 20, 0, 1, 13000);
        e.pattern.steps[0][20].set_condition(SeqStep::Condition::Half);
        e.pattern.steps[0][20].set_retrig_count(4);
        const Pattern saved = e.pattern;
        AUDITION_CHECK(e.audition_sequence_step(0, 20), "stopped accepted");
        AUDITION_CHECK(probe->triggers == 1 && probe->triggered_pitch == 21000 &&
                       probe->triggered_level == 13000, "locks resolved before one trigger");
        AUDITION_CHECK(!e.transport_playing && e.transport_frame == 0 && e.sequence_tick == 0 &&
                       !e.sequencer_enabled && !std::memcmp(&saved, &e.pattern, sizeof saved),
                       "no pattern, trig, condition, sequencer or transport edits");
        AUDITION_CHECK(probe->params[0].base == 3000 && probe->params[1].base == 6000 &&
                       !probe->params[0].step_lock_override, "independent runtime layer");
        e.render(audio, Engine::SEQUENCE_AUDITION_FRAMES - 1);
        AUDITION_CHECK(probe->params[0].audition_override && probe->params[0].effective == 21000,
                       "holds to exact frame before expiry");
        e.render(audio, 1);
        AUDITION_CHECK(!probe->params[0].audition_override && probe->params[0].effective == 3000 &&
                       probe->params[1].effective == 6000, "expires at exactly 250ms");
        AUDITION_CHECK(e.audition_sequence_step(0, 20), "repeat first");
        e.render(audio, 7901);
        e.set_step_lock(0, 20, 0, 0, 19000);
        AUDITION_CHECK(e.audition_sequence_step(0, 20), "repeat replaces");
        e.render(audio, 100);
        AUDITION_CHECK(probe->params[0].audition_override && probe->params[0].effective == 19000,
                       "old deadline cannot erase newer edit");
        // A simultaneous persistent edit is authoritative on cancellation.
        probe->params[0].base = 4000;
        e.cancel_sequence_audition();
        e.cancel_sequence_audition();
        AUDITION_CHECK(probe->params[0].effective == 4000, "idempotent cancel reveals new base");
        probe->params[0].performance = 5000;
        probe->params[0].performance_override = true;
        e.audition_sequence_step(0, 20);
        AUDITION_CHECK(probe->triggered_pitch == 5000, "performance priority preserved");
        e.cancel_sequence_audition();
        AUDITION_CHECK(probe->params[0].control_source() == 5000, "cancel preserves performance");
        probe->params[0].performance_override = false;
        e.chains[0].mute = true;
        const int triggers = probe->triggers;
        AUDITION_CHECK(!e.audition_sequence_step(0, 20) && probe->triggers == triggers, "mute refused");
        e.chains[0].mute = false;
        probe->bypass = true;
        AUDITION_CHECK(!e.audition_sequence_step(0, 20), "all bypass refused");
        auto* bypass_witness = new AuditionProbeUnit();
        e.replace_unit(0, 1, bypass_witness);
        AUDITION_CHECK(e.audition_sequence_step(0, 20) && probe->triggers == triggers &&
                       bypass_witness->triggers == 1, "bypassed trigger and locks untouched");
        AUDITION_CHECK(!e.audition_sequence_step(-1, 0) && !e.audition_sequence_step(4, 0) &&
                       !e.audition_sequence_step(0, -1) && !e.audition_sequence_step(0, 64),
                       "invalid coordinates refused");
        probe->bypass = false;
        auto& bad = e.pattern.steps[0][21];
        bad.lock_count = 255;
        for (auto& lock : bad.locks) lock = {255, 255, 32767};
        AUDITION_CHECK(e.audition_sequence_step(0, 21) && probe->triggered_pitch == 4000,
                       "malformed lock bounds ignored");
        e.cancel_sequence_audition();
    }
    {
        Engine a, b;
        auto* pa = new AuditionProbeUnit(); auto* pb = new AuditionProbeUnit();
        a.replace_unit(0, 0, pa); b.replace_unit(0, 0, pb);
        a.set_step_lock(0, 0, 0, 0, 16000); b.set_step_lock(0, 0, 0, 0, 16000);
        // Start off the normal control boundary and expire across arbitrary
        // caller chunks. Constant low-level output keeps limiter exactly unity.
        a.render(audio, 7); b.render(chunked, 7);
        a.audition_sequence_step(0, 0); b.audition_sequence_step(0, 0);
        a.render(audio, 8017);
        int done = 0;
        while (done < 8017) {
            int n = 1 + (done * 17 % 93); if (n > 8017 - done) n = 8017 - done;
            b.render(chunked + done * 2, n); done += n;
        }
        AUDITION_CHECK(!std::memcmp(audio, chunked, 8017 * 2 * sizeof(q15)) &&
                       !pa->params[0].audition_override && !pb->params[0].audition_override,
                       "sample-exact expiry invariant under render chunking");
    }
    {
        Engine e;
        auto* p = new AuditionProbeUnit(); e.replace_unit(0, 0, p);
        e.set_step_lock(0, 0, 0, 0, 10000);
        e.set_step_lock(0, 0, 0, 1, 12000);
        e.set_step_lock(0, 1, 0, 0, 11000);
        e.set_step_lock(0, 1, 0, 1, 14000);
        e.set_step_lock(0, 20, 0, 0, 22000);
        e.set_sequencer_enabled(true); e.transport_play();
        e.render(audio, 4000);
        AUDITION_CHECK(p->params[0].step_lock_override && p->params[0].effective == 10000,
                       "scheduled lock setup");
        const uint64_t frame = e.transport_frame, tick = e.sequence_tick;
        e.audition_sequence_step(0, 20);
        AUDITION_CHECK(e.transport_playing && e.transport_frame == frame && e.sequence_tick == tick &&
                       p->params[0].step_lock == 10000 && p->triggered_pitch == 22000 &&
                       p->triggered_level == 6000, "preview uses selected base not preceding event locks");
        e.cancel_sequence_audition();
        AUDITION_CHECK(p->params[0].effective == 10000 && p->params[1].effective == 12000,
                       "cancel restores intact current playback locks");
        e.audition_sequence_step(0, 20);
        e.render(audio, 4000);
        AUDITION_CHECK(!p->params[0].audition_override && p->params[0].effective == 11000 &&
                       p->params[1].effective == 14000 && e.transport_frame == frame + 4000,
                       "next scheduled event reclaims parameters at original clock");
        e.audition_sequence_step(0, 20); e.transport_stop();
        AUDITION_CHECK(!p->params[0].audition_override, "stop cancels");
        e.audition_sequence_step(0, 20); e.transport_play();
        AUDITION_CHECK(!p->params[0].audition_override, "play cancels");
        e.audition_sequence_step(0, 20); e.transport_reset();
        AUDITION_CHECK(!p->params[0].audition_override, "reset cancels");
        e.transport_stop();
        e.audition_sequence_step(0, 20);
        auto* replacement = new AuditionProbeUnit();
        Unit* old = e.replace_unit(0, 0, replacement);
        AUDITION_CHECK(!old->params[0].audition_override, "replacement releases before pointer detaches");
        delete old; e.render(audio, 8017);
        AUDITION_CHECK(replacement->params[0].effective == 3000, "no stale pointer cleanup");
        e.audition_sequence_step(0, 20);
        Engine other; e.swap_patch_state(other);
        AUDITION_CHECK(!replacement->params[0].audition_override, "project swap cancels before ownership move");
    }
    {
        // Preview must not consume the private probability RNG or pending event
        // lookahead. Compare every subsequent trigger timestamp with a twin.
        Engine a, b;
        auto* pa = new TriggerCounterUnit(); auto* pb = new TriggerCounterUnit();
        a.replace_unit(0, 0, pa); b.replace_unit(0, 0, pb);
        pa->transport_frame = &a.transport_frame; pb->transport_frame = &b.transport_frame;
        for (int step = 0; step < 16; ++step) {
            a.pattern.steps[0][step].flags = b.pattern.steps[0][step].flags = SeqStep::Trig;
            a.pattern.steps[0][step].set_condition(SeqStep::Condition::Half);
            b.pattern.steps[0][step].set_condition(SeqStep::Condition::Half);
        }
        a.set_sequencer_enabled(true); b.set_sequencer_enabled(true);
        a.transport_play(); b.transport_play();
        a.render(audio, 33); b.render(chunked, 33);
        for (int i = 0; i < 7; ++i) a.audition_sequence_step(0, i);
        a.cancel_sequence_audition(); pa->triggers = 0;
        for (int i = 0; i < 4; ++i) { a.render(audio, 16000); b.render(chunked, 16000); }
        AUDITION_CHECK(pa->triggers == pb->triggers && a.sequence_tick == b.sequence_tick &&
                       a.transport_frame == b.transport_frame &&
                       !std::memcmp(pa->trigger_frames, pb->trigger_frames, pa->triggers * sizeof(uint64_t)),
                       "preview does not consume random condition schedule");
    }
    {
        // Real OSC/VCA, FM and PLAY: non-silent preview without starting transport.
        for (const UnitKind kind : {UnitKind::Osc, UnitKind::Fm, UnitKind::SamplePlayer}) {
            Engine e; Unit* source = make_unit(kind); e.replace_unit(0, 0, source);
            int pitch = 0;
            if (kind == UnitKind::SamplePlayer) {
                auto& sample = SampleBank::instance().slot(61);
                sample.channels = 1;
                sample.data.assign(4096, 8000);
                static_cast<PlayUnit*>(source)->sample_slot = 61;
                e.set_step_lock(0, 3, 0, PlayUnit::P_LEN, 32767);
            } else if (kind == UnitKind::Osc) {
                auto* vca = make_unit(UnitKind::Vca); e.replace_unit(0, 1, vca);
                e.set_step_lock(0, 3, 1, VcaUnit::P_ATTACK, 0);
            }
            const q15 base = source->params[pitch].base;
            e.set_step_lock(0, 3, 0, pitch, 14000);
            const char* before_path = "/tmp/seance_audition_before.snc";
            const char* during_path = "/tmp/seance_audition_during.snc";
            AUDITION_CHECK(patchio::save(e, before_path), "pre-preview persistent save");
            AUDITION_CHECK(e.audition_sequence_step(0, 3), "real source accepted");
            AUDITION_CHECK(patchio::save(e, during_path), "save during preview");
            FILE* before = std::fopen(before_path, "rb"); FILE* during = std::fopen(during_path, "rb");
            AUDITION_CHECK(before && during, "serialized comparison open");
            bool same = true;
            for (;;) {
                const int a = std::fgetc(before), b = std::fgetc(during);
                if (a != b) same = false;
                if (a == EOF || b == EOF) break;
            }
            std::fclose(before); std::fclose(during);
            std::remove(before_path); std::remove(during_path);
            AUDITION_CHECK(same, "serialized patch byte-identical during preview");
            e.render(audio, 512);
            int peak = 0; for (int i = 0; i < 1024; ++i) if (std::abs((int)audio[i]) > peak) peak = std::abs((int)audio[i]);
            AUDITION_CHECK(peak > 100 && !e.transport_playing && e.transport_frame == 0 &&
                           source->params[pitch].base == base, "OSC/VCA, FM, PLAY audible while stopped");
            e.cancel_sequence_audition();
            AUDITION_CHECK(source->params[pitch].effective == base, "real source pitch restored");
            if (kind == UnitKind::SamplePlayer) {
                const auto& sample = SampleBank::instance().slot(61);
                AUDITION_CHECK(sample.data.size() == 4096 && sample.data[0] == 8000 &&
                               sample.data[4095] == 8000, "PLAY never writes PCM");
            }
        }
        SampleBank::instance().clear_slot(61);
    }
    {
        Engine e; e.replace_unit(0, 0, new AuditionProbeUnit());
        auto* rec = new RecUnit(); rec->sample_slot = 61; e.replace_unit(0, 1, rec);
        auto& sample = SampleBank::instance().slot(61);
        sample.channels = 1; sample.data.assign(4096, 777);
        sample.logical_frames = 4096; rec->adopt_take(4096);
        const uint32_t generation = rec->take_generation();
        e.set_step_lock(0, 0, 1, RecUnit::P_REC, 32767);
        AUDITION_CHECK(e.audition_sequence_step(0, 0), "chain with finalized REC accepted");
        e.render(audio, 512); e.cancel_sequence_audition();
        AUDITION_CHECK(!rec->is_recording() && rec->recorded_frames() == 4096 &&
                       rec->take_generation() == generation && sample.logical_frames == 4096 &&
                       sample.data[0] == 777 && !rec->params[RecUnit::P_REC].audition_override,
                       "REC never triggered, locked, rearmed or PCM changed");
        e.pending_rec_start = 1;
        AUDITION_CHECK(!e.audition_sequence_step(0, 0), "pending REC refused");
        e.pending_rec_start = 0;
        rec->clear_take(); rec->trigger();
        AUDITION_CHECK(rec->is_recording() && !e.audition_sequence_step(0, 0), "active REC refused");
        rec->bypass = true;
        AUDITION_CHECK(!e.audition_sequence_step(0, 0), "bypassed active REC refused");
        rec->stop(); SampleBank::instance().clear_slot(61);
    }
#undef AUDITION_CHECK
    printf("sequence audition: runtime locks, audio-frame expiry, playback ownership, RNG, sources, REC ok\n");
    return true;
}

static bool test_sequence_lifecycle() {
#define LIFE_CHECK(x, message) do { if (!(x)) { printf("FAIL: lifecycle %s\n", message); return false; } } while (0)
    Engine e; EngineUiSnapshot s;
    auto* noise = make_unit(UnitKind::Noise); e.replace_unit(0, 0, noise);
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow &&
               s.sequence_source_kind[0] == (uint8_t)UnitKind::Noise &&
               s.sequence_value_param[0] == NoiseUnit::P_GAIN && s.sequence_pitch_slot[0] == -1,
               "default NOISE drone flows, gain mapping preserved");
    noise->params[NoiseUnit::P_MODE].base = 0; // retained legacy follow-decay lifecycle
    noise->params[NoiseUnit::P_DECAY].base = 15000;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Strike,
               "finite noise decay is a truthful strike");
    noise->params[NoiseUnit::P_DECAY].base = 32767;
    auto* upstream_gate = make_unit(UnitKind::Vca); e.replace_unit(0, 1, upstream_gate);
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Gate,
               "downstream VCA gates continuous noise");
    auto* fm = make_unit(UnitKind::Fm); e.replace_unit(0, 2, fm);
    auto* strn = make_unit(UnitKind::Stranular); e.replace_unit(0, 3, strn);
    LIFE_CHECK(strn && !strn->sequencer_properties().source, "STRN is not an overwrite-source");
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Strike &&
               s.sequence_pitch_slot[0] == 2 && s.sequence_source_kind[0] == (uint8_t)UnitKind::Fm,
               "last audible source overrides upstream gate and STRN cannot hide FM");
    auto* osc = make_unit(UnitKind::Osc); e.replace_unit(0, 4, osc);
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow &&
               s.sequence_pitch_slot[0] == 4 && s.sequence_value_slot[0] == 4,
               "last OSC overwrite erases upstream VCA");
    auto* gate = make_unit(UnitKind::Vca); e.replace_unit(0, 5, gate);
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Gate, "final VCA gates last OSC");
    gate->params[VcaUnit::P_FLOOR].base = 32767; // modern gain-only: no implicit time threshold
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow, "gain-only VCA is not a gate");
    gate->params[VcaUnit::P_FLOOR].base = 0; gate->bypass = true;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow, "bypassed VCA ignored");
    osc->bypass = true;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_pitch_slot[0] == 2 && s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Strike,
               "bypassed final source ignored");
    fm->bypass = true;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_source_kind[0] == (uint8_t)UnitKind::Noise && s.sequence_pitch_slot[0] == -1 &&
               s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Gate,
               "STRN cannot hide preceding nonmelodic source");
    noise->bypass = true;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_source_kind[0] == (uint8_t)UnitKind::Stranular &&
               s.sequence_pitch_slot[0] == 3 && s.sequence_pitch_param[0] == StranularUnit::P_PITCH &&
               s.sequence_value_param[0] == StranularUnit::P_DENS &&
               s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow,
               "standalone STRN keeps signed pitch and density fallback without upstream gate");
    gate->bypass = false;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Gate &&
               s.sequence_value_param[0] == StranularUnit::P_MIX,
               "standalone STRN downstream gate retains original level mapping");
    strn->bypass = true;
    e.capture_ui_snapshot(s);
    LIFE_CHECK(s.sequence_source_kind[0] == (uint8_t)UnitKind::Empty && s.sequence_pitch_slot[0] == -1,
               "bypassed standalone STRN ignored");
#undef LIFE_CHECK
    printf("sequence lifecycle: effective overwrite source, noise modes, VCA, STRN fallback ok\n");
    return true;
}

// Frozen independent pre-extraction PLAY control reference. Keep this separate
// from play_window.h so changes to production calculation cannot bless themselves.
static PlayWindow legacy_play_window(uint32_t nf, const uint32_t* chops,
                                     uint16_t reverse, q15 speed, q15 start,
                                     q15 length, q15 slice_value, bool quantize) {
    PlayWindow out;
    const bool rev = speed < 0;
    int32_t v = rev ? -(int32_t)speed : speed;
    out.increment = v * 8;
    if (quantize && v != 0) {
        // Independently enumerate all octave/semitone candidates in ascending
        // pitch order, preserving old integer shifts and the strict tie rule.
        static const uint32_t bases[] = {65536,69433,73562,77936,82570,87480,
                                       92682,98199,104032,110218,116773,123715};
        uint32_t wanted = v * 8u; if (wanted < 1024) wanted = 1024;
        uint32_t best = 1024, distance = wanted - 1024;
        for (int oct = -6; oct <= 2; ++oct) for (int semi = 0; semi < 12; ++semi) {
            if (oct == 2 && semi != 0) continue;
            const uint32_t ratio = oct < 0 ? bases[semi] >> -oct : bases[semi] << oct;
            if (ratio < 1024 || ratio > 262136) continue;
            const uint32_t d = wanted > ratio ? wanted - ratio : ratio - wanted;
            if (d < distance) { distance = d; best = ratio; }
        }
        out.increment = rev ? -(int32_t)best : (int32_t)best;
    }
    if (out.increment < 0 ? -out.increment < 1024 : out.increment < 1024)
        out.increment = rev ? -1024 : 1024;
    if (!quantize && rev) out.increment = -out.increment;
    if (nf < 2) return out;
    out.start = (uint32_t)((uint64_t)(uint16_t)start * (nf - 1) / 32767u);
    uint32_t len = (uint32_t)((uint64_t)(uint16_t)length * nf / 32767u);
    if (len < 2) len = 2;
    if (out.start > nf - 2) out.start = nf - 2;
    out.end = out.start + len;
    if (out.end > nf) out.end = nf;
    int slice = (int)(((uint32_t)(uint16_t)slice_value * 17u) >> 15);
    if (slice > 16) slice = 16;
    if (slice > 0 && chops) {
        const uint32_t a = chops[slice - 1];
        if (a != UINT32_MAX && a < nf) {
            uint32_t b = nf;
            for (int i = 0; i < 16; ++i)
                if (chops[i] != UINT32_MAX && chops[i] > a && chops[i] < b) b = chops[i];
            if (b > a + 1) {
                out.start = a; out.end = b; out.slice = (uint8_t)slice;
                if (reverse & (uint16_t)(1u << (slice - 1))) out.increment = -out.increment;
            }
        }
    }
    out.normalized_start = (uint16_t)((uint64_t)out.start * 32767u / (nf - 1));
    out.normalized_end = (uint16_t)((uint64_t)(out.end - 1) * 32767u / (nf - 1));
    out.valid = !(out.end > nf || out.start + 1 >= out.end);
    return out;
}

static uint32_t play_test_random(uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

static bool same_play_window(const PlayWindow& a, const PlayWindow& b) {
    return a.valid == b.valid && a.start == b.start && a.end == b.end &&
        a.increment == b.increment && a.slice == b.slice &&
        a.normalized_start == b.normalized_start && a.normalized_end == b.normalized_end;
}

// This digest is recorded by compiling this same deterministic fixture against
// the archived pre-extraction units.cpp snapshot dated 20260905_144555,
// NOT generated from the refactored DSP. Explicit byte order makes it portable.
static uint64_t play_pcm_regression_digest() {
    uint64_t digest = 14695981039346656037ull;
    auto hash = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            digest ^= (uint8_t)(value >> (8 * i)); digest *= 1099511628211ull;
        }
    };
    auto& sample = SampleBank::instance().slot(63);
    uint32_t rng = 0xbada551u;
    q15 audio[1024];
    const q15 speeds[] = {-32768,-32767,-16384,-8192,-128,-127,-1,0,1,127,128,4096,8192,16384,32767};
    for (int trial = 0; trial < 384; ++trial) {
        sample = trackr::synth::Sample{};
        sample.channels = (trial & 1) ? 2 : 1;
        const uint32_t nf = trial < 4 ? (uint32_t)trial : 2 + play_test_random(rng) % 4095u;
        sample.data.resize(nf * sample.channels);
        for (auto& value : sample.data) value = (q15)play_test_random(rng);
        if (trial % 7 == 0) sample.logical_frames = nf / 2;
        sample.slice_reverse_mask = (uint16_t)play_test_random(rng);
        for (int i = 0; i < 16; ++i) {
            const uint32_t r = play_test_random(rng);
            sample.chops[i] = i % 5 == 0 ? UINT32_MAX : r % (nf + 8u);
        }
        if (trial % 3 == 0) sample.chops[3] = sample.chops[1];
        PlayUnit play;
        play.sample_slot = 63;
        play.pitch_quantize = (trial & 2) != 0;
        for (int stage = 0; stage < 4; ++stage) {
            play.params[PlayUnit::P_SPEED].effective = stage == 0
                ? speeds[trial % (sizeof speeds / sizeof speeds[0])] : (q15)play_test_random(rng);
            play.params[PlayUnit::P_START].effective = (q15)(play_test_random(rng) & 32767u);
            play.params[PlayUnit::P_LEN].effective = stage == 0 ? 32767
                : stage == 1 ? 0 : (q15)(play_test_random(rng) & 32767u);
            play.params[PlayUnit::P_SLICE].effective = (trial % 3 == 0 && stage == 0)
                ? 0 : (q15)(play_test_random(rng) & 32767u);
            play.params[PlayUnit::P_LOOP].effective = trial % 2 ? 0 : 32767;
            play.params[PlayUnit::P_GAIN].effective = stage == 0 ? 32767
                : (q15)(play_test_random(rng) & 32767u);
            play.control_tick();
            hash(play.visual_start()); hash(play.visual_end()); hash((uint16_t)play.visual_speed());
            if (stage != 2) play.trigger(); // stage 2 retunes the running head
            const int frames = stage == 0 ? 127 : stage == 1 ? 255 : 1024;
            play.process(audio, frames);
            for (int i = 0; i < frames; ++i) hash((uint16_t)audio[i]);
            hash(play.visual_position()); hash(play.visual_start()); hash(play.visual_end());
            hash((uint16_t)play.visual_speed()); hash(play.visual_state());
            hash(play.playback_active()); hash(play.playback_phase());
        }
    }
    sample = trackr::synth::Sample{};
    return digest;
}

static bool test_play_window_projection() {
#define WINDOW_CHECK(x, message) do { if (!(x)) { printf("FAIL: play window %s\n", message); return false; } } while (0)
    uint32_t chops[16]; for (auto& c : chops) c = UINT32_MAX;
    unsigned comparisons = 0;
    // Exhaust every signed q15 value, including the physical -32768 endpoint,
    // with and without semitone quantization. Covers every rounding transition.
    for (int q = -32768; q <= 32767; ++q) for (int quantize = 0; quantize < 2; ++quantize) {
        const PlayWindow a = play_window(32000, chops, 0, (q15)q, 0, 32767, 0, quantize);
        const PlayWindow b = legacy_play_window(32000, chops, 0, (q15)q, 0, 32767, 0, quantize);
        WINDOW_CHECK(same_play_window(a, b), "exhaustive speed equivalence");
        WINDOW_CHECK(a.increment != 0 && a.valid && a.slice == 0, "speed floor");
        ++comparisons;
    }
    WINDOW_CHECK(play_window(32000,nullptr,0,8192,0,32767,0,false).traversal_ms() == 1000,
                 "one-second window at unity");
    WINDOW_CHECK(play_window(32000,nullptr,0,4096,0,32767,0,false).traversal_ms() == 2000 &&
                 play_window(32000,nullptr,0,16384,0,32767,0,false).traversal_ms() == 500,
                 "half/double speed traversal");
    WINDOW_CHECK(play_window(32000,nullptr,0,-8192,0,32767,0,false).traversal_ms() == 1000,
                 "reverse nominal one-pass duration");
    WINDOW_CHECK(play_window(32000,nullptr,0,0,0,32767,0,false).increment == 1024 &&
                 play_window(32000,nullptr,0,-1,0,32767,0,false).increment == 1024 &&
                 play_window(32000,nullptr,0,-1,0,32767,0,true).increment == -1024,
                 "preserve old zero and tiny-negative floors");
    for (uint32_t nf = 0; nf < 4; ++nf) {
        const auto w = play_window(nf,nullptr,0,8192,32767,0,0,false);
        WINDOW_CHECK(w.valid == (nf >= 2), "minimum two source frames");
        if (nf >= 2) WINDOW_CHECK(w.start == nf - 2 && w.end == nf, "full start/len0 bounds");
        else WINDOW_CHECK(w.traversal_ms() == 0, "invalid has no duration");
    }
    // Unsorted, duplicate, empty, out-of-range and adjacent chops. SLCE1 starts
    // at frame100, ends at smallest strictly-greater marker500, then inverts SPD.
    chops[0]=100; chops[1]=800; chops[2]=500; chops[3]=100; chops[4]=1000;
    const q15 slice1 = (q15)((32768u + 16u) / 17u);
    auto w = play_window(1000,chops,1,8192,32767,0,slice1,false);
    WINDOW_CHECK(w.valid && w.start == 100 && w.end == 500 && w.slice == 1 &&
                 w.increment == -65536 && w.traversal_ms() == 12, "slice override/reverse");
    w = play_window(1000,chops,1,-8192,0,32767,slice1,false);
    WINDOW_CHECK(w.increment == 65536 && w.slice == 1, "reverse slice XOR negative speed");
    chops[2]=101;
    w = play_window(1000,chops,1,8192,0,32767,slice1,false);
    WINDOW_CHECK(w.slice == 0 && w.start == 0 && w.end == 1000 && w.increment == 65536,
                 "one-frame chop falls back without reverse");
    for (auto& c : chops) c = UINT32_MAX;
    chops[15] = 500;
    WINDOW_CHECK(play_window(1000,chops,0,8192,0,32767,32767,false).slice == 16,
                 "full slice selects final marker");
    uint32_t rng = 0x1234cafe;
    const uint32_t frame_edges[] = {0,1,2,3,63,64,127,128,32000,640000,0x7fffffffu,0xfffffffeu,0xffffffffu};
    const q15 param_edges[] = {-32768,-1,0,1,127,128,8192,16384,32766,32767};
    for (int trial = 0; trial < 60000; ++trial) {
        const uint32_t nf = trial % 3 ? frame_edges[trial % 13] : play_test_random(rng);
        for (int i = 0; i < 16; ++i) {
            const uint32_t c = play_test_random(rng);
            chops[i] = i % 5 == 0 ? UINT32_MAX : nf ? c % nf : c;
        }
        chops[1]=chops[3]; chops[4]=nf; // duplicate / out of range
        if (nf >= 2) chops[7]=nf-1;   // unusable one-frame tail
        const q15 speed = (q15)play_test_random(rng);
        const q15 start = trial % 2 ? param_edges[trial % 10] : (q15)play_test_random(rng);
        const q15 length = trial % 5 ? param_edges[(trial / 7) % 10] : (q15)play_test_random(rng);
        const q15 slice = trial % 7 ? (q15)play_test_random(rng) : 0;
        const uint16_t reverse = (uint16_t)play_test_random(rng);
        const bool quantize = (trial & 1) != 0;
        const auto a = play_window(nf,chops,reverse,speed,start,length,slice,quantize);
        const auto b = legacy_play_window(nf,chops,reverse,speed,start,length,slice,quantize);
        WINDOW_CHECK(same_play_window(a,b), "random/corner exact legacy equivalence");
        if (a.valid) {
            WINDOW_CHECK(a.start < a.end && a.end <= nf && a.end - a.start >= 2,
                         "valid window bounds even with max source frames");
            WINDOW_CHECK(a.normalized_start <= a.normalized_end && a.normalized_end <= 32767,
                         "valid normalized bounds");
            WINDOW_CHECK(a.traversal_ms() <= 8589934588ull, "64bit maximum traversal arithmetic");
        } else WINDOW_CHECK(a.traversal_ms() == 0, "overflowed legacy window never advertised");
        ++comparisons;
    }
    // Sample overload sees metadata only and respects recorded logical length.
    trackr::synth::Sample sample;
    sample.channels = 2; sample.data.resize(64000); sample.logical_frames = 16000;
    sample.chops[0] = 100; sample.chops[1] = 12000; sample.slice_reverse_mask = 1;
    const auto* pcm = sample.data.data(); const size_t storage = sample.data.capacity();
    const auto projected = play_window(sample,8192,0,32767,slice1,false);
    WINDOW_CHECK(same_play_window(projected,play_window(16000,sample.chops,1,8192,0,32767,slice1,false)),
                 "sample logical metadata overload");
    WINDOW_CHECK(sample.data.data() == pcm && sample.data.capacity() == storage &&
                 sample.logical_frames == 16000 && sample.slice_reverse_mask == 1,
                 "sample metadata and storage untouched");
    sample.logical_frames = 99999;
    WINDOW_CHECK(play_window(sample,8192,0,32767,0,false).end == 32000, "logical clamps to physical");
    sample.channels = 0;
    WINDOW_CHECK(!play_window(sample,8192,0,32767,0,false).valid, "zero channels metadata invalid");
    const uint64_t digest = play_pcm_regression_digest();
    printf("play window: %u exact metadata cases; pre-extraction pcm digest %016llx\n",
           comparisons, (unsigned long long)digest);
    constexpr uint64_t baseline = 0x6327d7da3baeb459ull;
    WINDOW_CHECK(digest == baseline, "pre-extraction PCM/control/visual golden");
#undef WINDOW_CHECK
    return true;
}

class FollowerProbeUnit final : public Unit {
public:
    q15 sample = 0;
    FollowerProbeUnit() { kind = UnitKind::Osc; }
    void process(q15* out, int frames) override {
        for (int i = 0; i < frames; ++i) out[i] = sample;
    }
};

static std::vector<unsigned char> torso_read_file(const char* path) {
    std::vector<unsigned char> bytes;
    FILE* f = std::fopen(path, "rb");
    if (!f) return bytes;
    int c; while ((c = std::fgetc(f)) != EOF) bytes.push_back((unsigned char)c);
    std::fclose(f); return bytes;
}
static bool torso_write_file(const char* path, const std::vector<unsigned char>& bytes) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

static bool test_torso_engine() {
#define TORSO_CHECK(x, message) do { if (!(x)) { printf("FAIL: torso %s (line %d)\n", message, __LINE__); return false; } } while (0)
    static_assert((int)ModSource::EnvFollow == 5 && (int)ModSource::Count == 6, "stable route ids");
    static_assert(sizeof(ParamInspector) <= 64, "scalar DTO bounded");
    static q15 audio[16000 * 2];
    {
        Engine e;
        FollowerProbeUnit* probes[NUM_CHAINS];
        for (int c = 0; c < NUM_CHAINS; ++c) {
            probes[c] = new FollowerProbeUnit(); probes[c]->sample = (q15)(4000 * (c + 1));
            e.replace_unit(c, 0, probes[c]);
        }
        TORSO_CHECK(e.env_follow_source() == 0 && e.env_follow_response() == EnvResponse::Fast &&
                    e.env_follow_value() == 0, "constructor follower defaults");
        e.render(audio, 32);
        TORSO_CHECK(e.env_follow_value() == 3000, "legacy fast first attack exact");
        int oracle = 3000;
        uint32_t rng = 1;
        for (int i = 0; i < 1000; ++i) {
            rng = rng * 1664525u + 1013904223u;
            probes[0]->sample = (q15)(rng >> 16);
            const int peak = std::abs((int)probes[0]->sample);
            oracle += peak > oracle ? ((peak - oracle) * 3 >> 2) : ((peak - oracle) >> 6);
            e.render(audio, 32);
            TORSO_CHECK(e.env_follow_value() == oracle, "fast exact signed peak and release oracle");
        }
        for (int c = 0; c < NUM_CHAINS; ++c) {
            e.set_env_follow_source(c); e.transport_reset();
            probes[c]->sample = (q15)(4000 * (c + 1));
            e.chains[c].volume = 0; // follower remains explicitly pre-mixer gain
            e.render(audio, 32);
            TORSO_CHECK(e.env_follow_value() == (c + 1) * 3000, "every selected chain pre-volume");
        }
        const q15 held = e.env_follow_value();
        TORSO_CHECK(!e.set_env_follow_source(-1) && !e.set_env_follow_source(4) &&
                    !e.set_env_follow_source(3) && !e.set_env_follow_response(EnvResponse::Count) &&
                    !e.set_env_follow_response((EnvResponse)255) &&
                    !e.set_env_follow_response(EnvResponse::Fast) && e.env_follow_value() == held,
                    "invalid/no-op settings preserve level");
        e.chains[3].mute = true; e.render(audio, 32);
        TORSO_CHECK(e.env_follow_value() == held + ((-held) >> 6), "muted fast follower releases");
        e.chains[3].mute = false;
        TORSO_CHECK(e.set_env_follow_response(EnvResponse::Body) && !e.env_follow_value(), "response clears state");
        e.render(audio, 32); TORSO_CHECK(e.env_follow_value() == 2000, "body rise");
        probes[3]->sample = 0; e.render(audio, 32);
        TORSO_CHECK(e.env_follow_value() == 1984, "body release");
        e.set_env_follow_response(EnvResponse::Slow); probes[3]->sample = 16000;
        e.render(audio, 32); TORSO_CHECK(e.env_follow_value() == 500, "slow rise");
        probes[3]->sample = 0; e.render(audio, 32);
        TORSO_CHECK(e.env_follow_value() == 498, "slow release");
        e.transport_reset(); TORSO_CHECK(!e.env_follow_value() && e.env_follow_source() == 3 &&
                    e.env_follow_response() == EnvResponse::Slow, "reset clears state not configuration");
        Engine other; e.swap_patch_state(other);
        TORSO_CHECK(e.env_follow_source() == 0 && e.env_follow_response() == EnvResponse::Fast &&
                    other.env_follow_source() == 3 && other.env_follow_response() == EnvResponse::Slow &&
                    !e.env_follow_value() && !other.env_follow_value(), "swap settings and reset both levels");
    }
    {
        // Muted, bypass-only and actually empty selected chains all release;
        // other audible chains must not keep the selected envelope alive.
        for (int response = 0; response < (int)EnvResponse::Count; ++response)
        for (int silence = 0; silence < 3; ++silence) {
            Engine e; auto* selected = new FollowerProbeUnit(); selected->sample = -32768;
            auto* other = new FollowerProbeUnit(); other->sample = 30000;
            e.replace_unit(2, 0, selected); e.replace_unit(0, 0, other);
            e.set_env_follow_source(2); e.set_env_follow_response((EnvResponse)response);
            e.render(audio, 32);
            int oracle = e.env_follow_value();
            TORSO_CHECK(oracle > 0, "negative full scale follower never wraps");
            if (silence == 0) e.chains[2].mute = true;
            if (silence == 1) selected->bypass = true;
            if (silence == 2) delete e.remove_unit(2, 0);
            const int shift = response == 0 ? 6 : (response == 1 ? 7 : 8);
            for (int block = 0; block < 2500; ++block) {
                // Independent floor-release division; drains sub-LSB state too.
                oracle -= (oracle + (1 << shift) - 1) / (1 << shift);
                e.render(audio, 32);
                TORSO_CHECK(e.env_follow_value() == oracle, "silent follower selected-response release oracle");
            }
            TORSO_CHECK(e.env_follow_value() == 0, "silent follower fully drains to zero");
        }
    }
    {
        Engine e; auto* probe = new AuditionProbeUnit(); e.replace_unit(0, 0, probe);
        Param& p = probe->params[0]; ParamInspector info;
        p.name = "long_parameter_name_cut";
        unsigned cases = 0;
        const int controls[] = {-32768, -32767, -10000, 0, 14000, 32767};
        const int depths[] = {-32768, -32767, -17000, 0, 9000, 32767};
        for (int bipolar = 0; bipolar < 2; ++bipolar) for (int source = 0; source < (int)ModSource::Count; ++source)
        for (int control : controls) for (int depth : depths) {
            p.bipolar = bipolar; p.base = (q15)control; p.mod_depth = (q15)depth; p.src = (ModSource)source;
            e.sigils[0].value = -12345; e.sigils[1].value = 20000; e.sigils[2].value = -30000; e.sigils[3].value = 32767;
            e.capture_param_inspector(0, 0, 0, info);
            auto resolve = [=](int mod) {
                // Independent floor division reference for arithmetic right shift.
                const int64_t product = (int64_t)mod * depth;
                const int delta = product >= 0 ? (int)(product / 32768) : -(int)((-product + 32767) / 32768);
                return control + delta;
            };
            const int floor = bipolar ? -32767 : 0;
            auto clamp = [=](int v) { return std::max(floor, std::min(32767, v)); };
            const int modlo = source >= 1 && source <= 4 ? -32768 : 0;
            const int modhi = source ? 32767 : 0;
            const int low = std::min(resolve(modlo), resolve(modhi));
            const int high = std::max(resolve(modlo), resolve(modhi));
            const int live = source >= 1 && source <= 4 ? e.sigils[source - 1].value : 0;
            TORSO_CHECK(info.valid && info.control == control && info.base == control &&
                info.range_min == clamp(low) && info.range_max == clamp(high) &&
                info.clip_low == (low < floor) && info.clip_high == (high > 32767) &&
                info.target == clamp(resolve(live)) && info.source_value == live &&
                info.layer == 0 && !info.temp && info.effective == p.effective &&
                info.name[15] == 0 && std::strlen(info.name) == 15, "signed full-source potential range oracle");
            Param reference = p; reference.slew_ready = false; reference.resolve((q15)live);
            TORSO_CHECK(reference.target == info.target, "target agrees with Param resolve");
            ++cases;
        }
        TORSO_CHECK(cases == 432, "range case count");
        // Existing legal Sigil::step extrema, including the extra negative LSB.
        for (int depth : {-32768, -32767, 17000, 32767}) {
            auto& sigil = e.sigils[0]; sigil.mode = SigilMode::RandomStep;
            sigil.depth = -32768; sigil.random_to = -32768; sigil.phase = 0;
            p.src = ModSource::SigilA; p.base = 0; p.bipolar = true; p.mod_depth = (q15)depth;
            e.render(audio, 32); e.capture_param_inspector(0, 0, 0, info);
            TORSO_CHECK(info.source_value == -32768 && info.target == p.target &&
                        info.target >= info.range_min && info.target <= info.range_max,
                        "actual negative extreme has exact safe target inside potential");
            const int unclamped = -depth;
            TORSO_CHECK(info.target == std::min(32767, std::max(-32767, unclamped)),
                        "extreme signed product no overflow");
        }
        for (int c : {-1, 4, 1000}) { e.capture_param_inspector(c, 0, 0, info); TORSO_CHECK(!info.valid && !info.name[0], "invalid DTO chain"); }
        for (int s : {-1, 1, 6}) { e.capture_param_inspector(0, s, 0, info); TORSO_CHECK(!info.valid, "invalid DTO slot"); }
        for (int v : {-1, 2, 12}) { e.capture_param_inspector(0, 0, v, info); TORSO_CHECK(!info.valid, "invalid DTO param"); }
        p = Param{}; p.base = 3000; p.name = "pitch";
        p.step_lock = 6000; p.step_lock_override = true;
        e.capture_param_inspector(0, 0, 0, info); TORSO_CHECK(info.layer == 1 && info.control == 6000, "lock layer");
        e.set_step_lock(0, 3, 0, 0, 9000); e.audition_sequence_step(0, 3);
        e.capture_param_inspector(0, 0, 0, info); TORSO_CHECK(info.layer == 2 && info.control == 9000, "audition layer");
        p.performance = 12000; p.performance_override = true;
        e.capture_param_inspector(0, 0, 0, info); TORSO_CHECK(info.layer == 3 && info.control == 12000, "performance layer");
        TORSO_CHECK(e.begin_param_temp(0, 0, 0) && e.param_temp_value() == 12000 &&
                    e.set_param_temp(15000), "begin from live control and set");
        e.capture_param_inspector(0, 0, 0, info);
        TORSO_CHECK(info.layer == 4 && info.temp && info.control == 15000 && info.target == 15000 &&
                    info.effective == 9000 && p.base == 3000, "temp wins without snapping or base write");
        e.render(audio, 32); TORSO_CHECK(p.effective == 10500, "temp normal slew");
        TORSO_CHECK(e.begin_param_temp(0, 0, 0) && e.param_temp_value() == 15000, "repeat begin idempotent");
        e.cancel_sequence_audition(); TORSO_CHECK(e.param_temp_active() && p.slew_ready, "audition release leaves temp owner");
        p.performance = 20000;
        const q15 before_release = p.effective;
        e.cancel_param_temp(); e.cancel_param_temp();
        TORSO_CHECK(!e.param_temp_active() && p.control_source() == 20000 && p.effective == before_release,
                    "cancel reveals current performance not stale base; no immediate snap");
        e.render(audio, 32);
        TORSO_CHECK(p.effective == before_release + (20000 - before_release) / 4, "release normal slew");
        p.performance_override = false;
        TORSO_CHECK(p.control_source() == 6000, "current underlying lock intact");
        TORSO_CHECK(e.begin_param_temp(0, 0, 0) && e.set_param_temp(-32768) && e.param_temp_value() == 0, "unipolar clamp");
        p.bipolar = true; e.set_param_temp(-32768); TORSO_CHECK(e.param_temp_value() == -32767, "bipolar clamp");
        TORSO_CHECK(e.begin_param_temp(0, 0, 1) && !p.temp_override && probe->params[1].temp_override, "single owner retarget");
        TORSO_CHECK(!e.begin_param_temp(-1, 0, 0) && !e.param_temp_active() && !e.set_param_temp(1), "invalid begin cancels");
        for (int s : {-1, 1, 6}) TORSO_CHECK(!e.begin_param_temp(0, s, 0), "invalid temp slot");
        for (int v : {-1, 2, 12}) TORSO_CHECK(!e.begin_param_temp(0, 0, v), "invalid temp param");
        e.chains[0].mute = true; TORSO_CHECK(!e.begin_param_temp(0, 0, 0), "muted begin rejected");
        e.chains[0].mute = false; e.begin_param_temp(0, 0, 0); e.chains[0].mute = true;
        e.render(audio, 1); TORSO_CHECK(!e.param_temp_active(), "mute during render cancels");
        e.chains[0].mute = false; probe->bypass = true;
        TORSO_CHECK(!e.begin_param_temp(0, 0, 0), "bypassed begin rejected"); probe->bypass = false;
        for (int action = 0; action < 5; ++action) {
            e.begin_param_temp(0, 0, 0);
            if (action == 0) e.transport_play();
            if (action == 1) e.transport_stop();
            if (action == 2) e.transport_reset();
            if (action == 3) e.set_bpm(130);
            if (action == 4) e.set_sequencer_enabled(true);
            TORSO_CHECK(!e.param_temp_active() && !p.temp_override, "transport changes cancel");
        }
        e.transport_play(); e.begin_param_temp(0, 0, 0); e.set_param_temp(14000);
        e.render(audio, 32); TORSO_CHECK(e.param_temp_active(), "lazy sequencer start not temp cancellation");
        e.set_step_lock(0, 2, 0, 0, 2000); e.render(audio, 8000);
        TORSO_CHECK(e.param_temp_active() && p.control_source() == 14000, "scheduled boundaries cannot reclaim temp");
        e.transport_stop();
        e.begin_param_temp(0, 0, 0); Unit* detached = e.replace_unit(0, 0, new AuditionProbeUnit());
        TORSO_CHECK(!e.param_temp_active() && !detached->params[0].temp_override, "replace releases before detach"); delete detached;
        e.render(audio, 32);
        TORSO_CHECK(e.chains[0].units[0]->params[0].control_source() == 3000 &&
                    e.chains[0].units[0]->params[0].effective == 3000, "old temp never leaks into fresh topology");
        e.begin_param_temp(0, 0, 0); e.insert_unit(0, 0, new AuditionProbeUnit());
        TORSO_CHECK(!e.param_temp_active(), "insert cancels");
        e.begin_param_temp(0, 0, 0); e.move_unit_before_with_undo(0, 1, 0);
        TORSO_CHECK(!e.param_temp_active(), "move cancels");
        e.begin_param_temp(0, 0, 0); TORSO_CHECK(e.undo_structural_edit() && !e.param_temp_active(), "undo cancels");
        e.begin_param_temp(0, 0, 0); detached = e.remove_unit(0, 0);
        TORSO_CHECK(!e.param_temp_active() && !detached->params[0].temp_override, "remove cancels"); delete detached;
        e.begin_param_temp(0, 0, 0); Engine other; e.swap_patch_state(other);
        TORSO_CHECK(!e.param_temp_active() && !other.param_temp_active() &&
                    !other.chains[0].units[0]->params[0].temp_override, "project swap cancels before ownership moves");
    }
    {
        Engine e; auto* probe = new AuditionProbeUnit(); e.replace_unit(0, 0, probe);
        auto* rec = new RecUnit(); rec->sample_slot = 61; e.replace_unit(3, 0, rec);
        auto& sample = SampleBank::instance().slot(61); sample.channels = 1; sample.data.assign(4096, 777);
        rec->adopt_take(4096);
        TORSO_CHECK(!e.begin_param_temp(3, 0, RecUnit::P_REC) && e.begin_param_temp(0, 0, 0), "REC excluded; finalized take safe");
        const uint32_t generation = rec->take_generation(); e.set_param_temp(10000); e.render(audio, 32);
        TORSO_CHECK(sample.data[0] == 777 && rec->recorded_frames() == 4096 &&
                    rec->take_generation() == generation && !rec->is_recording(), "TEMP never triggers/modifies REC");
        e.pending_rec_start = 8; e.render(audio, 1);
        TORSO_CHECK(!e.param_temp_active() && !e.begin_param_temp(0, 0, 0), "other-chain pending start cancels");
        e.pending_rec_start = 0; e.pending_rec_stop = 8;
        TORSO_CHECK(!e.begin_param_temp(0, 0, 0), "pending stop rejected"); e.pending_rec_stop = 0;
        e.begin_param_temp(0, 0, 0); rec->clear_take(); rec->trigger(); rec->bypass = true;
        TORSO_CHECK(rec->is_recording() && !e.set_param_temp(1000) && !e.param_temp_active(), "set rechecks bypassed active recording");
        rec->stop(); e.begin_param_temp(0, 0, 0); rec->clear_take(); rec->trigger(); e.render(audio, 1);
        TORSO_CHECK(!e.param_temp_active() && !e.begin_param_temp(0, 0, 0), "render rechecks active recording");
        rec->stop(); SampleBank::instance().clear_slot(61);
    }
    {
        // Accepted recording commands cancel immediately even between control
        // ticks; quantized deadlines never leave TEMP armed on either API path.
        for (bool one_card : {false, true}) for (bool quantized : {false, true}) {
            Engine e; e.replace_unit(0, 0, new AuditionProbeUnit());
            auto* rec = new RecUnit(); rec->sample_slot = 61; e.replace_unit(3, 0, rec);
            rec->params[RecUnit::P_LEN].base = rec->params[RecUnit::P_LEN].effective = 0;
            TORSO_CHECK(e.prepare_recorder(*rec, false) == trackr::synth::SampleEditResult::Ok, "late recorder preparation");
            e.transport_play(); e.render(audio, 7);
            TORSO_CHECK(e.begin_param_temp(0, 0, 0) && e.set_param_temp(14000), "off-tick TEMP before record command");
            const auto q = quantized ? LaunchQuantize::Sixteenth : LaunchQuantize::Immediate;
            const auto result = one_card ? e.request_recorder_record(3, 0, true, q) : e.request_chain_record(3, true, q);
            TORSO_CHECK(result == trackr::synth::SampleEditResult::Ok && !e.param_temp_active(), "record acceptance cancels immediately");
            TORSO_CHECK(!e.set_param_temp(18000), "canceled temp set cannot persist");
            if (quantized) {
                e.render(audio, 3993);
                TORSO_CHECK(rec->is_recording() || e.recorder_record_pending(3, 0), "late deadline reached/ready");
            }
            e.render(audio, 32);
            TORSO_CHECK(!e.param_temp_active() && rec->is_recording() &&
                        e.chains[0].units[0]->params[0].base == 3000 &&
                        !e.begin_param_temp(0, 0, 0), "late recording render excludes TEMP without base write");
            rec->stop(); SampleBank::instance().clear_slot(61);
        }
    }
    {
        Engine e;
        auto* source = new FollowerProbeUnit(); source->sample = 16000; e.replace_unit(2, 0, source);
        auto* destination = new AuditionProbeUnit(); e.replace_unit(0, 0, destination);
        Param& p = destination->params[0]; p.base = 10000; p.src = ModSource::EnvFollow; p.mod_depth = -20000;
        e.set_env_follow_source(2); e.set_env_follow_response(EnvResponse::Body);
        e.render(audio, 32); ParamInspector info; e.capture_param_inspector(0, 0, 0, info);
        TORSO_CHECK(info.env_chain == 2 && info.env_response == EnvResponse::Body && info.env_level == 2000 &&
                    info.source_value == 2000 && info.target == 8779 && info.range_min == 0 &&
                    info.range_max == 10000 && info.clip_low && !info.clip_high, "nonzero env DTO is signed and source-explicit");
        e.render(audio, 32);
        TORSO_CHECK(p.target == 8779, "selected chain env routes through actual control resolution");
        const q15 copied = info.source_value; delete e.remove_unit(0, 0);
        TORSO_CHECK(info.valid && info.source_value == copied && !std::strcmp(info.name, "PITCH"), "DTO owns scalars/name after source deletion");
    }
    {
        const char* path = "/tmp/seance_torso_test.snc";
        Engine e; e.replace_unit(0, 0, make_unit(UnitKind::Osc));
        e.set_env_follow_source(2); e.set_env_follow_response(EnvResponse::Slow);
        TORSO_CHECK(patchio::save(e, path), "persistent save"); const auto original = torso_read_file(path);
        e.begin_param_temp(0, 0, 0); e.set_param_temp(18000); e.render(audio, 32);
        TORSO_CHECK(patchio::save(e, path) && torso_read_file(path) == original, "serialized TEMP byte-identical");
        patchio::SampleManifest manifest; WorkspaceLayout workspace;
        TORSO_CHECK(patchio::load(e, path, &manifest, &workspace) && !e.param_temp_active() &&
                    e.env_follow_source() == 2 && e.env_follow_response() == EnvResponse::Slow && !e.env_follow_value(), "roundtrip cancels TEMP and clears env level");
        size_t env = 0;
        for (size_t at = 5; at + 8 <= original.size();) {
            const uint32_t length = original[at+4] | ((uint32_t)original[at+5] << 8) |
                ((uint32_t)original[at+6] << 16) | ((uint32_t)original[at+7] << 24);
            if (!std::memcmp(original.data() + at, "ENVR", 4)) { env = at; break; }
            at += 8 + length;
        }
        TORSO_CHECK(env > 5 && original[env+4] == 3 && original[env+8] == 1, "versioned exact ENVR chunk");
        auto missing = original; missing.erase(missing.begin() + env, missing.begin() + env + 11);
        TORSO_CHECK(torso_write_file(path, missing) && patchio::load(e, path, &manifest, &workspace) &&
                    e.env_follow_source() == 0 && e.env_follow_response() == EnvResponse::Fast, "old SNC7 absence migrates defaults");
        e.set_env_follow_source(1); e.set_env_follow_response(EnvResponse::Body);
        e.begin_param_temp(0, 0, 0); e.set_param_temp(12345);
        Unit* identity = e.chains[0].units[0];
        manifest.slots[0].path[0] = 'z'; workspace.node[0][0].x = 123;
        for (int bad = 0; bad < 8; ++bad) {
            auto bytes = original;
            if (bad == 0) bytes[env + 8] = 0;
            if (bad == 1) bytes[env + 9] = 4;
            if (bad == 2) bytes[env + 10] = (uint8_t)EnvResponse::Count;
            if (bad == 3) bytes[env + 4] = 2;
            if (bad == 4) bytes[env + 4] = 4;
            if (bad == 5) bytes.insert(bytes.end(), original.begin() + env, original.begin() + env + 11);
            if (bad == 6) bytes.resize(env + 10);
            if (bad == 7) { bytes[env + 4] = 255; bytes[env + 5] = 255; bytes[env + 6] = 255; bytes[env + 7] = 255; }
            TORSO_CHECK(torso_write_file(path, bytes) && !patchio::load(e, path, &manifest, &workspace), "malformed ENVR rejected");
            TORSO_CHECK(e.chains[0].units[0] == identity && e.env_follow_source() == 1 &&
                        e.env_follow_response() == EnvResponse::Body && e.param_temp_active() &&
                        e.param_temp_value() == 12345 && manifest.slots[0].path[0] == 'z' && workspace.node[0][0].x == 123,
                        "failed load transactional incl runtime, outputs and identity");
        }
        // Independently authored SNC1-SNC6 records, not a new-writer roundtrip.
        for (int version = 1; version <= 6; ++version) {
            std::vector<unsigned char> legacy = {'S','N','C',(unsigned char)('0'+version),(unsigned char)version};
            for (int c = 0; c < NUM_CHAINS; ++c) {
                legacy.insert(legacy.end(), {255,127});
                if (version >= 2) legacy.insert(legacy.end(), {0,0}); // pan
                legacy.push_back(0); // mute
                legacy.insert(legacy.end(), UNITS_PER_CHAIN * (3 + 6 * 5), 0);
            }
            legacy.insert(legacy.end(), NUM_SIGILS * (1 + SIGIL_POINTS * 2 + 2 + 2), 0);
            legacy.insert(legacy.end(), NUM_SIGILS * 3, 0);
            if (version >= 3) {
                legacy.insert(legacy.end(), {120,0});
                legacy.insert(legacy.end(), NUM_SIGILS * 3, 0);
            }
            if (version >= 4) legacy.insert(legacy.end(), NUM_CHAINS * UNITS_PER_CHAIN, 0);
            if (version >= 5) {
                legacy.insert(legacy.end(), patchio::SampleManifest::SLOT_COUNT * 4, 0);
                legacy.insert(legacy.end(), {0,4,0});
            }
            if (version >= 6) legacy.insert(legacy.end(), NUM_CHAINS * UNITS_PER_CHAIN * 11, 0);
            e.set_env_follow_source(3); e.set_env_follow_response(EnvResponse::Slow);
            TORSO_CHECK(torso_write_file(path, legacy) && patchio::load(e, path, &manifest, &workspace) &&
                        e.env_follow_source() == 0 && e.env_follow_response() == EnvResponse::Fast &&
                        !e.param_temp_active(), "legacy SNC1-SNC6 defaults and project cancellation");
        }
        std::remove(path);
    }
#undef TORSO_CHECK
    printf("torso engine: follower oracle/settings/chunks, 432 ranges, TEMP priority/slew/lifecycle/recording/persistence ok\n");
    return true;
}

static bool test_fm_strike_legacy() {
    // This exact driver was also built against pre-upgrade units.h/.cpp from
    // the fm_drone_20260906_124109 backup, not a rewritten DSP oracle. Exhaust
    // all 2^10 corners for each of four policies + 256 interior cases/policy.
    // Later stages change every control, retrigger, reset and resume rendering.
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](uint32_t n) {
        for (int b = 0; b < 4; ++b) { hash ^= (n >> (b * 8)) & 255; hash *= 1099511628211ull; }
    };
    for (int policy = 0; policy < 4; ++policy) for (int scenario = 0; scenario < 1280; ++scenario) {
        FmUnit fm; fm.phase_reset = policy & 1; fm.alias_safe = policy & 2;
        uint32_t rng = 0x6d2b79f5u ^ (uint32_t)(scenario * 33 + policy);
        auto random = [&]() { rng = rng * 1664525u + 1013904223u; return rng; };
        for (int p = 0; p < 10; ++p) {
            const int lo = p == FmUnit::P_RATIO_FINE ? -32767 : 0;
            fm.params[p].effective = (q15)(scenario < 1024
                ? ((scenario & (1 << p)) ? 32767 : lo)
                : lo + (int)(random() % (uint32_t)(32768 - lo)));
        }
        fm.control_tick();
        for (int stage = 0; stage < 48; ++stage) {
            if (stage && stage % 8 == 0) {
                for (int p = 0; p < 10; ++p) {
                    const int lo = p == FmUnit::P_RATIO_FINE ? -32767 : 0;
                    fm.params[p].effective = (q15)(lo + (int)(random() % (uint32_t)(32768 - lo)));
                }
                fm.control_tick();
            }
            if (stage == 12 || stage == 37) fm.reset();
            if (stage == 1 || stage == 3 || stage == 13 || stage == 22 || stage == 33) fm.trigger();
            q15 out[64]; std::fill(out, out + 64, (q15)1234);
            fm.process(out, 64);
            for (q15 s : out) mix((uint16_t)s);
            mix(fm.visual_phase()); mix(fm.visual_envelope());
            mix(fm.visual_index_envelope()); mix(fm.visual_feedback_envelope());
        }
    }
    constexpr uint64_t expected = 0xf8b0a53be96ab91bull; // independently compiled pre-change units.cpp
    printf("FM strike baseline: 5120 scenarios, 15728640 frames, hash %016llx\n", (unsigned long long)hash);
    if (hash != expected) { printf("FAIL: FM legacy PCM/control trace changed\n"); return false; }
    return true;
}

static bool test_fm_drone() {
#define FM_CHECK(x, msg) do { if (!(x)) { printf("FAIL: FM drone %s (line %d)\n", msg, __LINE__); return false; } } while (0)
    {
        FmUnit a, b;
        FM_CHECK(!a.drone && a.num_params == 10, "default strike/no parameter expansion");
        a.drone = b.drone = true; a.phase_reset = b.phase_reset = true;
        a.params[FmUnit::P_INDEX_FALL].effective = a.params[FmUnit::P_FEEDBACK_FALL].effective = 32767;
        a.params[FmUnit::P_ATTACK].effective = 32767; a.params[FmUnit::P_DECAY].effective = 0;
        b.params[FmUnit::P_ATTACK].effective = 0; b.params[FmUnit::P_DECAY].effective = 32767;
        a.control_tick(); b.control_tick();
        int peak = 0;
        for (int i = 0; i < 1024; ++i) {
            q15 x, y; const int env = a.visual_envelope(); const auto phase = a.visual_phase();
            a.trigger(); FM_CHECK(a.visual_envelope() == env && a.visual_phase() == phase, "trigger ignored before render");
            a.process(&x, 1); b.process(&y, 1);
            FM_CHECK(x == y && a.visual_envelope() == b.visual_envelope(), "dormant controls/trigger identical PCM");
            FM_CHECK(a.visual_envelope() >= env && a.visual_envelope() - env <= FmUnit::DRONE_FADE_STEP, "bounded entry");
            peak = std::max(peak, abs((int)x));
        }
        FM_CHECK(peak && a.visual_envelope() == 32767 && a.visual_index_envelope() == 32767 &&
                 a.visual_feedback_envelope() == 32767, "full sustained level/index/fb");
        FM_CHECK(a.params[FmUnit::P_ATTACK].effective == 32767 && a.params[FmUnit::P_DECAY].effective == 0 &&
                 a.params[FmUnit::P_INDEX_FALL].effective == 32767, "dormant controls not mutated");
        a.reset(); b.reset(); FM_CHECK(a.drone && a.visual_envelope() == 0, "reset retains policy but silences state");
        q15 x[512], y[512]; a.process(x, 512); b.process(y, 512);
        FM_CHECK(!std::memcmp(x, y, sizeof x) && x[0] == 0 && a.visual_envelope() == 32767, "reset smoothly reopens");
    }
    // No retrigger difference with feedback history, policies, and max decays.
    // Test 30 seconds/policy; every one-second window must remain non-silent.
    for (int policy = 0; policy < 4; ++policy) {
        FmUnit a, b; a.drone = b.drone = true;
        a.phase_reset = b.phase_reset = policy & 1; a.alias_safe = b.alias_safe = policy & 2;
        for (int p : {FmUnit::P_INDEX, FmUnit::P_FEEDBACK, FmUnit::P_INDEX_FALL, FmUnit::P_FEEDBACK_FALL})
            a.params[p].effective = b.params[p].effective = 32767;
        a.control_tick(); b.control_tick();
        q15 x[64], y[64]; int nonzero = 0;
        for (int block = 0; block < 15000; ++block) {
            if (block % 7 == 0) a.trigger();
            a.process(x, 64); b.process(y, 64);
            FM_CHECK(!std::memcmp(x, y, sizeof x), "30s retrigger equality");
            for (q15 s : x) { nonzero += s != 0; FM_CHECK(abs((int)s) <= a.params[FmUnit::P_GAIN].effective, "gain/output bound"); }
            if (block % 500 == 499) { FM_CHECK(nonzero > 0, "nonzero every sustained second"); nonzero = 0; }
        }
        FM_CHECK(a.visual_envelope() == 32767 && a.visual_index_envelope() == 32767 &&
                 a.visual_feedback_envelope() == 32767, "30s sustain holds operators");
    }
    // Every ten-control binary corner with all policies. No float/audio-time
    // allocation; sanitizers instrument extreme feedback arithmetic as well.
    for (int policy = 0; policy < 4; ++policy) for (int corner = 0; corner < 1024; ++corner) {
        FmUnit a; a.drone = true; a.phase_reset = policy & 1; a.alias_safe = policy & 2;
        for (int p = 0; p < 10; ++p)
            a.params[p].effective = (q15)((corner & (1 << p)) ? 32767 : (p == FmUnit::P_RATIO_FINE ? -32768 : 0));
        a.control_tick(); q15 out[1024]; a.process(out, 1024);
        for (q15 s : out) FM_CHECK(abs((int)s) <= a.params[FmUnit::P_GAIN].effective, "all-corner bounded PCM");
        FM_CHECK(a.visual_envelope() == 32767, "all-corner sustained envelope");
    }
    {
        FmUnit a;
        a.phase_reset = true;
        a.params[FmUnit::P_INDEX_FALL].effective = a.params[FmUnit::P_FEEDBACK_FALL].effective = 32767;
        a.control_tick(); a.trigger(); q15 out[2048]; a.process(out, 2048);
        uint32_t expected_phase = (uint32_t)a.visual_phase() << 16;
        // Independently compute full phase before transition (upper accessor is
        // intentionally just 16 bits); this voice started at phase zero.
        const uint32_t inc = (uint32_t)(((1071616ull << 5) << 16) / ENGINE_SR); // default C4
        expected_phase = inc * 2048u;
        a.drone = true;
        for (int n = 0; n < 600; ++n) {
            const int env = a.visual_envelope(), index = a.visual_index_envelope(), feedback = a.visual_feedback_envelope();
            a.trigger(); a.process(out, 1); expected_phase += inc;
            FM_CHECK(a.visual_phase() == (uint16_t)(expected_phase >> 16), "entry carrier phase continuity");
            FM_CHECK(a.visual_envelope() >= env && a.visual_envelope() - env <= FmUnit::DRONE_FADE_STEP &&
                     a.visual_index_envelope() >= index && a.visual_index_envelope() - index <= FmUnit::DRONE_FADE_STEP &&
                     a.visual_feedback_envelope() >= feedback && a.visual_feedback_envelope() - feedback <= FmUnit::DRONE_FADE_STEP,
                     "entry from decayed strike restores smoothly");
        }
        a.drone = false;
        for (int n = 0; n < 15000; ++n) {
            const int env = a.visual_envelope(); a.process(out, 1); expected_phase += inc;
            FM_CHECK(a.visual_phase() == (uint16_t)(expected_phase >> 16), "exit carrier phase continuity");
            FM_CHECK(a.visual_envelope() <= env && env - a.visual_envelope() <= FmUnit::DRONE_FADE_STEP, "bounded exit decay");
        }
        FM_CHECK(a.visual_envelope() == 0, "exit eventually silent");
        FmUnit fresh; fresh.phase_reset = true;
        for (int p = 0; p < 10; ++p) fresh.params[p].effective = a.params[p].effective;
        fresh.control_tick(); fresh.trigger(); a.trigger();
        q15 oracle[2048]; a.process(out, 2048); fresh.process(oracle, 2048);
        FM_CHECK(!std::memcmp(out, oracle, sizeof out), "next phase-reset strike resumes exact legacy state");
        // Direct exit + trigger before process must not be converted to a release.
        a.drone = true; a.process(out, 256); a.drone = false; a.trigger(); fresh.reset(); fresh.trigger();
        a.process(out, 2048); fresh.process(oracle, 2048);
        FM_CHECK(!std::memcmp(out, oracle, sizeof out), "immediate explicit strike owns transition");
    }
    {
        FmUnit a, b; a.drone = b.drone = true;
        a.params[FmUnit::P_FEEDBACK].effective = b.params[FmUnit::P_FEEDBACK].effective = 0;
        b.params[FmUnit::P_RATIO_FINE].effective = 1;
        a.control_tick(); b.control_tick(); q15 x[256], y[256]; int changed = 0;
        for (int n = 0; n < 500; ++n) { a.process(x, 256); b.process(y, 256); changed += std::memcmp(x, y, sizeof x) != 0; }
        FM_CHECK(changed > 100, "single RFINE LSB changes sustained PCM");
        const uint32_t inc = (uint32_t)(((1071616ull << 5) << 16) / ENGINE_SR); // default C4
        FM_CHECK(FmUnit::modulation_increment(inc, 8192, 0, false, false) ==
                 FmUnit::modulation_increment(inc, 8192, 1, false, false), "strike fine truncation preserved");
        // Full fine domain monotonicity, both precision/safety policies, and a
        // separate 128-bit rational oracle for extreme carrier/range ceilings.
        for (bool safe : {false, true}) for (bool drone : {false, true})
            for (uint32_t carrier : {1u, inc, 0xffffffffu}) for (q15 ratio : {(q15)0, (q15)8192, (q15)32767}) {
                uint32_t prev = 0;
                for (int fine = -32768; fine <= 32767; ++fine) {
                    const int64_t coarse = 2048 + (uint32_t)(uint16_t)ratio * 30720u / 32767u;
                    int64_t num = drone ? coarse * 32767 + fine * 1024 : (coarse + fine * 1024 / 32767) * 32767;
                    num = std::max(1024ll * 32767, std::min(33792ll * 32767, (long long)num));
                    const uint64_t raw = (uint64_t)(((__uint128_t)carrier * num) / (4096ull * 32767));
                    const uint32_t expected = (uint32_t)std::min<uint64_t>(raw, safe ? 1610612736ull : 0xffffffffull);
                    const auto got = FmUnit::modulation_increment(carrier, ratio, (q15)fine, drone, safe);
                    FM_CHECK(got == expected && got >= prev, "rational increment oracle/monotonicity"); prev = got;
                }
            }
    }
    {
        Engine e; auto* fm = new FmUnit(); fm->drone = true;
        e.replace_unit(0, 0, fm); q15 out[2048]; e.render(out, 1024);
        int peak = 0; for (q15 s : out) peak = std::max(peak, abs((int)s));
        FM_CHECK(peak && !e.transport_playing && e.transport_frame == 0 && e.sequence_tick == 0, "stopped engine auto sounds without advancing transport");
        EngineUiSnapshot snap; e.capture_ui_snapshot(snap);
        FM_CHECK(snap.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow &&
                 snap.sequence_pitch_param[0] == FmUnit::P_PITCH && snap.sequence_value_param[0] == FmUnit::P_GAIN &&
                 fm->sequencer_properties().level_param == FmUnit::P_GAIN, "truthful Flow pitch/gain metadata");
        auto* vca = new VcaUnit(); e.replace_unit(0, 1, vca); e.capture_ui_snapshot(snap);
        FM_CHECK(snap.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Gate, "downstream VCA Gate");
        vca->params[VcaUnit::P_FLOOR].base = 32767; // modern gain-only independent of times
        e.capture_ui_snapshot(snap);
        FM_CHECK(snap.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Flow, "gain-only VCA preserves Flow");
        fm->drone = false; e.capture_ui_snapshot(snap);
        FM_CHECK(snap.sequence_lifecycle[0] == (uint8_t)SequenceLifecycle::Strike, "strike metadata restored");
        fm->drone = true; e.transport_play(); e.render(out, 128); e.transport_stop(); e.render(out, 1024);
        FM_CHECK(fm->visual_envelope() == 32767 && !e.transport_playing, "transport stop leaves drone sounding");
    }
    {
        const char* path = "/tmp/seance_fm_drone.snc";
        for (int flags = 0; flags < 8; ++flags) {
            Engine e; auto* fm = new FmUnit(); fm->phase_reset = flags & 1; fm->alias_safe = flags & 2; fm->drone = flags & 4;
            for (int p = 0; p < 10; ++p) { fm->params[p].base = (q15)(p * 1200); fm->params[p].src = ModSource::SigilC; fm->params[p].mod_depth = (q15)(-p * 99); }
            e.replace_unit(0, 0, fm); auto* cloned = static_cast<FmUnit*>(clone_unit(fm));
            FM_CHECK(cloned && cloned->phase_reset == fm->phase_reset && cloned->alias_safe == fm->alias_safe &&
                     cloned->drone == fm->drone && cloned->visual_envelope() == 0, "clone policies/fresh voice");
            for (int p = 0; p < 10; ++p) FM_CHECK(cloned->params[p].base == fm->params[p].base &&
                cloned->params[p].src == fm->params[p].src && cloned->params[p].mod_depth == fm->params[p].mod_depth, "clone all controls/routes");
            delete cloned;
            FM_CHECK(patchio::save(e, path), "save"); const auto saved = torso_read_file(path);
            q15 out[2048]; e.render(out, 1024);
            FM_CHECK(patchio::save(e, path) && torso_read_file(path) == saved, "render/envelopes do not mutate persistence");
            Engine loaded; FM_CHECK(patchio::load(loaded, path), "load");
            auto* f = static_cast<FmUnit*>(loaded.chains[0].units[0]);
            FM_CHECK(f && f->phase_reset == fm->phase_reset && f->alias_safe == fm->alias_safe && f->drone == fm->drone,
                     "all 8 flags incl old0..3 roundtrip");
            for (int p = 0; p < 10; ++p) FM_CHECK(f->params[p].base == fm->params[p].base &&
                f->params[p].src == fm->params[p].src && f->params[p].mod_depth == fm->params[p].mod_depth,
                "roundtrip all controls/routes");
            size_t unit = 5;
            auto size_at = [&](size_t p) { return (uint32_t)saved[p] | (uint32_t)saved[p+1]<<8 | (uint32_t)saved[p+2]<<16 | (uint32_t)saved[p+3]<<24; };
            while (unit + 8 <= saved.size() && std::memcmp(saved.data()+unit, "UNIT", 4)) unit += 8 + size_at(unit+4);
            FM_CHECK(unit + 8 + NUM_CHAINS * UNITS_PER_CHAIN <= saved.size() && saved[unit+8] == flags, "exact UNIT flag bit");
            auto legacy = saved; legacy[unit+8] = (unsigned char)(flags & 3);
            FM_CHECK(torso_write_file(path, legacy) && patchio::load(loaded, path), "old flags accepted");
            f = static_cast<FmUnit*>(loaded.chains[0].units[0]);
            FM_CHECK(!f->drone && f->phase_reset == (bool)(flags & 1) && f->alias_safe == (bool)(flags & 2),
                     "old SNC7 flags preserve policies/default strike");
            legacy.erase(legacy.begin()+unit, legacy.begin()+unit+8+size_at(unit+4));
            FM_CHECK(torso_write_file(path, legacy) && !patchio::load(loaded, path) && loaded.chains[0].units[0] == f,
                     "missing required UNIT rejected transactionally");
        }
        // Independently authored old fixed-six-control records, not a new
        // writer roundtrip. SNC1..3 have no flags at all; SNC4..6 use 0..3.
        for (int version = 1; version <= 6; ++version) for (int flags = 0; flags < 4; ++flags) {
            std::vector<unsigned char> legacy = {'S','N','C',(unsigned char)('0'+version),(unsigned char)version};
            for (int c = 0; c < NUM_CHAINS; ++c) {
                legacy.insert(legacy.end(), {255,127});
                if (version >= 2) legacy.insert(legacy.end(), {0,0});
                legacy.push_back(0);
                const size_t first = legacy.size();
                legacy.insert(legacy.end(), UNITS_PER_CHAIN * (3 + 6 * 5), 0);
                if (!c) {
                    legacy[first] = (unsigned char)UnitKind::Fm;
                    for (int p = 0; p < 6; ++p) { legacy[first+3+p*5] = (unsigned char)(p*3000); legacy[first+4+p*5] = (unsigned char)((p*3000)>>8); }
                }
            }
            legacy.insert(legacy.end(), NUM_SIGILS * (1 + SIGIL_POINTS * 2 + 2 + 2), 0);
            legacy.insert(legacy.end(), NUM_SIGILS * 3, 0);
            if (version >= 3) { legacy.insert(legacy.end(), {120,0}); legacy.insert(legacy.end(), NUM_SIGILS * 3, 0); }
            if (version >= 4) { const size_t start = legacy.size(); legacy.insert(legacy.end(), NUM_CHAINS * UNITS_PER_CHAIN, 0); legacy[start] = (unsigned char)flags; }
            if (version >= 5) { legacy.insert(legacy.end(), patchio::SampleManifest::SLOT_COUNT * 4, 0); legacy.insert(legacy.end(), {0,4,0}); }
            if (version >= 6) legacy.insert(legacy.end(), NUM_CHAINS * UNITS_PER_CHAIN * 11, 0);
            Engine e;
            FM_CHECK(torso_write_file(path, legacy) && patchio::load(e, path), "SNC1..6 load");
            auto* fm = static_cast<FmUnit*>(e.chains[0].units[0]);
            FM_CHECK(fm && !fm->drone && fm->phase_reset == (bool)(version >= 4 && (flags & 1)) &&
                fm->alias_safe == (bool)(version >= 4 && (flags & 2)), "SNC1..6 flags/default strike");
            for (int p = 0; p < 6; ++p) FM_CHECK(fm->params[p].base == p*3000, "old six controls exact");
            for (int p = 6; p < 10; ++p) FM_CHECK(fm->params[p].base == 0, "old appended controls neutral");
        }
        std::remove(path);
    }
    {
        FmUnit a, b; a.drone = b.drone = true;
        q15 whole[4096], parts[4096]; a.process(whole, 4096);
        int done = 0; while (done < 4096) { const int n = std::min(4096 - done, 1 + done % 63); b.process(parts+done, n); done += n; }
        FM_CHECK(!std::memcmp(whole, parts, sizeof whole), "drone independent of process partition");
        for (int n = 0; n < 2000; ++n) {
            const int before = a.visual_envelope(); a.drone = (n % 3) != 0;
            a.process(whole, 1);
            FM_CHECK(abs((int)a.visual_envelope() - before) <= FmUnit::DRONE_FADE_STEP, "rapid mode switch envelope bound");
        }
        a.drone = false; a.params[FmUnit::P_ATTACK].effective = 32767; a.control_tick(); a.trigger();
        a.process(whole, 12); const int partial_attack = a.visual_envelope(); a.drone = true;
        a.process(whole, 1);
        FM_CHECK(a.visual_envelope() == partial_attack + FmUnit::DRONE_FADE_STEP, "entry preserves partial attack level");
        a.drone = false; a.process(whole, 1);
        FM_CHECK(a.visual_envelope() <= partial_attack + FmUnit::DRONE_FADE_STEP, "exit does not resume dormant attack");
        // The final LSBs of RFINE must settle despite the generic integer Param
        // slew's +/-3 deadband; no changes to strike's existing slew policy.
        for (bool drone : {false, true}) for (int value : {-4,-3,-1,0,1,3,4,32767,-32767}) {
            FmUnit f; f.drone = drone; auto& p = f.params[FmUnit::P_RATIO_FINE];
            p.resolve(0); p.base = (q15)value;
            for (int tick = 0; tick < 64; ++tick) { p.resolve(0); f.control_tick(); }
            FM_CHECK(p.base == value && p.target == value && (!drone || p.effective == value), "drone RFINE final LSB reaches target");
            if (!drone && value != 0) FM_CHECK(p.effective != value, "strike original slew deadband preserved");
        }
    }
    printf("FM drone: sustain/ignore-trigger120s, 4096 corners, transitions, 2359296 fine oracles, metadata, clone/persistence passed\n");
#undef FM_CHECK
    return true;
}

int main() {
    if (!test_fm_strike_legacy() || !test_fm_drone()) return 1;
    if (!test_torso_engine()) return 1;
    if (!test_play_window_projection()) return 1;
    if (!test_sequence_audition() || !test_sequence_lifecycle()) return 1;
    // Project-session guard: runtime edits dirty the project; destructive work
    // is deferred until explicit discard/save, while clean work proceeds.
    {
        ProjectSession session;
        session.reset_loaded(3);
        if (session.dirty() || session.active_slot() != 3) {
            printf("FAIL: project session load baseline\n"); return 1;
        }
        session.mark_mutated();
        if (!session.dirty() || session.request(DestructiveAction::LoadProject, 5) ||
            !session.has_pending() || session.pending().slot != 5) {
            printf("FAIL: project session dirty guard\n"); return 1;
        }
        session.mark_saved(3);
        if (session.dirty() || !session.has_pending()) {
            printf("FAIL: project session save/pending\n"); return 1;
        }
        const auto pending = session.take_pending();
        if (pending.action != DestructiveAction::LoadProject || pending.slot != 5 || session.has_pending()) {
            printf("FAIL: project session pending take\n"); return 1;
        }
        session.mark_mutated();
        if (session.request(DestructiveAction::ExitApp)) {
            printf("FAIL: dirty exit bypassed guard\n"); return 1;
        }
        session.cancel_pending();
        session.reset_new();
        if (session.dirty() || session.active_slot() != -1 ||
            !session.request(DestructiveAction::ExitApp)) {
            printf("FAIL: clean new project session\n"); return 1;
        }
    }

    // FMS-inspired safe chaos: deterministic, bounded, saturating shape-only jitter.
    {
        q15 a[SIGIL_POINTS] = {}, b[SIGIL_POINTS] = {};
        a[0] = b[0] = 32760;
        a[SIGIL_POINTS - 1] = b[SIGIL_POINTS - 1] = -32760;
        const uint32_t sa = jitter_sigil_curve(a, 0x12345678u, 4096);
        const uint32_t sb = jitter_sigil_curve(b, 0x12345678u, 4096);
        bool changed = false;
        for (int i = 0; i < SIGIL_POINTS; ++i) {
            if (a[i] != b[i]) { printf("FAIL: jitter determinism\n"); return 1; }
            if (a[i] != (i == 0 ? 32760 : i == SIGIL_POINTS - 1 ? -32760 : 0)) changed = true;
            if (a[i] == (q15)-32768) { printf("FAIL: jitter saturation\n"); return 1; }
        }
        if (!changed || sa != sb) { printf("FAIL: jitter mutation/state\n"); return 1; }
    }

    // FOLD contract: full-scale DRIVE and every MODE endpoint remain defined;
    // MIX=0 is exact dry and focused telemetry reports effective controls.
    {
        FoldUnit fold;
        for (int p = 0; p < fold.num_params; ++p) fold.params[p].effective = fold.params[p].base;
        fold.params[FoldUnit::P_DRIVE].effective = trackr::fx::Q15_ONE;
        fold.params[FoldUnit::P_TRIM].effective = trackr::fx::Q15_ONE;
        fold.params[FoldUnit::P_BIAS].effective = -32767;
        fold.params[FoldUnit::P_MIX].effective = trackr::fx::Q15_ONE;
        for (q15 mode : { (q15)0, (q15)16384, (q15)32767 }) {
            fold.params[FoldUnit::P_MODE].effective = mode;
            q15 extreme[64];
            for (int i = 0; i < 64; ++i) extreme[i] = (i & 1) ? 32767 : -32768;
            fold.process(extreme, 64);
        }
        fold.params[FoldUnit::P_MIX].effective = 0;
        q15 dry[8] = {-32768, -24000, -1, 0, 1, 24000, 32767, 12345};
        q15 expected[8]; std::memcpy(expected, dry, sizeof dry);
        fold.process(dry, 8);
        if (std::memcmp(dry, expected, sizeof dry) != 0) {
            printf("FAIL: FOLD MIX zero is not dry\n"); return 1;
        }
        Engine focused;
        auto* focused_fold = new FoldUnit();
        focused_fold->params[FoldUnit::P_DRIVE].effective = 12345;
        focused_fold->params[FoldUnit::P_TRIM].effective = 23456;
        focused_fold->params[FoldUnit::P_MODE].effective = 21000;
        focused_fold->params[FoldUnit::P_BIAS].effective = -12000;
        focused_fold->params[FoldUnit::P_MIX].effective = 20000;
        focused.replace_unit(3, 5, focused_fold);
        EngineUiSnapshot snapshot;
        focused.capture_ui_snapshot(snapshot, 3, 5);
        if (!snapshot.fold_focus_valid || snapshot.fold_focus_chain != 3 ||
            snapshot.fold_focus_slot != 5 ||
            snapshot.focus_effective[FoldUnit::P_DRIVE] != 12345 ||
            snapshot.focus_effective[FoldUnit::P_TRIM] != 23456 ||
            snapshot.focus_effective[FoldUnit::P_MODE] != 21000 ||
            snapshot.focus_effective[FoldUnit::P_BIAS] != -12000 ||
            snapshot.focus_effective[FoldUnit::P_MIX] != 20000) {
            printf("FAIL: focused FOLD snapshot\n"); return 1;
        }
        focused.capture_ui_snapshot(snapshot);
        if (snapshot.fold_focus_valid || snapshot.fold_focus_chain != -1 ||
            snapshot.fold_focus_slot != -1 || snapshot.focus_effective[0] != 0) {
            printf("FAIL: focused FOLD snapshot clear\n"); return 1;
        }
    }

    // FILT contract: extreme DRIVE/MORPH are defined, OFF is exact dry, reset
    // applies a newly selected type immediately, and focused telemetry is tagged.
    {
        FilterUnit filter;
        filter.params[FilterUnit::P_CUTOFF].base = filter.params[FilterUnit::P_CUTOFF].effective = 17000;
        filter.params[FilterUnit::P_RES].base = filter.params[FilterUnit::P_RES].effective = 29000;
        filter.params[FilterUnit::P_DRIVE].base = filter.params[FilterUnit::P_DRIVE].effective = trackr::fx::Q15_ONE;
        filter.params[FilterUnit::P_MORPH].base = filter.params[FilterUnit::P_MORPH].effective = trackr::fx::Q15_ONE;
        filter.type = trackr::dsp::FilterType::LPF;
        filter.reset();
        q15 extreme[64];
        for (int i = 0; i < 64; ++i) extreme[i] = (i & 1) ? 32767 : -32768;
        filter.process(extreme, 64);
        filter.type = trackr::dsp::FilterType::Off;
        filter.reset();
        q15 dry[8] = {-32768, -24000, -1, 0, 1, 24000, 32767, 12345};
        q15 expected[8]; std::memcpy(expected, dry, sizeof dry);
        filter.process(dry, 8);
        if (std::memcmp(dry, expected, sizeof dry) != 0) {
            printf("FAIL: FILT OFF is not exact dry\n"); return 1;
        }
        filter.type = trackr::dsp::FilterType::HPF;
        filter.reset(); // no intervening control tick
        q15 dc[32]; for (q15& value : dc) value = 12000;
        filter.process(dc, 32);
        bool changed = false;
        for (q15 value : dc) if (value != 12000) { changed = true; break; }
        if (!changed) {
            printf("FAIL: FILT immediate type/reset\n"); return 1;
        }
        if (trackr::fx::soft_clip_q31(INT32_MIN) >= 0) {
            printf("FAIL: soft clip INT32_MIN sign\n"); return 1;
        }

        Engine focused;
        auto* focused_filter = new FilterUnit();
        focused_filter->params[FilterUnit::P_CUTOFF].effective = 1000;
        focused_filter->params[FilterUnit::P_RES].effective = 22000;
        focused_filter->params[FilterUnit::P_DRIVE].effective = 12345;
        focused_filter->params[FilterUnit::P_MORPH].effective = 23456;
        focused_filter->control_tick();
        focused.replace_unit(2, 4, focused_filter);
        EngineUiSnapshot snapshot;
        focused.capture_ui_snapshot(snapshot, 2, 4);
        if (!snapshot.filter_focus_valid || snapshot.filter_focus_chain != 2 ||
            snapshot.filter_focus_slot != 4 || snapshot.filter_cutoff == 0 ||
            snapshot.filter_drive != 12345 || snapshot.filter_morph != 23456) {
            printf("FAIL: focused FILT snapshot\n"); return 1;
        }
        focused.capture_ui_snapshot(snapshot);
        if (snapshot.filter_focus_valid || snapshot.filter_focus_chain != -1 ||
            snapshot.filter_focus_slot != -1 || snapshot.filter_cutoff != 0) {
            printf("FAIL: focused FILT snapshot clear\n"); return 1;
        }
    }

    // MIC source: independent cards must read the shared device history without
    // stealing one another's cursor, MON mutes output without losing telemetry,
    // and the 32730->32000 fractional conversion remains bounded.
    {
        MicUnit::reset_input();
        q15 source[2048];
        for (int i = 0; i < 2048; ++i) source[i] = (q15)(((i * 1237) & 32767) - 16384);
        MicUnit a, b;
        for (int p = 0; p < a.num_params; ++p) {
            a.params[p].effective = a.params[p].base;
            b.params[p].effective = b.params[p].base;
        }
        MicUnit::push(source, 2048);
        q15 out_a[512], out_b[512];
        a.process(out_a, 512); b.process(out_b, 512);
        if (std::memcmp(out_a, out_b, sizeof out_a) != 0 ||
            a.visual_peak() == 0 || b.visual_fill() == 0) {
            printf("FAIL: MIC independent readers/telemetry\n"); return 1;
        }
        MicUnit::reset_input();
        MicUnit::push(source, 1024);
        a.process(out_a, 256);
        if (a.visual_fill() == 0 || a.visual_fill() > 1024) {
            printf("FAIL: MIC epoch reset/rebase\n"); return 1;
        }
        b.params[MicUnit::P_MON].effective = 0;
        MicUnit::push(source, 512); b.process(out_b, 512);
        for (q15 sample : out_b) if (sample != 0) {
            printf("FAIL: MIC monitor mute\n"); return 1;
        }
        Engine mic_ui;
        auto* focused_mic = new MicUnit();
        focused_mic->params[MicUnit::P_GAIN].effective = 12345;
        focused_mic->params[MicUnit::P_MON].effective = 32767;
        MicUnit::push(source, 1024); q15 probe[256]; focused_mic->process(probe, 256);
        mic_ui.replace_unit(0, 0, focused_mic);
        EngineUiSnapshot snapshot;
        mic_ui.capture_ui_snapshot(snapshot, 0, 0);
        if (!snapshot.effect_focus_valid ||
            snapshot.effect_focus_kind != (uint8_t)UnitKind::MicIn ||
            snapshot.focus_effective[MicUnit::P_GAIN] != 12345 || snapshot.mic_peak == 0) {
            printf("FAIL: MIC focused telemetry\n"); return 1;
        }
    }

    // Legacy VCA contract is frozen. Flexible AHD is covered by vca_dsp_test.cpp.
    // Silent before trigger, instant-attack threshold, then decay and reset.
    {
        VcaUnit vca;
        vca.set_flexible(false);
        q15 input[64];
        for (q15& value : input) value = 12000;
        vca.process(input, 64);
        for (q15 value : input) if (value != 0) { printf("FAIL: VCA pre-trigger gate\n"); return 1; }
        vca.params[VcaUnit::P_ATTACK].base = vca.params[VcaUnit::P_ATTACK].effective = 0;
        vca.params[VcaUnit::P_DECAY].base = vca.params[VcaUnit::P_DECAY].effective = 4096;
        vca.trigger();
        if (!(vca.visual_state() & VcaUnit::VisualActive) ||
            (vca.visual_state() & VcaUnit::VisualRising) ||
            vca.visual_envelope() != trackr::fx::Q15_ONE) {
            printf("FAIL: VCA instant-attack telemetry\n"); return 1;
        }
        for (q15& value : input) value = 12000;
        vca.process(input, 64);
        if (input[0] <= 0 || input[63] >= input[0]) {
            printf("FAIL: VCA triggered decay\n"); return 1;
        }
        if (!(vca.visual_state() & VcaUnit::VisualActive) ||
            vca.visual_envelope() >= trackr::fx::Q15_ONE) {
            printf("FAIL: VCA decay telemetry\n"); return 1;
        }
        vca.reset();
        if (vca.visual_state() != 0 || vca.visual_envelope() != 0) {
            printf("FAIL: VCA reset telemetry\n"); return 1;
        }
        vca.params[VcaUnit::P_ATTACK].base = vca.params[VcaUnit::P_ATTACK].effective = 0;
        vca.params[VcaUnit::P_DECAY].base = vca.params[VcaUnit::P_DECAY].effective = 0;
        for (q15& value : input) value = 12000;
        vca.process(input, 64);
        if (!(vca.visual_state() & VcaUnit::VisualGainOnly) ||
            vca.visual_envelope() != trackr::fx::Q15_ONE) {
            printf("FAIL: VCA gain-only telemetry\n"); return 1;
        }
        vca.reset();
        vca.params[VcaUnit::P_DECAY].base = vca.params[VcaUnit::P_DECAY].effective = 4096;
        for (q15& value : input) value = 12000;
        vca.process(input, 64);
        for (q15 value : input) if (value != 0) { printf("FAIL: VCA reset gate\n"); return 1; }
    }

    // PLAY contract: exact 1x mapping, tiny samples and stale window/sample swaps
    // stay in bounds, and focused telemetry follows the real cursor.
    {
        auto& bank = SampleBank::instance();
        auto& tiny = bank.slot(63);
        tiny = {};
        tiny.channels = 1;
        tiny.data = {12000};
        PlayUnit one;
        one.sample_slot = 63;
        one.control_tick();
        one.trigger();
        q15 one_out[8]; for (q15& value : one_out) value = 777;
        one.process(one_out, 8);
        for (q15 value : one_out) if (value != 0) {
            printf("FAIL: PLAY one-frame safety\n"); return 1;
        }

        auto& sample = bank.slot(62);
        sample = {};
        sample.channels = 1;
        sample.data.resize(128);
        for (int i = 0; i < 128; ++i) sample.data[i] = (q15)(i * 128 - 8192);
        PlayUnit play;
        play.sample_slot = 62;
        if (play.params[PlayUnit::P_SPEED].base != 8192) {
            printf("FAIL: PLAY default 1x mapping\n"); return 1;
        }
        for (int p = 0; p < play.num_params; ++p) play.params[p].snap();
        play.control_tick();
        play.trigger();
        q15 block[16] = {};
        play.process(block, 16);
        if (!(play.visual_state() & PlayUnit::VisualPlaying) ||
            play.visual_position() == 0 || play.visual_start() != 0 ||
            play.visual_end() != 32767) {
            printf("FAIL: PLAY focused diagnostics\n"); return 1;
        }
        sample.chops[0] = 0;
        sample.chops[1] = 64;
        sample.slice_reverse_mask = 0x0002;
        play.params[PlayUnit::P_SLICE].base = (q15)(((2 * 32767) + 8192) /
                                                     (trackr::synth::Sample::MAX_CHOPS + 1));
        play.params[PlayUnit::P_SLICE].snap();
        play.control_tick();
        play.trigger();
        q15 reverse_block[4] = {};
        play.process(reverse_block, 4);
        if (!(play.visual_state() & PlayUnit::VisualReverse) ||
            play.visual_speed() >= 0 || play.visual_start() == 0) {
            printf("FAIL: PLAY per-slice reverse state=%u pos=%u start=%u end=%u speed=%d\n",
                   play.visual_state(), play.visual_position(), play.visual_start(),
                   play.visual_end(), play.visual_speed()); return 1;
        }
        sample.slice_reverse_mask = 0;
        play.params[PlayUnit::P_SLICE].base = 0;
        play.params[PlayUnit::P_SLICE].snap();

        {
            Engine focused;
            auto* focused_play = new PlayUnit();
            focused_play->sample_slot = 62;
            for (int p = 0; p < focused_play->num_params; ++p) focused_play->params[p].snap();
            focused_play->control_tick();
            focused_play->trigger();
            focused_play->process(block, 16);
            focused.replace_unit(1, 2, focused_play);
            EngineUiSnapshot snapshot;
            focused.capture_ui_snapshot(snapshot, 1, 2);
            if (!snapshot.play_focus_valid || snapshot.play_focus_chain != 1 ||
                snapshot.play_focus_slot != 2 ||
                !(snapshot.play_state & PlayUnit::VisualPlaying) ||
                snapshot.play_position == 0) {
                printf("FAIL: focused PLAY snapshot\n"); return 1;
            }
            focused.capture_ui_snapshot(snapshot);
            if (snapshot.play_focus_valid || snapshot.play_focus_chain != -1 ||
                snapshot.play_focus_slot != -1 || snapshot.play_state != 0) {
                printf("FAIL: focused PLAY snapshot clear\n"); return 1;
            }
        }
        sample.data.resize(2); // replacement before the next control tick
        play.process(block, 16);
        if (play.visual_state() != 0) {
            printf("FAIL: PLAY stale-window replacement safety\n"); return 1;
        }
        tiny = {};
        sample = {};
    }

    // DLY contract: TIME is bounded/slewed, HOLD seals the inlet while keeping
    // the loop alive, and bounded diagnostics mirror the actual read head/energy.
    {
        DelayUnit delay;
        if (!delay.ready()) { printf("FAIL: DLY allocation\n"); return 1; }
        delay.params[DelayUnit::P_TIME].base = delay.params[DelayUnit::P_TIME].effective = 0;
        delay.params[DelayUnit::P_FEEDBACK].base = delay.params[DelayUnit::P_FEEDBACK].effective = 20000;
        delay.params[DelayUnit::P_MIX].base = delay.params[DelayUnit::P_MIX].effective = trackr::fx::Q15_ONE;
        delay.params[DelayUnit::P_WOW].base = delay.params[DelayUnit::P_WOW].effective = 0;
        delay.params[DelayUnit::P_SAT].base = delay.params[DelayUnit::P_SAT].effective = 0;
        delay.params[DelayUnit::P_HOLD].base = delay.params[DelayUnit::P_HOLD].effective = 0;
        delay.control_tick();
        delay.reset();
        q15 impulse[96] = {};
        impulse[0] = 12000;
        delay.process(impulse, 96);
        if (delay.visual_delay_frames() < 1 || delay.visual_delay_frames() > 64 ||
            delay.visual_wow_offset() != 0 || delay.visual_wet_peak() == 0 ||
            delay.visual_hold()) {
            printf("FAIL: DLY visual diagnostics\n"); return 1;
        }
        delay.params[DelayUnit::P_HOLD].base = delay.params[DelayUnit::P_HOLD].effective = trackr::fx::Q15_ONE;
        q15 held[64] = {};
        held[0] = 12000;
        delay.process(held, 64);
        if (!delay.visual_hold() || delay.visual_delay_frames() < 1 ||
            delay.visual_delay_frames() >= 32000) {
            printf("FAIL: DLY hold diagnostics\n"); return 1;
        }
        delay.reset();
        if (delay.visual_hold() || delay.visual_wet_peak() != 0 ||
            delay.visual_wow_offset() != 0) {
            printf("FAIL: DLY reset diagnostics\n"); return 1;
        }

        // Extreme SAT/feedback must reach the soft clip through a defined 64-bit
        // multiply, including the -32768 absolute-peak edge.
        delay.params[DelayUnit::P_FEEDBACK].effective = trackr::fx::Q15_ONE;
        delay.params[DelayUnit::P_SAT].effective = trackr::fx::Q15_ONE;
        delay.params[DelayUnit::P_HOLD].effective = 0;
        q15 extreme[64];
        for (int i = 0; i < 64; ++i) extreme[i] = (i & 1) ? 32767 : -32768;
        delay.process(extreme, 64);
        if (delay.visual_delay_frames() < 1 || delay.visual_delay_frames() >= 32000) {
            printf("FAIL: DLY extreme bounds\n"); return 1;
        }
    }

    // Sample ownership + budget: REC COW must preserve shared PLAY data and
    // failures must leave both unit target and bank contents untouched.
    {
        using trackr::synth::SampleEditResult;
        auto& bank = SampleBank::instance();
        for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) bank.clear_slot(slot);
        bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);

        Engine owner;
        auto* play_ref = static_cast<PlayUnit*>(make_unit(UnitKind::SamplePlayer));
        play_ref->sample_slot = 16;
        owner.replace_unit(0, 0, play_ref);
        auto& shared = bank.slot(16);
        shared.channels = 1;
        shared.logical_frames = UINT32_MAX;
        shared.data.assign(8, 1234);
        bank.set_provenance(16, trackr::synth::SampleOrigin::SdWav, "/tmp/shared.wav");

        auto* rec_a = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        owner.replace_unit(1, 0, rec_a);
        if (owner.prepare_recorder(*rec_a, false) != SampleEditResult::SharedSlot ||
            rec_a->sample_slot != 16 || shared.data[0] != 1234) {
            printf("FAIL: rec shared-slot report\n"); return 1;
        }
        if (owner.prepare_recorder(*rec_a, true) != SampleEditResult::Ok || rec_a->sample_slot == 16 ||
            shared.data[0] != 1234 || std::strcmp(bank.provenance(16).path, "/tmp/shared.wav")) {
            printf("FAIL: rec cow detach\n"); return 1;
        }

        // Planning is allocation-free; staging owns a private vector and the
        // guarded commit is only generation validation + move/swap.
        RecWorkspacePlan staged_plan;
        auto* rec_staged = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        owner.replace_unit(0, 1, rec_staged);
        if (owner.plan_recorder(*rec_staged, true, staged_plan) != SampleEditResult::Ok ||
            !staged_plan.staged.data.empty() || Engine::stage_recorder(staged_plan) != SampleEditResult::Ok ||
            staged_plan.staged.data.size() != staged_plan.frames ||
            owner.commit_recorder(staged_plan) != SampleEditResult::Ok ||
            bank.slot(rec_staged->sample_slot).data.size() != staged_plan.frames) {
            printf("FAIL: staged rec workspace\n"); return 1;
        }
        RecWorkspacePlan stale_plan;
        auto* rec_stale = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        owner.replace_unit(0, 2, rec_stale);
        if (owner.plan_recorder(*rec_stale, true, stale_plan) != SampleEditResult::Ok ||
            Engine::stage_recorder(stale_plan) != SampleEditResult::Ok) {
            printf("FAIL: stale rec plan setup\n"); return 1;
        }
        bank.clear_slot(stale_plan.target_slot); // generation conflict before commit
        const int stale_old_slot = rec_stale->sample_slot;
        if (owner.commit_recorder(stale_plan) != SampleEditResult::SharedSlot ||
            rec_stale->sample_slot != stale_old_slot) {
            printf("FAIL: staged rec generation rollback\n"); return 1;
        }
        auto* rec_b = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        owner.replace_unit(2, 0, rec_b);
        if (owner.prepare_recorder(*rec_b, true) != SampleEditResult::Ok ||
            rec_b->sample_slot == rec_a->sample_slot || rec_b->sample_slot == 16) {
            printf("FAIL: rec private allocation\n"); return 1;
        }

        const int old_slot = rec_b->sample_slot;
        const std::size_t used = bank.pcm_bytes_used();
        bank.set_pcm_budget_for_test(used);
        rec_b->params[RecUnit::P_LEN].base = trackr::fx::Q15_ONE;
        if (owner.prepare_recorder(*rec_b, true) != SampleEditResult::MemoryBudget ||
            rec_b->sample_slot != old_slot) {
            printf("FAIL: rec memory budget rollback\n"); return 1;
        }
        bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);

        // A finalized take must survive stray retrigger attempts until an explicit
        // clear/re-take action releases it.
        RecUnit protected_take;
        protected_take.sample_slot = 47;
        auto& protected_sample = bank.slot(47);
        protected_sample.channels = 1;
        protected_sample.data.assign(2048, 4321);
        protected_sample.logical_frames = 2048;
        protected_take.adopt_take(2048);
        protected_take.params[RecUnit::P_LEN].effective = 0;
        protected_take.trigger();
        if (protected_take.is_recording() || protected_take.recorded_frames() != 2048 ||
            protected_sample.data[0] != 4321 || protected_sample.logical_frames != 2048) {
            printf("FAIL: REC protected finalized take\n"); return 1;
        }
        protected_take.params[RecUnit::P_LEN].base = -32768;
        if (protected_take.required_capacity_frames() != 1024) {
            printf("FAIL: REC negative LEN clamp\n"); return 1;
        }

        Engine focused_rec;
        auto* only_a = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        auto* only_b = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        focused_rec.replace_unit(0, 0, only_a);
        focused_rec.replace_unit(0, 1, only_b);
        if (focused_rec.prepare_recorder(*only_a, true) != SampleEditResult::Ok ||
            focused_rec.prepare_recorder(*only_b, true) != SampleEditResult::Ok ||
            focused_rec.request_recorder_record(0, 1, true, LaunchQuantize::Immediate) != SampleEditResult::Ok ||
            only_a->is_recording() || !only_b->is_recording()) {
            printf("FAIL: focused REC command\n"); return 1;
        }
        only_b->stop();

        // REC capture follows transport even when chain output is muted or the REC
        // card is bypassed; those controls affect audition, never the stored take.
        Engine muted_capture;
        muted_capture.replace_unit(0, 0, make_unit(UnitKind::Osc));
        auto* muted_rec = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        muted_rec->params[RecUnit::P_LEN].base = 0;
        muted_capture.replace_unit(0, 1, muted_rec);
        if (muted_capture.prepare_recorder(*muted_rec, true) != SampleEditResult::Ok ||
            muted_capture.request_recorder_record(0, 1, true, LaunchQuantize::Immediate) != SampleEditResult::Ok) {
            printf("FAIL: REC muted capture prepare\n"); return 1;
        }
        muted_capture.chains[0].mute = true;
        muted_rec->bypass = true;
        q15 muted_audio[64 * 2] = {};
        muted_capture.render(muted_audio, 64);
        if (muted_rec->recorded_frames() != 64) {
            printf("FAIL: REC capture paused by mute/bypass\n"); return 1;
        }

        // Focused quantized token must never retarget after a topology edit, and an
        // opposite command before the boundary cancels a pending start cleanly.
        Engine token;
        auto* token_rec = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        token_rec->params[RecUnit::P_LEN].base = 0;
        token.replace_unit(1, 2, token_rec);
        if (token.prepare_recorder(*token_rec, true) != SampleEditResult::Ok) {
            printf("FAIL: focused REC token prepare\n"); return 1;
        }
        token.transport_play();
        if (token.request_recorder_record(1, 2, true, LaunchQuantize::Beat) != SampleEditResult::Ok ||
            !token.recorder_record_pending(1, 2) ||
            token.request_recorder_record(1, 2, false, LaunchQuantize::Beat) != SampleEditResult::Ok ||
            token.recorder_record_pending(1, 2) || token_rec->is_recording()) {
            printf("FAIL: focused REC pending cancel\n"); return 1;
        }
        token.request_recorder_record(1, 2, true, LaunchQuantize::Beat);
        if (token.next_recorder_deadline() != token.frames_per_beat()) {
            printf("FAIL: focused REC exact deadline\n"); return 1;
        }
        Unit* displaced_token = token.replace_unit(1, 2, make_unit(UnitKind::Osc));
        static q15 token_audio[16000 * 2] = {};
        token.render(token_audio, 16000);
        if (token_rec->is_recording()) {
            printf("FAIL: stale focused REC token fired\n"); return 1;
        }
        delete displaced_token;

        // Clear keeps physical workspace but makes the slot truthfully silent.
        if (owner.clear_recorder(*rec_a) != SampleEditResult::Ok ||
            !bank.slot(rec_a->sample_slot).empty() || !bank.slot(rec_a->sample_slot).has_storage()) {
            printf("FAIL: rec logical clear\n"); return 1;
        }
        PlayUnit silence;
        silence.sample_slot = rec_a->sample_slot;
        silence.trigger();
        q15 silent[16]; for (q15& value : silent) value = 777;
        silence.process(silent, 16);
        for (q15 value : silent) if (value != 0) { printf("FAIL: cleared REC playback\n"); return 1; }

        // A bank with no logically empty target reports NoFreeSlot and does not
        // rewrite the REC alias or any existing sample.
        const int cow_slot = rec_a->sample_slot;
        for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) {
            auto& occupied = bank.slot(slot);
            if (occupied.empty()) {
                occupied.channels = 1;
                occupied.logical_frames = UINT32_MAX;
                occupied.data.assign(1, (q15)(slot + 1));
            }
        }
        auto* rec_full = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        rec_full->sample_slot = cow_slot;
        owner.replace_unit(3, 0, rec_full);
        const q15 preserved = bank.slot(cow_slot).data[0];
        if (owner.prepare_recorder(*rec_full, true) != SampleEditResult::NoFreeSlot ||
            rec_full->sample_slot != cow_slot || bank.slot(cow_slot).data[0] != preserved) {
            printf("FAIL: rec full-bank rollback\n"); return 1;
        }

        // Multi-REC aggregate budget must reject before any owner/slot changes.
        for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) bank.clear_slot(slot);
        Engine aggregate;
        auto* aggregate_a = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        auto* aggregate_b = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        aggregate_a->params[RecUnit::P_LEN].base = trackr::fx::Q15_ONE;
        aggregate_b->params[RecUnit::P_LEN].base = trackr::fx::Q15_ONE;
        aggregate.replace_unit(0, 0, aggregate_a);
        aggregate.replace_unit(0, 1, aggregate_b);
        RecWorkspacePlan aggregate_plans[UNITS_PER_CHAIN];
        int aggregate_count = 0;
        if (aggregate.plan_chain_record(0, aggregate_plans, aggregate_count) != SampleEditResult::Ok)
            { printf("FAIL: aggregate REC plan\n"); return 1; }
        for (int i = 0; i < aggregate_count; ++i)
            if (Engine::stage_recorder(aggregate_plans[i]) != SampleEditResult::Ok)
                { printf("FAIL: aggregate REC stage\n"); return 1; }
        const int aggregate_a_old = aggregate_a->sample_slot;
        const int aggregate_b_old = aggregate_b->sample_slot;
        bank.set_pcm_budget_for_test(aggregate_plans[0].staged.data.capacity() * sizeof(q15));
        if (aggregate.commit_chain_record(aggregate_plans, aggregate_count) != SampleEditResult::MemoryBudget ||
            aggregate_a->sample_slot != aggregate_a_old || aggregate_b->sample_slot != aggregate_b_old ||
            bank.slot(aggregate_plans[0].target_slot).has_storage() ||
            bank.slot(aggregate_plans[1].target_slot).has_storage()) {
            printf("FAIL: aggregate REC rollback\n"); return 1;
        }
        bank.set_pcm_budget_for_test(SampleBank::DEFAULT_PCM_BUDGET_BYTES);

        for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) bank.clear_slot(slot);
    }

    // Generic musical triggers must not restart/erase a finalized recorder take.
    {
        auto& bank = SampleBank::instance();
        Engine trigger_owner;
        auto* rec = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        trigger_owner.replace_unit(0, 0, rec);
        if (trigger_owner.prepare_recorder(*rec, true) != trackr::synth::SampleEditResult::Ok)
            { printf("FAIL: trigger REC setup\n"); return 1; }
        rec->params[RecUnit::P_REC].effective = trackr::fx::Q15_ONE;
        rec->trigger();
        q15 recorded[16]; for (q15& value : recorded) value = 1234;
        rec->process(recorded, 16);
        rec->stop();
        const uint32_t frames = rec->recorded_frames();
        const q15 first = bank.slot(rec->sample_slot).data[0];
        trigger_owner.trigger_chain(0);
        if (rec->is_recording() || rec->recorded_frames() != frames ||
            bank.slot(rec->sample_slot).data[0] != first) {
            printf("FAIL: generic trigger erased REC take\n"); return 1;
        }
        bank.clear_slot(rec->sample_slot);
    }

    // synthetic sample in slot 0: 1s decaying 220hz saw-ish tone
    auto& s = SampleBank::instance().slot(0);
    s.channels = 1;
    s.data.resize(32000);
    for (int i = 0; i < 32000; ++i) {
        float t = (float)i / 32000.f;
        float ph = fmodf(t * 220.f, 1.f);
        s.data[i] = (int16_t)((ph * 2.f - 1.f) * 20000.f * (1.f - t));
    }

    Engine* eng = new Engine();
    eng->chains[0].pan = -12000;

    SampleBank::instance().slot(0).chops[0] = 0;
    SampleBank::instance().slot(0).chops[1] = 16000;
    SampleBank::instance().slot(0).slice_reverse_mask = 0x0002;
    patchio::SampleManifest reverse_manifest;
    Engine reverse_engine;
    const char* reverse_path = "/tmp/seance_slice_reverse.snc";
    if (!patchio::save(*eng, reverse_path) ||
        !patchio::load(reverse_engine, reverse_path, &reverse_manifest) ||
        !reverse_manifest.has_slice_reverse || reverse_manifest.slice_reverse[0] != 0x0002) {
        printf("FAIL: slice reverse patch roundtrip\n"); return 1;
    }
    SampleBank::instance().slot(0).slice_reverse_mask = 0;
    std::remove(reverse_path);

    // chain 0: PLAY -> FILT -> DLY
    auto* play = (PlayUnit*)make_unit(UnitKind::SamplePlayer);
    play->params[PlayUnit::P_LOOP].base = 32767;
    auto* filt = (FilterUnit*)make_unit(UnitKind::Filter);
    auto* dly  = (DelayUnit*)make_unit(UnitKind::Delay);
    eng->replace_unit(0, 0, play);
    eng->replace_unit(0, 1, filt);
    eng->replace_unit(0, 2, dly);

    // FOLLOW mode must read the real PLAY cursor across its audible lifetime,
    // including reverse progress, and return to zero once non-looping PLAY ends.
    {
        auto& follow_sample = SampleBank::instance().slot(40);
        follow_sample.channels = 1;
        follow_sample.logical_frames = UINT32_MAX;
        follow_sample.data.assign(4096, 12000);
        Engine follow_engine;
        auto* follow_play = new PlayUnit();
        follow_play->sample_slot = 40;
        follow_play->params[PlayUnit::P_LOOP].base = 0;
        follow_play->params[PlayUnit::P_LOOP].snap();
        follow_play->control_tick();
        follow_engine.replace_unit(2, 0, follow_play);
        Sigil& follow = follow_engine.sigils[0];
        follow.mode = SigilMode::Follow;
        follow_engine.set_step_dest(0, 2);
        for (int i = 0; i < SIGIL_POINTS; ++i) follow.set_point(i, (q15)(i * 256 - 16384));
        follow_engine.trigger_chain(2);
        q15 follow_audio[4096 * 2] = {};
        follow_engine.render(follow_audio, 1024);
        const uint32_t forward_phase = follow.phase;
        if (!follow.follow_active || forward_phase == 0 || follow.value == 0) {
            printf("FAIL: Sigil FOLLOW did not track active PLAY\n"); return 1;
        }
        follow_engine.render(follow_audio, 1024);
        if (follow.phase <= forward_phase) {
            printf("FAIL: Sigil FOLLOW phase did not advance with PLAY\n"); return 1;
        }
        follow_engine.render(follow_audio, 4096);
        follow_engine.render(follow_audio, 1);
        if (follow.follow_active || follow.value != 0) {
            printf("FAIL: Sigil FOLLOW stayed active after PLAY ended\n"); return 1;
        }

        follow_play->params[PlayUnit::P_SPEED].base = -8192;
        follow_play->params[PlayUnit::P_SPEED].snap();
        follow_play->control_tick();
        follow_engine.trigger_chain(2);
        follow_engine.render(follow_audio, 1024);
        if (!follow.follow_active || follow.phase == 0 ||
            !(follow_play->visual_state() & PlayUnit::VisualReverse)) {
            printf("FAIL: Sigil FOLLOW reverse PLAY progress\n"); return 1;
        }
        follow_sample = {};
    }

    // sigil A: drawn triangle in Loop mode -> filter cutoff
    Sigil& sa = eng->sigils[0];
    for (int i = 0; i < SIGIL_POINTS; ++i) {
        int tri = i < 64 ? i * 512 : (127 - i) * 512;
        sa.set_point(i, (trackr::fx::q15)(tri - 16384));
    }
    sa.set_rate_norm(12000, CONTROL_RATE);
    // new-api coverage: public setter must reach both the cache and the sigil
    eng->set_sigil_rate(0, 12000);
    if (eng->sigil_rate(0) != 12000) { printf("FAIL: sigil_rate roundtrip\n"); return 1; }
    filt->params[FilterUnit::P_CUTOFF].base = 16000;
    filt->params[FilterUnit::P_CUTOFF].src = ModSource::SigilA;
    filt->params[FilterUnit::P_CUTOFF].mod_depth = 14000;

    // sigil B: steps mode, a few high buckets -> retrigs chain 1... but route
    // to chain 0 for the test via mode; here just verify it steps without crash
    Sigil& sb = eng->sigils[1];
    for (int i = 0; i < SIGIL_POINTS; ++i)
        sb.set_point(i, (i / 8) % 3 == 0 ? 20000 : 0);
    sb.mode = SigilMode::Steps;
    sb.set_rate_norm(6000, CONTROL_RATE);

    // sigil chaining: A bends B's rate
    eng->set_sigil_mod(1, ModSource::SigilA, 8000);
    // reject test: B must NOT be able to modulate A (forward ref)
    eng->set_sigil_mod(0, ModSource::SigilB, 8000);
    if (eng->sigil_mods[0].src != ModSource::None) {
        printf("FAIL: forward sigil ref accepted\n"); return 1;
    }

    play->trigger();

    // render 4s
    constexpr int TOTAL = 32000 * 4;
    static trackr::fx::q15 out[TOTAL * 2];
    eng->render(out, TOTAL);

    // sanity: rms, peak, dc, nan-free by construction
    double sum = 0, dc = 0;
    int peak = 0;
    for (int i = 0; i < TOTAL * 2; ++i) {
        int v = out[i];
        sum += (double)v * v;
        dc  += v;
        if (abs(v) > peak) peak = abs(v);
    }
    double rms = sqrt(sum / (TOTAL * 2));
    dc /= (TOTAL * 2);
    printf("rms=%.1f peak=%d dc=%.2f\n", rms, peak, dc);

    // click scan: adjacent-sample jumps > 12000 (excluding trigger point)
    int clicks = 0;
    for (int i = 2; i < TOTAL * 2 - 2; i += 2) {
        int d = abs(out[i] - out[i - 2]);
        if (d > 12000) ++clicks;
    }
    printf("clicks(|d|>12000)=%d\n", clicks);

    // meters: chain 0 was audible, chain 3 empty; scope must have non-zero samples
    printf("chain_peak = %d %d %d %d\n",
           eng->chain_peak[0], eng->chain_peak[1], eng->chain_peak[2], eng->chain_peak[3]);
    bool scope_alive = false;
    for (int i = 0; i < Engine::SCOPE_LEN; ++i)
        if (eng->scope[i] != 0) { scope_alive = true; break; }
    if (!scope_alive) { printf("FAIL: scope empty\n"); return 1; }
    if (eng->chain_peak[3] != 0) { printf("FAIL: empty chain has peak\n"); return 1; }

    // patch io roundtrip: save -> fresh engine -> load -> compare
    {
        const char* pp = "/tmp/seance_patch.snc";
        if (MAX_PARAMS != 12 || play->num_params > MAX_PARAMS) {
            printf("FAIL: ParamBank capacity\n"); return 1;
        }
        eng->set_bpm(137);
        eng->set_sigil_div(1, 4);
        eng->set_step_dest(1, 3);
        eng->set_sigil_sync(1, true);
        eng->replace_unit(1, 0, make_unit(UnitKind::Reverb));
        eng->chains[1].units[0]->params[ReverbUnit::P_DECAY].base = 23456;
        eng->chains[1].units[0]->params[ReverbUnit::P_DAMP].base = 17654;
        eng->chains[1].units[0]->params[ReverbUnit::P_FREEZE].base = 32767;
        auto* bit_rt = static_cast<BitUnit*>(make_unit(UnitKind::Bitcrush));
        bit_rt->params[BitUnit::P_BITS].base = 4321;
        bit_rt->params[BitUnit::P_RATE].base = 8765;
        bit_rt->params[BitUnit::P_MIX].base = 22222;
        bit_rt->params[BitUnit::P_JITTER].base = 13579;
        bit_rt->params[BitUnit::P_DROPOUT].base = 24680;
        bit_rt->params[BitUnit::P_DRIFT].base = -12345;
        bit_rt->params[BitUnit::P_DRIFT].src = ModSource::SigilC;
        bit_rt->params[BitUnit::P_DRIFT].mod_depth = -9876;
        bit_rt->params[BitUnit::P_COMP].base = 30000;
        eng->replace_unit(1, 1, bit_rt);
        auto* tilt_rt = static_cast<TiltUnit*>(make_unit(UnitKind::Tilt));
        tilt_rt->params[TiltUnit::P_TILT].base = -12345;
        tilt_rt->params[TiltUnit::P_RANGE].base = 27654;
        tilt_rt->params[TiltUnit::P_RANGE].src = ModSource::SigilD;
        tilt_rt->params[TiltUnit::P_RANGE].mod_depth = -7654;
        eng->replace_unit(1, 2, tilt_rt);
        auto* ring_rt = static_cast<RingUnit*>(make_unit(UnitKind::Ring));
        ring_rt->params[RingUnit::P_FEEDBACK].base = -13579;
        ring_rt->params[RingUnit::P_FOLLOW].base = 24680;
        ring_rt->params[RingUnit::P_FOLLOW].src = ModSource::SigilB;
        ring_rt->params[RingUnit::P_FOLLOW].mod_depth = -8642;
        eng->replace_unit(1, 3, ring_rt);
        auto* noise_rt = static_cast<NoiseUnit*>(make_unit(UnitKind::Noise));
        noise_rt->params[NoiseUnit::P_COLOUR].base = 30000;
        noise_rt->params[NoiseUnit::P_TONE].src = ModSource::SigilC;
        noise_rt->params[NoiseUnit::P_TONE].mod_depth = -6000;
        noise_rt->params[NoiseUnit::P_BODY].base = 12345;
        noise_rt->params[NoiseUnit::P_DECAY].base = 23456;
        eng->replace_unit(1, 4, noise_rt);
        auto* fm_rt = static_cast<FmUnit*>(make_unit(UnitKind::Fm));
        fm_rt->params[FmUnit::P_RATIO].base = 17777;
        fm_rt->params[FmUnit::P_INDEX].base = 28888;
        fm_rt->params[FmUnit::P_INDEX].src = ModSource::SigilA;
        fm_rt->params[FmUnit::P_INDEX].mod_depth = 7000;
        fm_rt->params[FmUnit::P_INDEX_FALL].base = 12345;
        fm_rt->params[FmUnit::P_ATTACK].base = 6789;
        fm_rt->params[FmUnit::P_RATIO_FINE].base = -4321;
        fm_rt->params[FmUnit::P_FEEDBACK_FALL].base = 22222;
        fm_rt->phase_reset = true;
        fm_rt->alias_safe = true;
        eng->replace_unit(1, 5, fm_rt);
        auto* motor_rt = static_cast<MotorUnit*>(make_unit(UnitKind::Motor));
        motor_rt->params[MotorUnit::P_RPM].base = 12345;
        motor_rt->params[MotorUnit::P_SOURCE].base = 32767;
        eng->replace_unit(2, 5, motor_rt);
        auto* strain_rt = static_cast<StrainUnit*>(make_unit(UnitKind::Strain));
        strain_rt->params[StrainUnit::P_DRIVE].base = 27654;
        strain_rt->params[StrainUnit::P_BIAS].base = -7654;
        strain_rt->params[StrainUnit::P_BODY].src = ModSource::SigilB;
        strain_rt->params[StrainUnit::P_BODY].mod_depth = 10987;
        strain_rt->params[StrainUnit::P_FEEDBACK].base = 19876;
        eng->replace_unit(2, 2, strain_rt);
        play->pitch_quantize = true;
        auto* osc_q = (OscUnit*)make_unit(UnitKind::Osc);
        osc_q->pitch_quantize = true;
        osc_q->wave = OscUnit::Wave::Saw;
        osc_q->params[OscUnit::P_FOLD].base = 13579;
        osc_q->params[OscUnit::P_FOLD].src = ModSource::SigilA;
        osc_q->params[OscUnit::P_FOLD].mod_depth = 12000;
        osc_q->params[OscUnit::P_SUB].base = 8192;
        osc_q->params[OscUnit::P_DRIFT].base = 4096;
        eng->replace_unit(2, 0, osc_q);
        // GRAN cloud persistence: the six appended scheduler params live in the
        // upper ParamBank slots, and the clock lock rides the existing flag byte.
        auto* gran_rt = (GrainUnit*)make_unit(UnitKind::Grain);
        gran_rt->sample_slot = 7;
        gran_rt->clock_sync = true;
        gran_rt->sync_division = 2;
        gran_rt->params[GrainUnit::P_DENS].base = 21111;
        gran_rt->params[GrainUnit::P_JITTER].base = 19222;
        gran_rt->params[GrainUnit::P_ATTACK].base = 7333;
        gran_rt->params[GrainUnit::P_RELEASE].base = 26444;
        gran_rt->params[GrainUnit::P_PITCH_DEV].base = 15555;
        gran_rt->params[GrainUnit::P_PITCH_DEV].src = ModSource::SigilB;
        gran_rt->params[GrainUnit::P_PITCH_DEV].mod_depth = -8111;
        gran_rt->params[GrainUnit::P_VOICES].base = 9666;
        eng->replace_unit(2, 1, gran_rt);
        auto* strn_rt = (StranularUnit*)make_unit(UnitKind::Stranular);
        strn_rt->params[StranularUnit::P_SPRAY].base = 12345;
        strn_rt->params[StranularUnit::P_REVERSE].base = 23456;
        strn_rt->params[StranularUnit::P_PITCH_ENV].base = -12000;
        strn_rt->params[StranularUnit::P_HOLD].base = trackr::fx::Q15_ONE;
        strn_rt->window_mode = 2;
        strn_rt->long_mode = true;
        // Exercise the upper half of SNC7's 12-entry ParamBank even before a
        // production unit exposes all slots through num_params.
        strn_rt->num_params = PARAM_BANK_SIZE;
        strn_rt->params[11].base = 27123;
        strn_rt->params[11].src = ModSource::SigilD;
        strn_rt->params[11].mod_depth = -12345;
        eng->scenes[0][0].valid = true;
        eng->scenes[0][0].kind[1] = UnitKind::Filter;
        eng->scenes[0][0].base[1][FilterUnit::P_CUTOFF] = 4321;
        eng->scene_morph[0] = 12345;
        std::strcpy(eng->macro_pads[2].name, "DREAD");
        eng->macro_pads[2].route_count = 1;
        eng->macro_pads[2].routes[0] = {0, 1, FilterUnit::P_CUTOFF, 1000, 30000};
        eng->performance_bindings[(int)PerformanceControl::FaceA] = {
            PerformanceAction::ChainMute, 3, PerformanceMode::Press
        };
        eng->performance_bindings[(int)PerformanceControl::LDpadLeft] = {
            PerformanceAction::MacroPad, 6, PerformanceMode::Toggle
        };
        eng->performance_bindings[(int)PerformanceControl::LRFaceY] = {
            PerformanceAction::GlobalFx, (uint8_t)GlobalFxTarget::Reverb, PerformanceMode::Hold
        };
        eng->replace_unit(3, 0, strn_rt);
        eng->transport_playing = true;
        eng->beats_per_bar = 3;
        eng->launch_quantize = LaunchQuantize::Bar;
        SampleBank::instance().set_provenance(0, trackr::synth::SampleOrigin::SdWav,
                                              "/tmp/source kick.wav");
        SampleBank::instance().slot(0).chops[0] = 1234;
        SampleBank::instance().slot(0).chops[1] = 5678;
        WorkspaceLayout saved_workspace;
        workspace_layout_defaults(saved_workspace);
        saved_workspace.node[0][0] = {137, 91};
        saved_workspace.node[3][5] = {420, 222};
        patchio::PatchDocument workspace_document;
        if (!workspace_document.valid() || !patchio::capture(*eng, workspace_document, &saved_workspace) ||
            !patchio::save(workspace_document, pp)) { printf("FAIL: patch/workspace save\n"); return 1; }
        Engine* e2 = new Engine();
        patchio::SampleManifest manifest;
        WorkspaceLayout loaded_workspace;
        if (!seance::patchio::load(*e2, pp, &manifest, &loaded_workspace) ||
            !manifest.has_sample_manifest || !manifest.has_chops ||
            manifest.chops[0][0] != 1234 || manifest.chops[0][1] != 5678) {
            printf("FAIL: patch load/manifest/chop marker\n"); return 1;
        }
        Unit* f2 = e2->chains[0].units[1];
        bool rt = f2 && f2->kind == UnitKind::Filter
               && f2->params[FilterUnit::P_CUTOFF].base == 16000
               && f2->params[FilterUnit::P_CUTOFF].src == ModSource::SigilA
               && f2->params[FilterUnit::P_CUTOFF].mod_depth == 14000
               && e2->chains[0].pan == -12000
               && e2->bpm == 137
               && e2->sigil_sync[1]
               && e2->sigil_div[1] == 4
               && e2->step_dest[1] == 3
               && e2->sigil_rate(0) == 12000
               && e2->sigils[0].table[32] == eng->sigils[0].table[32]
               && e2->chains[0].units[2] && e2->chains[0].units[2]->kind == UnitKind::Delay
               && e2->chains[1].units[0] && e2->chains[1].units[0]->kind == UnitKind::Reverb
               && e2->chains[1].units[0]->params[ReverbUnit::P_DECAY].base == 23456
               && e2->chains[1].units[0]->params[ReverbUnit::P_DAMP].base == 17654
               && e2->chains[1].units[0]->params[ReverbUnit::P_FREEZE].base == 32767
               && e2->chains[1].units[1] && e2->chains[1].units[1]->kind == UnitKind::Bitcrush
               && e2->chains[1].units[1]->params[BitUnit::P_BITS].base == 4321
               && e2->chains[1].units[1]->params[BitUnit::P_RATE].base == 8765
               && e2->chains[1].units[1]->params[BitUnit::P_MIX].base == 22222
               && e2->chains[1].units[1]->params[BitUnit::P_JITTER].base == 13579
               && e2->chains[1].units[1]->params[BitUnit::P_DROPOUT].base == 24680
               && e2->chains[1].units[1]->params[BitUnit::P_DRIFT].base == -12345
               && e2->chains[1].units[1]->params[BitUnit::P_DRIFT].src == ModSource::SigilC
               && e2->chains[1].units[1]->params[BitUnit::P_DRIFT].mod_depth == -9876
               && e2->chains[1].units[1]->params[BitUnit::P_COMP].base == 30000
               && e2->chains[1].units[2] && e2->chains[1].units[2]->kind == UnitKind::Tilt
               && e2->chains[1].units[2]->params[TiltUnit::P_TILT].base == -12345
               && e2->chains[1].units[2]->params[TiltUnit::P_RANGE].base == 27654
               && e2->chains[1].units[2]->params[TiltUnit::P_RANGE].src == ModSource::SigilD
               && e2->chains[1].units[2]->params[TiltUnit::P_RANGE].mod_depth == -7654
               && e2->chains[1].units[3] && e2->chains[1].units[3]->kind == UnitKind::Ring
               && e2->chains[1].units[3]->params[RingUnit::P_FEEDBACK].base == -13579
               && e2->chains[1].units[3]->params[RingUnit::P_FOLLOW].base == 24680
               && e2->chains[1].units[3]->params[RingUnit::P_FOLLOW].src == ModSource::SigilB
               && e2->chains[1].units[3]->params[RingUnit::P_FOLLOW].mod_depth == -8642
               && e2->chains[1].units[4] && e2->chains[1].units[4]->kind == UnitKind::Noise
               && e2->chains[1].units[4]->params[NoiseUnit::P_COLOUR].base == 30000
               && e2->chains[1].units[4]->params[NoiseUnit::P_TONE].src == ModSource::SigilC
               && e2->chains[1].units[4]->params[NoiseUnit::P_TONE].mod_depth == -6000
               && e2->chains[1].units[4]->params[NoiseUnit::P_BODY].base == 12345
               && e2->chains[1].units[4]->params[NoiseUnit::P_DECAY].base == 23456
               && e2->chains[1].units[5] && e2->chains[1].units[5]->kind == UnitKind::Fm
               && e2->chains[1].units[5]->params[FmUnit::P_RATIO].base == 17777
               && e2->chains[1].units[5]->params[FmUnit::P_INDEX].base == 28888
               && e2->chains[1].units[5]->params[FmUnit::P_INDEX].src == ModSource::SigilA
               && e2->chains[1].units[5]->params[FmUnit::P_INDEX].mod_depth == 7000
               && e2->chains[1].units[5]->params[FmUnit::P_INDEX_FALL].base == 12345
               && e2->chains[1].units[5]->params[FmUnit::P_ATTACK].base == 6789
               && e2->chains[1].units[5]->params[FmUnit::P_RATIO_FINE].base == -4321
               && e2->chains[1].units[5]->params[FmUnit::P_FEEDBACK_FALL].base == 22222
               && static_cast<FmUnit*>(e2->chains[1].units[5])->phase_reset
               && static_cast<FmUnit*>(e2->chains[1].units[5])->alias_safe
               && e2->chains[2].units[5] && e2->chains[2].units[5]->kind == UnitKind::Motor
               && e2->chains[2].units[5]->params[MotorUnit::P_RPM].base == 12345
               && e2->chains[2].units[5]->params[MotorUnit::P_SOURCE].base == 32767
               && static_cast<PlayUnit*>(e2->chains[0].units[0])->pitch_quantize
               && e2->chains[2].units[0] && e2->chains[2].units[0]->kind == UnitKind::Osc
               && static_cast<OscUnit*>(e2->chains[2].units[0])->pitch_quantize
               && static_cast<OscUnit*>(e2->chains[2].units[0])->wave == OscUnit::Wave::Saw
               && e2->chains[2].units[0]->params[OscUnit::P_FOLD].base == 13579
               && e2->chains[2].units[0]->params[OscUnit::P_FOLD].src == ModSource::SigilA
               && e2->chains[2].units[0]->params[OscUnit::P_FOLD].mod_depth == 12000
               && e2->chains[2].units[0]->params[OscUnit::P_SUB].base == 8192
               && e2->chains[2].units[0]->params[OscUnit::P_DRIFT].base == 4096
               && e2->chains[2].units[1] && e2->chains[2].units[1]->kind == UnitKind::Grain
               && static_cast<GrainUnit*>(e2->chains[2].units[1])->sample_slot == 7
               && static_cast<GrainUnit*>(e2->chains[2].units[1])->clock_sync
               && static_cast<GrainUnit*>(e2->chains[2].units[1])->sync_division == 2
               && e2->chains[2].units[1]->params[GrainUnit::P_DENS].base == 21111
               && e2->chains[2].units[1]->params[GrainUnit::P_JITTER].base == 19222
               && e2->chains[2].units[1]->params[GrainUnit::P_ATTACK].base == 7333
               && e2->chains[2].units[1]->params[GrainUnit::P_RELEASE].base == 26444
               && e2->chains[2].units[1]->params[GrainUnit::P_PITCH_DEV].base == 15555
               && e2->chains[2].units[1]->params[GrainUnit::P_PITCH_DEV].src == ModSource::SigilB
               && e2->chains[2].units[1]->params[GrainUnit::P_PITCH_DEV].mod_depth == -8111
               && e2->chains[2].units[1]->params[GrainUnit::P_VOICES].base == 9666
               && e2->chains[2].units[2] && e2->chains[2].units[2]->kind == UnitKind::Strain
               && e2->chains[2].units[2]->params[StrainUnit::P_DRIVE].base == 27654
               && e2->chains[2].units[2]->params[StrainUnit::P_BIAS].base == -7654
               && e2->chains[2].units[2]->params[StrainUnit::P_BODY].src == ModSource::SigilB
               && e2->chains[2].units[2]->params[StrainUnit::P_BODY].mod_depth == 10987
               && e2->chains[2].units[2]->params[StrainUnit::P_FEEDBACK].base == 19876
               && manifest.slots[0].origin == (unsigned char)trackr::synth::SampleOrigin::SdWav
               && e2->transport_playing && e2->beats_per_bar == 3 &&
                  e2->launch_quantize == LaunchQuantize::Bar
               && e2->chains[3].units[0] && e2->chains[3].units[0]->kind == UnitKind::Stranular
               && e2->chains[3].units[0]->params[StranularUnit::P_SPRAY].base == 12345
               && e2->chains[3].units[0]->params[StranularUnit::P_REVERSE].base == 23456
               && e2->chains[3].units[0]->params[StranularUnit::P_PITCH_ENV].base == -12000
               && e2->chains[3].units[0]->params[StranularUnit::P_HOLD].base == trackr::fx::Q15_ONE
               && static_cast<StranularUnit*>(e2->chains[3].units[0])->window_mode == 2
               && static_cast<StranularUnit*>(e2->chains[3].units[0])->long_mode
               && e2->chains[3].units[0]->params[11].base == 27123
               && e2->chains[3].units[0]->params[11].src == ModSource::SigilD
               && e2->chains[3].units[0]->params[11].mod_depth == -12345
               && e2->scenes[0][0].valid
               && e2->scenes[0][0].base[1][FilterUnit::P_CUTOFF] == 4321
               && e2->scene_morph[0] == 12345
               && std::strcmp(e2->macro_pads[2].name, "DREAD") == 0
               && e2->macro_pads[2].routes[0].maximum == 30000
               && e2->performance_bindings[(int)PerformanceControl::FaceA].action == PerformanceAction::ChainMute
               && e2->performance_bindings[(int)PerformanceControl::FaceA].target == 3
               && e2->performance_bindings[(int)PerformanceControl::LDpadLeft].action == PerformanceAction::MacroPad
               && e2->performance_bindings[(int)PerformanceControl::LDpadLeft].target == 6
               && e2->performance_bindings[(int)PerformanceControl::LDpadLeft].mode == PerformanceMode::Toggle
               && e2->performance_bindings[(int)PerformanceControl::LRFaceY].action == PerformanceAction::GlobalFx
               && e2->performance_bindings[(int)PerformanceControl::LRFaceY].target == (uint8_t)GlobalFxTarget::Reverb
               && std::strcmp(manifest.slots[0].path, "/tmp/source kick.wav") == 0
               && loaded_workspace.node[0][0].x == 137
               && loaded_workspace.node[0][0].y == 91
               && loaded_workspace.node[3][5].x == 420
               && loaded_workspace.node[3][5].y == 222;
        Engine* swap_target = new Engine();
        swap_target->replace_unit(3, 5, make_unit(UnitKind::Osc));
        swap_target->swap_patch_state(*e2);
        bool swapped = swap_target->chains[0].units[1] &&
                       swap_target->chains[0].units[1]->kind == UnitKind::Filter &&
                       swap_target->bpm == 137 &&
                       swap_target->performance_bindings[(int)PerformanceControl::FaceA].action == PerformanceAction::ChainMute &&
                       swap_target->performance_bindings[(int)PerformanceControl::LDpadLeft].mode == PerformanceMode::Toggle &&
                       swap_target->performance_bindings[(int)PerformanceControl::LRFaceY].action == PerformanceAction::GlobalFx &&
                       e2->chains[3].units[5] && e2->chains[3].units[5]->kind == UnitKind::Osc;
        delete swap_target;
        delete e2;
        if (!rt || !swapped) { printf("FAIL: patch roundtrip/swap mismatch\n"); return 1; }
        printf("patch roundtrip ok\n");

        // A captured save document must not observe later live mutations. This is
        // the contract that lets the 3DS release its audio guard before fopen.
        const char* snapshot_path = "/tmp/seance_patch_document.snc";
        patchio::PatchDocument document;
        if (!document.valid() || !patchio::capture(*eng, document)) {
            printf("FAIL: patch document capture\n"); return 1;
        }
        eng->set_bpm(91);
        SampleBank::instance().set_provenance(0, trackr::synth::SampleOrigin::Runtime,
                                              "/tmp/mutated-after-capture.wav");
        if (!patchio::save(document, snapshot_path)) {
            printf("FAIL: patch document save\n"); return 1;
        }
        Engine snapshot_loaded;
        patchio::SampleManifest snapshot_manifest;
        if (!patchio::load(snapshot_loaded, snapshot_path, &snapshot_manifest) ||
            snapshot_loaded.bpm != 137 ||
            snapshot_manifest.slots[0].origin != (unsigned char)trackr::synth::SampleOrigin::SdWav ||
            std::strcmp(snapshot_manifest.slots[0].path, "/tmp/source kick.wav")) {
            printf("FAIL: patch document changed after capture\n"); return 1;
        }
        eng->set_bpm(137);
        SampleBank::instance().set_provenance(0, trackr::synth::SampleOrigin::SdWav,
                                              "/tmp/source kick.wav");
        std::remove(snapshot_path);

        // Missing recovery must clear stale PCM from the previous project while
        // retaining the source path and an explicit missing marker.
        auto& keep = SampleBank::instance().slot(0);
        keep.data.assign(4, 777);
        auto& stale_tail = SampleBank::instance().slot(63);
        stale_tail.data.assign(3, 999);
        patchio::restore_samples(manifest, 32000, 32000);
        if (!manifest.slots[0].missing || !keep.empty() || !stale_tail.empty() ||
            !SampleBank::instance().provenance(0).missing ||
            std::strcmp(SampleBank::instance().provenance(0).path, "/tmp/source kick.wav")) {
            printf("FAIL: missing/all-slot stale PCM clear\n"); return 1;
        }

        // Runtime slots are project-local scratch and likewise cannot inherit
        // bytes from whichever project happened to be loaded before them.
        keep.data.assign(4, 555);
        manifest.slots[0] = {};
        patchio::restore_samples(manifest, 32000, 32000);
        if (!keep.empty() || SampleBank::instance().provenance(0).origin != trackr::synth::SampleOrigin::Runtime ||
            SampleBank::instance().provenance(0).missing || SampleBank::instance().provenance(0).path[0]) {
            printf("FAIL: runtime sample clears stale PCM\n"); return 1;
        }

        // Truncation must not mutate either Engine state or manifest output.
        const char* corrupt = "/tmp/seance_patch_corrupt.snc";
        FILE* src = std::fopen(pp, "rb"); FILE* dstf = std::fopen(corrupt, "wb");
        unsigned char chunk[64]; size_t got = src ? std::fread(chunk, 1, sizeof chunk, src) : 0;
        if (src) std::fclose(src); if (dstf) { std::fwrite(chunk, 1, got, dstf); std::fclose(dstf); }
        Engine untouched; untouched.bpm = 222;
        patchio::SampleManifest sentinel; std::strcpy(sentinel.slots[0].path, "unchanged");
        WorkspaceLayout workspace_sentinel;
        workspace_layout_defaults(workspace_sentinel);
        workspace_sentinel.node[0][0] = {300, 200};
        if (patchio::load(untouched, corrupt, &sentinel, &workspace_sentinel) || untouched.bpm != 222 ||
            std::strcmp(sentinel.slots[0].path, "unchanged") != 0 ||
            workspace_sentinel.node[0][0].x != 300 || workspace_sentinel.node[0][0].y != 200) {
            printf("FAIL: corrupt patch transaction\n"); return 1;
        }
        std::remove(corrupt);
    }

    // Malformed WAV regression: short fmt and invalid limits must fail without
    // touching an existing destination sample.
    {
        const char* badp = "/tmp/seance_bad.wav";
        FILE* bf = std::fopen(badp, "wb");
        if (!bf) { printf("FAIL: malformed wav setup\n"); return 1; }
        const unsigned char bad[] = {
            'R','I','F','F', 12,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 0,0,0,0
        };
        std::fwrite(bad, 1, sizeof bad, bf);
        std::fclose(bf);
        trackr::synth::Sample keep;
        keep.data.assign(4, 1234);
        auto br = trackr::synth::load_wav_to_sample(badp, keep, 32000, 64);
        std::remove(badp);
        if (br != trackr::synth::WavLoadResult::NoFmtChunk || keep.data.size() != 4 || keep.data[0] != 1234 ||
            trackr::synth::load_wav_to_sample("/tmp/nope.wav", keep, 0, 64) != trackr::synth::WavLoadResult::UnsupportedFormat) {
            printf("FAIL: malformed wav validation\n"); return 1;
        }
        // Workspace preflight must reject before allocation and preserve dst.
        const char* wp = "/tmp/seance_preflight.wav";
        trackr::synth::Sample source;
        source.channels = 2;
        source.data.assign(128, 321);
        if (trackr::synth::save_sample_wav(wp, source, 32000) != trackr::synth::WavSaveResult::Ok) {
            printf("FAIL: wav preflight setup\n"); return 1;
        }
        trackr::synth::WavDecodeLimits tiny;
        tiny.workspace_bytes = 16;
        auto before = keep.data;
        auto limited = trackr::synth::load_wav_to_sample(wp, keep, 32000, 128, tiny);
        std::remove(wp);
        if (limited != trackr::synth::WavLoadResult::MemoryBudget || keep.data != before) {
            printf("FAIL: wav workspace preflight rollback\n"); return 1;
        }
    }

    // Recorder regression: capture/finalize must not resize or reallocate in audio path.
    {
        auto* rec = (RecUnit*)make_unit(UnitKind::Recorder);
        rec->sample_slot = 20;
        rec->prepare_slot();
        auto& rs = SampleBank::instance().slot(20);
        const auto* data_before = rs.data.data();
        const size_t size_before = rs.data.size();
        rec->params[RecUnit::P_REC].base = trackr::fx::Q15_ONE;
        rec->params[RecUnit::P_REC].effective = trackr::fx::Q15_ONE;
        rec->trigger();
        trackr::fx::q15 rb[64];
        for (int i = 0; i < 64; ++i) rb[i] = (trackr::fx::q15)(i * 200 - 6000);
        rec->process(rb, 64);
        rec->params[RecUnit::P_REC].base = 0;
        rec->params[RecUnit::P_REC].effective = 0;
        rec->stop();
        if (rs.empty() || rs.loop_end != 64 ||
            rs.data[10] != (trackr::fx::q15)(10 * 200 - 6000) ||
            rs.data.data() != data_before || rs.data.size() != size_before ||
            rec->is_recording()) {
            printf("FAIL: rec unit capture/finalize\n"); return 1;
        }
        rec->clear_take();
        if (rec->recorded_frames() != 0 || rs.loop_end != 0 || rec->is_recording()) {
            printf("FAIL: rec clear\n"); return 1;
        }
        rec->adopt_take(32);
        if (rec->recorded_frames() != 32 || rs.loop_end != 32) {
            printf("FAIL: rec adopt\n"); return 1;
        }
        // Fixed LEN must stop at the trigger-time target even while REC remains high.
        rec->clear_take();
        rec->params[RecUnit::P_LEN].base = 0;
        rec->params[RecUnit::P_LEN].effective = 0;
        rec->prepare_slot();
        const uint32_t fixed_target = rec->target_frames();
        rec->params[RecUnit::P_REC].effective = trackr::fx::Q15_ONE;
        rec->trigger();
        trackr::fx::q15 full[64] = {};
        while (rec->is_recording()) rec->process(full, 64);
        if (rec->is_recording() || rec->recorded_frames() != fixed_target ||
            rec->recorded_frames() != rec->capacity_frames()) {
            printf("FAIL: rec fixed length finalize\n"); return 1;
        }
        delete rec;
    }

    // UI snapshot keeps REC metadata for every slot but samples only the focused
    // recorder. Changing or clearing focus must not leak the previous trace.
    {
        Engine rec_ui;
        auto* first = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        auto* second = static_cast<RecUnit*>(make_unit(UnitKind::Recorder));
        first->sample_slot = 20;
        second->sample_slot = 21;
        rec_ui.replace_unit(0, 1, first);
        rec_ui.replace_unit(2, 3, second);
        if (rec_ui.prepare_recorder(*first, true) != trackr::synth::SampleEditResult::Ok ||
            rec_ui.prepare_recorder(*second, true) != trackr::synth::SampleEditResult::Ok) {
            printf("FAIL: focused REC snapshot setup\n"); return 1;
        }
        auto& first_sample = SampleBank::instance().slot(first->sample_slot);
        auto& second_sample = SampleBank::instance().slot(second->sample_slot);
        for (int i = 0; i < 64; ++i) {
            first_sample.data[i] = (q15)(i * 200 - 6000);
            second_sample.data[i] = (q15)(6000 - i * 150);
        }
        first->adopt_take(64);
        second->adopt_take(64);

        EngineUiSnapshot focused;
        rec_ui.capture_ui_snapshot(focused, 0, 1);
        if (focused.rec_frames[0][1] != 64 ||
            focused.rec_frames[2][3] != 64 || !focused.rec_waveform_valid ||
            focused.rec_waveform_chain != 0 || focused.rec_waveform_slot != 1 ||
            focused.rec_waveform[0] != -6000 ||
            focused.rec_waveform[63] != first_sample.data[63]) {
            printf("FAIL: focused REC snapshot trace/metadata\n"); return 1;
        }
        rec_ui.capture_ui_snapshot(focused, 2, 3);
        if (!focused.rec_waveform_valid || focused.rec_waveform_chain != 2 ||
            focused.rec_waveform_slot != 3 ||
            focused.rec_waveform[0] != 6000 ||
            focused.rec_waveform[63] != second_sample.data[63]) {
            printf("FAIL: focused REC snapshot switch\n"); return 1;
        }
        // Peak-per-bin capture must retain a transient between evenly spaced
        // sample points instead of drawing a falsely flat take.
        first_sample.data[0] = 100;
        first_sample.data[1] = -20000;
        for (int i = 2; i < 128; ++i) first_sample.data[i] = 100;
        first->adopt_take(128);
        rec_ui.capture_ui_snapshot(focused, 0, 1);
        if (focused.rec_waveform[0] != -20000) {
            printf("FAIL: focused REC peak-bin transient\n"); return 1;
        }

        rec_ui.capture_ui_snapshot(focused);
        if (focused.rec_waveform_valid || focused.rec_waveform_chain != -1 ||
            focused.rec_waveform_slot != -1) {
            printf("FAIL: focused REC snapshot clear\n"); return 1;
        }
        for (q15 value : focused.rec_waveform) if (value != 0) {
            printf("FAIL: focused REC snapshot stale data\n"); return 1;
        }
    }

    // WAV export regression: logical loop_end must limit the written take.
    {
        trackr::synth::Sample ws;
        ws.channels = 1;
        ws.data.resize(64);
        for (int i = 0; i < 64; ++i) ws.data[i] = (trackr::fx::q15)(i * 300 - 9000);
        ws.loop_start = 0;
        ws.loop_end = 32;
        const char* wp = "/tmp/seance_export.wav";
        if (trackr::synth::save_sample_wav(wp, ws, 32000) != trackr::synth::WavSaveResult::Ok) {
            printf("FAIL: wav export\n"); return 1;
        }
        struct StreamSource { const int16_t* data; uint32_t frames; } stream_source{ws.data.data(), 32};
        auto stream_read = [](void* opaque, uint32_t first, uint32_t frames, int16_t* out) -> bool {
            auto& source = *static_cast<StreamSource*>(opaque);
            if (first > source.frames || frames > source.frames - first) return false;
            std::memcpy(out, source.data + first, (size_t)frames * sizeof(int16_t));
            return true;
        };
        int16_t tiny_scratch[7];
        const char* stream_path = "/tmp/seance_export_stream.wav";
        if (trackr::synth::save_wav_stream(stream_path, 1, 32, 32000,
                tiny_scratch, 7, stream_read, &stream_source) != trackr::synth::WavSaveResult::Ok) {
            printf("FAIL: chunked wav export\n"); return 1;
        }
        trackr::synth::Sample stream_loaded;
        auto stream_result = trackr::synth::load_wav_to_sample(stream_path, stream_loaded, 32000, 32000);
        if ((stream_result != trackr::synth::WavLoadResult::Ok &&
             stream_result != trackr::synth::WavLoadResult::Truncated) ||
                stream_loaded.data.size() < 32 ||
                !std::equal(stream_loaded.data.begin(), stream_loaded.data.begin() + 30, ws.data.begin())) {
            printf("FAIL: chunked wav export mismatch\n"); return 1;
        }
        std::remove(stream_path);
        trackr::synth::Sample loaded;
        auto lr = trackr::synth::load_wav_to_sample(wp, loaded, 32000, 128);
        std::remove(wp);
        if ((int)lr < 0 || loaded.num_frames() != 32 || loaded.data[10] != ws.data[10]) {
            printf("FAIL: wav export roundtrip\n"); return 1;
        }
    }

    // New generators: deterministic, bounded sources with truthful trigger semantics.
    {
        NoiseUnit noise;
        q15 first[257] = {}, second[257] = {};
        noise.control_tick(); noise.process(first, 257); noise.reset(); noise.control_tick(); noise.process(second, 257);
        int noise_peak = 0;
        for (int i = 0; i < 257; ++i) {
            if (first[i] != second[i]) { printf("FAIL: NOISE deterministic reset\n"); return 1; }
            noise_peak = std::max(noise_peak, abs((int)first[i]));
        }
        if (!noise_peak || noise.kind != UnitKind::Noise || noise.num_params != NoiseUnit::P_COUNT ||
            noise.params[NoiseUnit::P_BODY].base != 0 ||
            noise.params[NoiseUnit::P_DECAY].base != 32767) {
            printf("FAIL: NOISE source\n"); return 1;
        }
        // CRACKLE density must be a meaningful event-rate control. Compare the
        // number of large upward transients from identical reset state.
        auto crackle_events = [](q15 density) {
            NoiseUnit n;
            n.params[NoiseUnit::P_COLOUR].base = 24000;
            n.params[NoiseUnit::P_COLOUR].effective = 24000;
            n.params[NoiseUnit::P_DENSITY].base = density;
            n.params[NoiseUnit::P_DENSITY].effective = density;
            n.params[NoiseUnit::P_TONE].base = 32767;
            n.params[NoiseUnit::P_TONE].effective = 32767;
            n.control_tick();
            q15 block[2048] = {};
            n.process(block, 2048);
            int events = 0;
            for (int i = 1; i < 2048; ++i)
                if (abs((int)block[i] - block[i - 1]) > 7000) ++events;
            return events;
        };
        const int sparse_crackle = crackle_events(1024);
        const int dense_crackle = crackle_events(24576);
        if (sparse_crackle >= dense_crackle || dense_crackle < 8) {
            printf("FAIL: NOISE crackle density %d/%d\n", sparse_crackle, dense_crackle);
            return 1;
        }
        // Legacy follow-decay remains a triggerable percussion source; new cards
        // use explicit MODE instead of hiding drone at maximum DECAY.
        NoiseUnit percussive;
        percussive.params[NoiseUnit::P_MODE].effective = 0;
        percussive.params[NoiseUnit::P_TONE].effective = 32767;
        percussive.params[NoiseUnit::P_DECAY].effective = 0;
        percussive.control_tick();
        percussive.trigger();
        q15 burst[2048] = {};
        percussive.process(burst, 2048);
        int burst_head = 0, burst_tail = 0;
        for (int i = 0; i < 128; ++i) burst_head = std::max(burst_head, abs((int)burst[i]));
        for (int i = 1920; i < 2048; ++i) burst_tail = std::max(burst_tail, abs((int)burst[i]));
        if (burst_head < 100 || burst_tail * 8 >= burst_head || percussive.visual_envelope() != 0) {
            printf("FAIL: NOISE decay envelope %d/%d/%u\n", burst_head, burst_tail,
                   (unsigned)percussive.visual_envelope());
            return 1;
        }
        NoiseUnit body_full, body_cut;
        body_full.params[NoiseUnit::P_TONE].effective = 32767;
        body_cut.params[NoiseUnit::P_TONE].effective = 32767;
        body_cut.params[NoiseUnit::P_BODY].effective = 32767;
        body_full.control_tick(); body_cut.control_tick();
        q15 full[1024] = {}, cut[1024] = {};
        body_full.process(full, 1024); body_cut.process(cut, 1024);
        int64_t full_dc = 0, cut_dc = 0;
        for (int i = 0; i < 1024; ++i) { full_dc += full[i]; cut_dc += cut[i]; }
        const int64_t full_dc_abs = full_dc < 0 ? -full_dc : full_dc;
        const int64_t cut_dc_abs = cut_dc < 0 ? -cut_dc : cut_dc;
        if (cut_dc_abs >= full_dc_abs) {
            printf("FAIL: NOISE body high-pass %lld/%lld\n",
                   (long long)full_dc, (long long)cut_dc);
            return 1;
        }

        FmUnit fm;
        q15 silent[64]; for (auto& sample : silent) sample = 1234;
        fm.control_tick(); fm.process(silent, 64);
        for (q15 sample : silent) if (sample != 0) { printf("FAIL: FM pre-trigger silence\n"); return 1; }
        fm.trigger(); q15 struck[512] = {}; fm.process(struck, 512);
        int fm_peak = 0; for (q15 sample : struck) fm_peak = std::max(fm_peak, abs((int)sample));
        if (!fm_peak || fm.visual_envelope() >= 32767 || fm.kind != UnitKind::Fm || fm.num_params != 10) {
            printf("FAIL: FM strike\n"); return 1;
        }
        fm.reset(); if (fm.visual_envelope() != 0) { printf("FAIL: FM reset\n"); return 1; }

        // Appended shape controls are neutral at zero and become independently
        // audible/stateful without changing the original six-parameter voice.
        FmUnit shaped;
        shaped.params[FmUnit::P_INDEX_FALL].effective = 32767;
        shaped.params[FmUnit::P_FEEDBACK_FALL].effective = 32767;
        shaped.params[FmUnit::P_ATTACK].effective = 32767;
        shaped.params[FmUnit::P_RATIO_FINE].effective = -32767;
        shaped.phase_reset = true;
        shaped.alias_safe = true;
        shaped.control_tick(); shaped.trigger();
        q15 shaped_attack[32] = {}; shaped.process(shaped_attack, 32);
        if (shaped.visual_envelope() == 0 || shaped.visual_envelope() >= 32767 ||
            shaped.visual_index_envelope() >= 32767) {
            printf("FAIL: FM shape controls\n"); return 1;
        }
        shaped.reset(); shaped.trigger(); q15 repeat_a[64] = {}; shaped.process(repeat_a, 64);
        shaped.reset(); shaped.trigger(); q15 repeat_b[64] = {}; shaped.process(repeat_b, 64);
        if (std::memcmp(repeat_a, repeat_b, sizeof repeat_a) != 0) {
            printf("FAIL: FM phase reset deterministic\n"); return 1;
        }

        MotorUnit motor;
        q15 driven[512] = {}; motor.control_tick(); motor.process(driven, 512);
        int motor_peak = 0; for (q15 sample : driven) motor_peak = std::max(motor_peak, abs((int)sample));
        if (!motor_peak || motor.visual_energy() == 0 || motor.kind != UnitKind::Motor || motor.num_params != 6) {
            printf("FAIL: MOTOR source\n"); return 1;
        }
        // RATE must remain monotonic across the old 32-bit overflow boundary
        // near 69% (about 530 Hz). Count pulse rising edges over one second.
        int previous_edges = -1;
        for (int pct : {60, 65, 70, 75, 100}) {
            MotorUnit sweep;
            sweep.params[MotorUnit::P_RPM].effective = (q15)(pct * 32767 / 100);
            sweep.params[MotorUnit::P_JITTER].effective = 0;
            sweep.params[MotorUnit::P_RESONANCE].effective = 0;
            sweep.params[MotorUnit::P_LOAD].effective = 0;
            sweep.params[MotorUnit::P_GAIN].effective = 32767;
            sweep.params[MotorUnit::P_SOURCE].effective = 16384;
            sweep.control_tick();
            q15 sweep_block[ENGINE_SR] = {};
            sweep.process(sweep_block, ENGINE_SR);
            int edges = 0;
            for (int frame = 1; frame < ENGINE_SR; ++frame)
                if (sweep_block[frame - 1] < 0 && sweep_block[frame] >= 0) ++edges;
            const int expected = 18 + pct * 742 / 100;
            if (edges <= previous_edges || edges < expected - 2 || edges > expected + 2) {
                printf("FAIL: MOTOR rate monotonic %d%% edges=%d expected=%d previous=%d\n",
                       pct, edges, expected, previous_edges);
                return 1;
            }
            previous_edges = edges;
        }

        Engine gen_ui;
        auto* focused = static_cast<FmUnit*>(make_unit(UnitKind::Fm));
        focused->params[FmUnit::P_INDEX].effective = 22222;
        focused->trigger(); q15 pulse[64] = {}; focused->process(pulse, 64);
        gen_ui.replace_unit(1, 2, focused);
        EngineUiSnapshot snapshot;
        gen_ui.capture_ui_snapshot(snapshot, 1, 2);
        bool fm_pitch_lane = false;
        bool non_pitch_sources = false;
        {
            Engine fm_seq;
            fm_seq.replace_unit(0, 0, make_unit(UnitKind::Fm));
            fm_seq.replace_unit(1, 0, make_unit(UnitKind::Noise));
            fm_seq.replace_unit(2, 0, make_unit(UnitKind::Motor));
            EngineUiSnapshot fm_snapshot;
            fm_seq.capture_ui_snapshot(fm_snapshot);
            fm_pitch_lane = fm_snapshot.sequence_pitch_kind[0] == (uint8_t)UnitKind::Fm &&
                fm_snapshot.sequence_pitch_param[0] == FmUnit::P_PITCH;
            non_pitch_sources = fm_snapshot.sequence_pitch_slot[1] < 0 &&
                fm_snapshot.sequence_pitch_slot[2] < 0;
        }
        if (!snapshot.effect_focus_valid || snapshot.effect_focus_kind != (uint8_t)UnitKind::Fm ||
            snapshot.focus_effective[FmUnit::P_INDEX] != 22222 || snapshot.generator_energy == 0 ||
            !fm_pitch_lane || !non_pitch_sources) {
            printf("FAIL: generator focused snapshot\n"); return 1;
        }
    }

    // Every factory product must be fully ready before ownership escapes.
    for (int kind = (int)UnitKind::SamplePlayer; kind < (int)UnitKind::Count; ++kind) {
        Unit* unit = make_unit((UnitKind)kind);
        if (!unit || !unit->ready()) {
            delete unit;
            printf("FAIL: unit allocation readiness contract\n"); return 1;
        }
        delete unit;
    }
    if (make_unit(UnitKind::Empty) != nullptr) {
        printf("FAIL: empty unit factory contract\n"); return 1;
    }

    // Clipboard clones copy patch-persistent controls but start with clean DSP state.
    // In particular STRN's three UI modes must not fall back to constructor defaults.
    {
        StranularUnit source;
        source.bypass = true;
        source.params[StranularUnit::P_PITCH].base = -12345;
        source.params[StranularUnit::P_PITCH].src = ModSource::SigilC;
        source.params[StranularUnit::P_PITCH].mod_depth = 23456;
        source.params[StranularUnit::P_PITCH].performance = 30000;
        source.params[StranularUnit::P_PITCH].performance_override = true;
        source.window_mode = 2;
        source.sync_division = 3;
        source.sync_interval_frames = 17777;
        source.long_mode = true;
        Unit* raw_clone = clone_unit(&source);
        auto* clone = raw_clone && raw_clone->kind == UnitKind::Stranular
            ? static_cast<StranularUnit*>(raw_clone) : nullptr;
        if (!clone || !clone->bypass || clone->params[StranularUnit::P_PITCH].base != -12345 ||
            clone->params[StranularUnit::P_PITCH].src != ModSource::SigilC ||
            clone->params[StranularUnit::P_PITCH].mod_depth != 23456 ||
            clone->params[StranularUnit::P_PITCH].effective != -12345 ||
            clone->params[StranularUnit::P_PITCH].performance_override ||
            clone->window_mode != 2 || clone->sync_division != 3 ||
            clone->sync_interval_frames != 17777 || !clone->long_mode) {
            delete raw_clone;
            printf("FAIL: persistent unit clone\n"); return 1;
        }
        delete clone;

        PlayUnit play;
        play.sample_slot = 23;
        play.pitch_quantize = true;
        auto* play_clone = static_cast<PlayUnit*>(clone_unit(&play));
        FilterUnit filter;
        filter.type = trackr::dsp::FilterType::HPF;
        auto* filter_clone = static_cast<FilterUnit*>(clone_unit(&filter));
        if (!play_clone || play_clone->sample_slot != 23 || !play_clone->pitch_quantize ||
            !filter_clone || filter_clone->type != trackr::dsp::FilterType::HPF || clone_unit(nullptr)) {
            delete play_clone; delete filter_clone;
            printf("FAIL: unit clone extras\n"); return 1;
        }
        delete play_clone; delete filter_clone;

        BitUnit bit_source;
        bit_source.params[BitUnit::P_DRIFT].base = -22222;
        bit_source.params[BitUnit::P_DRIFT].src = ModSource::SigilB;
        bit_source.params[BitUnit::P_DRIFT].mod_depth = 11111;
        q15 dirty[32];
        for (int i = 0; i < 32; ++i) dirty[i] = (q15)(i * 731 - 9000);
        bit_source.process(dirty, 32);
        auto* bit_clone = static_cast<BitUnit*>(clone_unit(&bit_source));
        if (!bit_clone || bit_clone->params[BitUnit::P_DRIFT].base != -22222 ||
            bit_clone->params[BitUnit::P_DRIFT].src != ModSource::SigilB ||
            bit_clone->params[BitUnit::P_DRIFT].mod_depth != 11111 ||
            bit_clone->visual_period() != 1 || bit_clone->visual_held() != 0 ||
            bit_clone->visual_dropout()) {
            delete bit_clone;
            printf("FAIL: BIT clone persistent/runtime split\n"); return 1;
        }
        delete bit_clone;
    }

    // GRAN is an eight-voice cloud. Its scheduler, voice ceiling, per-grain pitch
    // deviation, and bounded telemetry all have to hold at the endpoints, and the
    // published positions must stay inside the 0..95 display domain.
    {
        auto& grain_sample = SampleBank::instance().slot(41);
        grain_sample.channels = 1;
        grain_sample.logical_frames = UINT32_MAX;
        grain_sample.data.resize(32000);
        for (size_t i = 0; i < grain_sample.data.size(); ++i)
            grain_sample.data[i] = (q15)(((i * 811u) & 32767u) - 16384);

        // Exact endpoint mappings. These are the contract the UI labels rely on.
        if (GrainUnit::voice_limit(0) != 1 ||
            GrainUnit::voice_limit(32767) != GrainUnit::MAX_GRAINS) {
            printf("FAIL: GRAN voice limit endpoints\n"); return 1;
        }
        if (GrainUnit::grain_length_frames(0) != 960 ||
            GrainUnit::grain_length_frames(32767) != 16000) {
            printf("FAIL: GRAN grain length endpoints\n"); return 1;
        }
        if (GrainUnit::spawn_interval_frames(0) != 8000 ||
            GrainUnit::spawn_interval_frames(32767) != 320) {
            printf("FAIL: GRAN spawn interval endpoints\n"); return 1;
        }

        // A dense cloud must actually overlap more than the old two heads and
        // must never exceed the requested voice ceiling.
        GrainUnit grain;
        grain.sample_slot = 41;
        grain.params[GrainUnit::P_SIZE].base = 16384;
        grain.params[GrainUnit::P_DENS].base = 32767;
        grain.params[GrainUnit::P_SPRAY].base = 20000;
        grain.params[GrainUnit::P_PITCH_DEV].base = 24000;
        grain.params[GrainUnit::P_VOICES].base = 32767;
        for (int p = 0; p < grain.num_params; ++p) grain.params[p].snap();
        grain.trigger();
        q15 block[2048] = {};
        uint8_t peak_active = 0;
        for (int pass = 0; pass < 8; ++pass) {
            grain.process(block, 2048);
            if (grain.visual_active() > GrainUnit::MAX_GRAINS) {
                printf("FAIL: GRAN exceeded voice ceiling\n"); return 1;
            }
            if (grain.visual_active() > peak_active) peak_active = grain.visual_active();
            for (int g = 0; g < GrainUnit::MAX_GRAINS; ++g)
                if (grain.visual_positions()[g] > 95) {
                    printf("FAIL: GRAN visual position out of range\n"); return 1;
                }
        }
        if (peak_active < 3) {
            printf("FAIL: GRAN cloud never overlapped (%u)\n", (unsigned)peak_active);
            return 1;
        }
        if (grain.visual_interval() != GrainUnit::spawn_interval_frames(32767)) {
            printf("FAIL: GRAN interval telemetry\n"); return 1;
        }

        // Regression: PDEV must work in a patch that contains no STRN. The shared
        // semitone LUT used to be primed only by StranularUnit's constructor, so a
        // lone GRAN read zeros, clamped every grain to 1/64 speed, and the derived
        // length then inflated grains to tens of seconds until the unit was deleted.
        if (GrainUnit::pdev_increment(65536, 32767) == 0) {
            printf("FAIL: GRAN PDEV pitch LUT unavailable\n"); return 1;
        }
        {
            GrainUnit dev;
            dev.sample_slot = 41;
            dev.params[GrainUnit::P_SIZE].base = 32767;
            dev.params[GrainUnit::P_PITCH_DEV].base = 32767;
            dev.params[GrainUnit::P_DENS].base = 32767;
            dev.params[GrainUnit::P_VOICES].base = 32767;
            for (int p = 0; p < dev.num_params; ++p) dev.params[p].snap();
            dev.control_tick();
            q15 dev_block[2048] = {};
            for (int pass = 0; pass < 6; ++pass) dev.process(dev_block, 2048);
            // Grains must keep turning over. A wedged cloud reports the same eight
            // stuck voices forever and never refreshes its spawn count.
            const uint32_t spawned_before = dev.visual_spawned();
            for (int pass = 0; pass < 6; ++pass) dev.process(dev_block, 2048);
            if (dev.visual_spawned() == spawned_before) {
                printf("FAIL: GRAN PDEV wedged the cloud\n"); return 1;
            }
            if (dev.visual_longest() > GrainUnit::grain_length_frames(32767)) {
                printf("FAIL: GRAN PDEV inflated grain length to %u\n",
                       (unsigned)dev.visual_longest()); return 1;
            }
        }

        // VOICES=0 must be a hard mono cloud, not a silent or runaway one.
        GrainUnit mono;
        mono.sample_slot = 41;
        mono.params[GrainUnit::P_DENS].base = 32767;
        mono.params[GrainUnit::P_VOICES].base = 0;
        for (int p = 0; p < mono.num_params; ++p) mono.params[p].snap();
        bool mono_heard = false;
        for (int pass = 0; pass < 4; ++pass) {
            mono.process(block, 2048);
            if (mono.visual_active() > 1) {
                printf("FAIL: GRAN VOICES=0 spawned %u grains\n",
                       (unsigned)mono.visual_active()); return 1;
            }
            for (q15 v : block) if (v != 0) { mono_heard = true; break; }
        }
        if (!mono_heard) { printf("FAIL: GRAN VOICES=0 silent\n"); return 1; }

        // Clock lock is unit config, so it must survive clone and stay bounded.
        GrainUnit synced;
        synced.clock_sync = true;
        synced.sync_division = 3;
        synced.set_transport_bpm(300);
        if (synced.sync_interval_frames < 320) {
            printf("FAIL: GRAN sync interval floor\n"); return 1;
        }
        synced.sync_division = 0;
        synced.set_transport_bpm(120);
        if (synced.sync_interval_frames != (uint32_t)(32000 * 240 / (120 * 8))) {
            printf("FAIL: GRAN sync interval math (%u)\n",
                   (unsigned)synced.sync_interval_frames); return 1;
        }
        Unit* grain_clone = clone_unit(&synced);
        if (!grain_clone || grain_clone->kind != UnitKind::Grain) {
            printf("FAIL: GRAN clone kind\n"); delete grain_clone; return 1;
        }
        {
            const auto* c = static_cast<const GrainUnit*>(grain_clone);
            if (!c->clock_sync || c->sync_division != 0 ||
                c->visual_active() != 0 || c->visual_grains() != 0) {
                printf("FAIL: GRAN clone config/runtime\n");
                delete grain_clone; return 1;
            }
        }
        delete grain_clone;

        // An empty slot must be silent and must clear telemetry, not keep stale
        // grain markers on the focused page.
        GrainUnit orphan;
        orphan.sample_slot = 42;
        orphan.process(block, 256);
        if (orphan.visual_active() != 0 || orphan.visual_grains() != 0) {
            printf("FAIL: GRAN empty slot telemetry\n"); return 1;
        }

        grain_sample = {};
    }

    // BIT must keep its clock math bounded and truthful at every endpoint. The
    // old unsigned jitter wrap could turn a negative perturbation into a huge
    // hold, while signed drift accumulation eventually invoked UB.
    {
        BitUnit bit;
        q15 dry[8] = { -32768, -12345, -1, 0, 1, 12345, 32766, 32767 };
        q15 exact[8];
        std::memcpy(exact, dry, sizeof dry);
        bit.params[BitUnit::P_BITS].effective = 0;
        bit.params[BitUnit::P_RATE].effective = 32767;
        bit.params[BitUnit::P_MIX].effective = 0;
        bit.params[BitUnit::P_JITTER].effective = 32767;
        bit.params[BitUnit::P_DROPOUT].effective = 32767;
        bit.params[BitUnit::P_DRIFT].effective = -32767;
        bit.params[BitUnit::P_COMP].effective = 32767;
        bit.process(dry, 8);
        if (std::memcmp(dry, exact, sizeof dry) != 0) {
            printf("FAIL: BIT MIX=0 exact dry\n"); return 1;
        }

        BitUnit endpoint;
        endpoint.params[BitUnit::P_BITS].effective = 32767;
        endpoint.params[BitUnit::P_RATE].effective = 32767;
        endpoint.params[BitUnit::P_MIX].effective = 32767;
        endpoint.params[BitUnit::P_JITTER].effective = 0;
        endpoint.params[BitUnit::P_DROPOUT].effective = 0;
        endpoint.params[BitUnit::P_DRIFT].effective = 0;
        endpoint.params[BitUnit::P_COMP].effective = 0;
        q15 one = 123;
        endpoint.process(&one, 1);
        if (endpoint.visual_bits() != 15 || endpoint.visual_period() != 1 ||
            endpoint.visual_held() != 122) {
            printf("FAIL: BIT depth/rate endpoint bits=%u period=%u held=%d\n",
                   (unsigned)endpoint.visual_bits(), (unsigned)endpoint.visual_period(),
                   (int)endpoint.visual_held()); return 1;
        }

        BitUnit minimum;
        minimum.params[BitUnit::P_BITS].effective = 0;
        minimum.params[BitUnit::P_RATE].effective = 0;
        q15 min_block[32] = {};
        minimum.process(min_block, 32);
        if (minimum.visual_bits() != 3 || minimum.visual_period() != 32) {
            printf("FAIL: BIT minimum depth/rate endpoint\n"); return 1;
        }

        bit.params[BitUnit::P_BITS].effective = 32767;
        bit.params[BitUnit::P_RATE].effective = 32767;
        bit.params[BitUnit::P_MIX].effective = 32767;
        bit.params[BitUnit::P_DROPOUT].effective = 0;
        bit.params[BitUnit::P_COMP].effective = 0;
        bit.params[BitUnit::P_DRIFT].effective = 0;
        bit.params[BitUnit::P_JITTER].effective = 32767;
        q15 jitter_block[4096] = {};
        bit.process(jitter_block, 4096);
        if (bit.visual_period() < 1 || bit.visual_period() > 11) {
            printf("FAIL: BIT jitter period clamp\n"); return 1;
        }

        q15 drift_sample = 1000;
        bit.params[BitUnit::P_JITTER].effective = 0;
        bit.params[BitUnit::P_DRIFT].effective = 32767;
        for (int i = 0; i < 70000; ++i) bit.process(&drift_sample, 1);
        bit.params[BitUnit::P_DRIFT].effective = -32767;
        for (int i = 0; i < 70000; ++i) bit.process(&drift_sample, 1);
        if (bit.visual_period() < 1 || bit.visual_period() > 8) {
            printf("FAIL: BIT drift period bounds\n"); return 1;
        }

        bit.params[BitUnit::P_DROPOUT].effective = 32767;
        q15 drop_block[512];
        for (auto& sample : drop_block) sample = 12000;
        bit.process(drop_block, 512);
        if (!bit.visual_dropout()) {
            printf("FAIL: BIT dropout telemetry\n"); return 1;
        }

        Engine focused_bit;
        auto* installed = new BitUnit();
        installed->params[BitUnit::P_BITS].effective = 1234;
        installed->process(drop_block, 512);
        focused_bit.replace_unit(2, 4, installed);
        // Aggregate selected-Sigil patch map: multiple parameters on one module
        // become one structural knot with a route count and one live activity.
        Engine map;
        auto* mapped = new FilterUnit();
        mapped->params[FilterUnit::P_CUTOFF].src = ModSource::SigilB;
        mapped->params[FilterUnit::P_CUTOFF].mod_depth = 12000;
        mapped->params[FilterUnit::P_RES].src = ModSource::SigilB;
        mapped->params[FilterUnit::P_RES].mod_depth = -12000;
        mapped->params[FilterUnit::P_MORPH].src = ModSource::SigilA;
        mapped->params[FilterUnit::P_MORPH].mod_depth = 32767;
        map.replace_unit(2, 3, mapped);
        map.sigils[1].drawn = true;
        map.sigils[1].value = 16384;
        EngineUiSnapshot map_snapshot;
        map.capture_ui_snapshot(map_snapshot, -1, -1, 1);
        const int expected_activity = ((16384 * 12000) >> 15) * 255 / 32767;
        if (map_snapshot.sigil_patch.route_count[2][3] != 2 ||
            map_snapshot.sigil_patch.activity[2][3] != expected_activity ||
            map_snapshot.sigil_patch.route_count[0][0] != 0) {
            printf("FAIL: selected Sigil aggregate patch map\n"); return 1;
        }
        map.capture_ui_snapshot(map_snapshot, -1, -1, 0);
        if (map_snapshot.sigil_patch.route_count[2][3] != 1 ||
            map_snapshot.sigil_patch.activity[2][3] != 0) {
            printf("FAIL: selected Sigil patch map source switch\n"); return 1;
        }

        EngineUiSnapshot bit_snapshot;
        focused_bit.capture_ui_snapshot(bit_snapshot, 2, 4);
        if (!bit_snapshot.effect_focus_valid ||
            bit_snapshot.effect_focus_kind != (uint8_t)UnitKind::Bitcrush ||
            bit_snapshot.effect_focus_chain != 2 ||
            bit_snapshot.effect_focus_slot != 4 || bit_snapshot.focus_effective[0] != 1234 ||
            bit_snapshot.bit_depth != installed->visual_bits()) {
            printf("FAIL: BIT focused telemetry\n"); return 1;
        }
        focused_bit.capture_ui_snapshot(bit_snapshot, -1, -1);
        if (bit_snapshot.effect_focus_valid || bit_snapshot.effect_focus_chain != -1 ||
            bit_snapshot.effect_focus_slot != -1) {
            printf("FAIL: BIT focused telemetry clear\n"); return 1;
        }
    }

    // GRAN publishes its cloud only for the focused card. Switching or clearing
    // focus must not leave stale grain markers on another page, and the shared
    // focus_effective bank must belong to whichever module actually has focus.
    {
        auto& cloud_sample = SampleBank::instance().slot(43);
        cloud_sample.channels = 1;
        cloud_sample.logical_frames = UINT32_MAX;
        cloud_sample.data.resize(32000);
        for (size_t i = 0; i < cloud_sample.data.size(); ++i)
            cloud_sample.data[i] = (q15)(((i * 577u) & 32767u) - 16384);

        Engine cloud_ui;
        auto* cloud = new GrainUnit();
        cloud->sample_slot = 43;
        cloud->params[GrainUnit::P_DENS].effective = 32767;
        cloud->params[GrainUnit::P_VOICES].effective = 32767;
        cloud->params[GrainUnit::P_POS].effective = 8192;
        q15 cloud_block[2048] = {};
        for (int pass = 0; pass < 4; ++pass) cloud->process(cloud_block, 2048);
        cloud_ui.replace_unit(1, 3, cloud);

        EngineUiSnapshot cloud_snapshot;
        cloud_ui.capture_ui_snapshot(cloud_snapshot, 1, 3);
        if (!cloud_snapshot.gran_focus_valid || cloud_snapshot.gran_focus_chain != 1 ||
            cloud_snapshot.gran_focus_slot != 3 ||
            cloud_snapshot.gran_active != cloud->visual_active() ||
            cloud_snapshot.gran_grains != cloud->visual_grains() ||
            cloud_snapshot.gran_interval != cloud->visual_interval() ||
            cloud_snapshot.gran_limit != GrainUnit::MAX_GRAINS ||
            cloud_snapshot.focus_effective[GrainUnit::P_POS] != 8192) {
            printf("FAIL: GRAN focused telemetry\n"); return 1;
        }
        if (cloud_snapshot.gran_active == 0) {
            printf("FAIL: GRAN focused telemetry has no live grains\n"); return 1;
        }
        for (int g = 0; g < EngineUiSnapshot::GRAN_GRAIN_POINTS; ++g)
            if (cloud_snapshot.gran_positions[g] > 95) {
                printf("FAIL: GRAN snapshot position out of range\n"); return 1;
            }

        // Focusing a different, empty slot must invalidate the record.
        cloud_ui.capture_ui_snapshot(cloud_snapshot, 0, 0);
        if (cloud_snapshot.gran_focus_valid || cloud_snapshot.gran_focus_chain != -1) {
            printf("FAIL: GRAN focus switch left stale data\n"); return 1;
        }
        cloud_ui.capture_ui_snapshot(cloud_snapshot, -1, -1);
        if (cloud_snapshot.gran_focus_valid) {
            printf("FAIL: GRAN focused telemetry clear\n"); return 1;
        }
        cloud_sample = {};
    }

    // STRN publishes one focused chronological memory field. Waveform samples and
    // grain markers must share the same scan/freeze coordinates, and freeze must
    // keep that captured field immutable while new input continues outside it.
    {
        Engine strn_ui;
        auto* strn = new StranularUnit();
        strn->params[StranularUnit::P_SIZE].effective = 0;
        strn->params[StranularUnit::P_DENS].effective = 32767;
        strn->params[StranularUnit::P_POS].effective = 16384;
        strn->params[StranularUnit::P_MIX].effective = 32767;
        strn_ui.replace_unit(1, 2, strn);
        q15 input[512];
        for (int block = 0; block < 8; ++block) {
            for (int i = 0; i < 512; ++i)
                input[i] = (q15)(((i * 971 + block * 313) & 32767) - 16384);
            strn->process(input, 512);
        }
        if (!strn->visual_grains() || strn->visual_fill() == 0) {
            printf("FAIL: STRN live telemetry activity\n"); return 1;
        }
        for (int grain = 0; grain < StranularUnit::MAX_GRAINS; ++grain) {
            if ((strn->visual_grains() & (1u << grain)) &&
                strn->visual_positions()[grain] >= StranularUnit::VIZ_POINTS) {
                printf("FAIL: STRN grain/history coordinate domain\n"); return 1;
            }
        }
        if (StranularUnit::cluster_count(0) != 1 ||
            StranularUnit::cluster_count(10922) != 2 ||
            StranularUnit::cluster_count(21844) != 3 ||
            StranularUnit::cluster_count(32767) != 4) {
            printf("FAIL: STRN cluster 1..4 mapping\n"); return 1;
        }
        const int32_t base_inc = StranularUnit::pitch_increment_q16(16384);
        const int32_t bend_ratio = StranularUnit::pitch_increment_q16(-16384);
        const int32_t expected_target = (int32_t)(((int64_t)base_inc * bend_ratio) / 65536);
        if (base_inc < 65536 || expected_target >= base_inc || expected_target <= 0) {
            printf("FAIL: STRN pitch envelope relative ratio\n"); return 1;
        }
        StranularUnit bend;
        bend.params[StranularUnit::P_SIZE].effective = 0;
        bend.params[StranularUnit::P_DENS].effective = 32767;
        bend.params[StranularUnit::P_POS].effective = 16384;
        bend.params[StranularUnit::P_PITCH].effective = 16384;
        bend.params[StranularUnit::P_PITCH_ENV].effective = -16384;
        for (int block = 0; block < 8; ++block) bend.process(input, 512);
        int32_t active_increment = 0;
        for (int grain = 0; grain < StranularUnit::MAX_GRAINS; ++grain)
            if (bend.visual_grains() & (1u << grain)) {
                active_increment = bend.visual_increments()[grain]; break;
            }
        if (active_increment <= 0 || active_increment >= base_inc) {
            printf("FAIL: STRN pitch envelope applied relative to base\n"); return 1;
        }

        EngineUiSnapshot strn_snapshot;
        strn_ui.capture_ui_snapshot(strn_snapshot, 1, 2);
        if (!strn_snapshot.strn_focus_valid || strn_snapshot.strn_focus_chain != 1 ||
            strn_snapshot.strn_focus_slot != 2 || !strn_snapshot.strn_grains ||
            strn_snapshot.strn_fill != strn->visual_fill() ||
            strn_snapshot.strn_mix != strn->visual_mix()) {
            printf("FAIL: STRN focused snapshot\n"); return 1;
        }

        strn->params[StranularUnit::P_FREEZE].effective = 32767;
        strn->params[StranularUnit::P_MIX].effective = 0;
        for (int i = 0; i < 512; ++i) input[i] = (q15)(i * 41 - 10000);
        strn->process(input, 512);
        if (strn->visual_mix() != 32767) {
            printf("FAIL: STRN freeze full-wet mix\n"); return 1;
        }
        q15 frozen_wave[StranularUnit::VIZ_POINTS];
        std::memcpy(frozen_wave, strn->visual_wave(), sizeof frozen_wave);
        for (int block = 0; block < 4; ++block) {
            for (int i = 0; i < 512; ++i) input[i] = (q15)(15000 - i * 37 - block * 99);
            strn->process(input, 512);
        }
        if (!strn->visual_frozen() ||
            std::memcmp(frozen_wave, strn->visual_wave(), sizeof frozen_wave) != 0) {
            printf("FAIL: STRN immutable frozen field\n"); return 1;
        }
        strn_ui.capture_ui_snapshot(strn_snapshot, -1, -1);
        if (strn_snapshot.strn_focus_valid || strn_snapshot.strn_focus_chain != -1 ||
            strn_snapshot.strn_focus_slot != -1) {
            printf("FAIL: STRN focused snapshot clear\n"); return 1;
        }
        for (q15 sample : strn_snapshot.strn_wave) if (sample != 0) {
            printf("FAIL: STRN focused snapshot stale wave\n"); return 1;
        }
    }

    // REVERB's appended DAMP/FREEZE controls must be bounded, persistence-safe,
    // and musically truthful: damping reduces loop HF energy, while full freeze
    // blocks new excitation and holds existing memory without muting dry output.
    {
        constexpr int N = 4096;
        q15 bright[N] = {}, dark[N] = {}, frozen[N] = {}, dry_probe[N] = {};
        bright[0] = dark[0] = frozen[0] = 24000;
        ReverbUnit open, damped, freeze;
        open.params[ReverbUnit::P_MIX].effective = 32767;
        open.params[ReverbUnit::P_DAMP].effective = 0;
        damped.params[ReverbUnit::P_MIX].effective = 32767;
        damped.params[ReverbUnit::P_DAMP].effective = 32767;
        for (int pass = 0; pass < 8; ++pass) {
            open.process(bright, N);
            damped.process(dark, N);
            // After the impulse pass the next blocks contain only loop return.
            for (int i = 0; i < N; ++i) bright[i] = dark[i] = 0;
        }
        if (damped.visual_damping() != 32767) {
            printf("FAIL: REVERB damping telemetry\n"); return 1;
        }

        // Seed a tail, then freeze it. Once the 64-frame transition settles,
        // unrelated input must remain audible only through the dry side of MIX.
        freeze.params[ReverbUnit::P_MIX].effective = 32767;
        freeze.process(frozen, N);
        freeze.params[ReverbUnit::P_FREEZE].effective = 32767;
        for (int i = 0; i < N; ++i) frozen[i] = 0;
        freeze.process(frozen, N);
        if (freeze.visual_freeze() != 32767) {
            printf("FAIL: REVERB freeze did not settle\n"); return 1;
        }

        // Full freeze is a literal memory seal, not merely a coefficient rounded
        // near unity. Fill all three lines with positive material, then prove a
        // long hold does not bleed away through Q15's 32767/32768 rounding gap.
        ReverbUnit long_freeze;
        long_freeze.params[ReverbUnit::P_MIX].effective = 32767;
        long_freeze.params[ReverbUnit::P_DECAY].effective = 32767;
        long_freeze.params[ReverbUnit::P_DAMP].effective = 0;
        q15 held[N];
        for (int pass = 0; pass < 32; ++pass) {
            for (q15& sample : held) sample = 9000;
            long_freeze.process(held, N);
        }
        long_freeze.params[ReverbUnit::P_FREEZE].effective = 32767;
        std::memset(held, 0, sizeof held);
        long_freeze.process(held, N);
        int64_t sealed_start = 0;
        std::memset(held, 0, sizeof held);
        long_freeze.process(held, N);
        for (q15 sample : held) sealed_start += sample < 0 ? -(int32_t)sample : sample;
        int64_t sealed_end = 0;
        for (int pass = 0; pass < 256; ++pass) {
            std::memset(held, 0, sizeof held);
            long_freeze.process(held, N);
            if (pass == 255)
                for (q15 sample : held) sealed_end += sample < 0 ? -(int32_t)sample : sample;
        }
        if (sealed_start == 0 || sealed_end * 100 < sealed_start * 99) {
            printf("FAIL: REVERB full freeze leaks memory\n"); return 1;
        }

        Engine reverb_ui;
        auto* focused_reverb = new ReverbUnit();
        for (int param = 0; param < focused_reverb->num_params; ++param)
            focused_reverb->params[param].effective = (q15)(3000 + param * 4000);
        focused_reverb->params[ReverbUnit::P_FREEZE].effective = 32767;
        q15 focus_probe[128] = {}; focus_probe[0] = 18000;
        focused_reverb->process(focus_probe, 128);
        reverb_ui.replace_unit(1, 4, focused_reverb);
        EngineUiSnapshot reverb_snapshot;
        reverb_ui.capture_ui_snapshot(reverb_snapshot, 1, 4);
        if (!reverb_snapshot.reverb_focus_valid ||
            reverb_snapshot.reverb_focus_chain != 1 ||
            reverb_snapshot.reverb_focus_slot != 4 ||
            reverb_snapshot.focus_effective[ReverbUnit::P_SIZE] != 3000 ||
            reverb_snapshot.focus_effective[ReverbUnit::P_DAMP] != 15000 ||
            reverb_snapshot.focus_effective[ReverbUnit::P_FREEZE] != 32767 ||
            reverb_snapshot.reverb_freeze != 32767) {
            printf("FAIL: REVERB focused snapshot\n"); return 1;
        }
        focused_reverb->bypass = true;
        reverb_ui.capture_ui_snapshot(reverb_snapshot, 1, 4);
        if (reverb_snapshot.reverb_energy != 0) {
            printf("FAIL: REVERB bypass energy snapshot\n"); return 1;
        }
        reverb_ui.capture_ui_snapshot(reverb_snapshot);
        if (reverb_snapshot.reverb_focus_valid || reverb_snapshot.reverb_focus_chain != -1 ||
            reverb_snapshot.reverb_focus_slot != -1 || reverb_snapshot.reverb_freeze != 0 ||
            reverb_snapshot.focus_effective[ReverbUnit::P_FREEZE] != 0) {
            printf("FAIL: REVERB focused snapshot clear\n"); return 1;
        }
        // MIX=0 is exact dry even while the chamber remains frozen internally.
        freeze.params[ReverbUnit::P_MIX].effective = 0;
        for (int i = 0; i < N; ++i) dry_probe[i] = (i & 1) ? 7000 : -7000;
        freeze.process(dry_probe, N);
        for (int i = 0; i < N; ++i) {
            const q15 expected = (i & 1) ? 7000 : -7000;
            if (dry_probe[i] != expected) {
                printf("FAIL: REVERB freeze changed dry path\n"); return 1;
            }
        }
        freeze.reset();
        if (freeze.visual_freeze() != 0 || freeze.visual_energy() != 0) {
            printf("FAIL: REVERB reset telemetry\n"); return 1;
        }
    }

    // CHORUS is one mono delayed tap driven by a triangle LFO. MIX=0 must remain
    // exact dry, telemetry must follow real phase/delay, and focused publication
    // must reuse the bounded BIT diagnostics mailbox without stale ownership.
    {
        ChorusUnit chorus;
        for (int param = 0; param < chorus.num_params; ++param)
            chorus.params[param].effective = chorus.params[param].base;
        chorus.params[ChorusUnit::P_RATE].effective = 32767;
        chorus.params[ChorusUnit::P_DEPTH].effective = 32767;
        chorus.params[ChorusUnit::P_MIX].effective = 0;
        q15 dry[256], expected[256];
        for (int i = 0; i < 256; ++i) dry[i] = (q15)(((i * 971) & 32767) - 16384);
        std::memcpy(expected, dry, sizeof dry);
        chorus.process(dry, 256);
        if (std::memcmp(dry, expected, sizeof dry) != 0 ||
            chorus.visual_phase() == 0 || chorus.visual_delay() < 96 ||
            chorus.visual_delay() > 412) {
            printf("FAIL: CHORUS dry/telemetry contract\n"); return 1;
        }
        chorus.reset();
        if (chorus.visual_phase() != 0 || chorus.visual_delay() != 96) {
            printf("FAIL: CHORUS reset telemetry\n"); return 1;
        }

        Engine chorus_ui;
        auto* focused_chorus = new ChorusUnit();
        focused_chorus->params[ChorusUnit::P_RATE].effective = 12345;
        focused_chorus->params[ChorusUnit::P_DEPTH].effective = 23456;
        focused_chorus->params[ChorusUnit::P_MIX].effective = 21000;
        q15 probe[512]; for (q15& sample : probe) sample = 9000;
        focused_chorus->process(probe, 512);
        chorus_ui.replace_unit(2, 3, focused_chorus);
        EngineUiSnapshot snapshot;
        chorus_ui.capture_ui_snapshot(snapshot, 2, 3);
        const uint16_t phase = (uint16_t)(((uint16_t)snapshot.chorus_phase_hi << 8) |
                                           snapshot.chorus_phase_lo);
        if (!snapshot.effect_focus_valid ||
            snapshot.effect_focus_kind != (uint8_t)UnitKind::Chorus ||
            snapshot.effect_focus_chain != 2 ||
            snapshot.effect_focus_slot != 3 || phase != focused_chorus->visual_phase() ||
            snapshot.chorus_delay != focused_chorus->visual_delay() ||
            snapshot.focus_effective[ChorusUnit::P_RATE] != 12345 ||
            snapshot.focus_effective[ChorusUnit::P_DEPTH] != 23456 ||
            snapshot.focus_effective[ChorusUnit::P_MIX] != 21000) {
            printf("FAIL: CHORUS focused telemetry\n"); return 1;
        }
        chorus_ui.capture_ui_snapshot(snapshot);
        if (snapshot.effect_focus_chain != -1 || snapshot.effect_focus_slot != -1 ||
            snapshot.chorus_delay != 0 || snapshot.focus_effective[0] != 0) {
            printf("FAIL: CHORUS focused telemetry clear\n"); return 1;
        }
    }

    // Redesigned TILT: BODY/AIR are reciprocal shelves around a log 125 Hz..8 kHz
    // pivot. RANGE=0 keeps the corrected legacy transfer for old patches, while
    // nonzero RANGE is symmetric, bounded, and neutral TILT is exact identity.
    {
        TiltUnit neutral;
        for (int param = 0; param < neutral.num_params; ++param)
            neutral.params[param].effective = neutral.params[param].base;
        if (neutral.num_params != 5 || !neutral.params[TiltUnit::P_TILT].bipolar ||
            neutral.params[TiltUnit::P_RANGE].base != 8192) {
            printf("FAIL: TILT parameter contract\n"); return 1;
        }
        q15 identity[512], expected[512];
        for (int i = 0; i < 512; ++i) identity[i] = (q15)(((i * 977) & 65535) - 32768);
        std::memcpy(expected, identity, sizeof identity);
        neutral.process(identity, 512);
        if (std::memcmp(identity, expected, sizeof identity) != 0) {
            printf("FAIL: TILT neutral identity\n"); return 1;
        }

        TiltUnit dry;
        for (int param = 0; param < dry.num_params; ++param)
            dry.params[param].effective = dry.params[param].base;
        dry.params[TiltUnit::P_TILT].effective = -32767;
        dry.params[TiltUnit::P_RANGE].effective = 32767;
        dry.params[TiltUnit::P_DRIVE].effective = 32767;
        dry.params[TiltUnit::P_MIX].effective = 0;
        q15 bypass[256], bypass_expected[256];
        for (int i = 0; i < 256; ++i) bypass[i] = (i & 1) ? 32767 : -32768;
        std::memcpy(bypass_expected, bypass, sizeof bypass);
        dry.process(bypass, 256);
        if (std::memcmp(bypass, bypass_expected, sizeof bypass) != 0) {
            printf("FAIL: TILT MIX zero not dry\n"); return 1;
        }

        // Positive TILT must favor alternating/high material; negative must favor
        // body/DC. Compare settled absolute output energies at the 1 kHz pivot.
        TiltUnit air, body;
        for (int p = 0; p < air.num_params; ++p) {
            air.params[p].effective = air.params[p].base;
            body.params[p].effective = body.params[p].base;
        }
        air.params[TiltUnit::P_TILT].effective = 24000;
        body.params[TiltUnit::P_TILT].effective = -24000;
        air.params[TiltUnit::P_DRIVE].effective = 0;
        body.params[TiltUnit::P_DRIVE].effective = 0;
        q15 air_hi[2048], body_hi[2048];
        for (int i = 0; i < 2048; ++i) air_hi[i] = body_hi[i] = (i & 1) ? 4000 : -4000;
        air.process(air_hi, 2048); body.process(body_hi, 2048);
        int64_t air_energy = 0, body_energy = 0;
        for (int i = 1024; i < 2048; ++i) {
            air_energy += air_hi[i] < 0 ? -(int32_t)air_hi[i] : air_hi[i];
            body_energy += body_hi[i] < 0 ? -(int32_t)body_hi[i] : body_hi[i];
        }
        if (air_energy <= body_energy) {
            printf("FAIL: TILT AIR shelf direction\n"); return 1;
        }
        air.reset(); body.reset();
        q15 air_lo[2048], body_lo[2048];
        for (int i = 0; i < 2048; ++i) air_lo[i] = body_lo[i] = 4000;
        air.process(air_lo, 2048); body.process(body_lo, 2048);
        air_energy = body_energy = 0;
        for (int i = 1024; i < 2048; ++i) {
            air_energy += air_lo[i] < 0 ? -(int32_t)air_lo[i] : air_lo[i];
            body_energy += body_lo[i] < 0 ? -(int32_t)body_lo[i] : body_lo[i];
        }
        if (body_energy <= air_energy) {
            printf("FAIL: TILT BODY shelf direction\n"); return 1;
        }

        Engine tilt_ui;
        auto* focused_tilt = new TiltUnit();
        for (int p = 0; p < focused_tilt->num_params; ++p)
            focused_tilt->params[p].effective = (q15)(-12000 + p * 7000);
        focused_tilt->params[TiltUnit::P_TILT].effective = -12000;
        tilt_ui.replace_unit(3, 2, focused_tilt);
        EngineUiSnapshot tilt_snapshot;
        tilt_ui.capture_ui_snapshot(tilt_snapshot, 3, 2);
        if (!tilt_snapshot.effect_focus_valid ||
            tilt_snapshot.effect_focus_kind != (uint8_t)UnitKind::Tilt ||
            tilt_snapshot.effect_focus_chain != 3 ||
            tilt_snapshot.effect_focus_slot != 2 ||
            tilt_snapshot.focus_effective[TiltUnit::P_TILT] != -12000 ||
            tilt_snapshot.focus_effective[TiltUnit::P_RANGE] != 16000) {
            printf("FAIL: TILT focused effective snapshot\n"); return 1;
        }
        tilt_ui.capture_ui_snapshot(tilt_snapshot);
        if (tilt_snapshot.effect_focus_chain != -1 || tilt_snapshot.effect_focus_slot != -1 ||
            tilt_snapshot.focus_effective[0] != 0) {
            printf("FAIL: TILT focused effective snapshot clear\n"); return 1;
        }

        // Legacy mode and every redesigned corner must survive adversarial full
        // scale without signed overflow; sanitizers enforce the stronger contract.
        for (q15 range : {(q15)0, (q15)1, (q15)32767})
            for (q15 tilt : {(q15)-32767, (q15)0, (q15)32767}) {
                TiltUnit extreme;
                for (int p = 0; p < extreme.num_params; ++p)
                    extreme.params[p].effective = extreme.params[p].base;
                extreme.params[TiltUnit::P_RANGE].effective = range;
                extreme.params[TiltUnit::P_TILT].effective = tilt;
                extreme.params[TiltUnit::P_DRIVE].effective = 32767;
                q15 block[1024];
                for (int i = 0; i < 1024; ++i) block[i] = (i & 1) ? 32767 : -32768;
                extreme.process(block, 1024);
            }
    }

    if (!test_ghost_tuning()) return 1;

    // GHST memory wells: all six old indices remain stable, dry mix is exact,
    // freeze slews instead of clicking, and focused telemetry never leaks.
    {
        GhostUnit contract;
        if (contract.num_params != 8 ||
            std::strcmp(contract.params[GhostUnit::P_TONE].name, "TONE") != 0 ||
            std::strcmp(contract.params[GhostUnit::P_FREEZE].name, "FRZ") != 0) {
            printf("FAIL: GHST parameter contract\n"); return 1;
        }
        GhostUnit dry;
        for (int p = 0; p < dry.num_params; ++p) dry.params[p].effective = dry.params[p].base;
        dry.params[GhostUnit::P_MIX].effective = 0;
        dry.params[GhostUnit::P_DECAY].effective = 32767;
        dry.params[GhostUnit::P_DRIFT].effective = 32767;
        q15 identity[1024], expected[1024];
        for (int i = 0; i < 1024; ++i) identity[i] = (q15)(((i * 1879) & 65535) - 32768);
        std::memcpy(expected, identity, sizeof identity); dry.process(identity, 1024);
        if (std::memcmp(identity, expected, sizeof identity) != 0) {
            printf("FAIL: GHST dry identity\n"); return 1;
        }
        if (GhostUnit::root_hz(0) != 54 || GhostUnit::root_hz(32767) != 215 ||
            GhostUnit::root_hz(16384) < 84 || GhostUnit::root_hz(16384) > 88) {
            printf("FAIL: GHST root mapping\n"); return 1;
        }

        // Full FREEZE becomes a literal sealed integer loop: after capture one
        // complete 4096-sample memory cycle must repeat bit for bit.
        GhostUnit hold;
        for (int p = 0; p < hold.num_params; ++p) hold.params[p].effective = hold.params[p].base;
        hold.params[GhostUnit::P_TONE].effective = 32767;
        hold.params[GhostUnit::P_DECAY].effective = 32767;
        hold.params[GhostUnit::P_DRIFT].effective = 0;
        hold.params[GhostUnit::P_MIX].effective = 32767;
        q15 seed[4096] = {}; seed[0] = 28000; seed[347] = -18000;
        hold.process(seed, 4096);
        hold.params[GhostUnit::P_FREEZE].effective = 32767;
        q15 settle[512] = {}; hold.process(settle, 512);
        // The four prime-ish cycles do not align to a 4096-sample output period,
        // but their exact-copy loops must retain long-window energy indefinitely.
        int64_t held_energy[2] = {};
        q15 frozen[1024] = {};
        for (int block = 0; block < 128; ++block) {
            for (q15& sample : frozen) sample = (block & 1) ? 30000 : -30000;
            hold.process(frozen, 1024);
            for (q15 sample : frozen) held_energy[block >= 64] +=
                sample < 0 ? -(int32_t)sample : sample;
        }
        const int64_t delta = held_energy[0] > held_energy[1]
            ? held_energy[0] - held_energy[1] : held_energy[1] - held_energy[0];
        if (held_energy[0] == 0 || delta * 20 > held_energy[0] ||
            hold.visual_freeze() != 32767) {
            printf("FAIL: GHST exact freeze hold\n"); return 1;
        }

        for (q15 tone : {(q15)0, (q15)32767})
            for (q15 smear : {(q15)0, (q15)32767})
                for (q15 drift : {(q15)0, (q15)32767}) {
                    GhostUnit extreme;
                    for (int p = 0; p < extreme.num_params; ++p)
                        extreme.params[p].effective = extreme.params[p].base;
                    extreme.params[GhostUnit::P_TONE].effective = tone;
                    extreme.params[GhostUnit::P_DECAY].effective = 32767;
                    extreme.params[GhostUnit::P_SMEAR].effective = smear;
                    extreme.params[GhostUnit::P_DRIFT].effective = drift;
                    extreme.params[GhostUnit::P_FREEZE].effective = 32767;
                    extreme.params[GhostUnit::P_MIX].effective = 32767;
                    q15 block[2048];
                    for (int i = 0; i < 2048; ++i) block[i] = (i & 1) ? 32767 : -32768;
                    extreme.process(block, 2048);
                }
        Engine ghost_ui;
        auto* focused_ghost = new GhostUnit();
        for (int p = 0; p < focused_ghost->num_params; ++p)
            focused_ghost->params[p].effective = (q15)(2000 + p * 4000);
        q15 excite[2048] = {}; excite[0] = 24000;
        focused_ghost->process(excite, 2048);
        ghost_ui.replace_unit(1, 5, focused_ghost);
        EngineUiSnapshot snapshot;
        ghost_ui.capture_ui_snapshot(snapshot, 1, 5);
        const uint16_t energy = (uint16_t)(((uint16_t)snapshot.ghost_energy_hi << 8) |
                                            snapshot.ghost_energy_lo);
        if (snapshot.effect_focus_chain != 1 || snapshot.effect_focus_slot != 5 ||
            !snapshot.effect_focus_valid ||
            snapshot.effect_focus_kind != (uint8_t)UnitKind::Ghost ||
            snapshot.focus_effective[GhostUnit::P_MIX] != 22000 || energy == 0) {
            printf("FAIL: GHST focused snapshot\n"); return 1;
        }
        ghost_ui.capture_ui_snapshot(snapshot);
        if (snapshot.effect_focus_chain != -1 || snapshot.effect_payload_lo != 0 ||
            snapshot.focus_effective[0] != 0) {
            printf("FAIL: GHST focused snapshot clear\n"); return 1;
        }
    }

    // RING v2: a monotonic log carrier, true DEPTH identity, signed bounded
    // feedback, DC-safe bridge, envelope follow, and focused race-free telemetry.
    {
        RingUnit contract;
        if (contract.num_params != 6 ||
            !contract.params[RingUnit::P_FEEDBACK].bipolar ||
            std::strcmp(contract.params[RingUnit::P_DEPTH].name, "DEPTH") != 0 ||
            std::strcmp(contract.params[RingUnit::P_FOLLOW].name, "FOLLOW") != 0) {
            printf("FAIL: RING parameter contract\n"); return 1;
        }
        RingUnit identity;
        for (int p = 0; p < identity.num_params; ++p)
            identity.params[p].effective = identity.params[p].base;
        identity.params[RingUnit::P_DEPTH].effective = 0;
        identity.params[RingUnit::P_FEEDBACK].effective = 32767;
        identity.params[RingUnit::P_DIODE].effective = 32767;
        identity.params[RingUnit::P_MIX].effective = 32767;
        q15 dry[512], expected[512];
        for (int i = 0; i < 512; ++i) dry[i] = (q15)(((i * 1709) & 65535) - 32768);
        std::memcpy(expected, dry, sizeof dry); identity.process(dry, 512);
        if (std::memcmp(dry, expected, sizeof dry) != 0) {
            printf("FAIL: RING depth-zero identity\n"); return 1;
        }
        for (q15 feedback : {(q15)-32767, (q15)0, (q15)32767})
            for (q15 diode : {(q15)0, (q15)32767})
                for (q15 follow : {(q15)0, (q15)32767}) {
                    RingUnit extreme;
                    for (int p = 0; p < extreme.num_params; ++p)
                        extreme.params[p].effective = extreme.params[p].base;
                    extreme.params[RingUnit::P_FREQ].effective = 32767;
                    extreme.params[RingUnit::P_DEPTH].effective = 32767;
                    extreme.params[RingUnit::P_FEEDBACK].effective = feedback;
                    extreme.params[RingUnit::P_DIODE].effective = diode;
                    extreme.params[RingUnit::P_MIX].effective = 32767;
                    extreme.params[RingUnit::P_FOLLOW].effective = follow;
                    q15 block[1024];
                    for (int i = 0; i < 1024; ++i) block[i] = (i & 1) ? 32767 : -32768;
                    extreme.process(block, 1024);
                }
        Engine ring_ui;
        auto* focused_ring = new RingUnit();
        for (int p = 0; p < focused_ring->num_params; ++p)
            focused_ring->params[p].effective = (q15)(3000 + p * 3000);
        q15 probe[128]; for (auto& sample : probe) sample = 9000;
        focused_ring->process(probe, 128);
        ring_ui.replace_unit(2, 4, focused_ring);
        EngineUiSnapshot snapshot;
        ring_ui.capture_ui_snapshot(snapshot, 2, 4);
        if (snapshot.effect_focus_chain != 2 || snapshot.effect_focus_slot != 4 ||
            !snapshot.effect_focus_valid ||
            snapshot.effect_focus_kind != (uint8_t)UnitKind::Ring ||
            snapshot.focus_effective[RingUnit::P_FOLLOW] != 18000 ||
            snapshot.ring_envelope == 0) {
            printf("FAIL: RING focused snapshot\n"); return 1;
        }
        ring_ui.capture_ui_snapshot(snapshot);
        if (snapshot.effect_focus_chain != -1 || snapshot.focus_effective[0] != 0) {
            printf("FAIL: RING focused snapshot clear\n"); return 1;
        }
    }

    // New effects smoke: all must process bounded non-silent data without allocation in process.
    for (UnitKind k : { UnitKind::Bitcrush, UnitKind::Chorus, UnitKind::Reverb, UnitKind::Stranular, UnitKind::Ghost, UnitKind::Ring, UnitKind::Tilt, UnitKind::Strain }) {
        Unit* fx = make_unit(k);
        trackr::fx::q15 b[512];
        for (int i = 0; i < 512; ++i) b[i] = (trackr::fx::q15)(((i * 977) & 32767) - 16384);
        fx->process(b, 512);
        // STRN needs history before grains become audible; exercise several blocks
        // and freeze transitions on the same Old-3DS-sized eight-voice engine.
        if (k == UnitKind::Stranular) {
            for (int block = 0; block < 32; ++block) {
                for (int i = 0; i < 512; ++i) b[i] = (trackr::fx::q15)(((i * 977 + block * 311) & 32767) - 16384);
                fx->process(b, 512);
            }
            fx->params[StranularUnit::P_FREEZE].effective = trackr::fx::Q15_ONE;
            fx->process(b, 512);
        }
        int pk = 0;
        for (auto v : b) if (abs((int)v) > pk) pk = abs((int)v);
        if (pk == 0 || pk > 32767) { printf("FAIL: new effect %d\n", (int)k); return 1; }
        delete fx;
    }

    // STRAIN: exact dry at MIX=0; bounded block-delayed feedback; BIAS must
    // create a genuinely asymmetric transfer without leaving DC on silence.
    {
        StrainUnit strain;
        for (int p = 0; p < strain.num_params; ++p)
            strain.params[p].effective = strain.params[p].base;
        strain.params[StrainUnit::P_MIX].effective = 0;
        q15 dry[64], expected[64];
        for (int i = 0; i < 64; ++i) dry[i] = (q15)((i - 32) * 700);
        std::memcpy(expected, dry, sizeof dry);
        strain.process(dry, 64);
        if (std::memcmp(dry, expected, sizeof dry) != 0) {
            printf("FAIL: STRAIN exact dry\n"); return 1;
        }

        strain.reset();
        strain.params[StrainUnit::P_DRIVE].effective = 30000;
        strain.params[StrainUnit::P_BIAS].effective = 18000;
        strain.params[StrainUnit::P_BODY].effective = 22000;
        strain.params[StrainUnit::P_TEETH].effective = 25000;
        strain.params[StrainUnit::P_FEEDBACK].effective = 32767;
        strain.params[StrainUnit::P_MIX].effective = 32767;
        q15 block[CONTROL_INTERVAL];
        int peak = 0;
        for (int pass = 0; pass < 1024; ++pass) {
            for (int i = 0; i < CONTROL_INTERVAL; ++i)
                block[i] = pass == 0 && i == 0 ? 24000 : 0;
            strain.control_tick();
            strain.process(block, CONTROL_INTERVAL);
            for (q15 sample : block) {
                const int magnitude = sample == -32768 ? 32767 : abs((int)sample);
                if (magnitude > peak) peak = magnitude;
            }
        }
        if (peak == 0 || peak > 32767 || strain.visual_energy() > 32767) {
            printf("FAIL: STRAIN bounded feedback\n"); return 1;
        }
        strain.params[StrainUnit::P_FEEDBACK].effective = 9000;
        q15 silence[CONTROL_INTERVAL] = {};
        for (int pass = 0; pass < 512; ++pass) {
            std::memset(silence, 0, sizeof silence);
            strain.process(silence, CONTROL_INTERVAL);
        }
        int tail = 0;
        for (q15 sample : silence) tail += abs((int)sample);
        if (tail > 1024) { printf("FAIL: STRAIN feedback decay/DC\n"); return 1; }

        // Every control corner must stay bounded and free of persistent DC. This
        // catches fixed-point overflow that ordinary musical defaults never reach.
        for (q15 drive : {(q15)0, (q15)32767})
        for (q15 bias : {(q15)-32767, (q15)0, (q15)32767})
        for (q15 body : {(q15)0, (q15)32767})
        for (q15 teeth : {(q15)0, (q15)32767})
        for (q15 feedback : {(q15)0, (q15)32767}) {
            strain.reset();
            strain.params[StrainUnit::P_DRIVE].effective = drive;
            strain.params[StrainUnit::P_BIAS].effective = bias;
            strain.params[StrainUnit::P_BODY].effective = body;
            strain.params[StrainUnit::P_TEETH].effective = teeth;
            strain.params[StrainUnit::P_FEEDBACK].effective = feedback;
            strain.params[StrainUnit::P_MIX].effective = 32767;
            int64_t dc_sum = 0;
            for (int pass = 0; pass < 256; ++pass) {
                for (int i = 0; i < CONTROL_INTERVAL; ++i) {
                    const int phase = (pass * CONTROL_INTERVAL + i) & 63;
                    block[i] = (q15)((phase < 32 ? phase : 63 - phase) * 1300 - 20000);
                }
                strain.control_tick();
                strain.process(block, CONTROL_INTERVAL);
                if (pass >= 192) for (q15 sample : block) dc_sum += sample;
            }
            const int64_t mean = dc_sum / (64 * CONTROL_INTERVAL);
            if (mean < -1600 || mean > 1600) {
                printf("FAIL: STRAIN corner DC %d %d %d %d %d mean=%lld\n",
                       drive, bias, body, teeth, feedback, (long long)mean); return 1;
            }
        }

        // Parameter identity: BODY and TEETH must materially change the transfer,
        // while BIAS must change positive/negative symmetry.
        auto render_signature = [](q15 body, q15 teeth, q15 bias, int64_t& sum, int64_t& weighted) {
            StrainUnit unit;
            unit.params[StrainUnit::P_DRIVE].effective = 24000;
            unit.params[StrainUnit::P_BODY].effective = body;
            unit.params[StrainUnit::P_TEETH].effective = teeth;
            unit.params[StrainUnit::P_BIAS].effective = bias;
            unit.params[StrainUnit::P_FEEDBACK].effective = 0;
            unit.params[StrainUnit::P_MIX].effective = 32767;
            sum = weighted = 0;
            q15 signal[256];
            for (int i = 0; i < 256; ++i) signal[i] = (q15)(((i * 911) & 32767) - 16384);
            unit.process(signal, 256);
            for (int i = 0; i < 256; ++i) { sum += signal[i]; weighted += (int64_t)signal[i] * (i + 1); }
        };
        int64_t s0,w0,s1,w1,s2,w2;
        render_signature(0, 0, 0, s0, w0);
        render_signature(32767, 0, 0, s1, w1);
        render_signature(0, 32767, 0, s2, w2);
        if (llabs(w1 - w0) < 100000 || llabs(w2 - w0) < 100000) {
            printf("FAIL: STRAIN parameter identity\n"); return 1;
        }
        render_signature(16000, 18000, -24000, s0, w0);
        render_signature(16000, 18000, 24000, s1, w1);
        if (llabs(w1 - w0) < 100000) {
            printf("FAIL: STRAIN bias asymmetry w=%lld\n", (long long)(w1-w0)); return 1;
        }

        // On zero input even extreme BIAS must settle close to zero: asymmetry is
        // a transfer characteristic, never a hidden oscillator/DC source.
        for (q15 bias : {(q15)-32767, (q15)32767}) {
            StrainUnit unit;
            unit.params[StrainUnit::P_DRIVE].effective = 32767;
            unit.params[StrainUnit::P_BIAS].effective = bias;
            unit.params[StrainUnit::P_BODY].effective = 20000;
            unit.params[StrainUnit::P_TEETH].effective = 25000;
            unit.params[StrainUnit::P_FEEDBACK].effective = 32767;
            unit.params[StrainUnit::P_MIX].effective = 32767;
            q15 zero[CONTROL_INTERVAL] = {};
            int residual = 0;
            for (int pass = 0; pass < 512; ++pass) {
                std::memset(zero, 0, sizeof zero);
                unit.control_tick(); unit.process(zero, CONTROL_INTERVAL);
            }
            for (q15 sample : zero) residual += abs((int)sample);
            if (residual > 64) { printf("FAIL: STRAIN silent bias DC\n"); return 1; }
        }

        Engine strain_ui;
        auto* focused_strain = static_cast<StrainUnit*>(make_unit(UnitKind::Strain));
        focused_strain->params[StrainUnit::P_BODY].effective = 22222;
        q15 telemetry[CONTROL_INTERVAL];
        for (int i = 0; i < CONTROL_INTERVAL; ++i) telemetry[i] = (i & 1) ? 18000 : -18000;
        focused_strain->control_tick(); focused_strain->process(telemetry, CONTROL_INTERVAL);
        strain_ui.replace_unit(2, 3, focused_strain);
        EngineUiSnapshot strain_snapshot;
        strain_ui.capture_ui_snapshot(strain_snapshot, 2, 3);
        const uint16_t motion = (uint16_t)(((uint16_t)strain_snapshot.generator_phase_hi << 8) |
                                           strain_snapshot.generator_phase_lo);
        if (!strain_snapshot.effect_focus_valid ||
            strain_snapshot.effect_focus_kind != (uint8_t)UnitKind::Strain ||
            strain_snapshot.effect_focus_chain != 2 || strain_snapshot.effect_focus_slot != 3 ||
            strain_snapshot.focus_effective[StrainUnit::P_BODY] != 22222 ||
            motion != (uint16_t)((int32_t)focused_strain->visual_motion() + 32767)) {
            printf("FAIL: STRAIN focused telemetry\n"); return 1;
        }
    }

    // Moving one module through the linear DSP path must carry every slot-based
    // reference with it: live/banked step locks, scenes, macro routes and layout.
    {
        Engine reorder;
        reorder.replace_unit(0, 0, make_unit(UnitKind::Osc));
        reorder.replace_unit(0, 1, make_unit(UnitKind::Filter));
        reorder.replace_unit(0, 2, make_unit(UnitKind::Delay));
        reorder.pattern.steps[0][0].lock_count = 2;
        reorder.pattern.steps[0][0].locks[0] = {0, 0, 111};
        reorder.pattern.steps[0][0].locks[1] = {1, 0, 222};
        reorder.pattern_banks[1].steps[0][0] = reorder.pattern.steps[0][0];
        reorder.scenes[0][0].valid = true;
        reorder.scenes[0][0].kind[0] = UnitKind::Osc;
        reorder.scenes[0][0].kind[1] = UnitKind::Filter;
        reorder.scenes[0][0].kind[2] = UnitKind::Delay;
        reorder.macro_pads[0].route_count = 1;
        reorder.macro_pads[0].routes[0] = {0, 0, 0, 0, 32767};
        const int moved = reorder.move_unit_before(0, 0, 2);
        if (moved != 1 || reorder.chains[0].units[0]->kind != UnitKind::Filter ||
            reorder.chains[0].units[1]->kind != UnitKind::Osc ||
            reorder.chains[0].units[2]->kind != UnitKind::Delay ||
            reorder.pattern.steps[0][0].locks[0].slot != 1 ||
            reorder.pattern.steps[0][0].locks[1].slot != 0 ||
            reorder.pattern_banks[1].steps[0][0].locks[0].slot != 1 ||
            reorder.scenes[0][0].kind[0] != UnitKind::Filter ||
            reorder.scenes[0][0].kind[1] != UnitKind::Osc ||
            reorder.macro_pads[0].routes[0].slot != 1) {
            printf("FAIL: unit reorder reference migration\n"); return 1;
        }
        if (reorder.move_unit_before(0, 2, 0) != 0 ||
            reorder.chains[0].units[0]->kind != UnitKind::Delay) {
            printf("FAIL: reverse unit reorder\n"); return 1;
        }
        reorder.pattern.steps[0][1].lock_count = 2;
        reorder.pattern.steps[0][1].locks[0] = {0, 0, 333};
        reorder.pattern.steps[0][1].locks[1] = {2, 0, 444};
        reorder.pattern_banks[3].steps[0][1] = reorder.pattern.steps[0][1];
        reorder.scenes[0][0].valid = true;
        reorder.scenes[0][0].kind[0] = UnitKind::Filter;
        reorder.scenes[0][0].kind[1] = UnitKind::Osc;
        reorder.scenes[0][0].kind[2] = UnitKind::Osc;
        reorder.scenes[0][0].base[0][0] = 101;
        reorder.scenes[0][0].base[1][0] = 202;
        reorder.scenes[0][0].base[2][0] = 303;
        reorder.scenes[0][1] = reorder.scenes[0][0];
        reorder.macro_pads[1].route_count = 2;
        reorder.macro_pads[1].routes[0] = {0, 0, 0, 0, 32767};
        reorder.macro_pads[1].routes[1] = {0, 2, 0, 0, 32767};
        delete reorder.remove_unit(0, 0);
        if (reorder.chains[0].units[0]->kind != UnitKind::Filter ||
            reorder.chains[0].units[1]->kind != UnitKind::Osc ||
            reorder.chains[0].units[2] != nullptr ||
            reorder.pattern.steps[0][1].lock_count != 1 ||
            reorder.pattern.steps[0][1].locks[0].slot != 1 ||
            reorder.macro_pads[1].route_count != 1 ||
            reorder.macro_pads[1].routes[0].slot != 1) {
            printf("FAIL: compact unit removal migration\n"); return 1;
        }
        int insert_empty = -1;
        for (int slot = 1; slot < UNITS_PER_CHAIN; ++slot)
            if (!reorder.chains[0].units[slot]) { insert_empty = slot; break; }
        if (reorder.insert_unit(0, 1, make_unit(UnitKind::Ghost)) != insert_empty ||
            reorder.chains[0].units[1]->kind != UnitKind::Ghost ||
            reorder.pattern.steps[0][1].locks[0].slot != 2 ||
            reorder.pattern_banks[3].steps[0][1].locks[0].slot != 2 ||
            reorder.scenes[0][0].kind[1] != UnitKind::Ghost ||
            reorder.scenes[0][0].kind[2] != UnitKind::Osc ||
            reorder.scenes[0][0].base[2][0] != 303 ||
            reorder.scenes[0][1].kind[2] != UnitKind::Osc ||
            reorder.macro_pads[1].routes[0].slot != 2) {
            printf("FAIL: compact unit insertion migration\n"); return 1;
        }
        reorder.pattern.steps[0][2].lock_count = 2;
        reorder.pattern.steps[0][2].locks[0] = {1, 0, 555};
        reorder.pattern.steps[0][2].locks[1] = {0, 0, 666};
        reorder.pattern_banks[4].steps[0][2] = reorder.pattern.steps[0][2];
        delete reorder.remove_unit(0, 1);
        if (reorder.pattern.steps[0][2].lock_count != 1 ||
            reorder.pattern.steps[0][2].locks[0].slot != 0 ||
            reorder.pattern_banks[4].steps[0][2].lock_count != 1 ||
            reorder.pattern_banks[4].steps[0][2].locks[0].slot != 0 ||
            reorder.macro_pads[1].route_count != 1 ||
            reorder.macro_pads[1].routes[0].slot != 1) {
            printf("FAIL: unified removal reference map\n"); return 1;
        }
    }
    // Replacing a slot preserves slot-addressed locks/routes, while scene topology
    // truthfully becomes incompatible when the unit kind changes.
    {
        Engine replace;
        replace.replace_unit(0, 0, make_unit(UnitKind::Osc));
        replace.pattern.steps[0][0].lock_count = 1;
        replace.pattern.steps[0][0].locks[0] = {0, OscUnit::P_GAIN, 1234};
        replace.pattern_banks[1].steps[0][0] = replace.pattern.steps[0][0];
        replace.macro_pads[0].route_count = 1;
        replace.macro_pads[0].routes[0] = {0, 0, OscUnit::P_GAIN, 0, 32767};
        replace.capture_global_scene(0);
        replace.capture_global_scene(1);
        Unit* old = replace.replace_unit(0, 0, make_unit(UnitKind::Filter));
        if (!old || old->kind != UnitKind::Osc ||
            replace.pattern.steps[0][0].locks[0].slot != 0 ||
            replace.pattern_banks[1].steps[0][0].locks[0].slot != 0 ||
            replace.macro_pads[0].routes[0].slot != 0 ||
            replace.global_scene_topology_compatible()) {
            delete old;
            printf("FAIL: replace unit slot identity contract\n"); return 1;
        }
        delete old;
        replace.capture_global_scene(0);
        replace.capture_global_scene(1);
        if (!replace.global_scene_topology_compatible()) {
            printf("FAIL: replace scene recapture contract\n"); return 1;
        }
    }

    // One-level structural undo restores exact module identity plus every slot
    // reference touched by reorder/delete. A replacement invalidates stale history.
    {
        Engine undo;
        undo.replace_unit(0, 0, make_unit(UnitKind::Osc));
        undo.replace_unit(0, 1, make_unit(UnitKind::Filter));
        undo.replace_unit(0, 2, make_unit(UnitKind::Delay));
        Unit* osc = undo.chains[0].units[0];
        Unit* filter = undo.chains[0].units[1];
        Unit* delay = undo.chains[0].units[2];
        undo.pattern.steps[0][0].lock_count = 1;
        undo.pattern.steps[0][0].locks[0] = {0, 0, 111};
        undo.pattern_banks[2].steps[0][0] = undo.pattern.steps[0][0];
        undo.scenes[0][0].valid = true;
        undo.scenes[0][0].kind[0] = UnitKind::Osc;
        undo.scenes[0][0].kind[1] = UnitKind::Filter;
        undo.macro_pads[0].route_count = 1;
        undo.macro_pads[0].routes[0] = {0, 0, 0, 0, 32767};
        if (undo.move_unit_before_with_undo(0, 0, 2) != 1 || !undo.structural_undo_available() ||
            !undo.undo_structural_edit() || undo.structural_undo_available() ||
            undo.chains[0].units[0] != osc || undo.chains[0].units[1] != filter ||
            undo.chains[0].units[2] != delay || undo.pattern.steps[0][0].locks[0].slot != 0 ||
            undo.pattern_banks[2].steps[0][0].locks[0].slot != 0 ||
            undo.scenes[0][0].kind[0] != UnitKind::Osc || undo.macro_pads[0].routes[0].slot != 0) {
            printf("FAIL: structural reorder undo\n"); return 1;
        }
        int restored_chain = -1, restored_slot = -1;
        if (!undo.remove_unit_with_undo(0, 1) || undo.chains[0].units[1] != delay ||
            filter->kind != UnitKind::Filter ||
            !undo.undo_structural_edit(&restored_chain, &restored_slot) ||
            restored_chain != 0 || restored_slot != 1 || undo.chains[0].units[1] != filter ||
            undo.chains[0].units[2] != delay) {
            printf("FAIL: structural delete undo\n"); return 1;
        }
        if (!undo.remove_unit_with_undo(0, 1) || !undo.structural_undo_available()) {
            printf("FAIL: structural undo arm\n"); return 1;
        }
        undo.invalidate_structural_undo();
        if (undo.structural_undo_available() || filter->kind != UnitKind::Filter) {
            printf("FAIL: structural undo deferred invalidation\n"); return 1;
        }
        undo.reclaim_structural_undo();

        // Rebuild the topology for replace invalidation coverage after the
        // controller has reclaimed the retained deleted unit outside the guard.
        delete undo.replace_unit(0, 0, make_unit(UnitKind::Osc));
        delete undo.replace_unit(0, 1, make_unit(UnitKind::Filter));
        delete undo.replace_unit(0, 2, make_unit(UnitKind::Delay));
        if (!undo.remove_unit_with_undo(0, 1)) {
            printf("FAIL: structural undo rearm\n"); return 1;
        }
        Unit* displaced = undo.replace_unit(0, 1, make_unit(UnitKind::Ghost));
        delete displaced;
        if (undo.structural_undo_available() || undo.undo_structural_edit()) {
            printf("FAIL: structural undo invalidation\n"); return 1;
        }
    }

    // Transport regression: exact frame/beat/bar accounting and quantized launch.
    {
        Engine clock;
        clock.set_bpm(120); // 16000 frames/beat
        auto* osc = (OscUnit*)make_unit(UnitKind::Osc);
        clock.replace_unit(0, 0, osc);
        clock.transport_play();
        clock.launch_quantize = LaunchQuantize::Beat;
        clock.request_chain_trigger(0, LaunchQuantize::Beat);
        trackr::fx::q15 tb[16000 * 2] = {};
        clock.render(tb, 15999);
        if (!clock.chain_trigger_pending(0) || clock.beat_count != 0 || clock.frame_in_beat != 15999) {
            printf("FAIL: transport pre-boundary\n"); return 1;
        }
        clock.render(tb, 1);
        if (clock.chain_trigger_pending(0) || clock.beat_count != 1 || clock.frame_in_beat != 0) {
            printf("FAIL: transport beat launch\n"); return 1;
        }
        clock.transport_reset();
        if (clock.transport_frame || clock.beat_count || clock.frame_in_beat) {
            printf("FAIL: transport reset\n"); return 1;
        }

        // REC follows the same exact quantized boundary.
        auto* rec = (RecUnit*)make_unit(UnitKind::Recorder);
        rec->sample_slot = 30;
        rec->prepare_slot();
        clock.replace_unit(1, 0, rec);
        clock.transport_play();
        // The low-level request is allocation-free and therefore accepts an
        // already prepared private workspace at the quantized boundary.
        clock.request_chain_record(1, true, LaunchQuantize::Beat);
        clock.render(tb, 15999);
        if (!clock.chain_record_pending(1) || rec->is_recording()) {
            printf("FAIL: rec pending\n"); return 1;
        }
        clock.render(tb, 1);
        if (clock.chain_record_pending(1) || !rec->is_recording()) {
            printf("FAIL: rec quantized start\n"); return 1;
        }
    }

    // Deterministic 1/16 sequencer: p-locks land before trigs, trigless locks
    // work, and independent track lengths produce polymetric step positions.
    {
        Engine seq;
        seq.set_bpm(120); // 4000 frames per sixteenth
        auto* osc = (OscUnit*)make_unit(UnitKind::Osc);
        const q15 original_gain = osc->params[OscUnit::P_GAIN].base;
        seq.replace_unit(0, 0, osc);
        seq.pattern.length[0] = 3;
        seq.pattern.steps[0][0].flags = SeqStep::Trig;
        if (!seq.set_step_lock(0, 0, 0, OscUnit::P_GAIN, 11111) ||
            !seq.set_step_lock(0, 1, 0, OscUnit::P_GAIN, 22222)) {
            printf("FAIL: sequencer lock setup\n"); return 1;
        }
        // Updating the same destination must not consume a second lock slot.
        seq.set_step_lock(0, 1, 0, OscUnit::P_GAIN, 23456);
        seq.sequencer_enabled = true;
        seq.transport_play();
        static trackr::fx::q15 qb[4000 * 2] = {};
        seq.render(qb, 4000);
        EngineUiSnapshot seq_snap;
        seq.capture_ui_snapshot(seq_snap);
        const uint16_t first_meta = seq_snap.sequence_meta[0][0];
        if (seq.sequence_tick != 1 || seq_snap.sequence_step[0] != 0 ||
            seq_snap.sequence_length[0] != 3 || (seq_snap.step_trig[0] & 1u) == 0 ||
            (first_meta & SeqStep::Trig) == 0 || ((first_meta >> 6) & 7u) != 1u ||
            !seq_snap.sequencer_enabled || osc->params[OscUnit::P_GAIN].base != original_gain ||
            !osc->params[OscUnit::P_GAIN].step_lock_override ||
            osc->params[OscUnit::P_GAIN].control_source() != 11111 ||
            seq.pattern.steps[0][1].lock_count != 1) {
            printf("FAIL: sequencer first tick/lock\n"); return 1;
        }
        seq.render(qb, 4000);
        if (seq.sequence_tick != 2 || osc->params[OscUnit::P_GAIN].base != original_gain ||
            !osc->params[OscUnit::P_GAIN].step_lock_override ||
            osc->params[OscUnit::P_GAIN].control_source() != 23456 ||
            seq.sequence_step(0) != 2) {
            printf("FAIL: sequencer trigless lock/polymeter\n"); return 1;
        }
        seq.render(qb, 4000);
        if (seq.sequence_tick != 3 || osc->params[OscUnit::P_GAIN].base != original_gain ||
            osc->params[OscUnit::P_GAIN].step_lock_override) {
            printf("FAIL: sequencer lock release\n"); return 1;
        }
        seq.transport_reset();
        if (seq.sequence_tick != 0 || seq.sequence_step(0) != 0 ||
            osc->params[OscUnit::P_GAIN].base != original_gain) {
            printf("FAIL: sequencer reset/lock restore\n"); return 1;
        }
        seq.transport_play();
        seq.render(qb, 4000);
        seq.transport_stop();
        if (osc->params[OscUnit::P_GAIN].base != original_gain) {
            printf("FAIL: sequencer stop lock restore\n"); return 1;
        }
        seq.transport_reset();

        // Per-chain traversal is deterministic, including endpoint de-duplication
        // in ping-pong mode and the length-one edge case.
        Engine traversal;
        traversal.set_sequence_length(0, 4);
        traversal.set_sequence_direction(0, SequenceDirection::Forward);
        const uint8_t forward[] = {0, 1, 2, 3, 0};
        for (uint64_t tick = 0; tick < 5; ++tick) {
            traversal.sequence_tick = tick;
            if (traversal.sequence_step(0) != forward[tick]) {
                printf("FAIL: forward traversal\n"); return 1;
            }
        }
        traversal.set_sequence_direction(0, SequenceDirection::Reverse);
        const uint8_t reverse[] = {3, 2, 1, 0, 3};
        for (uint64_t tick = 0; tick < 5; ++tick) {
            traversal.sequence_tick = tick;
            if (traversal.sequence_step(0) != reverse[tick]) {
                printf("FAIL: reverse traversal\n"); return 1;
            }
        }
        traversal.set_sequence_direction(0, SequenceDirection::PingPong);
        const uint8_t pingpong[] = {0, 1, 2, 3, 2, 1, 0, 1};
        for (uint64_t tick = 0; tick < 8; ++tick) {
            traversal.sequence_tick = tick;
            if (traversal.sequence_step(0) != pingpong[tick]) {
                printf("FAIL: ping-pong traversal\n"); return 1;
            }
        }
        traversal.sequence_tick = 4;
        EngineUiSnapshot traversal_snapshot;
        traversal.capture_ui_snapshot(traversal_snapshot);
        if (traversal_snapshot.sequence_step[0] != 3 ||
            traversal_snapshot.sequence_length[0] != 4 ||
            traversal_snapshot.sequence_direction[0] != (uint8_t)SequenceDirection::PingPong) {
            printf("FAIL: traversal snapshot\n"); return 1;
        }
        traversal.set_sequence_length(0, 1);
        traversal.sequence_tick = 99;
        if (traversal.sequence_step(0) != 0) {
            printf("FAIL: one-step ping-pong traversal\n"); return 1;
        }

        // PLAY caches must see a step's P_SLICE lock before the same event
        // triggers. Two alternating slice locks catch stale-window regressions.
        auto& slice_sample = SampleBank::instance().slot(61);
        slice_sample = {};
        slice_sample.channels = 1;
        slice_sample.data.resize(96);
        for (int i = 0; i < 96; ++i) slice_sample.data[i] = (q15)(i * 200 - 9000);
        slice_sample.chops[0] = 0;
        slice_sample.chops[1] = 32;
        slice_sample.chops[2] = 64;
        Engine slice_seq;
        slice_seq.set_bpm(120);
        auto* sliced = new PlayUnit();
        sliced->sample_slot = 61;
        sliced->params[PlayUnit::P_LOOP].base = 0;
        sliced->params[PlayUnit::P_LOOP].snap();
        slice_seq.replace_unit(0, 0, sliced);
        slice_seq.pattern.length[0] = 3;
        slice_seq.pattern.steps[0][0].flags = SeqStep::Trig;
        slice_seq.pattern.steps[0][1].flags = SeqStep::Trig;
        slice_seq.pattern.steps[0][2].flags = SeqStep::Trig;
        const q15 slice_one = (q15)(((1 * 32767) + 8192) /
                                     (trackr::synth::Sample::MAX_CHOPS + 1));
        const q15 slice_three = (q15)(((3 * 32767) + 8192) /
                                       (trackr::synth::Sample::MAX_CHOPS + 1));
        slice_seq.set_step_lock(0, 0, 0, PlayUnit::P_SLICE, slice_one);
        slice_seq.set_step_lock(0, 1, 0, PlayUnit::P_SLICE, slice_three);
        slice_seq.sequencer_enabled = true;
        slice_seq.transport_play();
        slice_seq.render(qb, 4000);
        if (sliced->visual_start() != 0 || sliced->visual_end() < 10000) {
            printf("FAIL: sequencer PLAY first slice cache\n"); return 1;
        }
        slice_seq.render(qb, 4000);
        if (sliced->visual_start() < 21000 || sliced->visual_end() != 32767) {
            printf("FAIL: sequencer PLAY changed slice cache\n"); return 1;
        }
        slice_seq.render(qb, 4000);
        if (sliced->visual_start() != 0 || sliced->visual_end() != 32767) {
            printf("FAIL: sequencer PLAY released slice cache start=%u end=%u\n",
                   sliced->visual_start(), sliced->visual_end()); return 1;
        }
        slice_seq.transport_stop();
        slice_sample = {};

        // Deterministic cycle conditions gate both locks and trigs. A skipped
        // step also releases the previous temporary lock.
        seq.pattern.length[0] = 1;
        seq.clear_step(0, 0);
        seq.pattern.steps[0][0].set_condition(SeqStep::Condition::FirstOfTwo);
        seq.set_step_lock(0, 0, 0, OscUnit::P_GAIN, 12345);
        seq.transport_play();
        seq.render(qb, 4000);
        if (!osc->params[OscUnit::P_GAIN].step_lock_override ||
            osc->params[OscUnit::P_GAIN].control_source() != 12345 ||
            osc->params[OscUnit::P_GAIN].base != original_gain) {
            printf("FAIL: condition 1:2 first cycle\n"); return 1;
        }
        seq.render(qb, 4000);
        if (osc->params[OscUnit::P_GAIN].base != original_gain ||
            osc->params[OscUnit::P_GAIN].step_lock_override) {
            printf("FAIL: condition 1:2 second cycle\n"); return 1;
        }
        seq.transport_reset();
        seq.pattern.steps[0][0].set_condition(SeqStep::Condition::SecondOfTwo);
        seq.transport_play();
        seq.render(qb, 4000);
        if (osc->params[OscUnit::P_GAIN].base != original_gain ||
            osc->params[OscUnit::P_GAIN].step_lock_override) {
            printf("FAIL: condition 2:2 first cycle\n"); return 1;
        }
        seq.render(qb, 4000);
        if (!osc->params[OscUnit::P_GAIN].step_lock_override ||
            osc->params[OscUnit::P_GAIN].control_source() != 12345 ||
            osc->params[OscUnit::P_GAIN].base != original_gain) {
            printf("FAIL: condition 2:2 second cycle\n"); return 1;
        }
        seq.transport_stop();
        seq.clear_step(0, 0);

        seq.sequencer_enabled = true;
        seq.pattern_launch_quantize = LaunchQuantize::Beat;
        seq.set_swing(65);
        seq.sequence_tick = 9;
        seq.pattern.length[1] = 5;
        seq.pattern.direction[1] = SequenceDirection::Reverse;
        seq.pattern.steps[1][4].flags = SeqStep::Trig;
        seq.pattern.steps[1][4].set_condition(SeqStep::Condition::ThreeQuarter);
        seq.pattern.steps[1][4].set_retrig_count(3);
        seq.set_step_microtiming(1, 4, -17);
        seq.pattern_banks[2].length[3] = 11;
        seq.pattern_banks[2].direction[3] = SequenceDirection::PingPong;
        seq.pattern_banks[2].steps[3][10].flags = SeqStep::Trig;
        seq.pattern_banks[2].microtiming[3][10] = 19;
        if (!seq.set_step_lock(1, 4, 0, OscUnit::P_GAIN, 13579)) {
            printf("FAIL: sequencer persist setup\n"); return 1;
        }
        seq.pattern.length[2] = 64;
        seq.replace_unit(2, 0, make_unit(UnitKind::Osc));
        seq.pattern.steps[2][47].flags = SeqStep::Trig;
        seq.pattern.steps[2][47].set_condition(SeqStep::Condition::Half);
        seq.pattern.steps[2][47].set_retrig_count(4);
        seq.pattern.microtiming[2][47] = 11;
        if (!seq.set_step_lock(2, 47, 0, OscUnit::P_PITCH, 20000)) {
            printf("FAIL: folio metadata setup\n"); return 1;
        }
        EngineUiSnapshot metadata_snapshot;
        seq.capture_ui_snapshot(metadata_snapshot);
        const uint16_t packed = metadata_snapshot.sequence_meta[1][4];
        const uint8_t packed_flags = (uint8_t)(packed & 0x3fu);
        const int packed_locks = (packed >> 6) & 7;
        const int packed_timing = (int)((packed >> 9) & 0x3fu) - SEQ_MICROTIMING_MAX;
        if (!(packed_flags & SeqStep::Trig) ||
            (SeqStep::Condition)((packed_flags & SeqStep::ConditionMask) >> SeqStep::ConditionShift) !=
                SeqStep::Condition::ThreeQuarter ||
            ((packed_flags & SeqStep::RetrigMask) >> SeqStep::RetrigShift) + 1 != 3 ||
            packed_locks != 1 || packed_timing != -17 || metadata_snapshot.sequence_length[2] != 64) {
            printf("FAIL: truthful sequencer metadata snapshot\n"); return 1;
        }
        const uint16_t folio_packed = metadata_snapshot.sequence_meta[2][47];
        const uint8_t folio_flags = (uint8_t)(folio_packed & 0x3fu);
        const int folio_locks = (folio_packed >> 6) & 7;
        const int folio_timing = (int)((folio_packed >> 9) & 0x3fu) - SEQ_MICROTIMING_MAX;
        if (!(folio_flags & SeqStep::Trig) ||
            (SeqStep::Condition)((folio_flags & SeqStep::ConditionMask) >> SeqStep::ConditionShift) !=
                SeqStep::Condition::Half ||
            ((folio_flags & SeqStep::RetrigMask) >> SeqStep::RetrigShift) + 1 != 4 ||
            folio_locks != 1 || folio_timing != 11 ||
            metadata_snapshot.sequence_pitch_kind[2] != (uint8_t)UnitKind::Osc ||
            metadata_snapshot.sequence_pitch_slot[2] != 0 ||
            metadata_snapshot.sequence_pitch_param[2] != OscUnit::P_PITCH ||
            metadata_snapshot.sequence_pitch[2][47] != 20000 ||
            !(metadata_snapshot.sequence_pitch_lock_mask[2] & ((uint64_t)1u << 47)) ||
            metadata_snapshot.sequence_lifecycle[2] != (uint8_t)SequenceLifecycle::Flow ||
            metadata_snapshot.sequence_value_slot[2] != 0 ||
            metadata_snapshot.sequence_value_param[2] != OscUnit::P_GAIN ||
            metadata_snapshot.sequence_source_kind[2] != (uint8_t)UnitKind::Osc ||
            metadata_snapshot.sequence_value[2][47] != 14000) {
            printf("FAIL: folio full-pattern metadata snapshot\n"); return 1;
        }
        // The sequencer value DTO projects module-declared lifecycle and physical
        // parameters. GRAN/MOTOR are flow; OSC is flow until a downstream VCA
        // promotes the chain to gate. FM remains strike even with a VCA.
        Engine flow_projection;
        flow_projection.replace_unit(0, 0, make_unit(UnitKind::Grain));
        flow_projection.replace_unit(1, 0, make_unit(UnitKind::Motor));
        flow_projection.replace_unit(2, 0, make_unit(UnitKind::Osc));
        flow_projection.replace_unit(2, 1, make_unit(UnitKind::Vca));
        flow_projection.replace_unit(3, 0, make_unit(UnitKind::Fm));
        flow_projection.replace_unit(3, 1, make_unit(UnitKind::Vca));
        flow_projection.set_step_lock(0, 7, 0, GrainUnit::P_DENS, 11111);
        flow_projection.set_step_lock(1, 8, 0, MotorUnit::P_GAIN, 22222);
        flow_projection.set_step_lock(2, 9, 0, OscUnit::P_GAIN, 12345);
        EngineUiSnapshot flow_snapshot;
        flow_projection.capture_ui_snapshot(flow_snapshot);
        if (flow_snapshot.sequence_lifecycle[0] != (uint8_t)SequenceLifecycle::Flow ||
            flow_snapshot.sequence_value_param[0] != GrainUnit::P_DENS ||
            flow_snapshot.sequence_value[0][7] != 11111 ||
            !(flow_snapshot.sequence_value_lock_mask[0] & ((uint64_t)1u << 7)) ||
            flow_snapshot.sequence_lifecycle[1] != (uint8_t)SequenceLifecycle::Flow ||
            flow_snapshot.sequence_source_kind[1] != (uint8_t)UnitKind::Motor ||
            flow_snapshot.sequence_value_param[1] != MotorUnit::P_GAIN ||
            flow_snapshot.sequence_value[1][8] != 22222 ||
            flow_snapshot.sequence_lifecycle[2] != (uint8_t)SequenceLifecycle::Gate ||
            flow_snapshot.sequence_value_param[2] != OscUnit::P_GAIN ||
            flow_snapshot.sequence_value[2][9] != 12345 ||
            flow_snapshot.sequence_lifecycle[3] != (uint8_t)SequenceLifecycle::Strike) {
            printf("FAIL: sequencer module lifecycle/value projection\n"); return 1;
        }
        Engine loop_projection;
        loop_projection.replace_unit(0, 0, make_unit(UnitKind::SamplePlayer));
        loop_projection.set_step_lock(0, 5, 0, PlayUnit::P_LEN, 16384);
        EngineUiSnapshot loop_snapshot;
        loop_projection.capture_ui_snapshot(loop_snapshot);
        if (loop_snapshot.sequence_lifecycle[0] != (uint8_t)SequenceLifecycle::Loop ||
            loop_snapshot.sequence_duration_param[0] != PlayUnit::P_LEN ||
            loop_snapshot.sequence_duration[0][5] < 126 || loop_snapshot.sequence_duration[0][5] > 128) {
            printf("FAIL: sequencer loop duration projection\n"); return 1;
        }

        const char* sp = "/tmp/seance_seq.snc";
        if (!patchio::save(seq, sp)) { printf("FAIL: sequencer patch save\n"); return 1; }
        Engine loaded_seq;
        patchio::SampleManifest sm;
        if (!patchio::load(loaded_seq, sp, &sm)) { printf("FAIL: sequencer patch load\n"); return 1; }
        std::remove(sp);
        const SeqStep& restored = loaded_seq.pattern.steps[1][4];
        if (!loaded_seq.sequencer_enabled || loaded_seq.sequence_tick != 0 || loaded_seq.swing_percent != 65 ||
            loaded_seq.pattern_launch_quantize != LaunchQuantize::Beat ||
            loaded_seq.pattern.length[1] != 5 ||
            loaded_seq.pattern.direction[1] != SequenceDirection::Reverse ||
            !(restored.flags & SeqStep::Trig) ||
            restored.condition() != SeqStep::Condition::ThreeQuarter || restored.retrig_count() != 3 ||
            loaded_seq.step_microtiming(1, 4) != -17 ||
            restored.lock_count != 1 || restored.locks[0].slot != 0 ||
            restored.locks[0].param != OscUnit::P_GAIN || restored.locks[0].value != 13579 ||
            loaded_seq.pattern_banks[2].length[3] != 11 ||
            loaded_seq.pattern_banks[2].direction[3] != SequenceDirection::PingPong ||
            loaded_seq.pattern_banks[2].microtiming[3][10] != 19 ||
            !(loaded_seq.pattern_banks[2].steps[3][10].flags & SeqStep::Trig)) {
            printf("FAIL: sequencer patch roundtrip\n"); return 1;
        }
        if (!loaded_seq.remove_step_lock(1, 4, 0, OscUnit::P_GAIN) || restored.lock_count != 0 ||
            loaded_seq.remove_step_lock(1, 4, 0, OscUnit::P_GAIN)) {
            printf("FAIL: sequencer lock removal\n"); return 1;
        }
    }

    // Cohesive editing primitives always carry trigger metadata, locks, and
    // microtiming together across steps, tracks, rotations, and pattern banks.
    {
        Engine edit_seq;
        edit_seq.pattern.length[0] = 5;
        edit_seq.pattern.steps[0][0].flags = SeqStep::Trig;
        edit_seq.pattern.steps[0][0].set_condition(SeqStep::Condition::ThreeQuarter);
        edit_seq.pattern.steps[0][0].set_retrig_count(3);
        edit_seq.set_step_lock(0, 0, 2, 3, 12345);
        edit_seq.set_step_microtiming(0, 0, -9);
        if (!edit_seq.copy_step(0, 0, 1, 4)) { printf("FAIL: copy step api\n"); return 1; }
        const SeqStep& copied = edit_seq.pattern.steps[1][4];
        if (!(copied.flags & SeqStep::Trig) || copied.condition() != SeqStep::Condition::ThreeQuarter ||
            copied.retrig_count() != 3 || copied.lock_count != 1 || copied.locks[0].value != 12345 ||
            edit_seq.pattern.microtiming[1][4] != -9) {
            printf("FAIL: cohesive step copy\n"); return 1;
        }
        edit_seq.pattern.direction[0] = SequenceDirection::Reverse;
        if (!edit_seq.copy_track(0, 2) || edit_seq.pattern.length[2] != 5 ||
            edit_seq.pattern.direction[2] != SequenceDirection::Reverse ||
            edit_seq.pattern.microtiming[2][0] != -9 || edit_seq.pattern.steps[2][0].locks[0].value != 12345) {
            printf("FAIL: cohesive track copy\n"); return 1;
        }
        SeqStep restore_steps[SEQ_STEPS] = {};
        int8_t restore_timing[SEQ_STEPS] = {};
        restore_steps[7] = edit_seq.pattern.steps[0][0];
        restore_timing[7] = 13;
        if (!edit_seq.restore_track(3, restore_steps, restore_timing, 48,
                                    SequenceDirection::PingPong) ||
            edit_seq.pattern.length[3] != 48 ||
            edit_seq.pattern.direction[3] != SequenceDirection::PingPong ||
            edit_seq.pattern.microtiming[3][7] != 13 ||
            edit_seq.pattern.steps[3][7].locks[0].value != 12345) {
            printf("FAIL: cohesive track restore\n"); return 1;
        }
        edit_seq.pattern.steps[0][4].flags = SeqStep::Trig;
        edit_seq.set_step_microtiming(0, 4, 11);
        // Engine rotation uses positive=right, negative=left and preserves
        // complete events (condition, retrig, locks, and microtiming).
        if (!edit_seq.rotate_track(0, 1) || edit_seq.pattern.microtiming[0][1] != -9 ||
            edit_seq.pattern.microtiming[0][0] != 11 || !(edit_seq.pattern.steps[0][0].flags & SeqStep::Trig) ||
            !edit_seq.rotate_track(0, -1) || edit_seq.pattern.microtiming[0][0] != -9 ||
            edit_seq.pattern.microtiming[0][4] != 11) {
            printf("FAIL: cohesive track rotate\n"); return 1;
        }
        if (!edit_seq.copy_pattern_bank(0, 3) || edit_seq.pattern_banks[3].microtiming[0][0] != -9 ||
            edit_seq.pattern_banks[3].steps[0][0].locks[0].value != 12345) {
            printf("FAIL: cohesive bank copy\n"); return 1;
        }
        edit_seq.clear_track(2);
        if (edit_seq.pattern.steps[2][0].flags || edit_seq.pattern.microtiming[2][0] ||
            edit_seq.pattern.length[2] != 5) {
            printf("FAIL: clear track\n"); return 1;
        }
        edit_seq.clear_pattern_bank(3);
        if (edit_seq.pattern_banks[3].length[0] != 16 ||
            edit_seq.pattern_banks[3].direction[0] != SequenceDirection::Forward ||
            edit_seq.pattern_banks[3].steps[0][0].flags ||
            edit_seq.pattern_banks[3].microtiming[0][0]) {
            printf("FAIL: clear bank\n"); return 1;
        }
    }

    // Pattern bank launches are immediate while stopped and quantized to a bar
    // while transport runs. The outgoing working copy is committed to its bank.
    {
        Engine banks;
        banks.pattern.length[0] = 3;
        banks.pattern.steps[0][2].flags = SeqStep::Trig;
        banks.pattern_banks[1].length[0] = 7;
        banks.pattern_banks[2].length[0] = 9;
        banks.request_pattern(2, true);
        if (banks.active_pattern != 2 || banks.pending_pattern != 0xff ||
            banks.pattern.length[0] != 9 || banks.pattern_banks[0].length[0] != 3 ||
            !(banks.pattern_banks[0].steps[0][2].flags & SeqStep::Trig)) {
            printf("FAIL: immediate pattern switch\n"); return 1;
        }
        banks.request_pattern(0, false);
        banks.set_bpm(120);
        banks.sequencer_enabled = true;
        banks.pattern_launch_quantize = LaunchQuantize::Bar;
        banks.transport_play();
        banks.request_pattern(1, true);
        static q15 bank_buf[16000 * 2] = {};
        for (int beat = 0; beat < 3; ++beat) banks.render(bank_buf, 16000);
        banks.render(bank_buf, 15999);
        if (banks.active_pattern != 0 || banks.pending_pattern != 1) {
            printf("FAIL: early pattern launch\n"); return 1;
        }
        banks.render(bank_buf, 1);
        if (banks.active_pattern != 1 || banks.pending_pattern != 0xff ||
            banks.pattern.length[0] != 7 || banks.sequence_tick != 1) {
            printf("FAIL: quantized pattern launch\n"); return 1;
        }
        banks.pattern_launch_quantize = LaunchQuantize::Sixteenth;
        banks.request_pattern(2, true);
        banks.render(bank_buf, 3999);
        if (banks.active_pattern != 1 || banks.pending_pattern != 2) {
            printf("FAIL: early sixteenth pattern launch\n"); return 1;
        }
        banks.render(bank_buf, 1);
        if (banks.active_pattern != 2 || banks.pending_pattern != 0xff ||
            banks.pattern.length[0] != 9) {
            printf("FAIL: sixteenth pattern launch\n"); return 1;
        }
        banks.pattern_launch_quantize = LaunchQuantize::Immediate;
        banks.request_pattern(0, true);
        if (banks.active_pattern != 0 || banks.pending_pattern != 0xff) {
            printf("FAIL: immediate running pattern launch\n"); return 1;
        }
    }

    // Pattern chains hold each bank for a bounded number of bars, loop without
    // allocation, stop cleanly in one-shot mode, and manual launches cancel them.
    {
        Engine chain;
        chain.set_bpm(120);
        chain.beats_per_bar = 2;
        chain.pattern_banks[0].length[0] = 3;
        chain.pattern_banks[1].length[0] = 5;
        chain.pattern_banks[2].length[0] = 7;
        if (!chain.set_pattern_chain_step(0, 0, 1) ||
            !chain.set_pattern_chain_step(1, 1, 2) ||
            !chain.set_pattern_chain_step(2, 2, 1)) {
            printf("FAIL: pattern chain setup\n"); return 1;
        }
        chain.set_pattern_chain_length(3);
        if (!chain.insert_pattern_chain_step(1, 3, 4) || chain.pattern_chain_length != 4 ||
            chain.pattern_chain[1].pattern != 3 || chain.pattern_chain[2].pattern != 1 ||
            !chain.move_pattern_chain_step(1, 3) || chain.pattern_chain[3].pattern != 3 ||
            !chain.remove_pattern_chain_step(3) || chain.pattern_chain_length != 3 ||
            chain.insert_pattern_chain_step(-1, 0) || chain.remove_pattern_chain_step(3) ||
            chain.move_pattern_chain_step(0, 3)) {
            printf("FAIL: pattern chain editing\n"); return 1;
        }
        chain.sequencer_enabled = true;
        chain.transport_play();
        if (!chain.start_pattern_chain()) { printf("FAIL: pattern chain start\n"); return 1; }
        static q15 chain_buf[32000 * 2] = {};
        EngineUiSnapshot chain_snap;
        chain.capture_ui_snapshot(chain_snap);
        if (chain.active_pattern != 0 || chain.pattern_chain_bars_remaining != 1 ||
            !chain_snap.pattern_chain_playing || chain_snap.pattern_chain_start_pending ||
            chain_snap.pattern_chain_length != 3 || chain_snap.pattern_chain_position != 0 ||
            chain_snap.pattern_chain_bars_remaining != 1) {
            printf("FAIL: pattern chain first entry/snapshot\n"); return 1;
        }
        chain.render(chain_buf, 32000);
        if (chain.active_pattern != 1 || chain.pattern_chain_position != 1 ||
            chain.pattern_chain_bars_remaining != 2) {
            printf("FAIL: pattern chain first transition\n"); return 1;
        }
        chain.render(chain_buf, 32000);
        if (chain.active_pattern != 1 || chain.pattern_chain_bars_remaining != 1) {
            printf("FAIL: pattern chain hold\n"); return 1;
        }
        chain.render(chain_buf, 32000);
        if (chain.active_pattern != 2 || chain.pattern_chain_position != 2) {
            printf("FAIL: pattern chain second transition\n"); return 1;
        }
        chain.render(chain_buf, 32000);
        if (chain.active_pattern != 0 || chain.pattern_chain_position != 0 || !chain.pattern_chain_playing) {
            printf("FAIL: pattern chain loop\n"); return 1;
        }
        chain.request_pattern(2, false);
        if (chain.pattern_chain_playing || !chain.pattern_chain_paused || chain.active_pattern != 2) {
            printf("FAIL: manual launch chain pause\n"); return 1;
        }

        // Starting mid-bar is causal: the current pattern remains until the next
        // bar boundary, then entry zero receives its full bar count.
        chain.render(chain_buf, 8000);
        const uint8_t before_arm = chain.active_pattern;
        if (!chain.start_pattern_chain() || chain.active_pattern != before_arm) {
            printf("FAIL: mid-bar chain armed early\n"); return 1;
        }
        chain.capture_ui_snapshot(chain_snap);
        if (!chain_snap.pattern_chain_playing || !chain_snap.pattern_chain_start_pending) {
            printf("FAIL: mid-bar chain pending snapshot\n"); return 1;
        }
        chain.render(chain_buf, 23999);
        if (chain.active_pattern != before_arm) {
            printf("FAIL: mid-bar chain early launch\n"); return 1;
        }
        chain.render(chain_buf, 1);
        if (chain.active_pattern != 0 || chain.pattern_chain_position != 0 ||
            chain.pattern_chain_bars_remaining != 1) {
            printf("FAIL: mid-bar chain quantized launch\n"); return 1;
        }
        chain.stop_pattern_chain();

        chain.start_pattern_chain();
        chain.set_pattern_chain_step(0, 1, 3);
        if (!chain.pattern_chain_playing || chain.pattern_chain_bars_remaining != 1 ||
            chain.pattern_chain_current_bars != 1 || chain.active_pattern != 0) {
            printf("FAIL: live chain mutation changed current latch\n"); return 1;
        }
        chain.set_pattern_chain_step(0, 0, 1);
        chain.start_pattern_chain();
        chain.set_pattern_chain_length(2);
        if (!chain.pattern_chain_playing || chain.pattern_chain_position != 0) {
            printf("FAIL: live chain resize stopped progression\n"); return 1;
        }
        chain.set_pattern_chain_length(3);

        chain.pattern_chain_loop = false;
        chain.transport_reset();
        chain.transport_play();
        chain.start_pattern_chain();
        chain.render(chain_buf, 32000);
        chain.render(chain_buf, 32000);
        chain.render(chain_buf, 32000);
        chain.render(chain_buf, 32000);
        if (chain.pattern_chain_playing || chain.active_pattern != 2) {
            printf("FAIL: one-shot pattern chain stop\n"); return 1;
        }

        const char* cp = "/tmp/seance_chain.snc";
        if (!patchio::save(chain, cp)) { printf("FAIL: pattern chain save\n"); return 1; }
        Engine loaded_chain;
        patchio::SampleManifest cm;
        if (!patchio::load(loaded_chain, cp, &cm)) { printf("FAIL: pattern chain load\n"); return 1; }
        if (loaded_chain.pattern_chain_length != 3 || loaded_chain.pattern_chain_loop ||
            loaded_chain.pattern_chain_playing || loaded_chain.pattern_chain_position != 0 ||
            loaded_chain.pattern_chain[0].pattern != 0 || loaded_chain.pattern_chain[0].bars != 1 ||
            loaded_chain.pattern_chain[1].pattern != 1 || loaded_chain.pattern_chain[1].bars != 2 ||
            loaded_chain.pattern_chain[2].pattern != 2 || loaded_chain.pattern_chain[2].bars != 1) {
            printf("FAIL: pattern chain roundtrip\n"); return 1;
        }

        // A recognized PCHN with an invalid entry must fail transactionally.
        FILE* corrupt_chain = std::fopen(cp, "r+b");
        bool patched = false;
        if (corrupt_chain) {
            unsigned char id[4];
            if (std::fseek(corrupt_chain, 5, SEEK_SET) == 0) while (std::fread(id, 1, 4, corrupt_chain) == 4) {
                unsigned char size_bytes[4];
                if (std::fread(size_bytes, 1, 4, corrupt_chain) != 4) break;
                const uint32_t size = (uint32_t)size_bytes[0] | ((uint32_t)size_bytes[1] << 8) |
                                      ((uint32_t)size_bytes[2] << 16) | ((uint32_t)size_bytes[3] << 24);
                if (!std::memcmp(id, "PCHN", 4)) {
                    if (std::fseek(corrupt_chain, 4, SEEK_CUR) == 0) {
                        const unsigned char bad_pattern = NUM_PATTERNS;
                        patched = std::fwrite(&bad_pattern, 1, 1, corrupt_chain) == 1;
                    }
                    break;
                }
                if (std::fseek(corrupt_chain, (long)size, SEEK_CUR) != 0) break;
            }
            std::fclose(corrupt_chain);
        }
        Engine untouched_chain;
        untouched_chain.bpm = 231;
        patchio::SampleManifest untouched_manifest;
        std::strcpy(untouched_manifest.slots[0].path, "chain-unchanged");
        if (!patched || patchio::load(untouched_chain, cp, &untouched_manifest) ||
            untouched_chain.bpm != 231 || std::strcmp(untouched_manifest.slots[0].path, "chain-unchanged")) {
            printf("FAIL: corrupt pattern chain transaction\n"); return 1;
        }
        std::remove(cp);

        chain.clear_pattern_chain();
        if (chain.pattern_chain_length || chain.pattern_chain_playing ||
            chain.pattern_chain_position || chain.pattern_chain[0].pattern ||
            chain.pattern_chain[0].bars != 1) {
            printf("FAIL: clear pattern chain\n"); return 1;
        }
    }

    // Lookahead snapshots the complete step, so edits after planning cannot mix
    // old condition decisions with new trig/lock/retrig data.
    {
        Engine coherent;
        coherent.set_bpm(120);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &coherent.transport_frame;
        coherent.replace_unit(0, 0, counter);
        coherent.pattern.length[0] = 1;
        coherent.pattern.steps[0][0].flags = SeqStep::Trig;
        coherent.sequencer_enabled = true;
        coherent.transport_play();
        static q15 cbuf[4000 * 2] = {};
        coherent.render(cbuf, 3000);
        coherent.clear_step(0, 0);
        coherent.render(cbuf, 1000);
        if (counter->triggers != 1 || counter->trigger_frames[0] != 4000) {
            printf("FAIL: coherent lookahead snapshot\n"); return 1;
        }
    }

    // Immediate running pattern launch dispatches its due-now first step at the
    // exact current frame, before any further audio is rendered.
    {
        Engine immediate;
        immediate.set_bpm(120);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &immediate.transport_frame;
        immediate.replace_unit(0, 0, counter);
        immediate.pattern_banks[1].steps[0][0].flags = SeqStep::Trig;
        immediate.pattern_launch_quantize = LaunchQuantize::Immediate;
        immediate.sequencer_enabled = true;
        immediate.transport_play();
        static q15 ibuf[8100 * 2] = {};
        immediate.render(ibuf, 8100);
        immediate.request_pattern(1, true);
        if (counter->triggers != 1 || counter->trigger_frames[0] != 8100) {
            printf("FAIL: immediate launch exact frame\n"); return 1;
        }
    }

    // Fill/NotFill are momentary performance conditions evaluated at dispatch,
    // so changing fill after lookahead still affects the imminent event.
    {
        Engine fill;
        fill.set_bpm(120);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &fill.transport_frame;
        fill.replace_unit(0, 0, counter);
        fill.pattern.length[0] = 2;
        fill.pattern.steps[0][0].flags = SeqStep::Trig;
        fill.pattern.steps[0][0].set_condition(SeqStep::Condition::Fill);
        fill.pattern.steps[0][0].set_retrig_count(2);
        fill.pattern.steps[0][1].flags = SeqStep::Trig;
        fill.pattern.steps[0][1].set_condition(SeqStep::Condition::NotFill);
        fill.sequencer_enabled = true;
        fill.transport_play();
        static q15 fbuf[10000 * 2] = {};
        fill.render(fbuf, 3000); // step 0 is already in lookahead
        fill.set_sequencer_fill(true);
        fill.render(fbuf, 4999);
        if (counter->triggers != 2 || counter->trigger_frames[0] != 4000 || counter->trigger_frames[1] != 6000) {
            printf("FAIL: fill dispatch/retrig\n"); return 1;
        }
        fill.set_sequencer_fill(false);
        fill.render(fbuf, 4001);
        if (counter->triggers != 3 || counter->trigger_frames[2] != 8000) {
            printf("FAIL: not-fill dispatch\n"); return 1;
        }
        EngineUiSnapshot fill_snap; fill.capture_ui_snapshot(fill_snap);
        if (fill_snap.sequencer_fill_active) { printf("FAIL: fill snapshot\n"); return 1; }
        fill.set_sequencer_fill(true);
        fill.transport_reset();
        if (fill.sequencer_fill_active) { printf("FAIL: fill reset\n"); return 1; }
    }

    // Fill/NotFill consume the two formerly invalid 3-bit condition values.
    // They roundtrip in existing PTRN/PBNK records without a format change.
    {
        Engine saved_fill;
        saved_fill.pattern.steps[0][0].set_condition(SeqStep::Condition::Fill);
        saved_fill.pattern_banks[1].steps[0][1].set_condition(SeqStep::Condition::NotFill);
        const char* fp = "/tmp/seance_fill.snc";
        if (!patchio::save(saved_fill, fp)) { printf("FAIL: fill save\n"); return 1; }
        Engine loaded_fill;
        patchio::SampleManifest fm;
        if (!patchio::load(loaded_fill, fp, &fm)) { printf("FAIL: fill load\n"); return 1; }
        std::remove(fp);
        if (loaded_fill.pattern.steps[0][0].condition() != SeqStep::Condition::Fill ||
            loaded_fill.pattern_banks[1].steps[0][1].condition() != SeqStep::Condition::NotFill ||
            loaded_fill.sequencer_fill_active) {
            printf("FAIL: fill persistence\n"); return 1;
        }
    }

    // Disabling the sequencer mutes events without freezing its grid phase.
    {
        Engine phase;
        phase.set_bpm(120);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &phase.transport_frame;
        phase.replace_unit(0, 0, counter);
        phase.pattern.length[0] = 1;
        phase.pattern.steps[0][0].flags = SeqStep::Trig;
        phase.set_sequencer_enabled(true);
        phase.transport_play();
        static q15 pbuf[9000 * 2] = {};
        phase.render(pbuf, 4000);
        phase.set_sequencer_enabled(false);
        phase.render(pbuf, 4100);
        phase.set_sequencer_enabled(true);
        phase.render(pbuf, 3900);
        if (counter->triggers != 2 || counter->trigger_frames[0] != 4000 || counter->trigger_frames[1] != 12000) {
            printf("FAIL: sequencer re-enable phase\n"); return 1;
        }
    }

    // Probability conditions use a deterministic RNG sequence that resets with transport.
    {
        Engine probability;
        probability.set_bpm(120);
        auto* osc = (OscUnit*)make_unit(UnitKind::Osc);
        const q15 base = osc->params[OscUnit::P_GAIN].base;
        probability.replace_unit(0, 0, osc);
        probability.pattern.length[0] = 1;
        probability.pattern.steps[0][0].set_condition(SeqStep::Condition::Half);
        probability.set_step_lock(0, 0, 0, OscUnit::P_GAIN, 1234);
        probability.sequencer_enabled = true;
        static q15 pb[4000 * 2] = {};
        uint16_t masks[2] = {};
        for (int pass = 0; pass < 2; ++pass) {
            probability.transport_reset();
            probability.transport_play();
            for (int tick = 0; tick < 16; ++tick) {
                probability.render(pb, 4000);
                if (osc->params[OscUnit::P_GAIN].step_lock_override &&
                    osc->params[OscUnit::P_GAIN].control_source() == 1234) {
                    masks[pass] |= (uint16_t)(1u << tick);
                } else if (osc->params[OscUnit::P_GAIN].step_lock_override ||
                           osc->params[OscUnit::P_GAIN].base != base) {
                    printf("FAIL: probability lock restore\n"); return 1;
                }
            }
        }
        probability.transport_stop();
        if (masks[0] != masks[1] || masks[0] == 0 || masks[0] == 0xffffu) {
            printf("FAIL: deterministic probability\n"); return 1;
        }
    }

    // Ratchets distribute additional triggers inside the active sixteenth.
    {
        Engine ratchet;
        ratchet.set_bpm(120);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &ratchet.transport_frame;
        ratchet.replace_unit(0, 0, counter);
        ratchet.pattern.length[0] = 1;
        ratchet.pattern.steps[0][0].flags = SeqStep::Trig;
        ratchet.pattern.steps[0][0].set_retrig_count(4);
        ratchet.sequencer_enabled = true;
        ratchet.transport_play();
        static q15 rb[4000 * 2] = {};
        ratchet.render(rb, 4000);
        if (counter->triggers != 1) { printf("FAIL: ratchet primary\n"); return 1; }
        ratchet.render(rb, 4000);
        if (counter->triggers != 5) { printf("FAIL: ratchet subdivisions\n"); return 1; }

        ratchet.transport_reset();
        counter->triggers = 0;
        ratchet.pattern.steps[0][0].set_retrig_count(1);
        ratchet.pattern.steps[0][0].set_condition(SeqStep::Condition::FirstOfTwo);
        ratchet.transport_play();
        ratchet.render(rb, 4000); // cycle 1: pass
        ratchet.render(rb, 4000); // cycle 2: skip
        if (counter->triggers != 1) { printf("FAIL: condition trigger gate\n"); return 1; }
        ratchet.render(rb, 4000); // cycle 1 again
        if (counter->triggers != 2) { printf("FAIL: condition trigger repeat\n"); return 1; }
    }

    // Swing delays odd steps sample-accurately and redistributes ratchets over
    // the resulting long/short pair. At 120 BPM, a sixteenth is 4000 frames;
    // 75% swing produces a 6000/2000-frame pair.
    {
        Engine swing;
        swing.set_bpm(120);
        swing.set_swing(75);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &swing.transport_frame;
        swing.replace_unit(0, 0, counter);
        swing.pattern.length[0] = 2;
        for (int step = 0; step < 2; ++step) {
            swing.pattern.steps[0][step].flags = SeqStep::Trig;
            swing.pattern.steps[0][step].set_retrig_count(4);
        }
        swing.sequencer_enabled = true;
        swing.transport_play();
        static q15 sbuf[12000 * 2] = {};
        swing.render(sbuf, 12000);
        const uint64_t expected[] = {4000, 5500, 7000, 8500, 10000, 10500, 11000, 11500, 12000};
        if (counter->triggers != 9) { printf("FAIL: swing trigger count %d\n", counter->triggers); return 1; }
        for (int i = 0; i < 9; ++i) if (counter->trigger_frames[i] != expected[i]) {
            printf("FAIL: swing frame %d got %llu expected %llu\n", i,
                   (unsigned long long)counter->trigger_frames[i], (unsigned long long)expected[i]);
            return 1;
        }
    }

    // Signed microtiming uses real lookahead: negative offsets fire before the
    // grid, positive offsets after it, and retrigs span the shifted event window.
    {
        Engine micro;
        micro.set_bpm(120);
        auto* counter = new TriggerCounterUnit();
        counter->transport_frame = &micro.transport_frame;
        micro.replace_unit(0, 0, counter);
        micro.pattern.length[0] = 3;
        for (int step = 0; step < 3; ++step) micro.pattern.steps[0][step].flags = SeqStep::Trig;
        micro.set_step_microtiming(0, 0, -12); // -500 frames
        micro.set_step_microtiming(0, 1, 0);
        micro.set_step_microtiming(0, 2, 12);  // +500 frames
        micro.sequencer_enabled = true;
        micro.transport_play();
        static q15 mbuf[13000 * 2] = {};
        micro.render(mbuf, 13000);
        const uint64_t expected[] = {3500, 8000, 12500};
        if (counter->triggers != 3) { printf("FAIL: microtiming trigger count %d\n", counter->triggers); return 1; }
        for (int i = 0; i < 3; ++i) if (counter->trigger_frames[i] != expected[i]) {
            printf("FAIL: microtiming frame %d got %llu expected %llu\n", i,
                   (unsigned long long)counter->trigger_frames[i], (unsigned long long)expected[i]);
            return 1;
        }

        micro.transport_reset();
        counter->triggers = 0;
        micro.pattern.length[0] = 2;
        micro.clear_step(0, 0); micro.clear_step(0, 1);
        micro.pattern.steps[0][0].flags = SeqStep::Trig;
        micro.pattern.steps[0][0].set_retrig_count(4);
        micro.pattern.steps[0][1].flags = SeqStep::Trig;
        micro.set_step_microtiming(0, 0, -12);
        micro.set_step_microtiming(0, 1, 12);
        micro.transport_play();
        micro.render(mbuf, 9000);
        const uint64_t retrig_expected[] = {3500, 4750, 6000, 7250, 8500};
        if (counter->triggers != 5) { printf("FAIL: shifted retrig count %d\n", counter->triggers); return 1; }
        for (int i = 0; i < 5; ++i) if (counter->trigger_frames[i] != retrig_expected[i]) {
            printf("FAIL: shifted retrig frame %d got %llu expected %llu\n", i,
                   (unsigned long long)counter->trigger_frames[i], (unsigned long long)retrig_expected[i]);
            return 1;
        }
    }

    // Global performance scenes capture and morph all chains from immutable endpoints.
    {
        Engine performance;
        auto* a = (OscUnit*)make_unit(UnitKind::Osc);
        auto* b = (OscUnit*)make_unit(UnitKind::Osc);
        performance.replace_unit(0, 0, a);
        performance.replace_unit(1, 0, b);
        a->params[OscUnit::P_GAIN].base = 2000;
        b->params[OscUnit::P_GAIN].base = 6000;
        performance.chains[0].volume = 10000;
        performance.capture_global_scene(0);
        a->params[OscUnit::P_GAIN].base = 30000;
        b->params[OscUnit::P_GAIN].base = 26000;
        performance.chains[0].volume = 30000;
        performance.capture_global_scene(1);
        performance.apply_global_scene_morph(16384);
        if (a->params[OscUnit::P_GAIN].base != 16000 ||
            b->params[OscUnit::P_GAIN].base != 16000 ||
            performance.chains[0].volume != 20000 ||
            performance.scene_morph[0] != 16384 || performance.scene_morph[3] != 16384) {
            printf("FAIL: global performance morph\n"); return 1;
        }
        performance.apply_global_scene_morph(0);
        if (a->params[OscUnit::P_GAIN].base != 2000 || b->params[OscUnit::P_GAIN].base != 6000) {
            printf("FAIL: performance endpoint A\n"); return 1;
        }
        performance.apply_global_scene_morph(32767);
        if (a->params[OscUnit::P_GAIN].base != 29999 || b->params[OscUnit::P_GAIN].base != 25999) {
            printf("FAIL: performance endpoint B\n"); return 1;
        }
        delete performance.replace_unit(0, 1, make_unit(UnitKind::Filter));
        if (performance.global_scene_topology_compatible()) {
            printf("FAIL: morph topology compatibility\n"); return 1;
        }
        const q15 before_reject = a->params[OscUnit::P_GAIN].base;
        performance.apply_global_scene_morph(0);
        if (a->params[OscUnit::P_GAIN].base != before_reject) {
            printf("FAIL: incompatible morph mutation\n"); return 1;
        }
    }

    // Global performance FX: master-bus targets (Cutoff/Res/Crush/Drive) work
    // with NO matching units installed; time-based targets still take over
    // params of installed units.
    {
        Engine global_fx;
        auto* rev = new ReverbUnit(); auto* dly = new DelayUnit();
        global_fx.replace_unit(1, 1, rev); global_fx.replace_unit(2, 0, dly);
        global_fx.apply_global_fx(GlobalFxTarget::Cutoff, 32767);
        if (global_fx.master_fx_value(GlobalFxTarget::Cutoff) != 0) {
            // current must start at 0 and slew in render, not jump
            printf("FAIL: master cutoff pre-slew\n"); return 1;
        }
        global_fx.apply_global_fx(GlobalFxTarget::Reverb, 32767);
        if (rev->params[ReverbUnit::P_MIX].base != 9000 ||
            rev->params[ReverbUnit::P_MIX].performance < 25990 ||
            !rev->params[ReverbUnit::P_MIX].performance_override) {
            printf("FAIL: global reverb\n"); return 1;
        }
        global_fx.apply_global_fx(GlobalFxTarget::Reverb, 0);
        if (rev->params[ReverbUnit::P_MIX].base != 9000 ||
            rev->params[ReverbUnit::P_MIX].performance_override) {
            printf("FAIL: global reverb release\n"); return 1;
        }
        global_fx.apply_global_fx_mask(0);   // full release must clear master targets too
    }

    // Master-bus filter is audible with zero filter units in any chain:
    // a bright square through full Cutoff must lose most of its RMS energy
    // relative to the dry render, and produce no hard clicks.
    {
        Engine master;
        auto* osc = new OscUnit();
        osc->params[OscUnit::P_GAIN].base = 24000;
        master.replace_unit(0, 0, osc);
        master.trigger_chain(0);
        constexpr int N = 32000; // 1s
        static q15 dry[N * 2], wet[N * 2];
        master.render(dry, N);
        double dry_rms = 0;
        for (int i = 0; i < N * 2; ++i) dry_rms += (double)dry[i] * dry[i];
        dry_rms = sqrt(dry_rms / (N * 2));

        master.apply_global_fx(GlobalFxTarget::Cutoff, 32767);
        master.render(wet, N);
        double wet_rms = 0; int master_clicks = 0; q15 prev = 0;
        // skip the first 100ms of slew before measuring
        for (int i = 3200 * 2; i < N * 2; ++i) {
            wet_rms += (double)wet[i] * wet[i];
            int d = (int)wet[i] - prev; prev = wet[i];
            if (d > 24000 || d < -24000) ++master_clicks;
        }
        wet_rms = sqrt(wet_rms / (N * 2 - 6400));
        if (dry_rms < 500) { printf("FAIL: master fx dry silent\n"); return 1; }
        if (wet_rms > dry_rms * 0.7) {
            printf("FAIL: master cutoff inaudible (dry=%.0f wet=%.0f)\n", dry_rms, wet_rms); return 1;
        }
        if (master_clicks > 4) { printf("FAIL: master fx clicks=%d\n", master_clicks); return 1; }
        if (master.master_fx_value(GlobalFxTarget::Cutoff) < 30000) {
            printf("FAIL: master cutoff slew stuck\n"); return 1;
        }
        // release: mask clear must slew back to zero and restore level
        master.apply_global_fx_mask(0);
        master.render(wet, N);
        if (master.master_fx_value(GlobalFxTarget::Cutoff) != 0) {
            printf("FAIL: master cutoff release\n"); return 1;
        }
    }

    // Standalone .pad roundtrip and transactional macro application.
    {
        MacroPad pads[8]; std::strcpy(pads[0].name, "HAUNT"); pads[0].route_count=1;
        pads[0].routes[0].chain=0; pads[0].routes[0].slot=1; pads[0].routes[0].param=FilterUnit::P_CUTOFF;
        pads[0].routes[0].minimum=1000; pads[0].routes[0].maximum=30000;
        if(!padio::save(pads,"/tmp/seance_test.pad")){printf("FAIL: pad save\n");return 1;}
        MacroPad loaded[8]; if(!padio::load(loaded,"/tmp/seance_test.pad")||loaded[0].routes[0].maximum!=30000){printf("FAIL: pad load\n");return 1;}
        std::memcpy(eng->macro_pads,loaded,sizeof loaded); eng->apply_macro_pad(0,32767);
        if(eng->chains[0].units[1]->params[FilterUnit::P_CUTOFF].base!=29999){printf("FAIL: pad apply\n");return 1;}
        std::remove("/tmp/seance_test.pad");
    }

    bool ok = rms > 500 && rms < 20000 && peak < 32768 && fabs(dc) < 200 && clicks < 10;
    printf(ok ? "OK\n" : "FAIL\n");

    // write wav for ears
    FILE* f = fopen("/tmp/seance_test.wav", "wb");
    if (f) {
        uint32_t data_bytes = TOTAL * 2 * 2;
        uint32_t chunk = 36 + data_bytes;
        uint16_t fmt = 1, chans = 2, bits = 16;
        uint32_t sr = 32000, brate = sr * 4;
        uint16_t align = 4;
        fwrite("RIFF", 1, 4, f); fwrite(&chunk, 4, 1, f); fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f); uint32_t fsz = 16; fwrite(&fsz, 4, 1, f);
        fwrite(&fmt, 2, 1, f); fwrite(&chans, 2, 1, f); fwrite(&sr, 4, 1, f);
        fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
        fwrite(out, 2, TOTAL * 2, f);
        fclose(f);
        printf("wav: /tmp/seance_test.wav\n");
    }

    delete eng;
    return ok ? 0 : 1;
}
