// Included inside namespace seance by units.cpp. New note modes alone use this
// exact-window path; all legacy PCM and editor/gesture routines stay separate.
void PlayUnit::process_musical(q15* buf, int frames, const Sample& sample) {
    const uint32_t nf = sample.playable_frames();
    const auto& w = voice_edit_;
    if (!playing_ || !source_matches(voice_source_) || !w.valid ||
        win_end_ > nf || win_start_ >= win_end_ || win_end_ - win_start_ < 2) {
        playing_ = false; visual_state_ = 0; stretch_.reset();
        std::memset(buf, 0, (size_t)frames * sizeof(q15)); return;
    }
    const bool stretch = pitch_mode_ == PitchMode::Stretch;
    const bool reverse = inc_q16_ < 0;
    const bool loop = (preview_ ? preview_repeat_ : latched_loop_) &&
        w.loop_start >= win_start_ && w.loop_end <= win_end_ &&
        w.loop_start < w.loop_end && w.loop_end - w.loop_start >= 2;
    const int64_t begin = int64_t(win_start_) * 65536, end = int64_t(win_end_) * 65536;
    const int64_t last = end - 65536;
    const int64_t lo = int64_t(w.loop_start) * 65536, hi = int64_t(w.loop_end) * 65536;
    const int64_t cross = loop ? int64_t(std::min(w.crossfade, (w.loop_end-w.loop_start-1)/2)) * 65536 : 0;
    const int64_t period = hi - lo - cross;
    auto wrap = [](int64_t phase, int64_t start, int64_t length) -> int64_t {
        int64_t remainder = (phase - start) % length;
        if (remainder < 0) remainder += length;
        return start + remainder;
    };
    auto read_at = [&](int64_t phase) -> q15 {
        // Clamp the whole phase before deriving both PCM indices. No negative
        // fraction can borrow the preceding sample, even in a 2-frame selection.
        phase = std::max(begin, std::min(last, phase));
        const uint32_t a = uint32_t(phase >> 16), b = std::min(a + 1, win_end_ - 1);
        q15 av, bv;
        if (sample.channels == 2) {
            av = q15((int32_t(sample.data[size_t(a)*2]) + sample.data[size_t(a)*2+1]) >> 1);
            bv = q15((int32_t(sample.data[size_t(b)*2]) + sample.data[size_t(b)*2+1]) >> 1);
        } else { av = sample.data[a]; bv = sample.data[b]; }
        return lerp_q15(av, bv, (uint16_t)phase);
    };
    auto seam_read = [&](int64_t phase) -> q15 {
        q15 value = read_at(phase);
        if (cross) {
            int64_t weight = 0, partner = 0;
            if (!reverse && phase >= hi-cross && phase < hi) {
                weight = phase - (hi-cross); partner = lo + weight;
            } else if (reverse && phase >= lo && phase < lo+cross) {
                weight = lo + cross - phase; partner = hi - weight;
            }
            if (weight > 0) {
                const int32_t mix = int32_t(weight * 32767 / cross);
                value = q15((int64_t(value) * (32767-mix) + int64_t(read_at(partner)) * mix) / 32767);
            }
        }
        return value;
    };
    auto grain_read = [&](int64_t phase) -> q15 {
        // Pitch heads may outrun the temporal cursor. Sustain follows the real
        // accepted loop; finite grains recycle only the accepted trim/cut, while
        // the independent transport ends the one-shot (never a frozen last head).
        if (loop) {
            if (!reverse && phase >= hi) phase = lo + cross + (phase-hi) % period;
            else if (reverse && phase < lo) phase = hi-cross-1-(lo-1-phase)%period;
            if (loop_entered_ && (phase < lo || phase >= hi)) phase = wrap(phase, lo+cross, period);
            if (phase < begin || phase >= end) phase = wrap(phase, begin, end-begin);
        } else if (phase < begin || phase >= end) phase = wrap(phase, begin, end-begin);
        return seam_read(phase);
    };
    auto publish = [&]() {
        visual_position_ = uint16_t(uint64_t(visual_source_frame_) * 32767 / (nf-1));
        visual_start_ = uint16_t(uint64_t(win_start_) * 32767 / (nf-1));
        visual_end_ = uint16_t(uint64_t(win_end_-1) * 32767 / (nf-1));
        visual_speed_ = int16_t(std::max<int32_t>(-32767, std::min<int32_t>(32767, inc_q16_ / 8)));
    };
    for (int i = 0; i < frames; ++i) {
        int64_t phase = pos_ * 65536 + frac_;
        // Once in sustain, a live direction change cannot reopen the trim-only
        // lead-in. Also consume a pending boundary overshoot from the old sign.
        if (loop && phase >= hi && (!reverse || loop_entered_ || phase >= end)) {
            phase = lo+cross+(phase-hi)%period; loop_entered_ = true;
        } else if (loop && phase < lo && (reverse || loop_entered_ || phase < begin)) {
            phase = hi-cross-1-(lo-1-phase)%period; loop_entered_ = true;
        }
        // Reverse holds the first frame for its fractional final interval, just
        // as forward holds the last. Both one-shot lifetimes are ceil(span/rate).
        if ((!reverse && (phase < begin || phase >= end)) ||
            (reverse && (phase <= begin-65536 || phase >= end))) {
            playing_ = false; visual_state_ = 0; stretch_.reset(); publish();
            std::memset(buf+i, 0, (size_t)(frames-i)*sizeof(q15)); return;
        }
        q15 out = stretch ? stretch_.render(phase, note_inc_q16_, inc_q16_, grain_read) : seam_read(phase);
        q15 envelope = Q15_ONE;
        const int64_t ds = std::max<int64_t>(0, phase-begin), de = std::max<int64_t>(0, last-phase);
        const bool selection_preview = preview_ && !preview_seam_;
        if (w.fade_in && (selection_preview || !loop || (!loop_entered_ && !reverse))) {
            const int64_t width = int64_t(w.fade_in) * 65536;
            if (ds < width) envelope = q15(ds * Q15_ONE / width);
        }
        if (w.fade_out && (selection_preview || !loop || (!loop_entered_ && reverse))) {
            const int64_t width = int64_t(w.fade_out) * 65536;
            if (de < width) envelope = std::min(envelope, q15(de * Q15_ONE / width));
        }
        if (amp_q15_ < Q15_ONE) amp_q15_ = q15(std::min<int>(Q15_ONE, amp_q15_+1024));
        q31 gain = q31(params[P_GAIN].effective) * envelope >> 15;
        gain = gain * amp_q15_ >> 15;
        if (release_remaining_) gain = gain * release_remaining_ / 64;
        buf[i] = q15(int32_t(out) * gain >> 15);
        visual_source_frame_ = uint32_t(std::max(begin, std::min(last, phase)) >> 16);
        if (release_remaining_ && !--release_remaining_) {
            playing_ = false; visual_state_ = 0; stretch_.reset(); publish();
            std::memset(buf+i+1, 0, (size_t)(frames-i-1)*sizeof(q15)); return;
        }
        phase += inc_q16_;
        pos_ = phase >> 16; frac_ = (uint16_t)phase;
    }
    publish();
    visual_state_ = VisualPlaying | (reverse ? VisualReverse : 0) | (loop ? VisualLoop : 0);
}
