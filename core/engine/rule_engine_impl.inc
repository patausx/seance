namespace seance {
RuleUnit* Engine::bind_rule(int chain,int slot) {
    if(unsigned(chain)>=NUM_CHAINS || unsigned(slot)>=UNITS_PER_CHAIN)return nullptr;
    Unit* u=chains[chain].units[slot];if(!u || u->kind!=UnitKind::Rule)return nullptr;
    auto* r=static_cast<RuleUnit*>(u);r->engine_=this;r->chain_=int8_t(chain);r->slot_=int8_t(slot);return r;
}
void Engine::rebind_rules(int chain) {for(int s=0;s<UNITS_PER_CHAIN;++s)bind_rule(chain,s);}
void Engine::capture_rule_visual(int chain,int slot,RuleUnit::Visual& out) const {
    out={};if(unsigned(chain)>=NUM_CHAINS || unsigned(slot)>=UNITS_PER_CHAIN)return;
    const auto* u=chains[chain].units[slot];if(!u || u->kind!=UnitKind::Rule)return;
    out=static_cast<const RuleUnit*>(u)->visual();
    const auto& c=chains[chain];bool overwritten=false;
    for(int s=slot+1;s<UNITS_PER_CHAIN;++s)if(c.units[s] && !c.units[s]->bypass && c.units[s]->sequencer_properties().source)overwritten=true;
    if(c.mute || c.volume<=0 || u->bypass || bounce_preview_active() || !chain_audio_channels(chain) || overwritten) {
        out.active=false;out.sounding=0;out.voices=0;for(auto& spoke:out.spoke)spoke=0;
    }
}
}
