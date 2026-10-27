// Focused NOISE core proof included by the existing sequencer host harness.
// Uses its CHECK/standard includes; no standalone build, audio render or UI model.
void noise_core_set(NoiseUnit& u,int p,int value) {u.params[p].base=q15(value);u.params[p].snap();}

// Independent frozen six-control NOISE recurrence from before ATTACK/MODE.
// Reads only copied effective controls, never the new envelope helpers/state.
struct noise_core_legacy {
    uint32_t rng=0x4E4F4953u;int32_t held=0,pink=0,filtered=0,body_low=0,crackle=0,env=32767;
    uint16_t hold_count=0,hold_period=1,tone_alpha=8192,body_alpha=0,decay_step=0;bool drone=true;
    void tick(const NoiseUnit& u) {
        const uint32_t density=uint16_t(u.params[1].effective),tone=uint16_t(u.params[2].effective),body=uint16_t(u.params[4].effective),decay=uint16_t(u.params[5].effective);
        hold_period=uint16_t(1+(32767-density)*63/32767);tone_alpha=uint16_t(256+tone*32256/32767);
        body_alpha=body?uint16_t(48+body*16144/32767):0;
        const bool was=drone;drone=decay>=32760;if(drone&&!was)env=32767;
        if(drone)decay_step=0;else{const uint32_t inverse=32767-decay;uint32_t curve=inverse*inverse>>15;curve=curve*inverse>>15;decay_step=uint16_t(2+(curve*1022>>15));}
    }
    void trigger(const NoiseUnit& u){env=32767;drone=u.params[5].effective>=32760;}
    std::vector<q15> process(const NoiseUnit& u,int n) {
        std::vector<q15> out(n);const uint32_t density=uint16_t(u.params[1].effective);const int gain=u.params[3].effective;
        const int colour=std::min(3,int(uint32_t(uint16_t(u.params[0].effective))*4>>15));
        for(q15& sample:out) {
            rng=rng*1664525u+1013904223u;const int32_t white=int32_t(rng>>16)-32768;
            if(!hold_count){held=white;hold_count=hold_period;}--hold_count;pink+=(white-pink)>>4;int32_t source;
            if(colour==1)source=(white>>1)+pink;
            else if(colour==2){if((rng&0xffff)<(density<<1))crackle=white;source=crackle;crackle-=crackle>>5;}
            else if(colour==3)source=(((held^int32_t(rng>>9))&0x4000)?24576:-24576)+(held>>2);
            else source=hold_period<=2?white:held;
            filtered+=int32_t(int64_t(source-filtered)*tone_alpha>>15);int32_t shaped=filtered;
            if(body_alpha){body_low+=int32_t(int64_t(filtered-body_low)*body_alpha>>15);shaped=filtered-body_low;}else body_low=filtered;
            const int64_t raw=int64_t(shaped)*gain*env>>30;sample=q15(std::max<int64_t>(-32768,std::min<int64_t>(32767,raw)));
            if(!drone&&env>0)env=std::max(0,env-std::max(1,int(env*decay_step>>15)));
        }
        return out;
    }
};
void noise_core_legacy_exact() {
    size_t samples=0;
    for(int colour=0;colour<4;++colour)for(int decay:{0,17000,32767}) {
        NoiseUnit u;noise_core_legacy oracle;noise_core_set(u,0,colour*8192);noise_core_set(u,5,decay);noise_core_set(u,7,0);
        for(int block=0;block<18;++block) {
            noise_core_set(u,1,block%3==0?0:(block%3==1?20316:32767));noise_core_set(u,2,block%2?32767:2400);noise_core_set(u,4,block%2?18000:0);
            noise_core_set(u,3,block==4?0:24576);u.control_tick();oracle.tick(u);
            if(block==5||block==13){u.trigger();oracle.trigger(u);}
            std::vector<q15> actual(32);u.process(actual.data(),32);CHECK(actual==oracle.process(u,32));CHECK(u.visual_envelope()==oracle.env);samples+=actual.size();
        }
    }
    // Existing sigil DECAY route still crosses the implicit drone threshold,
    // with the same real parameter slew and both falling/rising transitions.
    NoiseUnit routed;noise_core_legacy oracle;noise_core_set(routed,7,0);noise_core_set(routed,5,32700);
    auto& decay=routed.params[5];decay.src=ModSource::SigilA;decay.mod_depth=300;bool finite=false,drone=false;
    for(int block=0;block<40;++block) {
        decay.resolve(block<12||block>=28?32767:-32767);routed.control_tick();oracle.tick(routed);finite|=!oracle.drone;drone|=oracle.drone;
        std::vector<q15> actual(32);routed.process(actual.data(),32);CHECK(actual==oracle.process(routed,32));CHECK(routed.visual_envelope()==oracle.env);samples+=32;
    }
    CHECK(finite&&drone);NoiseUnit fresh,follow;noise_core_set(follow,7,0);fresh.control_tick();follow.control_tick();std::array<q15,128>a{},b{};fresh.process(a.data(),128);follow.process(b.data(),128);CHECK(a==b);
    std::printf("noise core legacy: four colours, finite/drone/route threshold, %zu exact PCM samples\n",samples+128);
}
void noise_core_attack_modes() {
    CHECK(NoiseUnit::P_ATTACK==6&&NoiseUnit::P_MODE==7&&NoiseUnit::P_COUNT==8);
    CHECK(NoiseUnit::attack_frames(-32768)==0&&NoiseUnit::attack_frames(0)==0&&NoiseUnit::attack_frames(1)==32&&NoiseUnit::attack_frames(32767)==64000);
    CHECK(NoiseUnit::attack_frames(16384)==16024);
    CHECK(NoiseUnit::mode_index(-32768)==0&&NoiseUnit::mode_index(8191)==0&&NoiseUnit::mode_index(8192)==1&&NoiseUnit::mode_index(24575)==1&&NoiseUnit::mode_index(24576)==2);
    for(int mode:{0,16384,32767})for(int decay:{0,32759,32760,32767})CHECK(NoiseUnit::drone_mode(q15(mode),q15(decay))==(mode==32767||(mode==0&&decay>=32760)));
    // Short and long attack endpoints; mid-stage parameter edits/control ticks
    // cannot relatch duration or overlap decay. The oracle is simple linear math.
    for(int raw:{1,32767}) {
        NoiseUnit u;noise_core_set(u,7,16384);noise_core_set(u,6,raw);noise_core_set(u,5,0);u.control_tick();CHECK(u.visual_envelope()==0);u.trigger();
        const uint32_t n=NoiseUnit::attack_frames(q15(raw));std::vector<q15> out(n/2);u.process(out.data(),int(out.size()));CHECK(out[0]==0&&u.visual_envelope()==uint64_t(32767)*(n/2)/n);
        noise_core_set(u,6,0);u.control_tick();u.process(out.data(),int(out.size()));CHECK(u.visual_envelope()==32767);
        q15 sample;u.process(&sample,1);CHECK(u.visual_envelope()<32767);u.trigger();CHECK(u.visual_envelope()==32767); // new zero attack is immediate
    }
    NoiseUnit u;noise_core_set(u,7,16384);noise_core_set(u,6,1);noise_core_set(u,5,0);u.control_tick();u.trigger();q15 buf[64]{};
    u.process(buf,8);const int current=u.visual_envelope();CHECK(current==32767*8/32);u.trigger();CHECK(u.visual_envelope()==current);
    u.process(buf,16);CHECK(u.visual_envelope()==current+(32767-current)*16/32);u.control_tick();u.process(buf,16);CHECK(u.visual_envelope()==32767);
    u.process(buf,64);const int fallen=u.visual_envelope();CHECK(fallen>0&&fallen<32767);const auto decay_base=u.params[5].base;
    noise_core_set(u,7,32767);u.control_tick();CHECK(u.visual_envelope()==fallen&&u.params[5].base==decay_base);u.process(buf,32);CHECK(u.visual_envelope()==32767);u.process(buf,64);CHECK(u.visual_envelope()==32767);
    noise_core_set(u,7,16384);noise_core_set(u,5,32767);u.control_tick();CHECK(u.sequencer_properties().lifecycle==SequenceLifecycle::Strike);u.process(buf,1);CHECK(u.visual_envelope()==32766&&u.params[5].base==32767);
    // Lifecycle follows real override ownership, not only base/effective.
    u.params[7].step_lock_override=true;u.params[7].step_lock=32767;CHECK(u.sequencer_properties().lifecycle==SequenceLifecycle::Flow);
    u.params[7].temp_override=true;u.params[7].temp=0;u.params[5].step_lock_override=true;u.params[5].step_lock=1000;CHECK(u.sequencer_properties().lifecycle==SequenceLifecycle::Strike);
    // A configured fresh drone starts once; repeated control ticks do not reset it.
    NoiseUnit drone;noise_core_set(drone,6,1);drone.control_tick();CHECK(drone.visual_envelope()==0);drone.process(buf,16);drone.control_tick();drone.process(buf,16);CHECK(drone.visual_envelope()==32767);drone.trigger();drone.process(buf,64);CHECK(drone.visual_envelope()==32767);
    // Identical 1kHz control/trigger schedule with differently subdivided audio.
    NoiseUnit a,b;for(NoiseUnit* voice:{&a,&b}){noise_core_set(*voice,7,16384);noise_core_set(*voice,6,4000);noise_core_set(*voice,5,10000);voice->reset();voice->control_tick();voice->trigger();}
    std::array<q15,32>x{},y{};
    for(int block=0;block<80;++block) {
        for(NoiseUnit* voice:{&a,&b}){if(block==7)noise_core_set(*voice,6,16384);if(block==12)voice->trigger();if(block==20)noise_core_set(*voice,7,32767);if(block==51)noise_core_set(*voice,7,16384);voice->control_tick();}
        a.process(x.data(),32);b.process(y.data(),7);b.process(y.data()+7,11);b.process(y.data()+18,14);CHECK(x==y&&a.visual_envelope()==b.visual_envelope());
    }
    std::puts("noise core envelope: endpoints/linear latch/retrigger/mode-entry/lifecycle and 80 partitioned blocks");
}

using noise_core_bytes=std::vector<uint8_t>;
struct noise_core_file {
    std::string path="/tmp/seance-noise-core-"+std::to_string((long)getpid())+".snc";
    ~noise_core_file(){std::remove(path.c_str());}
    noise_core_bytes read(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
    void put(const noise_core_bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
    bool load(Engine& e){patchio::SampleManifest manifest;return patchio::load(e,path.c_str(),&manifest);}
};
uint32_t noise_core_r32(const noise_core_bytes& b,size_t at){CHECK(at+4<=b.size());return b[at]|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;}
void noise_core_w16(noise_core_bytes& b,size_t at,int v){CHECK(at+2<=b.size());b[at]=uint8_t(v);b[at+1]=uint8_t(uint16_t(v)>>8);}
size_t noise_core_chunk(const noise_core_bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+noise_core_r32(b,p+4))if(std::equal(id,id+4,b.begin()+p))return p;CHECK(false);return 0;}
NoiseUnit& noise_core_unit(Engine& e){Unit* u=e.chains[0].units[0];CHECK(u&&u->kind==UnitKind::Noise&&u->num_params==8);return *static_cast<NoiseUnit*>(u);}
void noise_core_tail(const NoiseUnit& u,int first){for(int p=first;p<PARAM_BANK_SIZE;++p)CHECK(u.params[p].base==0&&u.params[p].effective==0&&u.params[p].target==0&&u.params[p].src==ModSource::None&&u.params[p].mod_depth==0&&!u.params[p].temp_override&&!u.params[p].step_lock_override);}
void noise_core_persistence() {
    noise_core_file f;auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();delete a->replace_unit(0,0,new NoiseUnit);auto& u=noise_core_unit(*a);
    noise_core_set(u,0,9001);noise_core_set(u,4,10001);noise_core_set(u,5,12001);noise_core_set(u,6,7100);noise_core_set(u,7,16384);
    u.params[6].src=ModSource::SigilB;u.params[6].mod_depth=-5011;u.params[7].src=ModSource::SigilD;u.params[7].mod_depth=16000;
    a->capture_scene(0,0);noise_core_set(u,6,32767);noise_core_set(u,7,32767);a->capture_scene(0,1);noise_core_set(u,6,7100);noise_core_set(u,7,16384);
    CHECK(a->set_step_lock(0,0,0,0,10000)&&a->set_step_lock(0,0,0,6,32767)&&a->set_step_lock(0,0,0,7,32767));
    auto& bank=a->pattern_banks[1].steps[0][0];bank.lock_count=2;bank.locks[0]={0,6,16384};bank.locks[1]={0,7,32767};
    auto& pad=a->macro_pads[0];pad.route_count=3;pad.routes[0]={0,0,0,0,8192};pad.routes[1]={0,0,6,0,32767};pad.routes[2]={0,0,7,16384,32767};
    CHECK(patchio::save(*a,f.path.c_str()));const auto modern=f.read();const size_t flags=noise_core_chunk(modern,"UNIT")+8,core=noise_core_chunk(modern,"CORE")+8;
    CHECK(modern[flags]==0x81&&f.load(*b));auto& got=noise_core_unit(*b);
    CHECK(got.params[6].base==7100&&got.params[6].src==ModSource::SigilB&&got.params[6].mod_depth==-5011&&got.params[7].base==16384&&got.params[7].src==ModSource::SigilD&&got.params[7].mod_depth==16000);
    CHECK(b->scenes[0][1].base[0][6]==32767&&b->scenes[0][1].base[0][7]==32767&&b->macro_pads[0].route_count==3&&b->pattern.steps[0][0].lock_count==3&&b->pattern_banks[1].steps[0][0].lock_count==2);
    std::unique_ptr<Unit> clone(clone_unit(&got));CHECK(clone&&clone->num_params==8&&clone->params[6].base==7100&&clone->params[7].src==ModSource::SigilD);
    CHECK(patchio::save(*b,f.path.c_str())&&f.read()==modern);
    // UNIT before CORE remains authored; finalization must not depend on order.
    auto reordered=modern;const size_t begin=flags-8,end=begin+8+noise_core_r32(modern,begin+4);noise_core_bytes unit(reordered.begin()+begin,reordered.begin()+end);
    reordered.erase(reordered.begin()+begin,reordered.begin()+end);reordered.insert(reordered.begin()+5,unit.begin(),unit.end());f.put(reordered);CHECK(f.load(*b)&&noise_core_unit(*b).params[6].base==7100);
    for(int old_flags:{0x80,0}) {
        auto old=modern;old[flags]=uint8_t(old_flags);for(int p=6;p<12;++p){noise_core_w16(old,core+8+p*5,30000);old[core+8+p*5+2]=uint8_t(ModSource::SigilA);noise_core_w16(old,core+8+p*5+3,32767);}f.put(old);
        CHECK(f.load(*b));auto& legacy=noise_core_unit(*b);noise_core_tail(legacy,6);CHECK(legacy.params[0].base==9001&&legacy.params[4].base==(old_flags?10001:0)&&legacy.params[5].base==(old_flags?12001:32767));
        CHECK(b->scenes[0][0].base[0][6]==0&&b->scenes[0][1].base[0][7]==0&&b->macro_pads[0].route_count==1&&b->pattern.steps[0][0].lock_count==1&&b->pattern_banks[1].steps[0][0].lock_count==0);
        noise_core_legacy oracle;legacy.control_tick();oracle.tick(legacy);std::vector<q15> actual(128);legacy.process(actual.data(),128);CHECK(actual==oracle.process(legacy,128));
        b->apply_scene_morph(0,32767);b->apply_macro_pad(0,32767);noise_core_tail(legacy,6);
        CHECK(patchio::save(*b,f.path.c_str()));CHECK(f.read()[noise_core_chunk(f.read(),"UNIT")+8]==0x81);CHECK(f.load(*b));noise_core_tail(noise_core_unit(*b),6);
    }
    auto dirty=modern;noise_core_w16(dirty,core+8+8*5,30000);dirty[core+8+8*5+2]=1;f.put(dirty);CHECK(f.load(*b));noise_core_tail(noise_core_unit(*b),8);CHECK(noise_core_unit(*b).params[6].base==7100);
    for(int invalid:{0x82,1}){dirty=modern;dirty[flags]=uint8_t(invalid);f.put(dirty);Unit* sentinel=b->chains[0].units[0];CHECK(!f.load(*b)&&b->chains[0].units[0]==sentinel);}
    // One historical SNC6 record; old flags cannot enable new parameter slots.
    noise_core_bytes old={'S','N','C','6',6};
    for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127,0,0,0});const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){old[first]=uint8_t(UnitKind::Noise);for(int p=0;p<6;++p){noise_core_w16(old,first+3+p*5,p==0?9001:20000);old[first+3+p*5+2]=uint8_t(ModSource::SigilA);noise_core_w16(old,first+3+p*5+3,19001);}}}
    old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);const size_t old_flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[old_flags]=1;
    old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);f.put(old);
    CHECK(f.load(*b));auto& legacy=noise_core_unit(*b);noise_core_tail(legacy,6);CHECK(legacy.params[0].base==9001&&legacy.params[0].src==ModSource::SigilA&&legacy.params[4].base==0&&legacy.params[5].base==32767);
    std::puts("noise core persistence: SNC7 attack/mode/routes/scenes/locks/clone/roundtrip, six/unflagged/SNC6 migration and flag rollback");
}
void run_noise_core_tests(){noise_core_legacy_exact();noise_core_attack_modes();noise_core_persistence();}
