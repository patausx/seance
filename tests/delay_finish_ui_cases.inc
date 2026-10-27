// Extra Delay finish states on the same real composed native renderer.
namespace delay_finish_cases {
namespace h=module_headings_cases;
void run(const std::string& prefix) {
    for(bool modern:{true,false}) {
        h::Scene s(UnitKind::Delay);
        if(!modern) { auto* old=new DelayUnit(false);delete s.engine->replace_unit(0,s.slot,old);s.unit=old; }
        auto& u=*static_cast<DelayUnit*>(s.unit);
        u.reset();h::take(s,prefix+(modern?"delay_fresh":"delay_legacy_fresh"),"fresh");
        for(int param=0;param<u.num_params;++param)for(int edge:{0,16384,32767}) {
            s.set(param,edge);delay_focus_select(param);s.render(512);
            h::take(s,prefix+(modern?"delay_p":"delay_legacy_p")+std::to_string(param)+"_"+std::to_string(edge),"parameter-edge");
        }
    }
    {h::Scene s(UnitKind::Delay);auto& u=*static_cast<DelayUnit*>(s.unit);
     s.set(DelayUnit::P_MODE,32767);s.set(DelayUnit::P_TIME,32767);s.render(128);
     h::take(s,prefix+"delay_clean_handoff","clean-handoff");
     s.set(DelayUnit::P_HOLD,32767);s.render(128);h::take(s,prefix+"delay_hold_arrival","hold-arrival");
     u.bypass=true;u.request_clear();h::take(s,prefix+"delay_clear_pending","clear-pending");
     u.bypass=false;s.render(128);h::take(s,prefix+"delay_clear_fade","clear-fade");
     u.reset();h::take(s,prefix+"delay_reset","reset");}
}
int main(const std::filesystem::path& dir) {
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    h::output=dir;h::capture_files=true;h::manifest.open(dir/"surfaces.tsv");
    h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
    fader_lancet_cases::sample_fixture();CHECK(editor_style::init());
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        // Every module's default and the same old header fixture state is retained.
        for(const auto& item:fader_lancet_cases::catalogue)h::state(item.kind,prefix,"default");
        for(const char* state:{"bypass","long6","holes","live","lens","lock"})h::state(UnitKind::Delay,prefix,state);
        run(prefix);
    }
    editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();
    std::printf("delay finish: %d composed duals, peak %d/8192 host objects; synthetic/native, not hardware\n",h::duals,h::peak);
    return 0;
}
}
int main(int argc,char**argv) {try {CHECK(argc==3);return delay_finish_cases::main(argv[2]);}
    catch(const std::exception& error) {std::fprintf(stderr,"delay finish failed: %s\n",error.what());return 1;}}
