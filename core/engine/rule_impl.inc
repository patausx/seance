#include "engine.h"
#include "rule_dsp.h"
namespace seance {
RuleUnit::RuleUnit() {
    kind=UnitKind::Rule;
    add_param("RATE",4354);add_param("DENS",19660);add_param("ROOT",0,true);add_param("RANGE",32767);
    add_param("ATK",1920);add_param("REL",20800);add_param("MUT",6144);add_param("SPRD",22937);
    add_param("VOX",32767);add_param("LEVEL",10486);add_param("RULE",11565);add_param("SCALE",0);
    reset();control_tick();
}
uint32_t RuleUnit::period_frames(q15 rate,uint32_t beat) {
    const uint32_t raw=uint32_t(std::max(0,int(rate)));
    // Exact default .64 beat (320ms at120); endpoints .125 and4 beats.
    const uint32_t ratio=raw<=4354?8192u+uint32_t(uint64_t(raw)*(41943u-8192u)/4354u):
        41943u+uint32_t(uint64_t(raw-4354u)*(262144u-41943u)/(32767u-4354u));
    return std::max<uint32_t>(1,uint32_t((uint64_t(beat)*ratio+32768u)>>16));
}
int RuleUnit::transpose(q15 v) {return int((int64_t(std::max(-32767,int(v)))*24+(v<0?-16383:16383))/32767);}
uint8_t RuleUnit::rule_number(q15 v) {return uint8_t((std::max(0,int(v))*255+16383)/32767);}
uint8_t RuleUnit::scale_index(q15 v) {return uint8_t(std::max(0,int(v))*5/32768);}
uint8_t RuleUnit::voice_limit(q15 v) {return uint8_t(1+(std::max(0,int(v))*31+16383)/32767);}
uint32_t RuleUnit::attack_frames(q15 v) {return uint32_t(std::max(1,int(v)));}
uint32_t RuleUnit::release_frames(q15 v) {return uint32_t(std::max(1,int(v)*4));}
q15 RuleUnit::rule_value(uint8_t rule) {return q15((uint32_t(rule)*32767u+127u)/255u);}
bool RuleUnit::edit_rule(uint16_t rule) {
    if(rule>255)return false;
    const q15 raw=rule_value(uint8_t(rule));auto& p=params[P_RULE];
    if(!config_.phrase && p.base==raw)return false;
    config_.phrase=0;p.base=raw;
    // Discrete unmodulated table input must reach even an immediately following
    // edge. Do NOT snap(): it erases real step/audition/temp owners. Modulated
    // and overridden parameters retain ordinary resolve/control_tick authority.
    if(!p.temp_override && !p.performance_override && !p.audition_override && !p.step_lock_override &&
        (!p.mod_enabled || !p.mod_depth))p.target=p.effective=live_[P_RULE]=raw;
    return true;
}
bool RuleUnit::pending_row(uint16_t& row) const {
    if(pending_row_>=32768 || paused_ || released_ || !allowed() || !matches())return false;
    row=pending_row_;return true;
}
bool RuleUnit::edit_row(uint16_t row) {
    if(row>=32768)return false;
    uint16_t target=config_.seed_row;
    const bool continuing=started_ && score_.generation && !manual_ && !paused_ && !released_ && allowed() && matches();
    if(continuing)target=pending_row_<32768?pending_row_:score_.previous;
    const q15 raw=rule_value(rule_number(params[P_RULE].base));
    if(!config_.phrase && config_.seed_row==row && params[P_RULE].base==raw && target==row)return false;
    edit_rule(rule_number(params[P_RULE].base));config_.seed_row=row;
    pending_row_=continuing?row:32768;
    return true;
}
bool RuleUnit::set_config(const Config& c) {
    if(!valid_config(c))return false;
    if(c.seed!=config_.seed || c.seed_row!=config_.seed_row || c.phrase!=config_.phrase || c.source_root!=config_.source_root){config_=c;reset();}
    return true;
}
RuleUnit::Source RuleUnit::binding() const {
    Source out;if(sample_slot<0 || sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return out;
    const auto& b=trackr::synth::SampleBank::instance();const auto& s=b.slot(sample_slot);
    if(s.channels!=1 && s.channels!=2)return out;
    out.slot=sample_slot;out.generation=b.generation(sample_slot);out.frames=s.playable_frames();
    out.data=s.data.data();out.elements=s.data.size();out.channels=s.channels;return out;
}
bool RuleUnit::matches() const {
    const auto b=binding();return source_.frames>1 && source_.data && b.slot==source_.slot && b.generation==source_.generation &&
        b.frames==source_.frames && b.data==source_.data && b.elements==source_.elements && b.channels==source_.channels;
}
bool RuleUnit::allowed() const {
    return !bypass && (!engine_ || (unsigned(chain_)<NUM_CHAINS && unsigned(slot_)<UNITS_PER_CHAIN &&
        engine_->chains[int(chain_)].units[int(slot_)]==this && !engine_->chains[int(chain_)].mute &&
        engine_->chain_audio_channels(chain_)!=0));
}
void RuleUnit::reset() {
    for(auto& v:voices_){v={};}
    cancel_live_edits();source_={};score_.reset(config_.seed);visual_={};
    started_=manual_=paused_=source_blocked_=released_=false;countdown_=0;age_=0;steals_=0;
}
void RuleUnit::release(){cancel_live_edits();released_=true;for(int c=0;c<CELLS;++c)release_cell(c);}
void RuleUnit::stop() {cancel_live_edits();for(auto& v:voices_)v={};manual_=false;paused_=true;visual_.sounding=0;visual_.active=false;}
void RuleUnit::synchronize() {
    if(!allowed()){stop();return;}
    if(started_ && !matches()){reset();source_blocked_=true;}
}
void RuleUnit::control_tick() {
    synchronize();
    for(int p=0;p<P_COUNT;++p) {
        const auto& v=params[p];live_[p]=std::abs(int(v.target)-v.effective)<=3?v.target:v.effective;
        live_[p]=q15(p==P_ROOT?std::max(-32767,int(live_[p])):std::max(0,int(live_[p])));
    }
}
bool RuleUnit::start() {
    if(!allowed() || source_blocked_)return false;
    const auto b=binding();if(!b.data || b.frames<2)return false;
    source_=b;started_=true;paused_=false;countdown_=0;return true;
}
void RuleUnit::trigger() {
    // Repeated flow/sequence trigs are an explicit score reset, not layered
    // phrases. One stopped manual generation is bounded by source/release ends.
    reset();control_tick();if(!start())return;manual_=engine_ && !engine_->transport_playing;
}
void RuleUnit::release_cell(int cell) {
    for(auto& v:voices_)if(v.active && !v.releasing && v.cell==cell) {
        v.releasing=true;v.remaining=v.release;
    }
}
int RuleUnit::select_voice(int limit) {
    for(int i=0;i<limit;++i)if(!voices_[i].active)return i;
    const auto quiet=[](const Voice& v){
        const int64_t attack=std::min<int64_t>(1ll<<30,int64_t(v.age)*v.attack_step);
        int64_t amplitude=(v.envelope*attack)>>30;
        amplitude=(amplitude*v.velocity)>>15;return (amplitude*v.pan)>>15;
    };
    int best=-1;int64_t best_amplitude=INT64_MAX;
    for(int i=0;i<limit;++i)if(voices_[i].releasing){const int64_t amplitude=quiet(voices_[i]);
        if(best<0 || amplitude<best_amplitude || (amplitude==best_amplitude && voices_[i].birth<voices_[best].birth)){
            best=i;best_amplitude=amplitude;
        }
    }
    if(best<0){best=0;for(int i=1;i<limit;++i)if(voices_[i].birth<voices_[best].birth)best=i;}
    ++steals_;return best;
}
void RuleUnit::launch(uint8_t cell,bool lone) {
    static constexpr int scales[5][5]={{0,2,3,7,8},{0,2,4,7,9},{0,2,3,7,10},{0,3,5,7,10},{0,1,2,3,4}};
    const int scale=scale_index(live_[P_SCALE]);
    const int off=-12+12*(cell/5)+scales[scale][cell%5]; // actual maximum +20, not prose+19
    const int note=transpose(live_[P_ROOT])+int((int64_t(off)*live_[P_RANGE])/32767);
    Voice& v=voices_[select_voice(voice_limit(live_[P_VOICES]))];v={};
    v.active=true;v.cell=int8_t(cell);v.birth=age_;v.birth_generation=score_.generation-1;v.attack=attack_frames(live_[P_ATTACK]);v.release=release_frames(live_[P_RELEASE]);
    v.attack_step=uint32_t(((1ull<<30)+v.attack/2)/v.attack);v.envelope=1ll<<30;
    // Transcendentals only on note/generation boundaries, never sample loops.
    v.increment=uint64_t(std::pow(2.0,double(note)/12.0)*4294967296.0+0.5);
    v.decay=uint32_t(std::exp(-6.9/double(v.release))*double(1ull<<30)+0.5);
    v.velocity=lone?32768:18022;
    const double pan=(double(cell)/14.0*2.0-1.0)*double(live_[P_SPREAD])/32767.0;
    v.pan=uint16_t((std::sqrt((1.0-pan)/2.0)+std::sqrt((1.0+pan)/2.0))*16384.0+0.5);
}
void RuleUnit::generation() {
    if(score_.rng.failed)return;
    const uint32_t G=score_.generation;const uint32_t section=(G/64)%3;
    const uint64_t reference=section==0?5404319552844595ull:section==1?3783023686991217ull:4503599627370496ull;
    // Density's default is an exact anchor, retaining .6/.42/.5 even though
    // those probabilities cannot all be represented by a raw q15 number.
    const uint32_t d=uint32_t(live_[P_DENSITY]);
    const auto scale53=[](uint64_t base,uint32_t n,uint32_t den){return (base/den)*n+(base%den)*n/den;};
    const uint64_t density=config_.phrase?(d<=19660?scale53(reference,d,19660u):
        reference+scale53((1ull<<53)-reference,d-19660u,32767u-19660u)):scale53(1ull<<53,d,32767u);
    const uint32_t mutation=uint32_t(live_[P_MUTATE])*64u/32767u;
    score_.advance(config_.phrase,rule_number(live_[P_RULE]),config_.seed_row,mutation,density,pending_row_);
    cancel_live_edits();
    if(score_.rng.failed){stop();visual_.fault=true;return;}
    // Section boundary releases ALL open voices, including currently alive cells.
    if(config_.phrase && G && G%64==0)for(int c=0;c<CELLS;++c)release_cell(c);
    for(int c=0;c<CELLS;++c)if(!(score_.row&(1u<<c)))release_cell(c);
    const int limit=voice_limit(live_[P_VOICES]);for(int i=limit;i<MAX_VOICES;++i)voices_[i]={};
    uint16_t born=0;
    for(int i=0;i<score_.count;++i){const uint8_t c=score_.accepted[i];born|=uint16_t(1u<<c);
        const bool lone=!(score_.row&(1u<<((c+14)%15))) && !(score_.row&(1u<<((c+1)%15)));
        launch(c,lone);
    }
    for(int i=RINGS-1;i>0;--i){visual_.rows[i]=visual_.rows[i-1];visual_.born[i]=visual_.born[i-1];}
    visual_.rows[0]=score_.row;visual_.born[0]=born;visual_.rings=uint8_t(std::min(RINGS,int(visual_.rings)+1));
    visual_.generation=G;visual_.serial=++serial_;visual_.rule=score_.rule;visual_.scale=scale_index(live_[P_SCALE]);
    visual_.accepted_count=score_.count;for(int i=0;i<3;++i)visual_.accepted[i]=i<score_.count?score_.accepted[i]:0;
    std::copy(live_,live_+P_COUNT,visual_.effective);
    period_=period_frames(live_[P_RATE],engine_?engine_->frames_per_beat():16000u);countdown_=period_;
    visual_.period_frames=period_;visual_.period_ms=(period_*1000u)/ENGINE_SR;
    score_.finish();
}
RuleUnit::Visual RuleUnit::visual() const {
    Visual v=visual_;const auto b=binding();v.loaded=b.frames>1 && b.data;v.sample_slot=b.slot;v.source_generation=b.generation;v.source_frames=b.frames;
    v.source_root=config_.source_root;v.paused=paused_;v.steals=steals_;v.phase_frames=period_-std::min(period_,countdown_);
    v.phase_q8=uint16_t(uint64_t(v.phase_frames)*256u/std::max<uint32_t>(1,period_));
    v.push_q8=uint16_t(std::min<uint32_t>(256,uint32_t(uint64_t(v.phase_frames)*256u/(ENGINE_SR/10))));
    if(!allowed() || !matches()){v.active=false;v.sounding=0;v.voices=0;std::fill(v.spoke,v.spoke+CELLS,uint8_t(0));}
    return v;
}
void RuleUnit::process(q15* out,int frames) {
    if(!out || frames<=0)return;
    synchronize();
    const bool running=!engine_ || engine_->transport_playing;
    if(!allowed() || (!running && !manual_) || source_blocked_){std::fill(out,out+frames,q15(0));return;}
    if(!started_ && !start()){std::fill(out,out+frames,q15(0));return;}
    if(manual_ && running){reset();control_tick();if(!start()){std::fill(out,out+frames,q15(0));return;}}
    if(running)paused_=false;
    for(int n=0;n<frames;++n) {
        if(!released_ && !countdown_ && (running || !score_.generation))generation();
        int64_t sum=0;uint16_t sounding=0;uint8_t count=0;std::fill(visual_.spoke,visual_.spoke+CELLS,uint8_t(0));
        for(auto& v:voices_)if(v.active) {
            const uint32_t at=uint32_t(v.position>>32),frac=uint32_t(v.position);
            if(at>=source_.frames-1 || (v.releasing && !v.remaining)){v.active=false;continue;}
            auto read=[&](uint32_t i){return source_.channels==2?(int32_t(source_.data[size_t(i)*2])+source_.data[size_t(i)*2+1])/2:int32_t(source_.data[i]);};
            const int32_t a=read(at),b=read(at+1);const int32_t sample=a+int32_t((int64_t(b-a)*frac)>>32);
            const int64_t attack=std::min<int64_t>(1ll<<30,int64_t(v.age)*v.attack_step);
            const int64_t envelope=(attack*v.envelope)>>30;
            int64_t value=(int64_t(sample)*envelope)>>30;value=(value*v.velocity)>>15;value=(value*v.pan)>>15;sum+=value;
            if(!v.releasing){sounding|=uint16_t(1u<<v.cell);visual_.spoke[int(v.cell)]=uint8_t(std::min<uint32_t>(RINGS,score_.generation-v.birth_generation));}
            ++count;v.position+=v.increment;++v.age;
            if(v.releasing){v.envelope=(v.envelope*v.decay)>>30;--v.remaining;}
            // Manual stopped generation owns finite tails rather than an
            // indefinite CA gate. Death is one nominal period after its onset.
            if(manual_ && v.age>=period_ && !v.releasing){v.releasing=true;v.remaining=v.release;}
        }
        out[n]=trackr::fx::sat_q15(int32_t((sum*std::max(0,int(live_[P_LEVEL])))>>15));
        visual_.sounding=sounding;visual_.voices=count;visual_.active=count>0;visual_.fault=score_.rng.failed;
        visual_.effective[P_LEVEL]=live_[P_LEVEL]; // actual live gain, other score controls are generation-latched
        if(countdown_ && running)--countdown_;
        ++age_;
    }
}
}
