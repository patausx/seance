// Audio/controller guarded PAUL front doors; no UI FFT or snapshot expansion.
namespace seance {
PaulUnit* Engine::bind_paul(int chain,int slot) {
    Unit* base=chains[chain].units[slot];if(!base || base->kind!=UnitKind::Paul)return nullptr;
    auto* p=static_cast<PaulUnit*>(base);p->engine_=this;p->chain_=int8_t(chain);p->slot_=int8_t(slot);return p;
}
void Engine::capture_paul_visual(int chain,int slot,PaulUnit::Visual& out) const {
    out={};if(chain<0 || chain>=NUM_CHAINS || slot<0 || slot>=UNITS_PER_CHAIN)return;
    const Unit* u=chains[chain].units[slot];if(u && u->kind==UnitKind::Paul)out=static_cast<const PaulUnit*>(u)->visual();
}
} // namespace seance
