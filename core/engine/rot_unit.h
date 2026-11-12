// Dry private bank-sample tape. Guard all state/config/capture calls.
#pragma once
#include "unit.h"
#include "rule_dsp.h"
#include <type_traits>
#include <cstddef>
namespace seance {
class Engine;
class RotUnit final : public Unit {
public:
    enum {P_PITCH=0,P_LEVEL,P_ROT,P_SPREAD,P_SMEAR,P_HOLES,P_CRACKLE,P_WOW,P_BITS,P_HEAL,P_START,P_LENGTH,P_COUNT};
    enum class Mode : uint8_t {Rotting=0,Healing=1,Holding=2,Tail=3};
    // law 0 = every existing project/card (exact legacy sound); law 1 = current law (new picker cards):
    // no self-heal/Tail, and the damage knobs ROT..BITS are read in two halves (raw 16383 = the law-0 maximum).
    struct Config {uint32_t seed=11;uint16_t passes=14;Mode mode=Mode::Rotting;uint8_t law=0;};
    static constexpr uint8_t LAW_LEGACY=0,LAW_CURRENT=1;
    static constexpr q15 NEW_CARD_DAMAGE=24575; // ROT..BITS of a new picker card: 75% (50% = the law-0 maximum)
    static constexpr uint32_t MAX_FRAMES=ENGINE_SR*8u,MIN_FRAMES=64,CELLS=MAX_FRAMES/64;
    static constexpr int HISTORY=12,COLUMNS=372,SOURCE_COLUMNS=292;
    int sample_slot=0;
    static bool valid_config(const Config& c) {return c.passes>=4 && c.passes<=100 && uint8_t(c.mode)<=2 && c.law<=LAW_CURRENT;}
    Config config() const {return config_;}
    bool set_config(const Config&); // seed/pass/law edit resets runtime; mode-only preserves tape/readers
    // [ROT] new card start: picker place() only; damage knobs ROT..BITS to NEW_CARD_DAMAGE, law 1 through set_config (runtime resets).
    void apply_new_card_start();
    bool set_mode(Mode); // checked; HOLD freezes damage job/pass while readers continue
    static int pitch_semitones(q15);
    static q15 pitch_value(int);
    static uint32_t length_frames(q15);
    static q15 length_value(uint32_t);
    static q15 start_value(uint32_t first,uint32_t source_frames);
    struct Window {uint32_t start=0,span=0,seam=0;bool valid=false;};
    static Window loop_projection(uint32_t source_frames,q15 start,q15 length);
    struct Visual {
        uint32_t source_generation=0,source_frames=0,serial=0,pass=0,pass_count=14;
        uint32_t loop_start=0,loop_span=0,head=0,phase_frames=0,preparing_frames=0,crackles=0;
        int sample_slot=-1;
        uint16_t phase_q8=0,drop_q8=0,rot_mean=0;
        uint8_t history=0;Mode mode=Mode::Rotting;
        bool loaded=false,active=false,paused=false,fault=false,preparing=false;
        q15 effective[P_COUNT]={}; // sounding tape damage; live pitch/level; selected window grips
        // Additive scalar authority: active damage job is not the sounding tape or edit target.
        q15 damage_effective[P_HEAL-P_ROT+1]={},edit_target[P_COUNT]={};
        uint32_t target_loop_start=0,target_loop_span=0,damage_phase_frames=0;
        bool window_pending=false;
        uint8_t law=0; // Config law of this unit (appended scalar)
    };
    struct Artwork {
        uint8_t env[HISTORY][COLUMNS]={},mask[HISTORY][COLUMNS]={};
        uint8_t scar[COLUMNS]={},spark[COLUMNS]={},source_env[SOURCE_COLUMNS]={};
        uint32_t serial=0,source_generation=0,source_frames=0,loop_start=0,loop_span=0;
        uint32_t pass[HISTORY]={};uint8_t history=0;bool valid=false,preparing=false;
        uint32_t source_scanned_frames=0;bool source_env_complete=false;
    };
    RotUnit();~RotUnit() override;
    RotUnit(const RotUnit&)=delete;RotUnit& operator=(const RotUnit&)=delete;
    bool ready() const override;
    size_t workspace_bytes() const;
    SequencerProperties sequencer_properties() const override {return {true,false,SequenceLifecycle::Flow,P_PITCH,P_LEVEL,P_ROT,-1};}
    void control_tick() override;void process(q15*,int) override;
    void trigger() override;void reset() override;void stop();void release();
    Visual visual() const;void copy_artwork(Artwork&) const; // copy-only, no PCM traversal
private:
    friend class Engine;
#ifdef SEANCE_ROT_TEST
    friend struct RotTestAccess;
#endif
    struct Workspace;Workspace* work_=nullptr;
    struct Source {int slot=-1;uint32_t generation=0,frames=0;const q15* data=nullptr;size_t elements=0;uint8_t channels=0;} source_;
    Source binding() const;bool matches() const;bool allowed() const;void synchronize();bool start();bool refresh_window();
    void prepare_frame();void source_envelope_frame();void begin_pass();void seed_spores();void mask_cell();void write_frame();void publish_column(uint32_t);
    int64_t food_original_q16(uint32_t) const;
    int32_t original(uint32_t) const;int32_t read(uint8_t,uint64_t) const;
    Config config_;Window window_,pending_window_;
    q15 window_start_value_=0,window_length_value_=0;
    const Engine* engine_=nullptr;int8_t chain_=-1,slot_=-1;
    q15 live_[P_COUNT]={},latched_[P_COUNT]={};
    rule_dsp::Random rng_,impulse_rng_;
    uint64_t head_=0,half_=0;uint32_t increment_=65536;
    uint32_t pass_=0,phase_=0,prepare_=0,write_=0,mask_at_=0,serial_=0,drop_=0,manual_left_=0;
    uint32_t cells_=0,peak_=0,hits_=0,rng_debt_=0,job_=0;
    uint64_t crackle53_=0,mean_sum_=0;uint32_t food_at_=0;
    int64_t source_sum_=0,source_mean_q16_=0;
    uint32_t centers_[6]={},widths_[6]={},strengths_[6]={},spores_=0,spores_left_=0,wow_phase_=0,wow_step_=0,wow_amp_=0;
    // law 1 only: knob scales of the damage pass in work, latched in begin_pass (rot_impl.inc, ROT_UNITY).
    struct Law {uint32_t strength=0,spread=0,smear=0,holes=0,hole_floor=5243,bits=0,bit_from=16383,crackle_amp=32766;int32_t bit_levels=48;bool clamp_seed=false,clamp_room=false;} law_;
    int64_t low_=0;uint32_t alpha_=0;uint8_t current_=0;
    bool started_=false,paused_=false,blocked_=false,manual_=false,audition_=false,released_=false,finished_=false,fault_=false,healing_=false,next_ready_=false;
    uint16_t release_left_=0,crossfade_left_=0,crossfade_total_=1600;Visual published_;
};
static_assert(RotUnit::P_COUNT==PARAM_BANK_SIZE,"ROT fixed12");
static_assert(sizeof(RotUnit::Visual)<=192 && std::is_trivially_copyable<RotUnit::Visual>::value,"ROT scalar guard DTO");
static_assert(sizeof(RotUnit::Artwork)<=10240 && std::is_trivially_copyable<RotUnit::Artwork>::value,"ROT off-stack artwork");
}
