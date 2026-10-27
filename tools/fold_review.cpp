// Review-only native capture of the EXISTING Fold, not a redesign or acceptance test.
// Build with SEQUENCER_TEST_FLAGS/SOURCES, replacing sequencer_test.cpp with this
// file and adding platform/3ds/ui_module_focus_fold.cpp. No tests are rerun here.
#define main seance_existing_harness_main
#include "sequencer_test.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    Fixture f;
    auto* fold = new FoldUnit;
    delete f.engine->replace_unit(0, 0, fold);
    g_edit_chain = g_edit_slot = 0;
    g_view = View::Chains; g_overlay = Overlay::Editor;
    std::ofstream manifest(directory / "manifest.tsv");
    manifest << "frame\tscreen\tobjects\ttext_overlaps\toutside_rects\toutside_lines\n";
    const struct State { const char* name; int drive, mode, bias; } states[] = {
        {"01_default",8192,0,0}, {"02_wrap",8192,16384,0},
        {"03_diode",8192,32767,0}, {"04_drive_max",32767,0,0},
        {"05_bias_max",8192,0,32767}, {"06_diode_label_start",8192,21845,0}
    };
    for (const auto& state : states) {
        fold->params[FoldUnit::P_DRIVE].base = state.drive;
        fold->params[FoldUnit::P_MODE].base = state.mode;
        fold->params[FoldUnit::P_BIAS].base = state.bias;
        for (int p = 0; p < fold->num_params; ++p) fold->params[p].snap();
        f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);
        fm_context_capture(-1); stranular_focus_capture(); param_lens_capture();
        for (bool top : {true, false}) {
            module_inspector_surface(top);
            if (top) top_draw(); else fold_focus_draw(*fold);
            const std::string screen = top ? "top" : "bottom";
            dump_surface(directory, std::string(state.name) + "_" + screen);
            manifest << state.name << '\t' << screen << '\t' << objects << '\t'
                     << text_overlaps << '\t' << outside_rects << '\t' << outside_lines << '\n';
            std::ofstream text(directory / (std::string(state.name)+"_"+screen+"_text.tsv"));
            text << "x\ty\tw\th\ttext\n";
            for (const auto& t : texts) text << t.x << '\t' << t.y << '\t' << t.w << '\t' << t.h << '\t' << t.text << '\n';
        }
    }
    std::ofstream probe(directory / "dsp_probe.tsv");
    probe << "mode_raw\tinput\tmean_q15\tminimum\tmaximum\n";
    // Exact integer-period synthetic sine through the real FoldUnit::process.
    // A code probe, not listening, hardware validation or an aliasing measurement.
    for (q15 mode : {q15(0),q15(16384),q15(32767)}) {
        fold->params[FoldUnit::P_DRIVE].effective = 0;
        fold->params[FoldUnit::P_BIAS].effective = 0;
        fold->params[FoldUnit::P_MODE].effective = mode;
        for (bool silence : {false,true}) {
            std::array<q15,1024> samples{};
            if (!silence) for (int i=0;i<1024;++i)
                samples[i]=std::lround(16000.0*std::sin(2.0*3.141592653589793*i/128));
            fold->process(samples.data(), samples.size());
            int64_t sum=0; int low=32767, high=-32768;
            for (q15 v:samples) {sum+=v;low=std::min(low,int(v));high=std::max(high,int(v));}
            probe << mode << '\t' << (silence?"silence":"sine_250hz") << '\t'
                  << double(sum)/samples.size() << '\t' << low << '\t' << high << '\n';
        }
    }
    std::printf("fold review: 6 existing dual screens + real DSP probes; %s\n",argv[1]);
}
