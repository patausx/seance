// Focused PLAY core regression, no UI/device dependencies.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/play_window.h"
#include "engine/play_mode.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
using namespace seance;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
// Literal snapshots: pre-play units.h/units.cpp/play_window.h, 20260909_114138.
// Deliberately independent from current PLAY and current projection helper.
namespace old_play {
using namespace seance;
using trackr::fx::Q15_ONE;
using trackr::fx::lerp_q15;
static uint32_t playable_frames(const Sample& s) { return s.playable_frames(); }
struct PlayWindow {
    bool valid = false;
    uint32_t start = 0, end = 0; // source frames, end exclusive
    int32_t increment = 1024;   // signed source frames per output frame, q16
    uint8_t slice = 0;          // applied chop 1..16; 0 means STRT/LEN fallback
    // Same endpoint mapping as PLAY telemetry: end maps the final source frame.
    uint16_t normalized_start = 0, normalized_end = 0;

    // Nominal full-window traversal at 32 kHz, truncated to milliseconds. This is
    // an estimated ONE PASS, not a gate, sequencer step length, or voice lifetime.
    // Loop/retrigger, edge interpolation, modulation and later locks can change
    // audible lifetime; therefore loop is intentionally not an input.
    uint64_t traversal_ms() const {
        if (!valid || increment == 0) return 0;
        const uint32_t magnitude = (uint32_t)(increment < 0 ? -increment : increment);
        return (uint64_t)(end - start) * 65536u * 1000u /
               ((uint64_t)magnitude * trackr::synth::SAMPLER_SR);
    }
};

namespace play_window_detail {
inline int32_t quantize_speed(int32_t v) {
    if (v == 0) return 0;
    const bool neg = v < 0;
    uint32_t mag = (uint32_t)(neg ? -v : v);
    uint32_t inc = mag * 8u;
    if (inc < 1024) inc = 1024;
    // Exact original integer candidates and strict tie rule. In particular the
    // 262136 ceiling excludes the mathematical +24st ratio 262144.
    static const uint32_t SEMI_Q16[12] = {
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98199, 104032, 110218, 116773, 123715
    };
    uint32_t best = 1024;
    uint32_t best_diff = inc > best ? inc - best : best - inc;
    for (int semi = -72; semi <= 24; ++semi) {
        int oct = semi / 12;
        int rem = semi % 12;
        if (rem < 0) { rem += 12; --oct; }
        uint32_t ratio = SEMI_Q16[rem];
        if (oct >= 0) ratio <<= oct;
        else          ratio >>= -oct;
        if (ratio < 1024 || ratio > 262136) continue;
        uint32_t diff = inc > ratio ? inc - ratio : ratio - inc;
        if (diff < best_diff) { best = ratio; best_diff = diff; }
    }
    return neg ? -(int32_t)best : (int32_t)best;
}
} // namespace play_window_detail

// Metadata overload supports bounded testing without allocating enormous PCM.
// chops points to Sample::MAX_CHOPS entries, or nullptr for no chop metadata.
// Unipolar controls normally lie in 0..32767. The uint16 casts and uint32 window
// arithmetic deliberately retain the legacy DSP behavior even for malformed
// controls/extreme frame counts; valid rejects windows process() cannot play.
inline PlayWindow play_window(uint32_t frames, const uint32_t* chops,
                              uint16_t reverse_mask, trackr::fx::q15 speed,
                              trackr::fx::q15 start, trackr::fx::q15 length,
                              trackr::fx::q15 slice_value, bool pitch_quantize) {
    PlayWindow out;
    int32_t v = speed;
    bool rev = v < 0;
    if (rev) v = -v;
    out.increment = pitch_quantize
        ? play_window_detail::quantize_speed(rev ? -v : v) : v * 8;
    if (out.increment < 0 ? -out.increment < 1024 : out.increment < 1024)
        out.increment = rev ? -1024 : 1024;
    // Preserve legacy ordering: unquantized -1..-127 ends up +1024. Do not turn
    // a projection extraction into an unrequested audible speed-floor fix.
    if (!pitch_quantize && rev) out.increment = -out.increment;
    if (frames < 2) return out;

    out.start = (uint32_t)((uint64_t)(uint16_t)start * (frames - 1) / 32767u);
    uint32_t len = (uint32_t)((uint64_t)(uint16_t)length * frames / 32767u);
    if (len < 2) len = 2;
    if (out.start > frames - 2) out.start = frames - 2;
    out.end = out.start + len;
    if (out.end > frames) out.end = frames;

    int slice = (int)(((uint32_t)(uint16_t)slice_value *
                      (trackr::synth::Sample::MAX_CHOPS + 1)) >> 15);
    if (slice > trackr::synth::Sample::MAX_CHOPS) slice = trackr::synth::Sample::MAX_CHOPS;
    if (slice > 0 && chops) {
        uint32_t a = chops[slice - 1];
        if (a != 0xFFFFFFFFu && a < frames) {
            uint32_t b = frames;
            for (int i = 0; i < trackr::synth::Sample::MAX_CHOPS; ++i) {
                uint32_t c = chops[i];
                if (c != 0xFFFFFFFFu && c > a && c < b) b = c;
            }
            if (b > a + 1) {
                out.start = a;
                out.end = b;
                out.slice = (uint8_t)slice;
                if (reverse_mask & (uint16_t)(1u << (slice - 1)))
                    out.increment = -out.increment;
            }
        }
    }
    out.valid = out.end <= frames && out.start + 1 < out.end;
    out.normalized_start = (uint16_t)((uint64_t)out.start * 32767u / (frames - 1));
    out.normalized_end = (uint16_t)((uint64_t)(out.end - 1) * 32767u / (frames - 1));
    return out;
}

inline PlayWindow play_window(const trackr::synth::Sample& sample,
                              trackr::fx::q15 speed, trackr::fx::q15 start,
                              trackr::fx::q15 length, trackr::fx::q15 slice,
                              bool pitch_quantize) {
    return play_window(sample.playable_frames(), sample.chops, sample.slice_reverse_mask,
                       speed, start, length, slice, pitch_quantize);
}

class FrozenPlay : public Unit {
public:
    FrozenPlay();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    SequencerProperties sequencer_properties() const override {
        return {true, false, SequenceLifecycle::Loop, P_SPEED, P_GAIN, -1, P_LEN};
    }
    void reset() override {
        playing_ = false;
        visual_position_ = visual_start_ = visual_end_ = 0;
        visual_speed_ = 0;
        visual_state_ = 0;
    }

    enum VisualState : uint8_t {
        VisualPlaying = 1, VisualReverse = 2, VisualLoop = 4
    };
    uint16_t visual_position() const { return visual_position_; }
    uint16_t visual_start() const { return visual_start_; }
    uint16_t visual_end() const { return visual_end_; }
    int16_t visual_speed() const { return visual_speed_; }
    uint8_t visual_state() const { return visual_state_; }
    bool playback_active() const { return playing_; }
    // Normalized progress inside the effective playback window. Reverse playback
    // still moves 0 -> 0xffffffff from trigger to end, matching audible lifetime.
    uint32_t playback_phase() const {
        if (!playing_ || win_end_ <= win_start_ + 1) return 0;
        const int64_t last = (int64_t)win_end_ - 2;
        const uint64_t span = last > (int64_t)win_start_
            ? (uint64_t)(last - (int64_t)win_start_) : 1u;
        uint64_t numerator;
        if (inc_q16_ < 0)
            numerator = pos_ >= last ? 0u : pos_ <= (int64_t)win_start_ ? span : (uint64_t)(last - pos_);
        else
            numerator = pos_ <= (int64_t)win_start_ ? 0u : pos_ >= last ? span : (uint64_t)(pos_ - win_start_);
        if (numerator >= span) return 0xffffffffu;
        return (uint32_t)((numerator << 32) / span);
    }

    int sample_slot = 0;    // SampleBank slot
    bool pitch_quantize = false; // snap playback speed to semitone ratios

    // param indices. SLCE: 0 = window, 1..16 = Sample::chops[n-1] .. next chop/end.
    enum { P_SPEED = 0, P_START, P_LEN, P_LOOP, P_GAIN, P_SLICE };

private:
    bool     playing_  = false;
    int64_t  pos_      = 0;         // frame index
    trackr::fx::uq16 frac_ = 0;
    int32_t  inc_q16_  = 1 << 16;   // signed varispeed
    uint32_t win_start_ = 0, win_end_ = 0;
    q15      amp_q15_  = 0;          // tiny declick ramp after trigger / loop wrap
    // Bounded audio->UI mailbox. Values are normalized to the current playable
    // sample and published only for the focused PLAY through EngineUiSnapshot.
    uint16_t visual_position_ = 0, visual_start_ = 0, visual_end_ = 0;
    int16_t visual_speed_ = 0;
    uint8_t visual_state_ = 0;
};

FrozenPlay::FrozenPlay() {
    kind = UnitKind::SamplePlayer;
    add_param("SPD",  8192, true);    // signed varispeed: +/-8192 ~= +/-1x, max ~= 4x
    add_param("STRT", 0);
    add_param("LEN",  Q15_ONE);
    add_param("LOOP", Q15_ONE);       // >half = loop on
    add_param("GAIN", Q15_ONE);
    add_param("SLCE", 0);             // 0=window, 1..16=chops
}

void FrozenPlay::control_tick() {
    const Sample& s = SampleBank::instance().slot(sample_slot);
    const PlayWindow window = play_window(s, params[P_SPEED].effective,
        params[P_START].effective, params[P_LEN].effective,
        params[P_SLICE].effective, pitch_quantize);
    inc_q16_ = window.increment;
    win_start_ = window.start;
    win_end_ = window.end;
    if (playable_frames(s) < 2) {
        visual_position_ = visual_start_ = visual_end_ = 0;
        visual_state_ = 0;
        return;
    }
    visual_speed_ = (int16_t)(inc_q16_ < 0
        ? -(params[P_SPEED].effective < 0 ? -params[P_SPEED].effective : params[P_SPEED].effective)
        :  (params[P_SPEED].effective < 0 ? -params[P_SPEED].effective : params[P_SPEED].effective));
    visual_start_ = window.normalized_start;
    visual_end_ = window.normalized_end;
}

void FrozenPlay::trigger() {
    playing_ = true;
    frac_ = 0;
    amp_q15_ = 0;
    if (inc_q16_ >= 0) {
        pos_ = win_start_;
    } else {
        pos_ = (win_end_ > win_start_ + 1) ? (win_end_ - 2) : win_start_;
    }
}

void FrozenPlay::process(q15* buf, int frames) {
    const Sample& s = SampleBank::instance().slot(sample_slot);
    const uint32_t nf = playable_frames(s);
    // A slot switch/replacement can happen between control ticks. Revalidate the
    // cached window against the new vector before any pos+1 interpolation read.
    if (nf < 2 || win_end_ > nf || win_start_ + 1 >= win_end_) {
        playing_ = false;
        visual_state_ = 0;
        visual_position_ = visual_start_ = visual_end_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }
    if (!playing_) {
        visual_state_ = 0;
        std::memset(buf, 0, (size_t)frames * sizeof(q15));
        return;
    }

    const bool loop = params[P_LOOP].effective > (Q15_ONE / 2);
    const q15 gain  = params[P_GAIN].effective;
    const int ch    = s.channels;

    for (int i = 0; i < frames; ++i) {
        if (pos_ < (int64_t)win_start_ || pos_ + 1 >= (int64_t)win_end_) {
            if (loop) {
                pos_ = (inc_q16_ >= 0) ? win_start_ : ((win_end_ > win_start_ + 1) ? (win_end_ - 2) : win_start_);
                if (pos_ < 0) pos_ = 0;
                frac_ = 0;
                amp_q15_ = 0;        // soften the discontinuity at the loop seam
            } else {
                playing_ = false;
                visual_state_ = 0;
                visual_position_ = (uint16_t)((uint64_t)win_end_ * 32767u / nf);
                std::memset(buf + i, 0, (size_t)(frames - i) * sizeof(q15));
                return;
            }
        }
        // mono-ize stereo on the fly
        q15 a, b;
        if (ch == 2) {
            a = (q15)(((int32_t)s.data[pos_ * 2] + s.data[pos_ * 2 + 1]) >> 1);
            b = (q15)(((int32_t)s.data[(pos_ + 1) * 2] + s.data[(pos_ + 1) * 2 + 1]) >> 1);
        } else {
            a = s.data[(size_t)pos_];
            b = s.data[(size_t)pos_ + 1];
        }
        q15 out = lerp_q15(a, b, frac_);

        // 64-sample edge envelope plus trigger/loop ramp. cheap insurance against clicks.
        constexpr int FADE_FRAMES = 64;
        q15 eg = Q15_ONE;
        int64_t ds = pos_ - (int64_t)win_start_;
        int64_t de = (int64_t)win_end_ - 2 - pos_;
        if (ds < FADE_FRAMES) {
            if (ds < 0) ds = 0;
            eg = (q15)((ds * Q15_ONE) / FADE_FRAMES);
        }
        if (de < FADE_FRAMES) {
            if (de < 0) de = 0;
            q15 fg = (q15)((de * Q15_ONE) / FADE_FRAMES);
            if (fg < eg) eg = fg;
        }
        if (amp_q15_ < Q15_ONE) {
            int ramp = (int)amp_q15_ + 1024;
            amp_q15_ = (q15)(ramp > Q15_ONE ? Q15_ONE : ramp);
        }
        q31 g = ((q31)gain * eg) >> 15;
        g = (g * amp_q15_) >> 15;
        buf[i] = (q15)(((int32_t)out * g) >> 15);

        // advance
        int64_t adv = (int64_t)frac_ + inc_q16_;
        pos_ += adv >> 16;
        frac_ = (trackr::fx::uq16)(adv & 0xFFFF);
    }
    uint32_t shown = pos_ < 0 ? 0u : pos_ >= (int64_t)nf ? nf - 1 : (uint32_t)pos_;
    visual_position_ = (uint16_t)((uint64_t)shown * 32767u / (nf - 1));
    visual_start_ = (uint16_t)((uint64_t)win_start_ * 32767u / (nf - 1));
    visual_end_ = (uint16_t)((uint64_t)(win_end_ - 1) * 32767u / (nf - 1));
    const int16_t speed_magnitude = (int16_t)(params[P_SPEED].effective < 0
        ? -params[P_SPEED].effective : params[P_SPEED].effective);
    visual_speed_ = inc_q16_ < 0 ? (int16_t)-speed_magnitude : speed_magnitude;
    visual_state_ = VisualPlaying | (inc_q16_ < 0 ? VisualReverse : 0) |
                    (loop ? VisualLoop : 0);
}


}
using old_play::FrozenPlay;

namespace {
uint64_t checks=0;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} }while(0)
void set(Unit& u,int p,int value){u.params[p].base=(q15)value;u.params[p].snap();}
q15 slice(int n){return n?(q15)((n*32768+16)/17):0;}
void sample(int frames=160000,int channels=1,bool random=false){
    auto& s=SampleBank::instance().slot(0);s=Sample{};s.channels=(uint8_t)channels;s.data.resize(frames*channels);
    uint32_t seed=123;for(auto& x:s.data){seed=seed*1664525u+1013904223u;x=random?(q15)((int)(seed>>17)-16384):12000;}
}
void legacy_pcm(){
    sample(4097,2,true);auto& s=SampleBank::instance().slot(0);s.chops[0]=0;s.chops[2]=1000;s.chops[5]=2100;s.slice_reverse_mask=1u<<2;
    for(bool quant:{false,true})for(int speed:{-32767,-8192,-1,0,17,8192,13111,32767})for(int cut:{0,1,3,6,16})for(bool loop:{false,true}){
        PlayUnit a;FrozenPlay b;a.set_modern(false);CHECK(a.num_params==6);a.pitch_quantize=b.pitch_quantize=quant;
        const int values[]={speed,5911,23111,loop?32767:0,24111,slice(cut)};
        for(int p=0;p<6;++p){set(a,p,values[p]);set(b,p,values[p]);}
        a.control_tick();b.control_tick();a.trigger();b.trigger();
        for(int block=0;block<50;++block){if(block==9||block==23){int p=block==9?5:1;set(a,p,0);set(b,p,0);}a.control_tick();b.control_tick();
            q15 x[127],y[127];a.process(x,127);b.process(y,127);CHECK(!std::memcmp(x,y,sizeof x));CHECK(a.playback_active()==b.playback_active());}
    }
    std::puts("ok legacy6 frozen pre-change PCM: speed floors/quantization, reverse cuts, live window changes, stereo, loop, gain (160 voices)");
}
void trim_and_latch(){
    for(int i=0;i<6;++i){CHECK(play_mode::gate_index(play_mode::gate_value(i))==i);CHECK(play_mode::gate_quarters(play_mode::gate_value(i))==(1<<i));}
    sample(10000);auto& s=SampleBank::instance().slot(0);s.chops[0]=0;s.chops[1]=4000;s.chops[2]=7000;
    const auto trim=play_window(s,8192,16383,16383,0,false,true);
    auto w=play_window(s,8192,16383,16383,slice(2),false,true);
    CHECK(w.valid&&w.start==trim.start&&w.end==7000);
    auto gone=play_window(s,8192,16383,16383,slice(1),false,true);CHECK(!gone.valid);
    CHECK(play_window(s,8192,16383,16383,slice(1),false,false).valid);
    CHECK(!play_window(s,8192,0,32767,slice(16),false,true).valid);
    CHECK(play_window(s,8192,0,32767,slice(16),false,false).valid);
    for(bool reverse:{false,true})for(bool loop:{false,true}){
        PlayUnit p;set(p,5,slice(2));set(p,3,loop?32767:0);set(p,0,reverse?-8192:8192);p.control_tick();p.trigger();q15 pcm[200];p.process(pcm,200);
        const auto start=p.visual_start(),end=p.visual_end();set(p,5,0);set(p,1,24000);set(p,2,30000);set(p,3,loop?0:32767);s.slice_reverse_mask=0xffff;p.control_tick();p.process(pcm,200);
        CHECK(p.visual_start()==start&&p.visual_end()==end);CHECK(bool(p.visual_state()&PlayUnit::VisualReverse)==reverse);CHECK(bool(p.visual_state()&PlayUnit::VisualLoop)==loop);
        p.release();p.process(pcm,63);CHECK(p.playback_active());p.process(pcm,1);CHECK(!p.playback_active());p.trigger();p.process(pcm,200);CHECK(p.visual_start()!=start);
        s.slice_reverse_mask=0;
    }
    PlayUnit out;set(out,1,16383);set(out,2,16383);set(out,5,slice(1));out.control_tick();out.trigger();q15 b[128];out.process(b,128);for(auto x:b)CHECK(!x);
    std::puts("ok modern global trim intersects cuts; trimmed-out/missing cuts silent; reverse/loop/window latch; manual source release");
}
void preview_cases(bool cuts) {
    sample(24000,2,true);auto& source=SampleBank::instance().slot(0);
    source.chops[0]=0;source.chops[1]=6000;source.chops[2]=18000;source.slice_reverse_mask=2;
    for(bool modern:{false,true})for(bool quant:{false,true}) {
        PlayUnit p;p.set_modern(modern);p.pitch_quantize=quant;
        set(p,PlayUnit::P_START,19000);set(p,PlayUnit::P_LEN,7000);
        set(p,PlayUnit::P_SLICE,0);set(p,PlayUnit::P_LOOP,32767);
        set(p,PlayUnit::P_SPEED,8192);set(p,PlayUnit::P_GAIN,25123);
        p.params[PlayUnit::P_START].step_lock_override=true;p.params[PlayUnit::P_START].step_lock=1234;
        p.params[PlayUnit::P_LEN].temp_override=true;p.params[PlayUnit::P_LEN].temp=2345;
        p.params[PlayUnit::P_SLICE].audition_override=true;p.params[PlayUnit::P_SLICE].audition=slice(1);
        unsigned char before[sizeof p.params];std::memcpy(before,p.params,sizeof before);
        const q15 start=cuts?16383:0,length=cuts?16383:32767,cut=cuts?slice(2):0;
        const auto expected=play_window(source,8192,start,length,cut,quant,modern);
        CHECK(expected.valid);p.control_tick();p.trigger();p.preview(start,length,cut);
        CHECK(!std::memcmp(before,p.params,sizeof before));
        q15 audio[128];int done=0;const int duration=(int)(expected.end-expected.start);
        while(done<duration+128) {
            const int count=std::min(128,duration+128-done);p.control_tick();p.process(audio,count);done+=count;
            CHECK(!std::memcmp(before,p.params,sizeof before));
            if(done<duration-1){CHECK(p.playback_active());CHECK(p.visual_start()==expected.normalized_start&&p.visual_end()==expected.normalized_end);CHECK(!(p.visual_state()&PlayUnit::VisualLoop));}
        }
        CHECK(!p.playback_active()); // stored LOOP on cannot repeat the audition
        p.preview(start,length,cut);p.process(audio,128);p.release();p.process(audio,63);CHECK(p.playback_active());p.process(audio,1);CHECK(!p.playback_active());
        CHECK(!std::memcmp(before,p.params,sizeof before));
        // A next ordinary trigger restores the persistent/live legacy window,
        // even without waiting for an intervening control tick. Reset does too.
        for(bool reset:{false,true}) {
            p.preview(start,length,cut);if(reset)p.reset();p.trigger();
            if(!modern) {
                FrozenPlay oracle;oracle.pitch_quantize=quant;
                for(int i=0;i<6;++i)set(oracle,i,p.params[i].effective);
                oracle.control_tick();oracle.trigger();
                for(int block=0;block<80;++block){q15 ref[128];p.control_tick();p.process(audio,128);oracle.control_tick();oracle.process(ref,128);CHECK(!std::memcmp(audio,ref,sizeof ref));}
            } else {p.process(audio,128);CHECK(p.visual_state()&PlayUnit::VisualLoop);}
            CHECK(p.playback_active());CHECK(!std::memcmp(before,p.params,sizeof before));
        }
    }
    std::puts(cuts?"ok selected-cut preview: modern intersection/legacy chop window, one pass, all Params exact, release and normal-trigger/reset restore":"ok long full preview: LOOP-on ignored only at runtime, no gate, all Params exact, release and normal-trigger/reset restore");
}
struct Fixture {
    std::unique_ptr<Engine> e=std::make_unique<Engine>();PlayUnit* p=new PlayUnit;
    Fixture(bool phase=false,int bpm=120,int swing=50){delete e->replace_unit(0,0,p);e->set_bpm(bpm);e->set_swing(swing);if(phase){CHECK(e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1));CHECK(e->set_clock_mod(ClockTarget::Tempo,ModSource::None,0));}e->set_sequence_length(0,64);e->set_sequencer_enabled(true);}
    void start(){p->control_tick();e->pattern.steps[0][0].flags|=SeqStep::Trig;e->pattern_banks[1]=e->pattern;e->transport_play();e->request_pattern(1,false);}
    void render(int n,int part=317){q15 pcm[634];while(n){int k=std::min(n,part);e->render(pcm,k);n-=k;}}
};
void gate_lifetimes(){
    sample();
    for(bool phase:{false,true})for(bool loop:{false,true})for(bool gate:{false,true})for(int div=0;div<6;++div){
        Fixture f(phase);set(*f.p,3,loop?32767:0);set(*f.p,6,gate?32767:0);set(*f.p,7,play_mode::gate_value(div));f.start();
        const uint64_t increment=((uint64_t)120*65536*65536+ENGINE_SR*30u)/(ENGINE_SR*60u);
        const int span=phase?(int)((((uint64_t)1<<30)*play_mode::gate_quarters(play_mode::gate_value(div))/4+increment-1)/increment):1000*(1<<div);
        f.render(span);CHECK(f.p->playback_active());f.render(63);CHECK(f.p->playback_active());f.render(1);CHECK(f.p->playback_active()!=gate);
        if(gate){q15 quiet[64];f.p->process(quiet,64);for(auto x:quiet)CHECK(x==0);}else{f.render(127);CHECK(f.p->playback_active());}
    }
    // Empty step restores the unlocked selector, never expands a long held cut.
    sample(100000);auto& s=SampleBank::instance().slot(0);s.chops[0]=0;s.chops[1]=12000;s.chops[2]=50000;
    for(bool phase:{false,true}){Fixture f(phase);set(*f.p,6,0);CHECK(f.e->set_step_lock(0,0,0,5,slice(2)));f.start();f.render(100);const auto begin=f.p->visual_start(),end=f.p->visual_end();f.render(7000);CHECK(!f.p->params[5].step_lock_override);CHECK(f.p->visual_start()==begin&&f.p->visual_end()==end);CHECK(f.p->playback_active());}
    for(bool phase:{false,true}) {
        Fixture f(phase);set(*f.p,0,-8192);set(*f.p,7,0);
        CHECK(f.e->set_step_lock(0,0,0,7,play_mode::gate_value(3)));
        f.e->pattern.steps[0][1].flags|=SeqStep::Trig;
        f.e->pattern.steps[0][1].set_condition(SeqStep::Condition::Fill);
        f.start();f.render(4500);CHECK(!f.p->params[7].step_lock_override);CHECK(f.p->playback_active());
        set(*f.p,6,0);set(*f.p,3,32767);f.p->control_tick();
        const uint64_t inc=((uint64_t)120*65536*65536+ENGINE_SR*30u)/(ENGINE_SR*60u);
        const int end=phase?(int)(((uint64_t(1)<<31)+inc-1)/inc):8000;
        f.render(end-4500+64);CHECK(!f.p->playback_active());
    }
    std::puts("ok both scheduler paths: all six gate durations, oneshot vs gate, independently looping gate, reverse/skipped/empty-step window+mode+gate latch");
}
void timing_and_retrigs(){
    sample();
    for(bool phase:{false,true})for(int bpm:{87,137,231})for(int swing:{50,67,75}){
        Fixture f(phase,bpm,swing);set(*f.p,7,play_mode::gate_value(0));f.e->pattern.microtiming[0][1]=3;f.start();
        const uint64_t inc=((uint64_t)bpm*65536*65536+ENGINE_SR*30u)/(ENGINE_SR*60u);
        const uint64_t step=uint64_t(1)<<30;
        const uint64_t fpb=((uint64_t)ENGINE_SR*60+bpm/2)/bpm, grid=fpb/4;
        const uint64_t native=phase?step+step*(2*swing-100)/100+step*3/SEQ_MICROTIMING_DIVISIONS:grid+grid*(2*swing-100)/100+(grid*3+SEQ_MICROTIMING_DIVISIONS/2)/SEQ_MICROTIMING_DIVISIONS;
        const uint64_t duration=(native+3)/4;
        const int deadline=phase?(int)((duration+inc-1)/inc):(int)duration;
        f.render(deadline);CHECK(f.p->playback_active());f.render(64);CHECK(!f.p->playback_active());
    }
    for(bool phase:{false,true})for(int count:{2,3,4}){
        Fixture f(phase);set(*f.p,7,play_mode::gate_value(1));f.e->pattern.steps[0][0].set_retrig_count(count);f.start();
        const uint64_t inc=((uint64_t)120*65536*65536+ENGINE_SR*30u)/(ENGINE_SR*60u),step=uint64_t(1)<<30;
        int last=phase?(int)((step*(count-1)/count+inc-1)/inc):4000*(count-1)/count;
        // q32 trigger latches the first representable output-frame phase.
        const int duration=phase?(int)((step/2+inc-1)/inc):2000;
        f.render(last+duration);CHECK(f.p->playback_active());f.render(64);CHECK(!f.p->playback_active());
    }
    // Frame->Q32 activation and mid-gate legacy BPM change preserve the voice.
    for(int transition=0;transition<3;++transition){Fixture f(transition==2);set(*f.p,7,play_mode::gate_value(3));f.start();f.render(1000);
        if(transition==0){f.e->set_bpm(240);f.render(3500);CHECK(f.p->playback_active());f.render(64);CHECK(!f.p->playback_active());}
        else if(transition==1){f.e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1);f.e->set_clock_mod(ClockTarget::Tempo,ModSource::None,0);f.render(7000);CHECK(f.p->playback_active());f.render(65);CHECK(!f.p->playback_active());}
        else {
            f.e->set_bpm(240);
            const uint64_t initial_inc=((uint64_t)120*65536*65536+ENGINE_SR*30u)/(ENGINE_SR*60u);
            uint64_t phase=1000*initial_inc;int frames=0,release_at=-1;
            while(f.p->playback_active()&&frames<10000) {
                if(phase>=(uint64_t(1)<<31)&&release_at<0)release_at=frames;
                q15 b[2];f.e->render(b,1);
                phase+=((uint64_t)f.e->effective_bpm_q16()*65536+ENGINE_SR*30u)/(ENGINE_SR*60u);
                ++frames;CHECK(f.p->playback_active()==(release_at<0||frames-release_at<64));
            }
            CHECK(release_at>=0&&frames>3500&&frames<7000);
        }
    }
    {Fixture f(false,137);set(*f.p,7,play_mode::gate_value(3));f.start();const uint64_t oldbeat=f.e->frames_per_beat();f.render(1000);f.e->set_bpm(231);const uint64_t nowbeat=f.e->frames_per_beat();const int left=(int)(((oldbeat/4*2-1000)*nowbeat+oldbeat-1)/oldbeat);f.render(left+63);CHECK(f.p->playback_active());f.render(1);CHECK(!f.p->playback_active());}
    std::puts("ok fractional BPM/swing/microtiming native deadlines, 2/3/4 retrigger renewal, frame->q32 and exact integrated mid-gate tempo changes");
}
void partition_and_tails(){
    sample();
    for(bool phase:{false,true}){Fixture a(phase,137,67),b(phase,137,67);for(Fixture* f:{&a,&b}){set(*f->p,7,play_mode::gate_value(0));f->e->pattern.microtiming[0][1]=-3;f->e->pattern.steps[0][0].set_retrig_count(3);f->start();}
        std::vector<q15>x(40000),y(40000);a.e->render(x.data(),20000);int done=0;while(done<20000){const int n=std::min(20000-done,1+done%173);b.e->render(y.data()+done*2,n);done+=n;}CHECK(x==y);CHECK(a.p->playback_active()==b.p->playback_active());
    }
    Fixture tail;auto* d=new DelayUnit(false);delete tail.e->replace_unit(0,1,d);set(*d,0,1000);set(*d,1,24000);set(*d,2,30000);d->control_tick();set(*tail.p,7,0);tail.start();tail.render(1064);CHECK(!tail.p->playback_active());q15 audio[634];tail.e->render(audio,317);bool energy=false;for(auto x:audio)energy|=x!=0;CHECK(energy);
    for(bool phase:{false,true}){Fixture f(phase);f.start();f.render(100);f.e->trigger_chain(0);f.render(8000);CHECK(f.p->playback_active());f.p->release();f.render(64);CHECK(!f.p->playback_active());
        f.e->transport_reset();f.e->request_pattern(0,false);f.render(100);f.e->transport_stop();f.render(64);CHECK(!f.p->playback_active());f.e->transport_play();f.render(100);CHECK(!f.p->playback_active());
        auto other=std::make_unique<Engine>();f.e->swap_patch_state(*other);f.render(100);CHECK(!f.e->chains[0].units[0]);
    }
    std::puts("ok partition-identical unclipped PCM; source stops while downstream delay rings; manual trigger cancels stale gates; stop/reset/patch clear deadlines");
}
using Bytes=std::vector<uint8_t>;std::string path;
Bytes read(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const Bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
uint32_t r32(const Bytes& b,size_t at){CHECK(at+4<=b.size());return b[at]|(uint32_t)b[at+1]<<8|(uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<24;}
void w16(Bytes& b,size_t at,int v){CHECK(at+2<=b.size());b[at]=(uint8_t)v;b[at+1]=(uint8_t)((uint16_t)v>>8);}
size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
void persistence(){
    sample();
    for(bool modern:{false,true})for(bool quant:{false,true}){auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto* p=new PlayUnit;p->set_modern(modern);p->pitch_quantize=quant;delete a->replace_unit(0,0,p);
        for(int i=0;i<p->num_params;++i){set(*p,i,1111+i*2211);p->params[i].src=ModSource::SigilA;p->params[i].mod_depth=1991;CHECK(a->set_step_lock(0,i,0,i,(q15)(2222+i)));}a->capture_scene(0,0);
        CHECK(patchio::save(*a,path.c_str()));const auto before=read();CHECK(before[chunk(before,"UNIT")+8]==(modern?2:0)+(quant?1:0));CHECK(patchio::load(*b,path.c_str()));auto* got=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(got->modern()==modern&&got->pitch_quantize==quant&&got->num_params==(modern?PlayUnit::P_COUNT:6));
        for(int i=0;i<got->num_params;++i){CHECK(got->params[i].base==p->params[i].base);CHECK(got->params[i].src==p->params[i].src);CHECK(got->params[i].mod_depth==1991);CHECK(b->scenes[0][0].base[0][i]==a->scenes[0][0].base[0][i]);CHECK(b->pattern.steps[0][i].locks[0].param==i);}
        std::unique_ptr<Unit> copy(clone_unit(got));CHECK(static_cast<PlayUnit*>(copy.get())->modern()==modern&&copy->num_params==got->num_params);CHECK(patchio::save(*b,path.c_str()));CHECK(read()==before);
        auto reordered=before;size_t at=chunk(reordered,"UNIT"),end=at+8+r32(reordered,at+4);Bytes flags(reordered.begin()+at,reordered.begin()+end);reordered.erase(reordered.begin()+at,reordered.begin()+end);reordered.insert(reordered.begin()+5,flags.begin(),flags.end());put(reordered);CHECK(patchio::load(*b,path.c_str()));got=static_cast<PlayUnit*>(b->chains[0].units[0]);CHECK(got->modern()==modern&&got->pitch_quantize==quant);for(int i=0;i<got->num_params;++i)CHECK(got->params[i].base==p->params[i].base);
        auto bad=before;bad[chunk(bad,"UNIT")+8]=4;put(bad);Unit* sentinel=got;CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[0].units[0]==sentinel);
    }
    for(int ver=1;ver<=6;++ver){Bytes old={'S','N','C',(uint8_t)('0'+ver),(uint8_t)ver};const int raw[]={-9111,1011,27111,32767,25000,0};
        for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127});if(ver>=2)old.insert(old.end(),{0,0});old.push_back(0);size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){old[first]=(uint8_t)UnitKind::SamplePlayer;for(int p=0;p<6;++p)w16(old,first+3+p*5,raw[p]);}}
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);if(ver>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}if(ver>=4){size_t at=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[at]=255;}if(ver>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}if(ver>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old);auto e=std::make_unique<Engine>();CHECK(patchio::load(*e,path.c_str()));auto* p=static_cast<PlayUnit*>(e->chains[0].units[0]);CHECK(!p->modern()&&p->num_params==6&&p->pitch_quantize==(ver>=4));FrozenPlay ref;ref.pitch_quantize=ver>=4;
        for(int i=0;i<6;++i){CHECK(p->params[i].base==raw[i]);set(ref,i,raw[i]);}p->control_tick();ref.control_tick();p->trigger();ref.trigger();q15 x[5000],y[5000];p->process(x,5000);ref.process(y,5000);CHECK(!std::memcmp(x,y,sizeof x));
        CHECK(patchio::save(*e,path.c_str()));auto snc=read();CHECK(snc[chunk(snc,"UNIT")+8]==(ver>=4?1:0));size_t core=chunk(snc,"CORE")+8;for(int i=6;i<12;++i){w16(snc,core+8+i*5,30000);snc[core+8+i*5+2]=5;}put(snc);CHECK(patchio::load(*e,path.c_str()));p=static_cast<PlayUnit*>(e->chains[0].units[0]);CHECK(!p->modern()&&p->num_params==6);CHECK(p->params[6].base==32767&&p->params[7].base==play_mode::gate_value(2));CHECK(p->params[6].src==ModSource::None);CHECK(patchio::save(*e,path.c_str()));snc=read();for(int i=6;i<12;++i)for(int j=0;j<5;++j)CHECK(snc[core+8+i*5+j]==0);
    }
    std::puts("ok SNC7 mode/quant flags+UNIT order, 6/10 params (optional appended pair)/scenes/locks/clone/byte resave; synthetic SNC1..6 exact legacy PCM; old flags/tails never auto-convert; unknown flags transactional");
}
} // namespace
int main(){path="/tmp/seance-play-core-"+std::to_string((long)getpid())+".snc";legacy_pcm();trim_and_latch();preview_cases(false);preview_cases(true);gate_lifetimes();timing_and_retrigs();partition_and_tails();persistence();std::remove(path.c_str());std::printf("play core: 8/8 passed (%llu checks)\n",(unsigned long long)checks);}
