// Dry ring-CA sample source. All controls are ordinary ParamBank entries.
#pragma once
#include "unit.h"
#include "rule_dsp.h"
#include <type_traits>
#include <cstddef>
namespace seance {
class Engine;
class RuleUnit final : public Unit {
public:
    enum {P_RATE=0,P_DENSITY,P_ROOT,P_RANGE,P_ATTACK,P_RELEASE,P_MUTATE,P_SPREAD,P_VOICES,P_LEVEL,P_RULE,P_SCALE,P_COUNT};
    static constexpr int CELLS=15,RINGS=11,MAX_VOICES=32;
    struct Config {uint32_t seed=7;uint16_t seed_row=128;uint8_t phrase=1,source_root=60;};
    int sample_slot=0;
    static bool valid_config(const Config& c) {return c.seed_row<32768 && c.phrase<=1 && c.source_root<=120;}
    bool set_config(const Config& c); // checked; change resets score/voices, not params/PCM
    Config config() const {return config_;}
    // Audio-guarded persistent edits, never a trigger or voice/countdown reset.
    bool edit_rule(uint16_t rule); // raw0..255 (wide input rejects, never wraps)
    bool edit_row(uint16_t row);   // raw15bits; latest target replaces next actual row
    static q15 rule_value(uint8_t rule); // canonical persistent P_RULE encoding
    bool pending_row(uint16_t& row) const; // separate from generation-latched Visual
    void cancel_live_edits() {pending_row_=32768;} // transient only; committed bases/config stay
    static uint32_t period_frames(q15 rate,uint32_t beat); // beat multiplier .125..4; default .64 beat
    static int transpose(q15 root); // signed -24..24 semitones, source-relative (NOT absolute sample metadata)
    static uint8_t rule_number(q15 raw);
    static uint8_t scale_index(q15 raw);
    static uint8_t voice_limit(q15 raw);
    static uint32_t attack_frames(q15 raw);
    static uint32_t release_frames(q15 raw);
    struct Visual {
        bool loaded=false,active=false,paused=false,fault=false;
        int sample_slot=-1;uint32_t source_generation=0,source_frames=0,serial=0,generation=0,period_ms=0;
        uint16_t rows[RINGS]={},born[RINGS]={},sounding=0;
        uint8_t spoke[CELLS]={},accepted[3]={},accepted_count=0,rings=0,rule=90,scale=0,voices=0,source_root=60;
        uint16_t phase_q8=0,push_q8=0;
        q15 effective[P_COUNT]={};
        uint32_t period_frames=0,phase_frames=0,steals=0;
    };
    RuleUnit();
    SequencerProperties sequencer_properties() const override {return {true,false,SequenceLifecycle::Flow,P_ROOT,P_LEVEL,P_DENSITY,-1};}
    void control_tick() override;
    void process(q15*,int) override;
    void trigger() override; // reset phrase and latch current source; manual stopped audition = one generation
    void reset() override;
    void stop(); // pause score/countdown, retire voices immediately; transport resume continues score
    void release(); // end generation; release all actual notes, tails finish; next trigger resets
    Visual visual() const; // scalar only; copy under audio guard, no PCM reads
    size_t workspace_bytes() const {return sizeof(*this);}
private:
    friend class Engine;
#ifdef SEANCE_RULE_TEST
    friend struct RuleTestAccess;
    friend struct RuleLiveEditTestAccess;
#endif
    struct Source {int slot=-1;uint32_t generation=0,frames=0;const q15* data=nullptr;size_t elements=0;uint8_t channels=0;} source_;
    Source binding() const;bool matches() const;bool allowed() const;
    void synchronize();bool start();void generation();void launch(uint8_t cell,bool lone);
    void release_cell(int cell);int select_voice(int limit);
    Config config_;
    uint16_t pending_row_=32768; // sentinel; never serialized/cloned, tied to source_
    rule_dsp::Score score_;
    struct Voice {
        uint64_t position=0,increment=0,birth=0;
        uint32_t age=0,birth_generation=0,attack=1,attack_step=0,release=1,remaining=0,decay=0;
        int64_t envelope=0;uint16_t velocity=0,pan=0;int8_t cell=-1;bool active=false,releasing=false;
    } voices_[MAX_VOICES];
    const Engine* engine_=nullptr;int8_t chain_=-1,slot_=-1;
    bool started_=false,manual_=false,paused_=false,source_blocked_=false,released_=false;
    uint64_t age_=0;uint32_t period_=10240,countdown_=0,steals_=0,serial_=0;
    q15 live_[P_COUNT]={};Visual visual_;
};
static_assert(RuleUnit::P_COUNT==PARAM_BANK_SIZE,"RULE fixed12 contract");
static_assert(std::is_trivially_copyable<RuleUnit::Visual>::value,"RULE guard-copyable");
static_assert(sizeof(RuleUnit::Visual)<=160,"RULE scalar DTO budget");
}
