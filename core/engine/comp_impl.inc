// Included once by units.cpp: keeps legacy explicit source lists link-compatible.
// No allocation, locks, transcendental functions or block-maximum detection in process().
namespace seance {
namespace {
struct CompLut {
    float log[257]; // 20 log10(1 + i/256), normalized IEEE float mantissa
    float gain[193]; // 10^(-dB/20), 0..96dB at 0.5dB spacing
    CompLut() {
        for (int i=0; i<=256; ++i) log[i] = 20.f * std::log10(1.f + float(i)/256.f);
        for (int i=0; i<=192; ++i) gain[i] = std::pow(10.f, -float(i)/40.f);
    }
};
// Shared startup initialization, not a guarded local static on the audio path.
const CompLut comp_lut;
float comp_normal(q15 v) { return float(std::max(0, int(v))) * (1.f/32767.f); }
float comp_db(float amplitude) {
    if (amplitude <= 0.000015848932f) return -96.f;
    uint32_t bits;
    static_assert(sizeof(bits)==sizeof(amplitude), "32-bit float required");
    std::memcpy(&bits, &amplitude, sizeof(bits));
    const int exponent = int((bits >> 23) & 255) - 127;
    const unsigned index = (bits >> 15) & 255;
    const float fraction = float(bits & 32767u) * (1.f/32768.f);
    return float(exponent)*6.020599913f + comp_lut.log[index] +
        (comp_lut.log[index+1]-comp_lut.log[index])*fraction;
}
float comp_gain(float reduction) {
    const float position = std::max(0.f, std::min(96.f, reduction))*2.f;
    const int index = int(position);
    if (index>=192) return comp_lut.gain[192];
    return comp_lut.gain[index] + (comp_lut.gain[index+1]-comp_lut.gain[index])*(position-index);
}
float comp_tau(float ms) {
    // Store 1-exp(-1/tau), not exp itself: long sync taus retain precision.
    return -std::expm1(-1000.f / (float(ENGINE_SR)*ms));
}
}

float CompUnit::threshold_db(q15 v) { return -48.f + 48.f*comp_normal(v); }
float CompUnit::ratio(q15 v) { return 1.f + 19.f*comp_normal(v); }
float CompUnit::attack_ms(q15 v) { return .1f*std::exp(6.907755279f*comp_normal(v)); }
int CompUnit::release_index(q15 v) { return std::max(0,int(v))*RELEASE_DIVISIONS/32768; }
q15 CompUnit::release_value(int index) {
    index=std::max(0,std::min(RELEASE_DIVISIONS-1,index));
    return q15(index*(32768/RELEASE_DIVISIONS) + 16384/RELEASE_DIVISIONS);
}
const char* CompUnit::release_label(q15 v) {
    static const char* const labels[RELEASE_DIVISIONS] = {"1/64","1/32","1/16","1/8","1/4","1/2","1/1","2/1"};
    return labels[release_index(v)];
}
float CompUnit::release_ms(q15 v, bool sync, int32_t bpm_q16) {
    if (!sync) return 20.f*std::exp(4.605170186f*comp_normal(v));
    const float bpm=float(std::max<int32_t>(20*65536,std::min<int32_t>(300*65536,bpm_q16)))*(1.f/65536.f);
    // One whole note = four beats. 2/1 at20BPM is24s, not the free2s ceiling.
    return (3750.f*float(1u << release_index(v)))/bpm;
}
float CompUnit::makeup_db(q15 v) {
    const int centered=std::max(0,int(v))-16384;
    return centered*(centered<0 ? 12.f/16384.f : 12.f/16383.f);
}
float CompUnit::knee_db(q15 v) { return 12.f*comp_normal(v); }
float CompUnit::hp_hz(q15 v) {
    if (v<=0) return 0.f;
    return 20.f*std::exp(2.708050201f*float(int(v)-1)*(1.f/32766.f));
}
void CompUnit::Ramp::set(float next, bool snap) {
    if (snap) { value=target=next; step=0.f; remaining=0; }
    else if (next!=target) { target=next; remaining=RAMP_FRAMES; step=(target-value)/RAMP_FRAMES; }
}
float CompUnit::Ramp::advance() {
    if (remaining) {
        if (--remaining) value+=step;
        else value=target; // Exact dry/unity endpoints; no asymptotic ramp tails.
    }
    return value;
}
void CompUnit::Peak::push(float amplitude) {
    if (amplitude>=value) { value=amplitude; hold=ENGINE_SR/10; }
    else if (hold) --hold;
    else {
        value*=0.9999568275f; //12dB/second, measured in audio frames, not process calls.
        if (value<0.000001f) value=0.f;
        if (amplitude>value) { value=amplitude; hold=ENGINE_SR/10; }
    }
}
CompUnit::CompUnit() {
    kind=UnitKind::Comp;
    add_param("THRESHOLD",20479); add_param("RATIO",5174); add_param("ATTACK",21845);
    add_param("RELEASE",12749); add_param("MAKEUP",16384); add_param("MIX",32767);
    add_param("KNEE",16384); add_param("DETECT HP",0); add_param("CLOCK",0);
    reset();
}
void CompUnit::reset() {
    std::fill(cached_,cached_+P_COUNT,q15(-32768)); cached_bpm_=0;
    hp_input_=hp_output_=gr_=bin_max_=0.f;
    std::fill(history_,history_+HISTORY_SIZE,0.f);
    bin_frames_=clip_hold_=0; history_write_=history_count_=0;
    input_peak_={}; output_peak_={}; detector_peak_={};
    audio_started_=false;
    control_tick();
    enabled_.set(bypass ? 0.f : 1.f,true);
}
void CompUnit::control_tick() {
    const auto changed=[&](int p) { return params[p].effective!=cached_[p]; };
    if (changed(P_THRESHOLD)) threshold_=threshold_db(params[P_THRESHOLD].effective);
    if (changed(P_RATIO)) slope_=1.f-1.f/ratio(params[P_RATIO].effective);
    if (changed(P_KNEE) || changed(P_RATIO)) {
        const float knee=knee_db(params[P_KNEE].effective);
        half_knee_=.5f*knee;
        knee_scale_=knee>0.f ? slope_/(2.f*knee) : 0.f;
    }
    if (changed(P_ATTACK)) attack_=comp_tau(attack_ms(params[P_ATTACK].effective));
    if (changed(P_RELEASE) || changed(P_CLOCK) || cached_bpm_!=bpm_q16_) {
        release_=comp_tau(release_ms(params[P_RELEASE].effective,params[P_CLOCK].effective>16383,bpm_q16_));
        cached_bpm_=bpm_q16_;
    }
    if (changed(P_HP)) {
        const float hz=hp_hz(params[P_HP].effective);
        hp_=hz>0.f ? std::exp(-6.283185307f*hz/float(ENGINE_SR)) : 0.f;
    }
    if (changed(P_MIX)) mix_.set(comp_normal(params[P_MIX].effective),!audio_started_);
    if (changed(P_MAKEUP)) makeup_.set(std::pow(10.f,makeup_db(params[P_MAKEUP].effective)/20.f),!audio_started_);
    for (int p=0;p<P_COUNT;++p) cached_[p]=params[p].effective;
}
void CompUnit::set_transport_bpm_q16(int32_t bpm) {
    bpm=std::max<int32_t>(20*65536,std::min<int32_t>(300*65536,bpm));
    if (bpm_q16_==bpm) return;
    bpm_q16_=bpm; control_tick();
}
void CompUnit::process(q15* buf, int frames) {
    if (!buf || frames<=0) return;
    enabled_.set(bypass ? 0.f : 1.f,!audio_started_);
    audio_started_=true;
    for (int i=0;i<frames;++i) {
        const q15 dry=buf[i];
        const float input=float(dry)*(1.f/32768.f);
        hp_output_=hp_*(hp_output_+input-hp_input_); hp_input_=input;
        if (std::fabs(hp_output_)<1e-12f) hp_output_=0.f;
        const float detector=std::fabs(hp_>0.f ? hp_output_ : input);
        const float over=comp_db(detector)-threshold_;
        float desired=0.f;
        if (over>=half_knee_) desired=slope_*over;
        else if (over>-half_knee_) { const float x=over+half_knee_; desired=knee_scale_*x*x; }
        gr_+=(desired-gr_)*(desired>gr_ ? attack_ : release_);
        if (gr_<0.000001f) gr_=0.f;
        const float mix=mix_.advance()*enabled_.advance();
        const float makeup=makeup_.advance();
        bool clipped=false;
        if (mix>0.f) {
            // Widen once; makeup affects wet only, one final Q15 saturation.
            const float output=float(dry)*(1.f+mix*(comp_gain(gr_)*makeup-1.f));
            if (output>32767.f) { buf[i]=32767; clipped=true; }
            else if (output<-32768.f) { buf[i]=-32768; clipped=true; }
            else buf[i]=q15(int32_t(output+(output>=0.f ? .5f : -.5f)));
        } // mix0/bypass endpoints are a bit-exact copy, including -32768.
        input_peak_.push(std::fabs(input));
        detector_peak_.push(detector);
        output_peak_.push(std::fabs(float(buf[i])*(1.f/32768.f)));
        if (clipped) clip_hold_=ENGINE_SR/2;
        else if (clip_hold_) --clip_hold_;
        bin_max_=std::max(bin_max_,gr_);
        if (++bin_frames_==HISTORY_FRAMES) {
            history_[history_write_]=bin_max_;
            history_write_=(history_write_+1)%HISTORY_SIZE;
            if (history_count_<HISTORY_SIZE) ++history_count_;
            bin_frames_=0; bin_max_=0.f;
        }
    }
}
CompUnit::Visual CompUnit::visual() const {
    Visual out;
    out.input_db=comp_db(input_peak_.value); out.output_db=comp_db(output_peak_.value);
    out.detector_db=comp_db(detector_peak_.value); out.gr_db=gr_; out.clip=clip_hold_!=0;
    // A nonempty current partial bin is the last visible point. When full,
    // displace the oldest completed bin; never append an unmeasured zero point.
    const int completed=std::min(int(history_count_),HISTORY_SIZE-(bin_frames_ ? 1 : 0));
    const int start=(history_write_+HISTORY_SIZE-completed)%HISTORY_SIZE;
    for (int i=0;i<completed;++i) out.history[i]=history_[(start+i)%HISTORY_SIZE];
    out.history_count=uint8_t(completed);
    if (bin_frames_) out.history[out.history_count++]=bin_max_;
    return out;
}
} // namespace seance
