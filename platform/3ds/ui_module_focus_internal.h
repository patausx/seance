#pragma once
#include <3ds.h>
#include "../../core/engine/units.h"
#include "module_fader.h"

namespace seance::platform::ui3ds {

// UI capture only: call under the page's existing audio guard, never in draw.
// The tiny mask retains an enabled route at zero depth/equal edit/live values.
inline uint16_t focus_mod_links(const Unit& unit) {
    uint16_t links=0;
    for(int p=0;p<unit.num_params && p<16;++p)
        if(unit.params[p].src!=ModSource::None && unit.params[p].mod_enabled)
            links|=uint16_t(1u<<p);
    return links;
}

// Shared paper widgets for the focused module surface. These stay allocation-free
// and contain no engine mutation; module pages compose them with short guarded edits.
u32 paper();
u32 paper_hi();
u32 ink();
u32 spot();
u32 ghost();
u32 faint();
void paper_button(int x, int y, int w, int h, const char* label,
                  bool active = false, bool dim = false);
void paper_footer(const char* label);

// Internal Module Focus dispatch. Overlay::Editor remains the public facade;
// pages only own unit-specific controls and visuals, never patch/DSP state.
bool module_focus_touch_header(Unit& unit, int tx, int ty);
bool module_focus_touch_common_header(Unit& unit, int tx, int ty);
bool module_focus_touch_body(Unit& unit, int tx, int ty);
bool module_focus_draw_header(const Unit& unit);
void module_focus_draw_shell(const Unit& unit);
const char* module_focus_footer(const Unit& unit, int sample_slot);
void module_focus_draw_body(const Unit& unit);

enum class RecorderFocusAction : uint8_t {
    None = 0, Record, Stop, Clear, SaveWav, SendPlay, SendGrain,
    PreviousSlot, NextSlot, Setup, Back, Input, Monitor, Clock
};
RecorderFocusAction recorder_focus_action_at(int tx, int ty);
void recorder_focus_draw(const RecUnit& unit, const char* result_notice);
trackr::synth::SampleEditResult recorder_focus_send_recording_to(UnitKind destination);
void recorder_focus_command(RecUnit& unit, RecorderFocusAction action);
bool recorder_focus_touch_body(RecUnit& unit, int tx, int ty);
void recorder_focus_drag(RecUnit& unit, int tx, int ty);
void recorder_focus_capture();
int recorder_focus_selected();
void recorder_focus_select(int param);
void recorder_focus_release(); // cancellation only
void recorder_focus_touch_release(); // owned real lift
bool recorder_focus_back();
bool recorder_focus_draw_top();
bool recorder_focus_return_touch(int tx, int ty, bool down);
void recorder_focus_draw_return();
bool recorder_focus_confirming();
bool recorder_focus_input(u32& down, u32 held, bool blocked);
void recorder_focus_confirm_touch(bool down, bool held, int tx = 0, int ty = 0);
const char* recorder_focus_status(); // frozen take/progress summary, never live engine data
void module_focus_show_sample_result(trackr::synth::SampleEditResult result);
const char* module_focus_sample_result_notice();
void module_focus_tick();

void play_focus_capture(); // bounded waveform copy before GPU submission
int play_focus_selected();
void play_focus_select(int param);
void play_focus_release();
bool play_focus_draw_top();
void play_focus_value_text(char* out, size_t size, int param, q15 value);
bool play_focus_touch_header(PlayUnit& unit, int tx, int ty);
bool play_focus_back(PlayUnit& unit);
bool play_focus_touch_body(PlayUnit& unit, int tx, int ty);
void play_focus_touch_release(PlayUnit& unit);
void play_focus_drag(PlayUnit& unit, int tx, int ty);
void play_focus_draw_header(const PlayUnit& unit);
void play_focus_draw(const PlayUnit& unit);

bool module_focus_open_mod(); // release-owned latched ordinary modulation pane
void paul_focus_capture();
bool paul_focus_touch_body(PaulUnit& unit,int tx,int ty);
void paul_focus_drag(PaulUnit& unit,int tx,int ty);
void paul_focus_draw(const PaulUnit& unit);
bool paul_focus_draw_top();
int paul_focus_selected();
void paul_focus_select(int param);
void paul_focus_release();
void paul_focus_touch_release();
void paul_focus_input(u32 down,u32 held,bool blocked);
bool paul_focus_back();
void paul_focus_value_text(char* out,size_t size,int param,q15 value);

void scratch_focus_capture(); // one guarded source binding + actual Visual + paired screen Frame
bool scratch_focus_touch_body(ScratchUnit& unit, int tx, int ty);
void scratch_focus_drag(ScratchUnit& unit, int tx, int ty);
void scratch_focus_draw(const ScratchUnit& unit);
bool scratch_focus_draw_top();
int scratch_focus_selected();
void scratch_focus_select(int param);
void scratch_focus_release(); // cancellation; no rearm until physical lift
void scratch_focus_touch_release();
void scratch_focus_input(u32 down, u32 held, bool blocked);
bool scratch_focus_back();
void scratch_focus_value_text(char* out, size_t size, int param, q15 value);

void grain_focus_capture(); // bounded real PCM + own-envelope grain projection before GPU
int grain_focus_selected();
void grain_focus_select(int param);
void grain_focus_release(); // cancellation only; releases own pad token safely
void grain_focus_touch_release(); // physical lift may commit an owned config button
void grain_focus_input(u32 down, u32 held, bool blocked);
bool grain_focus_menu_modal();
bool grain_focus_menu_input(u32& down, u32 held, u32 up, bool blocked); // owns B only while open; global L/nav/transport retained
void grain_focus_menu_touch(u32 down, u32 held, int tx, int ty); // before facade/lens/rand, consumes modal contacts
bool grain_focus_draw_top(); // ordinary clean view; lens/locks retain shared inspector
bool grain_focus_touch_header(GrainUnit& unit, int tx, int ty);
bool grain_focus_touch_body(GrainUnit& unit, int tx, int ty);
void grain_focus_drag(GrainUnit& unit, int tx, int ty);
void grain_focus_draw(const GrainUnit& unit);

bool osc_focus_touch_body(OscUnit& unit, int tx, int ty);
void osc_focus_drag(OscUnit& unit, int tx, int ty);
void osc_focus_draw(const OscUnit& unit);
void osc_focus_capture(); // bounded audio Visual for clean; legacy reset model before GPU
bool osc_focus_draw_top(); // clean only, including the modulation lens
void osc_focus_value_text(char* out, size_t size, int param, q15 value); // captured mode/qnt
int osc_focus_selected();
void osc_focus_select(int param);
void osc_focus_release();

bool noise_focus_touch_body(NoiseUnit& unit, int tx, int ty);
void noise_focus_drag(NoiseUnit& unit, int tx, int ty);
void noise_focus_draw(const NoiseUnit& unit);
void noise_focus_capture(); // bounded published block peak, cached pre-GPU filter/amp models
bool noise_focus_draw_top(); // extended only, legacy retains shared inspector
bool noise_focus_modern();
void noise_focus_value_text(char* out, size_t size, int param, q15 value);
int noise_focus_selected();
void noise_focus_select(int param);
void noise_focus_release();

bool fm_focus_touch_body(FmUnit& unit, int tx, int ty);
void fm_focus_drag(FmUnit& unit, int tx, int ty);
void fm_focus_draw(const FmUnit& unit);
void fm_focus_capture(); // bounded lower controls; upper reference captured by fm_context
int fm_focus_selected();
int fm_focus_page(const FmUnit& unit); // capture/input only; normalize unavailable legacy selection
void fm_focus_select(int param);
void fm_focus_release();
void fm_focus_reset_reference(q15* mod, q15* out, q15* amp, q15* index,
                              q15* feedback, int frames, const q15* values,
                              bool drone, bool alias_safe);

bool motor_focus_touch_body(MotorUnit& unit, int tx, int ty);
void motor_focus_drag(MotorUnit& unit, int tx, int ty);
void motor_focus_draw(const MotorUnit& unit);
void motor_focus_capture(); // cached actual MotorUnit reset reference, outside the audio guard
bool motor_focus_draw_top(); // clean upper; MOD lens retains shared inspector
void motor_focus_value_text(char* out, size_t size, int param, q15 value, bool note);
bool motor_focus_modern();
bool motor_focus_note_mode();
int motor_focus_selected();
void motor_focus_select(int param);
void motor_focus_release();
// Legacy host oracle compatibility: uses the real old MotorUnit DSP, not a second model.
void motor_focus_reset_reference(q15* pre, q15* out, int frames, q15 rpm, q15 jitter,
                                 q15 resonance, q15 load, q15 gain, q15 source);

bool vca_focus_touch_body(VcaUnit& unit, int tx, int ty);
void vca_focus_drag(VcaUnit& unit, int tx, int ty);
void vca_focus_draw(const VcaUnit& unit);
int vca_focus_selected();
void vca_focus_select(int param);
void vca_focus_release();
void vca_focus_capture(); // shared bounded lower + upper frame, before GPU submission
bool vca_focus_draw_top();
bool vca_focus_flexible(); // captured model version for mod/temp formatting
void vca_focus_value_text(char* out, size_t size, int param, q15 value);
void vca_focus_input(u32 down, u32 held, bool blocked);
bool vca_focus_back();

bool delay_focus_touch_body(DelayUnit& unit, int tx, int ty);
void delay_focus_drag(DelayUnit& unit, int tx, int ty);
void delay_focus_draw(const DelayUnit& unit);
bool delay_focus_draw_top(); // same captured live/base/draft state as the lower panel
void delay_focus_capture(); // bounded shared pair DTO + pre-GPU iris, no ring audio
int delay_focus_selected();
void delay_focus_select(int param);
void delay_focus_release(); // cancellation only; never activates a pending tap
void delay_focus_touch_release(); // actual stylus lift commits the owned toggle
bool delay_focus_modern(); // captured version for shared mod/temp formatting

bool filter_focus_touch_header(FilterUnit& unit, int tx, int ty);
bool filter_focus_touch_body(FilterUnit& unit, int tx, int ty);
void filter_focus_drag(FilterUnit& unit, int tx, int ty);
void filter_focus_draw_header(const FilterUnit& unit);
void filter_focus_draw(const FilterUnit& unit);
void filter_focus_capture(); // bounded scalar/response copy; computation outside audio guard
bool filter_focus_draw_top(); // same frozen response DTO, including the shared MOD lens
int filter_focus_selected();
void filter_focus_select(int param);
void filter_focus_release();

bool fold_focus_touch_body(FoldUnit& unit, int tx, int ty);
void fold_focus_drag(FoldUnit& unit, int tx, int ty);
void fold_focus_draw(const FoldUnit& unit);
void fold_focus_capture(); // incremental private DSP reference outside guard / before GPU
bool fold_focus_draw_top(); // modern only; MOD/legacy retain shared inspector
bool fold_focus_modern();
void fold_focus_value_text(char* out, size_t size, int param, q15 value);
void fold_focus_input(u32 down, u32 held, bool blocked);
void fold_focus_touch_release(); // owned physical lift commits explicit legacy conversion
int fold_focus_selected();
void fold_focus_select(int param);
void fold_focus_release();

bool reverb_focus_touch_body(ReverbUnit& unit, int tx, int ty);
void reverb_focus_drag(ReverbUnit& unit, int tx, int ty);
void reverb_focus_draw(const ReverbUnit& unit);
void reverb_focus_capture();
bool reverb_focus_draw_top();
void reverb_focus_value_text(char* out, size_t size, int param, q15 value, bool modern = true);
int reverb_focus_selected();
void reverb_focus_select(int param); // UI-only selection for latched modulation navigation
void reverb_focus_release(); // cancellation, never commits a pending header action
void reverb_focus_touch_release(); // actual lift only
void reverb_focus_input(u32 down, u32 held, bool blocked);

bool chorus_focus_touch_body(ChorusUnit& unit, int tx, int ty);
void chorus_focus_drag(ChorusUnit& unit, int tx, int ty);
void chorus_focus_draw(const ChorusUnit& unit);
void chorus_focus_capture(); // bounded pre-GPU model/actual-delay copy for both LCDs
bool chorus_focus_draw_top();
void chorus_focus_release();
int chorus_focus_selected();
void chorus_focus_select(int param);

bool tilt_focus_touch_body(TiltUnit& unit, int tx, int ty);
void tilt_focus_drag(TiltUnit& unit, int tx, int ty);
void tilt_focus_draw(const TiltUnit& unit);
void tilt_focus_capture(); // bounded actual-coefficient response, computed outside guard/draw
bool tilt_focus_draw_top();
void tilt_focus_input(u32 down, u32 held, bool blocked);
void tilt_focus_value_text(char* out, size_t size, int param, q15 value, int law = 0); // 0 live, 1 draft, 2 stored
int tilt_focus_selected();
void tilt_focus_select(int param);
void tilt_focus_release();

bool bird_focus_touch_body(BirdUnit& unit, int tx, int ty);
void bird_focus_drag(BirdUnit& unit, int tx, int ty);
void bird_focus_draw(const BirdUnit& unit);
bool bird_focus_draw_top();
void bird_focus_capture(); // guarded scalar copy, then pure contour projection; no PCM or audio advancement
int bird_focus_selected();
void bird_focus_select(int param);
void bird_focus_release();

bool shift_focus_touch_body(ShiftUnit& unit, int tx, int ty);
void shift_focus_drag(ShiftUnit& unit, int tx, int ty);
void shift_focus_draw(const ShiftUnit& unit);
bool shift_focus_draw_top();
void shift_focus_capture(); // bounded routes + ideal reference-tone model, never audio/PCM
int shift_focus_selected();
void shift_focus_select(int param);
void shift_focus_release();

bool ring_focus_touch_body(RingUnit& unit, int tx, int ty);
void ring_focus_drag(RingUnit& unit, int tx, int ty);
void ring_focus_draw(const RingUnit& unit);
void ring_focus_capture(); // bounded incremental private reference, outside audio guard / before GPU
bool ring_focus_draw_top();
bool ring_focus_modern();
void ring_focus_value_text(char* out, size_t size, int param, q15 value);
void ring_focus_input(u32 down, u32 held, bool blocked); // cancel changed modifier/navigation owner
int ring_focus_selected();
void ring_focus_select(int param);
void ring_focus_release();

bool ghost_focus_touch_body(GhostUnit& unit, int tx, int ty);
void ghost_focus_drag(GhostUnit& unit, int tx, int ty);
void ghost_focus_draw(const GhostUnit& unit);
void ghost_focus_capture();
bool ghost_focus_draw_top();
void ghost_focus_value_text(char* out, size_t size, int param, q15 value, q15 fine = 0);
int ghost_focus_selected();
void ghost_focus_select(int param); // UI-only selection, never edits a parameter
void ghost_focus_release();
void ghost_focus_cancel();

bool mic_focus_touch_body(MicUnit& unit, int tx, int ty);
void mic_focus_drag(MicUnit& unit, int tx, int ty);
void mic_focus_draw(const MicUnit& unit);
void mic_focus_capture(); // bounded real input copy under guard; reduce before GPU, draw frozen DTO
int mic_focus_selected();
void mic_focus_select(int param);
void mic_focus_release(); // cancellation only
void mic_focus_touch_release(); // owned physical lift
void mic_focus_input(u32 down, bool blocked);
bool mic_focus_draw_top(); // ordinary relief; lens/locks retain detailed inspector

bool bit_focus_touch_body(BitUnit& unit, int tx, int ty);
void bit_focus_drag(BitUnit& unit, int tx, int ty);
void bit_focus_draw(const BitUnit& unit);
void bit_focus_capture(); // private actual reset reference outside guard / before GPU
bool bit_focus_draw_top(); // modern only; legacy and MOD use shared inspector
bool bit_focus_modern();
bool bit_focus_back(); // setup to main, never a parameter edit
void bit_focus_value_text(char* out, size_t size, int param, q15 value);
int bit_focus_selected();
void bit_focus_select(int param);
void bit_focus_release();

bool strain_focus_touch_body(StrainUnit& unit, int tx, int ty);
void strain_focus_drag(StrainUnit& unit, int tx, int ty);
void strain_focus_draw(const StrainUnit& unit);
void strain_focus_capture(); // cached private reset reference outside guard / before GPU
bool strain_focus_draw_top(); // modern only; MOD and legacy retain shared inspector
bool strain_focus_modern();
bool strain_focus_back();
void strain_focus_input(u32 down, u32 held, bool blocked); // held-r: one raw bias unit / pixel
void strain_focus_value_text(char* out, size_t size, int param, q15 value);
int strain_focus_selected();
void strain_focus_select(int param);
void strain_focus_release();

// INST owns both screens; only accepted voice scalars cross the audio guard.
bool inst_focus_touch_body(InstUnit& unit, int tx, int ty);
void inst_focus_drag(InstUnit& unit, int tx, int ty);
void inst_focus_draw(const InstUnit& unit);
void inst_focus_capture();
bool inst_focus_draw_top();
int inst_focus_selected();
void inst_focus_select(int param);
void inst_focus_release(); // cancel, never commit a pending button
void inst_focus_touch_release(); // real lift only
void inst_focus_input(u32 down, bool blocked); // cancel on button/nav ownership

// GLITCH owns both screens; real slice routing/trace are frozen before GPU draw.
bool glitch_focus_touch_body(GlitchUnit& unit, int tx, int ty);
void glitch_focus_drag(GlitchUnit& unit, int tx, int ty);
void glitch_focus_draw(const GlitchUnit& unit);
void glitch_focus_capture();
bool glitch_focus_draw_top();
int glitch_focus_selected();
void glitch_focus_select(int param);
void glitch_focus_release();
void glitch_focus_value_text(char* out, size_t size, int param, q15 value);

// ARP owns both screens; bounded note-event state is captured before GPU draw.
bool arp_focus_touch_body(ArpUnit& unit, int tx, int ty);
void arp_focus_drag(ArpUnit& unit, int tx, int ty);
void arp_focus_draw(const ArpUnit& unit);
void arp_focus_capture();
bool arp_focus_draw_top();
int arp_focus_selected();
void arp_focus_select(int param);
void arp_focus_release();
void arp_focus_value_text(char* out, size_t size, int param, q15 value);

// MOUTH owns both screens; graph is the actual filter model, never input PCM.
bool mouth_focus_touch_body(MouthUnit& unit, int tx, int ty);
void mouth_focus_drag(MouthUnit& unit, int tx, int ty);
void mouth_focus_draw(const MouthUnit& unit);
void mouth_focus_capture();
bool mouth_focus_draw_top();
int mouth_focus_selected();
void mouth_focus_select(int param);
void mouth_focus_release();
void mouth_focus_value_text(char* out, size_t size, int param, q15 value);

// COMP owns both screens; all drawing consumes a bounded pre-GPU snapshot.
bool comp_focus_touch_body(CompUnit& unit, int tx, int ty);
void comp_focus_drag(CompUnit& unit, int tx, int ty);
void comp_focus_draw(const CompUnit& unit);
void comp_focus_capture();
bool comp_focus_draw_top();
int comp_focus_selected();
void comp_focus_select(int param);
void comp_focus_release();
bool comp_focus_back();
void comp_focus_value_text(char* out, size_t size, int param, q15 value);

// FRACT owns its complete surface; the draw functions consume only a frozen DTO.
bool fract_focus_touch_body(FractUnit& unit, int tx, int ty);
void fract_focus_drag(FractUnit& unit, int tx, int ty);
void fract_focus_draw(const FractUnit& unit);
void fract_focus_capture();
bool fract_focus_draw_top();
int fract_focus_selected();
void fract_focus_select(int param);
void fract_focus_release(); // cancel the owned runtime window, independent HOLD survives
void fract_focus_touch_release(); // real lift: clear temporary squeeze, no saved-parameter edit
void fract_focus_value_text(char* out, size_t size, int param, q15 value);

bool stranular_focus_touch_body(StranularUnit& unit, int tx, int ty);
void stranular_focus_drag(StranularUnit& unit, int tx, int ty);
void stranular_focus_draw_body(const StranularUnit& unit);

} // namespace seance::platform::ui3ds
