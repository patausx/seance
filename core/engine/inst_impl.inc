// Included by units.cpp outside namespace seance, like arp_impl.inc.
// PCM and zone descriptors have one internal-linkage, read-only definition here.
#include "inst_unit.h"
#include "inst_scope.h"
#include "inst_bank_data.h"

namespace seance {
namespace {
constexpr int inst_note_min=24, inst_note_max=96;
constexpr uint32_t inst_env_one=65536, inst_declick_frames=64;
constexpr int inst_level_frames=128;
static_assert(ENGINE_SR==32000 && inst_bank::SAMPLE_RATE==24000, "inst rate ratio is exactly 3/4");
static_assert(InstUnit::SAMPLE_INSTRUMENT_COUNT==6 && InstUnit::INSTRUMENT_COUNT==7 &&
              InstUnit::VOICE_COUNT==4 && InstUnit::MODEL_VOICE_COUNT==2, "inst bounded bank/model contracts");
static_assert(inst_bank::ZONE_COUNT==22, "inst bank zones must match the fixed descriptors");

int inst_clamp(int value, int lo, int hi) { return value<lo ? lo : value>hi ? hi : value; }
int inst_positive(q15 value) { return value<0 ? 0 : int(value); }
int inst_index(q15 value, int count) { return (inst_positive(value)*(count-1)+16383)/32767; }
q15 inst_value(int index, int count) { return q15((inst_clamp(index,0,count-1)*32767+(count-1)/2)/(count-1)); }
unsigned inst_time(q15 value, unsigned low, unsigned high) {
    const uint64_t v=unsigned(inst_positive(value));
    return low+unsigned((v*v*(high-low)+536838144u)/1073676289u); // 32767^2, nearest ms
}
q15 inst_time_value(unsigned ms, unsigned low, unsigned high) {
    if(ms<=low)return 0;
    if(ms>=high)return 32767;
    // Monotone inverse; every integer ms in these ranges is representable.
    // UI/construction helper only, never used by the per-sample path.
    unsigned a=0,b=32767;
    while(a<b){const unsigned middle=(a+b)/2;if(inst_time(q15(middle),low,high)<ms)a=middle+1;else b=middle;}
    return q15(a);
}
const char* const inst_names[] = {"piano","violin","brass","flute","bass","drums","vibes"};
const char* const inst_drum_names[] = {"kick","snare","hat","tom","openhat","cymbal"};
constexpr int inst_drum_notes[]={36,38,42,45,46,49};

const inst_bank::Zone* inst_zone(int instrument, int note) {
    const int first=inst_bank::FAMILY_FIRST[instrument], count=inst_bank::FAMILY_COUNT[instrument];
    for(int i=0;i<count-1;++i)if(note<=inst_bank::ZONES[first+i].high)return &inst_bank::ZONES[first+i];
    return &inst_bank::ZONES[first+count-1];
}
uint32_t inst_increment(int semitones) {
    // Immutable equal-temperament ratios. Unlike Unit::arp_ratio_q24 (limited to
    // -24st), the zone-relative range includes notes below the flute's register.
    static constexpr uint32_t ratio[12]={16777216,17774841,18831788,19951585,21137968,22394897,
        23726566,25137421,26632170,28215802,29893600,31671166};
    semitones=inst_clamp(semitones,-72,72);
    int octave=semitones/12, note=semitones%12;
    if(note<0){note+=12;--octave;}
    const unsigned shift=unsigned(10-octave); // Q24 -> Q16 and source/engine = 3/4
    return (ratio[note]*3u+(1u<<(shift-1)))>>shift;
}
}

int InstUnit::instrument_index(q15 value){return inst_index(value,INSTRUMENT_COUNT);}
q15 InstUnit::instrument_value(int index){return inst_value(index,INSTRUMENT_COUNT);}
const char* InstUnit::instrument_name(int index){return inst_names[inst_clamp(index,0,INSTRUMENT_COUNT-1)];}
int InstUnit::note_midi(q15 value){return inst_note_min+inst_index(value,inst_note_max-inst_note_min+1);}
q15 InstUnit::note_value(int midi){return inst_value(inst_clamp(midi,inst_note_min,inst_note_max)-inst_note_min,inst_note_max-inst_note_min+1);}
int InstUnit::drum_index(int midi){
    for(int i=0;i<DRUM_COUNT-1;++i)if(midi<=(inst_drum_notes[i]+inst_drum_notes[i+1])/2)return i;
    return DRUM_COUNT-1;
}
int InstUnit::drum_note(int index){return inst_drum_notes[inst_clamp(index,0,DRUM_COUNT-1)];}
const char* InstUnit::drum_name(int index){return inst_drum_names[inst_clamp(index,0,DRUM_COUNT-1)];}
unsigned InstUnit::length_ms(q15 value){return inst_time(value,20,4000);}
unsigned InstUnit::attack_ms(q15 value){return inst_time(value,0,1000);}
unsigned InstUnit::release_ms(q15 value){return inst_time(value,5,3000);}
q15 InstUnit::length_value(unsigned ms){return inst_time_value(ms,20,4000);}
q15 InstUnit::attack_value(unsigned ms){return inst_time_value(ms,0,1000);}
q15 InstUnit::release_value(unsigned ms){return inst_time_value(ms,5,3000);}
unsigned InstUnit::decay_ms(q15 value){return inst_time(value,120,6000);}
q15 InstUnit::decay_value(unsigned ms){return inst_time_value(ms,120,6000);}

InstUnit::InstUnit(){
    kind=UnitKind::Inst;
    add_param("INST",instrument_value(MODEL_INSTRUMENT)); add_param("NOTE",note_value(60));
    add_param("LEN",length_value(4000)); add_param("ATK",attack_value(1));
    add_param("REL",release_value(800)); add_param("BODY",0); add_param("LEVEL",32767);
    add_param("DECAY",decay_value(2300)); add_param("HARD",18000); add_param("POS",11000);
    add_param("DAMP",15000); add_param("STRENGTH",26000);
    reset();
}
InstUnit::~InstUnit(){inst_scope::recorder.release(this);}
void InstUnit::reset(){
    inst_scope::recorder.reset(this);
    for(auto& voice:voices_)voice=Voice{};
    strikes_=0;steal_cursor_=0;model_strikes_=0;modal_.reset();
    level_current_=level_target_=inst_positive(params[P_LEVEL].target)*65536;
    level_step_=0;level_error_=level_remainder_=level_left_=0;level_sign_=0;
}
void InstUnit::control_tick(){
    params[P_TONE].name=modeled()?"BODY":"TONE";
    // Read resolved TARGET, not the preceding one-pole Param slew. INST owns its
    // exact sample-clock level ramp, including a no-jump mid-ramp retarget.
    const int32_t target=inst_positive(params[P_LEVEL].target)*65536;
    if(target==level_target_)return;
    level_target_=target;
    const int32_t delta=target-level_current_;
    level_step_=delta/inst_level_frames;
    const int32_t remainder=delta%inst_level_frames;
    level_remainder_=uint16_t(remainder<0 ? -remainder : remainder);
    level_sign_=delta<0 ? -1 : 1;
    level_error_=0;level_left_=inst_level_frames;
}
void InstUnit::advance_level(){
    if(!level_left_)return;
    level_current_+=level_step_;
    level_error_=uint16_t(level_error_+level_remainder_);
    if(level_error_>=inst_level_frames){level_current_+=level_sign_;level_error_-=inst_level_frames;}
    if(!--level_left_)level_current_=level_target_;
}
void InstUnit::trigger(){
    control_tick();
    const int instrument=instrument_index(params[P_INSTRUMENT].target);
    // Arp transposition is consumed ONCE, on this edge. Engine owns the arp gate.
    // Legato does not repitch an accepted acoustic recording or retune old tails.
    const int note=inst_clamp(note_midi(params[P_NOTE].target)+int(arp_voice.semitones),inst_note_min,inst_note_max);
    if(instrument==MODEL_INSTRUMENT){
        inst_modal::Instrument::Strike strike;
        strike.note=note;
        strike.body=uint16_t(inst_positive(params[P_BODY].target));
        strike.decay_ms=decay_ms(params[P_DECAY].target);
        strike.hardness=uint16_t(inst_positive(params[P_HARDNESS].target));
        strike.position=uint16_t(inst_positive(params[P_POSITION].target));
        strike.damping=uint16_t(inst_positive(params[P_DAMPING].target));
        strike.strength=uint16_t(inst_positive(params[P_STRENGTH].target));
        strike.length_ms=length_ms(params[P_LENGTH].target);
        strike.attack_ms=attack_ms(params[P_ATTACK].target);
        strike.release_ms=release_ms(params[P_RELEASE].target);
        modal_.strike(strike);++strikes_;++model_strikes_;
        return;
    }
    const inst_bank::Zone* zone=inst_zone(instrument,note);
    int slot=-1;
    for(int i=0;i<VOICE_COUNT;++i)if(voices_[i].stage==Stage::Off){slot=i;break;}
    if(slot<0){
        slot=steal_cursor_;
        for(int k=1;k<VOICE_COUNT;++k){
            const int candidate=(steal_cursor_+k)&(VOICE_COUNT-1);
            const Voice& a=voices_[candidate];const Voice& b=voices_[slot];
            const bool ar=a.stage==Stage::Release, br=b.stage==Stage::Release;
            // Quietest releasing tail first; otherwise oldest strike. Cursor
            // breaks exact ties, including many triggers without an audio frame.
            if((ar&&!br) || (ar==br && ((ar&&a.envelope<b.envelope) ||
                ((!ar||a.envelope==b.envelope)&&a.age>b.age))))slot=candidate;
        }
    }
    const bool steal=voices_[slot].stage!=Stage::Off;
    const int32_t old_output=voices_[slot].last_output;
    Voice& v=voices_[slot];v=Voice{};
    v.zone=zone;v.instrument=uint8_t(instrument);v.note=uint8_t(note);
    v.increment=instrument==5 ? 49152u : inst_increment(note-int(zone->root));
    v.attack_frames=attack_ms(params[P_ATTACK].target)*32u;
    if(v.attack_frames<32)v.attack_frames=32; // 1ms click protection, even at ATK=0
    // Drums are attack -> release one-shots: LENGTH is inert, including locks.
    // Do not even read its target for this family; finite PCM may end earlier.
    v.hold_frames=instrument==5 ? v.attack_frames : length_ms(params[P_LENGTH].target)*32u;
    v.release_frames=release_ms(params[P_RELEASE].target)*32u;
    v.attack_step=inst_env_one/v.attack_frames;v.attack_remainder=inst_env_one%v.attack_frames;
    const uint32_t release_start=v.hold_frames>=v.attack_frames ? inst_env_one :
        uint32_t(uint64_t(v.hold_frames)*inst_env_one/v.attack_frames);
    v.release_step=release_start/v.release_frames;v.release_remainder=release_start%v.release_frames;
    const uint64_t tone=unsigned(inst_positive(params[P_TONE].target));
    v.tone_coefficient=uint16_t(256u+(tone*tone*32512u+536838144u)/1073676289u);
    if(!zone->loop_end)v.end_age=uint32_t(((uint64_t(zone->frames)<<16)+v.increment-1)/v.increment);
    v.stage=Stage::Attack;
    if(steal){v.steal_output=old_output;v.steal_left=inst_declick_frames;}
    steal_cursor_=uint8_t((slot+1)&(VOICE_COUNT-1));
    ++strikes_;
}
void InstUnit::process(q15* buf, int frames){
    if(!buf || frames<=0)return;
    // Engine bypass skips this function, exactly as it does for other sources.
    // Four sample tails plus two modal tails remain independent across source
    // changes. No heap, filesystem, lock, coefficient building or float here.
    for(int frame=0;frame<frames;++frame){
        advance_level();
        int32_t mixed=0;
        for(auto& v:voices_){
            if(v.stage==Stage::Off)continue;
            const inst_bank::Zone& z=*v.zone;
            const int16_t* const data=inst_bank::PCM+z.offset;
            const int32_t a=data[v.position], b=data[v.position+1]; // final point has a prepared guard
            const int32_t input=a+int32_t((int64_t(b-a)*v.fraction)>>16);
            v.tone_state+=((input-v.tone_state)*int32_t(v.tone_coefficient))>>15;
            int32_t output=(v.tone_state*int32_t(v.envelope))>>16;
            if(v.end_age){
                const uint32_t remaining=v.end_age-v.age;
                if(remaining<=inst_declick_frames)output=(output*int32_t(remaining-1))>>6;
            }
            if(v.steal_left){
                // Scalar continuity tail, NOT a fifth sampler voice. A convex
                // 2ms crossfade cannot exceed either contributing voice's peak.
                output=(v.steal_output*v.steal_left+output*int32_t(inst_declick_frames-v.steal_left))>>6;
                --v.steal_left;
            }
            v.last_output=output;mixed+=output;
            const uint32_t next=v.fraction+v.increment;
            v.position+=next>>16;v.fraction=next&65535u;
            if(z.loop_end && v.position>=z.loop_end)v.position-=z.loop_end-z.loop_start;
            // Even the largest allowed zone-relative increment advances <8
            // source frames. Every loop is >14000 frames: one subtract suffices.
            if(v.stage==Stage::Attack){
                v.envelope+=v.attack_step;v.envelope_error+=v.attack_remainder;
                if(v.envelope_error>=v.attack_frames){++v.envelope;v.envelope_error-=v.attack_frames;}
            }else if(v.stage==Stage::Release){
                uint32_t decrement=v.release_step;v.envelope_error+=v.release_remainder;
                if(v.envelope_error>=v.release_frames){++decrement;v.envelope_error-=v.release_frames;}
                v.envelope=v.envelope>decrement ? v.envelope-decrement : 0;
            }
            ++v.age;
            if((v.end_age && v.age>=v.end_age) || v.age>=v.hold_frames+v.release_frames){v=Voice{};continue;}
            if(v.stage!=Stage::Release && v.age>=v.hold_frames){v.stage=Stage::Release;v.envelope_error=0;}
            else if(v.stage==Stage::Attack && v.age>=v.attack_frames){
                v.stage=Stage::Sustain;v.envelope=inst_env_one;v.envelope_error=0;
            }
        }
        // Samples keep their four-tail scaling. A further fixed 6dB reserve
        // covers simultaneous old sample + new model tails without a limiter,
        // wraparound, backend-dependent gain jump or implicit voice cancellation.
        const int32_t combined=(mixed>>2)+int32_t(modal_.next());
        buf[frame]=q15((int64_t(combined)*level_current_)>>32);
    }
    // Read-only post-block observer; disarmed path is constant work.
    inst_scope::recorder.write(this,buf,frames);
}
InstUnit::Visual InstUnit::visual()const{
    Visual result{};result.strikes=strikes_;
    for(int i=0;i<VOICE_COUNT;++i){
        const Voice& v=voices_[i];
        VoiceVisual& out=result.voices[i];
        out.age=v.age;out.instrument=v.instrument;out.note=v.note;out.stage=uint8_t(v.stage);
        out.envelope=uint16_t(v.envelope>=inst_env_one ? 32767 : v.envelope>>1);
        if(v.stage!=Stage::Off)++result.active;
    }
    return result;
}
InstUnit::ModelVisual InstUnit::model_visual()const{
    ModelVisual result{};result.strikes=model_strikes_;
    for(int i=0;i<MODEL_VOICE_COUNT;++i){
        const auto voice=modal_.info(i);auto& out=result.voices[i];
        out.age=voice.age;out.end=voice.end;out.envelope=voice.envelope;
        out.note=uint8_t(voice.note);out.stage=voice.stage;out.active=voice.active;
        out.decay_ms=voice.decay_ms;out.damping=voice.damping;out.hardness=voice.hardness;
        out.position=voice.position;out.body=voice.body;out.strength=voice.strength;
        if(voice.active)++result.active;
    }
    return result;
}
} // namespace seance
