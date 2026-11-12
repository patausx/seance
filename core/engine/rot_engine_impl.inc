namespace seance {
RotUnit* Engine::bind_rot(int chain,int slot){
    if(unsigned(chain)>=NUM_CHAINS || unsigned(slot)>=UNITS_PER_CHAIN)return nullptr;
    Unit* u=chains[chain].units[slot];if(!u || u->kind!=UnitKind::Rot)return nullptr;
    auto* r=static_cast<RotUnit*>(u);r->engine_=this;r->chain_=int8_t(chain);r->slot_=int8_t(slot);return r;
}
void Engine::rebind_rots(int chain){for(int s=0;s<UNITS_PER_CHAIN;++s)bind_rot(chain,s);}
void Engine::capture_rot_visual(int chain,int slot,RotUnit::Visual& out) const {
    out={};if(unsigned(chain)>=NUM_CHAINS || unsigned(slot)>=UNITS_PER_CHAIN)return;
    const Unit* u=chains[chain].units[slot];if(!u || u->kind!=UnitKind::Rot)return;
    out=static_cast<const RotUnit*>(u)->visual();const auto& c=chains[chain];bool overwritten=false;
    for(int s=slot+1;s<UNITS_PER_CHAIN;++s)if(c.units[s] && !c.units[s]->bypass && c.units[s]->sequencer_properties().source)overwritten=true;
    if(c.mute || c.volume<=0 || u->bypass || bounce_preview_active() || !chain_audio_channels(chain) || overwritten)out.active=false;
}
void Engine::capture_rot_artwork(int chain,int slot,RotUnit::Artwork& out) const {
    out.valid=false;out.history=0;out.preparing=false;
    if(unsigned(chain)>=NUM_CHAINS || unsigned(slot)>=UNITS_PER_CHAIN)return;
    const Unit* u=chains[chain].units[slot];if(u && u->kind==UnitKind::Rot)static_cast<const RotUnit*>(u)->copy_artwork(out);
}
}
