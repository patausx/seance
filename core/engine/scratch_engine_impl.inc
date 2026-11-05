// Paired controller transaction; include inside seance in engine.cpp.
uint32_t Engine::begin_scratch_gesture(int chain,int slot) {
    return begin_scratch_gesture_impl(chain,slot,false,0,0,0);
}
uint32_t Engine::begin_scratch_gesture_at(int chain,int slot,q15 x,q15 y,q15 source_position) {
    return begin_scratch_gesture_impl(chain,slot,true,x,y,source_position);
}
uint32_t Engine::begin_scratch_gesture_impl(int chain,int slot,bool absolute,q15 x,q15 y,q15 position) {
    // All refusal checks precede publication, stale-owner cleanup, voice arming
    // and parameter resolution. An occupied/protected begin is a pure refusal.
    if(chain<0 || chain>=NUM_CHAINS || slot<0 || slot>=UNITS_PER_CHAIN ||
        chains[chain].mute || recording_active_or_pending())return 0;
    Unit* base=chains[chain].units[slot];
    if(!base || base->kind!=UnitKind::Scratch || base->bypass)return 0;
    auto* unit=static_cast<ScratchUnit*>(base);
    const auto source=unit->source_binding();
    if(!source.frames || !source.data || unit->mode>ScratchUnit::Mode::Hold || unit->map>ScratchUnit::Map::SpeedPitch)return 0;
    int ax,ay;unit->axis_params(ax,ay);
    Param& px=unit->params[ax];Param& py=unit->params[ay];
    const bool own_stale_x=unit->temp_owner_[ax] && unit->temp_owner_[ax]==unit->touch_token_ && !unit->configuration_matches();
    const bool own_stale_y=unit->temp_owner_[ay] && unit->temp_owner_[ay]==unit->touch_token_ && !unit->configuration_matches();
    if((px.temp_override && !own_stale_x) || (py.temp_override && !own_stale_y) ||
        px.step_lock_override || py.step_lock_override)return 0;
    if(scratch_gesture_.token && scratch_gesture_active(scratch_gesture_.token))return 0;
    const q15 origin_x=unit->axis_control_source(ax),origin_y=unit->axis_control_source(ay);
    if(scratch_gesture_.token)cancel_scratch_gesture();
    unit->engine_=this;unit->chain_=int8_t(chain);unit->slot_=int8_t(slot);unit->synchronize();
    if(!++scratch_token_)++scratch_token_;
    px.temp=std::max<q15>(px.bipolar?-32767:0,absolute?x:origin_x);
    py.temp=std::max<q15>(py.bipolar?-32767:0,absolute?y:origin_y);
    unit->temp_owner_[ax]=unit->temp_owner_[ay]=scratch_token_;px.temp_override=py.temp_override=true;
    // Absolute initialization snaps BOTH axes before the voice is armed; other
    // parameters also snap on a fresh voice. Modulation follows control sources.
    for(int p=0;p<unit->num_params;++p)if(!unit->voice_ || (absolute && (p==ax || p==ay))) {
        unit->params[p].slew_ready=false;unit->resolve_param(p,modulation_value(unit->params[p].src));
    }
    unit->touch_begin(scratch_token_);
    if(unit->touch_token_!=scratch_token_) {
        for(int p:{ax,ay})if(unit->temp_owner_[p]==scratch_token_){unit->params[p].temp_override=false;unit->temp_owner_[p]=0;}
        return 0;
    }
    scratch_gesture_={unit,source,unit->mode,unit->map,int8_t(chain),int8_t(slot),int8_t(ax),int8_t(ay),scratch_token_,unit->epoch_};
    if(absolute){unit->control_tick();unit->reanchor(position);}
    return scratch_token_;
}
bool Engine::scratch_gesture_active(uint32_t token) const {
    const auto& g=scratch_gesture_;
    if(!token || token!=g.token || !g.unit || g.chain<0 || g.slot<0 ||
        chains[int(g.chain)].units[int(g.slot)]!=g.unit || chains[int(g.chain)].mute ||
        recording_active_or_pending())return false;
    const auto& u=*g.unit;
    int x,y;u.axis_params(x,y);
    return !u.bypass && u.kind==UnitKind::Scratch && u.mode==g.mode && u.map==g.map &&
        u.configuration_matches() && x==g.x && y==g.y &&
        u.epoch_==g.epoch && u.touch_token_==token && u.source_matches(g.source) &&
        u.params[int(g.x)].temp_override && u.params[int(g.y)].temp_override &&
        u.temp_owner_[int(g.x)]==token && u.temp_owner_[int(g.y)]==token;
}
bool Engine::set_scratch_gesture(uint32_t token,q15 x,q15 y) {
    if(!token)return false;
    if(!scratch_gesture_active(token)) { cancel_scratch_gesture(token);return false; }
    auto& g=scratch_gesture_;Param& px=g.unit->params[int(g.x)];Param& py=g.unit->params[int(g.y)];
    // One guard-owned publication, shared modulation resolves AFTER both axes.
    px.temp=std::max<q15>(px.bipolar?-32767:0,x);py.temp=std::max<q15>(py.bipolar?-32767:0,y);
    g.unit->resolve_param(g.x,modulation_value(px.src));
    g.unit->resolve_param(g.y,modulation_value(py.src));g.unit->control_tick();
    return true;
}
void Engine::end_scratch_gesture(uint32_t token) {
    if(!token)return;
    if(scratch_gesture_active(token) && scratch_gesture_.unit->latch_) {
        auto& g=scratch_gesture_;
        g.unit->latch_x_=g.unit->params[int(g.x)].temp;
        g.unit->latch_y_=g.unit->params[int(g.y)].temp;
    }
    cancel_scratch_gesture(token);
}
void Engine::cancel_scratch_gesture(uint32_t token) {
    auto& g=scratch_gesture_;if(!g.token || (token && token!=g.token))return;
    // Check installation before dereferencing a possibly retired pointer.
    if(g.chain>=0 && g.slot>=0 && chains[int(g.chain)].units[int(g.slot)]==g.unit && g.unit) {
        for(int p:{int(g.x),int(g.y)})if(g.unit->temp_owner_[p]==g.token) {
            g.unit->params[p].temp_override=false;g.unit->temp_owner_[p]=0;
        }
        g.unit->touch_end(g.token); // leaves independent sequence/manual/latch gates intact
        g.unit->synchronize();
        if(recording_active_or_pending() && !g.unit->sequence_gate_)g.unit->reset();
    }
    g={}; // cancellation never commits into latch or restores stale lower values
}
bool Engine::set_scratch_latch(int chain,int slot,bool enabled) {
    if(chain<0 || chain>=NUM_CHAINS || slot<0 || slot>=UNITS_PER_CHAIN)return false;
    Unit* base=chains[chain].units[slot];if(!base || base->kind!=UnitKind::Scratch)return false;
    auto* u=static_cast<ScratchUnit*>(base);
    if(!enabled){u->latch_=false;return true;}
    if(base->bypass || chains[chain].mute || recording_active_or_pending() ||
        u->mode>ScratchUnit::Mode::Hold || u->map>ScratchUnit::Map::SpeedPitch)return false;
    const auto source=u->source_binding();if(!source.frames || !source.data)return false;
    int x,y;u->axis_params(x,y);
    for(int p:{x,y}) {
        const Param& param=u->params[p];
        const bool own=u->touch_token_ && scratch_gesture_active(u->touch_token_) && u->temp_owner_[p]==u->touch_token_;
        if(param.performance_override || param.step_lock_override || (param.temp_override && !own))return false;
    }
    if(u->latched())return true;
    const q15 vx=u->params[x].control_source(),vy=u->params[y].control_source();
    u->engine_=this;u->chain_=int8_t(chain);u->slot_=int8_t(slot);u->synchronize();
    if(!u->voice_) {
        for(int p=0;p<u->num_params;++p){u->params[p].slew_ready=false;u->resolve_param(p,modulation_value(u->params[p].src));}
        if(!u->begin_voice())return false;
    }
    u->latch_x_=vx;u->latch_y_=vy;u->latch_=true;
    return true;
}
void Engine::capture_scratch_visual(int chain,int slot,ScratchUnit::Visual& out) const {
    out={};if(chain<0 || chain>=NUM_CHAINS || slot<0 || slot>=UNITS_PER_CHAIN)return;
    const Unit* u=chains[chain].units[slot];if(!u || u->kind!=UnitKind::Scratch)return;
    out=static_cast<const ScratchUnit*>(u)->visual();
    if(chains[chain].mute || u->bypass) {out.active=false;out.position_q16=0;out.speed_q16=0;out.window=0;}
}
