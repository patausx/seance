// Focused real-main/editor and native text submissions; no media export.
#include "../platform/3ds/small_button_motion.h"
namespace {
int run_small_button_visual() {
    try {
        namespace p=seance::platform::ui3ds::palette;
        for(auto theme:{p::Theme::Pink,p::Theme::Graphite}) {
            p::set(theme);small_press::cancel();OscCleanFixture f;
            const auto label=[](const char* name) {
                for(const auto& t:texts) if(t.text==name)return t;
                throw std::runtime_error("small button label missing");
            };
            const auto bases=f.bases();const int dirty=mutations;
            host_time_ms=1000;input::down(280,38);input::up();
            CHECK(g_drag==Drag::None&&f.bases()==bases&&mutations==dirty);
            editor_capture_button_motion(true);f.capture();f.lower();
            CHECK(label("fine /32").call_y==39); // action immediate, 1px decoration
            const auto pressed=pixels;host_time_ms=9000;f.lower();CHECK(pixels==pressed);
            host_time_ms=1080;editor_capture_button_motion(true);f.lower();
            CHECK(label("fine /32").call_y==38&&small_press::frame.weight>0);
            host_time_ms=1160;editor_capture_button_motion(true);f.lower();
            CHECK(label("fine /32").call_y==38&&!small_press::frame.weight);
            // Moving off, hiding, changing owner/context and clock reset cancel;
            // none can mutate a param or lend the pulse to another control.
            host_time_ms=1200;input::down(280,38);input::move(299,38);input::up();
            editor_capture_button_motion(true);CHECK(!small_press::frame.weight);
            input::down(280,38);input::up();editor_capture_button_motion(false);
            CHECK(!small_press::frame.weight);
            input::down(280,38);input::up();g_edit_slot=1;
            editor_capture_button_motion(true);CHECK(!small_press::frame.weight);g_edit_slot=0;
            input::down(280,38);input::up();host_time_ms=1199;
            editor_capture_button_motion(true);CHECK(!small_press::frame.weight);
            CHECK(f.bases()==bases&&mutations==dirty);
            // Disabled lock header: neither action nor fake pressed appearance.
            g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
            const bool quantized=f.osc->pitch_quantize;
            host_time_ms=1300;input::down(305,9);input::up();editor_capture_button_motion(true);
            f.capture();f.lower();CHECK(label("qnt").call_y==9&&f.osc->pitch_quantize==quantized);
            g_seq_lock_armed=false;g_view=View::Chains;
            // Shared boxed button uses the same pose; disabled state stays still.
            small_press::begin(30,210,1400,f.osc,0);small_press::capture(1400,f.osc,0,true);
            module_inspector_surface(false);paper_button(10,200,60,18,"hold");
            CHECK(label("hold").call_y==206);
            module_inspector_surface(false);paper_button(10,200,60,18,"hold",false,true);
            CHECK(label("hold").call_y==205);
            CHECK(!small_press::pose(80,200,60,18).weight);
            small_press::cancel();
        }
        std::puts("pass small buttons: actual main/editor touch, immediate fine action/release, frozen native 1px/60ms + tonal100ms, disabled/drag/context/re-touch, both palettes; no exports");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL small buttons: %s\n",e.what());return 1;}
}
}
