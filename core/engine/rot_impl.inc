#include "engine.h"
#include "rot_dsp.h"
#include <atomic>
namespace seance {
struct RotUnit::Workspace {
    const rot_dsp::Tables* lut=&rot_dsp::tables(); // once per allocation: callees skip the static guard/barrier
    q15 tape[2][MAX_FRAMES];uint16_t lost[MAX_FRAMES];
    uint32_t mask[2][CELLS],food[CELLS];int64_t seeded[CELLS];
    uint64_t food_sum=0,cumulative[CELLS];uint16_t env[COLUMNS]={},next_env[COLUMNS]={};
    uint16_t source_peaks[SOURCE_COLUMNS]={};uint32_t source_peak=0;
    uint8_t next_mask[COLUMNS]={},next_spark[COLUMNS]={},next_scar[COLUMNS]={};
    uint32_t peak_rot[COLUMNS]={}; // real column maxima across this source/window lifetime
    Artwork art;
    rot_dsp::SmearCoefficients smear_coefficients;
    rot_dsp::SmearState smear_state;
    uint32_t filter_at=0,filter_warm=0;
    int32_t quant[97]={},quant_levels=0; // law 1: quantizer value per step -48..48 for quant_levels levels, refilled when the level count changes
#ifdef SEANCE_ROT_TEST
    // Test-only actual work counters. No production storage or visual fields.
    uint64_t source_reads=0,tape_reads=0,filter_ticks=0,damage_writes=0;
    uint32_t frame_source_reads=0,frame_tape_reads=0,frame_filter_ticks=0,frame_damage_writes=0;
    uint32_t max_source_reads=0,max_tape_reads=0,max_filter_ticks=0,max_damage_writes=0;
    uint32_t frame_mask_cells=0,max_mask_cells=0;
#endif
};
// Exact int64_t(v)*h*13107/(32767ll*32768) without __aeabi_ldivmod, for the two
// overlap heads in process(). Domain: v=read() lies between two q15 tape values
// (|v|<=32768) and h=hann() is in [0,32767]. Truncating divisions nest:
// trunc(trunc(n/32768)/32767)==trunc(n/(32768*32767)), and |trunc(n/32768)|<=
// 32767*13107<2^31, so the last step is a 32-bit division by a constant.
static inline int32_t rot_overlap_term(int32_t v,int32_t h){return int32_t(int64_t(v)*h*13107/32768)/32767;}
// (y+n)%n for -n<=y<2n with no division: one conditional add or subtract.
static inline uint32_t rot_ring_near(int y,int n){return uint32_t(y<0?y+n:y>=n?y-n:y);}
static std::atomic<uint32_t> rot_workspace_used{0};
static constexpr uint32_t ROT_WORKSPACE_BUDGET=8u*1024u*1024u;
// [ROT] law 1 knob halves: every damage knob ROT..BITS of a law-1 card is read in two halves (HEAL is not).
//   raw 0..16383 (0..50%) = the whole law-0 range 0..100%, the same arithmetic with unity 16383:
//                           raw 16383 is exactly the law-0 maximum
//   raw 16383..32767      = extension, e=0..1 below
static constexpr int32_t ROT_UNITY=16383;
static constexpr int32_t ROT_X_COUNT=11; // spores per pass: 1..6 up to 50%, x(1+11e) = 12..72 at 100%
static constexpr int32_t ROT_X_STRENGTH=3; // spore amplitude .25...6 up to 50%, x(1+3e) = 1.0..2.4 at 100% (a cell saturates at 1)
static constexpr int32_t ROT_X_SPREAD=3; // growth .55 up to 50%, x(1+3e) = 2.2 at 100%
static constexpr int32_t ROT_X_SMEAR=1; // low-pass mix .55*r up to 50%, x(1+e) = 1.1*r at 100% (clamped at 1)
static constexpr int32_t ROT_X_HOLES=1; // hole loss .22*r*r per pass up to 50%, x(1+e) = .44*r*r at 100%
static constexpr int32_t ROT_HOLE_FLOOR=655; // hole floor .16 (5243) up to 50%, falls to .02 (655) at 100%
static constexpr int32_t ROT_BIT_LEVELS=16; // 48 quantizer levels up to 50%, falls to 16 at 100%
static constexpr int32_t ROT_BIT_FROM=8192; // bit erosion starts above r=.5 up to 50%, above r=.25 at 100%
static constexpr int32_t ROT_X_CRACKLE=3; // impulse probability r*(.004+.002*ramp)/32 up to 50%, x(1+3e) = x4 at 100%
static constexpr int32_t ROT_X_CRACKLE_AMP2=3; // impulse amplitude .02...12 of the loop peak up to 50%, x(1+1.5e) = x2.5 at 100% (in halves)
static constexpr int32_t ROT_X_WOW=1; // warp depth 2..62 frames up to 50%, x(1+e) = 4..124 frames at 100%
static inline int32_t rot_lower(q15 k){return std::min<int32_t>(ROT_UNITY,std::max<int32_t>(0,int32_t(k)));} // the law-0 knob 0..1 as 0..ROT_UNITY
static inline int32_t rot_excess(q15 k){return std::max<int32_t>(0,std::min<int32_t>(2*ROT_UNITY,int32_t(k))-ROT_UNITY);} // e as 0..ROT_UNITY
static inline uint32_t rot_scale(q15 k,int32_t x){return uint32_t(rot_lower(k)+x*rot_excess(k));} // (law-0 knob + x*e)*ROT_UNITY
RotUnit::RotUnit() {
    kind=UnitKind::Rot;add_param("PITCH",0,true);add_param("LEVEL",32767);add_param("ROT",32767);add_param("SPREAD",32767);
    add_param("SMEAR",32767);add_param("HOLES",32767);add_param("CRACKLE",32767);add_param("WOW",32767);add_param("BITS",32767);add_param("HEAL",32767);
    add_param("START",0);add_param("LENGTH",length_value(160000));
    (void)rot_dsp::tables();
    uint32_t used=rot_workspace_used.load();
    while(used<=ROT_WORKSPACE_BUDGET-sizeof(Workspace)) {
        if(rot_workspace_used.compare_exchange_weak(used,used+uint32_t(sizeof(Workspace)))) {
            work_=new(std::nothrow) Workspace;
            if(!work_)rot_workspace_used.fetch_sub(uint32_t(sizeof(Workspace)));
            break;
        }
    }
    reset();control_tick();
}
RotUnit::~RotUnit(){if(work_){delete work_;rot_workspace_used.fetch_sub(uint32_t(sizeof(Workspace)));}}
bool RotUnit::ready() const{return work_!=nullptr;}
size_t RotUnit::workspace_bytes() const{return sizeof(*this)+(work_?sizeof(Workspace):0);}
int RotUnit::pitch_semitones(q15 v){return int((int64_t(std::max(-32767,int(v)))*24+(v<0?-16383:16383))/32767);}
q15 RotUnit::pitch_value(int v){return q15(std::max(-24,std::min(24,v))*32767/24);}
uint32_t RotUnit::length_frames(q15 v){const uint32_t p=uint32_t(std::max(0,int(v)));return p<=8192?MIN_FRAMES+uint32_t(uint64_t(p)*(64000-MIN_FRAMES)/8192):p<=20480?64000+uint32_t(uint64_t(p-8192)*96000/12288):160000+uint32_t(uint64_t(p-20480)*96000/12287);}
q15 RotUnit::length_value(uint32_t v){v=std::max<uint32_t>(MIN_FRAMES,std::min<uint32_t>(MAX_FRAMES,v));return q15(v<=64000?(uint64_t(v-MIN_FRAMES)*8192+(64000-MIN_FRAMES)/2)/(64000-MIN_FRAMES):v<=160000?8192+(uint64_t(v-64000)*12288+48000)/96000:20480+(uint64_t(v-160000)*12287+48000)/96000);}
q15 RotUnit::start_value(uint32_t first,uint32_t frames){return frames>1?q15(uint64_t(std::min<uint32_t>(first,frames-2))*32767/(frames-2?frames-2:1)):0;}
RotUnit::Window RotUnit::loop_projection(uint32_t frames,q15 start,q15 length){
    Window w;if(frames<2)return w;
    w.start=uint32_t(uint64_t(std::max(0,int(start)))*(frames-2)/32767);
    const uint32_t available=frames-w.start;w.span=std::min<uint32_t>(length_frames(length),available);
    // Only physical guard frames may fold into the seam. No fictitious guard.
    w.seam=std::min<uint32_t>(ENGINE_SR/4,std::min<uint32_t>(w.span,available-w.span));w.valid=w.span>=2;return w;
}
bool RotUnit::set_config(const Config& c){
    if(!valid_config(c))return false;
    if(c.seed!=config_.seed || c.passes!=config_.passes || c.law!=config_.law){config_=c;reset();return true;}
    return set_mode(c.mode);
}
void RotUnit::apply_new_card_start(){
    // A new picker card rots by the current law with the damage knobs at 75%: stronger than the
    // law-0 maximum (50%), with room above. HEAL, pitch, level and the window stay as constructed.
    // The law goes through set_config, the same path as a law edit: runtime resets.
    for(int p=P_ROT;p<=P_BITS;++p){params[p].base=NEW_CARD_DAMAGE;params[p].snap();}
    Config c=config_;c.law=LAW_CURRENT;set_config(c);
}
bool RotUnit::set_mode(Mode mode){if(uint8_t(mode)>2)return false;config_.mode=mode;return true;}
RotUnit::Source RotUnit::binding() const {
    Source b;if(sample_slot<0 || sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return b;
    const auto& bank=trackr::synth::SampleBank::instance();const auto& s=bank.slot(sample_slot);
    if(s.channels!=1 && s.channels!=2)return b;
    b.slot=sample_slot;b.generation=bank.generation(sample_slot);b.frames=s.playable_frames();b.data=s.data.data();b.elements=s.data.size();b.channels=s.channels;
    if(b.frames<2 || !b.data || uint64_t(b.frames)*b.channels>b.elements)return {};
    return b;
}
bool RotUnit::matches() const {const auto b=binding();return source_.data && b.slot==source_.slot && b.generation==source_.generation && b.frames==source_.frames && b.data==source_.data && b.elements==source_.elements && b.channels==source_.channels;}
bool RotUnit::allowed() const{return !bypass && (!engine_ || (unsigned(chain_)<NUM_CHAINS && unsigned(slot_)<UNITS_PER_CHAIN && engine_->chains[int(chain_)].units[int(slot_)]==this && !engine_->chains[int(chain_)].mute && engine_->chain_audio_channels(chain_)));}
void RotUnit::reset(){source_={};window_={};pending_window_={};window_start_value_=window_length_value_=0;source_sum_=source_mean_q16_=0;healing_=false;started_=paused_=blocked_=manual_=audition_=released_=finished_=next_ready_=false;fault_=!ready();prepare_=phase_=pass_=write_=mask_at_=job_=peak_=hits_=rng_debt_=drop_=manual_left_=release_left_=crossfade_left_=food_at_=spores_left_=0;head_=half_=low_=0;published_={};rng_.seed(config_.seed);impulse_rng_.seed(config_.seed^0x524f5453u);if(work_){work_->art.valid=false;work_->art.history=0;work_->art.preparing=false;}}
void RotUnit::stop(){paused_=true;manual_=false;release_left_=0;published_.active=false;}
void RotUnit::release(){released_=true;release_left_=64;}
void RotUnit::synchronize(){if(started_ && !matches()){reset();blocked_=true;}if(!allowed())stop();}
void RotUnit::control_tick(){
    synchronize();for(int p=0;p<P_COUNT;++p){const auto& v=params[p];live_[p]=q15(std::max(p==P_PITCH?-32767:0,std::abs(int(v.target)-v.effective)<=3?int(v.target):int(v.effective)));}
    increment_=musical_note::ratio_q16(pitch_semitones(live_[P_PITCH]));
    pending_window_=loop_projection(started_?source_.frames:binding().frames,params[P_START].target,params[P_LENGTH].target);
}
bool RotUnit::start(){
    if(!ready() || !allowed() || blocked_){return false;}source_=binding();if(!source_.data)return false;
    window_=loop_projection(source_.frames,live_[P_START],live_[P_LENGTH]);if(!window_.valid)return false;
    window_start_value_=live_[P_START];window_length_value_=live_[P_LENGTH];
    pending_window_=window_;cells_=(window_.span+63)/64;started_=true;paused_=false;job_=0;prepare_=0;food_at_=0;current_=0;
#ifdef SEANCE_ROT_TEST
    work_->source_reads=work_->tape_reads=work_->filter_ticks=work_->damage_writes=0;
    work_->max_source_reads=work_->max_tape_reads=work_->max_filter_ticks=work_->max_damage_writes=work_->max_mask_cells=0;
#endif
    work_->food_sum=0;std::memset(static_cast<void*>(&work_->art),0,sizeof(work_->art));work_->art.preparing=true;work_->art.serial=serial_;
    std::fill(work_->peak_rot,work_->peak_rot+COLUMNS,uint32_t(0));
    std::fill(work_->source_peaks,work_->source_peaks+SOURCE_COLUMNS,uint16_t(0));work_->source_peak=0;
    std::fill(work_->env,work_->env+COLUMNS,uint16_t(0));
    std::fill(work_->mask[0],work_->mask[0]+cells_,uint32_t(0));std::fill(work_->mask[1],work_->mask[1]+cells_,uint32_t(0));std::fill(work_->food,work_->food+cells_,uint32_t(0));
    return true;
}
bool RotUnit::refresh_window(){
    if(!pending_window_.valid || (pending_window_.start==window_.start && pending_window_.span==window_.span))return false;
    // A genuinely different region is fresh, never reversible damage retention.
    // HOLD/Tail stay frozen; explicit auditions retain their bounded ownership.
    const bool tail=finished_,manual=manual_,audition=audition_;
    reset();live_[P_START]=params[P_START].target;live_[P_LENGTH]=params[P_LENGTH].target;
    ++serial_;if(!start())return false;finished_=tail;manual_=manual;audition_=audition;manual_left_=manual?window_.span:0;
    return true;
}
void RotUnit::trigger(){reset();control_tick();if(start()){manual_=audition_=engine_ && !engine_->transport_playing;manual_left_=window_.span;}}
int32_t RotUnit::original(uint32_t at) const {
    auto mono=[&](uint32_t i){
#ifdef SEANCE_ROT_TEST
        work_->source_reads+=source_.channels;work_->frame_source_reads+=source_.channels;work_->max_source_reads=std::max(work_->max_source_reads,work_->frame_source_reads);
#endif
        return source_.channels==2?(int32_t(source_.data[size_t(i)*2])+source_.data[size_t(i)*2+1])/2:int32_t(source_.data[i]);};
    int32_t v=mono(window_.start+at);
    if(at<window_.seam){const uint32_t fade=uint32_t(rot_dsp::hann(uint64_t(at)*65536,uint64_t(window_.seam)*131072));v=int32_t((int64_t(v)*fade+int64_t(mono(window_.start+window_.span+at))*(32767-fade))/32767);}
    return v;
}
int64_t RotUnit::food_original_q16(uint32_t at) const {
    auto centered=[&](uint32_t i){
#ifdef SEANCE_ROT_TEST
        work_->source_reads+=source_.channels;work_->frame_source_reads+=source_.channels;work_->max_source_reads=std::max(work_->max_source_reads,work_->frame_source_reads);
#endif
        const int64_t sum=source_.channels==2?int32_t(source_.data[size_t(i)*2])+source_.data[size_t(i)*2+1]:source_.data[i];return sum*65536/source_.channels-source_mean_q16_;};
    int64_t v=centered(window_.start+at);
    if(at<window_.seam){
        const uint32_t phase=window_.seam>1?uint32_t(uint64_t(at)*2147483648ull/(window_.seam-1)):0;
        const int64_t fade=(1073741824ll-rot_dsp::sine30(phase+0x40000000u))/2;
        v=(v*fade+centered(window_.start+window_.span+at)*(1073741824-fade))/1073741824;
    }
    return v;
}
void RotUnit::prepare_frame(){
    // Food must use the original recipe's whole-source DC and endpoint seam,
    // not q15-rounded peaks. This bounded scan is preparation, never a bank edit.
    if(!work_->art.source_env_complete)return;
    const auto& lut=rot_dsp::tables();
    for(int n=0;n<16 && prepare_<window_.span;++n,++prepare_){const int32_t v=original(prepare_);work_->tape[0][prepare_]=work_->tape[1][prepare_]=q15(v);work_->lost[prepare_]=32767;
        const uint32_t mag=uint32_t(std::abs(v));peak_=std::max<uint32_t>(peak_,mag);const uint32_t cell=prepare_/64;work_->food[cell]=std::max<uint32_t>(work_->food[cell],uint32_t(std::abs(food_original_q16(prepare_))));
        const uint32_t col=uint32_t(uint64_t(prepare_)*COLUMNS/window_.span);work_->env[col]=uint16_t(std::max<uint32_t>(work_->env[col],mag));
    }
    if(prepare_==window_.span && food_at_<cells_){
        for(int n=0;n<2 && food_at_<cells_;++n,++food_at_){uint64_t sum=0,weight=0;const bool near=cells_>=13; // |k|<=12<cells_, food_at_<cells_: k%cells_==k, food_at_+k in [-12,cells_+11]
            for(int k=-12;k<=12;++k){const uint32_t w=rot_dsp::gaussian(lut,uint32_t(std::abs(k)),4)>>5;const uint32_t at=near?rot_ring_near(int(food_at_)+k,int(cells_)):uint32_t((int(food_at_)+int(cells_)+k%int(cells_))%int(cells_));sum+=uint64_t(work_->food[at])*w;weight+=w;}
            work_->food_sum+=sum/weight;work_->cumulative[food_at_]=work_->food_sum;
        }
    }
    if(prepare_==window_.span && food_at_==cells_){
        auto& a=work_->art;a.valid=true;a.preparing=false;a.history=1;a.source_generation=source_.generation;a.source_frames=source_.frames;a.loop_start=window_.start;a.loop_span=window_.span;a.pass[0]=0;
        for(int c=0;c<COLUMNS;++c){a.env[0][c]=rot_dsp::envelope(lut,work_->env[c],peak_);a.mask[0][c]=0;}
        begin_pass();
    }
}
void RotUnit::source_envelope_frame(){
    auto& a=work_->art;if(a.source_env_complete)return;
    // At most32 source frames /64 stereo sample reads; reuse the same load for
    // peak and exact whole-source DC. Scheduling is per audio frame, not CPU time.
    for(int n=0;n<32 && a.source_scanned_frames<source_.frames;++n){const uint32_t i=a.source_scanned_frames++;
        const int32_t sum=source_.channels==2?(int32_t(source_.data[size_t(i)*2])+source_.data[size_t(i)*2+1]):int32_t(source_.data[i]);
        const int32_t v=sum/source_.channels;
        const uint32_t mag=uint32_t(std::abs(v)),c=uint32_t(uint64_t(i)*SOURCE_COLUMNS/source_.frames);
        source_sum_+=sum;
#ifdef SEANCE_ROT_TEST
        work_->source_reads+=source_.channels;work_->frame_source_reads+=source_.channels;work_->max_source_reads=std::max(work_->max_source_reads,work_->frame_source_reads);
#endif
        work_->source_peaks[c]=uint16_t(std::max<uint32_t>(work_->source_peaks[c],mag));work_->source_peak=std::max<uint32_t>(work_->source_peak,mag);
    }
    if(a.source_scanned_frames==source_.frames){const int64_t count=int64_t(source_.frames)*source_.channels;source_mean_q16_=(source_sum_/count)*65536+(source_sum_%count)*65536/count;const auto& lut=rot_dsp::tables();for(int c=0;c<SOURCE_COLUMNS;++c)a.source_env[c]=rot_dsp::envelope(lut,work_->source_peaks[c],work_->source_peak);a.source_env_complete=true;}
}
void RotUnit::seed_spores(){
    // One batch of <=6 spores: centers_/widths_/strengths_ are [6]. Draw order per spore: centre, width, strength.
    spores_=std::min<uint32_t>(6,spores_left_);spores_left_-=spores_;
    for(uint32_t n=0;n<spores_;++n){
        const auto product=rule_dsp::product(rng_.uniform53(),work_->food_sum);
        const uint64_t target=(product.hi<<11)|(product.lo>>53);
        uint32_t lo=0,hi=cells_-1;
        for(int k=0;k<12 && lo<hi;++k){const uint32_t mid=(lo+hi)/2;if(work_->cumulative[mid]<=target)lo=mid+1;else hi=mid;}
        centers_[n]=lo;widths_[n]=6+rng_.integer(34);
        const auto strength=rule_dsp::product(rng_.uniform53(),375809638u);
        strengths_[n]=268435456u+uint32_t((strength.hi<<11)|(strength.lo>>53));
    }
}
void RotUnit::begin_pass(){
    std::copy(live_,live_+P_COUNT,latched_);write_=mask_at_=hits_=mean_sum_=0;next_ready_=false;low_=0;
    const uint32_t heal_from=config_.passes-std::max<uint32_t>(3,config_.passes/5);
    // law 1: rotting never heals or stops by itself; healing is Mode::Healing only. `passes`
    // is the ramp length: spores, smear cutoff, crackle rate and wow depth grow until
    // pass==passes and then hold that strength.
    const bool current_law=config_.law==LAW_CURRENT;
    const uint32_t ramp=std::min<uint32_t>(pass_,config_.passes);
    if(current_law){
        healing_=config_.mode==Mode::Healing;
        // Knob scales of this pass, latched once here: mask_cell/write_frame only multiply by them.
        law_.strength=rot_scale(latched_[P_ROT],ROT_X_STRENGTH);law_.spread=rot_scale(latched_[P_SPREAD],ROT_X_SPREAD);
        law_.smear=rot_scale(latched_[P_SMEAR],ROT_X_SMEAR);law_.holes=rot_scale(latched_[P_HOLES],ROT_X_HOLES);
        law_.hole_floor=uint32_t(5243-int64_t(5243-ROT_HOLE_FLOOR)*rot_excess(latched_[P_HOLES])/ROT_UNITY);
        law_.bits=uint32_t(rot_lower(latched_[P_BITS]));
        law_.bit_from=uint32_t(16383-int64_t(16383-ROT_BIT_FROM)*rot_excess(latched_[P_BITS])/ROT_UNITY);
        law_.bit_levels=int32_t(48-int64_t(48-ROT_BIT_LEVELS)*rot_excess(latched_[P_BITS])/ROT_UNITY);
        law_.crackle_amp=uint32_t(2*ROT_UNITY+ROT_X_CRACKLE_AMP2*rot_excess(latched_[P_CRACKLE]));
        law_.clamp_seed=rot_excess(latched_[P_ROT])>0;law_.clamp_room=law_.clamp_seed || rot_excess(latched_[P_SPREAD])>0;
        // The quantizer of write_frame without a per-sample division by a variable level count.
        if(work_->quant_levels!=law_.bit_levels){for(int q=-48;q<=48;++q)work_->quant[q+48]=q*32768/law_.bit_levels;work_->quant_levels=law_.bit_levels;}
        // Spores of this pass: 1..6 by the ramp, times 1..12 by the upper half of ROT. They are seeded in
        // batches of <=6 by extra mask sweeps (job 9), so one mask cell still evaluates <=6 Gaussians.
        // Sweeps per pass: <=12 seeding + 1 growth, 13*L/128 frames, inside the L/8 fade of a short loop.
        spores_left_=healing_?0:uint32_t(uint64_t(1+5*ramp/config_.passes)*uint32_t(ROT_UNITY+ROT_X_COUNT*rot_excess(latched_[P_ROT]))/uint32_t(ROT_UNITY));
    }else{
    healing_=config_.mode==Mode::Healing || pass_>=heal_from;
    spores_left_=healing_?0:1+5*pass_/config_.passes; // <=5 while pass<passes: one batch
    }
    // Current mask copied into private scratch by a bounded cell job below.
    seed_spores();
    const uint32_t fc=1400-900*std::min<uint32_t>(pass_,config_.passes)/config_.passes;
    work_->smear_coefficients=rot_dsp::smear_coefficients(fc);
    work_->smear_state.reset();work_->filter_at=work_->filter_warm=0;
    if(current_law){
        crackle53_=uint64_t(1125899906843ull+562949953421ull*ramp)*rot_scale(latched_[P_CRACKLE],ROT_X_CRACKLE)/uint32_t(ROT_UNITY);
        wow_amp_=uint32_t(uint64_t(2+60*ramp/config_.passes)*rot_scale(latched_[P_WOW],ROT_X_WOW)/uint32_t(ROT_UNITY));
    }else{
    crackle53_=uint64_t(1125899906843ull+562949953421ull*pass_)*uint16_t(latched_[P_CRACKLE])/32767;
    wow_amp_=uint32_t(uint64_t(2+60*pass_/config_.passes)*uint16_t(latched_[P_WOW])/32767);
    }
    wow_phase_=impulse_rng_.u32();wow_step_=uint32_t(uint64_t(2+pass_%3)*4294967296ull/window_.span);
    std::fill(work_->next_env,work_->next_env+COLUMNS,uint16_t(0));std::fill(work_->next_spark,work_->next_spark+COLUMNS,uint8_t(0));job_=1;
}
void RotUnit::mask_cell(){
    const auto& lut=*work_->lut;
    const bool current_law=config_.law==LAW_CURRENT;
    if(job_==1 || job_==9){const uint32_t c=mask_at_;uint64_t v=job_==1?uint64_t(work_->mask[current_][c]):uint64_t(work_->seeded[c]); // job 9 (law 1): one more batch of spores over the same scratch
#ifdef SEANCE_ROT_TEST
        ++work_->frame_mask_cells;work_->max_mask_cells=std::max(work_->max_mask_cells,work_->frame_mask_cells);
#endif
        if(!healing_)for(uint32_t n=0;n<spores_;++n){const uint32_t d=std::min<uint32_t>(rot_dsp::reduce32(c+cells_-centers_[n],cells_),rot_dsp::reduce32(centers_[n]+cells_-c,cells_));const uint64_t g=(uint64_t(strengths_[n])*rot_dsp::gaussian(lut,d,widths_[n]))>>30;v+=current_law?g*law_.strength/uint32_t(ROT_UNITY):g*uint16_t(latched_[P_ROT])/32767;} // c,centers_<cells_: both reduce32 operands in [1,2cells_-1], one subtract
        // law 1 above 50% ROT: <=4.0 keeps the 55-tap sum of job 2 inside int64 (law 0 and law 1 up to 50%: a cell is <=1+6*.6).
        work_->seeded[c]=int64_t(current_law && law_.clamp_seed?std::min<uint64_t>(v,4294967296ull):v);
        if(++mask_at_==cells_){mask_at_=0;if(spores_left_){seed_spores();job_=9;}else job_=2;}
    } else if(job_==2){const uint32_t c=mask_at_;const int64_t v=work_->seeded[c];uint32_t next;
#ifdef SEANCE_ROT_TEST
        ++work_->frame_mask_cells;work_->max_mask_cells=std::max(work_->max_mask_cells,work_->frame_mask_cells);
#endif
        if(healing_)next=uint32_t(v-(v*uint16_t(latched_[P_HEAL])/32767)*483183821/1073741824);
        else {int64_t sum=0,weight=0;const bool near=cells_>=28 && c<cells_; // |k|<=27<cells_: k%cells_==k, c+k in [-27,cells_+26]
            for(int k=-27;k<=27;++k){const uint32_t w=rot_dsp::gaussian(lut,uint32_t(std::abs(k)),9)>>5;const uint32_t at=near?rot_ring_near(int(c)+k,int(cells_)):uint32_t((int(c)+int(cells_)+k%int(cells_))%int(cells_));sum+=int64_t(work_->seeded[at])*w;weight+=w;}
            int64_t grown;
            if(current_law){
                // Above 50% (ROT or SPREAD) an over-seeded cell (v>1) has no room left. Law 0 lets (1-v) go
                // negative there (a full mask flickers 96..99.9%); with stronger spores that would punch
                // saturated cells back to 0. Up to 50% the law-0 arithmetic is kept.
                const int64_t room=law_.clamp_room?std::max<int64_t>(0,1073741824-v):1073741824-v;
                const int64_t spread=((sum/weight)/64)*room/16777216;
                const int64_t growth=(spread*590558003)/1073741824;
                grown=v+growth*law_.spread/ROT_UNITY;
            }else{
            const int64_t spread=((sum/weight)/64)*(1073741824-v)/16777216;
            const int64_t growth=(spread*590558003)/1073741824;
            grown=v+growth*uint16_t(latched_[P_SPREAD])/32767;
            }
            next=uint32_t(std::max<int64_t>(0,std::min<int64_t>(1073741824,grown)));}
        work_->mask[1-current_][c]=next;mean_sum_+=next;
        if(++mask_at_==cells_){mask_at_=0;job_=healing_ || !latched_[P_SMEAR]?3:7;}
    }
}
int32_t RotUnit::read(uint8_t bank,uint64_t position) const {
#ifdef SEANCE_ROT_TEST
    work_->tape_reads+=2;work_->frame_tape_reads+=2;work_->max_tape_reads=std::max(work_->max_tape_reads,work_->frame_tape_reads);
#endif
    const uint64_t span=uint64_t(window_.span)*65536;position=rot_dsp::reduce(position,span);const uint32_t a=uint32_t(position>>16),b=a+1==window_.span?0:a+1;const int32_t av=work_->tape[bank][a];return av+int32_t((int64_t(work_->tape[bank][b]-av)*uint16_t(position))/65536);}
void RotUnit::write_frame(){
#ifdef SEANCE_ROT_TEST
    ++work_->damage_writes;++work_->frame_damage_writes;work_->max_damage_writes=std::max(work_->max_damage_writes,work_->frame_damage_writes);
    // Direct current-tape and next-filter-tape reads, in addition to read().
    ++work_->tape_reads;++work_->frame_tape_reads;
    if(!healing_ && latched_[P_SMEAR]){++work_->tape_reads;++work_->frame_tape_reads;}
    work_->max_tape_reads=std::max(work_->max_tape_reads,work_->frame_tape_reads);
#endif
    const auto& lut=*work_->lut;
    const bool current_law=config_.law==LAW_CURRENT;
    const uint32_t i=write_,cell=std::min<uint32_t>(cells_-1,i/64);const int32_t r0=work_->mask[1-current_][cell],r1=work_->mask[1-current_][std::min<uint32_t>(cells_-1,cell+1)];
    const uint32_t r30=uint32_t(r0+int64_t(r1-r0)*int32_t(i&63u)/64),r=uint32_t(uint64_t(r30)*32767/1073741824);int32_t v=work_->tape[current_][i];
    const uint32_t col=i<11545612u?i*uint32_t(COLUMNS)/window_.span:uint32_t(uint64_t(i)*COLUMNS/window_.span); // i*372<2^32 (i<span<=256000 on the real path)
    if(healing_){{const int64_t q=int64_t(original(i)-v)*uint16_t(latched_[P_HEAL])*2/32767;v+=int32_t(q==int32_t(q)?int64_t(int32_t(q)/5):q/5);}work_->lost[i]+=uint16_t(uint32_t(32767-work_->lost[i])*uint16_t(latched_[P_HEAL])*2/(32767u*5));} // ==/(32767ll*5): truncating divisions nest
    else {
        const int32_t warp=int32_t(int64_t(rot_dsp::sine(lut,wow_phase_))*wow_amp_*65536/32767);wow_phase_+=wow_step_;
        const int64_t pos=int64_t(i)*65536+warp,span=int64_t(window_.span)*65536;
        v=read(current_,rot_dsp::wrap_position(pos,span)); // causal warp before nonlinear damage, not offline post-warp
        // Complete bounded forward/backward private-tape filter precedes this
        // ascending damage/RNG job. No phase-bearing lowpass in the dry/wet mix.
        const int32_t low=latched_[P_SMEAR]?work_->tape[1-current_][i]:v;
        uint32_t before,after;
        if(current_law){
            // law 1 smear: the mix clamps at 1 (the frame becomes the low-passed tape). The same exact nested
            // truncation as law 0 with ROT_UNITY as the last divisor: |x|<65536*32767*32767*16383<2^63.
            const int64_t num=int64_t(r)*18022*law_.smear;
            if(num>=32767ll*32767*ROT_UNITY)v=low;
            else {const int64_t x=(low-v)*num;const uint64_t m=x<0?0-uint64_t(x):uint64_t(x);const int64_t q=int64_t(m/32767)/32767;v+=q<=2147483647?(x<0?-(int32_t(q)/ROT_UNITY):int32_t(q)/ROT_UNITY):int32_t(x/(32767ll*32767*ROT_UNITY));}
            // law 1 holes: wear<=32767*32767*7209*32766<2^58; after two /32767 it is <2^28, so the last step is 32-bit.
            const uint64_t wear=uint64_t(r)*r*7209*law_.holes;const uint32_t factor=32767-uint32_t(int64_t(wear/32767)/32767)/uint32_t(ROT_UNITY);
            // A floor that was lower on an earlier pass never refills (after<=before).
            before=work_->lost[i];after=std::min<uint32_t>(before,std::max<uint32_t>(law_.hole_floor,uint32_t(uint64_t(before)*factor/32767)));
        }else{
        {const int64_t x=(low-v)*int64_t(r)*18022*uint16_t(latched_[P_SMEAR]);const uint64_t m=x<0?0-uint64_t(x):uint64_t(x);const int64_t q=int64_t(m/32767)/32767;v+=q<=2147483647?(x<0?-(int32_t(q)/32767):int32_t(q)/32767):int32_t(x/(32767ll*32767*32767));} // floor(floor(floor(|x|/a)/a)/a)==floor(|x|/a^3), sign restored: ==trunc(x/32767^3); the u64->i64 step keeps gcc from refolding into one /32767^3 call
        const uint64_t holes=uint64_t(r)*r*7209*uint16_t(latched_[P_HOLES]);const int64_t holes_q=int64_t(holes/32767)/32767;const uint32_t factor=32767-(holes_q<=4294967295ll?uint32_t(holes_q)/32767:uint32_t(holes/(32767ull*32767*32767))); // same nesting, exact for every value
        before=work_->lost[i];after=std::max<uint32_t>(5243,uint32_t(uint64_t(before)*factor/32767));
        }
        work_->lost[i]=uint16_t(after);v=v>=-32768 && v<=32768 && after<=65535?int32_t(v*int32_t(after))/int32_t(before):int32_t(int64_t(v)*after/before); // |v*after|<=32768*65535<2^31
        if(current_law){
            // law 1 bits: 48 down to 16 levels, starting above r=.5 down to .25. |v|<=32768 here (a tape read
            // mixed towards a tape value, then scaled down), so the step is within +-levels<=48 and the
            // per-pass table stands for step*32768/levels; any other step keeps that expression.
            const uint32_t erosion=(r>law_.bit_from?std::min<uint32_t>(32767,2*(r-law_.bit_from)):0u)*law_.bits/uint32_t(ROT_UNITY);
            const int32_t step=(v*law_.bit_levels+(v<0?-16384:16384))/32768;
            const int32_t quant=step>=-48 && step<=48?work_->quant[step+48]:step*32768/law_.bit_levels;v+=int32_t(int64_t(quant-v)*erosion/32767);
        }else{
        // 32-bit is exact: the min() bounds the first factor by 32767 and the
        // second is a uint16, so the product is <=32767*65535<2^32.
        const uint32_t erosion=(r>16383?std::min<uint32_t>(32767,2*(r-16383)):0u)*uint32_t(uint16_t(latched_[P_BITS]))/32767u;
        const int32_t quant=(v*48+(v<0?-16384:16384))/32768*32768/48;v+=int32_t(int64_t(quant-v)*erosion/32767);
        }
        const auto probability=rule_dsp::product(crackle53_,r30);
        const uint64_t threshold=(probability.hi<<34)|(probability.lo>>30);
        const bool hit=rng_.uniform53()<threshold;
        if(hit){++hits_;const uint32_t sign=impulse_rng_.integer(2);uint32_t amp=655+uint32_t((uint64_t(impulse_rng_.u32())*3277)>>32);if(current_law)amp=amp*law_.crackle_amp/uint32_t(2*ROT_UNITY);const int32_t impulse=int32_t(uint64_t(amp)*peak_/32768)*(sign?1:-1);v+=impulse;if(impulse)work_->next_spark[col]=255;}
        v=rot_dsp::clip(lut,trackr::fx::sat_q15(v));
    }
    work_->tape[1-current_][i]=trackr::fx::sat_q15(v);work_->next_env[col]=uint16_t(std::max<uint32_t>(work_->next_env[col],uint32_t(std::abs(v))));work_->next_mask[col]=uint8_t(r*255/32767);work_->peak_rot[col]=std::max<uint32_t>(work_->peak_rot[col],r30);work_->next_scar[col]=uint8_t(uint64_t(work_->peak_rot[col])*255/1073741824);
    if(++write_==window_.span){job_=4;rng_debt_=healing_?0:hits_*2;}
}
void RotUnit::publish_column(uint32_t c){auto& a=work_->art;for(int h=HISTORY-1;h>0;--h){a.env[h][c]=a.env[h-1][c];a.mask[h][c]=a.mask[h-1][c];}a.env[0][c]=rot_dsp::envelope(work_->next_env[c],peak_);a.mask[0][c]=work_->next_mask[c];a.scar[c]=work_->next_scar[c];a.spark[c]=work_->next_spark[c];}
RotUnit::Visual RotUnit::visual() const {
    Visual v=published_;const auto b=binding();v.loaded=bool(b.data);v.source_generation=b.generation;v.source_frames=b.frames;v.sample_slot=b.slot;v.pass=pass_;v.pass_count=config_.passes;v.serial=serial_;v.history=work_?work_->art.history:0;
    v.mode=finished_?Mode::Tail:config_.mode==Mode::Holding?Mode::Holding:healing_?Mode::Healing:Mode::Rotting;v.paused=paused_;v.fault=fault_;v.preparing=started_&&job_==0;v.preparing_frames=prepare_;v.loop_start=window_.start;v.loop_span=window_.span;v.phase_frames=uint32_t(head_>>16);v.head=window_.start+v.phase_frames;v.phase_q8=window_.span?uint16_t(uint64_t(v.phase_frames)*256/window_.span):0;v.drop_q8=uint16_t(std::min<uint32_t>(256,drop_*256/(ENGINE_SR/10)));v.damage_phase_frames=phase_;
    v.effective[P_PITCH]=live_[P_PITCH];v.effective[P_LEVEL]=live_[P_LEVEL];
    v.effective[P_START]=window_start_value_;v.effective[P_LENGTH]=window_length_value_;
    for(int p=P_ROT;p<=P_HEAL;++p)v.damage_effective[p-P_ROT]=started_ && job_!=0?latched_[p]:q15(0);
    for(int p=0;p<P_COUNT;++p)v.edit_target[p]=params[p].target;
    v.target_loop_start=pending_window_.start;v.target_loop_span=pending_window_.span;
    v.window_pending=pending_window_.valid && (pending_window_.start!=window_.start || pending_window_.span!=window_.span);
    v.law=config_.law;
    v.active=started_ && !v.preparing && !paused_ && !released_ && allowed() && matches();return v;
}
void RotUnit::copy_artwork(Artwork& out) const {if(work_ && started_ && matches()){out=work_->art;out.preparing=job_==0;}else{out.valid=false;out.history=0;out.preparing=false;}}
void RotUnit::process(q15* out,int frames){
    if(!out || frames<=0)return;
    const bool running=!engine_ || engine_->transport_playing;
    if(audition_ && running){reset();control_tick();}
    synchronize();
    if(!allowed() || blocked_ || (!running && !manual_) || (released_ && !release_left_)){std::fill(out,out+frames,q15(0));return;}
    if(!started_ && !start()){std::fill(out,out+frames,q15(0));return;}if(running)paused_=false;
    const auto& lut=rot_dsp::tables();
    for(int n=0;n<frames;++n){
#ifdef SEANCE_ROT_TEST
        work_->frame_source_reads=work_->frame_tape_reads=work_->frame_filter_ticks=work_->frame_damage_writes=work_->frame_mask_cells=0;
#endif
        source_envelope_frame();
        if(job_==0){prepare_frame();out[n]=0;continue;}
        const uint64_t span=uint64_t(window_.span)*65536;
        const uint64_t opposite=rot_dsp::reduce(half_+span/2,span); // ==(half_+span/2)%span
        const uint32_t p0=uint32_t(half_*4294967296ull/span); // hann()'s own phase, including its uint64 wrap beyond 65536 frames
        // span<=2^32 and half_<span: opposite==half_+-span/2 and span/2*2^32/span==2^31 exactly, so its phase is p0+2^31 (mod 2^32).
        // Longer loops keep the original call: there pos*2^32 wraps in uint64 and that wrapped phase is the sound.
        const int32_t h0=rot_dsp::hann_phase(lut,p0),h1=span<=4294967296ull && half_<span?rot_dsp::hann_phase(lut,p0+0x80000000u):rot_dsp::hann(lut,opposite,span);
        int64_t sample=int64_t(read(current_,head_))*24576/32768+rot_overlap_term(read(current_,half_),h0)+rot_overlap_term(read(current_,opposite),h1);
        if(crossfade_left_){const int64_t previous=int64_t(read(1-current_,head_))*24576/32768+rot_overlap_term(read(1-current_,half_),h0)+rot_overlap_term(read(1-current_,opposite),h1);
            {const int64_t mix=sample*(crossfade_total_-crossfade_left_)+previous*crossfade_left_;sample=mix==int32_t(mix)?int64_t(int32_t(mix)/int32_t(crossfade_total_)):mix/crossfade_total_;}--crossfade_left_; // |mix|<=50790*1600<2^27 (bound below): 32-bit divide
        }
        // |sample|<=24576+2*13107: each term above is bounded by its weight, the
        // crossfade is a convex mix (0<=crossfade_left_<=crossfade_total_: both are
        // set together at the pass edge and left only counts down), release_left_
        // <=64 only scales down. LEVEL is a nonnegative q15, so the product fits
        // int32 and the 32-bit division equals the original 64-bit one.
        if(release_left_){sample=sample*release_left_/64;}out[n]=trackr::fx::sat_q15(int32_t(sample)*int32_t(uint16_t(live_[P_LEVEL]))/32767);
        const bool reader_wrap=head_+increment_>=span;
        head_=rot_dsp::reduce(head_+increment_,span);half_=rot_dsp::reduce(half_+increment_/2,span);
        if(reader_wrap && !released_ && (config_.mode==Mode::Holding || finished_) && refresh_window())continue;
        if(release_left_ && !--release_left_){paused_=true;manual_=false;}
        if(manual_ && manual_left_ && !--manual_left_)release();
        if(config_.mode!=Mode::Holding && !finished_ && !released_){
            // Maximum2 mask jobs +4 total filter ticks/PCM writes per audio frame.
            // Warmup <=L/4 per direction and a short-loop fade <=L/8 leave room
            // for L forward +L backward +L damage writes in one real pass.
            // Ordinary loops retain256 warmup ticks and the original50ms fade.
            // Scratch is the existing next tape, never a future mask/audio table.
            // law 1 adds up to 12 seeding sweeps (job 9) to the mask jobs: still 2 cells per frame.
            const uint32_t warm_frames=std::min<uint32_t>(256,window_.span/4);
            if(job_==1 || job_==2 || job_==9){mask_cell();mask_cell();}
            for(int k=0;k<4 && !crossfade_left_;++k){
                if(job_==7){
#ifdef SEANCE_ROT_TEST
                    ++work_->filter_ticks;++work_->frame_filter_ticks;work_->max_filter_ticks=std::max(work_->max_filter_ticks,work_->frame_filter_ticks);
#endif
                    const bool warm=work_->filter_warm<warm_frames;
                    const uint32_t i=warm?window_.span-warm_frames+work_->filter_warm:work_->filter_at;
                    const int32_t warp=int32_t(int64_t(rot_dsp::sine(lut,wow_phase_+wow_step_*i))*wow_amp_*65536/32767);
                    const int32_t v=read(current_,rot_dsp::wrap_position(int64_t(i)*65536+warp,int64_t(window_.span)*65536));
                    const int32_t filtered=work_->smear_state.tick(v,work_->smear_coefficients);
                    if(warm)++work_->filter_warm;
                    else {work_->tape[1-current_][i]=trackr::fx::sat_q15(filtered);
                        if(++work_->filter_at==window_.span){work_->filter_at=work_->filter_warm=0;work_->smear_state.reset();job_=8;}}
                }else if(job_==8){
#ifdef SEANCE_ROT_TEST
                    ++work_->filter_ticks;++work_->frame_filter_ticks;work_->max_filter_ticks=std::max(work_->max_filter_ticks,work_->frame_filter_ticks);
                    ++work_->tape_reads;++work_->frame_tape_reads;work_->max_tape_reads=std::max(work_->max_tape_reads,work_->frame_tape_reads);
#endif
                    const bool warm=work_->filter_warm<warm_frames;
                    const uint32_t i=warm?warm_frames-1-work_->filter_warm:window_.span-1-work_->filter_at;
                    const int32_t filtered=work_->smear_state.tick(work_->tape[1-current_][i],work_->smear_coefficients);
                    if(warm)++work_->filter_warm;
                    else {work_->tape[1-current_][i]=trackr::fx::sat_q15(filtered);
                        if(++work_->filter_at==window_.span)job_=3;}
                }else if(job_==3)write_frame();else break;
            }
            if(job_==4){for(int k=0;k<4 && rng_debt_;++k,--rng_debt_){if(rng_debt_>hits_)rng_.integer(2);else rng_.uniform53();}if(!rng_debt_){if(!healing_)rng_.uniform53();next_ready_=true;job_=6;}}
            ++phase_;if(phase_>=window_.span){
                // Preparation debt extends a real pass, never advertises a future tape.
                if(next_ready_){phase_=0;current_=1-current_;++pass_;++serial_;drop_=0;crossfade_total_=uint16_t(std::min<uint32_t>(1600,std::max<uint32_t>(1,window_.span/8)));crossfade_left_=crossfade_total_;
                    for(uint32_t c=0;c<COLUMNS;++c)publish_column(c); // fixed 9KiB publication, no PCM traversal
                    auto& a=work_->art;for(int h=HISTORY-1;h>0;--h)a.pass[h]=a.pass[h-1];a.pass[0]=pass_;a.history=uint8_t(std::min<int>(HISTORY,a.history+1));a.serial=serial_;
                    published_.rot_mean=uint16_t((mean_sum_/cells_)*32767/1073741824);published_.crackles=hits_;
                    for(int p=P_ROT;p<=P_HEAL;++p)published_.effective[p]=latched_[p];
                    if(refresh_window()){
                        // One bounded restart at an actual edge; repeated slew ticks cannot reset.
                    }else if(config_.law==LAW_CURRENT)begin_pass(); // law 1: no Tail, damage continues past passes until HOLD
                    else if(pass_>=config_.passes){finished_=true;}else begin_pass();
                }else phase_=window_.span-1;
            }
        }
        if(drop_<ENGINE_SR/10){++drop_;}fault_|=rng_.failed;
        if(released_ && !release_left_){std::fill(out+n+1,out+frames,q15(0));break;}
    }
}
}
