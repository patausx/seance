// Included by units.cpp inside namespace seance. Separate legacy path is frozen.
// Modern 2x/64k reconstruction +47tap decimator reduces, not eliminates, aliases.
namespace fold_detail {
inline int uni(q15 v) { return std::max(0,int(v)); }
inline int bi(q15 v) { return std::max(-32767,int(v)); }
// `lut` is osc_clean::sine_table(), fetched once per block by modern_process
// instead of once per sine call (function-local static guard). Same table.
inline float periodic_sine(const float* lut,float cycles) {
    const float phase=cycles-float(osc_clean::floor_small(cycles));
    return osc_clean::sine(lut,uint32_t(phase*16777216.f)<<8);
}
inline float fold(const float* lut,float x,float smooth) {
    const float u=x-4.f*float(osc_clean::floor_small((x+1.f)*.25f));
    const float sharp=u<=1.f?u:2.f-u;
    return smooth>0?sharp+(periodic_sine(lut,x*.25f)-sharp)*smooth:sharp;
}
inline float wrap(const float* lut,float x,float smooth) {
    const float sharp=x-2.f*float(osc_clean::floor_small((x+1.f)*.5f));
    return smooth>0?sharp+(periodic_sine(lut,x*.5f)-sharp)*smooth:sharp;
}
inline float diode(float x,float smooth) {
    const float a=std::abs(x),radius=.16f*smooth;
    // Rounded absolute cusp: zero stays zero, no sqrt/trig/audio-loop allocation.
    const float rounded=radius>0?x*x/(a+radius):a;
    return .375f*x+.625f*rounded;
}
inline float shape(const float* lut,float x,int mode,float smooth) {
    if(mode==0)return fold(lut,x,smooth);
    if(mode==16384)return wrap(lut,x,smooth);
    if(mode==32767)return diode(x,smooth);
    if(mode<16384){const float a=fold(lut,x,smooth);return a+(wrap(lut,x,smooth)-a)*(float(mode)/16384.f);}
    const float a=wrap(lut,x,smooth);return a+(diode(x,smooth)-a)*(float(mode-16384)/16383.f);
}
inline float shape(float x,int mode,float smooth){return shape(osc_clean::sine_table(),x,mode,smooth);}
inline q15 sample(float x) {
    const float scaled=std::max(-1.f,std::min(32767.f/32768.f,x))*32768.f;
    return q15(int(scaled+(scaled<0?-.5f:.5f)));
}
}
FoldUnit::FoldUnit(bool modern):modern_(modern) {
    kind=UnitKind::Fold;
    add_param("DRV",8192,modern);add_param("TRIM",Q15_ONE);add_param("MODE",0);
    add_param("BIAS",0,true);add_param("MIX",Q15_ONE);
    add_param("SMOOTH",0);add_param("TONE",32767);
    num_params=modern?P_COUNT:LEGACY_COUNT;
    if(!modern)for(int p=LEGACY_COUNT;p<P_COUNT;++p){params[p].base=0;params[p].snap();}
    // Construct read-only shared LUT outside the audio loop, including legacy
    // cards so later explicit conversion cannot lazily initialize it in audio.
    osc_clean::sine_table();
}
uint32_t FoldUnit::drive_q16(q15 value,bool modern) {
    if(!modern)return uint32_t(32767+7*fold_detail::uni(value))*2u;
    const int v=fold_detail::bi(value);
    return uint32_t(65536+(int64_t(v)*(v<0?49152:458752))/32767);
}
uint32_t FoldUnit::tone_hz(q15 value) {
    const uint32_t v=uint32_t(fold_detail::uni(value));
    if(v==32767)return 0;
    static constexpr uint32_t knots[33]={100,117,137,161,189,221,259,303,356,417,488,572,671,786,921,1079,1265,1482,1737,2036,2385,2795,3276,3839,4499,5272,6178,7240,8484,9942,11651,13653,16000};
    const uint32_t scaled=v*32u,i=scaled/32766u,f=scaled%32766u;
    return i>=32?knots[32]:knots[i]+uint32_t((uint64_t(knots[i+1]-knots[i])*f+16383)/32766);
}
q15 FoldUnit::control_value(int p) const {
    if(p<0||p>=num_params)return 0;
    const Param& a=params[p];
    return modern_&&std::abs(int(a.target)-int(a.effective))<=3?a.target:a.effective;
}
float FoldUnit::transfer(float input,const q15* values) {
    if(!values)return input;
    const float drive=float(drive_q16(values[P_DRIVE]))/65536.f;
    const float bias=float(fold_detail::bi(values[P_BIAS]))/32768.f;
    const float smooth=float(fold_detail::uni(values[P_SMOOTH]))/32767.f;
    return fold_detail::shape(input*drive+bias,fold_detail::uni(values[P_MODE]),smooth)-bias;
}
void FoldUnit::set_modern(bool enabled) {
    params[P_DRIVE].bipolar=enabled;params[P_BIAS].bipolar=true;
    if(modern_==enabled)return;
    const bool played=processed_;const q15 previous=last_out_;
    modern_=enabled;num_params=enabled?P_COUNT:LEGACY_COUNT;
    reset();
    if(played&&enabled){transition_from_=previous;transition_left_=128;}
}
void FoldUnit::reset() {
    input_previous_=dc_x_=dc_y_=tone_state_=0;
    fir_head_=transition_left_=0;last_out_=transition_from_=0;
    std::fill(fir_,fir_+FIR_TAPS,0.f);cache_valid_=processed_=false;
    if(modern_)control_tick();
}
void FoldUnit::control_tick() {
    if(!modern_)return;
    q15 next[P_COUNT];bool changed=!cache_valid_;
    for(int p=0;p<P_COUNT;++p){next[p]=control_value(p);changed|=next[p]!=resolved_[p];}
    if(!changed)return;
    drive_=float(drive_q16(next[P_DRIVE]))/65536.f;
    bias_=float(fold_detail::bi(next[P_BIAS]))/32768.f;
    smooth_=float(fold_detail::uni(next[P_SMOOTH]))/32767.f;
    trim_=float(fold_detail::uni(next[P_TRIM]))/32767.f;
    mix_=float(fold_detail::uni(next[P_MIX]))/32767.f;
    if(!cache_valid_||next[P_TONE]!=resolved_[P_TONE]){
        const auto hz=tone_hz(next[P_TONE]);
        tone_alpha_=hz?1.f-std::exp(-6.283185307179586f*float(hz)/64000.f):1.f;
    }
    std::copy(next,next+P_COUNT,resolved_);cache_valid_=true;
}
void FoldUnit::process(q15* buf,int frames) {
    if(!buf||frames<=0)return;
    if(modern_)modern_process(buf,frames);
    else {legacy_process(buf,frames);last_out_=buf[frames-1];processed_=true;}
}
void FoldUnit::modern_process(q15* buf,int frames) {
    control_tick();
    static_assert(FIR_TAPS==osc_clean::TAPS,"shared immutable decimation coefficients");
    const int mode=fold_detail::uni(resolved_[P_MODE]);
    const float* const sine_lut=osc_clean::sine_table(); // one init-guard check per block
    for(int i=0;i<frames;++i){
        const q15 dry=buf[i];const float source=float(dry)/32768.f;
        int newest=fir_head_;
        for(int sub=0;sub<2;++sub){
            const float input=input_previous_+(source-input_previous_)*float(sub+1)*.5f;
            const float wet=fold_detail::shape(sine_lut,input*drive_+bias_,mode,smooth_)-bias_;
            dc_y_=wet-dc_x_+.999214910f*dc_y_;dc_x_=wet;
            if(std::abs(dc_y_)<1e-20f)dc_y_=0;
            tone_state_+=tone_alpha_*(dc_y_-tone_state_);
            if(std::abs(tone_state_)<1e-20f)tone_state_=0;
            newest=fir_head_;fir_[newest]=tone_state_;
            if(++fir_head_==FIR_TAPS)fir_head_=0;
        }
        input_previous_=source;
        const float wet=osc_clean::decimate(fir_,newest)/osc_clean::HEADROOM*trim_;
        q15 out=resolved_[P_MIX]==0?dry:fold_detail::sample(source+(wet-source)*mix_);
        if(transition_left_){
            // Semantic dry/mute endpoints take priority over conversion smoothing.
            if(resolved_[P_MIX]!=0&&!(resolved_[P_MIX]==32767&&resolved_[P_TRIM]==0))
                out=q15((int(transition_from_)*transition_left_+int(out)*(128-transition_left_))/128);
            --transition_left_;
        }
        buf[i]=last_out_=out;processed_=true;
    }
}
void FoldUnit::legacy_process(q15* buf, int frames) {
    if (!buf || frames <= 0) return;
    const q31 drive = 32767 + ((q31)(uint16_t)params[P_DRIVE].effective * 7);
    const q15 trim=params[P_TRIM].effective, mode=params[P_MODE].effective;
    const q15 bias=params[P_BIAS].effective, mix=params[P_MIX].effective;
    for (int i=0;i<frames;++i) {
        const q31 dry=buf[i];
        // DRIVE reaches 8x; widen before the shift because -32768 * 262136
        // exceeds signed int32 even though the resulting shaped sample does not.
        const q31 x=(q31)(((int64_t)dry*drive)>>15)+bias;
        q31 folded=x;
        for(int k=0;k<8&&(folded>32767||folded<-32768);++k){if(folded>32767)folded=65534-folded;if(folded<-32768)folded=-65536-folded;}
        const q31 wrapped=((x+32768)&0xffff)-32768, diode=x<0?-(x>>2):x;
        q31 shaped=mode<16384
            ? folded+(q31)(((int64_t)(wrapped-folded)*mode)>>14)
            : wrapped+(q31)(((int64_t)(diode-wrapped)*(mode-16384))>>14);
        shaped=(q31)(((int64_t)(shaped-bias)*trim)>>15);
        buf[i]=sat_q15(dry+(q31)(((int64_t)(shaped-dry)*mix)>>15));
    }
}
