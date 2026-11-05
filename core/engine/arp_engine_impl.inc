// Included in engine.cpp. No retained Unit pointers: derive six-slot ownership
// at every render boundary and musical trigger (also before the first render).
namespace seance {
int Engine::arp_owner_for(int chain,int slot)const{
    const Chain& c=chains[chain]; const Unit* target=c.units[slot];
    if(c.mute || !target || target->bypass || !ArpUnit::accepts(target))return -1;
    if(target->kind==UnitKind::SamplePlayer && static_cast<const PlayUnit*>(target)->preview_active())return -1;
    for(int s=slot-1;s>=0;--s){
        const Unit* u=c.units[s];if(!u || u->bypass)continue;
        if(ArpUnit::accepts(u))return -1; // it is the nearer supported source
        if(u->kind==UnitKind::Arp)return s;
    }
    return -1;
}
int Engine::arp_trigger_owner_for(int chain,int slot)const{
    const Unit* unit=chains[chain].units[slot];
    if(!unit)return -1;
    if(ArpUnit::accepts(unit))return arp_owner_for(chain,slot);
    // Extended VCA accepts notes even when duck/floor keeps a source flowing.
    // Do not mislabel it a gate_processor: source lifecycle metadata is unchanged.
    const bool vca_receiver=unit->kind==UnitKind::Vca && static_cast<const VcaUnit*>(unit)->modern();
    if(unit->bypass || (!unit->sequencer_properties().gate_processor && !vca_receiver))return -1;
    for(int s=slot-1;s>=0;--s){
        const Unit* u=chains[chain].units[s];
        if(u && !u->bypass && u->sequencer_properties().source)return arp_owner_for(chain,s);
    }
    return -1;
}
void Engine::clear_arp_runtime(int chain){
    for(int c=0;c<NUM_CHAINS;++c)if(chain<0 || chain==c)
        for(Unit* u:chains[c].units)if(u){
            if(u->arp_voice.owner>=0 || u->arp_voice.semitones){u->set_arp_pitch(0);u->arp_voice={};}
            if(u->kind==UnitKind::Arp)static_cast<ArpUnit*>(u)->reset();
        }
}
uint64_t Engine::arp_phase()const{
    return clock_runtime_ ? clock_phase_ : (uint64_t(beat_count)<<32)+
        uint64_t(frame_in_beat)*(uint64_t(1)<<32)/frames_per_beat();
}
void Engine::update_arps(){
    const uint64_t phase=arp_phase();
    for(int c=0;c<NUM_CHAINS;++c){
        // Clear the previous owner before any newly mapped arp fires a note.
        for(int s=0;s<UNITS_PER_CHAIN;++s){
            Unit* u=chains[c].units[s];if(!u)continue;
            const int owner=arp_owner_for(c,s);
            if(u->arp_voice.owner!=owner){
                u->set_arp_pitch(0);u->arp_voice={};u->arp_voice.owner=int8_t(owner);
                if(owner>=0)u->arp_voice.gain=0;
            }
        }
        for(int s=0;s<UNITS_PER_CHAIN;++s){
            Unit* u=chains[c].units[s];if(!u || u->kind!=UnitKind::Arp)continue;
            auto* arp=static_cast<ArpUnit*>(u);
            int target=-1;
            for(int t=s+1;t<UNITS_PER_CHAIN;++t){
                Unit* next=chains[c].units[t];
                if(next && !next->bypass && ArpUnit::accepts(next)){target=t;break;}
            }
            ArpUnit::State state=ArpUnit::State::Running;
            if(u->bypass)state=ArpUnit::State::Bypassed;
            else if(chains[c].mute)state=ArpUnit::State::Muted;
            else if(target<0)state=ArpUnit::State::NoTarget;
            else if(arp_owner_for(c,target)!=s)state=ArpUnit::State::Conflict;
            else if(!transport_playing)state=ArpUnit::State::Stopped;
            if(state!=ArpUnit::State::Running){
                arp->suspend(state,target);
                if(target>=0 && chains[c].units[target]->arp_voice.owner==s){
                    chains[c].units[target]->set_arp_pitch(0);
                    chains[c].units[target]->arp_voice.gate=false;
                }
                continue;
            }
            Unit* source=chains[c].units[target];
            if(auto* p=bind_paul(c,target))p->synchronize();
            const bool note=arp->advance(phase,target);
            source->arp_voice.gate=arp->visual_.gate;
            source->set_arp_pitch(arp->visual_.active ? arp->visual_.semitone : 0);
            if(note){
                bool strike=true;
                if(source->kind==UnitKind::SamplePlayer && ArpUnit::play_legato(arp->params[ArpUnit::P_PLAY_MODE].target))
                    strike=!arp->voice_started_; // finite one-shot remains finished until a fresh run/reset
                if(strike){source->trigger();source->control_tick();}
                // PAUL's native manual duration must not close an ARP-owned voice
                // a second time. ARP gates once in the shared source gain layer.
                if(source->kind==UnitKind::Paul) {
                    auto* p=static_cast<PaulUnit*>(source);p->manual_remaining_=0;p->sequence_gate_=true;
                    p->gate_deadline_=UINT64_MAX;
                }
                arp->voice_started_=true;
                // Only downstream gate processors (e.g. VCA), never REC/commands.
                for(int t=target+1;t<UNITS_PER_CHAIN;++t){
                    Unit* processor=chains[c].units[t];
                    if(processor && arp_trigger_owner_for(c,t)==s)processor->trigger();
                }
            }
        }
    }
}
int Engine::arp_render_limit(int frames)const{
    if(!transport_playing)return frames;
    const uint64_t phase=arp_phase();
    for(const auto& c:chains)for(const Unit* u:c.units)if(u && u->kind==UnitKind::Arp){
        const uint64_t deadline=static_cast<const ArpUnit*>(u)->deadline();
        if(deadline==UINT64_MAX || deadline<=phase)continue;
        uint64_t remaining;
        if(clock_runtime_)remaining=(deadline-phase+clock_increment_-1)/clock_increment_;
        else{
            const uint64_t fpb=frames_per_beat();
            const uint64_t target=(deadline>>32)*fpb+(((deadline&0xffffffffu)*fpb+0xffffffffu)>>32);
            const uint64_t now=uint64_t(beat_count)*fpb+frame_in_beat;
            remaining=target>now ? target-now : 1;
        }
        if(remaining<uint64_t(frames))frames=int(remaining);
    }
    return frames;
}
} // namespace seance
