// Real core in the existing sequencer harness. No controller model/private access.
namespace transition_core_cases {
struct Hit {
    uint64_t frame;
    q15 first, second;
    bool locked_first, locked_second;
    bool operator==(const Hit& b) const {
        return frame == b.frame && first == b.first && second == b.second &&
               locked_first == b.locked_first && locked_second == b.locked_second;
    }
};
struct Probe : Unit {
    Engine& engine;
    std::vector<Hit> hits;
    explicit Probe(Engine& e) : engine(e) {
        kind = UnitKind::Osc; add_param("ONE", 500); add_param("TWO", 700);
        hits.reserve(1024);
    }
    void trigger() override {
        hits.push_back({engine.transport_frame, params[0].effective, params[1].effective,
                       params[0].step_lock_override, params[1].step_lock_override});
    }
    void process(q15* output, int frames) override {
        for (int i = 0; i < frames; ++i)
            output[i] = (q15)(params[0].effective / 32 + params[1].effective / 64);
    }
};
std::unique_ptr<Engine> engine(bool phase) {
    auto e = std::make_unique<Engine>();
    for (int c = 0; c < NUM_CHAINS; ++c) e->chains[c].units[0] = new Probe(*e);
    e->set_bpm(120); e->set_swing(63);
    if (phase) {
        // Nonzero route opts into the actual q32 scheduler. Constant source
        // gives repeatable noninteger applied tempo without unrelated triggers.
        e->sigils[0].drawn = true; e->sigils[0].depth = 32767;
        for (auto& v : e->sigils[0].table) v = 12000;
        CHECK(e->set_clock_mod(ClockTarget::Tempo, ModSource::SigilA, 7));
    }
    e->set_sequencer_enabled(true);
    return e;
}
void populate(Engine& e) {
    for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
        Pattern& p = e.sequence_pattern(bank);
        for (int c = 0; c < NUM_CHAINS; ++c) {
            p.length[c] = (uint8_t)(1 + bank + c);
            p.direction[c] = (SequenceDirection)((bank + c) % 3);
            for (int s = 0; s < SEQ_STEPS; ++s) {
                SeqStep& step = p.steps[c][s];
                step.flags = s % 5 == 4 ? 0 : SeqStep::Trig;
                step.set_condition((SeqStep::Condition)((s + c + bank) % 8));
                step.set_retrig_count((uint8_t)(1 + (s + bank) % 4));
                step.lock_count = s % 7 == 6 ? 0 : 2;
                const q15 id = (q15)(1000 + bank * 3000 + c * 200 + s * 2);
                step.locks[0] = {0, 0, id}; step.locks[1] = {0, 1, (q15)(30000 - id)};
                p.microtiming[c][s] = (int8_t)((s + bank) % 3 == 0 ? -23 :
                                             (s + bank) % 3 == 1 ? 23 : 0);
            }
        }
    }
}
void compare_hits(const Engine& a, const Engine& b) {
    for (int c = 0; c < NUM_CHAINS; ++c) {
        const auto& x = static_cast<const Probe*>(a.chains[c].units[0])->hits;
        const auto& y = static_cast<const Probe*>(b.chains[c].units[0])->hits;
        CHECK(x == y);
    }
}
void compare_audio(Engine& a, Engine& b, int frames, bool partition = false) {
    std::array<q15, 514> x{}, y{};
    while (frames > 0) {
        const int n = std::min(frames, 257);
        a.render(x.data(), n);
        if (!partition) b.render(y.data(), n);
        else for (int offset = 0; offset < n;) {
            const int part = std::min(n - offset, 1 + offset % 29);
            b.render(y.data() + offset * 2, part); offset += part;
        }
        CHECK(std::equal(x.begin(), x.begin() + n * 2, y.begin()));
        CHECK(a.transport_frame == b.transport_frame && a.sequence_tick == b.sequence_tick);
        frames -= n;
    }
    compare_hits(a, b);
}
// Independent materialized-score oracle: choose once, then put the complete
// native tuple into a plain 64-step forward pattern in a NON-transition engine.
int choose(int chain, uint64_t tick, int position) {
    uint32_t x = uint32_t(tick) ^ uint32_t(tick >> 32) ^ (0x9e3779b9u * uint32_t(chain + 1));
    x = (x ^ (x >> 16)) * 0x7feb352du;
    x = (x ^ (x >> 15)) * 0x846ca68bu;
    x ^= x >> 16;
    return (x % 32767u) < uint32_t(position);
}
void materialize(const Engine& from, Engine& to, int source, int destination, int position) {
    for (int c = 0; c < NUM_CHAINS; ++c) {
        to.pattern.length[c] = 64; to.pattern.direction[c] = SequenceDirection::Forward;
        for (int tick = 0; tick < 64; ++tick) {
            const Pattern& p = from.sequence_pattern(choose(c, tick, position) ? destination : source);
            const int length = p.length[c];
            const bool ping = p.direction[c] == SequenceDirection::PingPong && length > 1;
            const int period = ping ? (length - 1) * 2 : length;
            int step = tick % period;
            if (ping && step >= length) step = period - step;
            if (p.direction[c] == SequenceDirection::Reverse) step = length - 1 - step;
            SeqStep selected = p.steps[c][step];
            const auto condition = selected.condition();
            if (condition == SeqStep::Condition::FirstOfTwo || condition == SeqStep::Condition::SecondOfTwo) {
                const bool second = (tick / period) % 2;
                const bool pass = second == (condition == SeqStep::Condition::SecondOfTwo);
                selected.set_condition(SeqStep::Condition::Always);
                if (!pass) { selected.flags &= ~SeqStep::Trig; selected.lock_count = 0; }
            }
            to.pattern.steps[c][tick] = selected;
            to.pattern.microtiming[c][tick] = p.microtiming[c][step];
        }
    }
}
void endpoints_and_midpoint() {
    for (bool phase : {false, true}) {
        // All eight native banks, same-bank identity and exact both endpoints;
        // probability conditions catch any extra RNG consumption too.
        for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
            auto a = engine(phase), b = engine(phase); populate(*a); populate(*b);
            b->request_pattern(bank, false);
            if (bank % 3 == 0) CHECK(a->set_pattern_transition(bank, bank, 16384));
            else if (bank % 3 == 1) CHECK(a->set_pattern_transition(bank, (bank + 1) % 8, 0));
            else CHECK(a->set_pattern_transition((bank + 1) % 8, bank, 32767));
            a->transport_play(); b->transport_play();
            compare_audio(*a, *b, 85000);
        }
        for (int position : {1, 8192, 16384, 24576, 32766}) {
            auto a = engine(phase), b = engine(phase); populate(*a);
            materialize(*a, *b, 1, 6, position);
            CHECK(a->set_pattern_transition(1, 6, (q15)position));
            a->transport_play(); b->transport_play();
            // Fill is evaluated live at event time, not rerolled on rail movement.
            compare_audio(*a, *b, 42000, true);
            a->set_sequencer_fill(true); b->set_sequencer_fill(true);
            compare_audio(*a, *b, 42000, true);
            a->set_sequencer_fill(false); b->set_sequencer_fill(false);
            compare_audio(*a, *b, 42000, true);
            CHECK(a->sequence_tick < 60); // oracle has not wrapped
            EngineUiSnapshot snapshot{}; a->capture_ui_snapshot(snapshot);
            CHECK(snapshot.pattern_transition.active && snapshot.pattern_transition.position == position);
            CHECK(snapshot.active_pattern == 0);
            for (int bank = 0; bank < 8; ++bank) {
                a->capture_sequence_snapshot(snapshot, bank);
                for (auto head : snapshot.sequence_step) CHECK(head == 255);
            }
        }
    }
}
void queue_and_lifecycle() {
    for (bool phase : {false, true}) {
        auto a = engine(phase), b = engine(phase); populate(*a); populate(*b);
        a->set_swing(50); b->set_swing(50);
        for (Engine* e : {a.get(), b.get()}) {
            for (int bank = 0; bank < NUM_PATTERNS; ++bank)
                for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < SEQ_STEPS; ++s) {
                    auto& p = e->sequence_pattern(bank); p.steps[c][s].flags = SeqStep::Trig;
                    p.microtiming[c][s] = 0;
                }
            CHECK(e->set_pattern_transition(0, 1, 0)); e->transport_play();
        }
        compare_audio(*a, *b, 1); // actual lazy lookahead now owns two tuples
        const uint64_t tick = a->sequence_tick, frame = a->transport_frame;
        CHECK(!a->set_pattern_transition(-1, 1, 0));
        CHECK(!a->set_pattern_transition(0, 8, 0));
        CHECK(!a->set_pattern_transition(3, 1, 0));
        CHECK(!a->set_pattern_transition(0, 3, 123)); // cannot replace then contribute
        CHECK(a->set_pattern_transition(0, 3, 0));
        for (int i = 0; i < 300; ++i) CHECK(a->set_pattern_transition(0, 3, (q15)(i * 97 % 32768)));
        CHECK(a->set_pattern_transition(0, 3, 32767));
        CHECK(!a->set_pattern_transition(4, 3, 32000));
        CHECK(a->set_pattern_transition(4, 3, 32767));
        CHECK(a->transport_frame == frame && a->sequence_tick == tick && a->active_pattern == 0);
        CHECK(a->pending_pattern == 255);
        // No flush/reroll/double trigger: both originally queued source events
        // finish byte-exact, even after hundreds of rail updates/replacements.
        compare_audio(*a, *b, 9000);
        CHECK(a->pattern_transition().active);
        a->transport_stop(); CHECK(a->pattern_transition().position == 32767);
        a->transport_play(); a->request_pattern(a->active_pattern);
        CHECK(!a->pattern_transition().active && a->pending_pattern == 255);
        CHECK(a->set_pattern_transition(0, 1, 16000));
        a->request_pattern(7); CHECK(!a->pattern_transition().active && a->pending_pattern == 7);
        CHECK(!a->set_pattern_transition(0, 1, 16000));
        CHECK(a->pending_pattern == 7 && !a->pattern_transition().active); // launch owns playback
        a->transport_reset(); CHECK(!a->pattern_transition().active);
        CHECK(a->set_pattern_transition(0, 1, 16000));
        CHECK(a->set_pattern_chain_step(0, 3, 1)); a->set_pattern_chain_length(1);
        CHECK(a->start_pattern_chain()); CHECK(!a->pattern_transition().active);
        CHECK(!a->set_pattern_transition(3, 2, 16000));
        q15 one[64] = {}; a->render(one, 32); a->stop_pattern_chain();
        CHECK(a->start_pattern_chain());
        EngineUiSnapshot pending_chain{}; a->capture_ui_snapshot(pending_chain);
        CHECK(pending_chain.pattern_chain_start_pending);
        CHECK(!a->set_pattern_transition(3, 2, 16000));
        a->stop_pattern_chain();
        CHECK(a->set_pattern_transition(3, 2, 16000));
        CHECK(b->set_pattern_transition(0, 1, 0));
        a->swap_patch_state(*b);
        CHECK(!a->pattern_transition().active && !b->pattern_transition().active);
        // Clearing is also runtime-only; no launch/tick write.
        CHECK(a->set_pattern_transition(0, 1, -1)); CHECK(a->pattern_transition().position == 0);
        const auto clear_tick = a->sequence_tick; const auto clear_frame = a->transport_frame;
        a->clear_pattern_transition(); CHECK(!a->pattern_transition().active);
        CHECK(a->sequence_tick == clear_tick && a->transport_frame == clear_frame);
    }
}
void finish_and_scene() {
    for (bool phase : {false, true}) {
        for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
            auto a = engine(phase), b = engine(phase); populate(*a); populate(*b);
            CHECK(!a->finish_pattern_transition());
            const int source = bank % 3 == 1 ? (bank + 7) % 8 : bank;
            const int destination = bank % 3 == 1 ? bank : (bank + 1) % 8;
            for (Engine* e : {a.get(), b.get()}) {
                CHECK(e->set_pattern_transition(source, destination, 16000));
                e->transport_play();
            }
            compare_audio(*a, *b, 12000);
            std::array<uint64_t, NUM_PATTERNS> hashes{};
            for (int i = 0; i < NUM_PATTERNS; ++i) hashes[i] = pattern_hash(a->sequence_pattern(i));
            const auto tick = a->sequence_tick, frame = a->transport_frame, beat = a->beat_count;
            const auto inbeat = a->frame_in_beat;
            CHECK(!a->finish_pattern_transition()); // middle is a complete no-op
            CHECK(a->pattern_transition().active && a->pattern_transition().position == 16000);
            CHECK(a->active_pattern == 0 && a->sequence_tick == tick && a->transport_frame == frame);
            int kept = bank;
            for (Engine* e : {a.get(), b.get()}) {
                if (bank % 3 == 0) CHECK(e->set_pattern_transition(source, destination, 0));
                else if (bank % 3 == 1) CHECK(e->set_pattern_transition(source, destination, 32767));
                else {
                    e->clear_pattern_transition(); CHECK(e->set_pattern_transition(bank, bank, 12345));
                }
            }
            CHECK(a->finish_pattern_transition());
            CHECK(!a->pattern_transition().active && a->active_pattern == kept && a->pending_pattern == 255);
            CHECK(a->sequence_tick == tick && a->transport_frame == frame && a->beat_count == beat);
            CHECK(a->frame_in_beat == inbeat);
            CHECK(&a->sequence_pattern(kept) == &a->pattern);
            for (int i = 0; i < NUM_PATTERNS; ++i) CHECK(pattern_hash(a->sequence_pattern(i)) == hashes[i]);
            // Compare against still-transitioning identical endpoint, including
            // frozen previous mid-blend lookahead and probabilistic conditions.
            // Exact PCM/events prove keep did not reset clock, phase, or RNG.
            compare_audio(*a, *b, 42000, true);
        }
        auto a = engine(phase), b = engine(phase);
        for (Engine* e : {a.get(), b.get()}) {
            CHECK(!e->apply_transition_scene_morph(16000)); // absent pair, no mutation
            for (int scene = 0; scene < 2; ++scene) {
                for (int c = 0; c < NUM_CHAINS; ++c) {
                    e->chains[c].volume = (q15)(scene ? 28000 : 5000);
                    e->chains[c].pan = (q15)(scene ? 12000 : -12000);
                    for (int p = 0; p < 2; ++p)
                        e->chains[c].units[0]->params[p].base = (q15)(1000 + scene * 12000 + c * 200 + p * 500);
                }
                e->capture_global_scene(scene);
            }
            e->set_swing(50);
            for (int c = 0; c < NUM_CHAINS; ++c) {
                e->set_chain_volume_smooth(c, (q15)(14000 + c * 500));
                e->pattern.length[c] = 2;
                auto& locked = e->pattern.steps[c][0]; locked.flags = SeqStep::Trig;
                locked.lock_count = 2; locked.locks[0] = {0, 0, 6000}; locked.locks[1] = {0, 1, 7000};
                e->pattern.steps[c][1].flags = SeqStep::Trig; // releases both locks naturally
                auto& p = e->chains[c].units[0]->params[1];
                p.src = ModSource::SigilA; p.mod_depth = 1000;
                p.temp = 9000; p.temp_override = true;
                p.performance = 8000; p.performance_override = true;
            }
            e->transport_play();
        }
        compare_audio(*a, *b, 4200);
        const auto scenes_before = std::make_unique<std::array<PersistentScene, NUM_CHAINS * 2>>();
        std::memcpy(scenes_before.get(), a->scenes, sizeof a->scenes);
        for (int amount : {-1, 0, 9000, 16384, 32767}) {
            Param old[NUM_CHAINS][2];
            for (int c = 0; c < NUM_CHAINS; ++c) for (int p = 0; p < 2; ++p) {
                old[c][p] = a->chains[c].units[0]->params[p]; CHECK(old[c][p].step_lock_override);
            }
            const q15 position = (q15)std::max(0, amount);
            CHECK(a->apply_transition_scene_morph((q15)amount));
            auto blend = [position](q15 x, q15 y) { return (q15)(x + (((int32_t)(y - x) * position) >> 15)); };
            for (int c = 0; c < NUM_CHAINS; ++c) {
                CHECK(a->chains[c].volume == 14000 + c * 500);
                CHECK(a->scene_morph[c] == position);
                b->scene_morph[c] = position;
                b->chains[c].pan = blend(b->scenes[c][0].pan, b->scenes[c][1].pan);
                CHECK(a->chains[c].pan == b->chains[c].pan);
                for (int p = 0; p < 2; ++p) {
                    const Param& x = a->chains[c].units[0]->params[p]; const Param& previous = old[c][p];
                    b->chains[c].units[0]->params[p].base = blend(b->scenes[c][0].base[0][p], b->scenes[c][1].base[0][p]);
                    CHECK(x.base == b->chains[c].units[0]->params[p].base);
                    CHECK(x.step_lock == previous.step_lock && x.step_lock_override == previous.step_lock_override);
                    CHECK(x.effective == previous.effective && x.target == previous.target && x.slew_ready == previous.slew_ready);
                    CHECK(x.src == previous.src && x.mod_depth == previous.mod_depth);
                    CHECK(x.temp == previous.temp && x.temp_override == previous.temp_override);
                    CHECK(x.performance == previous.performance && x.performance_override == previous.performance_override);
                    CHECK(x.audition == previous.audition && x.audition_override == previous.audition_override);
                }
            }
            CHECK(!std::memcmp(scenes_before.get(), a->scenes, sizeof a->scenes));
        }
        // Reject a mismatch on the last chain before partially changing earlier
        // chains. Then prove retained bookkeeping releases locks at the next event.
        a->scenes[3][1].kind[0] = UnitKind::Fm;
        CHECK(!a->apply_transition_scene_morph(0));
        for (int c = 0; c < NUM_CHAINS; ++c) CHECK(a->scene_morph[c] == 32767);
        a->scenes[3][1].kind[0] = UnitKind::Osc;
        compare_audio(*a, *b, 4200, true);
        for (int c = 0; c < NUM_CHAINS; ++c) for (int p = 0; p < 2; ++p)
            CHECK(!a->chains[c].units[0]->params[p].step_lock_override);
    }
}
void storage() {
    Fixture f; Engine& e = *f.engine; populate(e);
    // The unchanged SNC7 reader canonicalizes unused lock slots to zero. The
    // audio probe deliberately leaves inert marker bytes there; use a canonical
    // stored fixture here rather than demanding persistence of unused garbage.
    for(int bank=0;bank<NUM_PATTERNS;++bank)for(auto& chain:e.sequence_pattern(bank).steps)
        for(auto& step:chain)for(int i=step.lock_count;i<SEQ_LOCKS;++i)step.locks[i]={};
    std::array<uint64_t, NUM_PATTERNS> banks{};
    for (int i = 0; i < NUM_PATTERNS; ++i) banks[i] = pattern_hash(e.sequence_pattern(i));
    const uint64_t stale = pattern_hash(e.pattern_banks[e.active_pattern]);
    CHECK(stale != banks[e.active_pattern]);
    struct File {
        char path[64] = "/tmp/seance-transition-core-XXXXXX";
        File() { int fd = mkstemp(path); CHECK(fd >= 0); close(fd); }
        ~File() { std::remove(path); std::remove((std::string(path) + ".tmp").c_str()); }
        std::vector<char> bytes() const {
            std::ifstream in(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    } saved;
    CHECK(patchio::save(e, saved.path)); const auto before = saved.bytes();
    for (int i = 0; i < NUM_PATTERNS; ++i) {
        e.clear_pattern_transition(); CHECK(e.set_pattern_transition(i, (i + 3) % 8, 16384));
        CHECK(&e.sequence_pattern(e.active_pattern) == &e.pattern);
        for (int bank = 0; bank < NUM_PATTERNS; ++bank)
            CHECK(pattern_hash(e.sequence_pattern(bank)) == banks[bank]);
    }
    CHECK(pattern_hash(e.pattern_banks[e.active_pattern]) == stale);
    CHECK(patchio::save(e, saved.path)); CHECK(saved.bytes() == before);
    auto loaded = std::make_unique<Engine>(); CHECK(patchio::load(*loaded, saved.path));
    CHECK(!loaded->pattern_transition().active);
    for (int bank = 0; bank < NUM_PATTERNS; ++bank)
        CHECK(pattern_hash(loaded->sequence_pattern(bank)) == banks[bank]);
}
} // namespace transition_core_cases
void test_transition_core() {
    transition_core_cases::endpoints_and_midpoint();
    transition_core_cases::queue_and_lifecycle();
    transition_core_cases::finish_and_scene();
    transition_core_cases::storage();
    std::puts("transition core: both schedulers, all8/endpoints/same/atomic oracle/partition/queues/lifecycle/SNC7 passed");
}
