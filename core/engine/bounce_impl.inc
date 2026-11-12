// Included once by engine.cpp, inside namespace seance. Keeps historical explicit
// host link recipes working without adding a new translation unit.

bool Engine::bounce_clock_safe() const {
    const ClockMod route = clock_mods_[(int)ClockTarget::Tempo];
    return !(route.src != ModSource::None && route.depth) &&
        bpm >= 20 && bpm <= 300 && effective_bpm_q16() == bpm * 65536;
}
bool Engine::bounce_storage_valid() const {
    const auto& v = bounce_.view;
    if (!bounce_source_valid(v.source) || v.slot < 0 || v.slot >= trackr::synth::SAMPLE_BANK_SIZE) return false;
    const auto& bank = trackr::synth::SampleBank::instance();
    const auto& sample = bank.slot(v.slot);
    return bank.generation(v.slot) == v.slot_generation && sample.channels == bounce_channels(v) &&
        sample.num_frames() >= v.target_frames && sample.logical_frames == v.frames;
}
BounceSnapshot Engine::bounce_snapshot() const {
    BounceSnapshot result = bounce_.view;
    if (result.slot >= 0 && !bounce_storage_valid()) {
        result.status = BounceStatus::Cancelled;
        result.reason = BounceReason::SampleChanged;
        result.frames = 0;
        result.saved = false;
    }
    return result;
}
BounceResult Engine::plan_bounce(int bars, BouncePlan& out, BounceSource source) const {
    using namespace trackr::synth;
    if (out.staged.has_storage()) return BounceResult::Busy;
    if (bounce_.view.slot >= 0) return BounceResult::Busy;
    if (!bounce_source_valid(source) || !(bars == 1 || bars == 2 || bars == 4 || bars == 8) || !beats_per_bar)
        return BounceResult::Invalid;
    if (!bounce_clock_safe()) return BounceResult::TempoModulated;
    const uint64_t beats = (uint64_t)bars * beats_per_bar;
    const uint64_t capacity = clock_runtime_
        ? (beats * CLOCK_BEAT + clock_increment_ - 1) / clock_increment_
        : beats * frames_per_beat();
    auto& bank = SampleBank::instance();
    const uint8_t channels = source == BounceSource::Master ? 2 : chain_audio_channels(int(source) - 1);
    if (!channels) return BounceResult::UnsafeChain;
    if (!capacity || capacity > BOUNCE_MAX_FRAMES || capacity * channels * sizeof(q15) > bank.pcm_budget_bytes())
        return BounceResult::MemoryBudget;
    int target = -1;
    for (int pass = 0; pass < 2 && target < 0; ++pass) {
        const int begin = pass == 0 ? 16 : 0, end = pass == 0 ? SAMPLE_BANK_SIZE : 16;
        for (int slot = begin; slot < end; ++slot)
            if (!bank.slot(slot).has_storage() && !sample_slot_refs(slot).total()) {
                target = slot; break;
            }
    }
    if (target < 0) return BounceResult::NoFreeSlot;
    if (!bank.can_replace(target, (size_t)capacity * channels)) return BounceResult::MemoryBudget;
    out.owner = this;
    out.source = source;
    out.channels = channels;
    out.generation = bounce_.view.generation;
    out.slot_generation = bank.generation(target);
    out.capacity_frames = (uint32_t)capacity;
    out.slot = (int8_t)target;
    out.bars = (uint8_t)bars;
    out.beats_per_bar = beats_per_bar;
    out.bpm_q16 = effective_bpm_q16();
    out.clock_increment = clock_increment_;
    out.phase_clock = clock_runtime_;
    return BounceResult::Ok;
}
BounceResult Engine::stage_bounce(BouncePlan& plan) {
    if (!plan.owner || !bounce_source_valid(plan.source) || plan.slot < 0 ||
        plan.slot >= trackr::synth::SAMPLE_BANK_SIZE || !plan.capacity_frames ||
        plan.capacity_frames > BOUNCE_MAX_FRAMES || plan.staged.has_storage())
        return BounceResult::Invalid;
    // Same allocation policy as REC/decoded SampleBank vectors. The checked bank
    // budget is an admission bound; std::vector allocation happens outside guard.
    const uint8_t channels = plan.channels;
    if (channels != 1 && channels != 2) return BounceResult::Invalid;
    plan.staged.channels = channels;
    plan.staged.logical_frames = 0;
#if defined(__cpp_exceptions)
    try { plan.staged.data.resize((size_t)plan.capacity_frames * channels); }
    catch (const std::bad_alloc&) { return BounceResult::MemoryBudget; }
#else
    plan.staged.data.resize((size_t)plan.capacity_frames * channels);
#endif
    return BounceResult::Ok;
}
BounceResult Engine::commit_bounce(BouncePlan& plan, bool start_if_stopped) {
    using namespace trackr::synth;
    if (plan.owner != this || plan.generation != bounce_.view.generation || bounce_.view.slot >= 0)
        return BounceResult::Stale;
    if (!bounce_clock_safe()) return BounceResult::TempoModulated;
    if (!bounce_source_valid(plan.source) || !(plan.bars == 1 || plan.bars == 2 || plan.bars == 4 || plan.bars == 8) ||
        !plan.beats_per_bar || !plan.capacity_frames || plan.capacity_frames > BOUNCE_MAX_FRAMES) return BounceResult::Invalid;
    if (plan.bpm_q16 != effective_bpm_q16() || plan.beats_per_bar != beats_per_bar ||
        plan.phase_clock != clock_runtime_ || (clock_runtime_ && plan.clock_increment != clock_increment_))
        return BounceResult::Stale;
    auto& bank = SampleBank::instance();
    if (plan.slot < 0 || plan.slot >= SAMPLE_BANK_SIZE ||
        bank.generation(plan.slot) != plan.slot_generation || bank.slot(plan.slot).has_storage() ||
        sample_slot_refs(plan.slot).total()) return BounceResult::Stale;
    const uint8_t channels = plan.channels;
    if (channels != 1 && channels != 2) return BounceResult::Invalid;
    if (plan.source == BounceSource::Master && channels != 2) return BounceResult::Invalid;
    if (plan.source != BounceSource::Master && channels != chain_audio_channels(int(plan.source) - 1))
        return BounceResult::Stale;
    const size_t elements = (size_t)plan.capacity_frames * channels;
    if (plan.staged.channels != channels || plan.staged.logical_frames != 0 ||
        plan.staged.data.size() != elements) return BounceResult::Invalid;
    if (!bank.can_replace(plan.slot, elements)) return BounceResult::MemoryBudget;
    const uint64_t beats = (uint64_t)plan.bars * beats_per_bar;
    const bool auto_start = start_if_stopped && !transport_playing;
    uint64_t start, end;
    if (auto_start) {
        start = 0;
        end = clock_runtime_ ? (beats * CLOCK_BEAT + clock_increment_ - 1) / clock_increment_
                             : beats * frames_per_beat();
    } else if (clock_runtime_) {
        const uint64_t phase = clock_next_boundary(LaunchQuantize::Bar);
        start = clock_frame_for_phase(phase);
        end = clock_frame_for_phase(phase + beats * CLOCK_BEAT);
    } else {
        const uint64_t fpb = frames_per_beat();
        start = transport_frame + (beats_per_bar - beat_in_bar()) * fpb - frame_in_beat;
        end = start + beats * fpb;
    }
    if (end <= start || end - start > plan.capacity_frames) return BounceResult::Stale;
    // No rejection below this point. Never move transport for a failed commit.
    if (auto_start) {
        transport_reset();
        // Reset normally drops a phase clock whose routes were removed. Retain
        // the admitted applied Q32 domain/length even in that settled state.
        if (plan.phase_clock && !clock_runtime_) activate_clock_runtime();
    }
    bank.swap_sample_unchecked(plan.slot, plan.staged, SampleOrigin::Runtime, "");
    auto& v = bounce_.view;
    ++v.generation;
    v.slot = plan.slot;
    v.source = plan.source;
    v.channels = channels;
    v.slot_generation = bank.generation(plan.slot);
    v.status = BounceStatus::Armed;
    v.reason = BounceReason::None;
    v.frames = 0;
    v.target_frames = (uint32_t)(end - start);
    v.bars = plan.bars;
    v.beats_per_bar = beats_per_bar;
    v.bpm_q16 = plan.bpm_q16;
    v.saved = false;
    v.start_frame = start;
    v.end_frame = end;
    bounce_.phase_clock = clock_runtime_;
    bounce_.clock_increment = clock_increment_;
    if (auto_start) transport_play();
    return BounceResult::Ok;
}
void Engine::stop_bounce(BounceReason reason) {
    auto& v = bounce_.view;
    if (!v.active()) return;
    v.status = v.frames ? BounceStatus::Partial : BounceStatus::Cancelled;
    v.reason = reason;
}
BounceResult Engine::discard_bounce(trackr::synth::Sample& retired) {
    using namespace trackr::synth;
    if (retired.has_storage()) return BounceResult::Invalid;
    stop_bounce_preview();
    auto& v = bounce_.view;
    if (v.slot < 0) return BounceResult::Ok;
    auto& bank = SampleBank::instance();
    // Discard frees an unreferenced take even when saved; its WAV survives.
    // Shared PLAY/GRAN sources remain ordinary samples after the hold is dropped.
    // Never clear a slot whose generation changed behind this runtime handle.
    if (bounce_storage_valid() && sample_slot_refs(v.slot).total() == 1)
        bank.swap_sample_unchecked(v.slot, retired, SampleOrigin::Runtime, "");
    const uint32_t generation = v.generation + 1;
    bounce_ = {};
    bounce_.view.generation = generation;
    return BounceResult::Ok;
}
const trackr::synth::Sample* Engine::bounce_sample(const BounceSnapshot& token) const {
    const auto& v = bounce_.view;
    if (!v.take() || !token.take() || !bounce_storage_valid() ||
        token.generation != v.generation || token.slot != v.slot ||
        token.slot_generation != v.slot_generation || token.frames != v.frames ||
        !bounce_source_valid(token.source) || token.source != v.source || bounce_channels(token) != bounce_channels(v)) return nullptr;
    return &trackr::synth::SampleBank::instance().slot(v.slot);
}
bool Engine::start_bounce_preview() {
    if (transport_playing || recording_active_or_pending() || !bounce_sample(bounce_.view)) return false;
    stop_sample_preview();
    stop_play_preview();
    bounce_preview_take_ = bounce_.view;
    bounce_preview_frame_ = 0;
    bounce_preview_active_ = true;
    return true;
}
void Engine::stop_bounce_preview() {
    bounce_preview_active_ = false;
    bounce_preview_frame_ = 0;
    bounce_preview_take_ = {};
}
bool Engine::bounce_preview_active() const {
    return bounce_preview_active_ && !transport_playing && !recording_active_or_pending() &&
        bounce_preview_frame_ < bounce_preview_take_.frames && bounce_sample(bounce_preview_take_);
}
uint32_t Engine::bounce_preview_position() const {
    return !transport_playing && bounce_sample(bounce_preview_take_) ? bounce_preview_frame_ : 0;
}
void Engine::process_bounce_preview(q15* stereo, int frames) {
    // Only called for a render that began with a valid audition. Zero the rest
    // of that call after natural finish; never leak project tails into its end.
    const auto* sample = transport_playing || recording_active_or_pending() ? nullptr : bounce_sample(bounce_preview_take_);
    if (!sample) {
        stop_bounce_preview();
        std::memset(stereo, 0, (size_t)frames * 2 * sizeof(q15));
        return;
    }
    const uint32_t count = std::min<uint32_t>((uint32_t)frames,
        bounce_preview_take_.frames - bounce_preview_frame_);
    const uint8_t channels = bounce_channels(bounce_preview_take_);
    const q15* source = sample->data.data() + (size_t)bounce_preview_frame_ * channels;
    if (channels == 2) std::memcpy(stereo, source, (size_t)count * 2 * sizeof(q15));
    else for (uint32_t i = 0; i < count; ++i) stereo[i * 2] = stereo[i * 2 + 1] = source[i];
    std::memset(stereo + count * 2, 0, ((size_t)frames - count) * 2 * sizeof(q15));
    bounce_preview_frame_ += count;
    if (bounce_preview_frame_ == bounce_preview_take_.frames) bounce_preview_active_ = false;
}
bool Engine::read_bounce_frames(const BounceSnapshot& token, uint32_t first,
                                uint32_t count, q15* interleaved) const {
    const auto* sample = bounce_sample(token);
    if (!sample || !interleaved || first > token.frames || count > token.frames - first) return false;
    const uint8_t channels = bounce_channels(token);
    std::memcpy(interleaved, sample->data.data() + (size_t)first * channels, (size_t)count * channels * sizeof(q15));
    return true;
}
bool Engine::mark_bounce_saved(const BounceSnapshot& token, const char* path) {
    if (!path || !path[0] || std::strlen(path) >= sizeof(trackr::synth::SampleProvenance::path) ||
        !bounce_sample(token)) return false;
    trackr::synth::SampleBank::instance().set_provenance(token.slot,
        trackr::synth::SampleOrigin::Recording, path);
    bounce_.view.saved = true;
    return true;
}
BounceSendPlan::~BounceSendPlan() {
    delete prepared;
    for (Unit* unit : retired) delete unit;
}
bool Engine::bounce_chain_safe(int chain) const {
    if (chain < 0 || chain >= NUM_CHAINS || chain_record_pending(chain)) return false;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        const Unit* unit = chains[chain].units[slot];
        if (recorder_record_pending(chain, slot)) return false;
        if (unit && unit->kind == UnitKind::Recorder) {
            const auto* rec = static_cast<const RecUnit*>(unit);
            if (rec->writer_open() || rec->take_needs_save()) return false;
        }
    }
    return true;
}
BounceResult Engine::plan_bounce_send(int chain, BounceSendPlan& out) const {
    if (out.prepared || out.owner) return BounceResult::Busy;
    if (transport_playing) return BounceResult::Busy;
    const auto token = bounce_snapshot();
    if (token.status != BounceStatus::Complete || !bounce_sample(token)) return BounceResult::Invalid;
    if (!token.saved) return BounceResult::NotSaved;
    if (!bounce_clock_safe() || token.bpm_q16 != effective_bpm_q16() ||
        token.beats_per_bar != beats_per_bar) return BounceResult::TempoModulated;
    if (!bounce_chain_safe(chain)) return BounceResult::UnsafeChain;
    const uint32_t steps = (uint32_t)token.bars * token.beats_per_bar * 4;
    if (!steps || (steps > SEQ_STEPS && (steps > SEQ_STEPS * 2 || (steps & 1))))
        return BounceResult::Invalid;
    out.owner = this;
    out.take = token;
    out.chain = (int8_t)chain;
    out.alternate = steps > SEQ_STEPS;
    out.length = (uint8_t)(out.alternate ? steps / 2 : steps);
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        Unit* unit = chains[chain].units[slot];
        out.expected[slot] = unit;
        out.rec_generation[slot] = unit && unit->kind == UnitKind::Recorder
            ? static_cast<const RecUnit*>(unit)->take_generation() : 0;
    }
    return BounceResult::Ok;
}
BounceResult Engine::stage_bounce_send(BounceSendPlan& plan) {
    if (!plan.owner || plan.prepared || plan.chain < 0 || !plan.length ||
        !bounce_source_valid(plan.take.source)) return BounceResult::Invalid;
    plan.prepared = new(std::nothrow) PlayUnit();
    if (!plan.prepared) return BounceResult::MemoryBudget;
    auto* play = plan.prepared;
    play->sample_slot = plan.take.slot;
    play->params[PlayUnit::P_SPEED].base = 8192; // exact Q16 +1x, not midpoint 16384
    play->params[PlayUnit::P_START].base = 0;
    play->params[PlayUnit::P_LEN].base = trackr::fx::Q15_ONE;
    play->params[PlayUnit::P_LOOP].base = 0;
    play->params[PlayUnit::P_GAIN].base = trackr::fx::Q15_ONE;
    play->params[PlayUnit::P_SLICE].base = 0;
    play->params[PlayUnit::P_MODE].base = 0; // one-shot, no inherited short step gate
    for (int p = 0; p < play->num_params; ++p) play->params[p].snap();
    return BounceResult::Ok;
}
BounceResult Engine::commit_bounce_send(BounceSendPlan& plan) {
    if (plan.owner != this || !plan.prepared || transport_playing || !bounce_sample(plan.take))
        return BounceResult::Stale;
    const auto token = bounce_snapshot();
    if (!token.saved || token.status != BounceStatus::Complete) return BounceResult::NotSaved;
    if (!bounce_clock_safe() || token.bpm_q16 != effective_bpm_q16() ||
        token.beats_per_bar != beats_per_bar) return BounceResult::TempoModulated;
    const int chain = plan.chain;
    if (!bounce_chain_safe(chain)) return BounceResult::UnsafeChain;
    const uint32_t steps = (uint32_t)token.bars * token.beats_per_bar * 4;
    if (plan.length < 1 || plan.length > SEQ_STEPS ||
        (uint32_t)plan.length * (plan.alternate ? 2u : 1u) != steps ||
        plan.prepared->sample_slot != token.slot) return BounceResult::Invalid;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        Unit* unit = chains[chain].units[slot];
        if (plan.retired[slot] || unit != plan.expected[slot]) return BounceResult::Stale;
        if (unit && unit->kind == UnitKind::Recorder &&
            static_cast<const RecUnit*>(unit)->take_generation() != plan.rec_generation[slot])
            return BounceResult::Stale;
    }
    // Everything below is bounded, non-allocating, and limited to this chain's
    // units/references. The controller destroys plan.retired after leaving guard.
    if (temp_chain_ == chain) cancel_param_temp();
    if (sequence_audition_chain_ == chain) cancel_sequence_audition();
    release_sequence_locks(chain);
    invalidate_structural_undo();
    if(relic_gesture_.chain==chain)cancel_relic_gesture();
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        plan.retired[slot] = chains[chain].units[slot];
        chains[chain].units[slot] = slot ? nullptr : plan.prepared;
    }
    plan.prepared = nullptr;
    auto& c = chains[chain];
    c.volume = trackr::fx::Q15_ONE; c.pan = 0; c.mute = false;
    volume_ramp_[chain] = {};
    chain_peak[chain] = 0;
    scenes[chain][0] = {}; scenes[chain][1] = {}; scene_morph[chain] = 0;
    auto install_lane = [&](Pattern& p) {
        p.length[chain] = plan.length;
        p.direction[chain] = SequenceDirection::Forward;
        for (int step = 0; step < SEQ_STEPS; ++step) {
            p.steps[chain][step] = {};
            p.microtiming[chain][step] = 0;
        }
        p.steps[chain][0].flags = SeqStep::Trig;
        if (plan.alternate) p.steps[chain][0].set_condition(SeqStep::Condition::FirstOfTwo);
    };
    install_lane(pattern);
    for (auto& bank : pattern_banks) install_lane(bank);
    for (auto& pad : macro_pads) {
        int kept = 0;
        const int count = std::min<int>(pad.route_count, MacroPad::MAX_ROUTES);
        for (int route = 0; route < count; ++route)
            if (pad.routes[route].chain != chain) pad.routes[kept++] = pad.routes[route];
        for (int route = kept; route < count; ++route) pad.routes[route] = {};
        pad.route_count = (uint8_t)kept;
    }
    for (auto& event : pending_sequence_events_[chain]) event = {};
    active_sequence_pass_[chain] = false;
    active_sequence_step_[chain] = {};
    pending_triggers &= (uint8_t)~(1u << chain);
    if (live_retrigger_active_ && live_retrigger_chain_ == chain) live_retrigger_active_ = false;
    return BounceResult::Ok;
}

void Engine::capture_bounce(const q15* stereo, int frames) {
    auto& v = bounce_.view;
    if (!v.active()) return;
    if (!bounce_storage_valid()) { stop_bounce(BounceReason::SampleChanged); return; }
    if (beats_per_bar != v.beats_per_bar) { stop_bounce(BounceReason::MeterChanged); return; }
    if (!bounce_clock_safe() || effective_bpm_q16() != v.bpm_q16 ||
        clock_runtime_ != bounce_.phase_clock ||
        (clock_runtime_ && clock_increment_ != bounce_.clock_increment)) {
        stop_bounce(BounceReason::TempoChanged); return;
    }
    if (!transport_playing) return;
    const uint64_t block_end = transport_frame + (uint32_t)frames;
    if (block_end <= v.start_frame || transport_frame >= v.end_frame) return;
    const uint64_t begin = std::max(transport_frame, v.start_frame);
    const uint64_t end = std::min(block_end, v.end_frame);
    const uint32_t count = (uint32_t)(end - begin);
    auto& sample = trackr::synth::SampleBank::instance().slot(v.slot);
    const uint8_t channels = bounce_channels(v);
    q15* destination = sample.data.data() + (size_t)v.frames * channels;
    const size_t bytes = (size_t)count * channels * sizeof(q15);
    // Each chain's scratch remains post-module until this subblock ends: the
    // mixer only reads it, while PLAY/browser previews and master FX use separate
    // buffers. Reuse that exact pass, never rerun DSP or split control fragments.
    const int chain = (int)v.source - (int)BounceSource::Chain1;
    if (v.source != BounceSource::Master && (chains[chain].rendered_channels != channels ||
        chain_audio_channels(chain) != channels)) { stop_bounce(BounceReason::ChannelsChanged); return; }
    if (v.source != BounceSource::Master && chains[chain].mute) {
        // Explicit policy: mute records silence, not the internally running DSP.
        std::memset(destination, 0, bytes);
    } else {
        const q15* source = v.source == BounceSource::Master ? stereo :
            channels == 2 ? chains[chain].stereo : chains[chain].scratch;
        std::memcpy(destination, source + (size_t)(begin - transport_frame) * channels, bytes);
    }
    v.frames += count;
    sample.logical_frames = v.frames;
    v.status = v.frames == v.target_frames ? BounceStatus::Complete : BounceStatus::Recording;
}
