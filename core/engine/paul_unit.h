// Independent mono spectral time-stretch source. No upstream PaulStream code.
#pragma once
#include "unit.h"
#include "../synth/sampler.h"
#include <type_traits>
namespace seance {
class Engine;
class PaulUnit : public Unit {
public:
    enum { P_POSITION=0,P_STRETCH,P_WINDOW,P_FREEZE,P_GAIN,P_PITCH,P_GATE,P_START,P_END,P_LOOP,P_COUNT };
    enum class Mode:uint8_t {Flow=0,Gate=1};
    enum class Direction:uint8_t {Forward=0,Reverse=1,PingPong=2};
    int sample_slot=0;
    Mode mode=Mode::Flow;
    Direction direction=Direction::Forward; // persistent config, NOT an eleventh parameter
    static constexpr q15 DEFAULT_STRETCH=21845,DEFAULT_WINDOW=16384,DEFAULT_GATE=6553;
    static uint32_t window_frames(q15); // nearest 2048/4096/8192
    static uint32_t stretch_q16(q15);   // continuous logarithmic 1..64; exact octave anchors
    static int pitch_cents(q15);       // bipolar +/-2400; once plus ARP note offset
    static uint8_t gate_quarters(q15);  // 1..16 quarters of native step (manual: beat/4)
    struct Visual {
        static constexpr uint32_t spectrum_count=96;
        bool loaded=false,active=false,frozen=false;
        int sample_slot=-1;
        uint32_t generation=0,frames=0,window=0,serial=0;
        int64_t position_q16=0,window_start_q16=0,window_end_q16=0;
        // Linear-frequency groups: max coherent-gain-corrected q15 amplitude
        // of actual pre-random FFT bins. Interior 2|X|/sum(window); DC/Nyquist
        // |X|/sum(window). Values may exceed32767, no UI logarithm inside guard.
        uint32_t magnitudes[spectrum_count]={};
        bool spectrum_valid=false;
    };
    PaulUnit();
    ~PaulUnit() override;
    bool ready() const override {return work_!=nullptr;}
    void set_mode(Mode); // clears PAUL runtime only
    void set_direction(Direction); // keep source location; ping-pong enters its forward leg
    Visual visual() const; // one pointer-free audio epoch, copy under guard
    SequencerProperties sequencer_properties() const override {
        return {true,false,mode==Mode::Gate?SequenceLifecycle::Gate:SequenceLifecycle::Flow,P_PITCH,P_GAIN,P_STRETCH,P_GATE};
    }
    void process(q15*,int) override;
    void control_tick() override;
    void trigger() override;
    void release();
    void reset() override;
    // Fixed workspace and bounded scheduling are local candidates, NOT ARM budgets.
    static constexpr uint32_t MAX_WINDOW=8192,WORK_PER_SAMPLE=40;
    static size_t workspace_bytes(); // shipping allocation, excluding test-only oracle copies
#ifdef SEANCE_PAUL_TEST
    uint32_t test_magnitude_bin(uint32_t) const; // last CONSUMED analysis, Q8 fractional q15 / N
    uint64_t test_gate_deadline() const {return gate_deadline_;}
    uint64_t test_clock_phase() const;
    uint32_t test_pitch_ratio() const {return ratio_;}
    uint8_t test_stage() const {return stage_;}
    bool test_reverse_leg() const {return reverse_leg_;}
    int64_t test_scan() const {return scan_;}
    uint32_t test_seek_serial() const {return seek_serial_;}
    uint32_t test_job_seek() const {return job_seek_;}
    uint64_t test_analysis_jobs() const {return analysis_jobs_;}
    uint64_t test_cached_jobs() const {return cached_jobs_;}
    uint64_t test_retired_jobs() const {return retired_jobs_;}
#endif
private:
    friend class Engine;
    PaulUnit(const PaulUnit&)=delete;
    PaulUnit& operator=(const PaulUnit&)=delete;
    struct Workspace;
    Workspace* work_=nullptr;
    struct Source {
        int slot=-1; uint32_t generation=0,frames=0;
        const q15* data=nullptr; size_t elements=0; uint8_t channels=0;
    } source_;
    Source source_binding() const;
    bool source_matches() const;
    bool context_allowed() const;
    void synchronize();
    bool begin_voice();
    void release_sequence_gate();
    void start_job();
    void job_run(uint32_t budget); // up to `budget` work items of the pending job, stage loops batched
    void consume_hop();
    int32_t read(int64_t) const;
    const Engine* engine_=nullptr;
    int8_t chain_=-1,slot_=-1;
    Mode runtime_mode_=Mode::Flow;
    Direction runtime_direction_=Direction::Forward;
    bool reverse_leg_=false,retired_for_hop_=false,analysis_valid_=false;
    uint32_t analysis_start_=0,analysis_end_=0;
#ifdef SEANCE_PAUL_TEST
    uint64_t analysis_jobs_=0,cached_jobs_=0,retired_jobs_=0;
#endif
    bool voice_=false,sequence_gate_=false,gate_phase_=false,exhausted_=false;
    uint64_t gate_deadline_=UINT64_MAX;
    uint32_t manual_remaining_=0;
    uint32_t window_=4096,stretch_=16*65536,ratio_=65536,gain_=32767;
    uint32_t region_start_=0,region_end_=0,seek_serial_=0,seen_seek_=0,serial_=0;
    q15 position_raw_=-1,start_raw_=-1,end_raw_=-1;
    bool frozen_=false,loop_=true;
    int64_t scan_=0;
    uint32_t scan_remainder_=0;
    uint8_t stage_=0; // idle/prep/forward/magnitude/clear/pitch/phase/inverse/OLA/ready
    uint32_t index_=0,length_=2,job_n_=0,job_ratio_=65536,job_stretch_=65536;
    uint32_t job_seek_=0,job_start_=0,job_end_=0;
    int64_t job_position_=0,job_analysis_start_=0;
    bool job_frozen_=false,job_loop_=true,job_terminal_=false;
    int current_=-1,pending_=0;
    uint32_t output_index_=0,output_count_=0,rng_=0x5041554cu,job_rng_=0x5041554cu;
    bool tail_valid_=false;
    int32_t envelope_=0,last_out_=0,transition_from_=0;
    uint32_t transition_left_=0;
    Visual visual_{};
};
static_assert(std::is_trivially_copyable<PaulUnit::Visual>::value,"guard-copyable PAUL DTO");
static_assert(sizeof(PaulUnit::Visual)<=448,"controller-only bounded PAUL spectrum");
} // namespace seance
