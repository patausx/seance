// MOTOR implementation, included by units.cpp inside namespace seance.
// Legacy branch is the original six-parameter arithmetic. Modern branch has
// 4x box-integrated oscillators, DC removal and a bounded hard-sync slave;
// it reduces high-frequency edges, NOT an alias-free / analog emulation claim.
namespace motor_detail {
inline uint32_t uni(q15 v) { return v < 0 ? 0u : uint16_t(v); }
inline uint32_t table(q15 value,const uint32_t (&knots)[33]) {
    const uint32_t pos=uni(value)*32u,index=pos/32767u,rest=pos%32767u;
    if(index>=32)return knots[32];
    return knots[index]+uint32_t((uint64_t(knots[index+1]-knots[index])*rest+16383)/32767);
}
constexpr uint32_t max_inc=0x70000000u; // 14kHz at 32kHz output
}
MotorUnit::MotorUnit(bool modern) : modern_(modern) {
    kind=UnitKind::Motor;
    add_param("RPM",10000);add_param("JIT",8192);add_param("RES",19660);
    add_param("LOAD",18000);add_param("GAIN",18000);add_param("SRC",16384);
    add_param("WIDTH",16384);add_param("SYNC",0);add_param("JRATE",32767);
    add_param("NOTE",musical_note::encode(45));add_param("MODE",0);
    num_params=modern ? P_COUNT : 6;
}
void MotorUnit::set_modern(bool enabled) {
    if(modern_==enabled)return;
    if(processed_){transition_from_=last_out_;transition_left_=128;}
    modern_=enabled;num_params=enabled ? P_COUNT : 6;
    slave_phase_=phase_;wander_q16_=int32_t(jitter_current_)*65536;
    dc_x_=dc_y_=0;
    control_tick();
}
uint32_t MotorUnit::rate_millihz(q15 v) {
    return 18000u+uint32_t(uint64_t(motor_detail::uni(v))*742000u/32767u);
}
uint32_t MotorUnit::note_millihz(q15 v) {
    return uint32_t((440000ull*musical_note::ratio_q16(musical_note::decode(v)-69)+32768)/65536);
}
uint32_t MotorUnit::width_q32(q15 v) {
    const uint32_t raw=motor_detail::uni(v);
    return raw<=16384 ? uint32_t(214748365ull+1932735283ull*raw/16384u)
                      : uint32_t(2147483648ull+1932735283ull*(raw-16384)/16383u);
}
uint32_t MotorUnit::sync_ratio_q16(q15 v) {
    static constexpr uint32_t knots[33]={65536,71468,77936,84990,92682,101070,110218,120194,131072,142935,155872,169979,185364,202141,220436,240387,262144,285870,311744,339959,370728,404281,440872,480774,524288,571740,623487,679917,741455,808563,881744,961548,1048576};
    return motor_detail::table(v,knots);
}
uint32_t MotorUnit::jitter_millihz(q15 v) {
    static constexpr uint32_t knots[33]={100,125,156,195,244,305,381,476,595,743,929,1160,1450,1812,2264,2829,3536,4418,5521,6899,8621,10773,13462,16823,21022,26270,32828,41022,51262,64058,80048,100030,125000};
    return motor_detail::table(v,knots);
}
uint32_t MotorUnit::nominal_millihz() const {
    if(!note_mode())return rate_millihz(params[P_RPM].effective);
    const uint64_t mhz=(uint64_t(note_millihz(params[P_NOTE].target))*arp_ratio_q24(arp_voice.semitones)+(1u<<23))>>24;
    return uint32_t(std::min<uint64_t>(14000000u,mhz));
}
void MotorUnit::reset() {
    phase_=phase_b_=phase_c_=slave_phase_=0;rng_=0x4D4F544Fu;
    jitter_count_=0;jitter_current_=jitter_target_=0;wander_q16_=0;
    resonator_=resonator_prev_=dc_x_=dc_y_=0;
    visual_energy_=transition_left_=0;last_out_=transition_from_=0;processed_=false;
    // Recompute coefficients from resolved parameters; no runtime pitch mutation.
    if(modern_)control_tick();
}
void MotorUnit::control_tick() {
    if(!modern_) {
        const uint32_t hz_q8=18u*256u+uint32_t(uint64_t(uint16_t(params[P_RPM].effective))*742u*256u/32767u);
        base_inc_=uint32_t((uint64_t(hz_q8)<<24)/ENGINE_SR);
        ratio_=65536;width_=0x80000000u;jitter_period_=256;jitter_smoothing_=64;sync_limited_=false;
        return;
    }
    base_inc_=uint32_t((uint64_t(nominal_millihz())<<32)/(ENGINE_SR*1000ull));
    base_inc_=std::max<uint32_t>(1u,std::min<uint32_t>(motor_detail::max_inc,base_inc_));
    ratio_=sync_ratio_q16(params[P_SYNC].effective);
    width_=width_q32(params[P_WIDTH].effective);
    jitter_period_=uint32_t((ENGINE_SR*1000ull+jitter_millihz(params[P_JITTER_SPEED].effective)/2)/jitter_millihz(params[P_JITTER_SPEED].effective));
    jitter_smoothing_=std::max<uint32_t>(64u,jitter_period_/4);
    sync_limited_=(uint64_t(base_inc_)*ratio_>>16)>motor_detail::max_inc;
}
void MotorUnit::process(q15* buf,int frames) { process_with_raw(buf,nullptr,frames); }
void MotorUnit::process_with_raw(q15* buf,q15* pre,int frames) {
    const uint32_t jitter=motor_detail::uni(params[P_JITTER].effective);
    const uint32_t load=motor_detail::uni(params[P_LOAD].effective);
    const uint32_t resonance=motor_detail::uni(params[P_RESONANCE].effective);
    const q15 gain=params[P_GAIN].effective;
    if(base_inc_==0)control_tick();
    if(modern_ && !processed_)jitter_count_=jitter_period_-1; // slow drift starts immediately
    int source=(uint32_t(uint16_t(params[P_SOURCE].effective))*3u)>>15;if(source>2)source=2;
    int32_t peak=0;
    for(int i=0;i<frames;++i) {
        if(++jitter_count_>=jitter_period_){jitter_count_=0;rng_=rng_*1664525u+1013904223u;jitter_target_=int16_t(int32_t(rng_>>17)-16384);}
        if(modern_){
            wander_q16_+=int32_t((int64_t(jitter_target_)*65536-wander_q16_)/jitter_smoothing_);
            jitter_current_=int16_t(wander_q16_/65536);
        } else jitter_current_+=(jitter_target_-jitter_current_)/64;
        int64_t inc=int64_t(base_inc_)+(int64_t(base_inc_)*jitter_current_*jitter>>29);
        if(inc<1)inc=1;
        const uint32_t spread=1u+load/2048u;
        int32_t raw;
        if(!modern_) {
            // Keep the order, rounding and saturation of the original MOTOR.
            phase_+=uint32_t(inc);phase_b_+=uint32_t(inc+(inc*spread)/512u);phase_c_+=uint32_t(inc-(inc*spread)/512u);
            const int32_t saw=int32_t(phase_>>16)-32768;
            const int32_t pulse=(phase_&0x80000000u)?32767:-32767;
            raw=source==int(Source::Saw)?saw:source==int(Source::Pulse)?pulse:(saw+pulse)/2;
            const int32_t b=int32_t(phase_b_>>16)-32768,c=int32_t(phase_c_>>16)-32768;
            raw+=int32_t((int64_t(b+c)*load)>>17);
        } else {
            inc=std::min<int64_t>(inc,motor_detail::max_inc);
            const uint32_t a=uint32_t(inc),b=uint32_t(inc+(inc*spread)/512u),c=uint32_t(inc-(inc*spread)/512u);
            const uint64_t requested=uint64_t(a)*ratio_>>16;
            const uint32_t slave=uint32_t(std::min<uint64_t>(motor_detail::max_inc,requested));
            const uint32_t ratio=requested<=motor_detail::max_inc ? ratio_ : uint32_t((uint64_t(slave)<<16)/a);
            int64_t sum=0;
            for(uint32_t k=1;k<=4;++k){
                const uint32_t old=phase_;
                phase_+=uint32_t(uint64_t(a)*k/4-uint64_t(a)*(k-1)/4);
                phase_b_+=uint32_t(uint64_t(b)*k/4-uint64_t(b)*(k-1)/4);
                phase_c_+=uint32_t(uint64_t(c)*k/4-uint64_t(c)*(k-1)/4);
                if(phase_<old)slave_phase_=uint32_t(uint64_t(phase_)*ratio>>16);
                else slave_phase_+=uint32_t(uint64_t(slave)*k/4-uint64_t(slave)*(k-1)/4);
                const int32_t saw=int32_t(slave_phase_>>16)-32768;
                const int32_t pulse=slave_phase_<width_ ? -32767 : 32767;
                int32_t value=source==int(Source::Saw)?saw:source==int(Source::Pulse)?pulse:(saw+pulse)/2;
                const int32_t vb=int32_t(phase_b_>>16)-32768,vc=int32_t(phase_c_>>16)-32768;
                value+=int32_t((int64_t(vb+vc)*load)>>17);sum+=value;
            }
            raw=int32_t(sum/4);
        }
        if(pre)pre[i]=sat_q15(raw);
        const int32_t damp=4096+int32_t((32767u-resonance)>>2);
        int32_t body=resonator_+((raw-resonator_)*damp>>15);
        body+=int32_t((int64_t(resonator_-resonator_prev_)*resonance)>>16);
        resonator_prev_=resonator_;resonator_=sat_q15(body);
        int32_t signal=resonator_;
        if(modern_){
            // Two-sample DC blocker; widened Q15 state retains the long decay.
            const int32_t x=signal*32768;
            const int64_t y=int64_t(x)-dc_x_+int64_t(dc_y_)*32752/32768;
            dc_x_=x;dc_y_=int32_t(std::max<int64_t>(INT32_MIN,std::min<int64_t>(INT32_MAX,y)));
            signal=dc_y_/32768;
        }
        int32_t out=sat_q15(int32_t(int64_t(signal)*gain>>15));
        if(transition_left_){out=int32_t((int64_t(transition_from_)*transition_left_+int64_t(out)*(128-transition_left_))/128);--transition_left_;}
        buf[i]=last_out_=q15(out);processed_=true;
        const int32_t magnitude=out<0 ? -out : out;if(magnitude>peak)peak=magnitude;
    }
    visual_energy_=uint16_t(peak>32767?32767:peak);
}
