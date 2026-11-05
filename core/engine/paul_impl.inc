// Original independent spectral pipeline from public Fourier/OLA ideas.
// Not an unmodified/adapted PaulStream; no upstream source or KissFFT imported.
#include "engine.h"
#include "paul_dsp.h"
namespace seance {
struct PaulUnit::Workspace {
    paul_dsp::Complex fft[MAX_WINDOW];
    uint32_t magnitude[MAX_WINDOW/2+1],pitched[MAX_WINDOW/2+1];
    int32_t hop[2][MAX_WINDOW/2],tail[MAX_WINDOW/2];
    Visual metadata[2],job,tail_metadata,analysis; // analysis never includes the preceding OLA tail
#ifdef SEANCE_PAUL_TEST
    uint32_t test_magnitudes[2][MAX_WINDOW/2+1];
#endif
};
size_t PaulUnit::workspace_bytes(){return sizeof(Workspace)
#ifdef SEANCE_PAUL_TEST
    -sizeof(Workspace::test_magnitudes)
#endif
    ;}
#ifdef SEANCE_PAUL_TEST
uint64_t PaulUnit::test_clock_phase() const {return engine_?engine_->clock_phase_:0;}
uint32_t PaulUnit::test_magnitude_bin(uint32_t bin) const {
    return current_>=0 && visual_.spectrum_valid && bin<=visual_.window/2?work_->test_magnitudes[current_][bin]:0;
}
#endif
PaulUnit::PaulUnit():work_(new(std::nothrow) Workspace) {
    kind=UnitKind::Paul;
    add_param("POS",0);add_param("STR",DEFAULT_STRETCH);add_param("WIN",DEFAULT_WINDOW);
    add_param("FRZ",0);add_param("GAIN",32767);add_param("PTCH",0,true);
    add_param("GATE",DEFAULT_GATE);add_param("START",0);add_param("END",32767);add_param("LOOP",32767);
}
PaulUnit::~PaulUnit(){delete work_;}
uint32_t PaulUnit::window_frames(q15 v){return v<8192?2048:v<24576?4096:8192;}
uint32_t PaulUnit::stretch_q16(q15 v){
    static constexpr int anchors[]={0,5461,10922,16384,21845,27306,32767};
    const int raw=std::max(0,int(v));int octave=0;
    while(octave<6 && raw>=anchors[octave+1])++octave;
    if(octave==6)return 64u*65536;
    const uint32_t fraction=uint32_t(raw-anchors[octave])*65536/uint32_t(anchors[octave+1]-anchors[octave]);
    const uint32_t i=fraction>>8,r=fraction&255;
    return (paul_tables::exp2[i]+uint32_t(uint64_t(paul_tables::exp2[i+1]-paul_tables::exp2[i])*r/256))<<octave;
}
int PaulUnit::pitch_cents(q15 v){const int raw=std::max(-32767,int(v));return int((int64_t(raw)*2400+(raw<0?-16383:16383))/32767);}
uint8_t PaulUnit::gate_quarters(q15 v){return uint8_t(1+(std::max(0,int(v))*15+16383)/32767);}
void PaulUnit::set_mode(Mode m){if(m<=Mode::Gate && mode!=m){mode=m;reset();}}
void PaulUnit::set_direction(Direction d){
    if(d>Direction::PingPong)return;
    direction=d;
    if(runtime_direction_!=d){runtime_direction_=d;reverse_leg_=d==Direction::Reverse;}
    // Do not mirror POS, seek, discard audio or restart a running voice. Pending
    // analysis is direction-independent; its consumed hop advances the new leg.
}
PaulUnit::Source PaulUnit::source_binding() const {
    Source s;if(sample_slot<0 || sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return s;
    const auto& b=trackr::synth::SampleBank::instance();const auto& p=b.slot(sample_slot);
    if(p.channels!=1 && p.channels!=2)return s;
    s.slot=sample_slot;s.generation=b.generation(sample_slot);s.frames=p.playable_frames();
    s.data=p.data.data();s.elements=p.data.size();s.channels=p.channels;return s;
}
bool PaulUnit::source_matches() const {
    const auto b=source_binding();return source_.frames && source_.data && source_.slot==b.slot &&
        source_.generation==b.generation && source_.frames==b.frames && source_.data==b.data &&
        source_.elements==b.elements && source_.channels==b.channels;
}
bool PaulUnit::context_allowed() const {
    return !bypass && (!engine_ || (chain_>=0 && slot_>=0 && engine_->chains[int(chain_)].units[int(slot_)]==this && !engine_->chains[int(chain_)].mute));
}
void PaulUnit::reset(){
    source_={};voice_=sequence_gate_=gate_phase_=exhausted_=false;gate_deadline_=UINT64_MAX;
    manual_remaining_=0;stage_=0;current_=-1;pending_=0;output_index_=output_count_=0;
    tail_valid_=false;envelope_=last_out_=transition_from_=0;transition_left_=0;
    scan_=0;scan_remainder_=0;position_raw_=start_raw_=end_raw_=-1;
    seek_serial_=seen_seek_=0;visual_={};runtime_mode_=mode;rng_=0x5041554cu;
    runtime_direction_=direction;reverse_leg_=false;retired_for_hop_=analysis_valid_=false;
    // No O(N) buffer clearing: every used FFT/magnitude/hop entry is overwritten
    // by bounded jobs. Invalid tails/outputs are never read after reset.
}
void PaulUnit::synchronize(){
    if(mode!=runtime_mode_ || !context_allowed() || (voice_ && !source_matches()))reset();
    if(direction!=runtime_direction_)set_direction(direction);
}
void PaulUnit::control_tick(){
    synchronize();
    for(int p=0;p<P_COUNT;++p)if(std::abs(int(params[p].target)-params[p].effective)<=3)params[p].effective=params[p].target;
    window_=window_frames(params[P_WINDOW].effective);stretch_=stretch_q16(params[P_STRETCH].effective);
    gain_=uint32_t(std::max(0,int(params[P_GAIN].effective)));
    frozen_=params[P_FREEZE].effective>16383;loop_=params[P_LOOP].effective>16383;
    // Independent pitch: source scan factor is never multiplied by this ratio.
    // ARP is added ONCE here; the engine owns its amplitude gate separately.
    const int cents=std::max(-4800,std::min(7200,pitch_cents(params[P_PITCH].effective)+100*int(arp_voice.semitones)));
    const int semitone=cents/100-(cents<0 && cents%100!=0),remainder=cents-semitone*100;
    // P_PITCH +/-24 plus ARP -24..48 gives -48..72 semitones. Extend the
    // existing -24..48 LUT by exact octave shifts, without clipping away an
    // otherwise valid second downward octave or adding another pitch layer.
    auto ratio=[](int note){
        if(note<-24){const int octaves=(-24-note+11)/12;return (Unit::arp_ratio_q24(note+12*octaves)>>8)>>octaves;}
        const int octaves=note>48?(note-48+11)/12:0;
        return (Unit::arp_ratio_q24(note-12*octaves)>>8)<<octaves;
    };
    const uint32_t lo=ratio(semitone),hi=ratio(semitone+1);
    ratio_=lo+uint32_t(uint64_t(hi-lo)*unsigned(remainder)/100);
    const auto s=source_binding();
    const q15 pos=std::max<q15>(0,params[P_POSITION].effective),start=std::max<q15>(0,params[P_START].effective),end=std::max<q15>(0,params[P_END].effective);
    region_start_=s.frames?uint32_t(uint64_t(start)*(s.frames-1)/32767):0;
    region_end_=s.frames?uint32_t(uint64_t(end)*s.frames/32767):0;
    region_end_=std::max(region_start_+uint32_t(s.frames>0),std::min(s.frames,region_end_));
    // Only a changed resolved POS/region seeks. Window/stretch/gain/freeze changes
    // and ordinary control ticks never silently reset the accumulated scan.
    if(pos!=position_raw_ || start!=start_raw_ || end!=end_raw_){
        position_raw_=pos;start_raw_=start;end_raw_=end;++seek_serial_;exhausted_=false;
    }
}
bool PaulUnit::begin_voice(){
    control_tick();if(!ready() || !context_allowed() || mode>Mode::Gate)return false;
    const auto s=source_binding();if(!s.frames || !s.data)return false;
    source_=s;voice_=true;tail_valid_=false;stage_=0;current_=-1;pending_=0;output_count_=output_index_=0;visual_={};
    seen_seek_=seek_serial_-1;envelope_=0;exhausted_=false;scan_remainder_=0;
    reverse_leg_=direction==Direction::Reverse;retired_for_hop_=analysis_valid_=false;
    transition_from_=last_out_;transition_left_=last_out_?128:0;return true;
}
void PaulUnit::trigger(){
    synchronize();if(!begin_voice())return;
    sequence_gate_=true;gate_deadline_=UINT64_MAX;
    const uint32_t beat=engine_?engine_->frames_per_beat():16000u;
    manual_remaining_=std::max<uint32_t>(1,beat*gate_quarters(params[P_GATE].target)/16);
}
void PaulUnit::release_sequence_gate(){sequence_gate_=false;manual_remaining_=0;gate_deadline_=UINT64_MAX;}
void PaulUnit::release(){release_sequence_gate();}
int32_t PaulUnit::read(int64_t p) const {
    // Predictable ZERO padding outside latched region; no repeat loop, tiny-source
    // division or wrapping search. Fractional seek interpolates the mono fold.
    if(p<int64_t(job_start_)*65536 || p>=int64_t(job_end_)*65536)return 0;
    const uint32_t a=uint32_t(p>>16),f=uint32_t(p)&65535;
    auto value=[&](uint32_t i){if(i>=job_end_)return int32_t(0);return source_.channels==2?
        (int32_t(source_.data[size_t(i)*2])+source_.data[size_t(i)*2+1])/2:int32_t(source_.data[i]);};
    const int32_t va=value(a),vb=value(a+1);return va+int32_t(int64_t(vb-va)*f/65536);
}
void PaulUnit::start_job(){
    if(!voice_ || exhausted_ || !source_matches())return;
    // A window change retires the previous-size tail at a hop boundary. This
    // rewarms the bounded pipeline, with a128-frame fade through silence rather
    // than wrong-size OLA or a giant synchronous FFT. Seek keeps ONE old tail.
    if(job_n_!=window_)tail_valid_=false;
    job_n_=window_;job_ratio_=ratio_;job_stretch_=stretch_;job_seek_=seek_serial_;
    job_start_=region_start_;job_end_=region_end_;job_frozen_=frozen_;job_loop_=loop_;
    if(seen_seek_!=seek_serial_){
        scan_=int64_t(job_start_)*65536+int64_t(position_raw_)*(job_end_-job_start_-1)*65536/32767;
        scan_remainder_=0;seen_seek_=seek_serial_;reverse_leg_=direction==Direction::Reverse;
    }
    scan_=std::max(int64_t(job_start_)*65536,std::min(scan_,int64_t(job_end_-1)*65536));
    job_position_=scan_;job_terminal_=false;
    // POSITION is the analysis CENTER. Region/tiny sources are zero-padded on
    // either side, so a one-frame source lands at the Hann peak, not its zero.
    job_analysis_start_=job_position_-int64_t(job_n_/2)*65536;
    auto& v=work_->job;v={};v.loaded=v.active=true;v.frozen=job_frozen_;v.sample_slot=source_.slot;
    v.generation=source_.generation;v.frames=source_.frames;v.window=job_n_;v.position_q16=job_position_;
    // Inclusive actual PCM support, including BOTH interpolation endpoints.
    // The consumed hop may also include the immediately preceding OLA tail;
    // metadata unions its latched analysis support below, never pending jobs.
    v.window_start_q16=std::max(int64_t(job_start_)*65536,(job_analysis_start_/65536)*65536);
    v.window_end_q16=std::min(int64_t(job_end_-1)*65536,
        ((job_analysis_start_+int64_t(job_n_-1)*65536+65535)/65536)*65536);
    v.spectrum_valid=true;
    // Exact pre-pitch analysis cache. Source pointer/generation/channels/length
    // changes invalidate it through source_matches/reset; voice restart also
    // invalidates. Region and fractional center are explicit, not inferred from
    // clipped support. Pitch/freeze/direction never change the pre-pitch bins.
    const auto& a=work_->analysis;
    const bool reuse=job_frozen_ && analysis_valid_ && a.window==job_n_ &&
        a.position_q16==job_position_ && analysis_start_==job_start_ && analysis_end_==job_end_;
    if(reuse){
        std::copy(a.magnitudes,a.magnitudes+Visual::spectrum_count,v.magnitudes);
#ifdef SEANCE_PAUL_TEST
        ++cached_jobs_;
#endif
    }else{
        analysis_valid_=false;
#ifdef SEANCE_PAUL_TEST
        ++analysis_jobs_;
#endif
    }
    job_rng_=rng_;index_=0;length_=2;stage_=reuse?4:1;
}
void PaulUnit::job_step(){
    using namespace paul_dsp;auto& w=*work_;const uint32_t h=job_n_/2;
    const uint32_t* win=window(job_n_);
    switch(stage_){
    case 1: // bit-reversed window input, Q8 fractional q15
        w.fft[reverse(index_,job_n_)]={int32_t(int64_t(read(job_analysis_start_+int64_t(index_)*65536))*win[index_]/256),0};
        if(++index_==job_n_){index_=0;stage_=2;}break;
    case 2:case 7:
        butterfly(w.fft,job_n_,length_,index_,stage_==7);
        if(++index_==h){index_=0;length_*=2;if(length_>job_n_){stage_=stage_==2?3:8;length_=2;}}break;
    case 3:{
        const uint32_t m=w.magnitude[index_]=magnitude(w.fft[index_]);
#ifdef SEANCE_PAUL_TEST
        w.test_magnitudes[pending_][index_]=m;
#endif
        static_assert(Visual::spectrum_count==96,"exact spectrum group divisors");
        const uint32_t amp=coherent(m,job_n_,index_!=0 && index_!=h);
        const uint32_t bin=spectrum_group(index_,job_n_);
        w.job.magnitudes[bin]=std::max(w.job.magnitudes[bin],amp);
        if(++index_==h+1){
            w.analysis=w.job;analysis_start_=job_start_;analysis_end_=job_end_;analysis_valid_=true;
            index_=0;stage_=4;
        }break;}
    case 4:
#ifdef SEANCE_PAUL_TEST
        // Bounded per-bin oracle copy also on cache hits; never expose pending bins.
        w.test_magnitudes[pending_][index_]=w.magnitude[index_];
#endif
        w.pitched[index_]=0;if(++index_==h+1){index_=0;stage_=5;}break;
    case 5:{
        // Energy-carrying positive bins are splatted to two destination bins.
        // Unlike inverse lookup this cannot multiply the L1 spectral magnitude
        // bound by a large pitch ratio. Above-Nyquist magnitudes are discarded.
        const uint64_t dest=uint64_t(index_)*job_ratio_;const uint32_t a=uint32_t(dest>>16),f=uint32_t(dest)&65535;
        uint32_t m=w.magnitude[index_];
        // Endpoint bins have one conjugate member, interiors two. Convert that
        // amplitude multiplicity when a bin crosses DC/Nyquist under pitch.
        if(index_==h && a<h)m/=2;
        if(a<=h){
            uint32_t left=uint32_t(uint64_t(m)*(65536-f)/65536),right=m-left;
            if(a==0 && index_!=0)left*=2;
            if(a==h && index_!=h)left*=2;
            w.pitched[a]+=left;
            if(a<h){if(a+1==h && index_!=h)right*=2;w.pitched[a+1]+=right;}
        }
        if(++index_==h+1){index_=0;stage_=6;}break;}
    case 6:{
        rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;
        const uint32_t phase=rng_&8191,m=w.pitched[index_];
        Complex c;
        if(index_==0 || index_==h)c={int32_t(m)*(rng_&8192?1:-1),0};
        else c={mul(int64_t(m)*paul_tables::cosine[phase]),mul(int64_t(m)*paul_tables::cosine[(phase+6144)&8191])};
        w.fft[reverse(index_,job_n_)]=c;
        if(index_!=0 && index_!=h)w.fft[reverse(job_n_-index_,job_n_)]={c.re,-c.im};
        if(++index_==h+1){index_=0;stage_=7;length_=2;}break;}
    case 8:{
        const int32_t first=int32_t(int64_t(w.fft[index_].re)*win[index_]/65536);
        const int32_t old=tail_valid_?w.tail[index_]:0;
        w.hop[pending_][index_]=int32_t((int64_t(first)+old)*normalization(job_n_)[index_]/(int64_t(1)<<30));
        w.tail[index_]=int32_t(int64_t(w.fft[index_+h].re)*win[index_+h]/65536);
        if(++index_==h){
            w.metadata[pending_]=w.job;
            if(tail_valid_){auto& v=w.metadata[pending_];v.window_start_q16=std::min(v.window_start_q16,w.tail_metadata.window_start_q16);
                v.window_end_q16=std::max(v.window_end_q16,w.tail_metadata.window_end_q16);}
            w.tail_metadata=w.job;tail_valid_=true;stage_=9;index_=0;
        }break;}
    default:break;
    }
}
void PaulUnit::consume_hop(){
    current_=pending_;pending_^=1;output_count_=job_n_/2;output_index_=0;stage_=0;retired_for_hop_=false;
    if(visual_.window!=job_n_)envelope_=0;
    visual_=work_->metadata[current_];visual_.serial=++serial_;
    // Advance only the analysis belonging to the hop actually entering audio.
    // Fractional division remainder prevents drift at non-power-of-two stretch.
    if(!job_frozen_){
        const uint64_t numerator=(uint64_t(output_count_)<<32)+scan_remainder_;
        const int64_t advance=int64_t(numerator/job_stretch_);scan_remainder_=uint32_t(numerator%job_stretch_);
        const int64_t end=int64_t(job_end_)*65536,start=int64_t(job_start_)*65536;
        if(runtime_direction_==Direction::PingPong){
            // Reflect between actual first/last frame CENTERS, not the padded
            // exclusive end. Modulo a double span handles arbitrarily many
            // reflections in tiny regions in O(1), including exact endpoints.
            const int64_t span=end-start-65536;
            const int64_t relative=job_position_-start;
            if(!job_loop_){
                scan_=job_position_+(reverse_leg_?-advance:advance);
                if(reverse_leg_?scan_<=start:scan_>=start+span)exhausted_=true;
            }else if(!span){scan_=start;}else{
                const int64_t phase=((reverse_leg_?2*span-relative:relative)+advance)%(2*span);
                scan_=start+(phase<=span?phase:2*span-phase);
                reverse_leg_=phase>=span; // upper endpoint leaves in reverse; lower leaves forward
            }
        }else if(runtime_direction_==Direction::Reverse){
            scan_=job_position_-advance;
            if(scan_<start){
                if(job_loop_){const int64_t span=end-start,offset=(start-scan_)%span;scan_=offset?end-offset:start;}
                else exhausted_=true;
            }
        }else{
            // Keep the original forward half-open wrapping and its exact DDA.
            scan_=job_position_+advance;
            if(scan_>=end){if(job_loop_)scan_=start+(scan_-start)%(end-start);else exhausted_=true;}
        }
    }
    // A terminal STALE hop must not defeat a newer explicit seek that already
    // rearmed the voice. It still enters audio with its own tail/metadata; the
    // next bounded job resolves the latest POS/region and resets the leg.
    if(job_seek_!=seek_serial_)exhausted_=false;
}
PaulUnit::Visual PaulUnit::visual() const {
    Visual v;const auto s=source_binding();v.loaded=s.frames && s.data;v.sample_slot=s.slot;v.generation=s.generation;v.frames=s.frames;
    if(!voice_ || !context_allowed() || mode!=runtime_mode_ || !source_matches() || !visual_.active)return v;
    const bool deadline=gate_deadline_==UINT64_MAX || !engine_ || (gate_phase_?engine_->clock_phase_:engine_->transport_frame)<gate_deadline_;
    const bool gate=mode==Mode::Flow || (sequence_gate_ && deadline);
    if(!gate || (arp_voice.owner>=0 && !arp_voice.gate && !arp_voice.gain))return v;
    return visual_;
}
void PaulUnit::process(q15* out,int frames){
    if(!out || frames<=0)return;
    synchronize();
    if(!voice_ && !exhausted_ && mode==Mode::Flow)begin_voice();
    if(!voice_ || !ready() || !source_matches() || !context_allowed()){std::fill(out,out+frames,q15(0));return;}
    for(int i=0;i<frames;++i){
        const bool gate=(mode==Mode::Flow || sequence_gate_) && !(exhausted_ && output_index_>=output_count_);
        if(stage_==0 && !exhausted_)start_job();
        // Latest seek may retire ONE safe pending job per consumed-hop epoch.
        // Stage8 overwrites the old tail entry by entry; stage9 owns that new
        // tail already. NEVER cancel either, or reuse a half-overwritten tail.
        // One retirement bounds work under continuous POS/region modulation:
        // the replacement must finish even if it becomes stale again. A full
        // uncached replacement must also fit the remaining current audio hop;
        // otherwise finish the stale job, not introduce a seek-induced underrun.
        // During startup/underrun there is no current audio deadline. Restore
        // unconsumed RNG phases, keeping the first audible new job fresh.
        if(job_seek_!=seek_serial_ && stage_>0 && stage_<8 && !retired_for_hop_){
            const uint32_t logn=31u-unsigned(__builtin_clz(window_)); // nonzero power-of-two window
            const uint32_t items=window_*(logn+3u)+window_/2+4u;
            const uint32_t frames_needed=(items+WORK_PER_SAMPLE-1)/WORK_PER_SAMPLE;
            if(output_index_>=output_count_ || frames_needed<=output_count_-output_index_){
                rng_=job_rng_;stage_=0;retired_for_hop_=true;
#ifdef SEANCE_PAUL_TEST
                ++retired_jobs_;
#endif
                start_job();
            }
        }
        // Fixed scalar job budget PER AUDIO SAMPLE (not per process call/UI tick).
        // N8192: <36 work items/sample averaged per4096-frame hop; budget40.
        // Each work item is one prep/bin/butterfly/OLA entry, stack O(1).
        for(uint32_t j=0;j<WORK_PER_SAMPLE && stage_>0 && stage_<9;++j)job_step();
        if(output_index_>=output_count_ && stage_==9)consume_hop();
        int32_t raw=0;
        if(output_index_<output_count_){raw=work_->hop[current_][output_index_++]/256;envelope_=gate?std::min<int32_t>(32768,envelope_+256):std::max<int32_t>(0,envelope_-256);}
        else {envelope_=std::max<int32_t>(0,envelope_-256);visual_.active=false;}
        int64_t value=int64_t(raw)*gain_*envelope_/(int64_t(1)<<30);
        // Finish the old-size hop through a128-sample taper while the new-size
        // job warms. No wrong-normalization OLA, instantaneous reset, or doubled
        // crossfade signal. The newly consumed size attacks from zero above.
        if(current_>=0 && work_->metadata[current_].window!=job_n_)
            value=value*std::min<uint32_t>(128,output_count_-output_index_)/128;
        if(transition_left_){value+=int64_t(transition_from_)*transition_left_/128;--transition_left_;}
        out[i]=trackr::fx::sat_q15(int32_t(std::max<int64_t>(-2147483647,std::min<int64_t>(2147483647,value))));last_out_=out[i];
        if(manual_remaining_ && !--manual_remaining_)release_sequence_gate();
        if(!gate && !envelope_){voice_=false;stage_=0;tail_valid_=false;visual_={};std::fill(out+i+1,out+frames,q15(0));break;}
    }
}
} // namespace seance
