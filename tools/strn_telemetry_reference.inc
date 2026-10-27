// Frozen pre-optimization StranularUnit::process, 2026-10-01.
// The narrow test compiles this exact body as a second class with the unchanged
// production constructor/spawn/pitch helpers; no production test hooks.
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
                visual_positions_[gi] = (uint8_t)((visual_span - 1 - grain_back) *
                    (VIZ_POINTS - 1) / (visual_span - 1));
            }
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
            visual_increments_[gi] = g.inc_q16;
            if (++g.age >= g.length) g.active = false;
        }
        q15 wet = active ? sat_q15(sum / active) : dry;
        q31 effective_mix = freeze_mix_ == Q15_ONE ? Q15_ONE :
            mix + (((q31)freeze_mix_ * (32767 - mix)) >> 15);
        if (effective_mix > 32767) effective_mix = 32767;
        visual_mix_ = (q15)effective_mix;
        buf[i] = sat_q15((q31)dry + ((((q31)wet - dry) * effective_mix) >> 15));
        visual_grains_ = grain_mask;
        if (++visual_decim_ >= 64) {
            visual_decim_ = 0;
            const uint32_t span = frozen_ ? freeze_span_ :
                (long_mode ? filled_ : (filled_ < 16384u ? filled_ : 16384u));
            const uint32_t reference = frozen_ ? freeze_write_ : write_;
            const uint32_t target_span = long_mode ? BUFFER_SIZE : 16384u;
            visual_fill_ = (uint16_t)(span >= target_span ? 32767u :
                (uint32_t)((uint64_t)span * 32767u / target_span));
            for (int p = 0; p < VIZ_POINTS; ++p) {
                const uint32_t back = span > 1
                    ? (uint32_t)((uint64_t)(VIZ_POINTS - 1 - p) * (span - 1) /
                                 (VIZ_POINTS - 1)) : 0;
                visual_wave_[p] = span
                    ? buffer_[(reference + BUFFER_SIZE - 1 - back) & (BUFFER_SIZE - 1)] : 0;
            }
        }
    }
}
