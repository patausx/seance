// Own capture entrypoint: includes the established native software raster only.
// The renamed regression main is never called; no test/dump gallery is run.
#define main quiet_unused_sequencer_test_main
#define C2D_DrawRectSolid quiet_source_rect
#include "sequencer_test.cpp"
#undef C2D_DrawRectSolid
#undef main
#include "quiet_capture_matte.h"

bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color) {
    return quiet_source_rect(x, y, z, w, h, quiet_capture::shade(x, y, w, h, color));
}

int main(int argc, char** argv) {
    const std::filesystem::path out = argc > 1 ? argv[1] : "ui_mockups/quiet_paper_study/source";
    std::filesystem::create_directories(out);
    palette::set(palette::Theme::Graphite);
    Fixture f;
    // An illustrative stopped score, not a recorded project or audio telemetry.
    delete f.engine->replace_unit(2, 0, new FmUnit);
    for (int c = 0; c < NUM_CHAINS; ++c) {
        f.engine->clear_track(c);
        f.engine->set_sequence_length(c, 16);
    }
    f.refresh();
    const int onsets[4][8] = {
        {0, 3, 6, 8, 10, 12, 14, -1},
        {0, 4, 7, 8, 12, 15, -1, -1},
        {0, 2, 4, 6, 8, 10, 12, 14},
        {0, 4, 8, 12, -1, -1, -1, -1}
    };
    const int notes[8] = {24, 24, 31, 27, 34, 31, 27, 36};
    for (int c = 0; c < NUM_CHAINS; ++c) {
        for (int i = 0; i < 8 && onsets[c][i] >= 0; ++i) {
            const int s = onsets[c][i];
            auto& event = f.engine->pattern.steps[c][s];
            event.flags = SeqStep::Trig;
            f.engine->set_step_lock(c, s, g_ui_snapshot.sequence_pitch_slot[c],
                g_ui_snapshot.sequence_pitch_param[c], q15((notes[i] + (c == 2 ? 12 : 0)) * 32767 / 72));
            f.engine->set_step_lock(c, s, g_ui_snapshot.sequence_value_slot[c],
                g_ui_snapshot.sequence_value_param[c], q15(15000 + (i % 4) * 3500));
        }
    }
    f.refresh(); f.mode(1); f.touch(0, 6);
    host_time_ms = 1000;
    // Initialize focus at the destination, then hold the same clock for both
    // matte passes. No transport or animated foreground is advanced.
    for (bool upper : {true, false}) {
        const int w = upper ? 400 : 320;
        quiet_capture::begin(w, false);
        const auto normal = draw(upper);
        quiet_capture::begin(w, true);
        const auto matte = draw(upper);
        quiet_capture::save(out, upper ? "sequencer_top" : "sequencer_bottom", w, normal, matte);
    }
    std::puts("sequencer: one settled native pair; graphite / note layer / 4x16 illustrative stopped score; no suite, no audio render");
}
