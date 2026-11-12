// Include inside seance. Relic is retired; retain literal legacy route semantics.
RelicUnit* Engine::relic_at(int,int) const { return nullptr; }
RelicUnit* Engine::relic_by_id(uint32_t,int*,int*) const { return nullptr; }
bool Engine::can_install_unit(const Unit* u,const Unit* replacing) const {
    if(!u)return true;
    if(u->kind==UnitKind::Relic)return false;
    for(const Param& p:u->params)if(p.relic_source)return false;
    if(!u->ready())return false;
    for(const auto& c:chains)for(const Unit* old:c.units)if(old==u && old!=replacing)return false;
    return true;
}
bool Engine::typed_mod_target_allowed(const Unit* u,int p) {
    if(!u || p<0 || p>=u->num_params || p>=PARAM_BANK_SIZE)return false;
    uint16_t mask=0;
    const auto bit=[](int n){return uint16_t(1u<<n);};
    switch(u->kind) {
    case UnitKind::Filter:mask=15;break;
    case UnitKind::Vca:mask=127; if(static_cast<const VcaUnit*>(u)->params[VcaUnit::P_SYNC].control_source()>16383)mask=bit(VcaUnit::P_GAIN)|bit(VcaUnit::P_FLOOR)|bit(VcaUnit::P_A_CURVE)|bit(VcaUnit::P_D_CURVE);break;
    case UnitKind::SamplePlayer:mask=bit(PlayUnit::P_SPEED)|bit(PlayUnit::P_START)|bit(PlayUnit::P_LEN)|bit(PlayUnit::P_GAIN)|bit(PlayUnit::P_POSITION);break;
    case UnitKind::Grain:mask=0x7ff & ~bit(GrainUnit::P_VOICES);if(static_cast<const GrainUnit*>(u)->clean)mask|=bit(GrainUnit::P_SCAN);if(static_cast<const GrainUnit*>(u)->clock_sync)mask&=~bit(GrainUnit::P_DENS);break;
    case UnitKind::Osc:mask=0x7f;break;
    case UnitKind::Delay:mask=0x1f|bit(DelayUnit::P_TONE);if(u->params[DelayUnit::P_SYNC].control_source()>16383)mask&=~bit(DelayUnit::P_TIME);break;
    case UnitKind::Fold:mask=0x7f;if(!static_cast<const FoldUnit*>(u)->modern())mask&=~bit(FoldUnit::P_MODE);break;
    case UnitKind::Bitcrush:mask=0xfff & ~(bit(BitUnit::P_BITS)|bit(BitUnit::P_COMP));break;
    case UnitKind::Chorus:mask=15;break;
    case UnitKind::Reverb:mask=0xff & ~bit(ReverbUnit::P_FREEZE);break;
    case UnitKind::Stranular:mask=0x1bf;if(u->params[StranularUnit::P_SYNC].control_source()>16383)mask&=~bit(StranularUnit::P_DENS);break;
    case UnitKind::Ghost:mask=0xff & ~bit(GhostUnit::P_FREEZE);break;
    case UnitKind::Ring:mask=0xfff & ~(bit(RingUnit::P_MODE)|bit(RingUnit::P_WAVE)|bit(RingUnit::P_NOTE));break;
    case UnitKind::Tilt:mask=0x3f;if(!static_cast<const TiltUnit*>(u)->extended())mask&=~bit(TiltUnit::P_RANGE);break;
    case UnitKind::Noise:mask=0xfff & ~(bit(NoiseUnit::P_COLOUR)|bit(NoiseUnit::P_MODE));break;
    case UnitKind::Fm:mask=0xfff;break;
    case UnitKind::Motor:mask=0xfff & ~(bit(MotorUnit::P_SOURCE)|bit(MotorUnit::P_SYNC)|bit(MotorUnit::P_MODE)|bit(MotorUnit::P_NOTE));break;
    case UnitKind::Strain:mask=0x1ff;break;
    case UnitKind::Shift:mask=15;break;
    case UnitKind::Bird:mask=0xff & ~bit(BirdUnit::P_SYLLABLE);break;
    case UnitKind::Fract:mask=bit(FractUnit::P_CHANCE)|bit(FractUnit::P_DAMAGE)|bit(FractUnit::P_SPEED)|bit(FractUnit::P_MIX);break;
    case UnitKind::Comp:mask=0xff;if(u->params[CompUnit::P_CLOCK].control_source()>16383)mask&=~bit(CompUnit::P_RELEASE);break;
    case UnitKind::Mouth:mask=31;break;
    case UnitKind::Glitch:mask=0x7f;break;
    case UnitKind::Scratch:mask=0x3f;if(static_cast<const ScratchUnit*>(u)->mode==ScratchUnit::Mode::Scratch)mask&=~(bit(ScratchUnit::P_WINDOW)|bit(ScratchUnit::P_PITCH));break;
    case UnitKind::Paul:mask=bit(PaulUnit::P_POSITION)|bit(PaulUnit::P_STRETCH)|bit(PaulUnit::P_GAIN)|bit(PaulUnit::P_PITCH)|bit(PaulUnit::P_START)|bit(PaulUnit::P_END);break;
    case UnitKind::Inst:mask=0xffc;break; // real continuous scalar controls; source and quantized NOTE excluded
    // REC, MIC commands, ARP clock/notes and RELIC feedback excluded.
    default:break;
    }
    return (mask&bit(p))!=0;
}
q15 Engine::parameter_modulation_value(const Unit* u,int p) const {
    if(!u || unsigned(p)>=PARAM_BANK_SIZE)return 0;
    const Param& v=u->params[p];
    if(!v.relic_source)return v.src==ModSource::None?0:modulation_value(v.src); // literal old path
    return 0; // retired sources never resolve or run; public orphan scalars cannot activate them
}
SourceRef Engine::param_source(int c,int s,int p) const {
    if(unsigned(c)>=NUM_CHAINS || unsigned(s)>=UNITS_PER_CHAIN || unsigned(p)>=PARAM_BANK_SIZE)return {};
    const Unit* u=chains[c].units[s];if(!u || p>=u->num_params)return {};
    const Param& v=u->params[p];return v.relic_source?SourceRef::from_relic(v.relic_source,relic::Port(v.relic_port)):SourceRef::from_legacy(v.src);
}
bool Engine::replace_mod_route(int c,int s,int p,SourceRef r,q15 depth,bool enabled) {
    if(unsigned(c)>=NUM_CHAINS || unsigned(s)>=UNITS_PER_CHAIN || unsigned(p)>=PARAM_BANK_SIZE)return false;
    Unit* u=chains[c].units[s];if(!u || p>=u->num_params)return false;
    if(r.type==SourceRef::Type::Relic) {
        return false; // no typed Relic route can be created
    } else if(r.type==SourceRef::Type::Legacy) {
        if(r.legacy==ModSource::None || r.legacy>=ModSource::Count || r.instance)return false;
    } else if(r.type!=SourceRef::Type::None || r.instance || r.legacy!=ModSource::None)return false;
    invalidate_structural_undo();
    Param& v=u->params[p];v.src=r.type==SourceRef::Type::Legacy?r.legacy:ModSource::None;
    v.relic_source=r.type==SourceRef::Type::Relic?r.instance:0;v.relic_port=v.relic_source?uint8_t(r.port):0;
    v.mod_depth=r.type==SourceRef::Type::None?0:depth;v.mod_enabled=r.type==SourceRef::Type::None?true:enabled;return true;
}
bool Engine::connect_mod_route(int c,int s,int p,SourceRef r,q15 d,bool enabled) {
    if(unsigned(c)>=NUM_CHAINS || unsigned(s)>=UNITS_PER_CHAIN || unsigned(p)>=PARAM_BANK_SIZE)return false;
    const Unit* u=chains[c].units[s];if(!u || p>=u->num_params || u->params[p].src!=ModSource::None || u->params[p].relic_source)return false;
    return r.type!=SourceRef::Type::None && replace_mod_route(c,s,p,r,d,enabled);
}
bool Engine::remove_mod_route(int c,int s,int p){return replace_mod_route(c,s,p,{},0,true);}
int Engine::capture_relic_sources(RelicSourceInfo*,int) const { return 0; }
int Engine::capture_mod_routes(ModRouteInfo* out,int capacity) const {
    int n=0;for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s){const Unit* u=chains[c].units[s];if(!u)continue;
        for(int p=0;p<u->num_params;++p){const auto r=param_source(c,s,p);if(r.type==SourceRef::Type::None)continue;
            if(out && n<capacity){auto& v=out[n];v={};v.source=r;v.chain=c;v.slot=s;v.param=p;v.kind=u->kind;v.depth=u->params[p].mod_depth;
                v.enabled=u->params[p].mod_enabled;v.value=parameter_modulation_value(u,p);v.epoch=relic_epoch_;}++n;}
    }return n;
}
bool Engine::relic_storage_valid() const {
    for(const auto& c:chains)for(const Unit* u:c.units)if(u) {
        if(u->kind==UnitKind::Relic)return false;
        for(const Param& p:u->params)if(p.relic_source)return false;
    }
    for(const auto& c:scenes)for(const auto& scene:c)for(UnitKind k:scene.kind)
        if(k==UnitKind::Relic)return false; // including inactive scene state
    return true;
}
void Engine::cleanup_relic_routes(const Unit*) {}
void Engine::prepare_relic_context() {}
void Engine::render_relic_fragments(uint32_t) {}
void Engine::publish_relic_bank() {}
uint32_t Engine::begin_relic_gesture(int,int,int,int) { return 0; }
bool Engine::relic_gesture_active(uint32_t) const { return false; }
bool Engine::set_relic_gesture(uint32_t,int,int,q15) { return false; }
void Engine::end_relic_gesture(uint32_t token) { cancel_relic_gesture(token); }
void Engine::cancel_relic_gesture(uint32_t) { relic_gesture_={}; }
bool Engine::set_relic_hold(int,int,bool) { return false; }
bool Engine::set_relic_audio_enabled(int,int,bool) { return false; }
bool Engine::retarget_relic_source(int,int,int) { return false; }
void Engine::capture_relic_visual(int,int,RelicUnit::Visual& out) const {
    ::new(static_cast<void*>(&out)) RelicUnit::Visual{};
}
void Engine::retire_relic_sources_for_slot(int) {}
void Engine::retire_relic_sources_all() { cancel_relic_gesture(); }
void Engine::hydrate_relic_sources() {}
