// Included by units.cpp inside namespace seance, after the frozen frequency LUT.
// Modern: local2x rate, event BLEP/BLAMP carrier, 47tap anti-imaging FIR. This
// reduces aliasing; nonlinear ring products/bridge/feedback are not alias-free.
namespace ring_detail {
uint32_t uni(q15 v){return uint32_t(std::max(0,int(v)));}
int bi(q15 v){return std::max(-32767,int(v));}
float clip(float x){x=std::max(-3.f,std::min(3.f,x));const float xx=x*x;return x*(27.f+xx)/(27.f+9.f*xx);}
q15 sample(float x){const float v=std::max(-1.f,std::min(32767.f/32768.f,x))*32768.f;return q15(int(v+(v<0?-.5f:.5f)));}
double base_hz(const q15* v){
    const double hz=v[RingUnit::P_MODE]>=16384?double(RingUnit::note_millihz(v[RingUnit::P_NOTE]))/1000.:
        double(ring_base_increment(q15(uni(v[RingUnit::P_FREQ]))))*ENGINE_SR/4294967296.;
    return hz*std::exp2(double(bi(v[RingUnit::P_FINE]))/(32767.*12.));
}
}
RingUnit::RingUnit(bool modern):modern_(modern){
    kind=UnitKind::Ring;
    add_param("FREQ",19000);add_param("DEPTH",24576);add_param("FDBK",0,true);
    add_param("DIODE",0);add_param("MIX",24576);add_param("FOLLOW",0,modern);
    add_param("DRIVE",0);add_param("FTIME",22709);add_param("WAVE",0);
    add_param("NOTE",musical_note::encode(45));add_param("MODE",0);add_param("FINE",0,true);
    num_params=modern?P_COUNT:LEGACY_COUNT;
    // Prime the shared read-only sine table during construction, not lazily in audio.
    if(modern)osc_clean::sine_table();
}
void RingUnit::set_modern(bool enabled){
    params[P_FOLLOW].bipolar=enabled;params[P_FINE].bipolar=true;params[P_FEEDBACK].bipolar=true;
    if(modern_==enabled)return;
    const bool played=processed_;const q15 previous=last_out_;
    modern_=enabled;num_params=enabled?P_COUNT:LEGACY_COUNT;
    reset();
    if(played&&enabled){transition_from_=previous;transition_left_=128;}
}
void RingUnit::reset(){
    phase_=0;feedback_=0;dc_x_=dc_y_=0;envelope_=0;visual_phase_=visual_envelope_=0;
    modern_phase_=0;modern_env_=modern_feedback_=modern_dc_x_=modern_dc_y_=0;
    input_previous_=sub_input_=triangle_=last_carrier_=0;pulse_=1;
    fir_head_=0;std::fill(wet_fir_,wet_fir_+FIR_TAPS,0.f);std::fill(carrier_fir_,carrier_fir_+FIR_TAPS,0.f);
    cache_valid_=processed_=false;transition_left_=0;last_out_=transition_from_=0;
    if(modern_)control_tick();
}
uint32_t RingUnit::frequency_millihz(q15 v){return uint32_t((uint64_t(ring_base_increment(q15(ring_detail::uni(v))))*32000000ull+(1ull<<31))>>32);}
uint32_t RingUnit::note_millihz(q15 v){return uint32_t((440000ull*musical_note::ratio_q16(musical_note::decode(v)-69)+32768)/65536);}
uint32_t RingUnit::follow_time_us(q15 v){
    static constexpr uint32_t knots[33]={1000,1241,1540,1911,2371,2943,3652,4532,5623,6978,8660,10746,13335,16548,20535,25483,31623,39242,48697,60430,74989,93057,115478,143301,177828,220673,273842,339821,421697,523299,649382,805842,1000000};
    const uint32_t scaled=ring_detail::uni(v)*32u,i=scaled/32767u,f=scaled%32767u;
    return i>=32?knots[32]:knots[i]+uint32_t((uint64_t(knots[i+1]-knots[i])*f+16383)/32767);
}
int RingUnit::drive_centidb(q15 v){return int(ring_detail::uni(v)*1800u/32767u);}
int RingUnit::fine_centicents(q15 v){return ring_detail::bi(v)*10000/32767;}
int RingUnit::wave_index(q15 v){return int(ring_detail::uni(v)*3u/32768u);}
uint32_t RingUnit::base_millihz(const q15* v){return uint32_t(ring_detail::base_hz(v)*1000.+.5);}
q15 RingUnit::control_value(int p)const{
    if(p<0||p>=num_params)return 0;
    const auto& a=params[p];
    return modern_&&(p==P_NOTE||p==P_MODE||p==P_WAVE||std::abs(int(a.target)-int(a.effective))<=3)?a.target:a.effective;
}
void RingUnit::control_tick(){
    if(!modern_)return;
    q15 next[P_COUNT];bool changed=!cache_valid_;
    for(int p=0;p<P_COUNT;++p){next[p]=control_value(p);changed|=next[p]!=resolved_[p];}
    if(!changed)return;
    if(!cache_valid_||next[P_FREQ]!=resolved_[P_FREQ]||next[P_NOTE]!=resolved_[P_NOTE]||
       next[P_MODE]!=resolved_[P_MODE]||next[P_FINE]!=resolved_[P_FINE])base_hz_=float(ring_detail::base_hz(next));
    if(!cache_valid_||next[P_FOLLOW_TIME]!=resolved_[P_FOLLOW_TIME])release_=std::exp(-1000000.f/(float(follow_time_us(next[P_FOLLOW_TIME]))*ENGINE_SR));
    if(!cache_valid_||next[P_DRIVE]!=resolved_[P_DRIVE]){drive_=std::pow(10.f,float(drive_centidb(next[P_DRIVE]))/2000.f);trim_=1.f/std::sqrt(drive_);}
    amount_=float(ring_detail::bi(next[P_FOLLOW]))/32767.f;
    depth_=float(ring_detail::uni(next[P_DEPTH]))/32767.f;
    feedback_gain_=.85f*float(ring_detail::bi(next[P_FEEDBACK]))/32767.f*depth_;
    diode_=float(ring_detail::uni(next[P_DIODE]))/32767.f;shape_=wave_index(next[P_WAVE]);
    std::copy(next,next+P_COUNT,resolved_);cache_valid_=true;
}
void RingUnit::process(q15* buf,int frames){process_with_carrier(buf,nullptr,frames);}
void RingUnit::process_with_carrier(q15* buf,q15* carrier,int frames){
    if(!buf||frames<=0)return;
    if(modern_)modern_process(buf,carrier,frames);
    else {
        if(!carrier)legacy_process(buf,frames);
        else for(int i=0;i<frames;++i){
            legacy_process(buf+i,1);
            const int p=visual_phase_,tri=p<32768?p*2-32768:32767-(p-32768)*2;
            carrier[i]=sat_q15(int32_t((int64_t(tri)*(65536-std::abs(tri)))>>15));
        }
        last_out_=buf[frames-1];processed_=true;
    }
}
void RingUnit::modern_process(q15* buf,q15* carrier,int frames){
    control_tick();
    static_assert(FIR_TAPS==osc_clean::TAPS,"shared immutable decimation coefficients");
    if(resolved_[P_DEPTH]==0){
        // Exact immediate dry, no hidden loop. Phase/follower remain paused.
        modern_feedback_=modern_dc_x_=modern_dc_y_=0;visual_envelope_=0;
        std::fill(wet_fir_,wet_fir_+FIR_TAPS,0.f);std::fill(carrier_fir_,carrier_fir_+FIR_TAPS,0.f);
        input_previous_=sub_input_=float(buf[frames-1])/32768.f;
        if(carrier)std::fill(carrier,carrier+frames,ring_detail::sample(last_carrier_));
        last_out_=buf[frames-1];processed_=true;transition_left_=0;return;
    }
    const float mix=float(ring_detail::uni(resolved_[P_MIX]))/32767.f;
    for(int i=0;i<frames;++i){
        const q15 dry=buf[i];const float source=float(dry)/32768.f,magnitude=std::abs(source);
        modern_env_=magnitude>modern_env_?magnitude:magnitude+(modern_env_-magnitude)*release_;
        const float factor=1.f+7.f*modern_env_*std::abs(amount_);
        const float hz=std::min(12000.f,base_hz_*(amount_<0?1.f/factor:factor));
        const uint32_t increment=uint32_t(double(hz)*(4294967296./(ENGINE_SR*2.)));
        const float delta=float(increment)*osc_clean::INV_PHASE;
        int newest=fir_head_;
        for(int sub=0;sub<2;++sub){
            const uint32_t old=modern_phase_;
            const float previous_phase=float(old>>8)*(1.f/16777216.f);
            modern_phase_+=increment;
            const float now=float(modern_phase_>>8)*(1.f/16777216.f);
            float tri=osc_clean::triangle(now),square=now<.5f?1.f:-1.f;
            float previous_tri=triangle_,previous_pulse=pulse_;
            osc_clean::ramp_event(previous_phase-.25f,previous_phase+delta-.25f,-8*delta,previous_tri,tri);
            osc_clean::ramp_event(previous_phase-.75f,previous_phase+delta-.75f,8*delta,previous_tri,tri);
            // Helper jump is AFTER-BEFORE: rising wrap and falling half-cycle.
            osc_clean::step_event(previous_phase,previous_phase+delta,2,previous_pulse,square);
            osc_clean::step_event(previous_phase-.5f,previous_phase+delta-.5f,-2,previous_pulse,square);
            triangle_=tri;pulse_=square;
            const float raw=shape_==0?osc_clean::sine(old):shape_==1?previous_tri:previous_pulse;
            last_carrier_=std::max(-1.f,std::min(1.f,raw));
            const float driven=sub_input_*drive_+modern_feedback_*feedback_gain_;
            const float multiplier=driven*last_carrier_;
            const float bridge=std::abs(driven+last_carrier_)-std::abs(driven-last_carrier_);
            const float core=ring_detail::clip(multiplier+(bridge-multiplier)*diode_)*trim_;
            const float wet=sub_input_+(core-sub_input_)*depth_;
            //8Hz DC blocker at64k, inside return; bounded feedback never explodes.
            modern_dc_y_=wet-modern_dc_x_+.999214910f*modern_dc_y_;modern_dc_x_=wet;
            modern_feedback_=std::max(-1.f,std::min(1.f,modern_dc_y_));
            newest=fir_head_;wet_fir_[newest]=modern_feedback_;carrier_fir_[newest]=last_carrier_;
            if(++fir_head_==FIR_TAPS)fir_head_=0;
            sub_input_=input_previous_+(source-input_previous_)*float(sub+1)*.5f;
        }
        input_previous_=source;
        // Unit-DC-gain FIR. The oscillator helper's fixed source headroom is not
        // a volume control for an input effect; final q15 conversion still bounds it.
        const float wet=osc_clean::decimate(wet_fir_,newest)/osc_clean::HEADROOM;
        q15 out=resolved_[P_MIX]==0?dry:ring_detail::sample(source+(wet-source)*mix);
        if(transition_left_){out=q15((int(transition_from_)*transition_left_+int(out)*(128-transition_left_))/128);--transition_left_;}
        buf[i]=last_out_=out;processed_=true;
        if(carrier)carrier[i]=ring_detail::sample(osc_clean::decimate(carrier_fir_,newest)/osc_clean::HEADROOM);
    }
    visual_phase_=uint16_t(modern_phase_>>16);visual_envelope_=uint16_t(std::min(32767.f,modern_env_*32767.f));
}
void RingUnit::legacy_process(q15* buf, int frames) {
    const q15 depth = params[P_DEPTH].effective;
    const q15 fb = params[P_FEEDBACK].effective, diode = params[P_DIODE].effective;
    const q15 mix = params[P_MIX].effective, follow = params[P_FOLLOW].effective;
    const uint32_t base_increment = ring_base_increment(params[P_FREQ].effective);
    constexpr uint32_t MAX_INCREMENT = 1610612736u; // 12 kHz, below Nyquist at 32 kHz
    constexpr q15 DC_POLE = 32604;                  // ~25 Hz DC blocker pole
    if (depth == 0) {
        // DEPTH is the real core amount: zero means exact identity, with no hidden
        // feedback or stale bridge offset waiting to erupt when the knob reopens.
        feedback_ = 0; dc_x_ = dc_y_ = 0; visual_envelope_ = 0;
        return;
    }

    for (int i = 0; i < frames; ++i) {
        const q15 dry = buf[i];
        const int32_t magnitude = dry == -32768 ? 32767 : (dry < 0 ? -dry : dry);
        if (magnitude > envelope_) envelope_ = (q15)magnitude;
        else envelope_ = (q15)(envelope_ - ((envelope_ - magnitude) >> 8));

        // FOLLOW pushes the carrier by up to eight times the base rate from the
        // actual input envelope. Saturating the increment prevents alias-wrap from
        // masquerading as a lower pitch at the top of the knob.
        const uint32_t follow_amount =
            (uint32_t)(((uint64_t)(uint16_t)envelope_ * (uint16_t)follow) >> 15);
        uint64_t increment = base_increment +
            (((uint64_t)base_increment * follow_amount * 7u) >> 15);
        if (increment > MAX_INCREMENT) increment = MAX_INCREMENT;
        phase_ += (uint32_t)increment;

        const uint32_t pp = phase_ >> 16;
        const int32_t tri = pp < 0x8000u
            ? (int32_t)pp * 2 - 32768
            : 32767 - ((int32_t)pp - 32768) * 2;
        const int32_t absolute_tri = tri < 0 ? -tri : tri;
        const int32_t sineish = (int32_t)(((int64_t)tri * (65536 - absolute_tri)) >> 15);
        const q15 carrier = sat_q15(sineish);

        // Feedback is signed, deliberately capped below unity, and fades out with
        // DEPTH. Thus DEPTH=0 cannot secretly excite a one-sample nonlinear loop.
        const q31 feedback_gain = (q31)(((int64_t)fb * 27852 * depth) >> 30);
        const q31 driven_input = (q31)dry +
            (q31)(((int64_t)feedback_ * feedback_gain) >> 15);
        const q31 clean_ring = (q31)(((int64_t)driven_input * carrier) >> 15);

        // A real diode bridge is closer to |x+c|-|x-c| than to the old one-sided
        // abs(ring), which injected a huge positive DC component. DIODE crossfades
        // from the clean four-quadrant multiplier into this bounded bridge law.
        const int64_t sum = (int64_t)driven_input + carrier;
        const int64_t difference = (int64_t)driven_input - carrier;
        const q31 bridge = (q31)((sum < 0 ? -sum : sum) -
                                  (difference < 0 ? -difference : difference));
        const q31 full_effect = clean_ring +
            (q31)(((int64_t)(bridge - clean_ring) * diode) >> 15);
        q31 wet = driven_input +
            (q31)(((int64_t)(full_effect - driven_input) * depth) >> 15);
        wet = trackr::fx::soft_clip_q31(wet);

        // Block residual bridge/feedback offset before it reaches the output or is
        // recirculated. q31 intermediates make full-scale feedback corners defined.
        const q31 highpassed = wet - dc_x_ +
            (q31)(((int64_t)dc_y_ * DC_POLE) >> 15);
        dc_x_ = wet;
        dc_y_ = highpassed > 32767 ? 32767 : highpassed < -32768 ? -32768 : highpassed;
        feedback_ = trackr::fx::soft_clip_q31(dc_y_);
        buf[i] = sat_q15((q31)dry +
            (q31)(((int64_t)(feedback_ - dry) * mix) >> 15));
    }
    visual_phase_ = (uint16_t)(phase_ >> 16);
    visual_envelope_ = (uint16_t)envelope_;
}
