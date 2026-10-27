// Included inside patch_io.cpp's namespace after helpers/types. Commit after
// every chunk: UNIT can precede CORE, and old padding must never enable modes.
static void canonicalize_vca(Engine& target,int chain,int slot,uint8_t flags) {
    auto* u=static_cast<VcaUnit*>(target.chains[chain].units[slot]);
    u->set_flexible((flags&1u)!=0);
    u->set_modern((flags&2u)!=0);
    u->set_clock_bpm_q16(target.effective_bpm_q16());
    const int first=u->num_params;
    for(int p=first;p<PARAM_BANK_SIZE;++p) {
        const char* name=u->params[p].name;u->params[p]=Param{};u->params[p].name=name;
    }
    for(auto& scene:target.scenes[chain])if(scene.kind[slot]==UnitKind::Vca)
        for(int p=first;p<MAX_PARAMS;++p)scene.base[slot][p]=0;
    for(auto& pad:target.macro_pads) {
        int kept=0;
        for(int i=0;i<pad.route_count;++i) {
            const auto r=pad.routes[i];
            if(r.chain==chain&&r.slot==slot&&r.param>=first)continue;
            pad.routes[kept++]=r;
        }
        for(int i=kept;i<pad.route_count;++i)pad.routes[i]=MacroPadRoute{};
        pad.route_count=uint8_t(kept);
    }
    const auto strip=[=](Pattern& pattern) {
        for(auto& step:pattern.steps[chain]) {
            int kept=0;
            for(int i=0;i<step.lock_count;++i) {
                const auto l=step.locks[i];
                if(l.slot==slot&&l.param>=first)continue;
                step.locks[kept++]=l;
            }
            for(int i=kept;i<step.lock_count;++i)step.locks[i]=StepLock{};
            step.lock_count=uint8_t(kept);
        }
    };
    strip(target.pattern);for(auto& pattern:target.pattern_banks)strip(pattern);
}
