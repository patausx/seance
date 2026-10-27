#include "units.h"
#include "bird_tables.h"
#include "play_window.h"
#include "grain_window.h"
#include "ghost_decay.h"
#include "chorus_motion.h"
#include "vca_envelope.h"
#include "vca_timing.h"
#include "delay_time.h"
#include "tilt_eq.h"
#include "tilt_extended.h"
#include "fm_extended.h"
#include "musical_note.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <climits>
#include <new>
#include <atomic>
#include "comp_impl.inc"
#include "mouth_impl.inc"
#include "osc_clean_impl.inc"
#include "arp_impl.inc"
#include "glitch_impl.inc"
#include "inst_impl.inc"
#include "reverb_impl.inc"

namespace seance {

using trackr::fx::Q15_ONE;
using trackr::fx::lerp_q15;
using trackr::fx::sat_q15;
using trackr::synth::SampleBank;
using trackr::synth::Sample;

// Shared exact semitone LUT (+/-24 st), consumed by STRN pitch and GRAN's PDEV.
// It is built through a function-local static so any caller — including a GRAN in
// a patch that contains no STRN — is guaranteed a populated table. Relying on a
// constructor to prime a bare global previously returned ratio 0 and collapsed
// every grain to the minimum increment.
static const int32_t* pitch_lut() {
    static const struct Lut {
        int32_t value[257];
        Lut() {
            for (int i = 0; i <= 256; ++i) {
                const double semitones = -24.0 + (48.0 * i / 256.0);
                value[i] = (int32_t)(std::pow(2.0, semitones / 12.0) * 65536.0 + 0.5);
            }
        }
    } table;
    return table.value;
}
// Retained for the existing call sites; the table is now self-initializing.
static void build_strn_pitch_lut() { (void)pitch_lut(); }
static int32_t semitone_inc_q16(q15 pitch) {
    const int32_t* lut = pitch_lut();
    uint32_t scaled = (uint32_t)((int32_t)pitch + 32767) * 256u;
    uint32_t index = scaled / 65534u;
    uint32_t rem = scaled % 65534u;
    if (index >= 256) return lut[256];
    return lut[index] +
           (int32_t)(((int64_t)(lut[index + 1] - lut[index]) * rem) / 65534u);
}

static uint32_t playable_frames(const Sample& s) {
    return s.playable_frames();
}

// ============================ PLAY ============================

PlayUnit::PlayUnit() {
    kind = UnitKind::SamplePlayer;
    add_param("SPD",  8192, true);    // signed varispeed: +/-8192 ~= +/-1x, max ~= 4x
    add_param("STRT", 0);
    add_param("LEN",  Q15_ONE);
    add_param("LOOP", 0);             // modern default: one traversal, independent of gate
    add_param("GAIN", Q15_ONE);
    add_param("SLCE", 0);             // 0=window, 1..16=chops
    add_param("MODE", Q15_ONE);       // sequence gate; manual triggers never arm it
    add_param("GATE", play_mode::gate_value(play_mode::DEFAULT_GATE));
    add_param("POS", 0);
    add_param("SCRUB", 0);
    add_param("HOLD", 0); // scratch by default; only meaningful while scrub is enabled
    add_param("NOTE", musical_note::encode(musical_note::ROOT));
    num_params = P_NOTE; // bare constructor preserves eleven-control modern legacy PLAY
}

void PlayUnit::set_modern(bool enabled) {
    if (modern_ != enabled) reset();
    modern_ = enabled;
    if (!enabled) pitch_mode_ = PitchMode::Legacy;
    num_params = enabled ? (pitch_mode_ == PitchMode::Legacy ? P_NOTE : P_COUNT) : 6;
    if (!enabled) { edit_ = {}; edit_source_ = {}; }
}

void PlayUnit::set_pitch_mode(PitchMode mode) {
    if (!modern_ || (mode != PitchMode::Normal && mode != PitchMode::Stretch)) mode = PitchMode::Legacy;
    if (mode != pitch_mode_) reset();
    pitch_mode_ = mode;
    num_params = modern_ ? (mode == PitchMode::Legacy ? P_NOTE : P_COUNT) : 6;
}

int PlayUnit::actual_note() const {
    const int midi = musical_note::decode(params[P_NOTE].target) + (preview_ ? 0 : arp_voice.semitones);
    return std::max(0, std::min(120, midi));
}

static int32_t play_musical_increment(PlayUnit::PitchMode mode, q15 speed, int midi,
                                      bool reverse, uint16_t cut_rate) {
    if (mode != PlayUnit::PitchMode::Normal && mode != PlayUnit::PitchMode::Stretch) return 0;
    int32_t magnitude = musical_note::ratio_q16(midi - musical_note::ROOT);
    if (mode == PlayUnit::PitchMode::Stretch) {
        const int32_t value = speed;
        magnitude = (int32_t)((int64_t)(value < 0 ? -value : value) * 8 * Sample::cut_speed(cut_rate) / 256);
        magnitude = std::max<int32_t>(16384, std::min<int32_t>(262144, magnitude)); // temporal .25..4x, no quantization
    }
    return ((speed < 0) != reverse) ? -magnitude : magnitude;
}

int32_t PlayUnit::projection_increment(PitchMode mode, q15 speed, q15 note,
                                      bool reversed_slice, uint16_t cut_rate) {
    return play_musical_increment(mode, speed, musical_note::decode(note), reversed_slice, cut_rate);
}

int32_t PlayUnit::musical_increment(bool reverse, uint16_t cut_rate) const {
    return play_musical_increment(pitch_mode_, params[P_SPEED].effective, actual_note(), reverse, cut_rate);
}

PlayUnit::SourceBinding PlayUnit::source_binding() const {
    SourceBinding out;
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return out;
    const auto& bank = SampleBank::instance();
    const Sample& sample = bank.slot(sample_slot);
    out.slot = sample_slot; out.generation = bank.generation(sample_slot);
    out.frames = sample.playable_frames(); out.data = sample.data.data();
    out.elements = sample.data.size(); out.channels = sample.channels;
    return out;
}

bool PlayUnit::source_matches(const SourceBinding& bound) const {
    const SourceBinding now = source_binding();
    return bound.slot >= 0 && now.slot == bound.slot && now.generation == bound.generation &&
        now.frames == bound.frames && now.data == bound.data && now.elements == bound.elements &&
        now.channels == bound.channels && (now.channels == 1 || now.channels == 2);
}

bool PlayUnit::edit_source_matches() const {
    return !edit_.enabled || (source_matches(edit_source_) && edit_source_.frames == edit_.source_frames);
}

bool PlayUnit::set_edit_state(const PlayEditState& edit, uint32_t expected_generation) {
    if (!play_edit_valid(edit) || (edit.enabled && !modern_)) return false;
    const SourceBinding now = source_binding();
    if (now.slot < 0 || now.generation != expected_generation ||
        (edit.enabled && (now.frames != edit.source_frames || (now.channels != 1 && now.channels != 2)))) return false;
    edit_ = edit.enabled ? edit : PlayEditState{};
    edit_source_ = edit.enabled ? now : SourceBinding{};
    pending_edit_source_.slot = -1;
    return true; // active musical voice keeps its already-latched window
}

bool PlayUnit::restore_edit_state(const PlayEditState& edit) {
    if (!play_edit_valid(edit) || (edit.enabled && !modern_)) return false;
    reset();
    edit_ = edit.enabled ? edit : PlayEditState{};
    edit_source_ = {};
    pending_edit_source_.slot = -1;
    return true;
}

void PlayUnit::stage_edit_source(const EditSourceMetadata& source, bool hydrated_missing) {
    if (edit_source_.slot >= 0 || source.slot != sample_slot) return;
    pending_edit_source_ = source;
    pending_hydrated_missing_ = hydrated_missing;
    if (hydrated_missing) pending_empty_source_ = source_binding();
}

const PlayUnit::EditSourceMetadata* PlayUnit::pending_edit_source() const {
    if (edit_source_.slot >= 0 || pending_edit_source_.slot != sample_slot ||
        sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return nullptr;
    if (pending_hydrated_missing_ && !source_matches(pending_empty_source_)) return nullptr;
    return &pending_edit_source_;
}

bool PlayUnit::bind_edit_source() {
    if (!edit_.enabled) { pending_edit_source_.slot = -1; return true; }
    const SourceBinding now = source_binding();
    if (!modern_ || now.slot < 0 || now.frames != edit_.source_frames ||
        (now.channels != 1 && now.channels != 2)) return false;
    edit_source_ = now;
    pending_edit_source_.slot = -1;
    return true;
}

PlayWindow PlayUnit::projected_window(q15 start, q15 length, q15 slice) const {
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE || !edit_source_matches()) return {};
    const auto& sample = SampleBank::instance().slot(sample_slot);
    if (pitch_mode_ != PitchMode::Legacy && sample.channels != 1 && sample.channels != 2) return {};
    PlayWindow window = play_edit_window(sample, params[P_SPEED].effective,
        start, length, slice, pitch_quantize, modern_, edit_);
    if (pitch_mode_ != PitchMode::Legacy && params[P_SCRUB].effective <= 16383) {
        const bool reverse = window.slice && (sample.slice_reverse_mask & (1u << (window.slice - 1)));
        const uint16_t rate = window.slice ? Sample::cut_speed(sample.slice_speed_q8[window.slice - 1]) : 256;
        window.increment = musical_increment(reverse, rate);
    }
    return window;
}

void PlayUnit::latch_edit_window(const PlayWindow& window) {
    voice_edit_ = window;
    if (pitch_mode_ != PitchMode::Legacy && !window.edited) {
        voice_edit_.loop_start = window.start; voice_edit_.loop_end = window.end;
    }
    latched_edit_ = window.edited;
    loop_entered_ = false;
    stretch_.reset();
    voice_source_ = source_binding();
}

void PlayUnit::release() {
    gate_deadline_ = UINT64_MAX;
    if (playing_ && !release_remaining_) release_remaining_ = 64;
}

void PlayUnit::release_sequence_gate() {
    if (gate_deadline_ != UINT64_MAX) release();
}

void PlayUnit::control_tick() {
    if (pitch_mode_ != PitchMode::Legacy && (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE)) {
        playing_ = false; visual_state_ = 0; stretch_.reset(); return;
    }
    const Sample& s = SampleBank::instance().slot(sample_slot);
    PlayWindow window;
    if (preview_ || (modern_ && window_latched_)) {
        // Speed/gain remain live; lock release, modulation and trim/slice edits
        // must not move an accepted voice's boundary or reverse-slice identity.
        window = play_window(s, params[P_SPEED].effective, 0, Q15_ONE, 0, pitch_quantize, modern_);
        inc_q16_ = latched_reverse_ ? -window.increment : window.increment;
        const uint16_t rate = preview_ ? preview_speed_q8_ : latched_slice_ && modern_
            ? Sample::cut_speed(s.slice_speed_q8[latched_slice_ - 1]) : 256;
        inc_q16_ = (int32_t)((int64_t)inc_q16_ * rate / 256);
        rate_bound_ = rate_bound_ || (modern_ && latched_slice_ && rate != 256);
        const uint32_t nf = playable_frames(s);
        window.normalized_start = nf > 1 ? (uint16_t)((uint64_t)win_start_ * 32767u / (nf - 1)) : 0;
        window.normalized_end = nf > 1 && win_end_ ? (uint16_t)((uint64_t)(win_end_ - 1) * 32767u / (nf - 1)) : 0;
    } else {
        window = projected_window(params[P_START].effective, params[P_LEN].effective,
            params[P_SLICE].effective);
        inc_q16_ = window.increment;
        win_start_ = window.start;
        win_end_ = window.end;
    }
    if (arp_voice.semitones && !preview_) {
        const int64_t pitched = int64_t(inc_q16_) * arp_ratio_q24(arp_voice.semitones) / (1u << 24);
        inc_q16_ = int32_t(std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, pitched)));
    }
    const bool next_scrub = (modern_ && params[P_SCRUB].effective > 16383) || (preview_ && preview_scrub_forced_);
    if (next_scrub != scrub_) stretch_.reset();
    scrub_ = next_scrub;
    if (pitch_mode_ != PitchMode::Legacy && !scrub_) {
        const uint8_t slice = window_latched_ ? latched_slice_ : window.slice;
        const bool reverse = window_latched_ ? latched_reverse_
            : slice && (s.slice_reverse_mask & (1u << (slice - 1)));
        const uint16_t rate = preview_ ? preview_speed_q8_
            : slice ? Sample::cut_speed(s.slice_speed_q8[slice - 1]) : 256;
        inc_q16_ = musical_increment(reverse, rate);
        const int32_t pitch = musical_note::ratio_q16(actual_note() - musical_note::ROOT);
        note_inc_q16_ = inc_q16_ < 0 ? -pitch : pitch;
    }
    if (playable_frames(s) < 2) {
        visual_position_ = visual_start_ = visual_end_ = 0;
        visual_state_ = 0;
        return;
    }
    visual_speed_ = (int16_t)(inc_q16_ < 0
        ? -(params[P_SPEED].effective < 0 ? -params[P_SPEED].effective : params[P_SPEED].effective)
        :  (params[P_SPEED].effective < 0 ? -params[P_SPEED].effective : params[P_SPEED].effective));
    visual_start_ = window.normalized_start;
    visual_end_ = window.normalized_end;
}

void PlayUnit::preview(q15 start, q15 length, q15 slice) {
    if (pitch_mode_ != PitchMode::Legacy && (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE)) { reset(); return; }
    const Sample& sample = SampleBank::instance().slot(sample_slot);
    const PlayWindow window = projected_window(start, length, slice);
    latch_edit_window(window);
    preview_repeat_ = false;
    preview_scrub_forced_ = scrub_override_ = false;
    latched_slice_ = window.slice;
    preview_speed_q8_ = modern_ && window.slice ? Sample::cut_speed(sample.slice_speed_q8[window.slice - 1]) : 256;
    preview_ = window_latched_ = true;
    latched_loop_ = latched_gate_ = false;
    latched_reverse_ = window.slice && (sample.slice_reverse_mask & (1u << (window.slice - 1)));
    gate_deadline_ = UINT64_MAX;
    release_remaining_ = 0;
    win_start_ = window.start; win_end_ = window.end; inc_q16_ = window.increment;
    playing_ = window.valid;
    frac_ = 0;
    amp_q15_ = 0;
    pos_ = inc_q16_ >= 0 ? win_start_ : (window.valid ? win_end_ - (latched_edit_ || pitch_mode_ != PitchMode::Legacy ? 1u : 2u) : win_start_);
    visual_start_ = window.normalized_start;
    visual_end_ = window.normalized_end;
    visual_position_ = inc_q16_ >= 0 ? visual_start_ : visual_end_;
    visual_state_ = playing_ ? (VisualPlaying | (inc_q16_ < 0 ? VisualReverse : 0)) : 0;
    control_tick(); // speed/telemetry only while preview window is latched
    if (scrub_) begin_scrub();
}

bool PlayUnit::preview_frames(uint32_t start, uint32_t end, bool repeat, uint32_t expected_generation,
                              bool reverse, uint16_t speed_q8, bool scrub) {
    const SourceBinding source = source_binding();
    if (source.slot < 0 || source.generation != expected_generation ||
        start >= end || end > source.frames || end - start < 2 ||
        (source.channels != 1 && source.channels != 2) || !edit_source_matches() ||
        speed_q8 < Sample::CUT_SPEED_MIN || speed_q8 > Sample::CUT_SPEED_MAX) return false;
    const Sample& sample = SampleBank::instance().slot(sample_slot);
    PlayWindow window = play_window(sample, params[P_SPEED].effective, 0, Q15_ONE, 0, pitch_quantize, modern_);
    window.start = start; window.end = end; window.valid = true; window.edited = true;
    window.loop_start = start; window.loop_end = end;
    if (edit_.enabled) {
        window.fade_in = std::min(edit_.fade_in, end - start);
        window.fade_out = std::min(edit_.fade_out, end - start);
        // Ordinary editor repeat repeats the entire fade-shaped selection.
        // Only explicit seam audition borrows the musical crossfade.
        window.crossfade = 0;
    }
    // Caller supplies selected-cut direction explicitly. Inferring a base SLCE
    // here would double-flip private preview voices and lose chain ownership.
    latched_reverse_ = reverse;
    latched_slice_ = 0; preview_speed_q8_ = speed_q8;
    preview_scrub_forced_ = scrub; scrub_override_ = false;
    if (latched_reverse_) window.increment = -window.increment;
    latch_edit_window(window);
    preview_ = window_latched_ = true; preview_repeat_ = repeat; preview_seam_ = false;
    latched_loop_ = repeat; latched_gate_ = false;
    gate_deadline_ = UINT64_MAX; release_remaining_ = 0;
    win_start_ = start; win_end_ = end; inc_q16_ = window.increment;
    if (pitch_mode_ != PitchMode::Legacy && !scrub && params[P_SCRUB].effective <= 16383)
        inc_q16_ = musical_increment(reverse, speed_q8);
    playing_ = true; frac_ = 0; amp_q15_ = 0;
    pos_ = inc_q16_ >= 0 ? start : end - 1;
    visual_source_frame_ = (uint32_t)pos_;
    control_tick();
    visual_position_ = inc_q16_ >= 0 ? visual_start_ : visual_end_;
    visual_state_ = VisualPlaying | (inc_q16_ < 0 ? VisualReverse : 0) | (repeat ? VisualLoop : 0);
    if (scrub_) begin_scrub();
    return true;
}

void PlayUnit::trigger() {
    if (pitch_mode_ != PitchMode::Legacy && (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE)) { reset(); return; }
    const bool restore_preview = preview_ && !modern_;
    preview_ = false;
    preview_repeat_ = false;
    preview_scrub_forced_ = scrub_override_ = false;
    scrub_ = modern_ && params[P_SCRUB].effective > 16383;
    scrub_motion_ = 0;
    latched_edit_ = false;
    if (restore_preview) {
        window_latched_ = false;
        control_tick(); // restore legacy live window before its normal trigger
    }
    gate_deadline_ = UINT64_MAX; // a newer/manual voice owns its lifetime
    release_remaining_ = 0;
    if (modern_) {
        const Sample& s = SampleBank::instance().slot(sample_slot);
        const PlayWindow w = projected_window(params[P_START].effective, params[P_LEN].effective,
            params[P_SLICE].effective);
        latch_edit_window(w);
        window_latched_ = true;
        win_start_ = w.start; win_end_ = w.end; inc_q16_ = w.increment;
        latched_reverse_ = w.slice && (s.slice_reverse_mask & (1u << (w.slice - 1)));
        latched_slice_ = w.slice;
        rate_bound_ = w.slice && Sample::cut_speed(s.slice_speed_q8[w.slice - 1]) != 256;
        latched_loop_ = params[P_LOOP].effective > Q15_ONE / 2;
        latched_gate_ = play_mode::is_gate(params[P_MODE].effective);
        latched_gate_quarters_ = play_mode::gate_quarters(params[P_GATE].effective);
        if (!w.valid) { playing_ = false; visual_state_ = 0; return; }
    }
    if (pitch_mode_ != PitchMode::Legacy) control_tick();
    playing_ = true;
    frac_ = 0;
    amp_q15_ = 0;
    if (inc_q16_ >= 0) {
        pos_ = win_start_;
    } else {
        pos_ = (win_end_ > win_start_ + 1) ? (win_end_ - (latched_edit_ || pitch_mode_ != PitchMode::Legacy ? 1u : 2u)) : win_start_;
    }
    visual_source_frame_ = (uint32_t)pos_;
    if (scrub_) begin_scrub();
}

int64_t PlayUnit::scrub_target() const {
    if (win_end_ <= win_start_ + 1) return (int64_t)win_start_ << 16;
    const int position = std::max(0, (int)(scrub_override_ ? scrub_position_ : params[P_POSITION].effective));
    return ((int64_t)win_start_ << 16) +
        (int64_t)((uint64_t)(win_end_ - win_start_ - 1) * 65536u * (uint32_t)position / 32767u);
}
void PlayUnit::begin_scrub() {
    const int64_t phase = scrub_target();
    pos_ = phase >> 16; frac_ = (uint16_t)phase; scrub_motion_ = 0; hold_phase_q16_ = 0;
    visual_source_frame_ = (uint32_t)pos_;
}
bool PlayUnit::preview_scrub(q15 position, bool initialize) {
    if (!preview_ || !playing_ || !source_matches(voice_source_) || position < 0) return false;
    scrub_position_ = position; scrub_override_ = true;
    scrub_ = preview_scrub_forced_ = true;
    if (initialize) { begin_scrub(); amp_q15_ = 0; }
    return true;
}

// Position-driven head: 2ms one-pole at 1x, scaled by speed magnitude (cut rate
// included), capped at 16 source frames/output sample. Direction comes from the
// gesture, not the reverse bit. No free-running head or repeated grain retrigger.
void PlayUnit::process_scrub(q15* buf, int frames, const Sample& sample) {
    const uint32_t nf = sample.playable_frames();
    if (!playing_ || !source_matches(voice_source_) || win_end_ > nf ||
        win_start_ + 1 >= win_end_) {
        playing_ = false; visual_state_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15)); return;
    }
    if (modern_ && params[P_HOLD].effective > 16383) { process_hold(buf, frames, sample); return; }
    const int64_t target = scrub_target();
    const int64_t begin = (int64_t)win_start_ << 16, last = (int64_t)(win_end_ - 1) << 16;
    auto publish_position = [&]() { visual_position_ = (uint16_t)((uint64_t)visual_source_frame_ * 32767u / (nf - 1)); };
    const int32_t rate = inc_q16_ < 0 ? -inc_q16_ : inc_q16_;
    for (int i = 0; i < frames; ++i) {
        int64_t phase = std::max<int64_t>((int64_t)win_start_ << 16,
            std::min<int64_t>((int64_t)(win_end_ - 1) << 16, pos_ * 65536 + frac_));
        const int64_t delta = target - phase;
        int64_t step = ((delta / 64) * rate) / 65536;
        step = std::max<int64_t>(-16 * 65536, std::min<int64_t>(16 * 65536, step));
        if (!step || std::abs(delta) < 64) step = delta;
        if (step > 0) step = std::min(step, delta);
        if (step < 0) step = std::max(step, delta);
        phase += step;
        pos_ = phase >> 16; frac_ = (uint16_t)phase;
        const uint32_t a = (uint32_t)pos_, b = std::min(a + 1, win_end_ - 1);
        q15 av, bv;
        if (sample.channels == 2) {
            av = (q15)(((int32_t)sample.data[(size_t)a*2] + sample.data[(size_t)a*2+1]) >> 1);
            bv = (q15)(((int32_t)sample.data[(size_t)b*2] + sample.data[(size_t)b*2+1]) >> 1);
        } else { av = sample.data[a]; bv = sample.data[b]; }
        const int motion = (int)std::min<int64_t>(32767, std::abs(step) / 2);
        const int change = motion - scrub_motion_;
        scrub_motion_ = (q15)(scrub_motion_ + (change >= 0 ? (change + 15) / 16 : (change - 15) / 16));
        if (amp_q15_ < Q15_ONE) amp_q15_ = (q15)std::min(32767, (int)amp_q15_ + 1024);
        q31 gain = ((q31)params[P_GAIN].effective * amp_q15_) >> 15;
        gain = gain * scrub_motion_ >> 15;
        q31 shape = Q15_ONE;
        if (voice_edit_.fade_in && phase - begin < (int64_t)voice_edit_.fade_in * 65536)
            shape = (q31)((phase - begin) * Q15_ONE / ((int64_t)voice_edit_.fade_in * 65536));
        if (voice_edit_.fade_out && last - phase < (int64_t)voice_edit_.fade_out * 65536)
            shape = std::min(shape, (q31)((last - phase) * Q15_ONE / ((int64_t)voice_edit_.fade_out * 65536)));
        if (shape != Q15_ONE) gain = gain * shape >> 15;
        if (release_remaining_) gain = gain * release_remaining_ / 64;
        buf[i] = (q15)(((q31)lerp_q15(av, bv, frac_) * gain) >> 15);
        visual_source_frame_ = a;
        visual_state_ = VisualPlaying | (step < 0 ? VisualReverse : 0);
        visual_speed_ = (int16_t)std::max<int64_t>(-32767, std::min<int64_t>(32767, step / 8));
        if (release_remaining_ && !--release_remaining_) {
            playing_ = false; visual_state_ = 0; publish_position();
            std::memset(buf+i+1, 0, (size_t)(frames-i-1)*sizeof(q15)); return;
        }
    }
    publish_position();
}

// Hold is a local, continuously moving 50ms source window, not a frozen buffer.
// Two heads half a cycle apart have complementary triangle weights: the wrapping
// head is exactly silent at its seam. Reads and source fades stay inside the
// accepted trim/cut, including two-frame sources. No allocation or grain restart.
void PlayUnit::process_hold(q15* buf, int frames, const Sample& sample) {
    const uint32_t nf = sample.playable_frames(); // process_scrub already validated binding/bounds
    const int64_t begin = (int64_t)win_start_ << 16, last = (int64_t)(win_end_ - 1) << 16;
    const int64_t target = scrub_target();
    const uint32_t period = std::min<uint32_t>(win_end_ - win_start_ - 1, ENGINE_SR / 20) << 16;
    const uint32_t half = period / 2;
    const int32_t rate = inc_q16_ < 0 ? -inc_q16_ : inc_q16_;
    auto read = [&](int64_t phase) -> q15 {
        const uint32_t a = (uint32_t)(phase >> 16), b = std::min(a + 1, win_end_ - 1);
        q15 av, bv;
        if (sample.channels == 2) {
            av = (q15)(((int32_t)sample.data[(size_t)a*2] + sample.data[(size_t)a*2+1]) >> 1);
            bv = (q15)(((int32_t)sample.data[(size_t)b*2] + sample.data[(size_t)b*2+1]) >> 1);
        } else { av = sample.data[a]; bv = sample.data[b]; }
        q31 value = lerp_q15(av, bv, (uint16_t)phase), shape = Q15_ONE;
        if (voice_edit_.fade_in && phase - begin < (int64_t)voice_edit_.fade_in * 65536)
            shape = (q31)((phase - begin) * Q15_ONE / ((int64_t)voice_edit_.fade_in * 65536));
        if (voice_edit_.fade_out && last - phase < (int64_t)voice_edit_.fade_out * 65536)
            shape = std::min(shape, (q31)((last - phase) * Q15_ONE / ((int64_t)voice_edit_.fade_out * 65536)));
        return (q15)(shape == Q15_ONE ? value : value * shape >> 15);
    };
    for (int i = 0; i < frames; ++i) {
        int64_t center = std::max(begin, std::min(last, pos_ * 65536 + frac_));
        const int64_t delta = target - center;
        int64_t step = std::max<int64_t>(-16 * 65536, std::min<int64_t>(16 * 65536, ((delta / 64) * rate) / 65536));
        if (!step || std::abs(delta) < 64) step = delta;
        if (step > 0) step = std::min(step, delta);
        if (step < 0) step = std::max(step, delta);
        center += step;
        pos_ = center >> 16; frac_ = (uint16_t)center;
        const int64_t window = std::max(begin, std::min(last - period, center - half));
        const uint32_t a = hold_phase_q16_, b = a < half ? a + half : a - half;
        const uint32_t triangle = a <= half ? a : period - a;
        const int weight = (int)((uint64_t)triangle * 32768 / half);
        const int value = ((int)read(window + a) * weight + (int)read(window + b) * (32768 - weight)) >> 15;
        if (amp_q15_ < Q15_ONE) amp_q15_ = (q15)std::min(32767, (int)amp_q15_ + 1024);
        q31 gain = ((q31)params[P_GAIN].effective * amp_q15_) >> 15;
        if (release_remaining_) gain = gain * release_remaining_ / 64;
        buf[i] = (q15)(value * gain >> 15);
        int32_t next = ((int32_t)a + inc_q16_) % (int32_t)period;
        if (next < 0) next += period;
        hold_phase_q16_ = (uint32_t)next;
        // Publish the smoothly followed cursor, not one arbitrary overlap head.
        visual_source_frame_ = (uint32_t)pos_;
        visual_position_ = (uint16_t)((uint64_t)visual_source_frame_ * 32767u / (nf - 1));
        visual_state_ = VisualPlaying | VisualLoop | (inc_q16_ < 0 ? VisualReverse : 0);
        visual_speed_ = (int16_t)std::max<int32_t>(-32767, std::min<int32_t>(32767, inc_q16_ / 8));
        if (release_remaining_ && !--release_remaining_) {
            playing_ = false; visual_state_ = 0;
            std::memset(buf+i+1, 0, (size_t)(frames-i-1)*sizeof(q15)); return;
        }
    }
}

void PlayUnit::process(q15* buf, int frames) {
    if (pitch_mode_ != PitchMode::Legacy && (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE)) {
        playing_ = false; visual_state_ = 0; stretch_.reset();
        std::memset(buf, 0, (size_t)frames * sizeof(q15)); return;
    }
    const Sample& s = SampleBank::instance().slot(sample_slot);
    if (scrub_) { process_scrub(buf, frames, s); return; }
    if (pitch_mode_ != PitchMode::Legacy) { process_musical(buf, frames, s); return; }
    if (latched_edit_) { process_edit(buf, frames, s); return; }
    if (rate_bound_ && !source_matches(voice_source_)) {
        playing_ = false; visual_state_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15)); return;
    }
    const uint32_t nf = playable_frames(s);
    // A slot switch/replacement can happen between control ticks. Revalidate the
    // cached window against the new vector before any pos+1 interpolation read.
    if (nf < 2 || win_end_ > nf || win_start_ + 1 >= win_end_) {
        playing_ = false;
        visual_state_ = 0;
        visual_position_ = visual_start_ = visual_end_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }
    if (!playing_) {
        visual_state_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }

    const bool loop = !preview_ && (modern_ ? latched_loop_ : params[P_LOOP].effective > (Q15_ONE / 2));
    const q15 gain  = params[P_GAIN].effective;
    const int ch    = s.channels;

    for (int i = 0; i < frames; ++i) {
        if (pos_ < (int64_t)win_start_ || pos_ + 1 >= (int64_t)win_end_) {
            if (loop) {
                pos_ = (inc_q16_ >= 0) ? win_start_ : ((win_end_ > win_start_ + 1) ? (win_end_ - 2) : win_start_);
                if (pos_ < 0) pos_ = 0;
                frac_ = 0;
                amp_q15_ = 0;        // soften the discontinuity at the loop seam
            } else {
                playing_ = false;
                visual_state_ = 0;
                visual_position_ = (uint16_t)((uint64_t)win_end_ * 32767u / nf);
                std::memset(buf + i, 0, (size_t)(frames - i) * sizeof(q15));
                return;
            }
        }
        // mono-ize stereo on the fly
        q15 a, b;
        if (ch == 2) {
            a = (q15)(((int32_t)s.data[pos_ * 2] + s.data[pos_ * 2 + 1]) >> 1);
            b = (q15)(((int32_t)s.data[(pos_ + 1) * 2] + s.data[(pos_ + 1) * 2 + 1]) >> 1);
        } else {
            a = s.data[(size_t)pos_];
            b = s.data[(size_t)pos_ + 1];
        }
        q15 out = lerp_q15(a, b, frac_);

        // 64-sample edge envelope plus trigger/loop ramp. cheap insurance against clicks.
        constexpr int FADE_FRAMES = 64;
        q15 eg = Q15_ONE;
        int64_t ds = pos_ - (int64_t)win_start_;
        int64_t de = (int64_t)win_end_ - 2 - pos_;
        if (ds < FADE_FRAMES) {
            if (ds < 0) ds = 0;
            eg = (q15)((ds * Q15_ONE) / FADE_FRAMES);
        }
        if (de < FADE_FRAMES) {
            if (de < 0) de = 0;
            q15 fg = (q15)((de * Q15_ONE) / FADE_FRAMES);
            if (fg < eg) eg = fg;
        }
        if (amp_q15_ < Q15_ONE) {
            int ramp = (int)amp_q15_ + 1024;
            amp_q15_ = (q15)(ramp > Q15_ONE ? Q15_ONE : ramp);
        }
        q31 g = ((q31)gain * eg) >> 15;
        g = (g * amp_q15_) >> 15;
        if (release_remaining_) g = g * release_remaining_ / 64;
        buf[i] = (q15)(((int32_t)out * g) >> 15);
        if (release_remaining_ && !--release_remaining_) {
            playing_ = false;
            visual_state_ = 0;
            std::memset(buf + i + 1, 0, (size_t)(frames - i - 1) * sizeof(q15));
            return;
        }

        // advance
        int64_t adv = (int64_t)frac_ + inc_q16_;
        pos_ += adv >> 16;
        frac_ = (trackr::fx::uq16)(adv & 0xFFFF);
    }
    uint32_t shown = pos_ < 0 ? 0u : pos_ >= (int64_t)nf ? nf - 1 : (uint32_t)pos_;
    visual_source_frame_ = shown;
    visual_position_ = (uint16_t)((uint64_t)shown * 32767u / (nf - 1));
    visual_start_ = (uint16_t)((uint64_t)win_start_ * 32767u / (nf - 1));
    visual_end_ = (uint16_t)((uint64_t)(win_end_ - 1) * 32767u / (nf - 1));
    const int16_t speed_magnitude = (int16_t)(params[P_SPEED].effective < 0
        ? -params[P_SPEED].effective : params[P_SPEED].effective);
    visual_speed_ = inc_q16_ < 0 ? (int16_t)-speed_magnitude : speed_magnitude;
    visual_state_ = VisualPlaying | (inc_q16_ < 0 ? VisualReverse : 0) |
                    (loop ? VisualLoop : 0);
}

// Exact editor path is deliberately separate from the legacy/modern PCM path.
// One primary interpolated head plus one seam head, constant work per sample.
void PlayUnit::process_edit(q15* buf, int frames, const Sample& sample) {
    const uint32_t nf = sample.playable_frames();
    if (!playing_ || !source_matches(voice_source_) || !voice_edit_.valid ||
        win_end_ > nf || win_start_ >= win_end_) {
        playing_ = false; visual_state_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }
    const auto& w = voice_edit_;
    const bool loop = (preview_ ? preview_repeat_ : latched_loop_) &&
        w.loop_start < w.loop_end && w.loop_end - w.loop_start >= 2;
    const int64_t lo = (int64_t)w.loop_start << 16;
    const int64_t hi = (int64_t)w.loop_end << 16;
    const int64_t cross = (int64_t)w.crossfade << 16;
    const int64_t period = hi - lo - cross;
    const int64_t begin = (int64_t)win_start_ << 16;
    const int64_t end = (int64_t)win_end_ << 16;
    const q15 gain = params[P_GAIN].effective;
    auto publish_position = [&]() {
        visual_position_ = (uint16_t)((uint64_t)visual_source_frame_ * 32767u / (nf - 1));
    };
    auto read_at = [&](int64_t phase) -> q15 {
        // Clamp the interpolation partner, not the musical window: the last
        // authored frame remains playable, including 2-frame reverse selections.
        uint32_t a = play_edit_clamp(phase >> 16, win_start_, win_end_ - 1);
        uint32_t b = a + 1 < win_end_ ? a + 1 : a;
        q15 av, bv;
        if (sample.channels == 2) {
            av = (q15)(((int32_t)sample.data[(size_t)a * 2] + sample.data[(size_t)a * 2 + 1]) >> 1);
            bv = (q15)(((int32_t)sample.data[(size_t)b * 2] + sample.data[(size_t)b * 2 + 1]) >> 1);
        } else { av = sample.data[a]; bv = sample.data[b]; }
        return lerp_q15(av, bv, (trackr::fx::uq16)(phase & 0xffff));
    };
    for (int i = 0; i < frames; ++i) {
        int64_t phase = pos_ * 65536 + frac_;
        // First pass starts at trim, not loop-in: retain the forward attack or
        // reverse lead-in. After wrap, preserve fractional/high-speed overshoot.
        if (loop && inc_q16_ >= 0 && phase >= hi) {
            phase = lo + cross + (phase - hi) % period;
            loop_entered_ = true;
        } else if (loop && inc_q16_ < 0 && phase < lo) {
            phase = hi - cross - 1 - (lo - 1 - phase) % period;
            loop_entered_ = true;
        }
        if (phase < begin || phase >= end) {
            playing_ = false; visual_state_ = 0;
            publish_position();
            std::memset(buf + i, 0, (size_t)(frames - i) * sizeof(q15));
            return;
        }
        q15 out = read_at(phase);
        if (loop && cross > 0) {
            int64_t partner = 0, weight = 0;
            if (inc_q16_ >= 0 && phase >= hi - cross && phase < hi) {
                weight = phase - (hi - cross);
                partner = lo + weight;
            } else if (inc_q16_ < 0 && phase >= lo && phase < lo + cross) {
                weight = lo + cross - phase;
                partner = hi - (lo + cross - phase);
            }
            if (weight > 0) {
                const int32_t mix = (int32_t)(weight * 32767 / cross);
                out = (q15)(((int64_t)out * (32767 - mix) + (int64_t)read_at(partner) * mix) / 32767);
            }
        }
        q15 eg = Q15_ONE;
        const int64_t ds = phase - begin;
        const int64_t de = end - 65536 - phase;
        // Source-domain fades are swapped naturally by reverse. Musical loop
        // sustains after its first attack, rather than re-fading every seam.
        const bool selection_preview = preview_ && !preview_seam_;
        if (w.fade_in && (selection_preview || !loop || (!loop_entered_ && inc_q16_ >= 0))) {
            const int64_t width = (int64_t)w.fade_in << 16;
            if (ds < width) eg = (q15)(std::max<int64_t>(0, ds) * Q15_ONE / width);
        }
        if (w.fade_out && (selection_preview || !loop || (!loop_entered_ && inc_q16_ < 0))) {
            const int64_t width = (int64_t)w.fade_out << 16;
            if (de < width) eg = std::min<q15>(eg, (q15)(std::max<int64_t>(0, de) * Q15_ONE / width));
        }
        if (amp_q15_ < Q15_ONE) amp_q15_ = (q15)std::min<int>(Q15_ONE, amp_q15_ + 1024);
        q31 g = ((q31)gain * eg) >> 15;
        g = (g * amp_q15_) >> 15;
        if (release_remaining_) g = g * release_remaining_ / 64;
        buf[i] = (q15)(((int32_t)out * g) >> 15);
        visual_source_frame_ = (uint32_t)(phase >> 16);
        if (release_remaining_ && !--release_remaining_) {
            playing_ = false; visual_state_ = 0;
            publish_position();
            std::memset(buf + i + 1, 0, (size_t)(frames - i - 1) * sizeof(q15));
            return;
        }
        phase += inc_q16_;
        pos_ = phase >> 16; frac_ = (trackr::fx::uq16)(phase & 0xffff);
    }
    publish_position();
    visual_start_ = (uint16_t)((uint64_t)win_start_ * 32767u / (nf - 1));
    visual_end_ = (uint16_t)((uint64_t)(win_end_ - 1) * 32767u / (nf - 1));
    const int16_t magnitude = (int16_t)(params[P_SPEED].effective < 0
        ? -params[P_SPEED].effective : params[P_SPEED].effective);
    visual_speed_ = inc_q16_ < 0 ? (int16_t)-magnitude : magnitude;
    visual_state_ = VisualPlaying | (inc_q16_ < 0 ? VisualReverse : 0) | (loop ? VisualLoop : 0);
}

#include "play_musical_impl.inc"

// ============================ GRAIN ============================
// Ambient-class cloud: up to eight concurrent grains with an independent
// scheduler. Each grain gets its own window position, per-grain pitch deviation,
// and attack/release ramp. Voice stealing routes the victim into a short declick
// tail so raising density never produces a click train.

GrainUnit::GrainUnit() {
    // PDEV reuses the shared semitone LUT. It must be built here too: a patch may
    // contain a GRAN and no STRN, and an unbuilt table returns ratio 0, which used
    // to collapse every grain to 1/64 speed.
    build_strn_pitch_lut();
    kind = UnitKind::Grain;
    add_param("POS",   0);
    add_param("SIZE",  8192);          // -> 30ms..500ms
    add_param("SPRY",  4096);
    add_param("PTCH",  16384, true);   // like PLAY speed
    add_param("GAIN",  Q15_ONE);
    // Cloud scheduler. These append into free ParamBank slots, so every existing
    // sequencer lock, scene, and SNC7 file keeps addressing the same five knobs.
    add_param("DENS",  16384);         // spawn rate: 8000..320 frames
    add_param("JIT",   0);             // spawn timing scatter, 0..100% of period
    add_param("ATK",   4096);          // 0..50% of grain length
    add_param("REL",   8192);          // 0..50% of grain length
    add_param("PDEV",  0);             // per-grain random pitch spread, 0..12st
    add_param("VOX",   Q15_ONE);       // active voice ceiling, 1..8
    add_param("SCAN",  0, true);       // clean only: independent signed source scan
}

uint8_t GrainUnit::voice_limit(q15 value) {
    const uint32_t v = (uint32_t)(uint16_t)value;
    uint32_t count = 1u + (v * (uint32_t)(MAX_GRAINS - 1) + 16383u) / 32767u;
    if (count < 1) count = 1;
    if (count > (uint32_t)MAX_GRAINS) count = (uint32_t)MAX_GRAINS;
    return (uint8_t)count;
}

uint32_t GrainUnit::grain_length_frames(q15 size) {
    // 30 ms .. 500 ms at 32 kHz. Divide by 32767 rather than shifting by 15 so
    // full scale reaches exactly 16000 frames instead of stopping one frame short.
    return 960u + (uint32_t)(((uint64_t)(uint16_t)size * 15040u + 16383u) / 32767u);
}

uint32_t GrainUnit::spawn_interval_frames(q15 density) {
    // DENS is a rate, so it must map inversely to a period. Minimum 320 frames
    // (100 grains/s) bounds worst-case scheduler cost on Old 3DS.
    const uint32_t v = (uint32_t)(uint16_t)density;
    const uint32_t span = 8000u - 320u;
    return 8000u - (uint32_t)(((uint64_t)v * span + 16383u) / 32767u);
}

int32_t GrainUnit::pdev_increment(int32_t base_increment, q15 deviation_sample) {
    const int32_t ratio = semitone_inc_q16(deviation_sample);
    int64_t scaled = ((int64_t)base_increment * ratio) / 65536;
    if (scaled > 262136) scaled = 262136;
    if (scaled < -262136) scaled = -262136;
    int32_t result = (int32_t)scaled;
    if (result >= 0 && result < 1024) result = 1024;
    if (result < 0 && result > -1024) result = -1024;
    return result;
}

int32_t GrainUnit::scan_increment_q16(q15 value) {
    const int32_t v = value < -32767 ? -32767 : value;
    return (int32_t)((int64_t)v * 262144 / 32767);
}

bool GrainUnit::scan_source_matches() const {
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE ||
        sample_slot != scan_source_slot_) return false;
    const auto& bank = SampleBank::instance();
    const Sample& s = bank.slot(sample_slot);
    return bank.generation(sample_slot) == scan_source_generation_ &&
        s.data.data() == scan_source_data_ && s.playable_frames() == scan_source_frames_ &&
        s.channels == scan_source_channels_;
}

int64_t GrainUnit::scan_center_q16(uint32_t nf) const {
    const int32_t pos = params[P_POS].effective < 0 ? 0 : params[P_POS].effective;
    int64_t center = (int64_t)pos * nf * 2 + scan_offset_q16_;
    const int64_t span = (int64_t)nf * 65536;
    if (center >= span) center -= span;
    return center;
}

q15 GrainUnit::scan_position() const {
    const q15 anchor = params[P_POS].effective < 0 ? 0 : params[P_POS].effective;
    if (!clean || !scan_source_matches() || !scan_source_frames_) return anchor;
    // Conversion is UI-rate only, not part of sample-clock advancement.
    return (q15)(scan_center_q16(scan_source_frames_) / ((int64_t)scan_source_frames_ * 2));
}

// Rescale the unconsumed period only; retain a 16-bit residue so 1 kHz
// modulation cannot accumulate a one-sample truncation on every update.
static void rescale_clock_countdown(uint32_t& countdown, uint16_t& residue,
                                    uint32_t before, uint32_t after) {
    if (!countdown || !before) { residue = 0; return; }
    const uint64_t remaining = ((uint64_t)countdown * 65536 + residue) * after / before;
    countdown = (uint32_t)(remaining >> 16);
    residue = (uint16_t)remaining;
}
void GrainUnit::set_transport_bpm_q16(int32_t bpm_q16) {
    if (bpm_q16 < 20 * 65536) bpm_q16 = 20 * 65536;
    if (bpm_q16 > 300 * 65536) bpm_q16 = 300 * 65536;
    const uint8_t d = sync_division > 3 ? 3 : sync_division;
    if (clock_bpm_q16_ == bpm_q16 && clock_division_ == d) return;
    clock_bpm_q16_ = bpm_q16;
    clock_bpm_ = 0;
    clock_division_ = d;
    static const uint8_t denominator[4] = {8,16,32,64};
    const uint32_t before = sync_interval_frames;
    sync_interval_frames = (uint32_t)((uint64_t)ENGINE_SR * 240u * 65536u /
        ((uint64_t)(uint32_t)bpm_q16 * denominator[d]));
    if (sync_interval_frames < 320) sync_interval_frames = 320;
    if (clock_sync && before != sync_interval_frames)
        rescale_clock_countdown(spawn_countdown_, clock_countdown_fraction_, before, sync_interval_frames);
}
void GrainUnit::set_transport_bpm(int bpm) {
    clock_bpm_q16_ = 0;
    clock_countdown_fraction_ = 0;
    if (bpm < 20) bpm = 20;
    if (bpm > 300) bpm = 300;
    const uint8_t d = sync_division > 3 ? 3 : sync_division;
    if (clock_bpm_ == (int16_t)bpm && clock_division_ == d) return;
    clock_bpm_ = (int16_t)bpm;
    clock_division_ = d;
    static const uint8_t denominator[4] = { 8, 16, 32, 64 };
    // frames per (1/denominator) note = SR * 240 / (bpm * denominator)
    sync_interval_frames = (uint32_t)(((uint64_t)ENGINE_SR * 240u) /
                                      ((uint32_t)bpm * denominator[d]));
    if (sync_interval_frames < 320) sync_interval_frames = 320;
}

int32_t GrainUnit::rand_signed(int32_t amplitude) {
    if (amplitude <= 0) return 0;
    return (int32_t)(rand_u32() % (uint32_t)(amplitude * 2 + 1)) - amplitude;
}

void GrainUnit::control_tick() {
    synchronize_performance();
    int32_t v = params[P_PITCH].effective;
    bool rev = v < 0;
    if (rev) v = -v;
    inc_q16_ = v * 8;
    if (inc_q16_ < 1024) inc_q16_ = 1024;
    if (rev) inc_q16_ = -inc_q16_;
    // Generic integer slew can settle 1..3 raw units short of zero. SCAN's zero
    // is a real hold, not a creeping minimum rate; do not quantize tiny authored
    // nonzero targets, or change historical parameter resolution.
    if (clean && params[P_SCAN].target == 0 && params[P_SCAN].effective > -4 &&
        params[P_SCAN].effective < 4) params[P_SCAN].effective = 0;
    scan_inc_q16_ = clean ? scan_increment_q16(params[P_SCAN].effective) : 0;
}

void GrainUnit::clear_launch_requests() {
    pad_token_ = 0;
    pending_launches_ = 0;
    sequence_burst_ = false;
    gate_deadline_ = UINT64_MAX;
    gate_phase_ = false;
}

void GrainUnit::synchronize_performance() {
    if (!clean || (uint8_t)launch_mode > (uint8_t)LaunchMode::Burst)
        launch_mode = LaunchMode::Flow;
    if (!clean || (uint8_t)window > (uint8_t)Window::Hann) window = Window::Linear;
    set_burst_count(burst_count);
    if (runtime_clean_ != clean || runtime_launch_mode_ != launch_mode) {
        clear_launch_requests();
        runtime_clean_ = clean;
        runtime_launch_mode_ = launch_mode;
        spawn_countdown_ = 0;
        clock_countdown_fraction_ = 0;
    }
}

void GrainUnit::bind_performance_source() {
    if (!clean || scan_source_matches()) return;
    // Bind BEFORE installing a new request, so first process cannot erase it.
    reset();
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return;
    const auto& bank = SampleBank::instance();
    const auto& sample = bank.slot(sample_slot);
    scan_source_slot_ = sample_slot;
    scan_source_generation_ = bank.generation(sample_slot);
    scan_source_frames_ = sample.playable_frames();
    scan_source_data_ = sample.data.data();
    scan_source_channels_ = sample.channels;
}

void GrainUnit::set_launch_mode(LaunchMode value) {
    launch_mode = clean && (uint8_t)value <= (uint8_t)LaunchMode::Burst ? value : LaunchMode::Flow;
    clear_launch_requests();
    synchronize_performance();
}
void GrainUnit::set_window(Window value) {
    window = clean && value == Window::Hann ? Window::Hann : Window::Linear;
}
void GrainUnit::set_burst_count(uint8_t value) {
    burst_count = value < 1 ? 1 : value > 16 ? 16 : value;
}

bool GrainUnit::gate_open() const {
    return clean && !bypass && runtime_clean_ && launch_mode == LaunchMode::Gate &&
        runtime_launch_mode_ == launch_mode && scan_source_matches() &&
        (pad_token_ || gate_deadline_ != UINT64_MAX);
}
uint8_t GrainUnit::burst_remaining() const {
    return clean && !bypass && runtime_clean_ && launch_mode == LaunchMode::Burst &&
        runtime_launch_mode_ == launch_mode && scan_source_matches() ? pending_launches_ : 0;
}
uint32_t GrainUnit::pad_begin() {
    synchronize_performance();
    bind_performance_source();
    if (bypass) { clear_launch_requests(); return 0; }
    trigger();
    // Issued at gesture entry under the shared audio guard, not per object.
    // Destroy/reallocate/clone at the same address must not reuse token 1.
    // Constant initialization, no allocation or work in the sample loop.
    static uint32_t issued = 0;
    if (++issued == 0) ++issued;
    pad_token_ = issued;
    if (clean && launch_mode == LaunchMode::Gate) pending_launches_ = 0;
    return pad_token_;
}
void GrainUnit::pad_end(uint32_t token) {
    if (token && token == pad_token_) pad_token_ = 0;
}
void GrainUnit::release_sequence_gate() {
    gate_deadline_ = UINT64_MAX;
    gate_phase_ = false;
    if (sequence_burst_) pending_launches_ = 0;
    sequence_burst_ = false;
}
void GrainUnit::begin_sequence_gate(uint64_t deadline, bool phase) {
    // trigger_chain already rewound/spawned the onset request. Do not trigger twice.
    synchronize_performance();
    if (!clean || bypass || launch_mode == LaunchMode::Flow) return;
    gate_deadline_ = launch_mode == LaunchMode::Gate ? deadline : UINT64_MAX;
    gate_phase_ = phase;
    if (launch_mode == LaunchMode::Gate) pending_launches_ = 0;
    else sequence_burst_ = true;
}

void GrainUnit::trigger() {
    synchronize_performance();
    bind_performance_source();
    if (clean && bypass && launch_mode != LaunchMode::Flow) { clear_launch_requests(); return; }
    // A/sigil/live-roll are onsets, never an invented indefinite held gate.
    // Manual onsets leave independent pad/sequence permission untouched.
    if (clean && launch_mode != LaunchMode::Flow) {
        pending_launches_ = launch_mode == LaunchMode::Burst ? burst_count : 1;
        sequence_burst_ = false;
    }
    spawn_countdown_ = 0;
    clock_countdown_fraction_ = 0;
    scan_offset_q16_ = 0; // return to POS; existing grains finish their latched paths
}

void GrainUnit::reset() {
    clear_launch_requests(); // process-wide token issuance is not reset or cloned
    for (auto& head : heads_) head = Head{};
    for (auto& tail : tails_) tail = Tail{};
    scan_offset_q16_ = 0;
    scan_source_slot_ = -1;
    scan_source_data_ = nullptr;
    scan_source_frames_ = 0;
    next_head_ = 0;
    spawn_countdown_ = 0;
    clock_countdown_fraction_ = 0;
    visual_grains_ = 0;
    visual_active_ = 0;
    visual_interval_ = 0;
    visual_spawned_ = 0;
    visual_longest_ = 0;
    for (auto& position : visual_positions_) position = 0;
    for (auto& phase : visual_phases_) phase = 0;
    for (auto& offset : visual_pitch_offsets_) offset = 0;
}

void GrainUnit::retire_into_tail(const Head& head) {
    if (!head.active) return;
    // Prefer a free tail; otherwise steal the one closest to finishing so the
    // audible artifact stays as short as possible.
    int selected = 0;
    uint32_t shortest = 0xffffffffu;
    for (int index = 0; index < MAX_TAILS; ++index) {
        if (!tails_[index].head.active || tails_[index].remaining == 0) {
            selected = index;
            shortest = 0;
            break;
        }
        if (tails_[index].remaining < shortest) {
            selected = index;
            shortest = tails_[index].remaining;
        }
    }
    tails_[selected].head = head;
    tails_[selected].remaining = DECLICK_FRAMES;
}

int GrainUnit::select_head(int limit) {
    if (limit < 1) limit = 1;
    if (limit > MAX_GRAINS) limit = MAX_GRAINS;
    const int first = next_head_ % limit;
    for (int offset = 0; offset < limit; ++offset) {
        const int index = (first + offset) % limit;
        if (!heads_[index].active) {
            next_head_ = (index + 1) % limit;
            return index;
        }
    }
    // Everything is busy: replace the grain with the least remaining audio.
    int best = first;
    uint32_t shortest = 0xffffffffu;
    for (int index = 0; index < limit; ++index) {
        const Head& head = heads_[index];
        const uint32_t remaining = head.length > head.age ? head.length - head.age : 0;
        if (remaining < shortest) { best = index; shortest = remaining; }
    }
    next_head_ = (best + 1) % limit;
    return best;
}

void GrainUnit::spawn_grain(uint32_t nf, int limit) {
    if (nf < 256) return;

    uint32_t len = grain_length_frames(params[P_SIZE].effective);

    // Position: POS center plus symmetric spray, both in the sample domain.
    const uint32_t center = clean ? (uint32_t)(scan_center_q16(nf) >> 16) :
        (uint32_t)(((uint64_t)(uint16_t)params[P_POS].effective * nf) >> 15);
    const uint32_t spray = (uint32_t)(((uint64_t)(uint16_t)params[P_SPRAY].effective * nf) >> 16);
    int64_t start = (int64_t)center + rand_signed((int32_t)spray);

    // Per-grain pitch: PTCH increment scaled by a random PDEV ratio up to +/-12
    // semitones. Reuse the exact cached semitone LUT instead of inventing a
    // second, less accurate pitch path.
    int32_t increment = inc_q16_;
    int8_t pitch_offset = 0;
    const int32_t deviation = params[P_PITCH_DEV].effective;
    if (deviation > 0) {
        // PDEV full scale is +/-12 st, i.e. half of the LUT's +/-24 st domain.
        const int32_t spread = (int32_t)(((int64_t)deviation * 16383) / 32767);
        const int32_t offset = rand_signed(spread);
        increment = pdev_increment(increment, (q15)offset);
        pitch_offset = (int8_t)((int64_t)offset * 24 / 32767);
    }

    // A grain consumes length * |increment| frames of source material. Clamp the
    // window so interpolation never reads past the sample, including at 4x speed.
    // This may only ever SHORTEN the grain: deriving length from the available
    // span previously inflated a slow grain to tens of seconds, wedging all eight
    // voices until the unit was deleted.
    const uint32_t magnitude = (uint32_t)(increment < 0 ? -increment : increment);
    if (magnitude) {
        const uint64_t usable = nf > 3 ? (uint64_t)(nf - 3) : 0;
        const uint32_t max_len = (uint32_t)((usable << 16) / magnitude);
        if (len > max_len) len = max_len;
    }
    if (len < 2) len = 2;
    // Hard ceiling: SIZE's own maximum. No derived value may exceed the documented
    // 500 ms grain, whatever the pitch or sample length.
    const uint32_t len_ceiling = grain_length_frames(32767);
    if (len > len_ceiling) len = len_ceiling;

    uint64_t span = ((uint64_t)len * magnitude) >> 16;
    if (span + 3 >= nf) span = nf > 3 ? nf - 3 : 0;
    if (clean) {
        // The last rendered source location, not an imaginary forward window.
        // A reverse grain at POS=0 moves its start right just enough to finish.
        const int64_t travel = ((uint64_t)(len - 1) * magnitude + 65535u) >> 16;
        const int64_t lo = increment < 0 ? travel : 0;
        const int64_t hi = (int64_t)nf - 2 - (increment < 0 ? 0 : travel);
        if (start < lo) start = lo;
        if (start > hi) start = hi;
    } else {
        // Frozen historical left-edge interpretation, including reverse behavior.
        if (start < 0) start = 0;
        if (start + (int64_t)span + 2 >= (int64_t)nf) start = (int64_t)nf - (int64_t)span - 3;
        if (start < 0) start = 0;
    }

    const int index = select_head(limit);
    retire_into_tail(heads_[index]);
    Head& h = heads_[index];
    h = Head{};
    h.active = true;
    h.pos = start;
    h.start = (uint32_t)start;
    h.frac = 0;
    h.inc_q16 = increment;
    h.length = len;
    h.age = 0;
    // ATK/REL are percentages of the grain, each bounded to 50% so the two ramps
    // can never overlap and fight for the same frames.
    h.attack = (uint32_t)(((uint64_t)(uint16_t)params[P_ATTACK].effective * len) >> 16);
    h.release = (uint32_t)(((uint64_t)(uint16_t)params[P_RELEASE].effective * len) >> 16);
    h.shape = clean ? window : Window::Linear;
    h.hann_step = h.shape == Window::Hann ? (uint32_t)((512ull << 22) / (len - 1)) : 0;
    h.window_shift = clean ? 16 : 0;
    const uint32_t numerator = 32767u << h.window_shift;
    // Ceil q16 slope: at <=7999 ramp frames, ideal envelope error stays <1 q15.
    // Products remain below 2^31; no variable division in the render loop.
    h.attack_step = h.attack ? (int32_t)((numerator + (clean ? h.attack - 1 : 0)) / h.attack) : 0;
    h.release_step = h.release ? (int32_t)((numerator + (clean ? h.release - 1 : 0)) / h.release) : 0;
    h.pitch_offset = pitch_offset;
    ++visual_spawned_;
    if (h.length > visual_longest_) visual_longest_ =
        (uint16_t)(h.length > 0xffffu ? 0xffffu : h.length);
}

namespace {
// Shared window/read helper. Grains and declick tails must use identical math,
// otherwise a stolen voice audibly jumps at the moment it is retired.
inline q15 grain_window_q15(uint32_t age, uint32_t length, uint32_t attack,
                            uint32_t release, int32_t attack_step,
                            int32_t release_step, uint8_t window_shift,
                            GrainUnit::Window shape, uint32_t hann_step) {
    if (shape == GrainUnit::Window::Hann) return grain_window::hann(age, length, hann_step);
    int32_t window = Q15_ONE;
    if (attack && age < attack) {
        const int32_t rising = (int32_t)(((uint32_t)age * attack_step) >> window_shift);
        if (rising < window) window = rising;
    }
    const uint32_t from_end = length > age ? length - 1 - age : 0;
    if (release && from_end < release) {
        const int32_t falling = (int32_t)((from_end * release_step) >> window_shift);
        if (falling < window) window = falling;
    }
    if (window < 0) window = 0;
    if (window > Q15_ONE) window = Q15_ONE;
    return (q15)window;
}
} // namespace

q15 GrainUnit::visual_window_q15(uint32_t age, uint32_t length, uint32_t attack,
                                  uint32_t release, Window shape) {
    if (length < 2 || age >= length) return 0;
    if (shape == Window::Hann)
        return grain_window::hann(age, length, (uint32_t)((512ull << 22) / (length - 1)));
    // Visual callers use latched valid grain dimensions; bound arbitrary inputs too.
    if (attack > 16000) attack = 16000;
    if (release > 16000) release = 16000;
    const uint32_t numerator = 32767u << 16;
    const int32_t a = attack ? (int32_t)((numerator + attack - 1) / attack) : 0;
    const int32_t r = release ? (int32_t)((numerator + release - 1) / release) : 0;
    return grain_window_q15(age, length, attack, release, a, r, 16, Window::Linear, 0);
}

void GrainUnit::capture_visual_grains(VisualGrain (&out)[MAX_GRAINS]) const {
    for (auto& grain : out) grain = VisualGrain{};
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return;
    const Sample& sample = SampleBank::instance().slot(sample_slot);
    const uint32_t nf = playable_frames(sample);
    if (!nf || (clean && !scan_source_matches())) return;
    for (int index = 0; index < MAX_GRAINS; ++index) {
        const Head& head = heads_[index];
        if (!head.active || head.age >= head.length || head.pos < 0 ||
            head.pos + 1 >= (int64_t)nf) continue;
        VisualGrain& grain = out[index];
        grain.active = true;
        // Reverse still uses floor(position) + positive fraction, exactly as
        // process advances it. Do not reinterpret h.start as a reverse right edge.
        grain.position_q16 = head.pos * 65536 + (int64_t)head.frac;
        grain.start_q16 = grain.position_q16 - (int64_t)head.age * head.inc_q16;
        grain.end_q16 = grain.start_q16 + (int64_t)head.length * head.inc_q16;
        grain.age = head.age;
        grain.length = head.length;
        grain.attack = head.attack;
        grain.release = head.release;
        grain.window = head.shape;
        grain.envelope = grain_window_q15(head.age, head.length, head.attack,
                                          head.release, head.attack_step, head.release_step,
                                          head.window_shift, head.shape, head.hann_step);
    }
}

void GrainUnit::process(q15* buf, int frames) {
    if (frames <= 0) return;
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) {
        if (clean) reset();
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }
    synchronize_performance();
    bind_performance_source();
    const Sample& s = SampleBank::instance().slot(sample_slot);
    if (s.empty()) {
        visual_grains_ = 0;
        visual_active_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }

    const q15 gain = params[P_GAIN].effective;
    const int ch = s.channels;
    const uint32_t nf = playable_frames(s);
    const int limit = (int)voice_limit(params[P_VOICES].effective);

    uint32_t interval = clock_sync ? sync_interval_frames
                                  : spawn_interval_frames(params[P_DENS].effective);
    if (interval < 32) interval = 32;
    visual_interval_ = (uint16_t)(interval > 0xffffu ? 0xffffu : interval);
    const int32_t jitter_span =
        (int32_t)(((uint64_t)(uint16_t)params[P_JITTER].effective * interval) >> 15);

    // Remember last rendered coordinates, including heads that end mid-block.
    // Normalize once after rendering, retaining historical last-sample/stale-slot
    // diagnostics without two dynamic 64-bit divisions per head per sample.
    uint32_t last_position[MAX_GRAINS], last_age[MAX_GRAINS];
    uint16_t touched = 0;
    const int64_t scan_span = (int64_t)nf * 65536;
    int32_t scan_step = clean ? scan_inc_q16_ : 0;
    // Tiny sources cannot spawn, but their independent scan must still wrap safely.
    if (nf < 4) scan_step = (int32_t)(scan_step % scan_span);

    // Reading a frame is identical for grains and tails; keep it inline-local so
    // both paths stay bit-identical.
    const auto read_frame = [&](int64_t position, trackr::fx::uq16 frac) -> q15 {
        q15 a, b;
        if (ch == 2) {
            a = (q15)(((int32_t)s.data[(size_t)position * 2] +
                       s.data[(size_t)position * 2 + 1]) >> 1);
            b = (q15)(((int32_t)s.data[((size_t)position + 1) * 2] +
                       s.data[((size_t)position + 1) * 2 + 1]) >> 1);
        } else {
            a = s.data[(size_t)position];
            b = s.data[(size_t)position + 1];
        }
        return lerp_q15(a, b, frac);
    };

    const bool free_flow = !clean || launch_mode == LaunchMode::Flow;
    const bool held_gate = clean && launch_mode == LaunchMode::Gate &&
        (pad_token_ || gate_deadline_ != UINT64_MAX);
    for (int i = 0; i < frames; ++i) {
        if (spawn_countdown_ == 0 && (free_flow || held_gate || pending_launches_)) {
            spawn_grain(nf, limit);
            if (!free_flow && pending_launches_) --pending_launches_;
            int32_t next = (int32_t)interval + rand_signed(jitter_span);
            if (next < 1) next = 1;
            spawn_countdown_ = (uint32_t)next;
            clock_countdown_fraction_ = 0;
        }
        if (spawn_countdown_) --spawn_countdown_;

        q31 acc = 0;
        uint16_t visual_mask = 0;
        uint8_t active_count = 0;

        for (int index = 0; index < MAX_GRAINS; ++index) {
            Head& h = heads_[index];
            if (!h.active) continue;
            if (h.age >= h.length || h.pos < 0 || h.pos + 1 >= (int64_t)nf) {
                h.active = false;
                continue;
            }
            const q15 sm = read_frame(h.pos, h.frac);
            const q15 win = grain_window_q15(h.age, h.length, h.attack, h.release,
                                             h.attack_step, h.release_step, h.window_shift,
                                             h.shape, h.hann_step);
            acc += ((q31)sm * win) >> 15;

            visual_mask |= (uint16_t)(1u << index);
            ++active_count;
            touched |= (uint16_t)(1u << index);
            last_position[index] = (uint32_t)h.pos;
            last_age[index] = h.age;
            visual_pitch_offsets_[index] = h.pitch_offset;

            const int64_t advance = (int64_t)h.frac + h.inc_q16;
            h.pos += advance >> 16;
            h.frac = (trackr::fx::uq16)(advance & 0xFFFF);
            ++h.age;
        }

        for (int index = 0; index < MAX_TAILS; ++index) {
            Tail& t = tails_[index];
            if (!t.head.active || t.remaining == 0) continue;
            Head& h = t.head;
            if (h.age >= h.length || h.pos < 0 || h.pos + 1 >= (int64_t)nf) {
                h.active = false;
                t.remaining = 0;
                continue;
            }
            const q15 sm = read_frame(h.pos, h.frac);
            const q15 win = grain_window_q15(h.age, h.length, h.attack, h.release,
                                             h.attack_step, h.release_step, h.window_shift,
                                             h.shape, h.hann_step);
            // Linear fade over the declick window, multiplied into the grain's
            // own envelope so a stolen voice decays from its true amplitude.
            const int32_t fade = (int32_t)((t.remaining - 1) * 32767u / (DECLICK_FRAMES - 1));
            const q15 shaped = (q15)(((q31)win * fade) >> 15);
            acc += ((q31)sm * shaped) >> 15;

            const int64_t advance = (int64_t)h.frac + h.inc_q16;
            h.pos += advance >> 16;
            h.frac = (trackr::fx::uq16)(advance & 0xFFFF);
            ++h.age;
            if (--t.remaining == 0) h.active = false;
        }

        visual_grains_ = visual_mask;
        visual_active_ = active_count;
        // Fixed 1/8 headroom, no density-driven pumping. Sum and user gain precede
        // the only saturation; 8 full-scale heads fit, tails may still overload.
        buf[i] = clean ? sat_q15((q31)(((int64_t)acc * gain) >> 18)) :
            (q15)(((q31)sat_q15(acc) * gain) >> 15);
        if (scan_step) {
            scan_offset_q16_ += scan_step;
            if (scan_offset_q16_ < 0) scan_offset_q16_ += scan_span;
            if (scan_offset_q16_ >= scan_span) scan_offset_q16_ -= scan_span;
        }
    }
    for (int index = 0; index < MAX_GRAINS; ++index) if (touched & (1u << index)) {
        visual_positions_[index] = (uint8_t)((uint64_t)last_position[index] * 95u / (nf > 1 ? nf - 1 : 1));
        const uint32_t length = heads_[index].length;
        visual_phases_[index] = length > 1 ? (uint8_t)((uint64_t)last_age[index] * 95u / (length - 1)) : 95;
    }
}

// ============================ OSC ============================

OscUnit::OscUnit() {
    kind = UnitKind::Osc;
    add_param("PTCH", 16384);          // 0..32767 -> midi 24..96
    add_param("PW",   16384);
    add_param("GAIN", 14000);
    add_param("FOLD", 0);              // 0..32767 -> 1x..7x bounded wavefold drive
    add_param("SUB",  0);              // octave-down companion amount
    add_param("DRFT", 0);              // bounded low-frequency pitch wandering
    add_param("SHAPE", 0);             // sine -> triangle -> saw -> pulse, UNIT bit1 opt-in
    (void)osc_clean::sine_table();      // prime outside audio; common immutable 4KiB LUT
    clean_seed_ = next_osc_seed();
    reset();
}

q15 OscUnit::shape_anchor(Wave value) {
    static const q15 anchors[] = {0, 10922, 21845, 32767};
    const int index = (int)value;
    return anchors[index >= 0 && index < (int)Wave::Count ? index : 0];
}

OscUnit::Wave OscUnit::nearest_shape_anchor(q15 value) {
    const int v = value < 0 ? 0 : (int)value;
    Wave nearest = Wave::Sine;
    int distance = v;
    for (int index = 1; index < (int)Wave::Count; ++index) {
        const int next = std::abs(v - (int)shape_anchor((Wave)index));
        if (next < distance) { distance = next; nearest = (Wave)index; }
    }
    return nearest;
}

static uint32_t pitch_phase_increment(q15 pitch, bool quantize) {
    int note100 = 2400 + (int)(((int64_t)(uint16_t)pitch * 7200) >> 15);
    if (quantize) note100 = ((note100 + 50) / 100) * 100;
    uint32_t semis = (uint32_t)(note100 / 100);
    static const uint32_t SEMI_Q16[12] = {
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98193, 104032, 110218, 116772, 123715
    };
    const uint32_t octave = semis / 12u;
    const uint32_t note = semis % 12u;
    uint64_t freq_q16 = ((uint64_t)1071616 * SEMI_Q16[note]) >> 16;
    freq_q16 <<= octave;
    return (uint32_t)((freq_q16 << 16) / ENGINE_SR);
}

static int32_t fast_sine(uint32_t phase) {
    const uint32_t p = phase >> 16;
    const int32_t tri = p < 0x8000u ? (int32_t)p * 2 - 32768
                                     : 32767 - ((int32_t)p - 32768) * 2;
    const int32_t magnitude = tri < 0 ? -tri : tri;
    return (int32_t)(((int64_t)tri * (65536 - magnitude)) >> 15);
}

static int32_t osc_wave_sample(OscUnit::Wave wave, uint32_t phase, uint32_t pw) {
    switch (wave) {
        case OscUnit::Wave::Saw:
            return (int32_t)(phase >> 16) - 32768;
        case OscUnit::Wave::Pulse:
            return phase < pw ? -32767 : 32767;
        case OscUnit::Wave::Tri: {
            const uint32_t p = phase >> 16;
            return p < 0x8000u ? (int32_t)p * 2 - 32768
                               : 32767 - ((int32_t)p - 32768) * 2;
        }
        case OscUnit::Wave::Sine:
        default: {
            const int32_t value = fast_sine(phase);
            return value > 32767 ? 32767 : value < -32768 ? -32768 : value;
        }
    }
}

static int32_t osc_shape_sample(q15 shape, uint32_t phase, uint32_t pw) {
    const int value = shape < 0 ? 0 : (int)shape;
    OscUnit::Wave left = OscUnit::Wave::Sine, right = OscUnit::Wave::Tri;
    int start = 0, span = 10922;
    if (value > 21845) {
        left = OscUnit::Wave::Saw; right = OscUnit::Wave::Pulse;
        start = 21845; span = 10922;
    } else if (value > 10922) {
        left = OscUnit::Wave::Tri; right = OscUnit::Wave::Saw;
        start = 10922; span = 10923;
    }
    const int32_t a = osc_wave_sample(left, phase, pw);
    const int32_t b = osc_wave_sample(right, phase, pw);
    return a + (int32_t)(((int64_t)(b - a) * (value - start)) / span);
}

void OscUnit::control_tick() {
    if (clean) { clean_control_tick(); return; }
    set_visual_capture(false);
    inc_ = arp_scale_increment(pitch_phase_increment(params[P_PITCH].effective, pitch_quantize));
}

void OscUnit::process(q15* buf, int frames) {
    if (clean) { clean_process(buf, frames); return; }
    const q15 gain = params[P_GAIN].effective;
    const q15 fold = params[P_FOLD].effective;
    const q15 sub = params[P_SUB].effective;
    const q15 drift = params[P_DRIFT].effective;
    const uint32_t pw = 0x40000000u + ((uint32_t)(uint16_t)params[P_PW].effective << 15); // 25..75%

    for (int i = 0; i < frames; ++i) {
        // DRFT chooses a deterministic target about twice per second and glides
        // toward it. At maximum the bounded excursion is about +/-1.6% (~27 cents):
        // audible as pitch instability without turning into a second pitch control.
        if (++drift_count_ >= 16384) {
            drift_count_ = 0;
            drift_rng_ = drift_rng_ * 1664525u + 1013904223u;
            drift_target_ = (q15)(((int32_t)(drift_rng_ >> 17) - 16384) *
                                   (int32_t)(uint16_t)drift >> 15);
        }
        drift_current_ = (q15)(drift_current_ + ((int32_t)drift_target_ - drift_current_) / 4096);
        int64_t drifted = (int64_t)inc_ + ((int64_t)inc_ * drift_current_ >> 20);
        if (drifted < 1) drifted = 1;
        phase_ += (uint32_t)drifted;
        sub_phase_ += (uint32_t)drifted >> 1;
        uint32_t p = phase_;
        int32_t v;
        if (shape_morph) {
            v = osc_shape_sample(params[P_SHAPE].effective, p, pw);
        } else {
            // Exact historical discrete-wave path for SNC1-6 and unflagged SNC7.
            v = osc_wave_sample(wave, p, pw);
        }

        // FOLD continuously increases pre-fold drive. Reflection is bounded to
        // four passes because the maximum drive is 7x, keeping Old-3DS cost fixed.
        int32_t shaped = (int32_t)(((int64_t)v * (32767 + (int32_t)(uint16_t)fold * 6)) >> 15);
        for (int pass = 0; pass < 4 && (shaped > 32767 || shaped < -32768); ++pass) {
            if (shaped > 32767) shaped = 65534 - shaped;
            if (shaped < -32768) shaped = -65536 - shaped;
        }
        shaped = sat_q15(shaped);

        // SUB is a bipolar square derived from a true half-rate phase accumulator.
        const int32_t sub_wave = (sub_phase_ & 0x80000000u) ? 32767 : -32767;
        shaped = sat_q15(shaped + (((sub_wave - shaped) * (int32_t)sub) >> 15));
        buf[i] = (q15)(((q31)shaped * gain) >> 15);
    }
}

// ============================ NOISE ============================

NoiseUnit::NoiseUnit(bool modern) : modern_(modern) {
    kind = UnitKind::Noise;
    add_param("COLR", 10922);
    add_param("DENS", modern ? 32767 : 20316);
    add_param("TONE", modern ? 28000 : 16384);
    add_param("GAIN", 24576);
    add_param("BODY", 0);       // appended: old patches keep their full low end
    add_param("DECAY", 32767);  // appended: maximum preserves the original drone
    add_param("ATK", 0);        // exact old envelope/output arithmetic at zero
    add_param("MODE", 32767);   // new cards drone; loaders migrate old cards to follow
    add_param("RES", 0);
    add_param("SWEEP", 0, true);
    add_param("STIME", modern ? 21085 : 0);
    add_param("EVENT", modern ? 17153 : 0);
    num_params = modern ? P_COUNT : LEGACY_COUNT;
}

uint32_t NoiseUnit::attack_frames(q15 attack) {
    if (attack <= 0) return 0;
    // Exact endpoints: raw 1 = 1ms, raw 32767 = 2000ms; quadratic in
    // engine samples, not rounded milliseconds. Largest product < 2^47.
    const uint32_t x = (uint16_t)attack - 1u;
    const uint32_t first = ENGINE_SR / 1000u;
    return first + (uint32_t)((uint64_t)x * x * (ENGINE_SR * 2u - first) /
                              (32766ull * 32766ull));
}

void NoiseUnit::begin_attack() {
    attack_total_ = attack_frames(params[P_ATTACK].effective);
    // A freshly loaded/cloned configured voice has not produced an envelope yet.
    // Later triggers preserve the current level, including a full-level drone.
    if (!envelope_started_ && attack_total_) envelope_ = 0;
    envelope_started_ = true;
    attack_elapsed_ = attack_error_ = 0;
    if (!attack_total_) { envelope_ = 32767; return; }
    const uint32_t rise = 32767u - (uint16_t)envelope_;
    attack_step_ = (uint16_t)(rise / attack_total_);
    attack_remainder_ = (uint16_t)(rise % attack_total_);
    // Bresenham-style linear ramp: exact floor(rise*n/total), no per-sample
    // divide or floating point; error is always <2*64000, envelope <=32767.
}

void NoiseUnit::reset() {
    if (modern_) { modern_reset(); return; }
    processed_ = false; last_out_ = transition_from_ = 0; transition_left_ = 0;
    rng_ = 0x4E4F4953u;
    held_ = pink_ = filtered_ = body_low_ = crackle_ = 0;
    envelope_ = params[P_ATTACK].effective > 0 ? 0 : 32767;
    envelope_started_ = true;
    drone_ = drone_mode(params[P_MODE].effective, params[P_DECAY].effective);
    attack_total_ = attack_elapsed_ = attack_error_ = 0;
    if (drone_) begin_attack();
    hold_count_ = 0;
    visual_activity_ = 0;
}

void NoiseUnit::trigger() {
    if (modern_) { modern_control(); modern_trigger(); return; }
    drone_ = drone_mode(params[P_MODE].effective, params[P_DECAY].effective);
    begin_attack();
}

void NoiseUnit::control_tick() {
    if (modern_) { modern_control(); return; }
    const uint32_t density = (uint16_t)params[P_DENSITY].effective;
    hold_period_ = (uint16_t)(1u + ((32767u - density) * 63u) / 32767u);
    const uint32_t tone = (uint16_t)params[P_TONE].effective;
    tone_alpha_ = (uint16_t)(256u + (tone * 32256u) / 32767u);
    const uint32_t body = (uint16_t)params[P_BODY].effective;
    // BODY is a cheap one-pole high-pass: zero is exact bypass for patch
    // compatibility, while the top end progressively removes low-frequency mass.
    body_alpha_ = body == 0 ? 0 : (uint16_t)(48u + (body * 16144u) / 32767u);
    uint32_t decay = (uint16_t)params[P_DECAY].effective;
    const bool was_drone = drone_;
    drone_ = drone_mode(params[P_MODE].effective, params[P_DECAY].effective);
    if (!envelope_started_) {
        envelope_ = params[P_ATTACK].effective > 0 ? 0 : 32767;
        envelope_started_ = true;
        if (drone_) begin_attack();
    } else if (drone_ && !was_drone) begin_attack();
    // Explicit HIT keeps the same cubic taper but never reaches the historical
    // drone threshold. MODE changes never rewrite the authored DECAY value.
    if (mode_index(params[P_MODE].effective) == 1 && decay >= 32760u) decay = 32759u;
    // Exponential envelope: about 10 ms at zero through several seconds near
    // maximum. Follow mode preserves the historical infinite threshold exactly.
    if (drone_) decay_step_ = 0;
    else {
        const uint32_t inverse = 32767u - decay;
        uint32_t curve = (inverse * inverse) >> 15;
        curve = (curve * inverse) >> 15;
        decay_step_ = (uint16_t)(2u + (curve * 1022u >> 15));
    }
}

void NoiseUnit::process(q15* buf, int frames) {
    if (modern_) { modern_process(buf, frames); return; }
    const uint32_t density = (uint16_t)params[P_DENSITY].effective;
    const q15 gain = params[P_GAIN].effective;
    int colour = ((uint32_t)(uint16_t)params[P_COLOUR].effective * 4u) >> 15;
    if (colour > 3) colour = 3;
    int32_t peak = 0;
    for (int i = 0; i < frames; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const int32_t white = (int32_t)(rng_ >> 16) - 32768;
        if (hold_count_ == 0) { held_ = white; hold_count_ = hold_period_; }
        --hold_count_;
        pink_ += (white - pink_) >> 4;
        int32_t source;
        if (colour == (int)Colour::Pink) {
            source = (white >> 1) + pink_;
        } else if (colour == (int)Colour::Crackle) {
            // DENS controls event rate here, not only S&H period. The old
            // 0xffff-density/2 threshold made the full range almost equally
            // silent (at most one event per two samples).
            const uint32_t threshold = density << 1;
            if ((rng_ & 0xffffu) < threshold) crackle_ = white;
            source = crackle_;
            crackle_ -= crackle_ >> 5;
        } else if (colour == (int)Colour::Metal) {
            const int32_t edge = ((held_ ^ (int32_t)(rng_ >> 9)) & 0x4000) ? 24576 : -24576;
            source = edge + (held_ >> 2);
        } else {
            source = hold_period_ <= 2 ? white : held_;
        }
        filtered_ += (int32_t)(((int64_t)(source - filtered_) * tone_alpha_) >> 15);
        int32_t shaped = filtered_;
        if (body_alpha_ != 0) {
            body_low_ += (int32_t)(((int64_t)(filtered_ - body_low_) * body_alpha_) >> 15);
            shaped = filtered_ - body_low_;
        } else body_low_ = filtered_;
        const int32_t out = sat_q15((int32_t)(((int64_t)shaped * gain * envelope_) >> 30));
        buf[i] = (q15)out;
        const int32_t magnitude = out < 0 ? -out : out;
        if (magnitude > peak) peak = magnitude;
        if (attack_elapsed_ < attack_total_) {
            int32_t next = (int32_t)envelope_ + attack_step_;
            attack_error_ += attack_remainder_;
            if (attack_error_ >= attack_total_) { attack_error_ -= attack_total_; ++next; }
            envelope_ = (q15)next;
            if (++attack_elapsed_ == attack_total_) envelope_ = 32767;
        } else if (!drone_ && envelope_ > 0) {
            int32_t fall = ((int32_t)envelope_ * decay_step_) >> 15;
            if (fall < 1) fall = 1;
            envelope_ = (q15)(envelope_ > fall ? envelope_ - fall : 0);
        }
    }
    visual_activity_ = (uint16_t)(peak > 32767 ? 32767 : peak);
    if (frames > 0) { last_out_ = buf[frames-1]; processed_ = true; }
}

#include "noise_impl.inc"

// ============================ FM ============================

FmUnit::FmUnit() {
    kind = UnitKind::Fm;
    add_param("PTCH", 16384);
    add_param("RATIO", 8192);
    add_param("INDEX", 13926);
    add_param("FDBK", 13762);
    add_param("DECAY", 12000);
    add_param("GAIN", 22000);
    // Appended controls are neutral at zero so dormant SNC7 ParamBank entries
    // preserve every patch authored by the original six-parameter FM voice.
    add_param("IFALL", 0);
    add_param("ATK", 0);
    add_param("RFINE", 0, true);
    add_param("FFALL", 0);
    add_param("PENV", 0, true);
    add_param("PTIME", DEFAULT_PITCH_TIME);
}

void FmUnit::set_extended(bool enabled) {
    extended = enabled;
    num_params = enabled ? P_COUNT : P_PITCH_ENV;
    if (!enabled) {
        clear_pitch_envelope();
        carrier_mix_.select(Wave::Sine, true);
        mod_mix_.select(Wave::Sine, true);
    }
}

int FmUnit::pitch_env_cents(q15 value) {
    const int v = std::max(-32767, (int)value);
    const int magnitude = v < 0 ? -v : v;
    const int cents = (magnitude * 4800 + 16383) / 32767;
    return v < 0 ? -cents : cents;
}

uint32_t FmUnit::pitch_env_samples(q15 value) {
    const uint32_t scaled = (uint32_t)std::max(0, (int)value) * 32u;
    const uint32_t index = scaled / 32767u, remainder = scaled % 32767u;
    if (index >= 32) return fm_extended::TIME_SAMPLES[32];
    const uint32_t a = fm_extended::TIME_SAMPLES[index];
    return a + ((fm_extended::TIME_SAMPLES[index + 1] - a) * remainder + 16383u) / 32767u;
}

int FmUnit::visual_pitch_cents() const {
    const int64_t cents = (int64_t)pitch_log2_ * 1200;
    return (int)((cents < 0 ? cents - (1 << 23) : cents + (1 << 23)) / (1 << 24));
}

void FmUnit::clear_pitch_envelope() {
    pitch_log2_ = pitch_step_ = 0;
    pitch_total_ = pitch_remaining_ = pitch_remainder_ = pitch_error_ = 0;
    pitch_sign_ = 0;
}

void FmUnit::advance_pitch_envelope() {
    if (!pitch_remaining_) return;
    if (!--pitch_remaining_) { pitch_log2_ = 0; return; }
    pitch_log2_ -= pitch_step_;
    pitch_error_ += pitch_remainder_;
    if (pitch_error_ >= pitch_total_) {
        pitch_error_ -= pitch_total_;
        pitch_log2_ -= pitch_sign_;
    }
}

uint32_t FmUnit::pitched_carrier_increment() const {
    // Floor signed octaves without an implementation-defined negative shift.
    const int octave = pitch_log2_ >= 0 ? pitch_log2_ / (1 << 24)
        : -1 - ((-1 - pitch_log2_) / (1 << 24));
    const uint32_t fraction = (uint32_t)pitch_log2_ & 0xffffffu;
    const uint32_t index = fraction >> 16, remainder = fraction & 65535u;
    const uint32_t a = fm_extended::EXP2_Q24[index];
    uint32_t factor = a + (uint32_t)(((uint64_t)(fm_extended::EXP2_Q24[index + 1] - a) * remainder) >> 16);
    if (octave >= 0) factor <<= octave;
    else factor >>= -octave;
    const uint64_t increment = ((uint64_t)carrier_inc_ * factor) >> 24;
    return (uint32_t)std::min<uint64_t>(increment, arp_voice.semitones ? 0x70000000u : UINT32_MAX);
}

static int32_t fm_wave_sample(FmUnit::Wave wave, uint32_t phase) {
    switch (wave) {
        case FmUnit::Wave::Triangle: {
            const uint32_t p = phase >> 16;
            return p < 0x8000u ? (int32_t)p * 2 - 32768
                              : 32767 - ((int32_t)p - 32768) * 2;
        }
        case FmUnit::Wave::Saw: return (int32_t)(phase >> 16) - 32768;
        case FmUnit::Wave::Pulse: return phase < 0x80000000u ? -32767 : 32767;
        default: return fast_sine(phase); // exact original sine, not a new oscillator
    }
}

void FmUnit::WaveMix::select(Wave wave, bool immediate) {
    if ((uint8_t)wave >= (uint8_t)Wave::Count) wave = Wave::Sine;
    if (immediate) {
        target = wave; remaining = 0;
        for (int i = 0; i < 4; ++i) weight[i] = start[i] = i == (int)wave ? 65536 : 0;
    } else if (wave != target) {
        target = wave; remaining = 64;
        // A retarget starts at the actual current mixture, never a stale shape.
        for (int i = 0; i < 4; ++i) start[i] = weight[i];
    }
}

int32_t FmUnit::WaveMix::sample(uint32_t phase) {
    if (!remaining) return fm_wave_sample(target, phase);
    int64_t value = 0;
    for (int i = 0; i < 4; ++i)
        if (weight[i]) value += (int64_t)weight[i] * fm_wave_sample((Wave)i, phase);
    const int elapsed = 65 - remaining;
    for (int i = 0; i < 4; ++i)
        weight[i] = start[i] + ((i == (int)target ? 65536 : 0) - start[i]) * elapsed / 64;
    --remaining;
    return sat_q15((int32_t)(value >> 16));
}

void FmUnit::reset() {
    clear_pitch_envelope();
    waves_started_ = false;
    carrier_phase_ = mod_phase_ = 0;
    last_mod_ = 0;
    env_ = 0;
    index_env_ = feedback_env_ = 32767;
    attacking_ = false;
    if (rendered_drone_) carrier_inc_ = 0; // refresh policy after reset/mode change
    rendered_drone_ = drone_release_ = false;
}

void FmUnit::trigger() {
    if (drone) return; // no phase, feedback-memory, envelope or attack restart
    if (rendered_drone_) {
        rendered_drone_ = false;
        control_tick();
    }
    drone_release_ = false;
    if (phase_reset) {
        carrier_phase_ = mod_phase_ = 0;
        last_mod_ = 0;
    }
    index_env_ = feedback_env_ = 32767;
    attacking_ = params[P_ATTACK].effective > 0;
    env_ = attacking_ ? 1 : 32767;
    clear_pitch_envelope();
    if (extended) {
        const int cents = pitch_env_cents(params[P_PITCH_ENV].effective);
        if (cents) {
            pitch_log2_ = (int32_t)((int64_t)cents * (1 << 24) / 1200);
            pitch_total_ = pitch_remaining_ = pitch_env_samples(params[P_PITCH_TIME].effective);
            const uint32_t magnitude = (uint32_t)(pitch_log2_ < 0 ? -pitch_log2_ : pitch_log2_);
            pitch_sign_ = cents < 0 ? -1 : 1;
            pitch_step_ = pitch_sign_ * (int32_t)(magnitude / pitch_total_);
            pitch_remainder_ = magnitude % pitch_total_;
        }
    }
}

uint32_t FmUnit::modulation_increment(uint32_t carrier, q15 ratio, q15 fine,
                                       bool drone_mode, bool safe) {
    int32_t ratio_q12 = 2048 + (int32_t)((uint32_t)(uint16_t)ratio * 30720u / 32767u);
    uint64_t mod;
    if (drone_mode) {
        // Keep the existing coarse RATIO grid but do not throw away the low
        // RFINE bits. Positive rational numerator, one final truncation. Largest
        // product is <2^63 even for the full uint32 carrier and q15 input range.
        int64_t numerator = (int64_t)ratio_q12 * 32767 + (int32_t)fine * 1024;
        const int64_t lo = (int64_t)1024 * 32767, hi = (int64_t)33792 * 32767;
        if (numerator < lo) numerator = lo;
        if (numerator > hi) numerator = hi;
        mod = (uint64_t)carrier * (uint64_t)numerator / (4096ull * 32767ull);
    } else {
        // Exact legacy path for all ten controls, including signed division.
        ratio_q12 += (int32_t)fine * 1024 / 32767;
        if (ratio_q12 < 1024) ratio_q12 = 1024;
        if (ratio_q12 > 33792) ratio_q12 = 33792;
        mod = ((uint64_t)carrier * (uint32_t)ratio_q12) >> 12;
    }
    constexpr uint32_t SAFE_MOD_INCREMENT = 1610612736u; // 12 kHz at 32 kHz
    const uint64_t ceiling = safe ? SAFE_MOD_INCREMENT : 0xffffffffu;
    return (uint32_t)(mod > ceiling ? ceiling : mod);
}

void FmUnit::control_tick() {
    // FM is a note-lane source: use the same rounded MIDI 24..96 contract that
    // the sequencer displays, including the exact upper endpoint.
    carrier_inc_ = arp_scale_increment(pitch_phase_increment(params[P_PITCH].effective, true));
    // Generic Param slew intentionally truncates its final three q15 steps.
    // In drone only, let the fine-ratio control actually reach each saved LSB;
    // otherwise precision edits settle to an audible-but-wrong nearby ratio.
    auto& fine = params[P_RATIO_FINE];
    if (drone && fine.slew_ready) {
        const int remaining = (int)fine.target - fine.effective;
        if (remaining >= -3 && remaining <= 3) fine.effective = fine.target;
    }
    mod_inc_ = modulation_increment(carrier_inc_, params[P_RATIO].effective,
                                   params[P_RATIO_FINE].effective, drone, alias_safe);
    // Envelope only exists in strike: cache the exact old signed Q12 ratio once
    // per control tick, then apply it to each pitched carrier before the guard.
    int32_t ratio = 2048 + (int32_t)((uint32_t)(uint16_t)params[P_RATIO].effective * 30720u / 32767u);
    ratio += (int32_t)params[P_RATIO_FINE].effective * 1024 / 32767;
    pitch_ratio_q12_ = (uint32_t)std::max<int32_t>(1024, std::min<int32_t>(33792, ratio));

    const uint32_t decay = (uint16_t)params[P_DECAY].effective;
    decay_step_ = (uint16_t)(16u + ((32767u - decay) * 496u) / 32767u);
    const uint32_t attack_samples = 1u + (uint32_t)(uint16_t)params[P_ATTACK].effective * 639u / 32767u;
    attack_step_ = (uint16_t)((32766u + attack_samples - 1u) / attack_samples);
    const uint32_t index_fall = (uint16_t)params[P_INDEX_FALL].effective;
    const uint32_t feedback_fall = (uint16_t)params[P_FEEDBACK_FALL].effective;
    index_fall_step_ = index_fall ? (uint16_t)(8u + index_fall * 248u / 32767u) : 0;
    feedback_fall_step_ = feedback_fall ? (uint16_t)(8u + feedback_fall * 248u / 32767u) : 0;
}

void FmUnit::process(q15* buf, int frames) {
    const q15 base_index = params[P_INDEX].effective;
    const q15 base_feedback = params[P_FEEDBACK].effective;
    const q15 gain = params[P_GAIN].effective;
    if (frames <= 0) return;
    if (!extended || drone) clear_pitch_envelope();
    carrier_mix_.select(extended ? carrier_wave : Wave::Sine, !waves_started_ || !extended);
    mod_mix_.select(extended ? mod_wave : Wave::Sine, !waves_started_ || !extended);
    waves_started_ = true;
    const bool enable_index_fall = params[P_INDEX_FALL].effective != 0;
    const bool enable_feedback_fall = params[P_FEEDBACK_FALL].effective != 0;
    if (drone != rendered_drone_) {
        // Neither direction resets either phase or feedback memory. A mode
        // switch is not a trigger; resume decay from the exact current level.
        attacking_ = false;
        drone_release_ = !drone;
        if (drone) {
            if (!enable_index_fall) index_env_ = 32767;
            if (!enable_feedback_fall) feedback_env_ = 32767;
        }
        rendered_drone_ = drone;
        control_tick();
    } else if (carrier_inc_ == 0) control_tick();
    for (int i = 0; i < frames; ++i) {
        const q15 index = (drone ? index_env_ < 32767 : enable_index_fall)
            ? (q15)(((int32_t)base_index * index_env_) >> 15) : base_index;
        const q15 feedback = (drone ? feedback_env_ < 32767 : enable_feedback_fall)
            ? (q15)(((int32_t)base_feedback * feedback_env_) >> 15) : base_feedback;
        uint32_t carrier_increment = carrier_inc_, mod_increment = mod_inc_;
        if (pitch_remaining_) {
            carrier_increment = pitched_carrier_increment();
            const uint64_t mod = ((uint64_t)carrier_increment * pitch_ratio_q12_) >> 12;
            mod_increment = (uint32_t)std::min<uint64_t>(mod, alias_safe ? 1610612736u : UINT32_MAX);
        }
        mod_phase_ += mod_increment;
        const int32_t feedback_phase = (int32_t)((int64_t)last_mod_ * feedback * 2);
        last_mod_ = sat_q15(mod_mix_.sample(mod_phase_ + (uint32_t)feedback_phase));
        const int32_t modulation = (int32_t)(((int64_t)last_mod_ * index * 8) >> 15);
        carrier_phase_ += carrier_increment;
        const uint32_t carrier_phase_mod = (uint32_t)((int64_t)modulation * 32768);
        const int32_t carrier = carrier_mix_.sample(carrier_phase_ + carrier_phase_mod);
        buf[i] = sat_q15((int32_t)(((int64_t)carrier * env_ * gain) >> 30));
        advance_pitch_envelope();

        if (drone) {
            // Bounded linear entry/recovery, then genuinely sustained operators.
            // Saved attack/decays are dormant, never rewritten to fake a hold.
            env_ = (q15)std::min(32767, (int)env_ + DRONE_FADE_STEP);
            index_env_ = (q15)std::min(32767, (int)index_env_ + DRONE_FADE_STEP);
            feedback_env_ = (q15)std::min(32767, (int)feedback_env_ + DRONE_FADE_STEP);
            continue;
        }
        if (attacking_) {
            const int32_t next = (int32_t)env_ + attack_step_;
            if (next >= 32767) { env_ = 32767; attacking_ = false; }
            else env_ = (q15)next;
        } else if (env_ > 0) {
            int32_t fall = ((int32_t)env_ * decay_step_) >> 15;
            if (fall < 1) fall = 1;
            if (drone_release_ && fall > DRONE_FADE_STEP) fall = DRONE_FADE_STEP;
            env_ = (q15)(env_ > fall ? env_ - fall : 0);
        }
        if (index_fall_step_ && index_env_ > 0) {
            int32_t fall = ((int32_t)index_env_ * index_fall_step_) >> 15;
            if (fall < 1) fall = 1;
            index_env_ = (q15)(index_env_ > fall ? index_env_ - fall : 0);
        }
        if (feedback_fall_step_ && feedback_env_ > 0) {
            int32_t fall = ((int32_t)feedback_env_ * feedback_fall_step_) >> 15;
            if (fall < 1) fall = 1;
            feedback_env_ = (q15)(feedback_env_ > fall ? feedback_env_ - fall : 0);
        }
    }
}

// ============================ MOTOR ============================
#include "motor_impl.inc"


// ============================ FILT ============================

FilterUnit::FilterUnit() {
    kind = UnitKind::Filter;
    add_param("CUT", 24000); add_param("RES", 4096);
    add_param("DRV", 0); add_param("MORPH", 0);
}

void FilterUnit::control_tick() {
    cutoff_smooth_ = (q15)(cutoff_smooth_ + ((int32_t)params[P_CUTOFF].effective - cutoff_smooth_) / 4);
    res_smooth_    = (q15)(res_smooth_    + ((int32_t)params[P_RES].effective    - res_smooth_)    / 4);
    svf_.type = type;
    svf_.set_params(cutoff_smooth_, res_smooth_);
}

void FilterUnit::reset() {
    cutoff_smooth_ = params[P_CUTOFF].effective;
    res_smooth_ = params[P_RES].effective;
    svf_.type = type;
    svf_.set_params(cutoff_smooth_, res_smooth_);
    svf_.reset();
}

void FilterUnit::process(q15* buf, int frames) {
    // Filter type OFF is the enum's promised exact core bypass. Unit BYP remains
    // the graph-level bypass, but OFF must not leave DRIVE/MORPH secretly active.
    if (type == trackr::dsp::FilterType::Off) return;
    const q31 gain = 32767 + (q31)params[P_DRIVE].effective * 3;
    const q15 morph = params[P_MORPH].effective;
    for (int i = 0; i < frames; ++i) {
        // DRIVE reaches 4x. Widen before multiply: q15 minimum * max gain
        // exceeds signed 32-bit even though the post-shift soft-clip input does not.
        q15 input = trackr::fx::soft_clip_q31(
            (q31)(((int64_t)buf[i] * gain) >> 15));
        const q15 filtered = svf_.step(input);
        const q31 inverse = (q31)input - filtered;
        // MORPH crossfades the selected SVF output toward its complementary
        // branch (input-filtered). The delta can be three q15 ranges wide.
        buf[i] = sat_q15((q31)filtered +
                         (q31)(((int64_t)(inverse - filtered) * morph) >> 15));
    }
}

// ============================ STRAIN ============================

static q15 strain_reflection_fold(q31 input, int32_t gain_q15) {
    int64_t value64 = ((int64_t)input * gain_q15) >> 15;
    if (value64 > 196602) value64 = 196602;
    if (value64 < -196608) value64 = -196608;
    q31 value = (q31)value64;
    for (int pass = 0; pass < 4 && (value > 32767 || value < -32768); ++pass) {
        if (value > 32767) value = 65534 - value;
        if (value < -32768) value = -65536 - value;
    }
    return sat_q15(value);
}

namespace strain_detail {
uint32_t rate_map(uint32_t raw,uint32_t span,const uint32_t (&knots)[17]) {
    const uint32_t scaled=raw*16u,index=scaled/span,rest=scaled%span;
    return index>=16?knots[16]:knots[index]+uint32_t((uint64_t(knots[index+1]-knots[index])*rest+span/2)/span);
}
int32_t trim_gain(q15 raw) {
    // Q16 gains at integer dB knots, linearly interpolated at centidB resolution.
    // Exact unity; no libm, allocation or transcendental work in the audio path.
    static constexpr int32_t gain[25]={8250,9257,10387,11654,13076,14672,16462,18471,20724,23253,26090,29274,32846,36854,41350,46396,52057,58409,65536,73533,82505,92572,103868,116541,130762};
    const int db=StrainUnit::trim_centidb(raw)+1800,index=db/100,rest=db%100;
    return index>=24?gain[24]:gain[index]+(gain[index+1]-gain[index])*rest/100;
}
}
uint32_t StrainUnit::rate_microhz(q15 value) {
    // Piecewise-log knots anchored to the original .032Hz rate exactly.
    static constexpr uint32_t low[17]={10000,10754,11565,12437,13375,14383,15468,16634,17889,19237,20688,22248,23926,25730,27670,29756,32000};
    static constexpr uint32_t high[17]={32000,45188,63811,90108,127243,179683,253733,358302,505964,714482,1008933,1424734,2011893,2841033,4011876,5665246,8000000};
    const uint32_t raw=uint32_t(std::max(0,int(value)));
    return raw<=DEFAULT_RATE?strain_detail::rate_map(raw,DEFAULT_RATE,low):strain_detail::rate_map(raw-DEFAULT_RATE,32767-DEFAULT_RATE,high);
}
uint32_t StrainUnit::motion_increment(q15 value) {
    return uint32_t((uint64_t(rate_microhz(value))*4294967296ull+500000000ull)/1000000000ull);
}
int StrainUnit::trim_centidb(q15 value) {
    const int raw=std::max(0,int(value));
    return raw<=DEFAULT_TRIM?-1800+(raw*1800+DEFAULT_TRIM/2)/DEFAULT_TRIM:
        ((raw-DEFAULT_TRIM)*600+(32767-DEFAULT_TRIM)/2)/(32767-DEFAULT_TRIM);
}
q15 StrainUnit::control_value(int p) const {
    if(p<0||p>=num_params)return 0;
    const auto& a=params[p];
    return modern_&&std::abs(int(a.target)-int(a.effective))<=3?a.target:a.effective;
}
q15 StrainUnit::model_motion(q15 body_value,q15 amount_value,uint32_t phase,q15 energy) {
    const int32_t body=std::max(0,int(body_value)),amount=std::max(0,int(amount_value));
    const uint32_t ramp=phase&0x7fffffffu;
    const int32_t tri=(phase&0x80000000u)?32767-int32_t(ramp>>15):-32767+int32_t(ramp>>15);
    const int32_t autonomous=int32_t((int64_t(tri)*(2048+body/5))>>15);
    const int32_t reactive=(std::max(0,int(energy))-8192)*(4096+body/4)/32767;
    const int32_t motion=std::max<int32_t>(-16384,std::min<int32_t>(16384,autonomous+reactive));
    return q15(amount==32767?motion:int64_t(motion)*amount/32767);
}
void StrainUnit::set_modern(bool enabled) {
    if(enabled==modern_)return;
    modern_=enabled;num_params=enabled?P_COUNT:LEGACY_COUNT;params[P_BIAS].bipolar=true;
    // Keep the running feedback/filter memory. Explicit conversion changes only
    // the extended laws, bridged from the preceding output for128 samples.
    if(processed_){transition_from_=last_out_;transition_left_=128;}
}
StrainUnit::StrainUnit(bool modern) : modern_(modern) {
    kind = UnitKind::Strain;
    add_param("DRV",  15000);
    add_param("BIAS", 0, true);
    add_param("BODY", 15000);
    add_param("TEETH", 12000);
    add_param("FDBK", 9000);
    add_param("MIX",  24500);
    add_param("MOTION",32767);add_param("TRIM",DEFAULT_TRIM);add_param("RATE",DEFAULT_RATE);
    num_params=modern?P_COUNT:LEGACY_COUNT;
    if(!modern)for(int p=LEGACY_COUNT;p<P_COUNT;++p){const char* name=params[p].name;params[p]=Param{};params[p].name=name;}
    control_tick();
}

void StrainUnit::reset() {
    std::memset(feedback_delay_, 0, sizeof feedback_delay_);
    feedback_pos_ = 0;
    pre_low_ = loop_low_ = post_low_ = 0;
    loop_dc_x_ = loop_dc_y_ = wet_dc_x_ = wet_dc_y_ = 0;
    energy_follow_ = 0;
    motion_phase_ = 0;
    visual_energy_ = 0;
    visual_motion_ = 0;
    last_out_=transition_from_=0;transition_left_=0;processed_=false;
    control_tick();
}

void StrainUnit::control_tick() {
    const int32_t body = (uint16_t)control_value(P_BODY);
    // BODY sweeps a useful low-mid emphasis range. A slow triangle plus the
    // smoothed loop envelope displaces the pole in opposite directions, giving
    // both autonomous motion and input-dependent "talk" without randomness.
    motion_phase_ += modern_?motion_increment(control_value(P_RATE)):137439u;
    const q15 envelope=q15(std::min(32767,int(energy_follow_)));
    const q15 motion=model_motion(q15(body),modern_?control_value(P_MOTION):32767,motion_phase_,envelope);
    visual_motion_ = motion;

    int32_t moving_body = body + motion;
    if (moving_body < 0) moving_body = 0;
    if (moving_body > 32767) moving_body = 32767;
    // Coefficients remain below one-pole unity and cover progressively brighter
    // bands. The feedback pole stays darker than pre/post stages for stability.
    pre_coeff_ = (q15)(700 + moving_body * 12500 / 32767);
    loop_coeff_ = (q15)(350 + moving_body * 7200 / 32767);
    post_coeff_ = (q15)(1800 + moving_body * 10500 / 32767);
}

void StrainUnit::process(q15* buf, int frames) {
    const int32_t drive = (uint16_t)control_value(P_DRIVE);
    const q15 bias = control_value(P_BIAS);
    const int32_t body = (uint16_t)control_value(P_BODY);
    const int32_t teeth = (uint16_t)control_value(P_TEETH);
    const int32_t mix = (uint16_t)control_value(P_MIX);
    const int32_t motion_amount=modern_?std::max(0,int(control_value(P_MOTION))):32767;
    const int32_t trim=modern_?strain_detail::trim_gain(control_value(P_TRIM)):65536;
    const int32_t drive_gain_q15 = 32767 + drive * 7; // 1x..8x
    // Raw feedback tops out at 0.91. Drive compensation prevents the loop gain
    // from silently multiplying with DRIVE, while a small floor keeps high-drive
    // feedback audible instead of collapsing to almost zero.
    const int32_t feedback_control = (uint16_t)control_value(P_FEEDBACK);
    int32_t feedback_gain = (int32_t)((int64_t)feedback_control * 29800 * 32767 /
                                      ((int64_t)32767 * drive_gain_q15));
    const int32_t feedback_floor = feedback_control * 4200 / 32767;
    if (feedback_gain < feedback_floor) feedback_gain = feedback_floor;
    if (feedback_gain > 29800) feedback_gain = 29800;
    const int32_t bias_offset = (int32_t)(((int64_t)bias * (5000 + drive / 3)) >> 15);
    uint16_t peak = 0;

    for (int i = 0; i < frames; ++i) {
        const q15 dry = buf[i];
        const q15 delayed = feedback_delay_[feedback_pos_];

        // DC blocker is in the return path, before its moving lowpass. All hot
        // products are widened: extreme feedback and state values cannot invoke UB.
        const q31 loop_hp = (q31)delayed - loop_dc_x_ +
            (q31)(((int64_t)loop_dc_y_ * 32604) >> 15);
        loop_dc_x_ = delayed;
        loop_dc_y_ = sat_q15(loop_hp);
        const int32_t loop_abs = loop_dc_y_ < 0 ? -(int32_t)loop_dc_y_ : (int32_t)loop_dc_y_;
        energy_follow_ += loop_abs > energy_follow_
            ? (loop_abs - energy_follow_) >> 4
            : (loop_abs - energy_follow_) >> 9;
        int32_t dynamic_lift = loop_abs * 2200 >> 15;
        if(motion_amount!=32767)dynamic_lift=int32_t(int64_t(dynamic_lift)*motion_amount/32767);
        int32_t dynamic_loop_coeff = loop_coeff_ + dynamic_lift;
        if (dynamic_loop_coeff > 12000) dynamic_loop_coeff = 12000;
        loop_low_ += (q31)(((int64_t)(loop_dc_y_ - loop_low_) * dynamic_loop_coeff) >> 15);
        const q31 fed = (q31)(((int64_t)loop_low_ * feedback_gain) >> 15);

        // Parallel low/high body split: lows remain anchored, while mids/highs are
        // pre-emphasized into the nonlinear stages. BODY changes both pole and lift.
        pre_low_ += (q31)(((int64_t)((q31)dry - pre_low_) * pre_coeff_) >> 15);
        const q31 high = (q31)dry - pre_low_;
        const q31 emphasis = (q31)(((int64_t)high * (4096 + body * 3 / 2)) >> 14);
        const q31 input = (q31)dry + emphasis + fed;
        int64_t driven64 = ((int64_t)input * drive_gain_q15) >> 15;
        if (driven64 > INT32_MAX) driven64 = INT32_MAX;
        if (driven64 < INT32_MIN) driven64 = INT32_MIN;

        // Bias-centred soft saturation, baseline-subtracted to preserve silence.
        const q31 biased = (q31)driven64 + bias_offset;
        const q15 saturated = trackr::fx::soft_clip_q31(biased);
        const q15 zero = trackr::fx::soft_clip_q31(bias_offset);
        const q31 asymmetric = (q31)saturated - zero;

        // TEETH is not just a mix: it raises fold drive from 1x to 3x, then blends
        // reflection fold with saturation. Low TEETH stays rounded; high values bite.
        const int32_t teeth_gain = 32767 + teeth * 2;
        // Fold the bias baseline through the exact same transfer, not a soft-clip
        // approximation. This is what makes silence remain silence at every BIAS.
        const q31 fold_centred = (q31)strain_reflection_fold(biased, teeth_gain) -
                                 strain_reflection_fold(bias_offset, teeth_gain);
        q31 shaped = asymmetric +
            (q31)(((int64_t)(fold_centred - asymmetric) * teeth) >> 15);
        shaped = trackr::fx::soft_clip_q31(shaped);

        // Stable post body plus a TEETH-controlled edge. A second DC blocker sits
        // before both output and feedback write so asymmetric settings cannot leak DC.
        post_low_ += (q31)(((int64_t)(shaped - post_low_) * post_coeff_) >> 15);
        q31 wet = post_low_ +
            (q31)(((int64_t)(shaped - post_low_) * (4096 + teeth * 3 / 4)) >> 15);
        const q31 wet_hp = wet - wet_dc_x_ +
            (q31)(((int64_t)wet_dc_y_ * 32604) >> 15);
        wet_dc_x_ = wet;
        wet_dc_y_ = sat_q15(wet_hp);
        const q15 safe_wet = trackr::fx::soft_clip_q31(wet_dc_y_);
        feedback_delay_[feedback_pos_] = safe_wet;
        feedback_pos_ = (uint8_t)((feedback_pos_ + 1) & (CONTROL_INTERVAL - 1));

        // Output trim is deliberately outside the return tap. Dry level and
        // internal feedback/energy state never depend on this output control.
        const q15 trimmed=sat_q15(q31((int64_t(safe_wet)*trim)>>16));
        q15 out = mix == 0 ? dry : sat_q15((q31)dry +
            (q31)(((int64_t)((q31)trimmed - dry) * mix) >> 15));
        if(transition_left_){out=q15((int32_t(transition_from_)*transition_left_+int32_t(out)*(128-transition_left_))/128);--transition_left_;}
        buf[i]=last_out_=out;processed_=true;
        const int32_t magnitude = safe_wet == -32768 ? 32767 :
            safe_wet < 0 ? -safe_wet : safe_wet;
        if (magnitude > peak) peak = (uint16_t)magnitude;
    }
    // Fast attack, slower release makes the focus display readable between blocks.
    visual_energy_ = peak > visual_energy_ ? peak :
        (uint16_t)(visual_energy_ - (visual_energy_ >> 3));
}

// ============================ VCA ============================

VcaUnit::VcaUnit(bool modern) : modern_(modern) {
    kind = UnitKind::Vca;
    add_param("GAIN", Q15_ONE);
    add_param("ATK", vca_envelope::time_value(480));
    add_param("DEC", vca_envelope::time_value(8000));
    add_param("HOLD", 0);
    add_param("FLOOR", 0);
    add_param("A.CRV", 0, true);
    add_param("D.CRV", 0, true);
    add_param("DUCK", 0);
    add_param("SYNC", 0);
    add_param("RETRIG", 0);
    add_param("RUN", 0);
    num_params = modern ? P_COUNT : AHD_COUNT;
}

void VcaUnit::set_flexible(bool enabled) {
    if (flexible != enabled || modern_) reset();
    modern_ = false;
    flexible = enabled;
    num_params = enabled ? AHD_COUNT : LEGACY_COUNT;
}
void VcaUnit::set_modern(bool enabled) {
    if(enabled && !flexible) reset();
    if(enabled) flexible=true;
    modern_=enabled;
    num_params=enabled?P_COUNT:flexible?AHD_COUNT:LEGACY_COUNT;
    if(!enabled) {cycle_armed_=false;reset_blend_left_=0;}
}
void VcaUnit::set_clock_bpm_q16(int32_t value) { bpm_q16_=vca_timing::bpm(value); }
q15 VcaUnit::resolved(int p) const {
    return modern_?vca_timing::resolved(params[p],p>=P_SYNC):params[p].effective;
}

void VcaUnit::reset() {
    env_ = 0; env_active_ = false; rising_ = false;
    cycle_armed_=false;reset_from_=last_multiplier_=0;reset_blend_left_=0;
    for(auto& f:latched_frames_)f=0;
    stage_ = 0; phase_ = 0; phase_step_ = phase_rem_ = phase_error_ = 0;
    elapsed_ = stage_frames_ = 0; attack_origin_ = 0; exponent_ = 1048576;
    for (auto& ramp : live_) ramp = LiveRamp{};
    live_ready_ = false;
    for (auto& v : latched_) v = 0;
    for (auto& v : visual_effective_) v = 0;
    visual_env_ = visual_gain_ = 0;
    visual_state_ = visual_stage_ = 0; visual_progress_ = 0; visual_ready_ = false;
}

void VcaUnit::LiveRamp::aim(q15 to, bool snap) {
    if (snap) { value = (int32_t)to * 65536; target = to; remaining = 0; step = 0; }
    else if (to != target) {
        target = to; remaining = 64;
        step = ((int32_t)to * 65536 - value) / 64;
    }
}
q15 VcaUnit::LiveRamp::advance() {
    if (remaining) {
        value += step;
        if (!--remaining) value = (int32_t)target * 65536;
    }
    return (q15)((value + 32768) >> 16);
}
void VcaUnit::prepare_live() {
    const bool snap = !live_ready_;
    if (snap) {
        for (int p = 0; p < P_COUNT; ++p)
            if (p != P_ATTACK && p != P_A_CURVE)
                latched_[p] = params[p].bipolar
                    ? vca_envelope::bipolar(resolved(p))
                    : (q15)vca_envelope::unipolar(resolved(p));
        // An idle card has no attack latch yet.
        if (!stage_) {
            latched_[P_ATTACK] = (q15)vca_envelope::unipolar(resolved(P_ATTACK));
            latched_[P_A_CURVE] = vca_envelope::bipolar(resolved(P_A_CURVE));
        }
    }
    const bool sync=modern_ && vca_timing::enabled(resolved(P_SYNC));
    if(snap) {
        const int p[]={P_ATTACK,P_HOLD,P_DECAY};
        for(int i=0;i<3;++i)if(!latched_frames_[i])
            latched_frames_[i]=vca_timing::frames(latched_[p[i]],i==1,sync,bpm_q16_);
    }
    for(int p=P_SYNC;p<P_COUNT;++p)latched_[p]=modern_?resolved(p):0;
    live_[0].aim((q15)vca_envelope::unipolar(resolved(P_GAIN)), snap);
    live_[1].aim((q15)vca_envelope::unipolar(resolved(P_FLOOR)), snap);
    live_[2].aim(resolved(P_DUCK) >= 16384 ? 32767 : 0, snap);
    live_ready_ = true;
}
void VcaUnit::enter_stage(uint8_t stage) {
    stage_ = stage; phase_ = 0; elapsed_ = phase_error_ = 0;
    const int time_param = stage == 1 ? P_ATTACK : stage == 2 ? P_HOLD : P_DECAY;
    const q15 raw = (q15)vca_envelope::unipolar(resolved(time_param));
    latched_[time_param] = raw;
    stage_frames_ = vca_timing::frames(raw, stage==2,
        modern_ && vca_timing::enabled(resolved(P_SYNC)), bpm_q16_);
    latched_frames_[stage-1]=stage_frames_;
    if (stage == 2 && !stage_frames_) { enter_stage(3); return; }
    if (stage != 2) {
        const int cp = stage == 1 ? P_A_CURVE : P_D_CURVE;
        latched_[cp] = vca_envelope::bipolar(resolved(cp));
        exponent_ = vca_envelope::exponent_q20(stage == 1
            ? latched_[cp] : (q15)-latched_[cp]);
    }
    // One 32-bit division only at stage entry. Divide 2^32-1, then add the
    // missing unit to its remainder; no expensive 64-bit divide/mod pair.
    phase_step_ = UINT32_MAX / stage_frames_;
    phase_rem_ = UINT32_MAX - phase_step_ * stage_frames_ + 1u;
    if (phase_rem_ == stage_frames_) { ++phase_step_; phase_rem_ = 0; }
}

void VcaUnit::trigger() {
    if (flexible) {
        if(modern_) {
            const int retrigger=vca_timing::retrigger(resolved(P_RETRIGGER));
            const bool busy=stage_ && !(stage_==3 && elapsed_==stage_frames_);
            if(retrigger==2 && busy)return;
            if(retrigger==1) {
                env_=0;
                // Reset canonical E; bridge the audible multiplier over2ms.
                // No lookahead/latency or universal click-free guarantee.
                reset_from_=last_multiplier_;
                reset_blend_left_=visual_ready_?64:0;
            }
            cycle_armed_=vca_timing::enabled(resolved(P_RUN));
        }
        attack_origin_ = (q15)env_;
        enter_stage(1); // default soft: full attack from the current canonical E
        return;
    }
    env_active_ = true;
    rising_ = true;
    // instant attack when ATK=0
    if (params[P_ATTACK].effective < 128) { env_ = Q15_ONE; rising_ = false; }
    visual_env_ = (uint16_t)(env_ < 0 ? 0 : env_ > Q15_ONE ? Q15_ONE : env_);
    visual_state_ = VisualActive | (rising_ ? VisualRising : 0);
}

void VcaUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0) return;
    if (!flexible) { process_legacy(buf, frames); return; }
    prepare_live();
    const bool run=modern_ && vca_timing::enabled(resolved(P_RUN));
    if(!run)cycle_armed_=false;
    q15 actual_gain = 0, floor = 0, duck = 0, opened = 0, multiplier = 0;
    uint8_t sounded_stage = stage_;
    for (int i = 0; i < frames; ++i) {
        if (stage_ && elapsed_ == stage_frames_) {
            if (stage_ == 3) {
                if(cycle_armed_ && run) {env_=0;attack_origin_=0;enter_stage(1);}
                else stage_=0;
            } else enter_stage((uint8_t)(stage_ + 1));
        }
        sounded_stage = stage_;
        if (stage_) {
            ++elapsed_;
            phase_ += phase_step_;
            phase_error_ += phase_rem_;
            if (phase_error_ >= stage_frames_) { phase_error_ -= stage_frames_; ++phase_; }
            if (elapsed_ == stage_frames_) phase_ = 1ull << 32;
            if (stage_ == 1) env_ = attack_origin_ + vca_envelope::multiply(
                (q15)(32767 - attack_origin_), vca_envelope::shape(phase_, exponent_));
            else if (stage_ == 2) env_ = 32767;
            else env_ = vca_envelope::shape((1ull << 32) - phase_, exponent_);
        }
        actual_gain = live_[0].advance(); floor = live_[1].advance(); duck = live_[2].advance();
        // Crossfade E <-> (1-E) over 64 samples, including while idle. No hard
        // polarity toggle, no retrigger side effect, bounded to [floor, unity].
        const q15 directed = (q15)(((uint32_t)env_ * (32767u - (uint32_t)duck) +
            (uint32_t)(32767 - env_) * (uint32_t)duck + 16383u) / 32767u);
        opened = vca_envelope::opening(directed, floor, false);
        multiplier = vca_envelope::multiply(actual_gain, opened);
        if(modern_ && reset_blend_left_) {
            const int t=65-reset_blend_left_;
            multiplier=q15(std::min<int>(actual_gain,(int(reset_from_)*(64-t)+int(multiplier)*t+32)/64));
            --reset_blend_left_;
        }
        last_multiplier_=multiplier;
        const int32_t product = (int32_t)buf[i] * multiplier;
        buf[i] = (q15)((product + (product < 0 ? -16383 : 16383)) / 32767);
    }
    for (int p = 0; p < P_COUNT; ++p) visual_effective_[p] = latched_[p];
    visual_effective_[P_GAIN] = actual_gain;
    visual_effective_[P_FLOOR] = floor;
    visual_effective_[P_DUCK] = duck;
    visual_env_ = (uint16_t)opened; visual_gain_ = (uint16_t)multiplier;
    visual_stage_ = sounded_stage;
    visual_progress_ = sounded_stage ? (uint32_t)(phase_ >> 16) : 0;
    const bool active = sounded_stage && !(sounded_stage == 3 && elapsed_ == stage_frames_);
    visual_state_ = (active ? VisualActive : 0) | (sounded_stage == 1 ? VisualRising : 0) |
        (sounded_stage == 2 ? VisualHolding : 0) | (live_[2].target ? VisualDuck : 0);
    visual_ready_ = true;
}

void VcaUnit::process_legacy(q15* buf, int frames) {
    // Frozen historical arithmetic, including gain-only threshold and integer AD.
    const q15 gain = params[P_GAIN].effective;
    const bool env_on = params[P_ATTACK].effective >= 128 || params[P_DECAY].effective >= 128;

    if (!env_on) {
        for (int i = 0; i < frames; ++i)
            buf[i] = (q15)(((q31)buf[i] * gain) >> 15);
        visual_env_ = Q15_ONE;
        visual_state_ = VisualGainOnly;
        visual_gain_ = (uint16_t)gain;
        visual_stage_ = 0; visual_progress_ = 0; visual_ready_ = true;
        for (int p = 0; p < 3; ++p) visual_effective_[p] = params[p].effective;
        return;
    }

    // per-sample linear attack/decay. q15 time controls map quadratically to
    // 1..32000 frames, giving useful short chops and an audible long tail.
    int64_t av = params[P_ATTACK].effective;
    int64_t dv = params[P_DECAY].effective;
    uint32_t atk_frames = (uint32_t)(((av * av) >> 15) * 32000 >> 15) + 1;
    uint32_t dec_frames = (uint32_t)(((dv * dv) >> 15) * 32000 >> 15) + 1;
    q31 dec_step = (Q15_ONE + (q31)dec_frames - 1) / (q31)dec_frames;
    if (dec_step < 1) dec_step = 1;

    for (int i = 0; i < frames; ++i) {
        if (env_active_) {
            if (rising_) {
                env_ += Q15_ONE / (q31)atk_frames;
                if (env_ >= Q15_ONE) { env_ = Q15_ONE; rising_ = false; }
            } else {
                env_ -= dec_step;
                if (env_ <= 0) { env_ = 0; env_active_ = false; }
            }
        }
        q31 g = ((q31)gain * env_) >> 15;
        buf[i] = (q15)(((q31)buf[i] * g) >> 15);
    }
    visual_env_ = (uint16_t)(env_ < 0 ? 0 : env_ > Q15_ONE ? Q15_ONE : env_);
    visual_state_ = (env_active_ ? VisualActive : 0) | (rising_ ? VisualRising : 0);
    visual_gain_ = (uint16_t)(((q31)gain * env_) >> 15);
    visual_stage_ = env_active_ ? (rising_ ? 1 : 3) : 0;
    visual_progress_ = 0; visual_ready_ = true;
    for (int p = 0; p < 3; ++p) visual_effective_[p] = params[p].effective;
}

// ============================ DLY ============================

DelayUnit::DelayUnit(bool modern) : capacity_(modern ? MODERN_CAPACITY : MAX_DELAY), modern_(modern) {
    kind = UnitKind::Delay;
    add_param("TIME", 8192);
    add_param("FDBK", 12288);
    add_param("MIX",  10922);
    add_param("WOW",  2048);
    add_param("SAT",  8192);
    add_param("HOLD", 0);
    add_param("TONE", 18000);
    add_param("SYNC", 0);
    add_param("DIV", delay_time::division_value(3));
    add_param("FEEL", delay_time::feel_value(0));
    add_param("MODE", 0);
    if (modern_) { params[P_TIME].base = delay_time::free_value(8000); params[P_TIME].snap(); }
    else {
        num_params = 6;
        for (int p = 6; p < PARAM_BANK_SIZE; ++p) {
            params[p].base = 0; params[p].snap();
        }
    }
    buf_ = new(std::nothrow) q15[capacity_];
    if (buf_) std::memset(buf_, 0, capacity_ * sizeof(q15));
    if (modern_) control_tick();
}

DelayUnit::~DelayUnit() { delete[] buf_; }

void DelayUnit::reset() {
    if (buf_) std::memset(buf_, 0, capacity_ * sizeof(q15));
    write_ = 0;
    if (!modern_) delay_cur_q16_ = (int32_t)delay_frames_ << 16;
    read_a_q8_ = read_b_q8_ = delay_frames_ << 8;
    visual_read_a_q8_ = visual_read_b_q8_ = 0;
    valid_frames_ = 0;
    legacy_processed_ = false;
    modern_ready_ = crossfading_ = clear_pending_ = false;
    crossfade_pos_ = hold_pos_ = clear_pos_ = clear_stage_ = 0;
    hold_amount_ = hold_origin_ = hold_target_ = 0;
    wow_phase_ = 0;
    feedback_lp_ = 0;
    visual_delay_frames_ = (uint16_t)std::min<uint32_t>(delay_frames_, 65535u);
    visual_wow_offset_ = 0;
    visual_wet_peak_ = 0;
    visual_hold_ = false;
}

bool DelayUnit::set_modern(bool modern) {
    const uint32_t wanted = modern ? MODERN_CAPACITY : MAX_DELAY;
    if (modern_ == modern && buf_) return true;
    // Allocate before committing any scalar or parameter mode changes.
    q15* replacement = new(std::nothrow) q15[wanted];
    if (!replacement) return false;
    std::memset(replacement, 0, wanted * sizeof(q15));
    delete[] buf_; buf_ = replacement; capacity_ = wanted; modern_ = modern;
    num_params = modern ? P_COUNT : 6;
    time_cache_ = tone_cache_ = -1;
    control_tick(); reset();
    return true;
}

void DelayUnit::refresh_time() {
    if (sync_) delay_frames_ = delay_time::sync_frames(division_, feel_, bpm_q16_, &limited_);
    else {
        limited_ = false;
        // Clean time requests bypass the common param slew: every transition
        // latches a destination, then coalesces incoming changes for the next.
        const q15 raw = clean_ ? params[P_TIME].target : params[P_TIME].effective;
        if (time_cache_ != raw) {
            time_cache_ = raw;
            delay_frames_ = delay_time::free_frames(raw);
        }
    }
}
void DelayUnit::set_clock_bpm_q16(int32_t bpm_q16) {
    bpm_q16_ = bpm_q16 > 0 ? bpm_q16 : 1;
    if (modern_ && sync_) refresh_time();
}
void DelayUnit::control_tick() {
    if (!modern_) {
        // Frozen legacy linear mapping (including its historical endpoint).
        delay_frames_ = 32 + (uint32_t)(((uint64_t)(uint16_t)params[P_TIME].effective * (MAX_DELAY - 33)) >> 15);
        return;
    }
    const bool sync = params[P_SYNC].target > 16384;
    if (sync != sync_) time_cache_ = -1;
    sync_ = sync; clean_ = params[P_MODE].target > 16384;
    division_ = params[P_DIV].target; feel_ = params[P_FEEL].target;
    refresh_time();
    if (tone_cache_ != params[P_TONE].effective) {
        tone_cache_ = params[P_TONE].effective;
        const double hz = delay_time::tone_hz((q15)tone_cache_);
        tone_alpha_q15_ = (int32_t)((1.0 - std::exp(-6.283185307179586 * hz / ENGINE_SR)) * 32768 + 0.5);
    }
}
void DelayUnit::request_clear() {
    if (modern_ && !clear_stage_) clear_pending_ = true;
}
DelayUnit::VisualState DelayUnit::visual_state() const {
    VisualState state;
    state.ready = modern_ ? modern_ready_ : legacy_processed_;
    state.sync = modern_ && sync_; state.clean = modern_ && clean_;
    state.crossfading = modern_ && crossfading_;
    state.clearing = modern_ && (clear_pending_ || clear_stage_);
    state.limited = modern_ && limited_; state.target_frames = delay_frames_;
    state.read_a_q8 = modern_ ? visual_read_a_q8_ : (uint32_t)visual_delay_frames_ << 8;
    state.read_b_q8 = modern_ ? visual_read_b_q8_ : state.read_a_q8;
    state.bpm_q16 = bpm_q16_; state.hold_amount = modern_ ? hold_amount_ : (visual_hold_ ? 32767 : 0);
    state.crossfade_q15 = crossfading_ ? (q15)((uint32_t)crossfade_pos_ * 32767 / CROSSFADE_FRAMES) : 0;
    return state;
}
void DelayUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0 || !buf_) return;
    if (modern_) process_modern(buf, frames);
    else process_legacy(buf, frames);
}

// Frozen original six-parameter DSP. Do not fold modern behavior into this path.
void DelayUnit::process_legacy(q15* buf, int frames) {
    if (!buf_) return;
    const q15 fb  = params[P_FEEDBACK].effective;
    const q15 mix = params[P_MIX].effective;
    const q15 wow = params[P_WOW].effective;
    const q15 sat = params[P_SAT].effective;
    const bool hold = params[P_HOLD].effective > 16384;
    const int32_t target_q16 = (int32_t)delay_frames_ << 16;
    uint16_t wet_peak = 0;
    int32_t last_mod_delay_q16 = delay_cur_q16_;
    int32_t last_wobble_q16 = 0;

    for (int i = 0; i < frames; ++i) {
        // Slew delay time at audio rate; fractional read head avoids zipper/crackle
        // when TIME is modulated by a sigil.
        delay_cur_q16_ += (target_q16 - delay_cur_q16_) >> 7;
        // Slow triangle wow plus faster low-level flutter. Both modulate the
        // fractional read head without moving or reallocating the delay buffer.
        wow_phase_ += 977u + ((uint32_t)(uint16_t)wow >> 5);
        int32_t tri = (wow_phase_ & 0x80000000u)
            ? 32767 - (int32_t)((wow_phase_ >> 15) & 0xffffu)
            : (int32_t)((wow_phase_ >> 15) & 0xffffu) - 32768;
        int32_t flutter = (int32_t)((wow_phase_ * 17u) >> 24) - 128;
        int32_t wobble_q16 = (int32_t)(((int64_t)(tri + flutter * 32) * wow * (64 << 16)) >> 30);
        int32_t mod_delay_q16 = delay_cur_q16_ + wobble_q16;
        last_mod_delay_q16 = mod_delay_q16;
        last_wobble_q16 = wobble_q16;
        int rd_int = mod_delay_q16 >> 16;

        if (rd_int < 1) rd_int = 1;
        if (rd_int > MAX_DELAY - 2) rd_int = MAX_DELAY - 2;
        uint16_t frac = (uint16_t)(mod_delay_q16 & 0xFFFF);


        uint32_t rd0 = (write_ + MAX_DELAY - (uint32_t)rd_int) % MAX_DELAY;
        uint32_t rd1 = (rd0 + MAX_DELAY - 1) % MAX_DELAY;
        q15 wet = lerp_q15(buf_[rd0], buf_[rd1], frac);
        const int32_t wet_abs = wet < 0 ? -(int32_t)wet : (int32_t)wet;
        if (wet_abs > wet_peak) wet_peak = (uint16_t)wet_abs;

        // Tape-like darkening and soft feedback saturation. HOLD recirculates
        // the existing tape loop without injecting new dry signal.
        feedback_lp_ = (q15)(feedback_lp_ + ((int32_t)wet - feedback_lp_) / 4);
        q31 feedback = ((q31)feedback_lp_ * (hold ? 32600 : fb)) >> 15;
        q31 drive = 32767 + ((q31)sat * 3);
        q31 into = hold ? feedback : (q31)buf[i] + feedback;
        const int64_t driven = ((int64_t)into * drive) >> 15;
        buf_[write_] = trackr::fx::soft_clip_q31(
            driven < INT32_MIN ? INT32_MIN : driven > INT32_MAX ? INT32_MAX : (q31)driven);

        write_ = (write_ + 1) % MAX_DELAY;
        // equal-ish mix
        q31 outv = (q31)buf[i] + (((q31)wet - buf[i]) * mix >> 15);
        buf[i] = sat_q15(outv);
    }
    int32_t visual_frames = last_mod_delay_q16 >> 16;
    if (visual_frames < 1) visual_frames = 1;
    if (visual_frames > MAX_DELAY - 2) visual_frames = MAX_DELAY - 2;
    visual_delay_frames_ = (uint16_t)visual_frames;
    int32_t wow_frames = last_wobble_q16 >> 16;
    if (wow_frames < -32767) wow_frames = -32767;
    if (wow_frames > 32767) wow_frames = 32767;
    visual_wow_offset_ = (int16_t)wow_frames;
    const uint16_t decayed_peak = (uint16_t)((uint32_t)visual_wet_peak_ * 15 / 16);
    visual_wet_peak_ = wet_peak > decayed_peak ? wet_peak : decayed_peak;
    visual_hold_ = hold;
    legacy_processed_ = true;
}

q15 DelayUnit::read_modern(uint32_t delay_q8) const {
    const uint32_t age = delay_q8 >> 8, frac = delay_q8 & 255u;
    const uint32_t first = write_ >= age ? write_ - age : write_ + MODERN_CAPACITY - age;
    const uint32_t second = first ? first - 1 : MODERN_CAPACITY - 1;
    const int32_t a = age <= valid_frames_ ? buf_[first] : 0;
    const int32_t b = age + 1 <= valid_frames_ ? buf_[second] : 0;
    return (q15)(a + ((b - a) * (int32_t)frac >> 8));
}
void DelayUnit::process_modern(q15* buf, int frames) {
    const int32_t fb = std::max(0, (int)params[P_FEEDBACK].effective);
    const int32_t mix = std::max(0, (int)params[P_MIX].effective);
    const int32_t wow = std::max(0, (int)params[P_WOW].effective);
    const int32_t drive = 32767 + std::max(0, (int)params[P_SAT].effective) * 3;
    const q15 hold = params[P_HOLD].target > 16384 ? 32767 : 0;
    if (hold != hold_target_) { hold_origin_ = hold_amount_; hold_target_ = hold; hold_pos_ = 0; }
    if (!modern_ready_) {
        read_a_q8_ = read_b_q8_ = delay_frames_ << 8;
        modern_ready_ = true;
    }
    if (clear_pending_) { clear_pending_ = false; clear_stage_ = 1; clear_pos_ = 0; }
    uint16_t peak = 0;
    for (int i = 0; i < frames; ++i) {
        const uint32_t target = delay_frames_ << 8;
        // Finish any already latched crossfade even when the mode changes. The
        // tape path resumes from its arrived head, never from a reset/jump.
        if (!crossfading_) {
            if (clean_ && read_a_q8_ != target) {
                read_b_q8_ = target; crossfade_pos_ = 0; crossfading_ = true;
            } else if (!clean_) {
                const int32_t delta = (int32_t)target - (int32_t)read_a_q8_;
                int32_t step = delta / 128;
                if (!step && delta) step = delta > 0 ? 1 : -1;
                read_a_q8_ = (uint32_t)((int32_t)read_a_q8_ + step);
                read_b_q8_ = read_a_q8_;
            }
        }
        wow_phase_ += 977u + ((uint32_t)wow >> 5);
        const int32_t tri = (wow_phase_ & 0x80000000u)
            ? 32767 - (int32_t)((wow_phase_ >> 15) & 0xffffu)
            : (int32_t)((wow_phase_ >> 15) & 0xffffu) - 32768;
        const int32_t flutter = (int32_t)((wow_phase_ * 17u) >> 24) - 128;
        const int32_t wobble = (int32_t)(((int64_t)(tri + flutter * 32) * wow * (64 << 8)) >> 30);
        const auto clamped = [wobble](uint32_t delay) {
            const int32_t read = (int32_t)delay + wobble;
            return (uint32_t)std::max<int32_t>(256, std::min(read, (int32_t)(delay_time::MAX_FRAMES << 8)));
        };
        visual_read_a_q8_ = clamped(read_a_q8_);
        visual_read_b_q8_ = clamped(crossfading_ ? read_b_q8_ : read_a_q8_);
        int32_t wet = read_modern(visual_read_a_q8_);
        if (crossfading_) {
            const int32_t incoming = read_modern(visual_read_b_q8_);
            ++crossfade_pos_;
            wet += (incoming - wet) * crossfade_pos_ / CROSSFADE_FRAMES;
            if (crossfade_pos_ == CROSSFADE_FRAMES) {
                read_a_q8_ = read_b_q8_; crossfading_ = false;
                visual_read_a_q8_ = visual_read_b_q8_;
            }
        }
        if (hold_pos_ < RAMP_FRAMES) {
            ++hold_pos_;
            hold_amount_ = (q15)(hold_origin_ + ((int32_t)hold_target_ - hold_origin_) * hold_pos_ / RAMP_FRAMES);
        }
        int32_t clear_gain = 32767;
        if (clear_stage_ == 1) {
            ++clear_pos_;
            clear_gain = 32767 * (RAMP_FRAMES - clear_pos_) / RAMP_FRAMES;
            if (clear_pos_ == RAMP_FRAMES) {
                // All old ring samples become stale in constant time. Current
                // dry input may begin a new tail; no pre-clear LP/history leaks.
                valid_frames_ = 0; feedback_lp_ = 0; wet = 0;
                clear_stage_ = 2; clear_pos_ = 0;
            }
        } else if (clear_stage_ == 2) {
            clear_gain = 32767 * ++clear_pos_ / RAMP_FRAMES;
            if (clear_pos_ == RAMP_FRAMES) { clear_stage_ = 0; clear_pos_ = 0; }
        }
        feedback_lp_ = (q15)(feedback_lp_ + (((int32_t)wet - feedback_lp_) * tone_alpha_q15_ / 32768));
        const int32_t feedback_gain = fb + (32600 - fb) * hold_amount_ / 32767;
        const int32_t feedback = (int32_t)feedback_lp_ * feedback_gain >> 15;
        const int32_t into = (int32_t)buf[i] * (32767 - hold_amount_) / 32767 + feedback;
        const int64_t driven = ((int64_t)into * drive) >> 15;
        buf_[write_] = trackr::fx::soft_clip_q31(
            driven < INT32_MIN ? INT32_MIN : driven > INT32_MAX ? INT32_MAX : (q31)driven);
        if (++write_ == MODERN_CAPACITY) write_ = 0;
        if (valid_frames_ < MODERN_CAPACITY) ++valid_frames_;
        wet = wet * clear_gain / 32767;
        const int32_t magnitude = wet < 0 ? -wet : wet;
        if (magnitude > peak) peak = (uint16_t)magnitude;
        const int32_t output = buf[i] + (int32_t)(((int64_t)wet - buf[i]) * mix / 32767);
        buf[i] = sat_q15(output);
        visual_wow_offset_ = (int16_t)(wobble / 256);
    }
    visual_delay_frames_ = (uint16_t)std::min<uint32_t>(visual_read_a_q8_ >> 8, 65535u);
    visual_wet_peak_ = peak; // actual final block; no block-dependent DSP state
    visual_hold_ = hold_amount_ > 16384;
}

// ============================ FOLD ============================

#include "fold_impl.inc"

// ============================ mic in ============================

q15      MicUnit::ring_[MicUnit::RING];
uint32_t MicUnit::wr_ = 0;
uint32_t MicUnit::epoch_ = 1;
uint32_t MicUnit::input_serial_ = 0;
uint16_t MicUnit::input_valid_ = 0;

MicUnit::MicUnit() {
    kind = UnitKind::MicIn;
    add_param("GAIN", 8192);        // 8192 = unity, up to 4x boost
    add_param("MON",  Q15_ONE);     // legacy source open/mute (>half = open)
    add_param("CUT", 0);            // new and migrated cards start sample-exact OFF
    read_q16_ = (uint64_t)wr_ << 16;
    epoch_seen_ = epoch_;
}

int MicUnit::low_cut_mode(q15 raw) {
    return raw <= 0 ? 0 : ((int32_t)raw * 3) / 32768;
}

void MicUnit::push(const q15* src, int n) {
    if (!src || n <= 0) return;
    for (int i = 0; i < n; ++i) ring_[wr_++ & (RING - 1)] = src[i];
    input_valid_ = n >= (int)(RING - input_valid_) ? (uint16_t)RING
        : (uint16_t)(input_valid_ + n);
    ++input_serial_;
}

int MicUnit::copy_input(q15* dst, int capacity) {
    if (!dst || capacity <= 0) return 0;
    int count = capacity < INPUT_VISUAL_SAMPLES ? capacity : INPUT_VISUAL_SAMPLES;
    if (count > input_valid_) count = input_valid_;
    const uint32_t start = wr_ - (uint32_t)count;
    for (int i = 0; i < count; ++i) dst[i] = ring_[(start + (uint32_t)i) & (RING - 1)];
    return count;
}

void MicUnit::reset_input() {
    wr_ = 0;
    input_valid_ = 0;
    ++input_serial_;
    if (++epoch_ == 0) ++epoch_;
    std::memset(ring_, 0, sizeof ring_);
}

void MicUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0) return;
    ++process_serial_;
    int32_t gain_value = params[P_GAIN].effective;
    if (gain_value < 0) gain_value = 0;
    const q31 gain = gain_value;              // 0..32767, 8192=1x
    const bool mon = params[P_MON].effective > 16384;
    const int cut_mode = low_cut_mode(params[P_LOW_CUT].effective);
    // Bilinear first-order HP at 32kHz: k=2*tan(pi*f/SR)/(1+tan(pi*f/SR)),
    // Q24 rounded constants. H(z)=(1-k/2)*(1-z^-1)/(1-(1-k)*z^-1):
    // exact DC zero, nominal -3dB corner, unity at Nyquist. One wide multiply.
    const int32_t coefficient = cut_mode == 2 ? 518963 : 261488;
    // OFF bypasses ALL filter arithmetic and clears its history: immediate exact
    // legacy samples. Enabling starts the stable natural step response from zero;
    // 80<->160 retains the same bounded state, as does source mute. No stale-state
    // reuse on re-enable/device reset, no gain/gate and no click-free-off claim.
    if (!cut_mode) low_state_q8_ = 0;
    if (!mon) clip_hold_samples_ = 0;
    if (epoch_seen_ != epoch_) {
        low_state_q8_ = 0;
        visual_peak_ = 0;
        visual_output_peak_ = 0;
        clip_hold_samples_ = 0;
        const uint32_t keep = wr_ < 512u ? wr_ : 512u;
        read_q16_ = (uint64_t)(wr_ - keep) << 16;
        epoch_seen_ = epoch_;
    }

    uint32_t read_frame = (uint32_t)(read_q16_ >> 16);
    uint32_t avail = wr_ - read_frame;
    // A stale reader (newly resumed app, long mute, or producer wrap) jumps to a
    // bounded 16ms backlog. Normal clock mismatch is handled continuously by the
    // fractional 32730->32000 step below, never by deleting audible chunks.
    constexpr uint32_t TARGET = 512;
    if (avail > RING) {
        const uint32_t keep = wr_ < TARGET ? wr_ : TARGET;
        visual_dropped_ += avail - keep;
        read_frame = wr_ - keep;
        read_q16_ = (uint64_t)read_frame << 16;
        avail = keep;
    }

    int32_t peak = 0;
    uint16_t output_peak = 0;
    for (int i = 0; i < frames; ++i) {
        q15 sample = 0;
        read_frame = (uint32_t)(read_q16_ >> 16);
        if (wr_ - read_frame >= 2) {
            const q15 a = ring_[read_frame & (RING - 1)];
            const q15 b = ring_[(read_frame + 1) & (RING - 1)];
            sample = lerp_q15(a, b, (uint16_t)read_q16_);
            read_q16_ += INPUT_STEP_Q16;
        }
        const int32_t magnitude = sample == -32768 ? 32767 : (sample < 0 ? -sample : sample);
        if (magnitude > peak) peak = magnitude;
        int32_t filtered = sample;
        if (cut_mode) {
            const int32_t difference = (int32_t)sample * 256 - low_state_q8_;
            const int32_t delta = (int32_t)(((int64_t)difference * coefficient) >> 24);
            low_state_q8_ += delta;
            filtered = (difference - delta / 2) >> 8;
        }
        // Retain headroom through cut + gain; only this final conversion clips.
        // |filtered|<65536 and gain<=32767 keep this product inside signed q31.
        const int32_t amplified = mon ? ((q31)filtered * gain) >> 13 : 0;
        const bool clipped = mon && (amplified > 32767 || amplified < -32768);
        if (clipped) clip_hold_samples_ = ENGINE_SR / 2; // 500ms by audio samples
        else if (clip_hold_samples_) --clip_hold_samples_;
        buf[i] = sat_q15(amplified);
        const uint16_t out_magnitude = buf[i] == -32768 ? 32768u
            : (uint16_t)(buf[i] < 0 ? -buf[i] : buf[i]);
        if (out_magnitude > output_peak) output_peak = out_magnitude;
    }
    visual_output_peak_ = output_peak;
    if (peak > visual_peak_) visual_peak_ = (q15)peak;
    else {
        const int32_t difference = visual_peak_ - peak;
        visual_peak_ = difference < 8 ? (q15)peak
            : (q15)(visual_peak_ - (difference >> 3));
    }
    read_frame = (uint32_t)(read_q16_ >> 16);
    const uint32_t remaining = wr_ - read_frame;
    visual_fill_ = (uint16_t)(remaining > RING ? RING : remaining);
}

// ============================ recorder ============================

static int32_t rec_wave_magnitude(q15 value) {
    return value < 0 ? -(int32_t)value : (int32_t)value;
}

static q15 rec_wave_pcm(const Sample& sample, uint32_t frame) {
#ifdef SEANCE_REC_WAVE_CACHE_TEST
    extern void rec_wave_cache_test_read(bool summary);
    rec_wave_cache_test_read(false);
#endif
    return sample.data[(size_t)frame * sample.channels];
}

void RecUnit::invalidate_visual_wave() const {
    wave_.slot = -1;
    wave_.streaming = false;
    wave_.streamed_frames = 0;
    wave_.displayed_frames = UINT32_MAX;
    wave_.reduced_per_bin = 0;
    wave_.ready = false;
}

void RecUnit::bind_visual_wave(const Sample& sample) const {
    const uint32_t generation = SampleBank::instance().generation(sample_slot);
    if (wave_.slot == sample_slot && wave_.bank_generation == generation &&
        wave_.data == sample.data.data() && wave_.elements == sample.data.size() &&
        wave_.channels == sample.channels) return;
    invalidate_visual_wave();
    wave_.slot = sample_slot;
    wave_.bank_generation = generation;
    wave_.data = sample.data.data();
    wave_.elements = sample.data.size();
    wave_.channels = sample.channels;
    wave_.streaming = wr_frames_ == 0 && sample.channels == 1;
}

void RecUnit::copy_visual_wave(q15* out) const {
    if (!out) return;
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) {
        invalidate_visual_wave();
        std::memset(out, 0, WAVE_POINTS * sizeof(q15));
        return;
    }
    const Sample& sample = SampleBank::instance().slot(sample_slot);
    bind_visual_wave(sample);
    const uint32_t frames = wr_frames_;
    const uint32_t physical = sample.num_frames();
    if (wave_.displayed_frames != frames) {
        wave_.displayed_frames = frames;
        wave_.reduced_per_bin = 0;
        wave_.ready = false;
        std::memset(wave_.points, 0, sizeof wave_.points);
    }
    if (!wave_.ready) {
        // Captured blocks are chronological signed maxima, retaining the first
        // equal-magnitude sample. Query only full blocks; scan their edges so
        // every moving full-duration UI bin remains EXACT, not fixed-time bins.
        // 64 * 2 * 255 = 32640 maximum edge PCM reads + <=2500 summaries.
        const bool streamed = wave_.streaming && wave_.streamed_frames == frames &&
                              frames <= MAX_REC_FRAMES;
        bool complete = true;
        for (int point = 0; point < WAVE_POINTS; ++point) {
            if (!frames || !physical) { wave_.points[point] = 0; continue; }
            uint32_t first = (uint32_t)((uint64_t)point * frames / WAVE_POINTS);
            uint32_t end = (uint32_t)((uint64_t)(point + 1) * frames / WAVE_POINTS);
            if (end <= first) end = first + 1;
            if (end > physical) end = physical;
            if (first >= end) { wave_.points[point] = 0; continue; }
            q15 strongest = streamed ? 0 : wave_.points[point];
            auto include = [&strongest](q15 value) {
                if (rec_wave_magnitude(value) > rec_wave_magnitude(strongest)) strongest = value;
            };
            if (streamed) {
                while (first < end && first % WAVE_BLOCK)
                    include(rec_wave_pcm(sample, first++));
                while (end - first >= WAVE_BLOCK) {
#ifdef SEANCE_REC_WAVE_CACHE_TEST
                    extern void rec_wave_cache_test_read(bool summary);
                    rec_wave_cache_test_read(true);
#endif
                    include(wave_.blocks[first / WAVE_BLOCK]);
                    first += WAVE_BLOCK;
                }
                while (first < end) include(rec_wave_pcm(sample, first++));
            } else {
                // Restore/adopt/replacement never rebuilds under its guard.
                // Each focused snapshot examines at most 512 new frames in
                // EACH bin (32768 PCM total), showing truthful partial peaks.
                // A <=20s restored take is exact after <=20 publications (~320ms
                // at 512/32k); longer imported takes converge without a spike.
                const uint32_t length = end - first;
                const uint32_t offset = std::min(wave_.reduced_per_bin, length);
                first += offset;
                const uint32_t count = std::min<uint32_t>(512, end - first);
                const uint32_t limit = first + count;
                while (first < limit) include(rec_wave_pcm(sample, first++));
                if (first < end) complete = false;
            }
            wave_.points[point] = strongest;
        }
        if (!streamed && !complete) wave_.reduced_per_bin += 512;
        wave_.ready = complete;
    }
    // An unchanged finalized take performs no PCM or block-summary reads.
    std::memcpy(out, wave_.points, sizeof wave_.points);
}

// Construction/staging may happen outside the audio guard. Only the 32-bit
// lifetime issuer is shared; lock-free on ARM11. Per-unit transitions are owned
// by the engine guard, avoiding a 64-bit libatomic dependency on 3DS.
static std::atomic<uint32_t> rec_identity_issuer{0};
static_assert(ATOMIC_INT_LOCK_FREE == 2, "REC lifetime identity needs lock-free 32-bit atomics");
static uint64_t rec_next_lifetime() {
    uint32_t identity;
    do { identity = rec_identity_issuer.fetch_add(1, std::memory_order_relaxed) + 1u; } while (!identity);
    return (uint64_t)identity << 32;
}

void RecUnit::renew_session() {
    session_token_ = ++state_token_;
}

RecUnit::RecUnit() {
    state_token_ = rec_next_lifetime();
    renew_session();
    kind = UnitKind::Recorder;
    add_param("REC",  0);          // >half records; trigger() also starts a fresh take
    add_param("LEN",  8192);       // max take length: ~1s..20s
    add_param("MON",  Q15_ONE);    // pass-through monitor (>half = on)
}

void RecUnit::reset() {
    if (writer_open()) return; // generic source resets cannot abort a reserved take
    invalidate_visual_wave();
    renew_session();
    recording_ = paused_ = false;
    gate_started_ = false;
    wr_frames_ = target_frames_ = 0;
}

uint32_t RecUnit::capacity_frames() const {
    int slot = sample_slot;
    if (slot < 0) slot = 0;
    slot %= trackr::synth::SAMPLE_BANK_SIZE;
    const Sample& s = SampleBank::instance().slot(slot);
    return s.channels ? (uint32_t)(s.data.size() / s.channels) : 0;
}

uint32_t RecUnit::target_frames() const {
    // LEN is the exact fixed take duration. It is sampled on trigger so
    // modulation cannot resize/change a take while the audio thread writes it.
    int32_t value = params[P_LEN].effective;
    if (value < 0) value = 0;
    uint32_t frames = 1024 + (uint32_t)(((uint64_t)value * (MAX_REC_FRAMES - 1024)) >> 15);
    const uint32_t cap = capacity_frames();
    if (frames > cap) frames = cap;
    return frames;
}

uint32_t RecUnit::required_capacity_frames() const {
    int32_t value = params[P_LEN].base;
    if (value < 0) value = 0;
    uint32_t max_len = 1024 + (uint32_t)(((uint64_t)value * (MAX_REC_FRAMES - 1024)) >> 15);
    return max_len > MAX_REC_FRAMES ? MAX_REC_FRAMES : max_len;
}

trackr::synth::SampleEditResult RecUnit::stage_workspace(uint32_t frames, Sample& prepared) {
    using trackr::synth::SampleEditResult;
    if (!frames || frames > MAX_REC_FRAMES) return SampleEditResult::InvalidSlot;
    prepared = {};
    prepared.channels = 1;
    prepared.root_note = 60;
    prepared.loop_start = 0;
    prepared.loop_end = 0;
    prepared.reversed = false;
    prepared.logical_frames = 0;
    prepared.data.resize(frames);
    return SampleEditResult::Ok;
}

void RecUnit::adopt_workspace() {
    if (writer_open()) return;
    invalidate_visual_wave();
    renew_session();
    recording_ = paused_ = false;
    gate_started_ = false;
    wr_frames_ = 0;
    target_frames_ = 0;
    take_generation_ = 0;
    saved_generation_ = 0;
}

trackr::synth::SampleEditResult RecUnit::prepare_slot() {
    using trackr::synth::SampleEditResult;
    if (writer_open()) return SampleEditResult::Busy;
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE)
        return SampleEditResult::InvalidSlot;
    SampleBank& bank = SampleBank::instance();
    const uint32_t max_len = required_capacity_frames();
    if (!bank.can_replace(sample_slot, max_len)) return SampleEditResult::MemoryBudget;

    Sample prepared;
    const auto staged = stage_workspace(max_len, prepared);
    if (staged != SampleEditResult::Ok) return staged;
    const auto result = bank.replace_sample(sample_slot, std::move(prepared),
                                            trackr::synth::SampleOrigin::Runtime, "");
    if (result == SampleEditResult::Ok) adopt_workspace();
    return result;
}

void RecUnit::trigger() {
    // A finalized take is immutable until the controller explicitly clears it.
    // This prevents a stray REC press or stale quantized command from destroying
    // unsaved PCM before the user has confirmed RETAKE.
    if (!can_start_fresh()) return; // A/sequence cannot resume OR stop an open take
    if (sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE) return;
    Sample& s = SampleBank::instance().slot(sample_slot);
    if (!s.has_storage() || s.channels != 1 || s.logical_frames != 0) {
        recording_ = false;
        gate_started_ = false;
        return;
    }
    s.loop_start = 0;
    s.loop_end = 0;
    s.logical_frames = 0;
    s.slice_reverse_mask = 0;
    s.reset_cut_speeds();
    for (int i = 0; i < Sample::MAX_CHOPS; ++i) s.chops[i] = 0xFFFFFFFFu;
    wr_frames_ = 0;
    invalidate_visual_wave();
    target_frames_ = target_frames();
    if (!target_frames_) { recording_ = false; gate_started_ = false; return; }
    // New native-rate recordings must retain their final PCM frame on SNC/WAV
    // reload. Use the existing per-sample flag only at accepted fresh start;
    // restored/shared old takes are not migrated by restore, adopt or stop.
    s.exact_native_endpoint = true;
    if (++take_generation_ == 0) ++take_generation_;
    SampleBank::instance().set_provenance(sample_slot, trackr::synth::SampleOrigin::Runtime, "");
    renew_session();
    writer_slot_ = sample_slot;
    writer_generation_ = SampleBank::instance().generation(sample_slot);
    writer_capacity_ = s.num_frames();
    writer_data_ = s.data.data();
    recording_ = true;
    paused_ = false;
    gate_started_ = params[P_REC].effective > (Q15_ONE / 2);
}

bool RecUnit::writer_storage_matches() const {
    if (sample_slot != writer_slot_ || sample_slot < 0 || sample_slot >= trackr::synth::SAMPLE_BANK_SIZE)
        return false;
    const Sample& s = SampleBank::instance().slot(sample_slot);
    return SampleBank::instance().generation(sample_slot) == writer_generation_ &&
        s.channels == 1 && s.data.data() == writer_data_ && s.num_frames() == writer_capacity_ &&
        s.logical_frames == wr_frames_;
}

bool RecUnit::pause() {
    if (!recording_ || !writer_storage_matches()) return false;
    recording_ = false; paused_ = true;
    ++state_token_;
    return true;
}

bool RecUnit::resume() {
    if (!paused_ || !writer_storage_matches() || wr_frames_ >= target_frames_ || wr_frames_ >= writer_capacity_)
        return false;
    paused_ = false; recording_ = true;
    ++state_token_;
    return true;
}

void RecUnit::stop() {
    if (!writer_open()) return; // repeated stop never rewrites restored/shared edits
    const bool same_storage = writer_storage_matches();
    recording_ = paused_ = false;
    ++state_token_;
    gate_started_ = false;
    params[P_REC].base = 0;
    params[P_REC].effective = 0;
    if (!same_storage) {
        // The old PCM is already gone/rebound. Never advertise unrelated new PCM
        // through the old take's counters or mutate that replacement's metadata.
        wr_frames_ = target_frames_ = 0;
        invalidate_visual_wave();
        renew_session();
        return;
    }
    Sample& s = SampleBank::instance().slot(sample_slot);
    s.loop_start = 0;
    s.loop_end = wr_frames_;
    s.logical_frames = wr_frames_;
    s.slice_reverse_mask = 0;
    s.reset_cut_speeds();
    if (wr_frames_ > 0) s.chops[0] = 0;
}

void RecUnit::clear_take() {
    if (writer_open()) return;
    invalidate_visual_wave();
    renew_session();
    recording_ = paused_ = false;
    gate_started_ = false;
    params[P_REC].base = 0;
    params[P_REC].effective = 0;
    wr_frames_ = 0;
    target_frames_ = 0;
    if (++take_generation_ == 0) ++take_generation_;
    Sample& s = SampleBank::instance().slot(sample_slot);
    SampleBank::instance().set_provenance(sample_slot, trackr::synth::SampleOrigin::Runtime, "");
    s.loop_start = 0;
    s.loop_end = 0;
    s.logical_frames = 0;
    s.slice_reverse_mask = 0;
    s.reset_cut_speeds();
    for (int i = 0; i < Sample::MAX_CHOPS; ++i) s.chops[i] = 0xFFFFFFFFu;
}

void RecUnit::restore_take(uint32_t frames) {
    if (writer_open()) return;
    invalidate_visual_wave();
    renew_session();
    recording_ = paused_ = false;
    gate_started_ = false;
    const uint32_t cap = capacity_frames();
    wr_frames_ = frames > cap ? cap : frames;
    target_frames_ = wr_frames_;
    if (++take_generation_ == 0) ++take_generation_;
    saved_generation_ = take_generation_;
}

void RecUnit::adopt_take(uint32_t frames) {
    if (writer_open()) return;
    restore_take(frames);
    Sample& s = SampleBank::instance().slot(sample_slot);
    s.loop_start = 0;
    s.loop_end = wr_frames_;
    s.logical_frames = wr_frames_;
    s.slice_reverse_mask = 0;
    s.reset_cut_speeds();
    if (wr_frames_ > 0) s.chops[0] = 0;
}

void RecUnit::capture_input(const q15* buf, int frames) {
    if (!buf || frames <= 0 || !recording_) return;
    if (!writer_storage_matches()) { stop(); return; }
    Sample& sample = SampleBank::instance().slot(sample_slot);
    bind_visual_wave(sample);
    bool finish = false;
    for (int i = 0; i < frames; ++i) {
        if (wr_frames_ >= target_frames_ || wr_frames_ >= sample.data.size()) {
            finish = true; break;
        }
        // One peak comparison per newly written sample, no PCM rescan/heap.
        if (wave_.streaming && wave_.streamed_frames == wr_frames_ && wr_frames_ < MAX_REC_FRAMES) {
            q15& peak = wave_.blocks[wr_frames_ / WAVE_BLOCK];
            if (wr_frames_ % WAVE_BLOCK == 0 || rec_wave_magnitude(buf[i]) > rec_wave_magnitude(peak))
                peak = buf[i];
            ++wave_.streamed_frames;
        }
        sample.data[wr_frames_++] = buf[i];
    }
    sample.loop_start = 0;
    sample.loop_end = wr_frames_;
    sample.logical_frames = wr_frames_;
    if (finish || wr_frames_ >= target_frames_ || wr_frames_ >= sample.data.size()) stop();
}

void RecUnit::process(q15* buf, int frames) {
    // Capture is called explicitly by Engine before bypass/output semantics. This
    // fallback keeps standalone/offline Unit callers correct.
    capture_input(buf, frames);
    if (params[P_MON].effective <= (Q15_ONE / 2))
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
}

// ============================ factory ============================
// ============================ lo-fi / spatial effects ============================

#include "bit_impl.inc"

ChorusUnit::ChorusUnit() {
    kind = UnitKind::Chorus;
    add_param("RATE", 7000);
    add_param("DPTH", 12000);
    add_param("MIX", 12000);
    add_param("SPRD", 0, true); // raw zero is historical 90-degree separation
    buf_ = new(std::nothrow) q15[SIZE];
    if (buf_) std::memset(buf_, 0, SIZE * sizeof(q15));
}
ChorusUnit::~ChorusUnit() { delete[] buf_; }
void ChorusUnit::reset() {
    if (buf_) std::memset(buf_, 0, SIZE * sizeof(q15));
    write_ = phase_ = 0;
    visual_phase_ = 0;
    visual_delay_ = 96;
    visual_delay_q16_[0] = visual_delay_q16_[1] = 96u << 16;
    for (auto& value : visual_effective_) value = 0;
    compare_lease_ = compare_q15_ = 0;
    visual_ready_ = false;
}
void ChorusUnit::process(q15* buf, int frames) {
    if (!buf_ || !buf || frames <= 0) return;
    const uint32_t raw_rate = chorus_motion::unipolar_raw(params[P_RATE].effective);
    const uint32_t raw_depth = chorus_motion::unipolar_raw(params[P_DEPTH].effective);
    const uint32_t raw_mix = chorus_motion::unipolar_raw(params[P_MIX].effective);
    const int raw_spread = chorus_motion::spread_raw(params[P_SPREAD].effective);
    const uint32_t separation = chorus_motion::spread_phase(raw_spread);
    const uint32_t inc = chorus_motion::rate_increment((int)raw_rate);
    const uint32_t depth = chorus_motion::depth_samples((int)raw_depth);
    // Keep the existing Q15 blend, with an exact full-wet endpoint. The largest
    // possible difference * mix is 65535 * 32768, still inside signed int32.
    const int32_t mix = (int32_t)(raw_mix + (raw_mix == 32767u));
    uint32_t delay[2] = {96u << 16, 96u << 16};
    for (int i = 0; i < frames; ++i) {
        phase_ += inc;
        delay[0] = chorus_motion::delay_q16(phase_, depth);
        delay[1] = chorus_motion::delay_q16(phase_ + separation, depth);
        q15 voice[2];
        for (int v = 0; v < 2; ++v) {
            const uint32_t rd = (write_ - (delay[v] >> 16)) & (SIZE - 1);
            // Increasing fractional delay moves toward the OLDER ring sample.
            voice[v] = lerp_q15(buf_[rd], buf_[(rd - 1u) & (SIZE - 1)],
                                delay[v] & 65535u);
        }
        const q15 dry = buf[i];
        const int32_t wet = ((int32_t)voice[0] + voice[1]) / 2;
        buf_[write_] = dry;
        write_ = (write_ + 1u) & (SIZE - 1);
        // A convex blend cannot exceed either endpoint; no extra wet gain.
        const int32_t mixed = (int32_t)dry + (((wet - dry) * mix) >> 15);
        // Disabled comparison is exactly the old arithmetic, without an extra
        // multiply/quantization. The ring/LFO always run, including fully dry.
        if (compare_lease_ || compare_q15_) {
            constexpr uint16_t step = 32768u / DRY_COMPARE_RAMP_FRAMES;
            if (compare_lease_) {
                --compare_lease_;
                if (compare_q15_ < 32768u) compare_q15_ += step;
            } else {
                compare_q15_ -= step;
            }
            // Compare the CURRENT processed mix, never a saved parameter. The
            // convex signed product is bounded by 65535*32768 <= INT32_MAX.
            buf[i] = (q15)(mixed + (((int32_t)dry - mixed) * compare_q15_ >> 15));
        } else {
            buf[i] = (q15)mixed;
        }
    }
    visual_phase_ = (uint16_t)(phase_ >> 16);
    visual_delay_q16_[0] = delay[0];
    visual_delay_q16_[1] = delay[1];
    visual_delay_ = (uint16_t)(delay[0] >> 16);
    visual_effective_[P_RATE] = (q15)raw_rate;
    visual_effective_[P_DEPTH] = (q15)raw_depth;
    visual_effective_[P_MIX] = (q15)raw_mix;
    visual_effective_[P_SPREAD] = (q15)raw_spread;
    visual_ready_ = true;
}

void ReverbUnit::process_legacy(q15* buf, int frames) {
    if (!buf_) return;
    uint32_t scale = 16384 + (uint16_t)params[P_SIZE].effective;
    uint32_t la = 733 + (1499 * scale >> 16);
    uint32_t lb = 997 + (1901 * scale >> 16);
    uint32_t lc = 1201 + (2309 * scale >> 16);
    if (la > 2000) la = 2000;
    if (lb > 2400) lb = 2400;
    if (lc > 3000) lc = 3000;
    const q15 decay_fb = (q15)(6000 + ((uint32_t)(uint16_t)params[P_DECAY].effective * 24000 >> 15));
    const q15 mix = params[P_MIX].effective;
    // DAMP maps to a one-pole coefficient from open (~0.5) to dark (~0.03).
    // The state is q15 stored in q31 to keep the update bounded and cheap.
    const q15 damp = params[P_DAMP].effective;
    const int32_t open_coeff = 16384;
    const int32_t dark_coeff = 1024;
    const int32_t damp_coeff = open_coeff -
        (int32_t)(((int64_t)(uint16_t)damp * (open_coeff - dark_coeff)) >> 15);
    visual_damping_ = damp;
    const bool want_freeze = params[P_FREEZE].effective > 16384;
    const q15 freeze_target = want_freeze ? Q15_ONE : 0;
    q31 peak = 0;

    for (int i = 0; i < frames; ++i) {
        // Roughly 2 ms transition at 32 kHz. It suppresses the click from abruptly
        // removing excitation/changing loop gain, while still feeling immediate.
        if (freeze_mix_ != freeze_target) {
            int32_t next = freeze_mix_ + (freeze_target > freeze_mix_ ? 512 : -512);
            if ((freeze_target > freeze_mix_ && next > freeze_target) ||
                (freeze_target < freeze_mix_ && next < freeze_target)) next = freeze_target;
            freeze_mix_ = (q15)next;
        }

        const q15 wa = buf_[a_];
        const q15 wb = buf_[2048 + b_];
        const q15 wc = buf_[4608 + c_];
        const q31 wet_acc = ((q31)wa + wb + wc) / 3;
        const q15 wet = sat_q15(wet_acc);
        const q31 magnitude = wet < 0 ? -(q31)wet : wet;
        if (magnitude > peak) peak = magnitude;

        // Freeze crossfades the damped state toward raw loop memory. At full
        // freeze: no excitation, exact unity feedback, no damping loss.
        damp_a_ += (((q31)wa - damp_a_) * damp_coeff) >> 15;
        damp_b_ += (((q31)wb - damp_b_) * damp_coeff) >> 15;
        damp_c_ += (((q31)wc - damp_c_) * damp_coeff) >> 15;
        q15 fa, fb_line, fc, feedback, excitation;
        if (freeze_mix_ == Q15_ONE) {
            // Q15_ONE is 32767, so an ordinary >>15 multiply still loses one
            // least-significant bit per loop. Full freeze must copy raw memory
            // directly or a supposedly sealed chamber slowly dies.
            fa = wa; fb_line = wb; fc = wc;
            feedback = Q15_ONE;
            excitation = 0;
            buf_[a_] = fa;
            buf_[2048 + b_] = fb_line;
            buf_[4608 + c_] = fc;
        } else {
            fa = sat_q15(damp_a_ + ((((q31)wa - damp_a_) * freeze_mix_) >> 15));
            fb_line = sat_q15(damp_b_ + ((((q31)wb - damp_b_) * freeze_mix_) >> 15));
            fc = sat_q15(damp_c_ + ((((q31)wc - damp_c_) * freeze_mix_) >> 15));
            feedback = (q15)(decay_fb + ((((q31)Q15_ONE - decay_fb) * freeze_mix_) >> 15));
            excitation = (q15)(((q31)buf[i] * (Q15_ONE - freeze_mix_)) >> 15);
            buf_[a_] = sat_q15((q31)excitation + (((q31)fa * feedback) >> 15));
            buf_[2048 + b_] = sat_q15((q31)excitation + (((q31)fb_line * feedback) >> 15));
            buf_[4608 + c_] = sat_q15((q31)excitation + (((q31)fc * feedback) >> 15));
        }
        if (++a_ >= la) a_ = 0;
        if (++b_ >= lb) b_ = 0;
        if (++c_ >= lc) c_ = 0;
        buf[i] = sat_q15((q31)buf[i] + ((((q31)wet - buf[i]) * mix) >> 15));
    }
    // Fast attack / slow release peak for a truthful chamber-energy marker.
    if (peak > visual_energy_) visual_energy_ = (q15)(peak > 32767 ? 32767 : peak);
    else if (visual_energy_ > 0) {
        q15 release = (q15)(visual_energy_ / 64);
        if (release < 1) release = 1;
        visual_energy_ = (q15)(visual_energy_ - release);
    }
}


// ============================ TILT spectral balance ============================

TiltUnit::TiltUnit():TiltUnit(Revision::Extended) {}
TiltUnit::TiltUnit(bool modern):TiltUnit(modern?Revision::Clear:Revision::Legacy) {}
TiltUnit::TiltUnit(Revision revision) {
    kind = UnitKind::Tilt;
    // Original five indices remain frozen. New cards opt into interpolated
    // shelves and wet trim; every historical patch explicitly loads legacy.
    add_param("TILT", 0, true); add_param("FREQ", 16384);
    add_param("DRIVE", 0); add_param("MIX", 32767); add_param("RANGE", 8192);
    add_param("TRIM", tilt_eq::TRIM_UNITY);
    set_revision(revision);
}
void TiltUnit::set_modern(bool modern) { set_revision(modern?Revision::Clear:Revision::Legacy); }
void TiltUnit::set_revision(Revision revision) {
    if(revision!=Revision::Legacy && revision!=Revision::Clear && revision!=Revision::Extended)return;
    if(revision!=revision_ && revision==Revision::Extended){
        eq_state_=0;previous_input_=0;
        if(processed_){transition_left_=128;transition_from_=last_out_;}
    }
    revision_=revision;modern_=modern();cache_valid_=false;
    num_params = modern_ ? P_COUNT : P_TRIM;
    // Enabling preserves a staged modern CORE record. Disabling removes all
    // hidden routes/overrides, never interpreting historical zero padding as trim.
    if (!modern_) {
        params[P_TRIM] = Param{};
        params[P_TRIM].name = "TRIM";
        params[P_TRIM].base = tilt_eq::TRIM_UNITY;
        params[P_TRIM].snap();
    }
}
void TiltUnit::legacy_process(q15* buf, int frames) {
    if (!buf || frames <= 0) return;
    const q15 tilt = params[P_TILT].effective;
    const q15 freq = params[P_FREQ].effective;
    const q15 drive = params[P_DRIVE].effective;
    const q15 mix = params[P_MIX].effective;
    const q15 range = params[P_RANGE].effective;

    if (!modern_ && range == 0) {
        // Compatibility law for files saved before RANGE existed. Its sonics stay
        // intact, but all formerly overflowing multiplies are widened and bounded.
        const int32_t coeff = 256 + ((uint32_t)(uint16_t)freq * 12000u >> 15);
        const int32_t gain = 32767 + (int32_t)drive * 3;
        for (int i = 0; i < frames; ++i) {
            const int32_t dry = buf[i];
            low_ += (q31)(((int64_t)(dry - low_) * coeff) >> 15);
            const int32_t high = dry - low_;
            int64_t shaped = (int64_t)dry + (((int64_t)high * tilt) >> 15) -
                (((int64_t)low_ * tilt) >> 15);
            shaped = (shaped * gain) >> 15;
            if (shaped > INT32_MAX) shaped = INT32_MAX;
            if (shaped < INT32_MIN) shaped = INT32_MIN;
            const q15 colored = trackr::fx::soft_clip_q31((q31)shaped);
            buf[i] = sat_q15(dry + (q31)((((int64_t)colored - dry) * mix) >> 15));
        }
        return;
    }

    const int32_t coeff = tilt_eq::coeff(freq);
    uint32_t low_gain, high_gain;
    tilt_eq::shelf_gains(tilt, range, modern_, low_gain, high_gain);
    const int32_t drive_gain_q16 = 65536 + (int32_t)drive * 6;
    const uint32_t trim_gain = modern_ ? tilt_eq::trim_gain_q16(params[P_TRIM].effective) : 65536;

    for (int i = 0; i < frames; ++i) {
        const int32_t dry = buf[i];
        low_ += (q31)(((int64_t)(dry - low_) * coeff) >> 15);
        const int32_t high = dry - low_;
        const int32_t tilted = (int32_t)(((int64_t)low_ * low_gain +
                                           (int64_t)high * high_gain) >> 16);
        q15 colored;
        if (tilt == 0 && drive == 0) {
            colored = (q15)dry;
        } else if (drive == 0) {
            // Neutral TILT with zero DRIVE is an exact identity, not a hidden
            // saturator. At nonzero tilt, only the shelf itself may reach q15 rails.
            colored = sat_q15(tilted);
        } else {
            int64_t driven = ((int64_t)tilted * drive_gain_q16) >> 16;
            if (driven > INT32_MAX) driven = INT32_MAX;
            if (driven < INT32_MIN) driven = INT32_MIN;
            colored = trackr::fx::soft_clip_q31((q31)driven);
        }
        // Wet trim follows the existing shelf/drive saturation, before dry/wet.
        // Keep wide headroom into mix; only the final output clamps boosted trim.
        const int32_t wet = trim_gain == 65536 ? (int32_t)colored
            : (int32_t)(((int64_t)colored * trim_gain) >> 16);
        buf[i] = sat_q15(dry + (q31)((((int64_t)wet - dry) * mix) >> 15));
    }
}

#include "tilt_impl.inc"

// ============================ RING / diode bridge ============================

// 0.5 Hz..8.192 kHz in 7/32-octave steps. The old implementation called a
// linear map "logarithmic-ish", which parked almost the whole knob in unusable
// ultrasonic clang. This LUT gives slow tremolo real travel while retaining an
// audio-rate carrier, with one interpolation per block and no floating point.
static const uint32_t RING_INC_LUT[65] = {
    67109u,78096u,90883u,105762u,123078u,143229u,166679u,193969u,
    225726u,262683u,305691u,355741u,413984u,481764u,560640u,652431u,
    759250u,883558u,1028219u,1196564u,1392471u,1620453u,1885761u,2194507u,
    2553803u,2971924u,3458502u,4024744u,4683695u,5450532u,6342920u,7381414u,
    8589935u,9996320u,11632967u,13537572u,15754010u,18333332u,21334955u,24828017u,
    28892981u,33623480u,39128480u,45534784u,52989960u,61665734u,71761948u,83511163u,
    97184016u,113095455u,131611993u,153160148u,178236271u,207417980u,241377459u,280896949u,
    326886763u,380406252u,442688212u,515167277u,599512966u,697668142u,811893760u,944820951u,
    1099511628u
};

static uint32_t ring_base_increment(q15 frequency) {
    const uint32_t scaled = (uint32_t)(uint16_t)frequency * 64u;
    uint32_t index = scaled >> 15;
    uint32_t fraction = scaled & 32767u;
    if (index >= 64) { index = 63; fraction = 32767; }
    return RING_INC_LUT[index] +
        (uint32_t)(((uint64_t)(RING_INC_LUT[index + 1] - RING_INC_LUT[index]) * fraction) >> 15);
}

#include "ring_impl.inc"

// ============================ GHOST resonant memory ============================

GhostUnit::GhostUnit() {
    kind = UnitKind::Ghost;
    add_param("TONE", 15000); add_param("DECAY", 24000); add_param("SMEAR", 12000);
    add_param("DRIFT", 4096); add_param("FRZ", 0); add_param("MIX", 16384);
    add_param("FINE", 0, true); add_param("RATE", 0);
    memory_ = new(std::nothrow) q15[SIZE * BANDS];
    if (memory_) std::memset(memory_, 0, SIZE * BANDS * sizeof(q15));
}
GhostUnit::~GhostUnit() { delete[] memory_; }
void GhostUnit::reset() {
    if (memory_) std::memset(memory_, 0, SIZE * BANDS * sizeof(q15));
    std::memset(write_, 0, sizeof write_); std::memset(damp_, 0, sizeof damp_);
    std::memset(frozen_delay_, 0, sizeof frozen_delay_);
    phase_ = 0; freeze_mix_ = visual_energy_ = visual_drift_ = 0;
    freeze_latched_ = false;
    tail_ready_ = tail_ramping_ = false;
    std::memset(damp_residue_, 0, sizeof damp_residue_);
    std::memset(tap_residue_, 0, sizeof tap_residue_);
    std::memset(feedback_residue_, 0, sizeof feedback_residue_);
}
uint16_t GhostUnit::root_hz(q15 value) {
    int32_t tone = value;
    if (tone < 0) tone = 0;
    const uint32_t scale_q15 = 32767u + (32767u - (uint32_t)tone) * 3u;
    const uint32_t denominator = 149u * scale_q15;
    return denominator ? (uint16_t)(((uint64_t)ENGINE_SR * 32767u + denominator / 2u) /
                                    denominator) : 0;
}
uint32_t GhostUnit::fine_delay_ratio(q15 fine) {
    if (fine == 0) return 1u << 24;
    // exp(-cents * ln(2)/1200), fourth-order at |x| <= ln(2)/24.
    // q24 preserves tiny beat adjustments without float math on the audio path.
    const int64_t x = -(int64_t)fine * 484545 / 32767;
    const int64_t x2 = x * x / (1 << 24);
    const int64_t x3 = x2 * x / (1 << 24);
    const int64_t x4 = x3 * x / (1 << 24);
    return (uint32_t)((1 << 24) + x + x2 / 2 + x3 / 6 + x4 / 24);
}
uint32_t GhostUnit::root_millihz(q15 tone, q15 fine) {
    const uint32_t scale = 32767u + (32767u - (uint32_t)(tone < 0 ? 0 : tone)) * 3u;
    uint32_t delay = (uint32_t)(((uint64_t)149 * scale * 256) >> 15);
    delay = (uint32_t)(((uint64_t)delay * fine_delay_ratio(fine)) >> 24);
    return (uint32_t)(((uint64_t)ENGINE_SR * 256000 + delay / 2) / delay);
}
uint32_t GhostUnit::drift_period_ms(q15 rate) {
    const uint32_t v = rate < 0 ? 0 : (uint32_t)rate;
    return 2500u + (uint32_t)((117500ULL * v * v) / (32767ULL * 32767));
}
uint32_t GhostUnit::drift_increment(q15 rate) {
    // Zero is the exact legacy increment, including its original rounding.
    if (rate <= 0) return 53687u;
    return (uint32_t)((4294967296ULL * 1000) / ((uint64_t)ENGINE_SR * drift_period_ms(rate)));
}
void GhostUnit::control_tick() {
    // Shared slew otherwise stops up to three LSB short; exact fine-zero restores
    // the untouched legacy geometry. Do not change slew on the original controls.
    Param& fine=params[P_FINE];
    const int delta=(int)fine.target-fine.effective;
    if (fine.slew_ready && delta>=-3 && delta<=3) fine.effective=fine.target;
}
void GhostUnit::process(q15* buf, int frames) {
    if (!memory_) return;
    // Four mutually-prime resonators. TONE moves their shared register upward;
    // DRIFT bends each line at a different phase, with fractional reads so the
    // memory glides rather than zippering between integer samples.
    static const uint16_t BASE[BANDS] = { 149, 263, 431, 683 };
    static const uint32_t PHASE_OFFSET[BANDS] = {
        0u, 0x3C6EF372u, 0x78DDE6E4u, 0xB54CDA56u
    };
    int32_t tone_value = params[P_TONE].effective;
    int32_t decay_value = params[P_DECAY].effective;
    int32_t smear_value = params[P_SMEAR].effective;
    int32_t drift_value = params[P_DRIFT].effective;
    int32_t mix_value = params[P_MIX].effective;
    if (tone_value < 0) tone_value = 0;
    if (decay_value < 0) decay_value = 0;
    if (smear_value < 0) smear_value = 0;
    if (drift_value < 0) drift_value = 0;
    if (mix_value < 0) mix_value = 0;
    const q15 tone = (q15)tone_value, decay = (q15)decay_value;
    const q15 smear = (q15)smear_value, drift = (q15)drift_value;
    const q15 mix = (q15)mix_value;
    const bool freeze_requested = params[P_FREEZE].effective > 16384;
    const q15 freeze_target = freeze_requested ? 32767 : 0;
    uint32_t base_delay_q8[BANDS];
    // Low TONE = four times the nominal delay (low spectral register), high TONE
    // = nominal delay. The full range remains safely inside the fixed 4096 buffer.
    const uint32_t scale_q15 = 32767u +
        (uint32_t)(32767u - (uint16_t)tone) * 3u;
    const uint32_t fine_ratio = fine_delay_ratio(params[P_FINE].effective);
    const uint32_t phase_increment = drift_increment(params[P_RATE].effective);
    for (int b = 0; b < BANDS; ++b) {
        base_delay_q8[b] = (uint32_t)(((uint64_t)BASE[b] * scale_q15 * 256u) >> 15);
        base_delay_q8[b] = (uint32_t)(((uint64_t)base_delay_q8[b] * fine_ratio) >> 24);
    }
    if (freeze_requested && !freeze_latched_) {
        // Latch one integer read distance per well. At full capture each line
        // copies exactly that tap back into itself: no q15 "almost one", damping,
        // interpolation, or drifting remains to bleed the held memory away.
        for (int b = 0; b < BANDS; ++b) {
            const uint32_t p = phase_ + PHASE_OFFSET[b];
            const uint16_t half = (uint16_t)(p >> 16);
            const int32_t triangle = half < 32768u
                ? (int32_t)half * 2 - 32767
                : 32767 - ((int32_t)half - 32768) * 2;
            const int64_t bend = ((int64_t)base_delay_q8[b] * triangle * drift) >> 33;
            int64_t delay_q8 = (int64_t)base_delay_q8[b] + bend;
            if (delay_q8 < (16 << 8)) delay_q8 = 16 << 8;
            if (delay_q8 > ((SIZE - 2) << 8)) delay_q8 = (SIZE - 2) << 8;
            frozen_delay_[b] = (uint16_t)(((uint32_t)delay_q8 + 128u) >> 8);
        }
        freeze_latched_ = true;
    }
    const q15 desired_gain = long_tail ? (q15)ghost_decay::gain(decay, base_delay_q8[0])
        : (q15)(18000 + ((uint32_t)(uint16_t)decay * 14600u >> 15));
    if (!tail_ready_) { tail_gain_ = desired_gain; tail_mode_ = long_tail; tail_ready_ = true; }
    if (tail_mode_ != long_tail) {
        tail_mode_ = long_tail; tail_ramping_ = true;
        std::memset(damp_residue_, 0, sizeof damp_residue_);
        std::memset(tap_residue_, 0, sizeof tap_residue_);
        std::memset(feedback_residue_, 0, sizeof feedback_residue_);
    }
    const q15 damp_coeff = (q15)(384 +
        ((uint32_t)(32767u - (uint16_t)smear) * 15999u >> 15));
    int32_t peak = 0;

    for (int i = 0; i < frames; ++i) {
        // Only explicit version changes ramp; untouched legacy parameter arithmetic
        // stays bit-exact. No buffer/phase reset when comparing old and long tails.
        if (tail_ramping_) {
            const int delta = (int)desired_gain - tail_gain_;
            tail_gain_ = (q15)(tail_gain_ + std::max(-16, std::min(16, delta)));
            if (tail_gain_ == desired_gain) tail_ramping_ = false;
        } else tail_gain_ = desired_gain;
        const q15 decay_gain = tail_gain_;
        const q15 dry = buf[i];
        if (freeze_mix_ < freeze_target) {
            int32_t next = freeze_mix_ + 192;
            freeze_mix_ = (q15)(next > freeze_target ? freeze_target : next);
        } else if (freeze_mix_ > freeze_target) {
            int32_t next = freeze_mix_ - 192;
            freeze_mix_ = (q15)(next < freeze_target ? freeze_target : next);
            if (freeze_mix_ == 0) freeze_latched_ = false;
        }
        // Once the capture ramp reaches full scale the read geometry is sealed;
        // phase may keep wrapping harmlessly, ready for a smooth release.
        if (freeze_mix_ < 32767) phase_ += phase_increment; // legacy 2.5 s .. 120 s
        q31 sum = 0;
        int32_t first_lfo = 0;
        for (int b = 0; b < BANDS; ++b) {
            if (freeze_mix_ == 32767 && freeze_latched_) {
                q15* band = memory_ + b * SIZE;
                const uint32_t delay = frozen_delay_[b];
                const uint32_t rd = (write_[b] + SIZE - delay) & (SIZE - 1);
                const q15 tap = band[rd];
                band[write_[b]] = tap;
                write_[b] = (write_[b] + 1) & (SIZE - 1);
                damp_[b] = tap;
                damp_residue_[b] = tap_residue_[b] = feedback_residue_[b] = 0;
                sum += tap;
                continue;
            }
            const uint32_t p = phase_ + PHASE_OFFSET[b];
            const uint16_t half = (uint16_t)(p >> 16);
            const int32_t triangle = half < 32768u
                ? (int32_t)half * 2 - 32767
                : 32767 - ((int32_t)half - 32768) * 2;
            if (b == 0) first_lfo = triangle;
            const int64_t bend = ((int64_t)base_delay_q8[b] * triangle * drift) >> 33;
            int64_t delay_q8 = (int64_t)base_delay_q8[b] + bend;
            if (delay_q8 < (16 << 8)) delay_q8 = 16 << 8;
            if (delay_q8 > ((SIZE - 2) << 8)) delay_q8 = (SIZE - 2) << 8;
            const uint32_t delay_frames = (uint32_t)delay_q8 >> 8;
            const uint32_t fraction = (uint32_t)delay_q8 & 255u;
            q15* band = memory_ + b * SIZE;
            const uint32_t rd0 = (write_[b] + SIZE - delay_frames) & (SIZE - 1);
            const uint32_t rd1 = (rd0 + SIZE - 1) & (SIZE - 1);
            q15 tap;
            if (long_tail) {
                const int32_t interpolated = (int32_t)band[rd0] * (256 - (int32_t)fraction) +
                    (int32_t)band[rd1] * (int32_t)fraction + tap_residue_[b];
                tap = sat_q15(interpolated / 256);
                tap_residue_[b] = interpolated - (int32_t)tap * 256;
            } else tap = (q15)(band[rd0] +
                (int32_t)(((int64_t)(band[rd1] - band[rd0]) * fraction) >> 8));
            if (long_tail) {
                // Retain fractions symmetrically: no accumulated negative-rounding DC
                // or one-LSB loss on every turn of a quiet impulse tail.
                const int64_t update = (int64_t)((q31)tap - damp_[b]) * damp_coeff + damp_residue_[b];
                const int32_t step = (int32_t)(update / 32768);
                damp_residue_[b] = (int32_t)(update - (int64_t)step * 32768);
                damp_[b] = sat_q15((q31)damp_[b] + step);
            } else damp_[b] = sat_q15((q31)damp_[b] +
                (q31)(((int64_t)((q31)tap - damp_[b]) * damp_coeff) >> 15));
            const q15 feedback_gain = (q15)(decay_gain +
                (q31)(((int64_t)(32680 - decay_gain) * freeze_mix_) >> 15));
            q31 feedback;
            if (long_tail) {
                // Below four LSB, discard feedback remainder so numerical dust
                // cannot become a permanent limit cycle (-78 dBFS per well).
                if (damp_[b] >= -4 && damp_[b] <= 4) feedback_residue_[b] = 0;
                const int64_t product = (int64_t)damp_[b] * feedback_gain + feedback_residue_[b];
                feedback = (q31)(product / 32768);
                feedback_residue_[b] = (int32_t)(product - (int64_t)feedback * 32768);
            } else feedback = (q31)(((int64_t)damp_[b] * feedback_gain) >> 15);
            // Alternate polarity stops all four low modes from building the same DC
            // sign while retaining a dense, resonant tail.
            if (b & 1) feedback = -feedback;
            const q15 input_gain = (q15)(((int64_t)8192 * (32767 - freeze_mix_)) >> 15);
            const q31 input = (q31)(((int64_t)dry * input_gain) >> 15) + feedback;
            band[write_[b]] = trackr::fx::soft_clip_q31(input);
            write_[b] = (write_[b] + 1) & (SIZE - 1);
            sum += tap;
        }
        const q15 wet = sat_q15(sum >> 2);
        const int32_t magnitude = wet == -32768 ? 32767 : (wet < 0 ? -wet : wet);
        if (magnitude > peak) peak = magnitude;
        buf[i] = sat_q15((q31)dry +
            (q31)(((int64_t)((q31)wet - dry) * mix) >> 15));
        visual_drift_ = (q15)(((int64_t)first_lfo * drift) >> 15);
    }
    if (peak > visual_energy_) visual_energy_ = (q15)peak;
    else {
        const int32_t difference = visual_energy_ - peak;
        visual_energy_ = difference < 8 ? (q15)peak
            : (q15)(visual_energy_ - (difference >> 3));
    }
}

// ============================ STRN live granular ============================

StranularUnit::StranularUnit() {
    build_strn_pitch_lut();
    kind = UnitKind::Stranular;
    add_param("POS", 24576); add_param("SIZE", 9000); add_param("DENS", 12000);
    add_param("PTCH", 0, true); add_param("FRZ", 0); add_param("MIX", 16384);
    add_param("SPRY", 8192); add_param("REV", 0); add_param("PENV", 0, true);
    add_param("HOLD", 0); add_param("SYNC", 0); add_param("CLST", 0);
    buffer_ = new(std::nothrow) q15[BUFFER_SIZE];
    if (buffer_) std::memset(buffer_, 0, BUFFER_SIZE * sizeof(q15));
}
StranularUnit::~StranularUnit() { delete[] buffer_; }
int32_t StranularUnit::pitch_increment_q16(q15 pitch) {
    build_strn_pitch_lut();
    return semitone_inc_q16(pitch);
}
uint8_t StranularUnit::cluster_count(q15 value) {
    const uint32_t amount = (uint16_t)value;
    return (uint8_t)(1u + (amount * 3u + 32766u) / 32767u);
}
void StranularUnit::set_transport_bpm_q16(int32_t bpm_q16) {
    if (bpm_q16 < 20 * 65536) bpm_q16 = 20 * 65536;
    if (bpm_q16 > 300 * 65536) bpm_q16 = 300 * 65536;
    const uint8_t d = sync_division > 3 ? 3 : sync_division;
    if (clock_bpm_q16_ == bpm_q16 && clock_division_ == d) return;
    clock_bpm_q16_ = bpm_q16;
    clock_division_ = d;
    static const uint8_t denominator[4] = {4,2,1,1}, multiplier[4] = {1,1,1,2};
    const uint32_t before = sync_interval_frames;
    sync_interval_frames = (uint32_t)((uint64_t)ENGINE_SR * 60u * multiplier[d] * 65536u /
        ((uint64_t)(uint32_t)bpm_q16 * denominator[d]));
    if (sync_interval_frames < 32) sync_interval_frames = 32;
    if (params[P_SYNC].effective > 16384 && before != sync_interval_frames)
        rescale_clock_countdown(spawn_countdown_, clock_countdown_fraction_, before, sync_interval_frames);
}
void StranularUnit::set_transport_bpm(int bpm) {
    clock_bpm_q16_ = 0;
    clock_countdown_fraction_ = 0;
    if (bpm < 20) bpm = 20;
    if (bpm > 300) bpm = 300;
    static const uint8_t numerator[4] = { 1, 1, 1, 1 };
    static const uint8_t denominator[4] = { 4, 2, 1, 1 };
    static const uint8_t multiplier[4] = { 1, 1, 1, 2 };
    const int d = sync_division > 3 ? 3 : sync_division;
    sync_interval_frames = (uint32_t)((uint64_t)ENGINE_SR * 60u * numerator[d] * multiplier[d] /
                                      ((uint32_t)bpm * denominator[d]));
    if (sync_interval_frames < 32) sync_interval_frames = 32;
}
void StranularUnit::reset() {
    if (buffer_) std::memset(buffer_, 0, BUFFER_SIZE * sizeof(q15));
    for (auto& g : grains_) g = Grain{};
    write_ = filled_ = spawn_countdown_ = 0; frozen_ = false;
    clock_countdown_fraction_ = 0;
    freeze_write_ = freeze_filled_ = freeze_start_ = freeze_span_ = 0;
    freeze_mix_ = 0;
    std::memset(visual_wave_, 0, sizeof visual_wave_); std::memset(visual_positions_, 0, sizeof visual_positions_);
    std::memset(visual_increments_, 0, sizeof visual_increments_);
    visual_grains_ = 0; visual_fill_ = 0; visual_mix_ = 0; visual_decim_ = 0;
    visual_wave_span_ = visual_wave_reference_ = 0;
    visual_wave_oldest_ = 0; visual_wave_dirty_ = false;
}
const q15* StranularUnit::visual_wave() const {
    if (!visual_wave_dirty_) return visual_wave_;
    const uint32_t span = visual_wave_span_, reference = visual_wave_reference_;
    // The first bin may already have been overwritten after the last tick (full
    // long-mode ring). All other bins are farther away than 63 possible writes.
    visual_wave_[0] = visual_wave_oldest_;
    for (int p = 1; p < VIZ_POINTS; ++p) {
        const uint32_t back = span > 1
            ? (uint32_t)((uint64_t)(VIZ_POINTS - 1 - p) * (span - 1) /
                         (VIZ_POINTS - 1)) : 0;
        visual_wave_[p] = span
            ? buffer_[(reference + BUFFER_SIZE - 1 - back) & (BUFFER_SIZE - 1)] : 0;
    }
    visual_wave_dirty_ = false;
    return visual_wave_;
}
void StranularUnit::spawn_grain(uint32_t length, int32_t increment, uint32_t position, uint32_t spray) {
    Grain* voice = nullptr;
    for (auto& g : grains_) if (!g.active) { voice = &g; break; }
    const uint32_t available = frozen_ ? freeze_span_ : filled_;
    if (!voice || available < length + 4) return;
    uint32_t history;
    uint32_t start;
    if (frozen_) {
        // Freeze owns an immutable circular scan window. POS traverses only that
        // captured region and spray is clamped/wrapped inside it.
        const uint32_t max_back = freeze_span_ > length + 4 ? freeze_span_ - length - 4 : 0;
        history = (uint32_t)(((uint64_t)position * max_back) >> 15);
        int32_t jitter = spray ? (int32_t)(random_u32() % (spray * 2u + 1u)) - (int32_t)spray : 0;
        int64_t offset = (int64_t)history + jitter;
        while (offset < 0) offset += freeze_span_;
        offset %= freeze_span_;
        start = (freeze_write_ + BUFFER_SIZE - 1 - (uint32_t)offset) & (BUFFER_SIZE - 1);
    } else {
        const uint32_t scan_span = long_mode ? filled_ : (filled_ < 16384u ? filled_ : 16384u);
        const uint32_t max_back = scan_span > length + 4 ? scan_span - length - 4 : 0;
        history = 64 + (uint32_t)(((uint64_t)position * max_back) >> 15);
        int32_t jitter = spray ? (int32_t)(random_u32() % (spray * 2u + 1u)) - (int32_t)spray : 0;
        int64_t back = (int64_t)history + jitter;
        if (back < 1) back = 1;
        if ((uint64_t)back >= scan_span) back = scan_span - 1;
        start = (write_ + BUFFER_SIZE - (uint32_t)back) & (BUFFER_SIZE - 1);
    }
    if ((random_u32() & 0x7fffu) < (uint16_t)params[P_REVERSE].effective) increment = -increment;
    voice->active = true; voice->pos_q16 = (int64_t)start << 16; voice->inc_q16 = increment;
    const int32_t envelope_ratio = semitone_inc_q16(params[P_PITCH_ENV].effective);
    const int32_t envelope_target = (int32_t)(((int64_t)increment * envelope_ratio) / 65536);
    voice->pitch_delta_q16 = length
        ? (envelope_target - increment) / (int32_t)length : 0;
    voice->age = 0; voice->length = length;
}
void StranularUnit::process(q15* buf, int frames) {
    if (!buffer_) return;
    const bool want_freeze = params[P_FREEZE].effective > 16384;
    if (want_freeze != frozen_) {
        frozen_ = want_freeze;
        if (frozen_) {
            freeze_write_ = write_;
            freeze_filled_ = filled_;
            freeze_span_ = long_mode ? freeze_filled_ : (freeze_filled_ < 16384u ? freeze_filled_ : 16384u);
            freeze_start_ = (freeze_write_ + BUFFER_SIZE - freeze_span_) & (BUFFER_SIZE - 1);
        }
    }
    const q15 freeze_target = frozen_ ? Q15_ONE : 0;
    uint32_t length_max = long_mode ? 32768u : 8192u;
    uint32_t length = 128 + (uint32_t)((uint64_t)(uint16_t)params[P_SIZE].effective *
                                      (length_max - 128u) / 32767u);

    uint32_t interval = 32 + (uint32_t)((uint64_t)(32767 - (uint16_t)params[P_DENS].effective) *
                                       2016u / 32767u);
    if (params[P_SYNC].effective > 16384) interval = sync_interval_frames;
    uint32_t spray_frames = (uint32_t)((uint64_t)(uint16_t)params[P_SPRAY].effective *
                                       8192u / 32767u);
    int32_t increment = semitone_inc_q16(params[P_PITCH].effective);
    const q15 mix = params[P_MIX].effective;
    // Retain the last visible PRE-advance coordinate, even for a voice which
    // retires or leaves the visible span mid-block. Normalize only once per head.
    uint32_t last_position[MAX_GRAINS], last_span[MAX_GRAINS];
    uint16_t visible_touched = 0, rendered = 0;
    for (int i = 0; i < frames; ++i) {
        const q15 dry = buf[i];
        if (!frozen_) { buffer_[write_] = dry; write_ = (write_ + 1) & (BUFFER_SIZE - 1); if (filled_ < BUFFER_SIZE) ++filled_; }
        if (freeze_mix_ != freeze_target) {
            int step = freeze_target > freeze_mix_ ? 512 : -512; int v = freeze_mix_ + step;
            if ((step > 0 && v > freeze_target) || (step < 0 && v < freeze_target)) v = freeze_target;
            freeze_mix_ = (q15)v;
        }
        if (params[P_HOLD].effective <= 16384 && spawn_countdown_ == 0) {
            const int burst = cluster_count(params[P_CLUSTER].effective);
            for (int grain = 0; grain < burst; ++grain)
                spawn_grain(length, increment, (uint16_t)params[P_POS].effective, spray_frames);
            spawn_countdown_ = interval;
            clock_countdown_fraction_ = 0;
        } else if (spawn_countdown_) --spawn_countdown_;
        q31 sum = 0; int active = 0; uint16_t grain_mask = 0;
        const uint32_t visual_span = frozen_ ? freeze_span_ :
            (long_mode ? filled_ : (filled_ < 16384u ? filled_ : 16384u));
        const uint32_t visual_write = frozen_ ? freeze_write_ : write_;
        for (int gi = 0; gi < MAX_GRAINS; ++gi) {
            auto& g = grains_[gi];
            if (!g.active) continue;
            const uint32_t grain_index = (uint32_t)(g.pos_q16 >> 16) & (BUFFER_SIZE - 1);
            const uint32_t grain_back = (visual_write + BUFFER_SIZE - 1 - grain_index) & (BUFFER_SIZE - 1);
            if (visual_span > 1 && grain_back < visual_span) {
                grain_mask |= (uint16_t)(1u << gi);
                visible_touched |= (uint16_t)(1u << gi);
                last_position[gi] = visual_span - 1 - grain_back;
                last_span[gi] = visual_span - 1;
            }
            rendered |= (uint16_t)(1u << gi);
            uint32_t index = grain_index, next = (index + 1) & (BUFFER_SIZE - 1);
            q15 sample = lerp_q15(buffer_[index], buffer_[next], (uint16_t)g.pos_q16);
            uint32_t half = g.length / 2, ramp = g.age < half ? g.age : g.length - g.age;
            q15 window;
            if (window_mode == 1) {
                // Parabolic Hann approximation: 4*x*(1-x), no LUT/multiply-heavy trig.
                uint32_t x = (uint32_t)((uint64_t)g.age * 32767u / g.length);
                window = (q15)(((uint64_t)x * (32767u - x) * 4u) >> 15);
            } else if (window_mode == 2) {
                uint32_t edge = g.length / 8u;
                window = edge && ramp < edge ? (q15)((uint64_t)ramp * 32767u / edge) : Q15_ONE;
            } else {
                window = half ? (q15)((uint64_t)ramp * 32767u / half) : Q15_ONE;
            }
            sum += ((q31)sample * window) >> 15; ++active; g.pos_q16 += g.inc_q16;
            // Frozen grains wrap inside the captured scan window. The old code
            // wrapped only their spawn point, then let playback escape into
            // unrelated ring history despite claiming an immutable freeze.
            if (frozen_ && freeze_span_ > 0) {
                const int64_t ring_q16 = (int64_t)BUFFER_SIZE << 16;
                const int64_t span_q16 = (int64_t)freeze_span_ << 16;
                const int64_t start_q16 = (int64_t)freeze_start_ << 16;
                int64_t relative = g.pos_q16 - start_q16;
                if (relative < 0) relative += ring_q16;
                // A reverse head stepping just before freeze_start appears near
                // the end of the physical ring after wrap; recover the signed
                // crossing before applying the captured-window modulo.
                if (g.inc_q16 < 0 && relative >= span_q16) relative -= ring_q16;
                relative %= span_q16;
                if (relative < 0) relative += span_q16;
                g.pos_q16 = start_q16 + relative;
                if (g.pos_q16 >= ring_q16) g.pos_q16 -= ring_q16;
            }
            // Update pitch envelope every four samples to bound Old 3DS cost.
            if ((g.age & 3u) == 0) g.inc_q16 += g.pitch_delta_q16 * 4;
            if (++g.age >= g.length) g.active = false;
        }
        q15 wet = active ? sat_q15(sum / active) : dry;
        q31 effective_mix = freeze_mix_ == Q15_ONE ? Q15_ONE :
            mix + (((q31)freeze_mix_ * (32767 - mix)) >> 15);
        if (effective_mix > 32767) effective_mix = 32767;
        visual_mix_ = (q15)effective_mix;
        buf[i] = sat_q15((q31)dry + ((((q31)wet - dry) * effective_mix) >> 15));
        visual_grains_ = grain_mask;
        if (++visual_decim_ >= VIZ_INTERVAL) {
            visual_decim_ = 0;
            const uint32_t span = frozen_ ? freeze_span_ :
                (long_mode ? filled_ : (filled_ < 16384u ? filled_ : 16384u));
            const uint32_t reference = frozen_ ? freeze_write_ : write_;
            const uint32_t target_span = long_mode ? BUFFER_SIZE : 16384u;
            visual_fill_ = (uint16_t)(span >= target_span ? 32767u :
                (uint32_t)((uint64_t)span * 32767u / target_span));
            visual_wave_span_ = span; visual_wave_reference_ = reference;
            visual_wave_oldest_ = span
                ? buffer_[(reference + BUFFER_SIZE - span) & (BUFFER_SIZE - 1)] : 0;
            visual_wave_dirty_ = true;
        }
    }
    for (int gi = 0; gi < MAX_GRAINS; ++gi) {
        if (visible_touched & (1u << gi))
            visual_positions_[gi] = (uint8_t)(last_position[gi] * (VIZ_POINTS - 1) / last_span[gi]);
        if (rendered & (1u << gi)) visual_increments_[gi] = grains_[gi].inc_q16;
    }
}

// ============================ SHIFT ============================

int32_t ShiftUnit::shift_millihz(q15 value) {
    const int32_t v = std::max(-32767, int(value));
    const int32_t magnitude = v < 0 ? -v : v;
    const int32_t mhz = int32_t((int64_t(magnitude) * magnitude * 2000000 +
                                536838144) / 1073676289);
    return v < 0 ? -mhz : mhz;
}
uint16_t ShiftUnit::feedback_permille(q15 value) {
    return uint16_t((std::max(0, int(value)) * 850 + 16383) / 32767);
}
int32_t ShiftUnit::damping_alpha(q15 value) {
    // alpha = 1/64 + (63/64)*(1-d)^2, rounded once to Q30.
    // int64 products stay below 2^60; raw zero produces exact unity.
    constexpr int64_t Q30 = int64_t(1) << 30, DEN = int64_t(32767) * 32767;
    const int32_t open = 32767 - std::max(0, int(value));
    return int32_t(Q30 / 64 +
        ((Q30 - Q30 / 64) * open * open + DEN / 2) / DEN);
}
ShiftUnit::ShiftUnit() {
    kind = UnitKind::Shift;
    add_param("SHIFT", 3663, true); // +24.994 Hz; quadratic resolution near zero
    add_param("FDBK", 0);
    add_param("MIX", 32767);
    add_param("DAMP", 0);
}
void ShiftUnit::control_tick() {
    const int32_t mhz = shift_millihz(params[P_SHIFT].effective);
    const int32_t inc = int32_t((int64_t(mhz) * (int64_t(1) << 32)) /
                                (ENGINE_SR * 1000));
    const int32_t fb = int32_t((int64_t(feedback_permille(params[P_FEEDBACK].effective)) *
                               (int64_t(1) << 30)) / 1000);
    const int32_t mix = int32_t((int64_t(std::max(0, int(params[P_MIX].effective))) *
                                (int64_t(1) << 30)) / 32767);
    const int32_t alpha = damping_alpha(params[P_DAMP].effective);
    if (!coefficients_ready_) {
        increment_.snap(inc); feedback_amount_.snap(fb); mix_.snap(mix);
        damp_alpha_.snap(alpha);
        coefficients_ready_ = true;
    } else {
        increment_.set(inc); feedback_amount_.set(fb); mix_.set(mix);
        damp_alpha_.set(alpha);
    }
}
void ShiftUnit::reset() {
    for (int n = 0; n < 4; ++n) { a_[n] = {}; b_[n] = {}; }
    phase_ = 0; b_delay_ = feedback_ = input_dc_ = output_dc_ = 0;
    damped_feedback_ = 0;
    coefficients_ready_ = false;
}
void ShiftUnit::process(q15* buf, int frames) {
    if (frames <= 0) return;
    if (!coefficients_ready_) control_tick();
    constexpr int64_t Q30 = int64_t(1) << 30;
    for (int n = 0; n < frames; ++n) {
        const int32_t dry = int32_t(buf[n]) * 256;
        const int32_t inc = increment_.tick();
        const int32_t fb = feedback_amount_.tick(), mix = mix_.tick();
        const int32_t alpha = damp_alpha_.tick();
        // Track even at unity/zero feedback. Only the previous limited wet return
        // is filtered; first wet pass and dry are untouched. Unity is exact legacy.
        damped_feedback_ = alpha == Q30 ? feedback_ : shift_dsp::clamp(
            int64_t(damped_feedback_) +
            ((int64_t(feedback_) - damped_feedback_) * alpha) / Q30,
            shift_dsp::FS - 256);
        const int32_t input = dry + int32_t((int64_t(damped_feedback_) * fb) / Q30);
        // Two DC blockers (~4.98 Hz). One prevents incoming DC being translated;
        // the other removes translated-through-zero DC before recirculation.
        input_dc_ += (input - input_dc_) / 1024;
        int32_t a = input - input_dc_, b = a;
        for (int section = 0; section < 4; ++section) {
            a = a_[section].tick(a, shift_dsp::A[section]);
            b = b_[section].tick(b, shift_dsp::B[section]);
        }
        const int32_t quadrature = b_delay_; b_delay_ = b;
        const int32_t cosine = shift_dsp::sine(phase_ + 0x40000000u);
        const int32_t sine = shift_dsp::sine(phase_);
        // B lags A by ~90 degrees: minus selects f + signed carrier rate.
        const int32_t shifted = shift_dsp::clamp((int64_t(a) * cosine -
                           int64_t(quadrature) * sine) / Q30, 16 * shift_dsp::FS);
        phase_ += uint32_t(inc); // defined wrapping also for a negative increment
        output_dc_ += (shifted - output_dc_) / 1024;
        // Explicit hard ceiling in the wet/feedback path. No auto gain or hidden
        // feedback compensation; loud inputs/large feedback can deliberately clip.
        const int32_t wet = shift_dsp::clamp(int64_t(shifted) - output_dc_,
                                            shift_dsp::FS - 256);
        feedback_ = wet;
        const int64_t output = int64_t(dry) + (int64_t(wet - dry) * mix) / Q30;
        buf[n] = sat_q15(int32_t(output / 256));
    }
}

Unit* make_unit(UnitKind kind) {
    Unit* unit = nullptr;
    switch (kind) {
        case UnitKind::SamplePlayer: unit = new(std::nothrow) PlayUnit(); break;
        case UnitKind::Grain:        unit = new(std::nothrow) GrainUnit(); break;
        case UnitKind::Osc:          unit = new(std::nothrow) OscUnit(); break;
        case UnitKind::Filter:       unit = new(std::nothrow) FilterUnit(); break;
        case UnitKind::Vca:          unit = new(std::nothrow) VcaUnit(); break;
        case UnitKind::Delay:        unit = new(std::nothrow) DelayUnit(); break;
        case UnitKind::Fold:         unit = new(std::nothrow) FoldUnit(); break;
        case UnitKind::MicIn:        unit = new(std::nothrow) MicUnit(); break;
        case UnitKind::Recorder:     unit = new(std::nothrow) RecUnit(); break;
        case UnitKind::Bitcrush:     unit = new(std::nothrow) BitUnit(); break;
        case UnitKind::Chorus:       unit = new(std::nothrow) ChorusUnit(); break;
        case UnitKind::Reverb:       unit = new(std::nothrow) ReverbUnit(); break;
        case UnitKind::Stranular:    unit = new(std::nothrow) StranularUnit(); break;
        case UnitKind::Ghost:        unit = new(std::nothrow) GhostUnit(); break;
        case UnitKind::Ring:         unit = new(std::nothrow) RingUnit(); break;
        case UnitKind::Tilt:         unit = new(std::nothrow) TiltUnit(); break;
        case UnitKind::Noise:        unit = new(std::nothrow) NoiseUnit(); break;
        case UnitKind::Fm:           unit = new(std::nothrow) FmUnit(); break;
        case UnitKind::Motor:        unit = new(std::nothrow) MotorUnit(); break;
        case UnitKind::Strain:       unit = new(std::nothrow) StrainUnit(); break;
        case UnitKind::Shift:        unit = new(std::nothrow) ShiftUnit(); break;
        case UnitKind::Bird:         unit = new(std::nothrow) BirdUnit(); break;
        case UnitKind::Fract:        unit = new(std::nothrow) FractUnit(); break;
        case UnitKind::Comp:         unit = new(std::nothrow) CompUnit(); break;
        case UnitKind::Mouth:        unit = new(std::nothrow) MouthUnit(); break;
        case UnitKind::Arp:          unit = new(std::nothrow) ArpUnit(); break;
        case UnitKind::Glitch:       unit = new(std::nothrow) GlitchUnit(); break;
        case UnitKind::Inst:         unit = new(std::nothrow) InstUnit(); break;
        default:                     return nullptr;
    }
    if (unit && !unit->ready()) { delete unit; unit = nullptr; }
    return unit;
}

Unit* clone_unit(const Unit* source) {
    if (!source) return nullptr;
    Unit* clone = source->kind == UnitKind::Delay
        ? new(std::nothrow) DelayUnit(static_cast<const DelayUnit*>(source)->modern())
        : source->kind == UnitKind::Reverb
        ? new(std::nothrow) ReverbUnit(static_cast<const ReverbUnit*>(source)->modern())
        : source->kind == UnitKind::Noise
        ? new(std::nothrow) NoiseUnit(static_cast<const NoiseUnit*>(source)->modern())
        : source->kind == UnitKind::Motor
        ? new(std::nothrow) MotorUnit(static_cast<const MotorUnit*>(source)->modern())
        : source->kind == UnitKind::Fold
        ? new(std::nothrow) FoldUnit(static_cast<const FoldUnit*>(source)->modern())
        : source->kind == UnitKind::Ring
        ? new(std::nothrow) RingUnit(static_cast<const RingUnit*>(source)->modern())
        : source->kind == UnitKind::Bitcrush
        ? new(std::nothrow) BitUnit(static_cast<const BitUnit*>(source)->modern())
        : source->kind == UnitKind::Strain
        ? new(std::nothrow) StrainUnit(static_cast<const StrainUnit*>(source)->modern())
        : source->kind == UnitKind::Tilt
        ? new(std::nothrow) TiltUnit(static_cast<const TiltUnit*>(source)->revision())
        : make_unit(source->kind);
    if (!clone) return nullptr;
    if (!clone->ready()) { delete clone; return nullptr; }

    clone->bypass = source->bypass;
    clone->random_source = source->random_source; // runtime RNG/edge/undo stay fresh
    const int count = source->num_params < clone->num_params ? source->num_params : clone->num_params;
    for (int param = 0; param < count; ++param) {
        clone->params[param].base = source->params[param].base;
        clone->params[param].src = source->params[param].src;
        clone->params[param].mod_depth = source->params[param].mod_depth;
        clone->params[param].mod_enabled = source->params[param].mod_enabled;
        clone->params[param].snap();
    }

    switch (source->kind) {
        case UnitKind::SamplePlayer: {
            const auto* from = static_cast<const PlayUnit*>(source);
            auto* to = static_cast<PlayUnit*>(clone);
            to->set_modern(from->modern());
            to->set_pitch_mode(from->pitch_mode());
            // The bare destination exposes eleven params until mode is copied.
            // Copy persistent note data explicitly; never runtime overrides/grains.
            auto& note = to->params[PlayUnit::P_NOTE];
            const auto& stored = from->params[PlayUnit::P_NOTE];
            note.base = stored.base; note.src = stored.src;
            note.mod_depth = stored.mod_depth; note.mod_enabled = stored.mod_enabled;
            note.snap();
            to->sample_slot = from->sample_slot;
            to->pitch_quantize = from->pitch_quantize;
            to->restore_edit_state(from->edit_state());
            if (const auto* pending = from->pending_edit_source())
                to->stage_edit_source(*pending, SampleBank::instance().slot(from->sample_slot).empty());
            else if (from->edit_source_matches()) to->bind_edit_source();
            break;
        }
        case UnitKind::Grain: {
            const auto* from = static_cast<const GrainUnit*>(source);
            auto* to = static_cast<GrainUnit*>(clone);
            to->sample_slot = from->sample_slot;
            to->clean = from->clean;
            to->set_launch_mode(from->launch_mode);
            to->set_window(from->window);
            to->set_burst_count(from->burst_count);
            to->clock_sync = from->clock_sync;
            to->sync_division = from->sync_division;
            to->sync_interval_frames = from->sync_interval_frames;
            break;
        }
        case UnitKind::Recorder:
            static_cast<RecUnit*>(clone)->sample_slot = static_cast<const RecUnit*>(source)->sample_slot;
            break;
        case UnitKind::Osc: {
            const auto* from = static_cast<const OscUnit*>(source);
            auto* to = static_cast<OscUnit*>(clone);
            to->wave = from->wave;
            to->pitch_quantize = from->pitch_quantize;
            to->shape_morph = from->shape_morph;
            to->clean = from->clean;
            break;
        }
        case UnitKind::Fm: {
            const auto* from = static_cast<const FmUnit*>(source);
            auto* to = static_cast<FmUnit*>(clone);
            to->phase_reset = from->phase_reset;
            to->alias_safe = from->alias_safe;
            to->drone = from->drone;
            to->set_extended(from->extended);
            to->carrier_wave = from->carrier_wave;
            to->mod_wave = from->mod_wave;
            break;
        }
        case UnitKind::Filter:
            static_cast<FilterUnit*>(clone)->type = static_cast<const FilterUnit*>(source)->type;
            break;
        case UnitKind::Vca:
            static_cast<VcaUnit*>(clone)->set_flexible(static_cast<const VcaUnit*>(source)->flexible);
            static_cast<VcaUnit*>(clone)->set_modern(static_cast<const VcaUnit*>(source)->modern());
            break;
        case UnitKind::Ghost:
            static_cast<GhostUnit*>(clone)->long_tail = static_cast<const GhostUnit*>(source)->long_tail;
            break;
        case UnitKind::Glitch:
            static_cast<GlitchUnit*>(clone)->restore_pattern(static_cast<const GlitchUnit*>(source)->pattern_state());
            break;
        case UnitKind::Arp:
            static_cast<ArpUnit*>(clone)->note_recall = static_cast<const ArpUnit*>(source)->note_recall;
            clone->reset();
            break;
        case UnitKind::Mouth:
        case UnitKind::Reverb:
        case UnitKind::Inst:
            clone->reset(); // copied stored values, never borrow the source's tails/ramps
            break;
        case UnitKind::Stranular: {
            const auto* from = static_cast<const StranularUnit*>(source);
            auto* to = static_cast<StranularUnit*>(clone);
            to->window_mode = from->window_mode;
            to->sync_division = from->sync_division;
            to->sync_interval_frames = from->sync_interval_frames;
            to->long_mode = from->long_mode;
            break;
        }
        default:
            break;
    }
    return clone;
}

} // namespace seance

// ============================ BIRD (additive implementation) ============================
namespace seance {
namespace {
uint32_t positive(q15 v) { return uint32_t(std::max(0, int(v))); }
uint32_t table_map(const uint32_t* table, q15 v) {
    const uint32_t position = positive(v) * 64u;
    const uint32_t i = position / 32767u, f = position % 32767u;
    return i == 64 ? table[64] : table[i] +
        uint32_t((uint64_t(table[i + 1] - table[i]) * f + 16383u) / 32767u);
}
int32_t sine(uint32_t p) { return shift_dsp::sine(p) >> 15; }
int32_t smooth(int32_t t) { // Q15 domain [0,32768], smoothstep 3t²-2t³
    const int32_t t2 = int32_t((int64_t(t) * t) >> 15);
    return int32_t((int64_t(t2) * (98304 - 2 * t)) >> 15);
}
uint32_t ratio(int32_t position) { // signed Q30 [-1,+1] -> 2^[-2,+2], Q20
    const uint32_t p = uint32_t(int64_t(position) + (1ll << 30));
    const uint32_t i = p >> 24, f = p & 0xffffffu;
    return i == 128 ? bird_tables::RATIO[128] : bird_tables::RATIO[i] +
        uint32_t((uint64_t(bird_tables::RATIO[i + 1] - bird_tables::RATIO[i]) * f) >> 24);
}
constexpr uint32_t MAX_INCREMENT = uint32_t((10000ull << 32) / ENGINE_SR);
constexpr int32_t BIRD_ONE = 1 << 30;
int32_t bird_contour(int32_t t, int32_t amount) {
    const int32_t neutral = int32_t((int64_t(t) * t) >> 30);
    if (!amount) return neutral; // exact original integer path
    int32_t target;
    if (amount < 0) target = int32_t((int64_t(t) * (BIRD_ONE - t)) >> 28); // arc
    else {
        const int32_t u = BIRD_ONE - t;
        const int32_t square = int32_t((int64_t(u) * u) >> 30);
        target = BIRD_ONE - int32_t((int64_t(square) * square) >> 30); // late hook
    }
    return neutral + int32_t((int64_t(target - neutral) * std::abs(amount)) >> 16);
}
struct BirdPoint { uint32_t increment; int32_t envelope; };
BirdPoint bird_point(const BirdUnit::CallShape& shape, uint32_t age) {
    const uint32_t remaining = shape.duration - 1 - age;
    const int32_t t = int32_t(std::min<uint64_t>(1ull << 30,
        (uint64_t(remaining) * shape.inverse_duration) >> 2));
    int32_t trajectory = bird_contour(t, shape.contour_q16);
    const uint32_t trill_phase = shape.trill_inc * age; // defined modulo Q32
    const int32_t vibrato = shape.trill_inc ? sine(trill_phase) : 0;
    if (shape.trill_inc && shape.syllable_q16) {
        // Smooth periodic descent through the audible crest, ascent in the gap.
        // Multiplying by remaining time returns the final pitch to its base.
        const int32_t local_t = int32_t(int64_t(32768 - vibrato) << 14);
        const int32_t local = int32_t((int64_t(bird_contour(local_t, shape.contour_q16)) * t) >> 30);
        trajectory += int32_t((int64_t(local - trajectory) * shape.syllable_q16) >> 16);
    }
    const int32_t sweep = int32_t((int64_t(shape.sweep_q30) * trajectory) >> 30);
    uint64_t increment = (uint64_t(shape.base_inc) * ratio(sweep)) >> 20;
    int32_t articulation = 32768;
    if (shape.trill_inc) {
        const int32_t lobe = (32768 + sine(trill_phase + 0x40000000u)) >> 1;
        const int32_t square = int32_t((int64_t(lobe) * lobe) >> 15);
        const int32_t fourth = int32_t((int64_t(square) * square) >> 15);
        articulation = 2621 + int32_t((int64_t(30147) * fourth) >> 15);
        increment = uint64_t(int64_t(increment) + ((int64_t(increment) * vibrato) >> 22));
    }
    const int32_t attack = smooth(int32_t(std::min<uint32_t>(128, age) << 8));
    const int32_t release = smooth(int32_t(std::min<uint32_t>(320, remaining) * 32768u / 320u));
    const int32_t body = 14746 + int32_t((int64_t(18022) * t) >> 30);
    int32_t envelope = int32_t((int64_t(attack) * release) >> 15);
    envelope = int32_t((int64_t(envelope) * body) >> 15);
    envelope = int32_t((int64_t(envelope) * articulation) >> 15);
    return {uint32_t(std::min<uint64_t>(MAX_INCREMENT, increment)), envelope};
}
}

uint32_t BirdUnit::pitch_millihz(q15 value) { return table_map(bird_tables::PITCH, value); }
int32_t BirdUnit::sweep_cents(q15 value) {
    const int32_t p = std::max(-32767, int(value));
    const int32_t c = (std::abs(p) * 2400 + 16383) / 32767;
    return p < 0 ? -c : c;
}
uint32_t BirdUnit::duration_samples(q15 value) { return table_map(bird_tables::DURATION, value); }
uint32_t BirdUnit::trill_millihz(q15 value) {
    return uint32_t((uint64_t(positive(value)) * 40000u + 16383u) / 32767u);
}
BirdUnit::CallShape BirdUnit::prepare_shape(const q15* values) {
    CallShape shape;
    shape.duration = duration_samples(values[P_TIME]);
    shape.inverse_duration = uint32_t(((1ull << 32) + (shape.duration - 2) / 2) / (shape.duration - 1));
    shape.base_inc = uint32_t((uint64_t(pitch_millihz(values[P_PITCH])) << 32) / (ENGINE_SR * 1000ull));
    shape.trill_inc = uint32_t((uint64_t(trill_millihz(values[P_TRILL])) << 32) / (ENGINE_SR * 1000ull));
    shape.sweep_q30 = int32_t(int64_t(sweep_cents(values[P_SWEEP])) * (1ll << 30) / 2400);
    shape.contour_q16 = int32_t(int64_t(std::max(-32767, int(values[P_CONTOUR]))) * 65536 / 32767);
    shape.syllable_q16 = uint32_t(uint64_t(positive(values[P_SYLLABLE])) * 65536 / 32767);
    return shape;
}
BirdUnit::ShapePoint BirdUnit::shape_point(const CallShape& shape, uint32_t age) {
    if (!shape.duration) return {};
    const BirdPoint point = bird_point(shape, std::min(age, shape.duration - 1));
    return {uint32_t((uint64_t(point.increment) * (ENGINE_SR * 1000ull) + (1ull << 31)) >> 32),
        q15(age < shape.duration ? std::min<int32_t>(32767, point.envelope) : 0)};
}
BirdUnit::Visual BirdUnit::visual() const {
    Visual result;
    result.active = age_ < shape_.duration;
    result.age = age_;
    result.duration = shape_.duration;
    std::copy(latched_, latched_ + P_COUNT, result.latched);
    return result;
}
BirdUnit::BirdUnit() {
    kind = UnitKind::Bird;
    add_param("PITCH", 21223); // about 2.500 kHz
    add_param("SWEEP", 12288, true); // +900 cents, returning down to PITCH
    add_param("TIME", 13360); // about 180 ms
    add_param("TRILL", 0);
    add_param("AIR", 2621); // 8% colour, not literal noise volume
    add_param("GAIN", 18022); // 55%
    add_param("CONTOUR", 0, true);
    add_param("SYLLABLE", 0);
    reset();
}
void BirdUnit::reset() {
    shape_ = {};
    std::fill(latched_, latched_ + P_COUNT, q15(0));
    age_ = phase_ = 0;
    rng_ = 0x42495244u;
    air_ = noise_low_ = noise_slow_ = 0;
    last_ = retrigger_from_ = 0;
    retrigger_left_ = 0;
    gain_.snap(int32_t(positive(params[P_GAIN].effective)));
}
void BirdUnit::control_tick() {
    const int32_t gain = int32_t(positive(params[P_GAIN].effective));
    // No voice can be revived by a coefficient/level edit. Silent changes snap;
    // an audible call retargets a sample-clock 128-sample exact-endpoint ramp.
    if (age_ >= shape_.duration) gain_.snap(gain); else gain_.set(gain);
}
void BirdUnit::trigger() {
    control_tick();
    retrigger_from_ = last_;
    retrigger_left_ = age_ < shape_.duration ? 64 : 0;
    for (int p = 0; p < P_COUNT; ++p) latched_[p] = params[p].effective;
    shape_ = prepare_shape(latched_);
    air_ = int32_t(positive(latched_[P_AIR]));
    age_ = phase_ = 0;
    noise_low_ = noise_slow_ = 0;
    // RNG is local and advances across strikes, never borrowed from the engine.
    // reset() alone restores its seed. Even AIR=0 advances it for stable timing.
}
void BirdUnit::process(q15* buf, int frames) {
    for (int n = 0; n < frames; ++n) {
        const int32_t gain = gain_.tick();
        if (age_ >= shape_.duration) { last_ = 0; buf[n] = 0; continue; }
        const BirdPoint point = bird_point(shape_, age_);
        const int32_t whistle = sine(phase_);
        const int32_t harmonic = sine(phase_ * 2u);
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        const int32_t white = int32_t(rng_ >> 16) - 32768;
        noise_low_ += (white - noise_low_) >> 2;
        noise_slow_ += (noise_low_ - noise_slow_) >> 5;
        const int32_t noise = std::max<int32_t>(-32768, std::min<int32_t>(32767, noise_low_ - noise_slow_));
        const int32_t harmonic_gain = air_ >> 3, noise_gain = air_ >> 2;
        const int32_t whistle_gain = 32768 - harmonic_gain - noise_gain;
        int32_t sample = int32_t((int64_t(whistle) * whistle_gain +
            int64_t(harmonic) * harmonic_gain + int64_t(noise) * noise_gain) >> 15);
        sample = int32_t((int64_t(sample) * point.envelope) >> 15);
        if (retrigger_left_) {
            sample = (sample * (64 - retrigger_left_) + retrigger_from_ * retrigger_left_) >> 6;
            --retrigger_left_;
        }
        last_ = sample;
        buf[n] = trackr::fx::sat_q15(int32_t((int64_t(sample) * gain) >> 15));
        phase_ += arp_scale_increment(point.increment);
        ++age_;
    }
}
} // namespace seance

// ============================ FRACT ============================
namespace seance {
namespace {
constexpr uint64_t FRACT_GRID = 1ull << 30; // one straight sixteenth note
uint32_t fract_beat(int32_t bpm) {
    bpm = std::max<int32_t>(20 * 65536, std::min<int32_t>(300 * 65536, bpm));
    return uint32_t((uint64_t(ENGINE_SR) * 60 * 65536 + bpm / 2) / uint32_t(bpm));
}
uint32_t fract_seam(uint32_t length) {
    // Power-of-two boundary fade: 8 frames at the 2ms floor, 32 for longer fragments.
    return length >= 256 ? 32 : length >= 128 ? 16 : 8;
}
int32_t fract_ramp(int32_t current, int32_t target, int32_t step) {
    return current < target ? std::min(current + step, target) : std::max(current - step, target);
}
}
uint32_t FractUnit::size_frames(q15 size, bool sync, int32_t bpm_q16) {
    const uint32_t p = positive(size);
    uint32_t frames;
    if (sync) {
        // Seven divisions: 1/128, 1/64, 1/32, 1/16, 1/8, 1/4, 1/2 note.
        const uint32_t index = p * 7u / 32768u;
        frames = uint32_t((uint64_t(fract_beat(bpm_q16)) << index) / 32u);
    } else {
        static constexpr uint32_t nodes[] = {64,128,256,512,1024,2048,4096,8192,16000,32000};
        const uint32_t scaled = p * 9u, index = scaled / 32767u, f = scaled % 32767u;
        frames = index == 9 ? nodes[9] : nodes[index] + (nodes[index+1]-nodes[index])*f/32767u;
    }
    return std::max(MIN_LOOP, std::min(MAX_CAPTURE, frames));
}
const char* FractUnit::size_label(q15 size) {
    static const char* const labels[] = {"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2"};
    return labels[positive(size) * 7u / 32768u];
}
uint32_t FractUnit::length_frames(q15 length, int32_t bpm_q16) {
    // 1/16, 1/8, 1/4, 1/2, 1, 2, 4 beats; independent of fragment size.
    return (fract_beat(bpm_q16) << (positive(length) * 7u / 32768u)) / 16u;
}
int32_t FractUnit::speed_q16(q15 speed) {
    const int32_t p = std::max(-32767, int(speed));
    const int32_t magnitude = std::max<int32_t>(16384, std::abs(p) * 8);
    return p < 0 ? -magnitude : magnitude;
}
FractUnit::FractUnit() {
    kind = UnitKind::Fract;
    add_param("SIZE", 16384); add_param("LENGTH", 21065);
    add_param("CHANCE", 0); add_param("DAMAGE", 0);
    add_param("SPEED", 8192, true); add_param("MIX", 32767);
    add_param("SYNC", 32767); add_param("HOLD", 0);
    banks_ = new(std::nothrow) q15[CAPACITY * 2]; // uninitialised: guarded by fill
    reset();
}
FractUnit::~FractUnit() { delete[] banks_; }
void FractUnit::reset() {
    write_ = fill_[0] = fill_[1] = capture_start_ = 0;
    capture_frames_ = loop_frames_ = auto_remaining_ = 0;
    live_bank_ = 0; frozen_bank_ = -1; position_ = 0;
    manual_ = held_ = pending_grab_ = hold_high_ = hold_blocked_ = false;
    running_ = suspended_ = false; reverse_ = false;
    repeats_ = edge_remaining_ = 0; wet_ = 0; last_wet_ = edge_from_ = 0;
    clock_phase_ = last_grid_ = 0; clock_remainder_ = clock_increment_ = clock_fraction_step_ = 0;
    clock_divisor_ = 1; rng_ = 0x46524143u;
    control_tick(); speed_ = speed_target_; reverse_ = speed_ < 0;
    stop_transport();
}
void FractUnit::control_tick() {
    size_ = size_frames(params[P_SIZE].effective, params[P_SYNC].effective > 16383, bpm_q16_);
    length_ = length_frames(params[P_LENGTH].effective, bpm_q16_);
    damage_ = positive(params[P_DAMAGE].effective);
    chance_ = positive(params[P_CHANCE].effective);
    speed_target_ = speed_q16(params[P_SPEED].effective);
    mix_ = int32_t(positive(params[P_MIX].effective));
    const bool high = params[P_HOLD].effective > 16383;
    if (!high) {
        if (params[P_HOLD].control_source() <= 16383) hold_blocked_ = false;
        held_ = false;
    }
    else if (!hold_high_ && !hold_blocked_ && !suspended_) {
        held_ = true;
        if (frozen_bank_ < 0) pending_grab_ = true;
    }
    hold_high_ = high;
}
void FractUnit::set_transport_bpm_q16(int32_t bpm) {
    bpm = std::max<int32_t>(20 * 65536, std::min<int32_t>(300 * 65536, bpm));
    if (bpm == bpm_q16_) return;
    bpm_q16_ = bpm; control_tick();
}
void FractUnit::set_manual_grab(bool down) {
    if (suspended_ || bypass) down = false;
    if (down && !manual_) {
        // Manual takes ownership of an already repeating fragment; otherwise
        // captures current history on the next audio sample, not on the UI thread.
        if (frozen_bank_ < 0 || (!held_ && !auto_remaining_ && !wet_)) pending_grab_ = true;
        auto_remaining_ = 0;
    }
    const bool released = manual_ && !down;
    manual_ = down;
    if (released && !held_) { auto_remaining_ = 0; pending_grab_ = false; }
}
void FractUnit::stop_transport() {
    manual_ = held_ = pending_grab_ = false; auto_remaining_ = 0;
    hold_blocked_ = params[P_HOLD].effective > 16383 || params[P_HOLD].control_source() > 16383;
    running_ = false;
}
void FractUnit::set_enabled(bool enabled) {
    if (enabled) { suspended_ = false; return; }
    if (suspended_) return;
    stop_transport(); suspended_ = true;
    fill_[0] = fill_[1] = 0; write_ = 0; live_bank_ = 0; frozen_bank_ = -1;
    capture_frames_ = loop_frames_ = 0; wet_ = 0; edge_remaining_ = repeats_ = 0;
}
void FractUnit::set_transport_clock(bool running, uint64_t phase, uint32_t increment,
                                     uint32_t remainder, uint32_t divisor) {
    if (running_ && !running) stop_transport();
    // Resume waits for the next actual grid edge; no extra decision at play.
    if (!running_ && running) last_grid_ = phase / FRACT_GRID;
    running_ = running; clock_phase_ = phase; clock_increment_ = increment;
    clock_divisor_ = std::max<uint32_t>(1u, divisor); clock_remainder_ = remainder;
    clock_fraction_step_ = clock_divisor_ > 1 ? uint32_t((1ull << 32) % clock_divisor_) : 0;
}
bool FractUnit::capture() {
    if (!banks_ || fill_[live_bank_] < MIN_LOOP) return false;
    const int next = 1 - live_bank_;
    if (frozen_bank_ == next) fill_[next] = 0; // was frozen, not contiguous live data
    frozen_bank_ = live_bank_; live_bank_ = next;
    capture_frames_ = loop_frames_ = std::min(size_, fill_[frozen_bank_]);
    capture_start_ = (write_ + CAPACITY - capture_frames_) & (CAPACITY - 1);
    position_ = speed_ < 0 ? int64_t(loop_frames_ - 1) * 65536 : 0;
    repeats_ = 0; edge_from_ = last_wet_; edge_remaining_ = wet_ ? 32 : 0;
    // O(1): not a single PCM frame is copied or traversed at capture.
    return true;
}
q15 FractUnit::read_loop(int64_t phase) const {
    const uint32_t index = uint32_t(phase >> 16), fraction = uint32_t(phase) & 65535;
    const uint32_t next = index + 1 < loop_frames_ ? index + 1 : index;
    const q15* bank = banks_ + frozen_bank_ * CAPACITY;
    const int32_t a = bank[(capture_start_ + index) & (CAPACITY - 1)];
    const int32_t b = bank[(capture_start_ + next) & (CAPACITY - 1)];
    return q15(a + ((int64_t(b-a)*fraction) >> 16));
}
q15 FractUnit::repeat_sample() {
    int32_t sample = read_loop(position_);
    const uint32_t fade = fract_seam(loop_frames_);
    const int64_t overlap = int64_t(fade) * 65536;
    const int64_t tail = int64_t(loop_frames_ - fade) * 65536;
    const unsigned alpha_shift = fade == 32 ? 6 : fade == 16 ? 5 : 4;
    // Fade toward the NEXT boundary sample without borrowing or skipping any
    // phase. Full nominal loop timing is preserved, including at the 2ms floor.
    int64_t boundary = 0; int32_t alpha = 0;
    if (!reverse_ && position_ >= tail) {
        alpha = int32_t((position_ - tail) >> alpha_shift);
    } else if (reverse_ && position_ < overlap) {
        boundary = int64_t(loop_frames_ - 1) * 65536;
        alpha = int32_t((overlap - position_) >> alpha_shift);
    }
    if (alpha) sample += int32_t((int64_t(int32_t(read_loop(boundary))-sample)*alpha) >> 15);
    if (edge_remaining_) {
        sample = (sample*(32-edge_remaining_) + int32_t(edge_from_)*edge_remaining_) / 32;
        --edge_remaining_;
    }
    return q15(sample);
}
void FractUnit::advance_loop() {
    position_ += speed_;
    const int64_t limit = int64_t(loop_frames_) * 65536;
    if (position_ >= 0 && position_ < limit) return;
    if (repeats_ < 65535) ++repeats_;
    const uint32_t old_length = loop_frames_;
    if (damage_ && loop_frames_ > MIN_LOOP) {
        const uint32_t shrink = std::max<uint32_t>(1u, uint32_t((uint64_t(loop_frames_) * damage_) >> 18));
        loop_frames_ = std::max(MIN_LOOP, loop_frames_ - shrink); // <=12.5% per wrap
    }
    if (position_ < 0) position_ += int64_t(loop_frames_) * 65536;
    else position_ -= limit;
    if (old_length != loop_frames_) { edge_from_ = last_wet_; edge_remaining_ = 32; }
}
void FractUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0) return;
    if (bypass || suspended_ || !banks_) { if (bypass) set_enabled(false); return; }
    for (int n = 0; n < frames; ++n) {
        const q15 dry = buf[n];
        if (running_) {
            const uint64_t grid = clock_phase_ / FRACT_GRID;
            if (grid != last_grid_) {
                last_grid_ = grid;
                rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                // Exactly one RNG decision per grid even if chance=0 or held.
                if (!manual_ && !held_ && !auto_remaining_ && !wet_ &&
                    (chance_ == 32767 || (rng_ & 32767u) < chance_) && capture())
                    auto_remaining_ = length_;
            }
        }
        if (pending_grab_ && (manual_ || held_) && capture()) pending_grab_ = false;
        const bool active = frozen_bank_ >= 0 && (manual_ || held_ || auto_remaining_);
        // No output is ever fed back to either input history bank.
        banks_[live_bank_*CAPACITY + write_] = dry;
        fill_[live_bank_] = std::min(MAX_CAPTURE, fill_[live_bank_] + 1);
        if (frozen_bank_ < 0) {
            banks_[(1-live_bank_)*CAPACITY + write_] = dry;
            fill_[1-live_bank_] = std::min(MAX_CAPTURE, fill_[1-live_bank_] + 1);
        }
        write_ = (write_ + 1) & (CAPACITY - 1);
        if (frozen_bank_ >= 0) {
            // Slew magnitude, then declick direction changes without passing
            // through a stuck zero-speed head.
            const bool reverse = speed_target_ < 0;
            const int32_t magnitude = fract_ramp(std::abs(speed_), std::abs(speed_target_), 1024);
            if (reverse != reverse_) { edge_from_ = last_wet_; edge_remaining_ = 32; }
            reverse_ = reverse; speed_ = reverse ? -magnitude : magnitude;
            last_wet_ = repeat_sample();
            const int32_t target = active ? mix_ + (mix_ == 32767) : 0;
            wet_ = fract_ramp(wet_, target, 256); // full-scale 4ms enter/release
            // Convex mix endpoints are exact, including dry identity at wet=0.
            buf[n] = q15(int32_t(dry) + ((int64_t(int32_t(last_wet_)-dry)*wet_) >> 15));
            advance_loop();
            if (!active && !wet_) {
                fill_[frozen_bank_] = 0; frozen_bank_ = -1;
                loop_frames_ = capture_frames_ = 0;
            }
        } else { wet_ = 0; speed_ = speed_target_; reverse_ = speed_ < 0; }
        if (auto_remaining_) --auto_remaining_;
        if (running_) {
            clock_phase_ += clock_increment_;
            clock_remainder_ += clock_fraction_step_;
            if (clock_remainder_ >= clock_divisor_) { ++clock_phase_; clock_remainder_ -= clock_divisor_; }
        }
    }
}
FractUnit::Visual FractUnit::visual() const {
    Visual out;
    if (loop_frames_ && frozen_bank_ >= 0) for (uint32_t i = 0; i < 48; ++i) {
        const uint32_t at = (capture_start_ + i * (loop_frames_ - 1) / 47) & (CAPACITY - 1);
        out.wave[i] = banks_[frozen_bank_ * CAPACITY + at];
    }
    out.loop_frames = loop_frames_; out.capture_frames = capture_frames_;
    out.phase = loop_frames_ ? uint16_t((uint64_t(position_) * 32767u) / (uint64_t(loop_frames_) * 65536)) : 0;
    out.repeats = repeats_; out.wet = uint16_t(std::min<int32_t>(32767, wet_));
    out.fill = uint16_t(uint64_t(std::min(size_, fill_[live_bank_])) * 32767u / size_);
    out.active = frozen_bank_ >= 0 && (manual_ || held_ || auto_remaining_ || wet_);
    out.manual = manual_; out.held = held_;
    return out;
}
} // namespace seance
