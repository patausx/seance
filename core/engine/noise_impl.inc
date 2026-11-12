// Included inside namespace seance by units.cpp. No heap or audio-rate libm.
// Modern sound is opt-in for old cards; the original eight-param path is frozen.
namespace noise_detail {
constexpr float pi=3.14159265358979323846f;
uint32_t map(q15 v,const uint32_t (&table)[33],bool zero_off=false) {
    uint32_t value=uint32_t(std::max(0,int(v))),span=32767;
    if(zero_off){if(!value)return 0;--value;--span;}
    const uint32_t scaled=value*32u,index=scaled/span,rest=scaled%span;
    if(index>=32)return table[32];
    return table[index]+uint32_t((uint64_t(table[index+1]-table[index])*rest+span/2)/span);
}
uint32_t random(uint32_t& state) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
float white(uint32_t& state) {return float(int32_t(random(state)>>8)-8388608)*(1.f/8388608.f);}
float q(q15 value){const float r=float(std::max(0,int(value)))/32767.f;return .55f+7.f*r*r;}
float cutoff(q15 value,int cents){return std::max(25.f,std::min(14000.f,float(NoiseUnit::tone_millihz(value))*.001f*std::exp2(float(cents)/1200.f)));}
float bounded(float value){return std::max(-16.f,std::min(16.f,value));}
}
uint32_t NoiseUnit::grain_millihz(q15 v) {
    static constexpr uint32_t t[33]={30000,37303,46385,57677,71717,89177,110886,137880,171446,213184,265082,329614,409857,509633,633700,787970,979796,1218320,1514912,1883707,2342282,2912494,3621521,4503155,5599417,6962556,8657542,10765160,13385863,16644559,20696561,25734995,32000000};
    return noise_detail::map(v,t);
}
uint32_t NoiseUnit::dust_millihz(q15 v) {
    static constexpr uint32_t t[33]={100,142,203,288,410,584,830,1182,1682,2393,3406,4847,6897,9815,13967,19876,28284,40250,57278,81510,115993,165064,234895,334269,475683,676923,963298,1370826,1950760,2776037,3950453,5621710,8000000};
    return noise_detail::map(v,t,true);
}
uint32_t NoiseUnit::tone_millihz(q15 v) {
    static constexpr uint32_t t[33]={25000,30466,37128,45246,55140,67196,81889,99794,121615,148207,180613,220105,268232,326882,398357,485460,591608,720966,878609,1070722,1304841,1590151,1937846,2361566,2877935,3507211,4274081,5208632,6347527,7735447,9426843,11488072,14000000};
    return noise_detail::map(v,t);
}
uint32_t NoiseUnit::body_millihz(q15 v) {
    static constexpr uint32_t t[33]={20000,23902,28566,34140,40801,48762,58276,69647,83236,99476,118886,142082,169805,202937,242533,289855,346410,414000,494778,591318,706693,844580,1009371,1206315,1441687,1722983,2059164,2460940,2941109,3514966,4200792,5020433,6000000};
    return noise_detail::map(v,t,true);
}
uint32_t NoiseUnit::decay_frames(q15 v) {
    static constexpr uint32_t t[33]={160,203,257,326,414,525,665,844,1070,1357,1721,2182,2767,3509,4450,5643,7155,9074,11507,14592,18504,23465,29756,37734,47851,60681,76950,97581,123743,156920,198992,252344,320000};
    return noise_detail::map(v,t);
}
uint32_t NoiseUnit::sweep_frames(q15 v) {
    static constexpr uint32_t t[33]={32,42,54,71,93,121,158,206,269,351,458,598,780,1018,1329,1734,2263,2953,3853,5028,6562,8562,11174,14581,19027,24830,32401,42282,55176,72002,93958,122610,160000};
    return noise_detail::map(v,t);
}
uint32_t NoiseUnit::event_frames(q15 v) {
    static constexpr uint32_t t[33]={32,38,45,54,64,76,90,107,127,151,180,214,254,302,358,426,506,601,714,849,1009,1199,1425,1693,2012,2391,2841,3376,4012,4767,5665,6732,8000};
    return noise_detail::map(v,t);
}
int NoiseUnit::sweep_cents(q15 v) {return std::max(-32767,int(v))*2400/32767;}
q15 NoiseUnit::control_value(int p) const {
    if(p<0||p>=num_params)return 0;
    const Param& a=params[p];
    if(!modern_)return a.effective;
    // Resolve's integer slew can stop 1..3 counts short: meaningful zero/off
    // endpoints and categorical selection must actually reach their targets.
    if(p==P_MODE||p==P_COLOUR||std::abs(int(a.target)-int(a.effective))<=3)return a.target;
    return a.effective;
}
float NoiseUnit::model_response_db(float hz,q15 tone,q15 body,q15 resonance,int cents) {
    // Exact digital transfer of the linear stages used below (not source PSD).
    hz=std::max(.01f,std::min(15999.f,hz));
    const float g=std::tan(noise_detail::pi*noise_detail::cutoff(tone,cents)/float(ENGINE_SR));
    const float w=std::tan(noise_detail::pi*hz/float(ENGINE_SR))/g;
    const float real=1.f-w*w,imag=w/noise_detail::q(resonance);
    float magnitude=1.f/std::sqrt(real*real+imag*imag);
    const uint32_t mhz=body_millihz(body);
    if(mhz){
        const float a=1.f/(1.f+2.f*noise_detail::pi*float(mhz)*.001f/float(ENGINE_SR));
        const float s=std::sin(noise_detail::pi*hz/float(ENGINE_SR));
        magnitude*=2.f*a*s/std::sqrt((1.f-a)*(1.f-a)+4.f*a*s*s);
    }
    return 20.f*std::log10(std::max(1e-8f,magnitude));
}
float NoiseUnit::model_envelope(uint32_t elapsed,uint32_t attack,uint32_t decay,bool drone) {
    if(elapsed<attack)return float(elapsed)/float(attack);
    if(drone)return 1.f;
    if(!decay||elapsed-attack>=decay)return 0.f;
    const float u=1.f-float(elapsed-attack)/float(decay);return u*u*u;
}
void NoiseUnit::set_modern(bool enabled) {
    if(enabled==modern_)return;
    const q15 previous=last_out_;const bool was_processed=processed_;
    modern_=enabled;num_params=enabled?P_COUNT:LEGACY_COUNT;
    params[P_SWEEP].bipolar=enabled;
    if(enabled)modern_reset();else reset();
    if(was_processed){transition_from_=previous;transition_left_=128;}
}
void NoiseUnit::modern_reset() {
    random_=0x9E3779B9u;grain_phase_=0;grain_=noise_detail::white(random_);
    for(float& state:pink_state_)state=0;
    event_amplitude_=0;event_left_=0;event_clock_=0;
    filter_ic1_=filter_ic2_=hp_input_=hp_output_=0;
    applied_cutoff_=applied_q_=applied_body_=-1;
    filter_tone_=filter_body_=filter_res_=-1;filter_cents_=32767;
    modern_started_=false;modern_env_=0;env_stage_=0;env_total_=env_left_=env_phase_=env_error_=0;
    sweep_left_=0;filter_clock_=0;envelope_=0;visual_activity_=0;
    processed_=false;last_out_=transition_from_=0;transition_left_=0;
    modern_control();
}
void NoiseUnit::begin_segment(uint8_t stage,uint32_t frames) {
    env_stage_=stage;env_total_=env_left_=std::max<uint32_t>(1,frames);
    env_phase_=env_error_=0;env_start_=modern_env_;
    env_step_=uint32_t((1ull<<32)/env_total_);env_rem_=uint32_t((1ull<<32)%env_total_);
}
void NoiseUnit::modern_trigger() {
    modern_started_=true;
    modern_drone_=drone_mode(control_value(P_MODE),control_value(P_DECAY));
    latched_decay_=decay_frames(control_value(P_DECAY));
    const uint32_t attack=attack_frames(control_value(P_ATTACK));
    if(attack)begin_segment(1,attack);
    else {modern_env_=1.f;if(modern_drone_)env_stage_=3;else begin_segment(2,latched_decay_);}
    // The timbral contour is hit-only. Neither modulation nor retrigger changes
    // the authored base. Soft attack retrigger starts from the current level.
    sweep_total_=sweep_frames(control_value(P_SWEEP_TIME));
    sweep_amount_=modern_drone_?0:sweep_cents(control_value(P_SWEEP));
    sweep_left_=sweep_amount_?sweep_total_:0;
    filter_clock_=0;update_filter();
    if(!modern_drone_ && int(control_value(P_COLOUR))*4/32768==int(Colour::Crackle) && dust_threshold_){
        // One real initial event makes sparse percussion triggerable. Density0
        // remains silence; subsequent events are free stochastic dust.
        event_amplitude_=noise_detail::white(random_);event_left_=event_clock_=event_total_;
    }
    envelope_=q15(std::min(32767,int(modern_env_*32767.f)));
}
void NoiseUnit::advance_envelope() {
    if(env_stage_!=1&&env_stage_!=2)return;
    if(--env_left_==0){
        if(env_stage_==1){modern_env_=1.f;if(modern_drone_)env_stage_=3;else begin_segment(2,latched_decay_);}
        else {modern_env_=0;env_stage_=0;}
    } else {
        env_phase_+=env_step_;env_error_+=env_rem_;
        if(env_error_>=env_total_){env_error_-=env_total_;++env_phase_;}
        const float phase=float(env_phase_)*(1.f/4294967296.f);
        if(env_stage_==1)modern_env_=env_start_+(1.f-env_start_)*phase;
        else {const float left=1.f-phase;modern_env_=env_start_*left*left*left;}
    }
}
void NoiseUnit::update_filter() {
    const int cents=sweep_left_?int(int64_t(sweep_amount_)*sweep_left_/sweep_total_):0;
    const q15 tone=control_value(P_TONE),body=control_value(P_BODY),res=control_value(P_RESONANCE);
    if(tone==filter_tone_&&body==filter_body_&&res==filter_res_&&cents==filter_cents_)return;
    filter_tone_=tone;filter_body_=body;filter_res_=res;filter_cents_=cents;
    const float cutoff=noise_detail::cutoff(tone,cents),q=noise_detail::q(res);
    if(cutoff!=applied_cutoff_||q!=applied_q_){
        const float g=std::tan(noise_detail::pi*cutoff/float(ENGINE_SR));
        filter_k_=1.f/q;filter_a1_=1.f/(1.f+g*(g+filter_k_));
        filter_a2_=g*filter_a1_;filter_a3_=g*filter_a2_;
        applied_cutoff_=cutoff;applied_q_=q;
    }
    const float high=float(body_millihz(body))*.001f;
    if(high!=applied_body_){hp_alpha_=1.f/(1.f+2.f*noise_detail::pi*high/float(ENGINE_SR));applied_body_=high;}
}
void NoiseUnit::modern_control() {
    const uint32_t rate=grain_millihz(control_value(P_DENSITY));full_rate_=rate==32000000;
    grain_increment_=uint32_t((uint64_t(rate)<<32)/32000000u);
    dust_threshold_=uint32_t((uint64_t(dust_millihz(control_value(P_DENSITY)))<<32)/32000000u);
    event_total_=event_frames(control_value(P_EVENT_LENGTH));
    const bool drone=drone_mode(control_value(P_MODE),control_value(P_DECAY));
    if(!modern_started_){modern_started_=true;modern_drone_=drone;modern_env_=0;if(drone)modern_trigger();}
    else if(drone!=modern_drone_){
        modern_drone_=drone;
        if(drone)modern_trigger();
        else {latched_decay_=decay_frames(control_value(P_DECAY));begin_segment(2,latched_decay_);}
    }
    update_filter();
}
void NoiseUnit::modern_process(q15* buf,int frames) {
    if(!modern_started_)modern_control();
    const int colour=std::min(3,int(control_value(P_COLOUR))*4/32768);
    const float gain=float(control_value(P_GAIN))/32767.f;
    int peak=0;
    int i=0;
    while(i<frames){
        // Render only to the exact retained sweep-clock/end edge. Coefficients
        // are constant in this span; libm work happens after the inner loop,
        // at most once per control edge, never for a future unused curve.
        const int span=sweep_left_?std::min(frames-i,int(std::min(sweep_left_,uint32_t(32-filter_clock_)))):frames-i;
        const int end=i+span;
        for(;i<end;++i){
        const float white=noise_detail::white(random_);
        const uint32_t before=grain_phase_;grain_phase_+=grain_increment_;
        if(full_rate_||grain_phase_<before)grain_=white;
        float source=grain_;
        if(colour==int(Colour::Pink)){
            // Three bounded poles + white feedthrough (Kellet approximation).
            // Grain-rate feeds the whole network, so pink has no dormant knob.
            pink_state_[0]=.99765f*pink_state_[0]+.099046f*grain_;
            pink_state_[1]=.96300f*pink_state_[1]+.2965164f*grain_;
            pink_state_[2]=.57000f*pink_state_[2]+1.0526913f*grain_;
            source=(pink_state_[0]+pink_state_[1]+pink_state_[2]+.1848f*grain_)*.16f;
        } else if(colour==int(Colour::Crackle)){
            if(noise_detail::random(random_)<dust_threshold_){event_amplitude_=white;event_left_=event_clock_=event_total_;}
            if(event_left_){
                // One bounded pulse, later events replace/retrigger it. No
                // growing voice list and no quantized positive residual/DC.
                const float left=float(event_left_)/float(event_clock_);
                source=event_amplitude_*left*left*left;--event_left_;
            } else source=0;
            if(!dust_threshold_){event_left_=0;source=0;}
        } else if(colour==int(Colour::Metal)){
            source=(white<0?-.62f:.62f)+grain_*.38f;
        }
        if(applied_body_>0){
            const float high=hp_alpha_*(hp_output_+source-hp_input_);
            hp_input_=source;hp_output_=high;source=high;
        } else {hp_input_=source;hp_output_=0;}
        const float v3=source-filter_ic2_;
        const float v1=filter_a1_*filter_ic1_+filter_a2_*v3;
        const float v2=filter_ic2_+filter_a2_*filter_ic1_+filter_a3_*v3;
        filter_ic1_=noise_detail::bounded(2.f*v1-filter_ic1_);
        filter_ic2_=noise_detail::bounded(2.f*v2-filter_ic2_);
        const float output=std::max(-1.f,std::min(1.f,v2*modern_env_*gain));
        int out=int(output*32767.f);
        if(transition_left_){out=(int(transition_from_)*transition_left_+out*(128-transition_left_))/128;--transition_left_;}
        buf[i]=last_out_=q15(out);processed_=true;peak=std::max(peak,std::abs(out));
        advance_envelope();
        }
        if(sweep_left_){
            sweep_left_-=uint32_t(span);filter_clock_+=uint8_t(span);
            // Publish even if this edge is the final sample of a caller block:
            // immediate control/trigger/observation sees the original next state.
            if(filter_clock_>=32||!sweep_left_){filter_clock_=0;update_filter();}
        }
    }
    envelope_=q15(std::max(0,std::min(32767,int(modern_env_*32767.f))));
    visual_activity_=uint16_t(peak);
}
