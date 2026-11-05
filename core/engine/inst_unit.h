// inst: acoustic multisamples plus a bounded struck modal vibes source.
// Include outside namespace seance (units.h does so). Capture visual() under the
// existing audio guard; it performs no rendering, allocation or sample lookup.
#pragma once
#include "unit.h"
#include "inst_modal.h"

namespace seance {
namespace inst_bank { struct Zone; }

class InstUnit : public Unit {
public:
    enum { P_INSTRUMENT=0, P_NOTE, P_LENGTH, P_ATTACK, P_RELEASE, P_TONE, P_LEVEL,
           P_DECAY, P_HARDNESS, P_POSITION, P_DAMPING, P_STRENGTH, P_COUNT };
    static constexpr int P_BODY=P_TONE;
    static constexpr int SAMPLE_INSTRUMENT_COUNT=6, INSTRUMENT_COUNT=7,
                         MODEL_INSTRUMENT=6, DRUM_COUNT=6, VOICE_COUNT=4, MODEL_VOICE_COUNT=2;
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

    struct ModelVoiceVisual {
        uint32_t age=0, end=0;
        uint16_t envelope=0;
        uint8_t note=60, stage=0;
        bool active=false;
        uint16_t decay_ms=0,damping=0,hardness=0,position=0,body=0,strength=0;
    };
    struct ModelVisual {
        ModelVoiceVisual voices[MODEL_VOICE_COUNT]{};
        uint32_t strikes=0;
        uint8_t active=0;
    };

    InstUnit();
    ~InstUnit() override; // disarm shared focused scope before address reuse
    void process(q15* buf, int frames) override;
    void control_tick() override; // live level only; edits never strike/retune a voice
    void trigger() override;
    void reset() override;
    Visual visual() const; // sample tails only; capture both visuals for hybrid overlap
    ModelVisual model_visual() const;
    bool modeled() const { return instrument_index(params[P_INSTRUMENT].target)==MODEL_INSTRUMENT; }
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Strike, P_NOTE,
                int8_t(modeled()?P_STRENGTH:P_LEVEL), -1, P_LENGTH};
    }

    // Family IDs: piano, violin, brass, flute, bass, drums, vibes. Seven-bin map
    // is intentionally incompatible with pre-SNCG projects (explicit format fence).
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
    static unsigned decay_ms(q15 value);        // 120..6000, natural modal body decay
    static q15 decay_value(unsigned ms);

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
    inst_modal::Instrument modal_;
    uint32_t model_strikes_=0;
    uint32_t strikes_=0;
    uint8_t steal_cursor_=0;
    int32_t level_current_=0, level_target_=0, level_step_=0; // Q15 * 65536
    uint16_t level_error_=0, level_remainder_=0, level_left_=0;
    int8_t level_sign_=0;
    void advance_level();
};

static_assert(sizeof(InstUnit::Visual)<=64, "inst frozen visual must stay small");
static_assert(sizeof(InstUnit::ModelVisual)<=64, "small frozen model visual");
static_assert(InstUnit::P_COUNT==PARAM_BANK_SIZE, "inst fills but never grows fixed ParamBank");
static_assert(sizeof(InstUnit)<=1600, "inst must use only bounded per-unit voice state");
} // namespace seance
