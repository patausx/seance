// Included once by units.cpp: no additional compilation unit/source-list churn.
// process(): fixed state, sample-time ramps; no heap, locks or transcendental math.
namespace seance {
namespace {
float mouth_normal(q15 value) { return float(std::max(0,int(value)))*(1.f/32767.f); }
const q15 mouth_defaults[MouthUnit::P_COUNT] = {2785,16384,20316,32767,16384};
// Equal vowel segments A/E/I/O/U; no wrap from U to A.
const float mouth_hz[5][3] = {
    {730.f,1090.f,2440.f}, {530.f,1840.f,2480.f}, {270.f,2290.f,3010.f},
    {570.f,840.f,2410.f}, {300.f,870.f,2240.f}
};
template<class F> void mouth_fields(MouthUnit::Model& out,const MouthUnit::Model& a,
                                    const MouthUnit::Model& b,F fn) {
    for (int i=0;i<3;++i) {
        out.hz[i]=fn(a.hz[i],b.hz[i]); out.b0[i]=fn(a.b0[i],b.b0[i]);
        out.b2[i]=fn(a.b2[i],b.b2[i]); out.a1[i]=fn(a.a1[i],b.a1[i]);
        out.a2[i]=fn(a.a2[i],b.a2[i]);
    }
    out.wet_gain=fn(a.wet_gain,b.wet_gain); out.mix=fn(a.mix,b.mix);
    out.output_gain=fn(a.output_gain,b.output_gain);
}
}

float MouthUnit::size_ratio(q15 value) {
    const int centered=std::max(0,int(value))-16384;
    return std::exp2(float(centered)*(centered<0 ? 1.f/16384.f : 1.f/16383.f));
}
float MouthUnit::resonance_q(q15 value) { return 2.f+10.f*mouth_normal(value); }
float MouthUnit::output_db(q15 value) {
    const int centered=std::max(0,int(value))-16384;
    return float(centered)*(centered<0 ? 18.f/16384.f : 12.f/16383.f);
}
MouthUnit::Model MouthUnit::model_from_values(const q15* values) {
    if (!values) values=mouth_defaults;
    Model result;
    const float position=4.f*mouth_normal(values[P_VOWEL]);
    const int segment=std::min(3,int(position));
    const float fraction=position-float(segment);
    const float throat=size_ratio(values[P_SIZE]),q=resonance_q(values[P_RESONANCE]);
    const float weights[3]={1.f,.8f,.6f};
    for (int i=0;i<3;++i) {
        const float first=mouth_hz[segment][i],last=mouth_hz[segment+1][i];
        result.hz[i]=(fraction<=0.f ? first : fraction>=1.f ? last :
            first*std::pow(last/first,fraction))/throat;
        // RBJ constant-peak bandpass, weighted in its numerator. All legal centers
        // are 135..6020 Hz at 32 kHz. Linear denominator ramps stay Schur-stable.
        const float w=6.283185307179586f*result.hz[i]/float(ENGINE_SR);
        const float alpha=std::sin(w)/(2.f*q),inverse=1.f/(1.f+alpha);
        result.b0[i]=weights[i]*alpha*inverse;
        result.b2[i]=-result.b0[i];
        result.a1[i]=-2.f*std::cos(w)*inverse;
        result.a2[i]=(1.f-alpha)*inverse;
    }
    // Bounded bandwidth compensation, not a limiter or loudness normalizer.
    result.wet_gain=.65f*std::sqrt(q*.5f); // .65 .. 1.5922, no reciprocal-Q boost
    result.mix=mouth_normal(values[P_MIX]);
    result.output_gain=std::pow(10.f,output_db(values[P_OUT])*.05f);
    return result;
}
float MouthUnit::response_db(const Model& model,float hz) {
    if (model.mix<=0.f) return 0.f; // wet-only trim cannot change the dry endpoint
    if (!std::isfinite(hz)) return -96.f;
    hz=std::max(0.f,std::min(float(ENGINE_SR)*.5f,hz));
    const float w=6.283185307179586f*hz/float(ENGINE_SR);
    const float c=std::cos(w),s=std::sin(w),c2=std::cos(2.f*w),s2=std::sin(2.f*w);
    float real=0.f,imag=0.f;
    for (int i=0;i<3;++i) {
        const float nr=model.b0[i]+model.b2[i]*c2,ni=-model.b2[i]*s2;
        const float dr=1.f+model.a1[i]*c+model.a2[i]*c2;
        const float di=-model.a1[i]*s-model.a2[i]*s2,den=dr*dr+di*di;
        if (den>0.f) { real+=(nr*dr+ni*di)/den; imag+=(ni*dr-nr*di)/den; }
    }
    const float wet=model.mix*model.wet_gain*model.output_gain;
    real=(1.f-model.mix)+wet*real; imag*=wet;
    return 10.f*std::log10(std::max(2.511886432e-10f,real*real+imag*imag)); // -96 dB floor
}
MouthUnit::MouthUnit() {
    kind=UnitKind::Mouth;
    add_param("VOWEL",mouth_defaults[P_VOWEL]); add_param("SIZE",mouth_defaults[P_SIZE]);
    add_param("RESONANCE",mouth_defaults[P_RESONANCE]); add_param("MIX",mouth_defaults[P_MIX]);
    add_param("OUT",mouth_defaults[P_OUT]); reset();
}
void MouthUnit::retarget(const Model& next,bool snap) {
    target_=next;
    if (snap) { current_=target_; remaining_=0; return; }
    mouth_fields(step_,target_,current_,[](float a,float b) { return (a-b)/float(RAMP_FRAMES); });
    remaining_=RAMP_FRAMES;
}
void MouthUnit::advance() {
    if (remaining_) {
        if (--remaining_) mouth_fields(current_,current_,step_,[](float a,float b) { return a+b; });
        else current_=target_;
    }
    if (enabled_remaining_) {
        if (--enabled_remaining_) enabled_+=enabled_step_;
        else enabled_=enabled_target_;
    }
}
void MouthUnit::reset() {
    input1_=input2_=0.f;
    std::fill(y1_,y1_+3,0.f); std::fill(y2_,y2_+3,0.f);
    audio_started_=false;
    enabled_=enabled_target_=bypass ? 0.f : 1.f;
    enabled_step_=0.f; enabled_remaining_=0;
    // Avoid an invalid-cache sentinel collision when a malformed signed input is
    // clamped to zero: build from every effective value unconditionally on reset.
    for (int p=0;p<P_COUNT;++p) cached_[p]=params[p].effective;
    retarget(model_from_values(cached_),true);
}
void MouthUnit::control_tick() {
    bool changed=false;
    for (int p=0;p<P_COUNT;++p) {
        changed|=cached_[p]!=params[p].effective;
        cached_[p]=params[p].effective;
    }
    if (changed) retarget(model_from_values(cached_),!audio_started_);
}
void MouthUnit::process(q15* buf,int frames) {
    if (!buf || frames<=0) return;
    if (!audio_started_) control_tick(); // restored/edited params snap before first audio
    const float next_enabled=bypass ? 0.f : 1.f;
    if (!audio_started_) enabled_=enabled_target_=next_enabled;
    else if (next_enabled!=enabled_target_) {
        enabled_target_=next_enabled; enabled_remaining_=RAMP_FRAMES;
        enabled_step_=(enabled_target_-enabled_)/float(RAMP_FRAMES);
    }
    audio_started_=true;
    for (int n=0;n<frames;++n) {
        advance();
        const q15 dry=buf[n];
        const float input=float(dry)*(1.f/32768.f),difference=input-input2_;
        input2_=input1_; input1_=input;
        float sum=0.f;
        for (int i=0;i<3;++i) {
            // Direct form I: zero DC numerator survives coefficient motion exactly.
            float y=current_.b0[i]*difference-current_.a1[i]*y1_[i]-current_.a2[i]*y2_[i];
            // Defensive finite state ceiling under adversarial rapid retargets.
            // It is well above normal bandpass levels; final output saturates once.
            y=std::max(-16.f,std::min(16.f,y));
            if (std::fabs(y)<1.e-18f) y=0.f;
            y2_[i]=y1_[i]; y1_[i]=y; sum+=y;
        }
        const float mix=current_.mix*enabled_;
        if (mix<=0.f) continue; // exact dry including -32768; filters stay warm
        const float wet=sum*current_.wet_gain*current_.output_gain;
        const float output=(input+mix*(wet-input))*32768.f;
        if (output>=32767.f) buf[n]=32767;
        else if (output<=-32768.f) buf[n]=-32768;
        else buf[n]=q15(int32_t(output+(output>=0.f ? .5f : -.5f)));
    }
}
MouthUnit::Model MouthUnit::visual_model() const {
    Model result=current_;
    result.mix*=enabled_;
    return result;
}
} // namespace seance
