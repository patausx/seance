// Included inside namespace seance by units.cpp. Old seven-param DSP stays exact.
// Modern controls/cache are instance-local; no heap, transcendental sample loop,
// shared PCM history or universal Param changes. Aliasing is intentional here.
namespace bit_detail {
uint32_t uni(q15 value){return uint32_t(std::max(0,int(value)));}
uint32_t map(q15 value,const uint32_t (&table)[33],uint32_t span=32767){
    const uint32_t v=std::min(span,uni(value)),scaled=v*32u,index=scaled/span,rest=scaled%span;
    return index>=32?table[32]:table[index]+uint32_t((uint64_t(table[index+1]-table[index])*rest+span/2)/span);
}
uint32_t random(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
float unit(uint32_t& state){return float(random(state)>>8)*(1.f/16777216.f);}
float sine(uint32_t phase){
    // Bounded fast sine approximation, not an audio oscillator/alias-free claim.
    const float x=float(int64_t(phase)-2147483648ll)*(1.f/2147483648.f);
    const float y=4.f*x*(1.f-std::abs(x));return -(y+.225f*(y*std::abs(y)-y));
}
q15 sample(float value){
    value=std::max(-1.f,std::min(32767.f/32768.f,value));
    const float scaled=value*32768.f;
    return q15(int(scaled+(scaled<0?-.5f:.5f)));
}
float quantize(float value,int bits){
    value=std::max(-1.f,std::min(32767.f/32768.f,value));
    if(bits==16)return value; // genuine bypass of the quantizer
    if(bits==1)return value==0?0.f:value<0?-32767.f/32768.f:32767.f/32768.f;
    const int step=1<<(16-bits);
    const int level=int(std::abs(value)*32768.f/float(step)+.5f);
    const int rounded=(value<0?-level:level)*step;
    // Symmetric interior rounding, representable signed endpoints. Subsequent
    // DC blocker also removes the small saturation asymmetry, not via dither.
    return float(std::max(-32768,std::min(32768-step,rounded)))*(1.f/32768.f);
}
}
BitUnit::BitUnit(bool modern):modern_(modern){
    kind=UnitKind::Bitcrush;
    add_param("BITS",20000);add_param("RATE",28000);add_param("MIX",trackr::fx::Q15_ONE);
    add_param("JIT",0);add_param("DROP",0);add_param("DRIFT",0,true);add_param("COMP",0);
    add_param("DRIVE",0);add_param("TONE",32767);add_param("GAIN",26214);
    add_param("DITHER",0);add_param("DLEN",14746);
    num_params=modern?P_COUNT:LEGACY_COUNT;
}
int BitUnit::bit_depth(q15 value){return 1+int(bit_detail::uni(value)*16u/32768u);}
uint32_t BitUnit::rate_millihz(q15 value){
    static constexpr uint32_t t[33]={100000,119753,143408,171735,205657,246280,294928,353184,422949,506493,606540,726349,869824,1041639,1247392,1493788,1788854,2142204,2565351,3072082,3678906,4405596,5275828,6317955,7565933,9060422,10850115,12993325,15559880,18633403,22314035,26721697,32000000};
    return bit_detail::map(value,t);
}
uint32_t BitUnit::tone_millihz(q15 value){
    if(value==32767)return 0;
    static constexpr uint32_t t[33]={100000,116699,136186,158928,185467,216437,252580,294758,343979,401420,468452,546678,637967,744500,868822,1013906,1183216,1380799,1611376,1880457,2194472,2560923,2988567,3487623,4070015,4749660,5542798,6468380,7548524,8809039,10280045,11996693,14000000};
    return bit_detail::map(value,t,32766);
}
uint32_t BitUnit::drop_frames(q15 value){
    static constexpr uint32_t t[33]={32,38,45,54,64,76,90,107,127,151,180,214,254,302,358,426,506,601,714,849,1009,1199,1425,1693,2012,2391,2841,3376,4012,4767,5665,6732,8000};
    return bit_detail::map(value,t);
}
int BitUnit::drive_centidb(q15 value){return int(bit_detail::uni(value)*2400u/32767u);}
int BitUnit::gain_centidb(q15 value){const int v=int(bit_detail::uni(value));return v<=26214?-2400+(v*2400+13107)/26214:((v-26214)*600+3276)/6553;}
int BitUnit::drift_millihz(q15 value){return std::max(-32767,int(value))*8000/32767;}
q15 BitUnit::control_value(int p)const{
    if(p<0||p>=num_params)return 0;
    const auto& a=params[p];
    return modern_&&std::abs(int(a.target)-int(a.effective))<=3?a.target:a.effective;
}
void BitUnit::set_modern(bool enabled){
    if(enabled==modern_)return;
    const bool played=processed_;const q15 previous=last_out_;
    modern_=enabled;num_params=enabled?P_COUNT:LEGACY_COUNT;params[P_DRIFT].bipolar=true;
    if(enabled)reset();
    if(played&&enabled){transition_from_=previous;transition_left_=128;}
}
void BitUnit::reset(){
    if(!modern_)return; // inherited old reset was intentionally a no-op
    clock_rng_=0x0B17C0DEu;drop_rng_=0xD09F0A17u;dither_rng_=0xD17E1234u;
    modern_phase_=phase_inc_=drop_left_=0;
    remaining_=modern_held_=low_=dc_input_=dc_output_=0;
    processed_=false;cache_valid_=false;last_out_=transition_from_=0;transition_left_=0;
    visual_bits_=16;visual_period_=1;visual_held_=0;visual_dropout_=false;
    control_tick();
}
void BitUnit::control_tick(){
    if(!modern_)return;
    q15 next[P_COUNT];bool changed=!cache_valid_;
    for(int p=0;p<P_COUNT;++p){next[p]=control_value(p);changed|=next[p]!=resolved_[p];}
    if(!changed)return;
    if(cache_valid_&&resolved_[P_DRIFT]!=0&&next[P_DRIFT]==0){modern_phase_=0;remaining_=0;}
    if(!next[P_DROPOUT])drop_left_=0;
    depth_=bit_depth(next[P_BITS]);interval_=32000000.f/float(rate_millihz(next[P_RATE]));
    if(!cache_valid_||next[P_DRIVE]!=resolved_[P_DRIVE])drive_=std::pow(10.f,float(drive_centidb(next[P_DRIVE]))/2000.f);
    if(!cache_valid_||next[P_GAIN]!=resolved_[P_GAIN])gain_=std::pow(10.f,float(gain_centidb(next[P_GAIN]))/2000.f);
    if(!cache_valid_||next[P_TONE]!=resolved_[P_TONE]){const uint32_t mhz=tone_millihz(next[P_TONE]);alpha_=mhz?1.f-std::exp(-6.28318530718f*float(mhz)/(1000.f*ENGINE_SR)):1.f;}
    // Keep sub-millihertz signed movement in the phase accumulator.
    const int raw=std::max(-32767,int(next[P_DRIFT]));
    phase_inc_=uint32_t(int64_t(raw)*(8ll<<32)/(32767ll*ENGINE_SR));
    jitter_=float(next[P_JITTER])*(.5f/32767.f);comp_=float(next[P_COMP])/32767.f;
    dither_=float(next[P_DITHER])*(1.f/32767.f)*float(1u<<(16-depth_))*(1.f/32768.f);
    drop_total_=drop_frames(next[P_DROP_TIME]);
    neutral_=depth_==16&&next[P_RATE]==32767&&next[P_DRIVE]==0&&next[P_TONE]==32767&&next[P_JITTER]==0&&next[P_DROPOUT]==0&&next[P_DRIFT]==0&&next[P_COMP]==0&&next[P_DITHER]==0;
    std::copy(next,next+P_COUNT,resolved_);cache_valid_=true;
}
void BitUnit::process(q15* buf,int frames){process_with_events(buf,nullptr,frames);}
void BitUnit::process_with_events(q15* buf,uint8_t* events,int frames){
    if(!buf||frames<=0)return;
    if(modern_)modern_process(buf,events,frames);else legacy_process(buf,events,frames);
}
void BitUnit::modern_process(q15* buf,uint8_t* events,int frames){
    control_tick();bool any_drop=false;uint8_t period=visual_period_;
    const int drop=resolved_[P_DROPOUT],mix=resolved_[P_MIX];
    const uint64_t threshold=uint64_t(drop)*4294967296ull/32767u;
    const float mix_scale=float(mix)/32767.f; // loop-invariant: the same division the loop made per sample
    for(int i=0;i<frames;++i){
        const q15 dry=buf[i];const float source=float(dry)*(1.f/32768.f);
        modern_phase_+=phase_inc_;
        const bool capture=remaining_<=0;
        if(capture){
            const float drift=phase_inc_?.125f*bit_detail::sine(modern_phase_):0.f;
            const float interval=std::max(1.f,interval_/(1.f+drift)*(1.f+(2.f*bit_detail::unit(clock_rng_)-1.f)*jitter_));
            remaining_+=interval;period=uint8_t(std::min(255,int(interval+.5f)));
            float wet=std::max(-1.f,std::min(32767.f/32768.f,source*drive_));
            if(dither_>0&&depth_<16)wet+=(bit_detail::unit(dither_rng_)-bit_detail::unit(dither_rng_))*dither_;
            modern_held_=bit_detail::quantize(wet,depth_);
            if(!drop_left_&&drop>0&&uint64_t(bit_detail::random(drop_rng_))<threshold)drop_left_=drop_total_;
        }
        remaining_-=1.f;
        const bool dropped=drop==32767||drop_left_>0;
        if(drop_left_)--drop_left_;
        if(events)events[i]=uint8_t((capture?1:0)|(dropped?2:0));
        any_drop|=dropped;
        float wet=dropped?0.f:modern_held_;
        const float compressed=wet*(1.f+comp_)/(1.f+std::abs(wet)*comp_);
        wet+=(compressed-wet)*comp_;
        low_+=alpha_*(wet-low_);
        dc_output_=low_-dc_input_+.998430436f*dc_output_;dc_input_=low_;
        wet=(dropped?0.f:neutral_?source:dc_output_)*gain_;
        q15 out=mix==0?dry:bit_detail::sample(mix==32767?wet:source+(wet-source)*mix_scale);
        if(transition_left_){out=q15((int(transition_from_)*transition_left_+int(out)*(128-transition_left_))/128);--transition_left_;}
        buf[i]=last_out_=out;processed_=true;
    }
    visual_bits_=uint8_t(depth_);visual_period_=period;visual_held_=bit_detail::sample(modern_held_);visual_dropout_=any_drop;
}
void BitUnit::legacy_process(q15* buf,uint8_t* events,int frames){
    const int bits = 3 + ((uint16_t)params[P_BITS].effective * 13 >> 15);
    const int shift = 16 - bits;
    const int32_t hold = 1 + (int32_t)((uint32_t)(32767 - (uint16_t)params[P_RATE].effective) * 31u / 32767u);
    const q15 mix=params[P_MIX].effective, jitter=params[P_JITTER].effective;
    const q15 dropout=params[P_DROPOUT].effective, drift=params[P_DRIFT].effective;
    const q15 comp=params[P_COMP].effective;
    bool dropped_in_block = false;
    uint8_t last_period = (uint8_t)hold;
    // The compressor is a pure function of (held_, comp) and comp is fixed for the
    // block, so its division runs only when the sample being used differs from
    // the last one computed, instead of on every output sample. It is evaluated
    // for exactly the held values the per-sample form used, never a stale one.
    q31 crushed = 0, crushed_for = INT32_MIN; // INT32_MIN: no q15 held value
    for (int i=0;i<frames;++i) {
        const q15 dry=buf[i];
        rng_=rng_*1664525u+1013904223u;
        drift_phase_ += (uint32_t)(int32_t)drift;
        const int32_t jitter_n=(int32_t)(rng_>>24)-128;
        const int32_t jitter_offset = (int32_t)(((int64_t)jitter_n * jitter) / (1 << 20));
        int32_t period=hold + jitter_offset + (int32_t)((drift_phase_>>20)&7u);
        if(period<1)period=1;
        if(period>255)period=255;
        last_period=(uint8_t)period;
        const bool captured=++count_ >= (uint32_t)period;
        if(captured){
            count_=0;
            const uint16_t mask=(uint16_t)~((1u<<shift)-1u);
            const bool dropped=(rng_&0x7fffu)<(uint16_t)dropout;
            dropped_in_block |= dropped;
            held_=dropped?0:(q15)((uint16_t)dry&mask);
            legacy_drop_=dropped;
        }
        if((q31)held_!=crushed_for){
            const q31 v=held_;
            const q31 av=v<0?-v:v;
            const q31 compressed=(v*(32767+(q31)comp))/(32767+((av*comp)>>15));
            crushed=v+(((compressed-v)*comp)>>15);crushed_for=v;
        }
        buf[i]=sat_q15((q31)dry+(((crushed-dry)*mix)>>15));
        if(events)events[i]=uint8_t((captured?1:0)|(legacy_drop_?2:0));
    }
    visual_bits_=(uint8_t)bits;visual_period_=last_period;visual_held_=held_;visual_dropout_=dropped_in_block;
    last_out_=buf[frames-1];processed_=true;
}
