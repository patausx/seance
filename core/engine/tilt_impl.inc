// Included inside namespace seance after the three frozen historical Tilt laws.
// One first-order reciprocal shelf; no heap, audio floats or transcendental math.
q15 TiltUnit::control_value(int p) const {
    if(p<0 || p>=num_params)return p==P_TRIM?tilt_eq::TRIM_UNITY:0;
    const auto& v=params[p];
    if(!extended())return v.effective;
    const int delta=int(v.target)-v.effective;
    const int value=v.slew_ready && delta>=-3 && delta<=3?v.target:v.effective;
    return q15(std::max(p==P_TILT?-32767:0,std::min(32767,value)));
}
void TiltUnit::control_tick() {
    if(!extended())return;
    q15 next[P_COUNT];for(int p=0;p<P_COUNT;++p)next[p]=control_value(p);
    if(cache_valid_ && std::equal(next,next+P_COUNT,resolved_))return;
    std::copy(next,next+P_COUNT,resolved_);
    const auto c=tilt_extended::coefficients(next[P_TILT],next[P_FREQ],next[P_RANGE]);
    b0_=c.b0;b1_=c.b1;a1_=c.a1;
    trim_gain_=tilt_eq::trim_gain_q16(next[P_TRIM]);cache_valid_=true;
}
void TiltUnit::reset() {
    low_=0;eq_state_=0;previous_input_=0;cache_valid_=false;
    processed_=false;last_out_=0;transition_from_=0;transition_left_=0;
}
void TiltUnit::extended_process(q15* buf,int frames) {
    control_tick(); // also handles direct/private reset renders, not just Engine ticks
    constexpr int64_t FRACTION=4096;
    constexpr int64_t LIMIT=16LL*32768*FRACTION;
    const int mix=resolved_[P_MIX];
    for(int i=0;i<frames;++i){
        const int32_t dry=buf[i];
        const int64_t sum=int64_t(b0_)*dry*FRACTION + int64_t(b1_)*previous_input_*FRACTION - int64_t(a1_)*eq_state_;
        // Signed division truncates toward zero: no negative floor/DC limit cycle.
        int64_t y=resolved_[P_TILT]==0?int64_t(dry)*FRACTION:sum/tilt_extended::COEFF_ONE;
        y=std::max(-LIMIT,std::min(LIMIT,y)); // bounded state on adversarial modulation
        previous_input_=q15(dry);eq_state_=y;
        const int32_t tilted=int32_t(y/FRACTION);
        const int32_t coloured=tilt_extended::colour(tilted,resolved_[P_DRIVE]);
        const int64_t wet=int64_t(coloured)*trim_gain_/65536;
        int64_t out=mix==0?dry:mix==32767?wet:int64_t(dry)+(wet-dry)*mix/32767;
        // Preserve exact dry over the explicit 128-sample revision bridge.
        if(transition_left_){
            if(mix)out=(int64_t(transition_from_)*transition_left_+out*(128-transition_left_))/128;
            --transition_left_;
        }
        buf[i]=q15(std::max<int64_t>(-32768,std::min<int64_t>(32767,out)));
    }
}
void TiltUnit::process(q15* buf,int frames) {
    if(!buf || frames<=0)return;
    if(extended())extended_process(buf,frames);else legacy_process(buf,frames);
    processed_=true;last_out_=buf[frames-1];
}
