// Included by units.cpp: preserves all existing host/3ds .cpp link lists.
namespace seance {
namespace {
int arp_index(q15 v, int count) { return (std::max(0,int(v)) * (count-1) + 16383) / 32767; }
q15 arp_value(int i, int count) { i=std::max(0,std::min(count-1,i)); return q15((i*32767+(count-1)/2)/(count-1)); }
const char* const arp_rates[] = {"1/4","1/8","1/16","1/32","1/4t","1/8t","1/16t","1/32t","1/4d","1/8d","1/16d","1/32d"};
const char* const arp_orders[] = {"up","down","updown","played","random","shuffle"};
// Q32 beats, rational triplets carry their sub-Q32 remainder across notes.
const uint8_t arp_numerator[] = {1,1,1,1,2,2,2,2,3,3,3,3};
const uint8_t arp_denominator[] = {1,2,4,8,3,6,12,24,2,4,8,16};
}
uint32_t Unit::arp_ratio_q24(int semitones) {
    // Rounded immutable equal-temperament LUT: no libm/static initialization on audio.
    static constexpr uint32_t ratio[12] = {16777216,17774841,18831788,19951585,21137968,22394897,
        23726566,25137421,26632170,28215802,29893600,31671166};
    semitones=std::max(-24,std::min(48,semitones));
    int octave=semitones/12, note=semitones%12;
    if(note<0){note+=12;--octave;}
    return octave<0 ? ratio[note]>>(-octave) : ratio[note]<<octave;
}
uint32_t Unit::arp_scale_increment(uint32_t increment) const {
    if(!arp_voice.semitones) return increment;
    const uint64_t value=(uint64_t(increment)*arp_ratio_q24(arp_voice.semitones)+(1u<<23))>>24;
    return uint32_t(std::min<uint64_t>(value,0x70000000u)); // 14 kHz, only transposed synth notes
}
float Unit::arp_pitch_ratio() const { return float(arp_ratio_q24(arp_voice.semitones))*(1.f/16777216.f); }
void Unit::set_arp_pitch(int semitones) {
    semitones=std::max(-24,std::min(48,semitones));
    if(arp_voice.semitones==semitones)return;
    arp_voice.semitones=int8_t(semitones); control_tick();
}
bool ArpUnit::supports(UnitKind k) { return k==UnitKind::Osc || k==UnitKind::Fm || k==UnitKind::Bird || k==UnitKind::SamplePlayer || k==UnitKind::Inst || k==UnitKind::Motor; }
bool ArpUnit::accepts(const Unit* unit) {
    return unit && supports(unit->kind) &&
        (unit->kind!=UnitKind::Motor || static_cast<const MotorUnit*>(unit)->note_mode());
}
int ArpUnit::rate_index(q15 v){return arp_index(v,RATE_COUNT);}
q15 ArpUnit::rate_value(int i){return arp_value(i,RATE_COUNT);}
const char* ArpUnit::rate_name(int i){return arp_rates[std::max(0,std::min(RATE_COUNT-1,i))];}
int ArpUnit::gate_percent(q15 v){return 5+arp_index(v,96);}
q15 ArpUnit::gate_value(int p){return arp_value(p-5,96);}
int ArpUnit::order_index(q15 v){return arp_index(v,ORDER_COUNT);}
q15 ArpUnit::order_value(int i){return arp_value(i,ORDER_COUNT);}
const char* ArpUnit::order_name(int i){return arp_orders[std::max(0,std::min(ORDER_COUNT-1,i))];}
int ArpUnit::octaves(q15 v){return 1+arp_index(v,3);}
q15 ArpUnit::octaves_value(int n){return arp_value(n-1,3);}
int ArpUnit::note_semitones(q15 v){const int n=arp_index(v,50);return n ? n-25 : NOTE_OFF;}
q15 ArpUnit::note_value(int n){return n==NOTE_OFF ? 0 : arp_value(std::max(-24,std::min(24,n))+25,50);}
ArpUnit::ArpUnit() {
    kind=UnitKind::Arp;
    add_param("RATE",rate_value(2)); add_param("GATE",gate_value(75));
    add_param("ORDER",order_value(0)); add_param("OCT",octaves_value(1));
    add_param("RESET",0); add_param("PLAY",0);
    add_param("NOTE1",note_value(0)); add_param("NOTE2",note_value(4));
    add_param("NOTE3",note_value(7)); add_param("NOTE4",note_value(NOTE_OFF));
    reset();
}
uint32_t ArpUnit::random(){rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;return rng_;}
void ArpUnit::make_cycle() {
    visual_.count=candidate_count_;
    for(int i=0;i<candidate_count_;++i) visual_.notes[i]=candidates_[i];
    if(order_==4) for(int i=0;i<candidate_count_;++i) visual_.notes[i]=candidates_[random()%candidate_count_];
    if(order_==5) for(int i=candidate_count_-1;i>0;--i) std::swap(visual_.notes[i],visual_.notes[random()%unsigned(i+1)]);
    if(order_==2) for(int i=candidate_count_-2;i>0;--i) visual_.notes[visual_.count++]=candidates_[i];
}
void ArpUnit::control_tick() {
    int8_t notes[12]={}; int count=0;
    const int order=order_index(params[P_ORDER].target);
    for(int octave=0;octave<octaves(params[P_OCTAVES].target);++octave)
        for(int p=P_NOTE1;p<=P_NOTE4;++p){
            const int note=note_semitones(params[p].target);
            if(note!=NOTE_OFF)notes[count++]=int8_t(note+octave*12);
        }
    if(order!=3) {
        // Bounded insertion sort (12 notes), no library sort/heap dependency.
        for(int i=1;i<count;++i){int8_t n=notes[i];int j=i;while(j&&notes[j-1]>n){notes[j]=notes[j-1];--j;}notes[j]=n;}
        if(order==1)std::reverse(notes,notes+count);
    }
    const int rate=rate_index(params[P_RATE].target);
    const bool changed=!configured_ || count!=candidate_count_ || order!=order_ ||
        std::memcmp(notes,candidates_,size_t(count))!=0;
    if(rate!=rate_)phase_remainder_=0;
    rate_=rate; order_=order;
    for(int p=0;p<P_COUNT;++p)config_[p]=params[p].target;
    if(changed){
        candidate_count_=uint8_t(count);std::copy(notes,notes+count,candidates_);
        if(started_ && count) {
            // Keep the accepted cycle/note/gate/deadlines intact while controls
            // move. Multiple edits collapse to the latest pool at the next edge.
            pending_cycle_=true;
        } else {
            if(!count)suspend(State::AllOff,visual_.target_slot);
            make_cycle();visual_.current=-1;pending_cycle_=false;
        }
    }
    configured_=true;
}
void ArpUnit::reset(){
    visual_=Visual{}; configured_=started_=voice_started_=restart_=pending_cycle_=false;
    rng_=0x41525031u; phase_remainder_=0;next_phase_=gate_phase_=0;
    control_tick(); restart_=false;
}
void ArpUnit::trigger(){if(reset_on_trigger(params[P_RESET].target))restart_=true;}
void ArpUnit::suspend(State state,int target){
    pending_cycle_=false;
    if(started_){
        started_=voice_started_=false;visual_.current=-1;visual_.semitone=0;
        rng_=0x41525031u;phase_remainder_=0;make_cycle();
    }
    visual_.active=visual_.gate=false;visual_.target_slot=int8_t(target);visual_.state=state;
}
uint64_t ArpUnit::interval(){
    const uint64_t numerator=(uint64_t(arp_numerator[rate_])<<32)+phase_remainder_;
    const uint64_t result=numerator/arp_denominator[rate_];
    phase_remainder_=uint32_t(numerator%arp_denominator[rate_]);return result;
}
bool ArpUnit::advance(uint64_t phase,int target){
    if(!candidate_count_){suspend(State::AllOff,target);return false;}
    if(!started_ || visual_.target_slot!=target || restart_){
        voice_started_=false; // trigger reset also rearms an exhausted PLAY legato voice
        if(!started_ || visual_.target_slot!=target || restart_)rng_=0x41525031u;
        make_cycle();visual_.current=-1;next_phase_=phase;phase_remainder_=0;
        started_=true;restart_=pending_cycle_=false;
    }
    visual_.active=true;visual_.state=State::Running;visual_.target_slot=int8_t(target);
    if(phase>=next_phase_){
        if(pending_cycle_){
            make_cycle();visual_.current=0;pending_cycle_=false;
        } else if(++visual_.current>=visual_.count){make_cycle();visual_.current=0;}
        visual_.semitone=visual_.notes[visual_.current];visual_.gate=true;++visual_.events;
        const uint64_t span=interval();
        gate_phase_=next_phase_+span*unsigned(gate_percent(params[P_GATE].effective))/100;
        next_phase_+=span;
        return true;
    }
    if(phase>=gate_phase_)visual_.gate=false;
    return false;
}
uint64_t ArpUnit::deadline()const{
    if(!visual_.active)return UINT64_MAX;
    return visual_.gate ? std::min(next_phase_,gate_phase_) : next_phase_;
}
} // namespace seance
