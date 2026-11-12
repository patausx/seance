// Included once outside namespace by units.cpp. Selected components are immutable.
#include "relic_unit.h"
#include <atomic>
#include <algorithm>
#include <new>
namespace seance {
namespace relic_unit_detail {
static std::atomic<uint32_t> identity{0};
static uint32_t fresh() {
    uint32_t old=identity.load();
    for(;;) {const uint32_t next=old==UINT32_MAX?1:old+1;if(identity.compare_exchange_weak(old,next))return next;}
    // Project-local identities may coincide across projects; Engine admission and
    // outbound cleanup prevent live/parked ambiguity. Gesture leases never reuse.
}
}
RelicUnit::RelicUnit() {
    kind=UnitKind::Relic;instance_id_=relic_unit_detail::fresh();
    add_param("GAIN",32767);add_param("DEPTH",32767);add_param("START",0);add_param("LENGTH",32767);
}
bool RelicUnit::restore_instance_id(uint32_t id) {
    if(!id)return false;
    uint32_t old=relic_unit_detail::identity.load();
    while(old<id && !relic_unit_detail::identity.compare_exchange_weak(old,id)) {}
    instance_id_=id;return true;
}
q15 RelicUnit::port_value(const relic::Features& f,relic::Port p) {
    float v=0;
    switch(p){case relic::Port::Pull:v=f.pull;break;case relic::Port::Compress:v=f.compress;break;
    case relic::Port::Bend:v=f.bend;break;case relic::Port::Wave:v=f.wave;break;default:return 0;}
    v=std::max(-1.f,std::min(1.f,v));return q15(v*32767.f);
}
relic::Source RelicUnit::source_binding() const {
    relic::Source s;
    if(!hydrated_ || sample_slot<0 || sample_slot>=trackr::synth::SAMPLE_BANK_SIZE)return s;
    const auto& b=trackr::synth::SampleBank::instance();const auto& pcm=b.slot(sample_slot);
    s.generation=b.generation(sample_slot);
    const uint32_t n=pcm.playable_frames();
    if(!n || (pcm.channels!=1 && pcm.channels!=2) || pcm.data.size()<size_t(n)*pcm.channels)return s;
    s.data=pcm.data.data();s.elements=pcm.data.size();s.frames=n;s.channels=pcm.channels;
    const uint32_t a=uint16_t(std::max<q15>(0,params[P_START].effective));
    const uint32_t l=uint16_t(std::max<q15>(0,params[P_LENGTH].effective));
    s.first=uint32_t(uint64_t(n)*a/32767);
    s.end=s.first+uint32_t(uint64_t(n-s.first)*l/32767);
    return s;
}
void RelicUnit::synchronize() {
    const auto next=source_binding();
    if(bound_slot_!=sample_slot || source_.data!=next.data || source_.elements!=next.elements ||
        source_.frames!=next.frames || source_.channels!=next.channels || source_.generation!=next.generation ||
        source_.first!=next.first || source_.end!=next.end) {
        // Audio retires changed identity itself BEFORE any tap. Initial binding
        // retains its already-zero tank and the selected initial FDN arithmetic.
        if(bound_slot_>=0 && bound_slot_!=sample_slot)audio_.detach_source();
        source_=next;bound_slot_=sample_slot;audio_.set_source(next);
    }
}
void RelicUnit::detach_source(){audio_.detach_source();source_={};bound_slot_=-1;fragment_frames_=0;}
void RelicUnit::stage_source(){model_.cancel();detach_source();hydrated_=false;manual_run_=false;}
void RelicUnit::hydrate_source(){detach_source();hydrated_=true;synchronize();}
bool RelicUnit::retarget_source(int slot) {
    if(slot<0 || slot>=trackr::synth::SAMPLE_BANK_SIZE)return false;
    model_.cancel();detach_source();sample_slot=slot;hydrated_=true;synchronize();return true;
}
void RelicUnit::context(bool transport,bool enabled,bool pipe) {
    transport_=transport;enabled_=enabled;pipe_=pipe;
    if(!enabled_)model_.cancel();
    synchronize();control_tick();
}
void RelicUnit::control_tick() {
    synchronize();
    const q15 depth=std::max<q15>(0,params[P_DEPTH].effective);
    audio_.set_shape(model_.features(),depth?float(depth)/32767.f:1.f,
        playback_requested() && enabled_ && pipe_ && own_audio_enabled,depth>0);
}
void RelicUnit::trigger(){if(!bypass)manual_run_=true;control_tick();}
void RelicUnit::stop(){manual_run_=false;transport_=false;model_.cancel();control_tick();}
void RelicUnit::reset(){fragment_frames_=0;model_.reset_runtime();audio_.reset();manual_run_=false;transport_=false;bank_epoch_=0;bank_revision_=0;for(auto& p:bank_ports_)p=0;}
void RelicUnit::process(q15* b,int n){if(b && n>0)std::fill_n(b,n,q15(0));}
void RelicUnit::render_fragment(uint32_t frames) {
    fragment_frames_=0;
    if(!frames || frames>CONTROL_INTERVAL)return;
    synchronize();
    for(uint32_t done=0;done<frames;) {
        // A new/reset source need not share the engine's32 origin. Step exact
        // material320 edges INSIDE this source, never split global ENV/DSP blocks.
        const uint32_t distance=relic::MECHANICS_FRAMES-uint32_t(model_.surface().sample_clock%relic::MECHANICS_FRAMES);
        const uint32_t n=std::min(frames-done,distance);
        control_tick();
        if(enabled_ && pipe_ && !bypass)audio_.render(fragment_+done*2,int(n));
        else std::fill_n(fragment_+done*2,n*2,q15(0));
        model_.advance(n);done+=n;
    }
    const int gain=std::max<int>(0,params[P_GAIN].effective);
    if(!own_audio_enabled || !enabled_ || !pipe_ || bypass)std::fill_n(fragment_,frames*2,q15(0));
    else if(gain!=32767)for(uint32_t i=0;i<frames*2;++i)fragment_[i]=q15(int32_t(fragment_[i])*gain/32767);
    fragment_frames_=uint8_t(frames);
}
bool RelicUnit::process_stereo(q15* b,int n) {
    // Actual Engine consumes the source prepass ONCE. A standalone wrong entry
    // has no owned sample timeline and must refuse, not rerender/fold/advance.
    if(!b || n<=0 || n!=fragment_frames_){if(b && n>0)std::fill_n(b,size_t(n)*2,q15(0));return false;}
    std::copy_n(fragment_,size_t(n)*2,b);fragment_frames_=0;return true;
}
void RelicUnit::capture_visual(Visual& out) const {
    ::new(static_cast<void*>(&out)) Visual{};
    out.surface=model_.surface();audio_.snapshot(out.audio);out.instance_id=instance_id_;
    out.valid=ready();out.own_audio_enabled=own_audio_enabled;out.contributing=enabled_;
    out.pipe_admitted=pipe_;out.control_epoch=bank_epoch_;out.bank_revision=bank_revision_;
    out.output_gain=std::max<q15>(0,params[P_GAIN].effective);
    out.sound_requested=playback_requested() && enabled_ && pipe_ && own_audio_enabled;
    out.sound_running=enabled_ && pipe_ && own_audio_enabled && out.output_gain>0 && out.audio.running;
    for(int p=0;p<4;++p)out.ports[p]=bank_ports_[p];
}
} // namespace seance
