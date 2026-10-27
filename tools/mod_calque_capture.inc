// Design-study capture entry, appended to a TEMPORARY copy of the current host
// driver. No production source or existing harness is modified. No suite runs.
namespace mod_calque_capture {
namespace {
using namespace fader_lancet_cases;
void save(const std::filesystem::path& out, const std::string& name, const Pair& images) {
    width=400; pixels=images.top; dump_surface(out,name+"_top");
    width=320; pixels=images.bottom; dump_surface(out,name+"_bottom");
}
void route(Scene& scene, int param) {
    auto& p=scene.unit->params[param];
    p.src=ModSource::SigilB; p.mod_depth=11000; p.mod_enabled=true; p.snap();
    for(int i=0;i<SIGIL_POINTS;++i)scene.engine->sigils[1].set_point(i,q15(19000));
    auto config=scene.engine->sigil_config[1]; config.enabled=true; config.legacy=true;
    scene.engine->set_sigil_config(1,config); scene.engine->set_sigil_sync(1,false);
    scene.engine->reset_sigil(1); scene.select(param); scene.render(4096); scene.capture();
}
void one(const std::filesystem::path& out, std::ofstream& manifest,
         palette::Theme theme, UnitKind kind) {
    palette::set(theme); host_time_ms=1000;
    Scene scene(kind);
    int selected=0;
    if(kind==UnitKind::Filter) {
        auto& unit=*static_cast<FilterUnit*>(scene.unit); unit.type=FilterMode::LPF;
        scene.set(FilterUnit::P_CUTOFF,12000); scene.set(FilterUnit::P_RES,6000);
        scene.set(FilterUnit::P_DRIVE,5000); scene.set(FilterUnit::P_MORPH,12000);
        selected=FilterUnit::P_CUTOFF;
    } else {
        scene.set(ReverbUnit::P_SIZE,18000); scene.set(ReverbUnit::P_DECAY,20000);
        scene.set(ReverbUnit::P_DAMP,9000); scene.set(ReverbUnit::P_MIX,9000);
        selected=ReverbUnit::P_MIX;
    }
    route(scene,selected);
    const std::string name=std::string(theme==palette::Theme::Graphite?"graphite_":"pink_")+
                           (kind==UnitKind::Filter?"filter":"rev");
    case_name=name+" module";
    const auto ordinary=pair(scene,false);
    save(out,name+"_module",ordinary);
    const uint64_t clock=scene.engine->transport_frame;
    const auto before=scene.unit->params[selected];
    u32 down=KEY_L; CHECK(param_lens_input(down,KEY_L,0,false));
    CHECK(param_lens_is_open()); param_lens_touch_release(); scene.capture();
    case_name=name+" held l";
    const auto lens=pair(scene,false);
    save(out,name+"_lens",lens);
    const auto& after=scene.unit->params[selected];
    CHECK(scene.engine->transport_frame==clock);
    CHECK(before.base==after.base && before.effective==after.effective &&
          before.mod_depth==after.mod_depth && before.src==after.src &&
          before.mod_enabled==after.mod_enabled);
    ParamInspector detail{};
    scene.engine->capture_param_inspector(0,scene.slot,selected,detail);
    CHECK(detail.valid);
    surface(false); menu_style::paper(320); dump_surface(out,name+"_paper");
    manifest<<name<<'\t'<<selected<<'\t'<<detail.base<<'\t'<<detail.effective<<'\t'
            <<detail.depth<<'\t'<<clock<<'\t'<<ordinary.upper+ordinary.lower<<'\t'
            <<lens.upper+lens.lower<<'\n';
    param_lens_cancel();
}
} // namespace
int run(const char* directory) {
    try {
        const std::filesystem::path out(directory);
        CHECK(!std::filesystem::exists(out)); std::filesystem::create_directories(out);
        std::ofstream manifest(out/"manifest.tsv");
        manifest<<"frame\tparam\tbase\teffective\tdepth\ttransport_frame\tmodule_objects\tlens_objects\n";
        for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink})
            for(auto kind:{UnitKind::Filter,UnitKind::Reverb})one(out,manifest,theme,kind);
        std::puts("mod calque: 4 matched module/lens/paper states; 20 fresh native surfaces; 0 regression groups");
        std::puts("private host engine frames only; no audio export, hardware, deployment or application edits");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"mod calque capture: %s\n",e.what()); return 1;
    }
}
} // namespace mod_calque_capture
int main(int argc,char** argv) {
    if(argc!=2) {std::fprintf(stderr,"usage: mod-calque-capture empty-output-directory\n"); return 2;}
    return mod_calque_capture::run(argv[1]);
}
