// unit - one card in a chain. audio flows left to right through units.
//
// each unit exposes a fixed ParamBank of up to 12 params. a param is a q15 base value
// plus one mod slot ({source, depth}) resolved by the engine at control rate:
//   effective = clamp(base + sigil_value * depth)
//
// audio processing is block-based: process(buf, frames) IN PLACE, mono q15.
// sources (SamplePlayer, Osc, MicIn) ignore input and overwrite the buffer.
// processors (Filter, Vca, Delay...) transform it. this is the er-301 trick
// simplified: no patch cables, just a pipe.
#pragma once
#include "../audio/fixed.h"
#include <cstdint>

namespace seance {

using q15 = trackr::fx::q15;
using q31 = trackr::fx::q31;

constexpr int PARAM_BANK_SIZE  = 12;
// Compatibility name used by the UI, scenes, and serializers. New code should
// think of this as the fixed ParamBank capacity, not a per-unit visible count.
constexpr int MAX_PARAMS       = PARAM_BANK_SIZE;
constexpr int CONTROL_INTERVAL = 32;    // frames per control-rate tick (1khz @ 32k)
constexpr int ENGINE_SR        = 32000;

// modulation sources visible to every param's mod slot
enum class ModSource : uint8_t {
    None = 0,
    SigilA, SigilB, SigilC, SigilD,     // persisted ids 1..4
    EnvFollow,                         // persisted id 5: post-module chain follower
    SigilE, SigilF, SigilG, SigilH,     // ids 6..9 migrate former PBAY sources
    Count
};

// ENV splits the persisted ids; never derive a source by adding the slot index.
inline ModSource sigil_mod_source(int index) {
    return index >= 0 && index < 8 ? (ModSource)(index + (index < 4 ? 1 : 2)) : ModSource::None;
}
inline int mod_source_sigil(ModSource source) {
    const int s = (int)source;
    return s >= 1 && s <= 4 ? s - 1 : (s >= 6 && s <= 9 ? s - 2 : -1);
}

inline const char* mod_source_name(ModSource s) {
    switch (s) {
        case ModSource::None:      return "---";
        case ModSource::SigilA:    return "SG.A";
        case ModSource::SigilB:    return "SG.B";
        case ModSource::SigilC:    return "SG.C";
        case ModSource::SigilD:    return "SG.D";
        case ModSource::EnvFollow: return "ENV";
        case ModSource::SigilE:    return "SG.E";
        case ModSource::SigilF:    return "SG.F";
        case ModSource::SigilG:    return "SG.G";
        case ModSource::SigilH:    return "SG.H";
        default:                   return "?";
    }
}

// one exposed parameter of a unit
struct Param {
    const char* name  = "";
    q15 base=0;                         // persistent ui-set value
    q15 mod_depth=0;                    // persistent signed attenuverter
    q15 effective=0,target=0;           // control tick output / unsmoothed target
    q15 step_lock=0,performance=0,audition=0,temp=0;
    // Exclusive typed source, target-owned; no persisted struct layout. Scalars
    // are grouped to retain the original host Param/Unit size and voice budgets.
    uint32_t relic_source=0;
    ModSource src=ModSource::None;       // ids0..9 unchanged, None for typed cables
    uint8_t relic_port=0;
    bool mod_enabled=true;              // off/zero still occupies its one cable
    bool step_lock_override=false;
    bool performance_override=false;
    bool audition_override=false;
    bool temp_override=false;
    bool slew_ready=false;
    bool bipolar=false;

    q15 control_source() const {
        return temp_override ? temp : (performance_override ? performance :
               (audition_override ? audition : (step_lock_override ? step_lock : base)));
    }

    void resolve(q15 mod_value) {
        const q15 source = control_source();
        q31 v = (q31)source + (mod_enabled ? (((q31)mod_value * mod_depth) >> 15) : 0);
        q31 lo = bipolar ? -32767 : 0;
        if (v < lo)     v = lo;
        if (v > 32767)  v = 32767;
        target = (q15)v;
        if (!slew_ready) {
            effective = target;
            slew_ready = true;
        } else {
            // One-pole parameter slew at the 1 kHz control rate. 1/4 per tick
            // settles in roughly 20 ms: fast enough for gestures, slow enough
            // to suppress control-rate steps without audio-rate per-param work.
            effective = (q15)(effective + ((q31)target - effective) / 4);
        }
    }

    void snap() {
        effective = target = base;
        step_lock = 0;
        step_lock_override = false;
        audition_override = false;
        temp_override = false;
        slew_ready = false;
    }
};

enum class UnitKind : uint8_t {
    Empty = 0,
    SamplePlayer,   // varispeed sample playback (source)
    Grain,          // two overlapped players w/ random spray (source)
    Osc,            // sine/tri/saw/pulse (source)
    Filter,         // svf lp/hp/bp/notch
    Vca,            // gain, optionally enveloped
    Delay,          // mono delay w/ feedback
    Fold,           // wavefolder drive
    MicIn,          // microphone input source
    Recorder,       // records incoming chain audio into a SampleBank slot
    Bitcrush,       // bit depth/sample-rate reducer
    Chorus,         // modulated short delay
    Reverb,         // compact Schroeder-style ambience
    Stranular,      // Old-3DS-safe live granular processor
    Ghost,          // resonant spectral-memory / haunted comb processor
    Ring,           // ring/diode modulator with feedback carrier
    Tilt,           // cheap spectral tilt and body/air balance
    // Append-only: UnitKind numeric values are persisted in SNC patches.
    Noise,          // coloured stochastic source
    Fm,             // triggerable two-operator FM/metal source
    Motor,          // irregular pulse-train / detuned motor source
    Strain,         // moving asymmetric feedback distortion processor
    Shift,          // signed-Hz single-sideband frequency shifter
    Bird,           // monophonic triggered whistle / bird call source
    Fract,          // live history repeat / progressively shrinking microloop
    Comp,           // continuous feed-forward peak compressor (append-only: 24)
    Mouth,          // continuous three-formant vowel filter (append-only: 25)
    Arp,            // event arpeggiator, targets next pitched source (26)
    Glitch,         // previous-bar rhythmic live-input shuffle (27)
    Inst,           // built-in multisampled instrument source (append-only: 28)
    Scratch = 29,   // separate scratch/hold sample source; persisted append-only
    Paul = 30,      // independent spectral time stretch, append-only
    Relic = 31,     // retired; persisted SNCJ19 id remains reserved, never reused
    Pent = 32,      // five-vertex delayed PM source; append-only
    Rule = 33,      // dry ring-CA sample source; append-only
    Rot = 34,       // dry private bank-sample tape source; append-only
    Count = 35
};

// Persisted catalogue is append-only; availability is a separate runtime contract.
constexpr bool unit_kind_available(UnitKind kind) {
    return kind > UnitKind::Empty && kind < UnitKind::Count && kind != UnitKind::Relic;
}

const char* unit_kind_name(UnitKind k);

// A module reports its own sequencer semantics. The engine combines the last
// audible overwrite-source with downstream gate processors instead of guessing lifecycle
// from a hard-coded UI topology.
enum class SequenceLifecycle : uint8_t { Strike = 0, Gate, Loop, Flow };
struct SequencerProperties {
    bool source = false;
    bool gate_processor = false;
    SequenceLifecycle lifecycle = SequenceLifecycle::Strike;
    int8_t pitch_param = -1;
    int8_t level_param = -1;
    int8_t flow_param = -1;
    int8_t duration_param = -1;
};

class Unit {
public:
    virtual ~Unit() = default;

    UnitKind kind = UnitKind::Empty;
    bool     bypass = false;
    // Persistent action route, independent of parameter modulation. None or A-H.
    ModSource random_source = ModSource::None;

    Param  params[PARAM_BANK_SIZE];
    int    num_params = 0;

    // Construction may require a fixed heap buffer. Factories reject units whose
    // allocation failed, but this virtual keeps the contract explicit and testable.
    virtual bool ready() const { return true; }

    // Read-only, allocation-free module metadata used by the sequencer snapshot.
    virtual SequencerProperties sequencer_properties() const { return {}; }

    // The historical mono entry stays exact. Stereo admission is explicit:
    // sources declare their overwrite width; processors retain the input width.
    // A false stereo result never means pass-through or an implicit downmix.
    virtual void process(q15* buf, int frames) = 0;
    virtual uint8_t output_channels(uint8_t input) const {
        return sequencer_properties().source ? 1 : input;
    }
    virtual bool supports_stereo() const { return false; }
    virtual bool process_stereo(q15*, int) { return false; }

    // control-rate hook (after params resolve): recompute coefficients here
    virtual void control_tick() {}

    // Dedicated runtime note layer. Never a Param override or patch field.
    // Engine owns the source gate; source implementations consume only the ratio.
    struct ArpVoice {
        int8_t owner = -1, semitones = 0;
        bool gate = false;
        uint16_t gain = 32768;
    } arp_voice;
    static uint32_t arp_ratio_q24(int semitones);
    uint32_t arp_scale_increment(uint32_t increment) const;
    float arp_pitch_ratio() const;
    void set_arp_pitch(int semitones);

    // transport events from the engine
    virtual void trigger() {}          // note/step trigger (sigil steps, tap)
    virtual void reset() {}

private:
    friend class Engine;
    // Unit-owned: follows move/delete undo, never serialized or DSP-cloned.
    struct ModuleRandomRuntime {
        uint32_t rng = 0;
        uint64_t policy = 0;
        q15 before[PARAM_BANK_SIZE] = {}, after[PARAM_BANK_SIZE] = {};
        uint16_t mask = 0;
        ModSource observed_source = ModSource::None;
        bool armed = false, undo_valid = false;
    } module_random_;

protected:
    Param& add_param(const char* name, q15 base, bool bipolar = false) {
        // Constructors are the only callers. Keep release builds memory-safe if
        // a future unit accidentally declares a thirteenth parameter.
        if (num_params >= PARAM_BANK_SIZE) return params[PARAM_BANK_SIZE - 1];
        Param& p = params[num_params++];
        p.name = name; p.base = base; p.effective = base; p.target = base;
        p.slew_ready = false; p.bipolar = bipolar;
        return p;
    }
};

} // namespace seance
