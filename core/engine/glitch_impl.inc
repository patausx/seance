// Included by units.cpp to retain existing portable/ARM link lists.
namespace seance {
namespace {
constexpr uint64_t GLITCH_GRID = 1ull << 30;
constexpr uint64_t GLITCH_BAR = GLITCH_GRID * GlitchUnit::STEPS;
int32_t glitch_ramp(int32_t value, int32_t target) {
    return value < target ? std::min(value + 256, target) : std::max(value - 256, target);
}
}

GlitchUnit::GlitchUnit() {
    kind = UnitKind::Glitch;
    add_param("DENSITY", 11468); // 35%, independent of relative operation weights
    add_param("JUMP", 24000); add_param("REPEAT", 20000); add_param("DROP", 7000);
    add_param("REVERSE", 12000); add_param("PITCH", 10000);
    add_param("MIX", 32767); add_param("KEEP", 0);
    // Uninitialized storage is intentional: only a validated complete bank is read.
    banks_ = new(std::nothrow) q15[2u * CAPACITY];
    generate();
    reset();
}
GlitchUnit::~GlitchUnit() { delete[] banks_; }
uint32_t GlitchUnit::random() {
    uint32_t x = pattern_.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return pattern_.rng = x;
}
void GlitchUnit::generate() {
    uint32_t weights[5], total = 0;
    for (int i=0; i<5; ++i) {
        weights[i] = uint32_t(std::max(0, int(params[P_JUMP+i].target)));
        total += weights[i];
    }
    const uint32_t density = uint32_t(std::max(0, int(params[P_DENSITY].target)));
    for (int i=0; i<STEPS; ++i) {
        Cell cell; cell.source = uint8_t(i);
        // Endpoints are unconditional; all-zero weights intentionally produce dry.
        if (total && density && (density == 32767 || random() % 32767u < density)) {
            uint32_t choice = random() % total;
            int op=0;
            while (op<4 && choice >= weights[op]) { choice -= weights[op]; ++op; }
            cell.op = Op(op+1);
            if (cell.op == Op::Jump) cell.source = uint8_t((i+1+random()%15u)%STEPS);
            if (cell.op == Op::Repeat) cell.repeats = random() & 1u ? 2 : 4;
            if (cell.op == Op::Pitch) {
                static constexpr int8_t pitches[] = {-12,-7,7,12};
                cell.semitones = pitches[random() & 3u];
            }
        }
        pattern_.cells[i] = cell;
    }
    ++pattern_.revision;
    pending_ = false;
}
bool GlitchUnit::valid_pattern(const PatternState& state) {
    if (!state.rng) return false;
    for (const Cell& cell : state.cells) {
        if (uint8_t(cell.op) > uint8_t(Op::Pitch) || cell.source >= STEPS) return false;
        if (cell.semitones < -24 || cell.semitones > 24) return false;
        if (cell.op != Op::Pitch && cell.semitones != 0) return false;
        if (cell.op == Op::Repeat) { if (cell.repeats != 2 && cell.repeats != 4) return false; }
        else if (cell.repeats != 1) return false;
    }
    return true;
}
bool GlitchUnit::restore_pattern(const PatternState& state) {
    if (!valid_pattern(state)) return false;
    pattern_ = state;
    reset();
    return true;
}
void GlitchUnit::invalidate() {
    write_ = previous_frames_ = read_length_ = 0;
    live_bank_ = 0; previous_bank_ = -1;
    recording_ = grid_seen_ = overflow_ = false;
    visual_phase_ = visual_fill_ = 0; step_ = 0;
    seam_left_ = 0;
    // Neither PCM bank is cleared/scanned/copied here, even on an audio edge.
}
void GlitchUnit::reset() {
    invalidate();
    running_ = suspended_ = pending_ = false;
    clock_phase_ = last_grid_ = 0;
    clock_increment_ = clock_remainder_ = clock_fraction_step_ = 0; clock_divisor_ = 1;
    wet_ = 0; last_wet_ = seam_from_ = 0;
    control_tick();
}
void GlitchUnit::control_tick() {
    // Discrete KEEP consumes the resolved target, never an in-between slew value.
    kept_ = params[P_KEEP].target > 16383;
    // Our own finite 4ms audio ramp reaches both exact endpoints (no slew residue).
    mix_ = std::max(0, int(params[P_MIX].target));
}
void GlitchUnit::request_reroll() {
    if (running_ && !suspended_ && !bypass) pending_ = true;
    else { control_tick(); generate(); }
}
void GlitchUnit::stop_transport() {
    invalidate(); running_ = false;
    if (pending_) generate(); // no audible cell exists now; commit one coalesced request
    // A scalar-only 4ms release may remain; invalid PCM is never consulted.
}
void GlitchUnit::set_enabled(bool enabled) {
    if (!enabled) {
        if (!suspended_) stop_transport();
        suspended_ = true;
    } else if (suspended_) {
        suspended_ = false; wet_ = 0; last_wet_ = 0;
    }
}
void GlitchUnit::set_transport_clock(bool running, uint64_t phase, uint32_t increment,
                                      uint32_t remainder, uint32_t divisor) {
    running = running && !suspended_;
    if (!running) {
        if (running_) stop_transport();
    } else if (!running_) {
        // Start/resume cannot expose a previous transport's PCM or a partial bar.
        invalidate(); wet_ = 0; last_wet_ = 0;
    } else if (phase != clock_phase_) {
        const bool retimed = increment != clock_increment_ || divisor != clock_divisor_;
        if (retimed && grid_seen_ && phase/GLITCH_BAR == last_grid_/STEPS) {
            // Legacy set_bpm can reinterpret frame_in_beat rather than preserve
            // fractional phase. Keep the valid previous bar's recorded cuts;
            // reject only the now-discontinuous in-progress recording.
            recording_ = false; write_ = 0; visual_fill_ = 0;
            const uint64_t grid = phase/GLITCH_GRID;
            if (grid != last_grid_) { step_ = uint8_t(grid%STEPS); enter_cell(); }
            last_grid_ = grid; grid_seen_ = true;
        } else { invalidate(); wet_ = 0; last_wet_ = 0; }
    }
    running_ = running; clock_phase_ = phase; clock_increment_ = increment;
    clock_divisor_ = std::max<uint32_t>(1u, divisor);
    clock_remainder_ = remainder % clock_divisor_;
    clock_fraction_step_ = clock_divisor_ > 1 ? uint32_t((1ull << 32) % clock_divisor_) : 0;
}
void GlitchUnit::grid_edge(uint64_t grid) {
    step_ = uint8_t(grid % STEPS);
    if (!step_) {
        const uint64_t bar = grid / STEPS;
        bool complete = recording_ && !overflow_ && bar == record_bar_+1 && write_ > 0;
        cuts_[live_bank_][STEPS] = write_;
        if (complete) for (int i=0; i<STEPS; ++i)
            if (cuts_[live_bank_][i] >= cuts_[live_bank_][i+1]) complete = false;
        previous_bank_ = complete ? live_bank_ : -1;
        previous_frames_ = complete ? write_ : 0;
        if (complete) live_bank_ = 1-live_bank_;
        write_ = 0; cuts_[live_bank_][0] = 0;
        record_bar_ = bar; recording_ = true; overflow_ = false;
        if (pending_ || !kept_) generate();
    } else if (recording_) cuts_[live_bank_][step_] = write_;
    enter_cell();
}
void GlitchUnit::enter_cell() {
    const Cell& cell = pattern_.cells[step_];
    read_length_ = 0;
    if (previous_bank_ >= 0 && cell.op != Op::Dry && cell.op != Op::Drop) {
        read_start_ = cuts_[previous_bank_][cell.source];
        read_length_ = cuts_[previous_bank_][cell.source+1]-read_start_;
        if (cell.op == Op::Repeat) read_length_ = std::max<uint32_t>(1u, read_length_/cell.repeats);
        read_speed_ = cell.op == Op::Reverse ? -65536 : cell.op == Op::Pitch
            ? int32_t(Unit::arp_ratio_q24(cell.semitones) >> 8) : 65536;
        read_phase_ = read_speed_ < 0 ? int64_t(read_length_-1)*65536 : 0;
    }
    seam_from_ = last_wet_; seam_left_ = 32;
}
q15 GlitchUnit::slice_sample() {
    const uint32_t index = uint32_t(read_phase_ >> 16);
    const uint32_t fraction = uint32_t(read_phase_) & 65535u;
    const q15* source = banks_ + uint32_t(previous_bank_)*CAPACITY + read_start_;
    const int32_t a = source[index];
    const int32_t b = source[index+1 < read_length_ ? index+1 : index];
    int32_t sample = a + int32_t((int64_t(b-a)*fraction) >> 16);
    if (seam_left_) {
        sample += int32_t((int64_t(int32_t(seam_from_)-sample)*seam_left_) / 32);
        --seam_left_;
    }
    read_phase_ += read_speed_;
    const int64_t limit = int64_t(read_length_)*65536;
    if (read_phase_ < 0 || read_phase_ >= limit) {
        // Only at a slice wrap, never a per-sample division on the normal path.
        read_phase_ %= limit; if (read_phase_ < 0) read_phase_ += limit;
        seam_from_ = q15(sample); seam_left_ = 32;
    }
    return q15(sample);
}
void GlitchUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0 || !banks_) return;
    if (bypass) set_enabled(false);
    for (int n=0; n<frames; ++n) {
        const q15 dry = buf[n];
        if (running_ && !suspended_) {
            const uint64_t grid = clock_phase_/GLITCH_GRID;
            if (!grid_seen_) {
                grid_seen_ = true; last_grid_ = grid; step_ = uint8_t(grid%STEPS);
                // With no preceding sample we can certify only an exact boundary.
                // Even a one-sample-late enable must wait for an observed crossing.
                if (clock_phase_%GLITCH_BAR == 0) grid_edge(grid);
            } else if (grid != last_grid_) {
                if (grid != last_grid_+1) invalidate();
                grid_seen_ = true; last_grid_ = grid;
                grid_edge(grid);
            }
            visual_phase_ = uint16_t((clock_phase_ & (GLITCH_GRID-1)) >> 15);
            visual_fill_ = recording_ ? uint16_t((clock_phase_%GLITCH_BAR) >> 19) : 0;
        }
        const Cell& cell = pattern_.cells[step_];
        const bool altered = running_ && !suspended_ && previous_bank_ >= 0 && cell.op != Op::Dry;
        if (altered) {
            if (cell.op == Op::Drop) {
                if (seam_left_) { last_wet_ = q15(int32_t(seam_from_)*seam_left_/32); --seam_left_; }
                else last_wet_ = 0;
            } else if (read_length_) last_wet_ = slice_sample();
        }
        const int32_t target = altered ? mix_+(mix_==32767) : 0;
        wet_ = glitch_ramp(wet_, target);
        buf[n] = q15(int32_t(dry) + ((int64_t(int32_t(last_wet_)-dry)*wet_) >> 15));
        // Capture unprocessed live input, never our own shuffled output.
        if (running_ && recording_ && !suspended_) {
            if (write_ < CAPACITY) banks_[uint32_t(live_bank_)*CAPACITY + write_++] = dry;
            else overflow_ = true; // malformed/too-slow clocks fail dry next bar, never overwrite
        }
        if (running_) {
            clock_phase_ += clock_increment_;
            clock_remainder_ += clock_fraction_step_;
            if (clock_remainder_ >= clock_divisor_) { ++clock_phase_; clock_remainder_ -= clock_divisor_; }
        }
    }
}
GlitchUnit::Visual GlitchUnit::visual() const {
    Visual out;
    for (int i=0; i<STEPS; ++i) out.cells[i] = pattern_.cells[i];
    out.ready = previous_bank_ >= 0 && previous_frames_ > 0;
    // Four samples PER actual stored slice; variable-tempo bars need not have
    // equal frame lengths. Never draw another slice's PCM under this slice's id.
    if (out.ready) for (uint32_t i=0; i<STEPS; ++i) {
        const uint32_t start = cuts_[previous_bank_][i];
        const uint32_t span = cuts_[previous_bank_][i+1] - start;
        for (uint32_t k=0; k<4; ++k)
            out.trace[i*4+k] = banks_[uint32_t(previous_bank_)*CAPACITY + start + k*(span-1)/3u];
    }
    out.phase = visual_phase_; out.fill = visual_fill_;
    out.wet = uint16_t(std::min<int32_t>(wet_, 32767)); out.step = step_; out.source = pattern_.cells[step_].source;
    out.revision = pattern_.revision; out.running = running_ && !suspended_;
    out.kept = params[P_KEEP].target > 16383; out.pending = pending_;
    return out;
}
} // namespace seance
