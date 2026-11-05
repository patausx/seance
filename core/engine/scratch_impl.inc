// Included by units.cpp; standalone fixed-point SCRT, no changes to PLAY.
#include "engine.h"
namespace seance {
namespace scratch_detail {
int32_t clamp_raw(q15 v, bool bipolar=false) { return std::max(bipolar?-32767:0,int(v)); }
int64_t bound(int64_t v,int64_t hi) { return std::max<int64_t>(0,std::min(v,hi)); }
// Signed finite-time power-of-two response, including a final exact Q16 LSB.
int64_t follow(int64_t delta,unsigned shift) {
    const int64_t magnitude=delta<0?-delta:delta;
    const int64_t step=std::min<int64_t>(8*65536,std::max<int64_t>(1,magnitude>>shift));
    return delta==0?0:delta<0?-step:step;
}
}
ScratchUnit::ScratchUnit() {
    kind=UnitKind::Scratch;
    add_param("POS",0); add_param("GAIN",32767); add_param("WIN",DEFAULT_WINDOW);
    add_param("SPD",0,true); add_param("PTCH",0,true); add_param("FOLLOW",8192);
    add_param("GATE",DEFAULT_GATE);
}
uint32_t ScratchUnit::window_frames(q15 v) { return 16u+(uint32_t(scratch_detail::clamp_raw(v))*6384u+16383u)/32767u; }
int32_t ScratchUnit::speed_q16(q15 v) { return int32_t(int64_t(scratch_detail::clamp_raw(v,true))*262144/32767); }
int ScratchUnit::pitch_cents(q15 v) {
    const int32_t raw=scratch_detail::clamp_raw(v,true);
    return int((int64_t(raw)*2400+(raw<0?-16383:16383))/32767);
}
uint8_t ScratchUnit::gate_quarters(q15 v) { return uint8_t(1+(scratch_detail::clamp_raw(v)*15+16383)/32767); }
void ScratchUnit::map_params(Mode mode,Map m,int& x,int& y) {
    x=m==Map::SpeedPitch?P_SPEED:P_POSITION;
    y=mode==Mode::Scratch?P_GAIN:m==Map::PositionGain?P_GAIN:m==Map::PositionWindow?P_WINDOW:P_PITCH;
}
bool ScratchUnit::axis_parameter_available(Mode m,int p) {
    return (m==Mode::Scratch || m==Mode::Hold) && p>=P_POSITION && p<=P_PITCH &&
        (m==Mode::Hold || (p!=P_WINDOW && p!=P_PITCH));
}
void ScratchUnit::axis_params(int& x,int& y) const {
    map_params(mode,map,x,y);
    const int dx=x,dy=y;
    if(axis_parameter_available(mode,axis_x))x=axis_x;
    if(axis_parameter_available(mode,axis_y))y=axis_y;
    // Public persisted fields may be directly restored by a loader. Invalid pairs
    // safely fall back; the mutator refuses them instead of silently changing them.
    if(x==y){x=dx;y=dy;}
}
bool ScratchUnit::set_axis(int axis,int param) {
    if((axis!=0 && axis!=1) || (param!=-1 && !axis_parameter_available(mode,param)))return false;
    int x,y;map_params(mode,map,x,y);
    const int nx=axis==0?param:axis_x,ny=axis==1?param:axis_y;
    if(axis_parameter_available(mode,nx))x=nx;
    if(axis_parameter_available(mode,ny))y=ny;
    if(x==y)return false;
    int8_t& value=axis==0?axis_x:axis_y;
    if(value!=param){value=int8_t(param);reset();}
    return true;
}
void ScratchUnit::set_mode(Mode m) {
    if(m!=Mode::Scratch && m!=Mode::Hold)return;
    const Mode before=mode;const int ax=axis_x,ay=axis_y;mode=m;
    if(axis_x!=-1 && !axis_parameter_available(m,axis_x))axis_x=-1;
    if(axis_y!=-1 && !axis_parameter_available(m,axis_y))axis_y=-1;
    int x,y;map_params(mode,map,x,y);
    if(axis_x!=-1)x=axis_x;
    if(axis_y!=-1)y=axis_y;
    if(x==y)axis_x=axis_y=-1;
    if(before!=m || ax!=axis_x || ay!=axis_y)reset();
}
void ScratchUnit::set_map(Map m) {
    if(m!=Map::PositionGain && m!=Map::PositionWindow && m!=Map::SpeedPitch)return;
    if(map!=m || axis_x!=-1 || axis_y!=-1){map=m;axis_x=axis_y=-1;reset();}
}
ScratchUnit::Source ScratchUnit::source_binding() const {
    Source s;
    if(sample_slot<0 || sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return s;
    const auto& bank=trackr::synth::SampleBank::instance(); const auto& pcm=bank.slot(sample_slot);
    if(pcm.channels!=1 && pcm.channels!=2)return s;
    s.slot=sample_slot;s.generation=bank.generation(sample_slot);s.frames=pcm.playable_frames();
    s.data=pcm.data.data();s.elements=pcm.data.size();s.channels=pcm.channels;
    return s;
}
bool ScratchUnit::source_matches(const Source& a) const {
    const Source b=source_binding();
    return a.slot>=0 && a.frames>0 && a.data && a.slot==b.slot && a.generation==b.generation &&
        a.frames==b.frames && a.data==b.data && a.elements==b.elements && a.channels==b.channels;
}
bool ScratchUnit::context_allowed() const {
    if(bypass)return false;
    return !engine_ || (chain_>=0 && slot_>=0 && engine_->chains[int(chain_)].units[int(slot_)]==this &&
        !engine_->chains[int(chain_)].mute);
}
bool ScratchUnit::configuration_matches() const {
    return mode==runtime_mode_ && map==runtime_map_ && axis_x==runtime_axis_x_ && axis_y==runtime_axis_y_;
}
bool ScratchUnit::latched() const {
    return latch_ && context_allowed() && configuration_matches() && source_matches(source_) &&
        (!engine_ || !engine_->recording_active_or_pending());
}
q15 ScratchUnit::axis_control_source(int p) const {
    const Param& param=params[p];
    if(!latched() || param.temp_override || param.performance_override)return param.control_source();
    int x,y;axis_params(x,y);
    return p==x?latch_x_:p==y?latch_y_:param.control_source();
}
void ScratchUnit::resolve_param(int p,q15 modulation) {
    // SCRT-only virtual performance layer: never writes any shared override bit
    // or value. Foreign temp/performance wins; lower layers keep changing live.
    Param& param=params[p];
    if(!latched() || param.temp_override || param.performance_override){param.resolve(modulation);return;}
    int x,y;axis_params(x,y);
    if(p!=x && p!=y){param.resolve(modulation);return;}
    Param held=param;held.temp_override=true;held.temp=p==x?latch_x_:latch_y_;
    held.resolve(modulation);
    param.target=held.target;param.effective=held.effective;param.slew_ready=held.slew_ready;
}
void ScratchUnit::reset() {
    if(touch_token_)for(int p=0;p<P_COUNT;++p)if(temp_owner_[p]==touch_token_) {
        params[p].temp_override=false;temp_owner_[p]=0;
    }
    if(!++epoch_)++epoch_;
    source_={};touch_token_=0;latch_=false;latch_x_=latch_y_=0;voice_=sequence_gate_=gate_phase_=false;
    gate_deadline_=UINT64_MAX;manual_remaining_=0;center_=target_=last_position_=0;
    phase_=0;phase_increment_=0;target_span_=0;start_[0]=start_[1]=span_[0]=span_[1]=0;
    position_component_=position_offset_=0;
    envelope_=dc_previous_=dc_state_=0;visual_={};runtime_mode_=mode;runtime_map_=map;
    runtime_axis_x_=axis_x;runtime_axis_y_=axis_y;
}
void ScratchUnit::synchronize() {
    if(!configuration_matches() || !context_allowed() || (voice_ && !source_matches(source_)) ||
        (engine_ && engine_->recording_active_or_pending() && (latch_ || (voice_ && !sequence_gate_)))) {
        // A sequence gate is independent: recording cancels controller/latch only.
        if(configuration_matches() && context_allowed() && source_matches(source_) && sequence_gate_) {
            if(touch_token_)for(int p=0;p<P_COUNT;++p)if(temp_owner_[p]==touch_token_){
                params[p].temp_override=false;temp_owner_[p]=0;
            }
            touch_token_=0;latch_=false;
        } else reset();
    }
}
void ScratchUnit::control_tick() {
    synchronize();
    // Local finite Param tail only. Historical Param::resolve and PLAY remain unchanged.
    for(int p=0;p<P_COUNT;++p)if(std::abs(int(params[p].target)-params[p].effective)<=3)
        params[p].effective=params[p].target;
    // Zero speed is an actual stop, not the +/-3-LSB tail of shared one-pole slew.
    if(params[P_SPEED].target==0)params[P_SPEED].effective=0;
    speed_=speed_q16(params[P_SPEED].effective);
    gain_=scratch_detail::clamp_raw(params[P_GAIN].effective);
    window_=window_frames(params[P_WINDOW].effective);
    follow_shift_=uint8_t(4+(uint32_t(scratch_detail::clamp_raw(params[P_FOLLOW].effective))*6u>>15));
    const Source source=source_binding();
    const uint32_t frames=source.frames;
    target_=frames?int64_t(scratch_detail::clamp_raw(params[P_POSITION].effective))*(frames-1)*65536/32767:0;
    // Continuous equal-tempered reader ratio from immutable semitone LUT, at control rate only.
    const uint32_t scaled=uint32_t(scratch_detail::clamp_raw(params[P_PITCH].effective,true)+32767)*48u;
    const int note=int(scaled/65534u)-24; const uint32_t remainder=scaled%65534u;
    const int32_t lo=int32_t(Unit::arp_ratio_q24(note)>>8);
    const int32_t hi=int32_t(Unit::arp_ratio_q24(std::min(24,note+1))>>8);
    ratio_=lo+int32_t(int64_t(hi-lo)*remainder/65534u);
    target_span_=int64_t(frames>1?std::min(window_,frames-1):0)*65536;
    // Pitch <=4x and the smallest nonzero span is one frame: at most four cycles.
    // Keep the full increment (not uint32 truncation) so tiny sources cross both seams.
    phase_increment_=target_span_?std::min<uint64_t>(uint64_t(4)<<32,
        (uint64_t(ratio_)<<32)/uint64_t(target_span_)):0;
}
int64_t ScratchUnit::local_start(int64_t span) const {
    return scratch_detail::bound(center_-span/2,int64_t(source_.frames-1)*65536-span);
}
bool ScratchUnit::begin_voice() {
    synchronize();control_tick();
    if(!context_allowed() || mode>Mode::Hold || map>Map::SpeedPitch)return false;
    Source s=source_binding(); if(!s.frames || !s.data)return false;
    source_=s;voice_=true;center_=target_;last_position_=center_;phase_=0;
    position_component_=target_;position_offset_=0;
    span_[0]=span_[1]=target_span_;start_[0]=start_[1]=local_start(target_span_);
    envelope_=dc_previous_=dc_state_=0;return true;
}
void ScratchUnit::reanchor(q15 position) {
    // Guard-owned initialization, including active HOLD heads: no synthetic audio
    // sweep, no previous envelope/DC/phase history survives the finger press.
    center_=int64_t(scratch_detail::clamp_raw(position))*(source_.frames-1)*65536/32767;
    last_position_=center_;position_component_=target_;position_offset_=center_-target_;
    phase_=0;span_[0]=span_[1]=target_span_;start_[0]=start_[1]=local_start(target_span_);
    envelope_=dc_previous_=dc_state_=0;
    visual_.loaded=true;visual_.active=true;visual_.sample_slot=source_.slot;
    visual_.generation=source_.generation;visual_.frames=source_.frames;
    visual_.position_q16=center_;visual_.speed_q16=0;
    visual_.window=mode==Mode::Hold?uint32_t(target_span_>>16):0;
    visual_.window_start_q16=mode==Mode::Hold?start_[0]:center_;
    visual_.window_end_q16=mode==Mode::Hold?start_[0]+span_[0]:center_;
}
void ScratchUnit::trigger() {
    control_tick();
    // Trigger is finite even outside transport (125ms default at120bpm).
    if(engine_ && engine_->chains[int(chain_)].mute)return;
    if(!voice_ && !begin_voice())return;
    sequence_gate_=true;gate_deadline_=UINT64_MAX;
    const uint32_t beat=engine_?engine_->frames_per_beat():16000u;
    manual_remaining_=std::max<uint32_t>(1,beat*gate_quarters(params[P_GATE].target)/16);
}
void ScratchUnit::release_sequence_gate() { sequence_gate_=false;manual_remaining_=0;gate_deadline_=UINT64_MAX; }
void ScratchUnit::release() { release_sequence_gate(); }
void ScratchUnit::touch_begin(uint32_t token) {
    if(!voice_ && !begin_voice())return;
    touch_token_=token;
}
void ScratchUnit::touch_end(uint32_t token) { if(touch_token_==token)touch_token_=0; }
q15 ScratchUnit::read(int64_t p) const {
    p=scratch_detail::bound(p,int64_t(source_.frames-1)*65536);
    const uint32_t a=uint32_t(p>>16),b=std::min(a+1,source_.frames-1);
    const uint32_t fraction=uint32_t(p)&65535u;
    const q15* data=source_.data;
    const int32_t va=source_.channels==2?(int32_t(data[size_t(a)*2])+data[size_t(a)*2+1])/2:data[a];
    const int32_t vb=source_.channels==2?(int32_t(data[size_t(b)*2])+data[size_t(b)*2+1])/2:data[b];
    return q15(va+((int64_t(vb-va)*fraction)>>16));
}
ScratchUnit::Visual ScratchUnit::visual() const {
    Visual result;const Source s=source_binding();
    result.loaded=s.frames && s.data;result.sample_slot=s.slot;result.generation=s.generation;result.frames=s.frames;
    if(!context_allowed() || !configuration_matches() || !source_matches(source_))return result;
    result=visual_;result.loaded=true;
    const bool sequence=sequence_gate_ && (gate_deadline_==UINT64_MAX || !engine_ ||
        (gate_phase_?engine_->clock_phase_:engine_->transport_frame)<gate_deadline_);
    const bool touch=touch_token_ && (!engine_ || engine_->scratch_gesture_active(touch_token_));
    if(!voice_ || (!sequence && !touch && !latched()))result.active=false;
    if(!result.active){result.position_q16=0;result.speed_q16=0;result.window=0;
        result.window_start_q16=result.window_end_q16=0;}
    return result;
}
void ScratchUnit::process(q15* buf,int frames) {
    if(!buf || frames<=0)return;
    synchronize();
    if(!voice_ || !source_matches(source_) || !context_allowed()) { std::fill(buf,buf+frames,q15(0));return; }
    const int64_t last=int64_t(source_.frames-1)*65536;
    int ax,ay;axis_params(ax,ay);
    // A custom SPD axis must remain playable even from the position preset.
    // Resolve this configuration once per block, never per audio sample.
    const bool speed_motion=map==Map::SpeedPitch || ax==P_SPEED || ay==P_SPEED;
    for(int i=0;i<frames;++i){
        const bool gate=sequence_gate_ || touch_token_ || latch_;
        envelope_=gate?std::min<int32_t>(32768,envelope_+256):std::max<int32_t>(0,envelope_-256); // 128 frames /4ms
        const int64_t before=center_;
        if(speed_motion) {
            // Independent POS displacement plus SPD integration. Only the CHANGE
            // in the finite positional follower contributes; a stationary POS
            // never attracts the speed reader back or integrates twice. Bounding
            // the actual center each sample discards outward edge overshoot.
            const int64_t delta=scratch_detail::follow(target_-position_component_,follow_shift_);
            position_component_+=delta;
            center_=scratch_detail::bound(center_+speed_+delta,last);
        } else {
            const int64_t absolute=scratch_detail::bound(target_+position_offset_,last);
            center_=scratch_detail::bound(center_+scratch_detail::follow(absolute-center_,follow_shift_),last);
        }
        int32_t value=0;
        int64_t window_start=center_,window_end=center_;
        if(mode==Mode::Scratch) {
            const int64_t motion=center_-before;
            if(motion) {
                const int32_t raw=read(center_);
                const int32_t dc=raw-dc_previous_+int32_t((int64_t(dc_state_)*32604)>>15);
                dc_previous_=raw;dc_state_=dc;
                value=int32_t((int64_t(dc)*std::min<int64_t>(65536,motion<0?-motion:motion))>>16);
            } else dc_previous_=dc_state_=0; // stationary scratch is exact digital zero
        } else if(target_span_ && phase_increment_) {
            // Each reader owns its extent until its zero-weight seam. Window/pitch edits
            // only change the shared cycle rate; there is no offset integrator to drift.
            // During a window transition old readers' pitch is scaled by own/target span
            // for at most one target cycle; stable windows retain independent exact pitch.
            const uint32_t half_phase=phase_^0x80000000u;
            const uint32_t weight=phase_<0x80000000u?phase_>>16:(0x100000000ULL-phase_)>>16;
            const int64_t offset0=int64_t((uint64_t(phase_)*uint64_t(span_[0]))>>32);
            const int64_t offset1=int64_t((uint64_t(half_phase)*uint64_t(span_[1]))>>32);
            window_start=std::min(start_[0],start_[1]);
            window_end=std::max(start_[0]+span_[0],start_[1]+span_[1]);
            const int32_t raw=int32_t((int64_t(read(start_[0]+offset0))*weight+
                int64_t(read(start_[1]+offset1))*(32768-weight))>>15);
            value=raw-dc_previous_+int32_t((int64_t(dc_state_)*32604)>>15);dc_previous_=raw;dc_state_=value;
            // Unsigned, unwrapped comparisons detect every crossed seam, including
            // increments >=one full cycle. Multiple crossings latch the same current
            // center/target deterministically; all sums/products stay below 2^62.
            if(uint64_t(phase_)+phase_increment_>=0x100000000ULL) {
                span_[0]=target_span_;start_[0]=local_start(span_[0]);
            }
            if(uint64_t(half_phase)+phase_increment_>=0x100000000ULL) {
                span_[1]=target_span_;start_[1]=local_start(span_[1]);
            }
            phase_=uint32_t(uint64_t(phase_)+phase_increment_);
        }
        buf[i]=trackr::fx::sat_q15(int32_t((int64_t(value)*gain_*envelope_)>>30));
        visual_.loaded=true;visual_.active=gate;visual_.sample_slot=source_.slot;
        visual_.generation=source_.generation;visual_.frames=source_.frames;
        visual_.window=mode==Mode::Hold?uint32_t(target_span_>>16):0;
        // Bounds describe this sample's readers BEFORE phase/seam advancement, with
        // the same sample's center; a control tick alone cannot mix snapshot epochs.
        visual_.window_start_q16=window_start;visual_.window_end_q16=window_end;
        visual_.position_q16=center_;visual_.speed_q16=int32_t(center_-before);
        last_position_=center_;
        if(manual_remaining_ && !--manual_remaining_)release_sequence_gate();
        if(!gate && !envelope_){voice_=false;visual_.active=false;std::fill(buf+i+1,buf+frames,q15(0));break;}
    }
}
} // namespace seance
