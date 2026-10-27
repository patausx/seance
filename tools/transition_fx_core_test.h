// Real temporary master DSP + engine lifecycle, in the existing sequencer harness.
namespace transition_fx_cases {
using Kind = TransitionFxKind;
using Fx = TransitionFx;
std::vector<q15> signal(int frames, int seed=0) {
    std::vector<q15> v(size_t(frames)*2);
    for(int i=0;i<frames;++i) { v[i*2]=(q15)(((i+seed)*97%20001)-10000); v[i*2+1]=(q15)(((i+seed)*53%16001)-8000); }
    return v;
}
void process(Fx& fx,std::vector<q15>& v,bool partition=false) {
    const int n=(int)v.size()/2;
    if(!partition) fx.process(v.data(),n);
    else for(int off=0;off<n;) { const int count=std::min(n-off,1+off%137);fx.process(v.data()+off*2,count);off+=count; }
}
void zeros(Fx& fx,int frames) { std::vector<q15> v(size_t(frames)*2);process(fx,v); }
void idle_filter() {
    Fx unavailable(false);CHECK(!unavailable.state().ready);
    CHECK(!unavailable.set(Kind::Echo,32767,120*65536));CHECK(!unavailable.set(Kind::Roll,0,120*65536));
    CHECK(unavailable.state().kind==Kind::None);CHECK(unavailable.set(Kind::Filter,-20000,120*65536));
    Fx available;CHECK(available.set(Kind::Filter,-20000,120*65536));CHECK(unavailable.state().ready);
    auto fail_audio=signal(1000),reference=fail_audio;process(unavailable,fail_audio);process(available,reference);CHECK(fail_audio==reference);
    unavailable.release();available.release();fail_audio=reference=signal(1000);process(unavailable,fail_audio);process(available,reference);CHECK(fail_audio==reference);
    Fx fx; CHECK(fx.state().ready);CHECK(Fx::STORAGE_BYTES==768000);
    CHECK(Fx::beat_frames(20*65536)==96000&&Fx::beat_frames(300*65536)==6400);
    CHECK(Fx::beat_frames(-1)==96000&&Fx::beat_frames(INT32_MAX)==6400);
    auto original=signal(60000), data=original;process(fx,data);CHECK(data==original);
    CHECK(fx.set(Kind::Filter,0,120*65536));process(fx,data);CHECK(data==original);
    CHECK(!fx.set((Kind)255,123,120*65536));CHECK(fx.state().kind==Kind::Filter);
    CHECK(fx.set(Kind::Filter,-999999,120*65536));CHECK(fx.state().amount==-32767);
    std::vector<q15> alternating(8000*2);
    for(int i=0;i<8000;++i) { alternating[i*2]=(q15)(i%2?12000:-12000);alternating[i*2+1]=0; }
    process(fx,alternating);
    for(int i=7000;i<8000;++i) { CHECK(std::abs(alternating[i*2])<80);CHECK(alternating[i*2+1]==0); }
    CHECK(fx.set(Kind::Filter,999999,120*65536));CHECK(fx.state().amount==32767);
    std::vector<q15> dc(8000*2,10000);process(fx,dc);
    for(int i=7000;i<8000;++i) CHECK(std::abs(dc[i*2])<=1);
    fx.release();CHECK(fx.state().releasing);zeros(fx,Fx::RAMP_FRAMES);CHECK(!fx.state().releasing); data=original;process(fx,data);CHECK(data==original);
    fx.reset();for(auto k:{Kind::Filter,Kind::Echo,Kind::Roll}) {
        CHECK(fx.set(k,k==Kind::Filter?-32767:k==Kind::Roll?3:32767,120*65536));
        std::vector<q15> silence(40000*2);process(fx,silence);
        CHECK(std::all_of(silence.begin(),silence.end(),[](q15 x){return x==0;}));fx.reset();
    }
}
void echo() {
    for(int bpm:{20*65536,40*65536,120*65536+32768,300*65536}) {
        Fx fx;CHECK(fx.set(Kind::Echo,32767,bpm));zeros(fx,Fx::RAMP_FRAMES);
        const uint32_t period=Fx::beat_frames(bpm);CHECK(fx.echo_delay_frames()==period);
        CHECK(fx.set(Kind::Echo,32767,70*65536));CHECK(fx.echo_delay_frames()==period); // held tempo latch
        std::vector<q15> impulse(size_t(period*3+10)*2);impulse[0]=12000;impulse[1]=-6000;
        process(fx,impulse,true);
        for(uint32_t i=0;i<period*3+10;++i) {
            const int generation=i/period;
            const int l=i%period==0 ? 12000/(generation>0?(1<<(generation-1)):1) : 0;
            const int r=i%period==0 ? -6000/(generation>0?(1<<(generation-1)):1) : 0;
            CHECK(impulse[i*2]==l);CHECK(impulse[i*2+1]==r);
        }
        fx.release();CHECK(fx.state().kind==Kind::None&&fx.state().echo_tail);
        std::vector<q15> tail(size_t(Fx::TAIL_FRAMES+Fx::RAMP_FRAMES+2)*2);process(fx,tail);
        CHECK(std::any_of(tail.begin(),tail.end(),[](q15 x){return x!=0;}));CHECK(!fx.state().echo_tail);
        auto dry=signal(5000), out=dry;process(fx,out);CHECK(out==dry);
        CHECK(fx.set(Kind::Echo,40000,80*65536));CHECK(fx.state().amount==32767);
        CHECK(fx.echo_delay_frames()==24000);zeros(fx,256);
        q15 one[2]={15000,-8000};fx.process(one,1);fx.release();
        CHECK(fx.set(Kind::Echo,20000,160*65536));CHECK(fx.echo_delay_frames()==12000);
        zeros(fx,300);CHECK(fx.state().kind==Kind::Echo);
        fx.release();fx.release();zeros(fx,Fx::TAIL_FRAMES+Fx::RAMP_FRAMES+2);CHECK(!fx.state().echo_tail);
        CHECK(fx.set(Kind::Echo,-100,1));CHECK(fx.state().amount==0);
        CHECK(fx.echo_delay_frames()==Fx::MAX_FRAMES);fx.reset();
        std::vector<q15> silence(50000*2);process(fx,silence);CHECK(std::all_of(silence.begin(),silence.end(),[](q15 x){return !x;}));
    }
}
void roll() {
    const auto history=signal(Fx::MAX_FRAMES);
    for(int division=0;division<4;++division) {
        Fx fx;auto seed=history;process(fx,seed);CHECK(seed==history);
        CHECK(fx.set(Kind::Roll,division,120*65536));const int period=16000>>division;
        CHECK(fx.roll_length_frames()==(uint32_t)period);
        auto out=signal(period*3,345);process(fx,out,true);CHECK(fx.state().ready);
        CHECK(fx.set(Kind::Roll,division,40*65536));CHECK(fx.roll_length_frames()==(uint32_t)period);
        for(int i=Fx::RAMP_FRAMES;i<period*3;++i)for(int c=0;c<2;++c) {
            const int phase=i%period, start=Fx::MAX_FRAMES-period;
            int sample=history[(start+phase)*2+c];
            if(phase>=period-(int)Fx::SEAM_FRAMES) {
                const int head=history[start*2+c],fade=phase-(period-(int)Fx::SEAM_FRAMES)+1;
                sample+=(head-sample)*fade/(int)Fx::SEAM_FRAMES;
            }
            CHECK(out[i*2+c]==sample);
            if(phase==period-1) CHECK(out[i*2+c]==history[start*2+c]);
        }
        CHECK(fx.set(Kind::Roll,(division+1)%4,320*65536));
        zeros(fx,Fx::RAMP_FRAMES+1);CHECK(fx.state().ready);
        CHECK(fx.roll_length_frames()==(uint32_t)(16000>>((division+1)%4)));
        fx.release();zeros(fx,Fx::RAMP_FRAMES+1);auto dry=signal(1000), copy=dry;process(fx,copy);CHECK(dry==copy);
    }
    Fx cold;CHECK(cold.set(Kind::Roll,3,120*65536));CHECK(!cold.state().ready);
    auto a=signal(2000), saved=a;process(cold,a);CHECK(a==saved);CHECK(!cold.state().ready);
    q15 one[2]={1234,4567};cold.process(one,1);CHECK(cold.state().ready);
    CHECK(cold.set(Kind::Roll,-999,320*65536));CHECK(cold.state().roll_division==0);
    zeros(cold,256);CHECK(!cold.state().ready);zeros(cold,17000);CHECK(cold.state().ready);
    CHECK(cold.set(Kind::Roll,999,320*65536));CHECK(cold.state().roll_division==3);
    cold.reset();CHECK(cold.state().kind==Kind::None);
    CHECK(cold.set(Kind::Roll,3,120*65536));CHECK(!cold.state().ready);
}
void partition() {
    Fx whole,parts;
    auto run=[&](int n) {auto a=signal(n,123),b=a;process(whole,a);process(parts,b,true);CHECK(a==b);
        const auto x=whole.state(),y=parts.state();CHECK(x.kind==y.kind&&x.ready==y.ready&&x.echo_tail==y.echo_tail&&x.amount==y.amount);};
    run(48000);
    for(auto kind:{Kind::Filter,Kind::Echo,Kind::Roll,Kind::Filter,Kind::Echo,Kind::Roll}) {
        const int amount=kind==Kind::Filter?-25000:kind==Kind::Echo?29000:2;
        CHECK(whole.set(kind,amount,123*65536+999));CHECK(parts.set(kind,amount,123*65536+999));run(12003);
        CHECK(whole.set(kind,kind==Kind::Roll?0:20000,200*65536));CHECK(parts.set(kind,kind==Kind::Roll?0:20000,200*65536));run(511);
        whole.release();parts.release();run(257); // rapid repress / previous tail
    }
    whole.stop();parts.stop();run(Fx::TAIL_FRAMES+Fx::RAMP_FRAMES+3);
    whole.reset();parts.reset();run(137);
}
struct Impulse : Unit {
    bool fire=false;int wait=0;
    Impulse(){kind=UnitKind::Osc;}
    void process(q15* out,int frames) override {for(int i=0;i<frames;++i){out[i]=0;if(fire&&wait--<=0){out[i]=12000;fire=false;}}}
};
void engine_timing() {
    auto e=std::make_unique<Engine>();auto* source=new Impulse;e->chains[0].units[0]=source;
    e->sigils[0].drawn=true;e->sigils[0].depth=32767;for(auto& v:e->sigils[0].table)v=12000;
    CHECK(e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,37));
    std::vector<q15> warm(4000*2);e->render(warm.data(),4000);
    const int32_t applied=e->effective_bpm_q16();CHECK(applied!=e->bpm*65536);
    CHECK(e->set_transition_fx(Kind::Echo,32767));e->render(warm.data(),256);
    const uint32_t delay=Fx::beat_frames(applied);e->set_bpm(240);source->fire=true;
    std::vector<q15> out(size_t(delay+3)*2);e->render(out.data(),delay+3);
    CHECK(out[0]>0&&out[delay*2]==out[0]&&out[delay*2+1]==out[1]);
    for(uint32_t i=1;i<delay;++i)CHECK(out[i*2]==0&&out[i*2+1]==0);
    // Independent engine: actual filtered eighth sample is published verbatim.
    auto scoped=std::make_unique<Engine>();auto* impulse_source=new Impulse;scoped->chains[0].units[0]=impulse_source;
    CHECK(scoped->set_transition_fx(Kind::Filter,-32767));scoped->render(warm.data(),256);
    const int previous_pos=scoped->scope_pos;impulse_source->fire=true;impulse_source->wait=7;
    scoped->render(warm.data(),8);CHECK(scoped->scope_pos==(previous_pos+1)%Engine::SCOPE_LEN);
    CHECK(scoped->scope[previous_pos]==warm[14]&&warm[14]>0&&warm[14]<100);
    source->fire=true;e->render(warm.data(),256);e->release_transition_fx();const auto epoch=e->transition_fx().epoch;
    e->transport_stop();CHECK(e->transition_fx().epoch==epoch+1&&e->transition_fx().echo_tail);
    auto other=std::make_unique<Engine>();CHECK(other->set_transition_fx(Kind::Roll,3));const auto ep2=other->transition_fx().epoch;
    e->swap_patch_state(*other);CHECK(e->transition_fx().epoch==epoch+2&&other->transition_fx().epoch==ep2+1);
    CHECK(!e->transition_fx().echo_tail&&!other->transition_fx().echo_tail);
    CHECK(e->transition_fx().kind==Kind::None&&other->transition_fx().kind==Kind::None);
    e->render(warm.data(),4000);CHECK(std::all_of(warm.begin(),warm.end(),[](q15 x){return !x;}));
}
void engine_ownership_storage() {
    auto e=transition_core_cases::engine(true);transition_core_cases::populate(*e);
    CHECK(e->set_pattern_transition(0,3,16000));e->transport_play();std::vector<q15> warm(2048);e->render(warm.data(),1024);
    const auto frame=e->transport_frame,tick=e->sequence_tick;const auto applied=e->effective_bpm_q16();
    std::array<uint64_t,8> banks{};for(int i=0;i<8;++i)banks[i]=pattern_hash(e->sequence_pattern(i));
    std::array<Param,8> params{};for(int c=0;c<4;++c)for(int p=0;p<2;++p)params[c*2+p]=e->chains[c].units[0]->params[p];
    for(auto kind:{Kind::Filter,Kind::Echo,Kind::Roll}) {
        CHECK(e->set_transition_fx(kind,kind==Kind::Roll?1:15000));e->release_transition_fx();
        CHECK(e->transport_frame==frame&&e->sequence_tick==tick&&e->effective_bpm_q16()==applied);
        CHECK(e->pattern_transition().active&&e->pattern_transition().position==16000);
        for(int i=0;i<8;++i)CHECK(banks[i]==pattern_hash(e->sequence_pattern(i)));
        for(int c=0;c<4;++c)for(int p=0;p<2;++p)CHECK(!std::memcmp(&params[c*2+p],&e->chains[c].units[0]->params[p],sizeof(Param)));
    }
    CHECK(e->set_transition_fx(Kind::Filter,19000));CHECK(e->set_pattern_transition(0,3,32767));CHECK(e->finish_pattern_transition());
    CHECK(e->transition_fx().kind==Kind::Filter);e->clear_pattern_transition();CHECK(e->transition_fx().kind==Kind::Filter);
    Fixture fixture;Engine& stored=*fixture.engine;fixture.dense();stored.capture_global_scene(0);stored.capture_global_scene(1);
    struct File {
        char path[64]="/tmp/seance-transition-fx-XXXXXX";
        File(){int fd=mkstemp(path);CHECK(fd>=0);close(fd);}~File(){std::remove(path);std::remove((std::string(path)+".tmp").c_str());}
        std::vector<char> bytes(){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
    } file;
    CHECK(patchio::save(stored,file.path));const auto before=file.bytes();
    for(auto kind:{Kind::Filter,Kind::Echo,Kind::Roll}) {
        CHECK(stored.set_transition_fx(kind,kind==Kind::Roll?2:24000));CHECK(patchio::save(stored,file.path));CHECK(file.bytes()==before);
        auto loaded=std::make_unique<Engine>();CHECK(loaded->set_transition_fx(Kind::Echo,27000));const auto epoch=loaded->transition_fx().epoch;
        CHECK(patchio::load(*loaded,file.path));CHECK(loaded->transition_fx().kind==Kind::None&&!loaded->transition_fx().echo_tail);
        CHECK(loaded->transition_fx().epoch==epoch+1);CHECK(patchio::save(*loaded,file.path));CHECK(file.bytes()==before);
    }
}
} // namespace transition_fx_cases
void test_transition_fx_core() {
    transition_fx_cases::idle_filter();transition_fx_cases::echo();transition_fx_cases::roll();
    transition_fx_cases::partition();transition_fx_cases::engine_timing();transition_fx_cases::engine_ownership_storage();
    std::puts("transition fx core: real stereo filter/echo/capture roll, latched q16 clock, finite tails, ramps/partition/lifecycle/SNC7 passed");
}
