// Separate source; PLAY's controls, reader math and runtime remain independent.
#pragma once
#include "unit.h"
#include "../synth/sampler.h"

namespace seance {
class Engine;
class ScratchUnit : public Unit {
public:
    enum { P_POSITION=0, P_GAIN, P_WINDOW, P_SPEED, P_PITCH, P_FOLLOW, P_GATE, P_COUNT };
    enum class Mode : uint8_t { Scratch=0, Hold=1 };
    enum class Map : uint8_t { PositionGain=0, PositionWindow=1, SpeedPitch=2 };
    struct Visual {
        bool loaded=false, active=false;
        int sample_slot=-1;
        uint32_t generation=0, frames=0;
        // Source-clamped pending HOLD span in frames, not a radius or current reader union.
        uint32_t window=0;
        int64_t position_q16=0;
        // Inclusive source-time endpoints of the last-used latched HOLD windows' union.
        // SCRATCH publishes its read position at both ends. Never reconstruct from center.
        int64_t window_start_q16=0, window_end_q16=0;
        int32_t speed_q16=0;
    };
    int sample_slot=0;
    Mode mode=Mode::Hold;
    Map map=Map::SpeedPitch;
    int8_t axis_x=-1, axis_y=-1; // persistent; -1 follows the selected preset
    void axis_params(int& x,int& y) const;
    static bool axis_parameter_available(Mode,int);
    bool set_axis(int axis,int param);
    bool latched() const;
    ScratchUnit();
    void set_mode(Mode);
    void set_map(Map);
    // Stable persisted map ids; SCRATCH uses two playable position/speed + gain
    // layouts. Historical PositionWindow in SCRATCH aliases position + gain.
    static void map_params(Mode, Map, int& x, int& y);
    static void map_params(Map m, int& x, int& y) { map_params(Mode::Hold,m,x,y); }
    static Map next_map(Mode mode, Map m) {
        return mode==Mode::Scratch ? (m==Map::SpeedPitch?Map::PositionGain:Map::SpeedPitch) : Map((int(m)+1)%3);
    }
    static bool parameter_available(Mode mode, int p) {
        return p>=0 && p<P_COUNT && (mode==Mode::Hold || (p!=P_WINDOW && p!=P_PITCH));
    }
    // Local candidate: linear 16..6400 source-frame span; raw16343 =>3200.
    static uint32_t window_frames(q15);
    static int32_t speed_q16(q15); // exact -4..4x, raw0 is exact0
    static int pitch_cents(q15);   // continuous +/-2400 cents
    // Gate is a duration, NOT the touch latch: raw0..32767 =>1..16 quarters
    // of the accepted native step; raw6553 =>4 quarters (one step).
    static uint8_t gate_quarters(q15);
    static constexpr q15 DEFAULT_WINDOW=16343, DEFAULT_GATE=6553;
    Visual visual() const; // copy ONCE under audio guard for both screens
    SequencerProperties sequencer_properties() const override {
        return {true,false,SequenceLifecycle::Gate,P_PITCH,P_GAIN,-1,P_GATE};
    }
    void process(q15*, int) override;
    void control_tick() override;
    void trigger() override; // finite manual gate; engine replaces its deadline for sequence
    void release();         // manual/sequence gate only, never the touch owner
    void reset() override;
private:
    friend class Engine;
    struct Source {
        int slot=-1;
        uint32_t generation=0, frames=0;
        const q15* data=nullptr;
        size_t elements=0;
        uint8_t channels=0;
    } source_;
    Source source_binding() const;
    bool source_matches(const Source&) const;
    bool context_allowed() const;
    bool configuration_matches() const;
    bool begin_voice();
    void reanchor(q15 source_position);
    void resolve_param(int p,q15 modulation);
    q15 axis_control_source(int p) const;
    void synchronize();
    void touch_begin(uint32_t);
    void touch_end(uint32_t);
    void release_sequence_gate();
    int64_t local_start(int64_t span) const;
    q15 read(int64_t position) const;
    const Engine* engine_=nullptr;
    int8_t chain_=-1, slot_=-1;
    Mode runtime_mode_=Mode::Hold;
    Map runtime_map_=Map::SpeedPitch;
    int8_t runtime_axis_x_=-1, runtime_axis_y_=-1;
    bool latch_=false;
    q15 latch_x_=0, latch_y_=0; // pre-mod controls, never shared override ownership
    uint32_t epoch_=1, touch_token_=0;
    uint32_t temp_owner_[P_COUNT]={}; // SCRT-only, keeps the shared Param/Unit footprint unchanged
    bool voice_=false, sequence_gate_=false, gate_phase_=false;
    uint64_t gate_deadline_=UINT64_MAX;
    uint32_t manual_remaining_=0;
    int64_t center_=0, target_=0, last_position_=0;
    int64_t position_component_=0, position_offset_=0;
    int64_t target_span_=0, span_[2]={}, start_[2]={};
    uint32_t phase_=0;
    uint64_t phase_increment_=0;
    uint32_t window_=3200;
    int32_t speed_=0, ratio_=65536, gain_=32767;
    uint8_t follow_shift_=6;
    int32_t envelope_=0, dc_previous_=0, dc_state_=0;
    Visual visual_{};
};
static_assert(sizeof(ScratchUnit::Visual)<=64,"bounded controller scratch DTO");
} // namespace seance
