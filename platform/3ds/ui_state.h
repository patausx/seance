#pragma once
#include <3ds.h>
#include "../../core/engine/engine.h"
#include "../../core/engine/units.h"
#include "audio_out.h"

namespace seance::platform::ui3ds {
enum class View : u8 { Chains, Sequencer, Performance, Sigils, Bounce, Patchbay };
enum class Overlay : u8 { None, Picker, Editor, SampleBrowser, PatchBrowser, ConfirmDiscard };
// UI gesture tokens only; never persisted. Pages with one capture reuse a single token.
enum class Drag : u8 { None, ParamBase, PlaySpeed, PlayStart, PlayLength, PlayGain, PlayModDepth, PlaySliceSeam, GrainPosition, NoiseDensity, FmPitch, FmRatio, FmIndex, FmFeedback, FmDecay, FmGain, FmIndexFall, FmAttack, FmRatioFine, FmFeedbackFall, FmModDepth, MotorRpm, VcaGain, VcaAttack, VcaDecay, FilterCutoff, FilterResonance, FilterDrive, FilterMorph, FoldDrive, FoldTrim, FoldMode, FoldBias, FoldMix, ReverbSize, ChorusRate, ChorusDepth, ChorusMix, TiltBalance, TiltPivot, TiltDrive, TiltMix, TiltRange, TiltModDepth, RingFreq, RingDepth, RingFeedback, RingDiode, RingMix, RingFollow, GhostTone, GhostDecay, GhostSmear, GhostDrift, GhostFreeze, GhostMix, GhostFine, GhostRate, MicGain, RecLength, BitBits, BitRate, BitMix, BitJitter, BitDropout, BitDrift, BitComp, StrainParam, StrnPitch, PerformanceMorph, SigCanvas, SigRate, SigDepth, SigModDepth, OscShape, ShiftParam, BirdParam, FractParam, FractGrab, CompParam, MouthParam, ArpParam, GlitchParam, GlitchCell, InstParam, ScratchParam, PaulParam };
extern Engine* g_eng; extern platform::AudioOut* g_audio; extern bool g_has_dsp;
extern bool g_mic_ok; extern q15 g_mic_peak;
extern EngineUiSnapshot g_ui_snapshot;
extern View g_view; extern Overlay g_overlay; extern Drag g_drag;
extern int g_sel_chain,g_sel_slot,g_edit_chain,g_edit_slot,g_sel_sigil,g_drag_param,g_last_px,g_last_py;
extern bool g_smooth_active; extern int g_smooth_py,g_save_flash,g_loaded_samples,g_sample_flash,g_export_flash; extern bool g_export_ok,g_export_is_load;
extern int g_sample_page,g_patch_slot; constexpr int MAX_SAMPLE_FILES=128,SAMPLE_FILES_PER_PAGE=8,SAMPLE_NAME_LEN=64;
extern char g_sample_files[MAX_SAMPLE_FILES][SAMPLE_NAME_LEN]; extern int g_sample_file_count,g_sample_file_page,g_sample_browser_mode;
extern const char *SAMPLE_DIR,*RECORDING_DIR,*SCREENSHOT_DIR; extern bool g_screenshot_pending;
extern Unit* g_clip_unit; extern q15 g_sigil_undo[NUM_SIGILS][SIGIL_POINTS]; extern bool g_sigil_undo_valid[NUM_SIGILS]; extern q15 g_sigil_clip[SIGIL_POINTS]; extern bool g_sigil_clip_valid; extern bool g_sig_canvas_mutated;
extern u32 g_ui_rng;
extern u32 g_ui_frame;
extern bool g_seq_lock_armed; extern int g_seq_lock_chain,g_seq_lock_step,g_seq_lock_flash;
bool seq_record_lock(int slot,int param,q15 value);
q15 seq_display_value(int slot,int param,q15 fallback);
constexpr int CARD_W=50,CARD_H=40,CARD_X0=4,CARD_Y0=4,CARD_SX=52,CARD_SY=42;
constexpr int PICK_X=30,PICK_Y=40,PICK_W=260,PICK_H=160;
constexpr int TAB_X0=4,TAB_Y=8,TAB_W=24,TAB_H=24,TAB_SX=36;
constexpr int CANV_X=28,CANV_Y=43,CANV_W=263,CANV_H=102;
constexpr int SIG_DEPTH_X=4,SIG_DEPTH_Y=48,SIG_DEPTH_W=24,SIG_DEPTH_H=84;
constexpr int SIG_RATE_X=46,SIG_RATE_Y=147,SIG_RATE_W=198,SIG_RATE_H=27;
constexpr int SIG_MODE_X=258,SIG_MODE_Y=145,SIG_MODE_W=62,SIG_MODE_H=29;
constexpr int SIG_MOD_X=4,SIG_MOD_Y=178,SIG_MOD_W=150,SIG_MOD_H=38;
constexpr int SIG_DEST_X=166,SIG_DEST_Y=178,SIG_DEST_W=94,SIG_DEST_H=38;
constexpr int SIG_TRIG_X=267,SIG_TRIG_Y=178,SIG_TRIG_W=53,SIG_TRIG_H=49;
constexpr int SIG_UNDO_X=194,SIG_UNDO_Y=8,SIG_UNDO_W=58,SIG_UNDO_H=24;
constexpr int SIG_TOOLS_X=258,SIG_TOOLS_Y=8,SIG_TOOLS_W=62,SIG_TOOLS_H=24;
q15 norm_h(int,int,int,bool); ModSource sigil_source(int); u32 ui_rand(); void randomize_unit(Unit*,int); void quick_mod_unit(Unit*,int); bool chain_is_soloed(int);
void mark_project_mutated();
void mark_project_structural_mutated();
bool project_is_dirty();
}
