// inst: one compact acoustic multisample source, independent of mutable SampleBank.
// Include outside namespace seance (units.h does so). Capture visual() under the
// existing audio guard; it performs no rendering, allocation or sample lookup.
#pragma once
#include "unit.h"

namespace seance {
namespace inst_bank { struct Zone; }

class InstUnit : public Unit {
public:
    enum { P_INSTRUMENT=0, P_NOTE, P_LENGTH, P_ATTACK, P_RELEASE, P_TONE, P_LEVEL, P_COUNT };
    static constexpr int INSTRUMENT_COUNT=6, DRUM_COUNT=6, VOICE_COUNT=4;
    enum class Stage : uint8_t { Off=0, Attack, Sustain, Release };
    struct VoiceVisual {
        uint32_t age=0;       // completed engine samples since this accepted strike
        uint16_t envelope=0;  // own A/S/R amplitude, 0..32767; not master PCM
        uint8_t instrument=0, note=60, stage=0;
    };
    struct Visual {
        VoiceVisual voices[VOICE_COUNT]{};
        uint32_t strikes=0;
        uint8_t active=0;     // active voice COUNT, not a bitmask
    };

    InstUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override; // live level only; edits never strike/retune a voice
    void trigger() override;
    void reset() override;
    Visual visual() const;
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Strike, P_NOTE, P_LEVEL, -1, P_LENGTH};
    }

    // Stable family IDs: piano, violin, brass (trumpet), flute, bass, drums.
    static int instrument_index(q15 value);
    static q15 instrument_value(int index);
    static const char* instrument_name(int index);
    static int note_midi(q15 value);             // nearest semitone, MIDI 24..96
    static q15 note_value(int midi);
    // Nearest GM key; ties select the lower key, extremes clamp to end zones.
    // Stable IDs: 36 kick, 38 snare, 42 hat, 45 tom, 46 openhat, 49 cymbal.
    // Every key inside a drum zone plays the SAME unpitched one-shot.
    static int drum_index(int midi);
    static int drum_note(int index);
    static const char* drum_name(int index);
    static unsigned length_ms(q15 value);       // 20..4000, pitched strike -> release; drums ignore
    static unsigned attack_ms(q15 value);       // 0..1000 (+ internal 1ms safety floor)
    static unsigned release_ms(q15 value);      // 5..3000
    static q15 length_value(unsigned ms);
    static q15 attack_value(unsigned ms);
    static q15 release_value(unsigned ms);

private:
    struct Voice {
        const inst_bank::Zone* zone=nullptr;
        uint32_t position=0, fraction=0, increment=0;
        uint32_t age=0, hold_frames=0, end_age=0;
        uint32_t attack_frames=0, release_frames=0;
        uint32_t attack_step=0, attack_remainder=0;
        uint32_t release_step=0, release_remainder=0;
        uint32_t envelope=0, envelope_error=0; // unsigned Q16 unity = 65536
        int32_t tone_state=0, last_output=0, steal_output=0;
        uint16_t tone_coefficient=0;          // Q15, 256..32768
        uint8_t steal_left=0, instrument=0, note=60;
        Stage stage=Stage::Off;
    };
    Voice voices_[VOICE_COUNT]{};
    uint32_t strikes_=0;
    uint8_t steal_cursor_=0;
    int32_t level_current_=0, level_target_=0, level_step_=0; // Q15 * 65536
    uint16_t level_error_=0, level_remainder_=0, level_left_=0;
    int8_t level_sign_=0;
    void advance_level();
};

static_assert(sizeof(InstUnit::Visual)<=64, "inst frozen visual must stay small");
static_assert(sizeof(InstUnit)<=1536, "inst must use only bounded per-unit voice state");
} // namespace seance
