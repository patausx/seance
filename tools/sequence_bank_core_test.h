// Included by sequencer_test.cpp after Fixture. Real engine/storage only;
// no alternate bank-selection model or device/backend dependency.
void test_sequence_bank_core() {
    Fixture f;
    Engine& e = *f.engine;
    auto reference = std::make_unique<Engine>();
    reference->chains[0].units[0] = new FmUnit;
    reference->chains[1].units[0] = new OscUnit;
    reference->chains[1].units[1] = new VcaUnit;
    reference->chains[2].units[0] = new PlayUnit;
    reference->chains[3].units[0] = new OscUnit;
    const Engine& readonly = e;
    CHECK(&e.sequence_pattern() == &e.pattern);
    CHECK(&readonly.sequence_pattern() == &e.pattern);
    for (int invalid : {-100, -1, NUM_PATTERNS, 100}) {
        CHECK(&e.sequence_pattern(invalid) == &e.pattern);
        CHECK(&readonly.sequence_pattern(invalid) == &e.pattern);
    }

    // Distinct data in all eight banks, including every inactive tail step.
    for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
        Pattern& p = e.sequence_pattern(bank);
        CHECK(&p == (bank == e.active_pattern ? &e.pattern : &e.pattern_banks[bank]));
        for (int chain = 0; chain < NUM_CHAINS; ++chain) {
            p.length[chain] = (uint8_t)(8 + bank * 7 + chain);
            p.direction[chain] = (SequenceDirection)((bank + chain) % 3);
            for (int step = 0; step < SEQ_STEPS; ++step) {
                SeqStep& s = p.steps[chain][step];
                s.flags = (step + bank) % 3 ? SeqStep::Trig : 0;
                s.set_retrig_count((uint8_t)(step % 4 + 1));
                s.set_condition((SeqStep::Condition)(step % 8));
                p.microtiming[chain][step] = (int8_t)((step + bank) % 47 - 23);
                if (step % 5 == 0) continue;
                s.lock_count = 2;
                s.locks[0] = {(uint8_t)g_ui_snapshot.sequence_pitch_slot[chain],
                    (uint8_t)g_ui_snapshot.sequence_pitch_param[chain],
                    (q15)(1000 + bank * 2000 + step * 17)};
                s.locks[1] = {(uint8_t)g_ui_snapshot.sequence_value_slot[chain],
                    (uint8_t)g_ui_snapshot.sequence_value_param[chain],
                    (q15)(7000 + bank * 1700 + step * 11)};
            }
        }
        reference->sequence_pattern(bank) = p;
    }
    e.set_sequencer_enabled(true); reference->set_sequencer_enabled(true);
    e.transport_play(); reference->transport_play();
    std::array<q15, 514> audio{}, baseline{};
    e.render(audio.data(), 73); reference->render(baseline.data(), 73);
    CHECK(audio == baseline);
    e.request_pattern(7); reference->request_pattern(7);
    CHECK(e.pending_pattern == 7 && e.active_pattern == 0);
    const uint64_t active_hash = pattern_hash(e.pattern), tick = e.sequence_tick;
    const uint64_t transport = e.transport_frame, beat = e.beat_count;
    const uint32_t frame_in_beat = e.frame_in_beat;
    std::array<uint64_t, NUM_PATTERNS> original{};
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        original[bank] = pattern_hash(e.sequence_pattern(bank));

    EngineUiSnapshot published{};
    e.capture_ui_snapshot(published, 0, 0, 1);
    // Exact byte whitelist: helper cannot touch telemetry, focus, scope, scene,
    // transport, actual/pending bank identity, or otherwise republish the engine.
    std::array<bool, sizeof(EngineUiSnapshot)> sequence_bytes{};
    const auto* origin = reinterpret_cast<const unsigned char*>(&published);
    auto allow = [&](const auto& member) {
        const auto* start = reinterpret_cast<const unsigned char*>(&member);
        for (size_t i = 0; i < sizeof member; ++i) sequence_bytes[start - origin + i] = true;
    };
#define BANK_SEQUENCE_FIELDS(X) \
    X(sequence_step); X(sequence_length); X(sequence_direction); X(step_trig); \
    X(sequence_meta); X(sequence_pitch); X(sequence_pitch_lock_mask); \
    X(sequence_pitch_slot); X(sequence_pitch_param); X(sequence_pitch_kind); \
    X(sequence_lifecycle); X(sequence_duration); X(sequence_duration_slot); \
    X(sequence_duration_param); X(sequence_value); X(sequence_value_lock_mask); \
    X(sequence_value_slot); X(sequence_value_param); X(sequence_source_kind)
#define BANK_ALLOW(field) allow(published.field)
    BANK_SEQUENCE_FIELDS(BANK_ALLOW);
#undef BANK_ALLOW
    for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
        EngineUiSnapshot selected = published;
        e.capture_sequence_snapshot(selected, bank);
        const auto* actual = reinterpret_cast<const unsigned char*>(&selected);
        for (size_t byte = 0; byte < sizeof selected; ++byte)
            if (!sequence_bytes[byte]) CHECK(actual[byte] == origin[byte]);
        CHECK(selected.active_pattern == 0 && selected.pending_pattern == 7);
        for (int chain = 0; chain < NUM_CHAINS; ++chain) {
            const Pattern& p = e.sequence_pattern(bank);
            CHECK(selected.sequence_length[chain] == p.length[chain]);
            CHECK(selected.sequence_direction[chain] == (uint8_t)p.direction[chain]);
            CHECK(selected.sequence_step[chain] == (bank ? 255 : published.sequence_step[chain]));
            for (int step = 0; step < SEQ_STEPS; ++step) {
                const SeqStep& s = p.steps[chain][step];
                CHECK(selected.sequence_meta[chain][step] == (uint16_t)((s.flags & 63u) |
                    (s.lock_count << 6) | ((p.microtiming[chain][step] + 23) << 9)));
                if (s.lock_count) {
                    CHECK(selected.sequence_pitch[chain][step] == s.locks[0].value);
                    CHECK(selected.sequence_value[chain][step] == s.locks[1].value);
                }
                CHECK(bool(selected.sequence_pitch_lock_mask[chain] & (uint64_t(1) << step)) == bool(s.lock_count));
                CHECK(bool(selected.sequence_value_lock_mask[chain] & (uint64_t(1) << step)) == bool(s.lock_count));
            }
        }
    }
    EngineUiSnapshot default_capture = published;
    e.capture_sequence_snapshot(default_capture);
    CHECK(!std::memcmp(&default_capture, &published, sizeof published));
    e.capture_sequence_snapshot(default_capture, NUM_PATTERNS);
    CHECK(!std::memcmp(&default_capture, &published, sizeof published));
#undef BANK_SEQUENCE_FIELDS

    // Inactive geometry, lock erasure, step clearing and direct stored edits.
    e.set_sequence_length(1, 1, 3);
    e.set_sequence_direction(1, SequenceDirection::PingPong, 3);
    CHECK(e.sequence_pattern(3).steps[1][63].lock_count == 2); // shortening preserves tail
    CHECK(e.remove_step_lock(1, 63, g_ui_snapshot.sequence_pitch_slot[1],
                             g_ui_snapshot.sequence_pitch_param[1], 3));
    CHECK(e.sequence_pattern(3).steps[1][63].lock_count == 1);
    e.clear_step(1, 62, 3);
    CHECK(e.sequence_pattern(3).steps[1][62].flags == 0);
    CHECK(e.sequence_pattern(3).steps[1][62].lock_count == 0);
    CHECK(e.sequence_pattern(3).microtiming[1][62] == 0);
    e.sequence_pattern(3).steps[1][61].set_condition(SeqStep::Condition::Fill);
    e.set_sequence_length(1, 1000, 3); CHECK(e.sequence_pattern(3).length[1] == 64);
    e.set_sequence_length(1, -1, 3); CHECK(e.sequence_pattern(3).length[1] == 1);
    CHECK(e.sequence_pattern(3).steps[1][63].lock_count == 1);
    CHECK(pattern_hash(e.pattern) == active_hash && e.sequence_tick == tick);
    CHECK(e.transport_frame == transport && e.beat_count == beat && e.frame_in_beat == frame_in_beat);
    CHECK(e.transport_playing && e.sequencer_enabled && e.pending_pattern == 7 && e.active_pattern == 0);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        if (bank != 3) CHECK(pattern_hash(e.sequence_pattern(bank)) == original[bank]);
    // Comparing actual PCM and ticks to untouched scheduler proves edits did not
    // flush private lookahead, not merely that public counters stayed unchanged.
    for (int block = 0; block < 32; ++block) {
        e.render(audio.data(), 257); reference->render(baseline.data(), 257);
        CHECK(audio == baseline);
        CHECK(e.sequence_tick == reference->sequence_tick);
        CHECK(e.transport_frame == reference->transport_frame);
    }

    // Actual immediate launch resolves the alias without stale active-bank data.
    std::array<uint64_t, NUM_PATTERNS> retained{};
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        retained[bank] = pattern_hash(e.sequence_pattern(bank));
    e.request_pattern(3, false);
    CHECK(e.active_pattern == 3 && e.pending_pattern == 255);
    CHECK(&e.sequence_pattern(3) == &e.pattern && &readonly.sequence_pattern(3) == &e.pattern);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        CHECK(pattern_hash(e.sequence_pattern(bank)) == retained[bank]);
    e.set_sequence_length(0, 27, 3);
    CHECK(e.pattern.length[0] == 27 && e.pattern_banks[3].length[0] != 27);
    EngineUiSnapshot launched{}; e.capture_ui_snapshot(launched);
    CHECK(launched.active_pattern == 3 && launched.sequence_length[0] == 27);
    e.capture_sequence_snapshot(launched, 0);
    CHECK(launched.active_pattern == 3 && launched.sequence_step[0] == 255);

    // Existing SNC7 route preserves every bank/tail; no new serialization field.
    struct TempBankPatch {
        char path[64] = "/tmp/seance-bank-core-XXXXXX";
        TempBankPatch() { const int fd = mkstemp(path); CHECK(fd >= 0); close(fd); }
        ~TempBankPatch() { std::remove(path); std::remove((std::string(path) + ".tmp").c_str()); }
    } saved;
    CHECK(patchio::save(e, saved.path));
    auto loaded = std::make_unique<Engine>();
    CHECK(patchio::load(*loaded, saved.path));
    CHECK(loaded->active_pattern == e.active_pattern);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        CHECK(pattern_hash(loaded->sequence_pattern(bank)) == pattern_hash(e.sequence_pattern(bank)));

    // Same audition lifetime/refusal/layer contract, different selected lock data.
    e.transport_stop();
    Param& pitch = e.chains[0].units[0]->params[FmUnit::P_PITCH];
    const q15 base = pitch.base;
    const q15 playing = e.sequence_pattern().steps[0][1].locks[0].value;
    const q15 editing = e.sequence_pattern(6).steps[0][1].locks[0].value;
    CHECK(playing != editing);
    const auto audition_hash = pattern_hash(e.pattern);
    const auto audition_tick = e.sequence_tick;
    CHECK(e.audition_sequence_step(0, 1));
    CHECK(pitch.audition_override && pitch.audition == playing && pitch.effective == playing);
    CHECK(e.audition_sequence_step(0, 1, 6));
    CHECK(pitch.audition_override && pitch.audition == editing && pitch.effective == editing);
    CHECK(pitch.base == base && pattern_hash(e.pattern) == audition_hash && e.sequence_tick == audition_tick);
    e.sequence_pattern(6).steps[0][1].locks[0].value = 1234;
    CHECK(pitch.audition == editing); // accepted audition latches, not a live bank pointer
    pitch.performance = 5555; pitch.performance_override = true;
    CHECK(e.audition_sequence_step(0, 1, 6) && pitch.effective == 5555);
    pitch.performance_override = false;
    e.chains[0].mute = true;
    CHECK(!e.audition_sequence_step(0, 1, 6) && !pitch.audition_override);
    e.chains[0].mute = false; e.pending_rec_start = 1;
    CHECK(!e.audition_sequence_step(0, 1, 6));
    e.pending_rec_start = 0;
    CHECK(e.audition_sequence_step(0, 1, 6) && pitch.audition == 1234);
    e.cancel_sequence_audition(); CHECK(!pitch.audition_override && pitch.base == base);
}
