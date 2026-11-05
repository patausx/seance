#include "module_inspector.h"
#include "osc_readout.h"
#include "play_readout.h"
#include "inst_readout.h"
#include "ui_module_focus_internal.h"
#include "ui_param_lens.h"
#include "ui_state.h"
#include "ui_sequencer.h"
#include "editor_style.h"
#include "module_heading.h"
#include "inspector_gothic.h"
#include "note_display.h"
#include "delay_echo_model.h"
#include "filter_response_model.h"
#include "tilt_response_model.h"
#include "ring_reference.h"
#include "../../core/engine/delay_time.h"
#include "../../core/ui/draw.h"
#include "../../core/engine/vca_envelope.h"
#include "../../core/engine/vca_timing.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <cstring>

namespace seance::platform::ui3ds {
namespace {
namespace m = editor_style;
struct Frame {
    ParamInspector detail;
    uint8_t kinds[UNITS_PER_CHAIN] = {};
    bool bypassed[UNITS_PER_CHAIN] = {};
    q15 edit = 0;
    bool lock = false, lock_full = false, lens = false, mute = false, gain_only = false;
    uint16_t delay_frames = 0;
    bool delay_hold = false, delay_ready = false, modern_delay = false;
    DelayUnit::VisualState delay = {};
    bool grain_sync = false, grain_clean = false, grain_hann = false;
    q15 filter_live = 0;
    q15 fold_mode = 0;
    q15 noise_colour = 0, noise_mode = 0, noise_decay = 0;
    bool osc_morph = true;
    bool modern_tilt = true, legacy_tilt_zero = false, legacy_tilt_edit_zero = false, legacy_tilt_base_zero = false;
    q15 tilt_values[6] = {};
    trackr::dsp::FilterType filter_type = trackr::dsp::FilterType::LPF;
};
static_assert(sizeof(Frame) <= 160, "bounded UI-only inspector frame");
static_assert(std::is_trivially_copyable<Frame>::value, "no engine pointers in inspector");
Frame frame;

int uni(q15 v) { return v < 0 ? 0 : v; }
void decimal(char* out, size_t size, uint64_t scaled, int digits, const char* suffix) {
    const unsigned divisor = digits == 3 ? 1000 : digits == 2 ? 100 : 10;
    std::snprintf(out, size, "%lu.%0*u%s", (unsigned long)(scaled / divisor),
                  digits, (unsigned)(scaled % divisor), suffix);
}
const char* title(UnitKind kind, int param) {
    static const char* osc[]={"pitch / note and nominal frequency", "width / pulse duty", "gain / linear output",
        "fold / pre-shape drive", "sub / octave-down blend", "drift / bounded pitch extent", "shape / oscillator morph"};
    if(kind==UnitKind::Osc)return osc[param];
    static const char* gran[]={"position / nominal source start", "size / nominal grain duration", "spray / +/- source radius",
        "speed / signed source rate", "gain / linear output", "density / free-clock launch rate", "jitter / interval scatter",
        "attack / grain opening", "release / grain closing", "p.dev / +/- pitch spread", "voices / head limit",
        "scan / source travel, not grain pitch"};
    if(kind==UnitKind::Grain)return gran[param];
    static const char* vca[] = {"gain / linear level", "attack / stage time", "decay / stage time",
        "hold / plateau time", "floor / minimum opening", "attack / curve bend", "decay / curve bend",
        "action / open or duck", "time / free or sync", "retrigger / while active", "run / trigger-armed cycle"};
    static const char* chorus[] = {"rate / sine lfo", "depth / delay excursion", "mix / wet pair level", "spread / relative lfo phase"};
    static const char* delay[] = {"time / free", "feedback / return gain", "mix / dry and wet",
                                  "wow / read modulation", "sat / record drive", "hold / filtered recirculation",
                                  "tone / feedback lowpass", "clock / time mode", "division / note length",
                                  "feel / note multiplier", "mode / time transition"};
    static const char* filter[] = {"cutoff / nominal frequency", "resonance / emphasis",
                                   "drive / soft clip input", "morph / pass to reject"};
    static const char* fold[] = {"drive / input gain", "trim / wet level", "morph / continuous shape",
                                 "bias / input offset", "mix / dry and shaped", "smooth / nonlinear rounding", "tone / post-shape lowpass"};
    static const char* bit_legacy[] = {"bits / quantizer depth", "rate / nominal hold clock", "mix / dry and crushed",
                                "jitter / clock scatter", "drop / chance per capture", "drift / signed clock sweep", "comp / static companding"};
    static const char* bit[] = {"bits / quantizer depth", "rate / fractional capture clock", "mix / dry and crushed",
        "jitter / interval scatter", "dropout / chance per eligible capture", "drift / signed clock sweep",
        "comp curve / static shaping", "drive / before quantizer", "tone / wet lowpass",
        "wet gain / before mix", "dither / tpdf extent", "drop length / fixed event time"};
    static const char* tilt[]={"tilt / body to air", "pivot / nominal frequency", "drive / before soft clip",
                               "mix / dry and tilted", "range / shelf limit", "trim / wet level after drive"};
    static const char* ring[]={"freq / base carrier", "depth / modulation core", "feedback / signed return",
                               "diode / multiplier to bridge", "mix / dry and processed", "follow / input level to carrier rate",
        "drive / input into wet core", "time / follower release", "wave / carrier shape", "pitch / manual carrier note",
        "tuning / free or note", "fine / carrier cents"};
    static const char* strain[]={"drive / pre-shaper gain", "bias / asymmetric shaping", "body / moving tone and emphasis",
                                 "teeth / fold and edge", "feedback / compensated return", "mix / dry and processed",
                                 "motion / internal movement amount", "wet trim / after return tap", "rate / internal motion cycle"};
    static const char* mic[]={"gain / digital input level", "input / source gate", "low-cut / input highpass"};
    static const char* rec[]={"rec / explicit command", "length / maximum take", "monitor / output pass-through"};
    static const char* noise_legacy[]={"colour / stochastic source", "density / temporal grain", "tone / lowpass amount",
        "gain / linear output", "body / low-cut amount", "decay / fall amount", "attack / rise time", "mode / hit or continuous"};
    static const char* noise[]={"colour / stochastic source", "density / grain or events", "tone / lowpass cutoff",
        "gain / linear output", "body / low-cut cutoff", "decay / finite fall", "attack / rise time", "mode / hit or continuous",
        "resonance / filter emphasis", "sweep / signed onset cutoff", "sweep time / return to tone", "event length / crackle only"};
    static const char* motor[]={"rate / nominal core frequency", "jitter / bounded speed wander", "res / resonator memory",
        "load / satellite oscillator spread", "gain / linear output", "source / core waveform",
        "width / pulse and mix only", "hard sync / slave oscillator ratio", "jitter speed / target update rate",
        "note / absolute root c1..c7", "tuning / free or note"};
    if(kind==UnitKind::Motor)return motor[param];
    if(kind==UnitKind::Noise)return noise_focus_modern()?noise[param]:noise_legacy[param];
    if(kind==UnitKind::Recorder)return rec[param];
    if(kind==UnitKind::MicIn)return mic[param];
    if(kind==UnitKind::Strain)return strain[param];
    if(kind==UnitKind::Ring)return ring[param];
    if(kind==UnitKind::Tilt)return tilt[param];
    if(kind==UnitKind::Bitcrush)return bit_focus_modern()?bit[param]:bit_legacy[param];
    return kind == UnitKind::Fold ? fold[param] : kind == UnitKind::Filter ? filter[param] : kind == UnitKind::Delay ? delay[param] : kind == UnitKind::Vca ? vca[param] : chorus[param];
}
void right(int x, int y, const char* text, uint32_t color) {
    ui::text(x - ui::text_w(text), y, text, color);
}
void format(q15 value, char* out, size_t size, int law=0) {
    if (UnitKind(frame.detail.kind)==UnitKind::Vca) {
        vca_focus_value_text(out,size,frame.detail.param,value);return;
    }
    if (UnitKind(frame.detail.kind)==UnitKind::Tilt) {
        tilt_focus_value_text(out,size,frame.detail.param,value,law);return;
    }
    if (UnitKind(frame.detail.kind)==UnitKind::Osc) {
        osc_focus_value_text(out,size,frame.detail.param,value);return;
    }
    module_inspector_value_text(out, size, UnitKind(frame.detail.kind), frame.detail.param, value,true,frame.modern_delay,
                               frame.modern_tilt,law==2?frame.legacy_tilt_base_zero:law==1?frame.legacy_tilt_edit_zero:frame.legacy_tilt_zero);
}
void hero(const char* value) {
    // Choose integer scale before drawing. Units get their own native-font line;
    // long values never fall back to the old chunky scaled 6x8 numeric face.
    int width = 0;
    for (const char* p = value; *p; ++p) {
        if (const auto* glyph = inspector_gothic::digit(*p)) width += glyph->width + 2;
        else if (*p == '.') width += 6;
        else if (*p == '+' || *p == '-') width += 18;
        else break;
    }
    const int scale = width * 2 <= 210 ? 2 : 1;
    const auto result = inspector_gothic::draw_numeric(14, 134, value, m::ink(), m::spot(), scale);
    if (!result.drawn) ui::text(14, 112, value, m::ink());
    else ui::text(14, 150, result.suffix, m::ghost());
}
const char* explanation() {
    const auto& d = frame.detail;
    if (frame.lock_full) return "lock bank full / existing locks still editable";
    if (frame.bypassed[d.slot]) return "bypassed / live is parameter, not audio";
    if (frame.mute) return "chain muted / live is parameter, not audio";
    if (d.temp) return "temporary / release l restores stored base";
    if (UnitKind(d.kind) == UnitKind::Osc) {
        if (!frame.osc_morph) return "legacy discrete wave / first shape touch converts without jump";
        if (d.param == OscUnit::P_SHAPE) return "adjacent raw waves / before fold, sub and gain / edges clamp";
        if (d.param == OscUnit::P_PW) return d.effective <= 21845 ? "stored and routable / audible only as pulse enters" : "pulse duty enters progressively across saw to pulse";
        if (d.param == OscUnit::P_DRIFT) return "deterministic bounded pitch wander / graph stays reset reference";
        return "normalized reset model / fixed scale / not live pcm";
    }
    if (UnitKind(d.kind) == UnitKind::Grain) {
        if(frame.grain_hann&&(d.param==GrainUnit::P_ATTACK||d.param==GrainUnit::P_RELEASE))
            return "full hann / attack and release stored, not applied";
        if(d.param==GrainUnit::P_SCAN)return frame.grain_clean?
            "0 holds position / +/-4x travel / trigger rewinds":"legacy: scan unavailable / old > enables v2";
        if(d.param==GrainUnit::P_DENS && frame.grain_sync)return "sync active / density stored, not applied";
        if(d.param==GrainUnit::P_SIZE)return "30-500ms nominal / source edges can shorten grains";
        if(d.param==GrainUnit::P_PITCH)return "signed rate / minimum 1/64x / latched per grain";
        if(d.param==GrainUnit::P_SPRAY)return "radius 0-50% of source / starts clamped at edges";
        if(d.param==GrainUnit::P_GAIN)return frame.grain_clean?
            "v2: fixed /8 headroom / gain before saturation":"legacy: gain after saturation / no headroom";
        if(d.param==GrainUnit::P_VOICES)return "1-8 main heads / short declick tails not drawn";
        return "next grains use new values / old grains keep envelope";
    }
    if (UnitKind(d.kind) == UnitKind::Motor) {
        if(d.param==MotorUnit::P_RPM)return "free rate / nominal hz, not detected pitch";
        if(d.param==MotorUnit::P_JITTER)return "bounded seeded speed wander";
        if(d.param==MotorUnit::P_RESONANCE)return "post-core resonator memory";
        if(d.param==MotorUnit::P_LOAD)return "two detuned saw satellites";
        if(d.param==MotorUnit::P_SOURCE)return "saw / pulse / mix; width unavailable on saw";
        if(d.param==MotorUnit::P_WIDTH)return "pulse duty / inactive on saw";
        if(d.param==MotorUnit::P_SYNC)return "hard sync ratio / not tempo sync";
        if(d.param==MotorUnit::P_JITTER_SPEED)return "seeded jitter target rate";
        if(d.param==MotorUnit::P_NOTE)return "c1..c7 root / arp adds accepted semitones";
        if(d.param==MotorUnit::P_MODE)return "free / note; arp only in note mode";
        return "post-resonator linear gain";
    }
    if (UnitKind(d.kind) == UnitKind::Noise) {
        if(noise_focus_modern()){
            if(d.param==NoiseUnit::P_DENSITY)return "white/pink/metal: grain rate / crackle: event rate";
            if(d.param==NoiseUnit::P_SWEEP)return "onset +/-24st / stored but inactive in drone";
            if(d.param==NoiseUnit::P_SWEEP_TIME)return "return to nominal tone / stored in drone";
            if(d.param==NoiseUnit::P_EVENT_LENGTH)return "individual crackle event / stored in other colours";
            if(d.param==NoiseUnit::P_DECAY)return "exact finite hit / stored in drone";
            if(d.param==NoiseUnit::P_BODY)return "low-cut off or 20..6000hz";
            if(d.param==NoiseUnit::P_TONE)return "digital lowpass 25..14000hz";
            return "native filter model / not measured audio";
        }
        if(d.param==NoiseUnit::P_DENSITY) {
            const int colour=uni(frame.noise_colour)*4/32768;
            return colour==1?"pink ignores density / stored value and route kept":
                   colour==2?"crackle: event probability per sample / not hz":"white/metal: temporal grain / not a pitch control";
        }
        if(d.param==NoiseUnit::P_ATTACK)return "0 instant / latched on trig / rise from current level";
        if(d.param==NoiseUnit::P_MODE)return NoiseUnit::mode_index(frame.noise_mode)==0?
            "legacy auto: decay selects hit/drone / sound retained":"hit: finite fall / drone: continuous / decay kept";
        if(d.param==NoiseUnit::P_DECAY)return NoiseUnit::drone_mode(frame.noise_mode,frame.noise_decay)?
            "drone: decay stored, not applied / attack on trigger":"fall control, not ms / after attack / a trig";
        if(d.param==NoiseUnit::P_BODY)return "0 = full low end / higher removes bass / not hz";
        if(d.param==NoiseUnit::P_TONE)return "higher opens one-pole lowpass / not a calibrated hz";
        return "env = current envelope / out = last block peak";
    }
    if (UnitKind(d.kind) == UnitKind::Recorder) {
        if(d.param==RecUnit::P_LEN)return "latched on rec / capped by prepared capacity";
        return "monitor off mutes output, not captured input";
    }
    if (UnitKind(d.kind) == UnitKind::MicIn) {
        if(d.param==MicUnit::P_GAIN)return "0db = unity / digital gain, not hardware preamp";
        if(d.param==MicUnit::P_MON)return "mute closes source / rec receives silence";
        return "off / 80 / 160hz / before gain / old patches off";
    }
    if (UnitKind(d.kind) == UnitKind::Tilt) {
        if (!frame.modern_tilt) return "legacy sound retained / trim off / no auto upgrade";
        if (d.param==TiltUnit::P_TRIM) return "after drive / before mix / dry level untouched";
        if (d.param==TiltUnit::P_RANGE) return "shelf range / eq target, not input spectrum";
        if (d.param==TiltUnit::P_TILT) return "zero changes tilt only / drive, trim and mix retained";
        return "small-signal transfer / no saturation or live audio";
    }
    if (UnitKind(d.kind) == UnitKind::Strain) {
        static const char* info[]={
            "gain into shaping / not output loudness",
            "offset scales with drive / wet dc is blocked",
            "moving poles + emphasis / not one cutoff in hz",
            "fold drive + blend + edge / 0 = soft saturation",
            "1ms loop / gain compensated by drive / not decay time",
            "0 = exact dry / internal loop still runs",
            "0 = fixed body / scales auto and reactive movement",
            "-18..+6db after loop tap / dry level untouched",
            "0.01..8hz / independent of tempo and input pitch"};
        return info[d.param];
    }
    if (UnitKind(d.kind) == UnitKind::Ring) {
        if(ring_focus_modern()){
            static const char* modern[]={"base rate / fine and signed follow are applied",
                "inner dry to core / depth 0 pauses carrier", "signed return / limited by depth",
                "multiply to diode bridge", "final dry to processed / mix 0 still advances",
                "signed level-to-rate / not pitch tracking", "0..18db into wet / dry unchanged",
                "1ms..1s release / kept when follow is zero", "sine / tri / pulse at fixed half duty",
                "manual c1..c7 / no arp or pitch detection", "free rate or chromatic manual note",
                "+/-100 cents / vertical primary gesture"};return modern[d.param];
        }
        static const char* info[]={
            "follow can raise rate / capped at 12khz",
            "0 = exact dry, paused / 100 = full core before mix",
            "loop gain limited to +/-0.85 x depth",
            "0 = multiply / 100 = diode bridge",
            "0 = exact dry / internal state still advances",
            "input level, not pitch tracking / up to 8x"};
        return info[d.param];
    }
    if (UnitKind(d.kind) == UnitKind::Bitcrush) {
        if(bit_focus_modern()){
            static const char* modern[]={"1-16 bits / 16 preserves neutral q15 input",
                "100hz-32khz / fractional deadlines / deliberately aliased",
                "0 exact dry / 100 exact wet / not output loudness",
                "0-50% capture interval scatter / bounded clock",
                "chance while idle / no retrigger / 100 wet mute",
                "-8..+8hz / fixed 12.5% depth / zero neutral",
                "static post-quantizer curve / not a compressor",
                "0..+24db input drive / before quantization",
                "100hz-14khz / maximum bypasses lowpass",
                "-24..+6db wet gain / 0db unity / before mix",
                "0 off / 100 one lsb peak tpdf / no dither at 16bit",
                "1..250ms per event / active events do not retrigger"};
            return modern[d.param];
        }
        static const char* info[]={
            "3-15 bits / masked low bits / no dither",
            "nominal 32k / hold; jitter + drift change actual clock",
            "0 exact dry / 100 near-wet q15 / not output loudness",
            "integer sample offsets / clock clamps to at least 1",
            "probability per held capture / 100 rounds near-total",
            "adds 0-7 samples / zero stops sweep, not its offset",
            "static nonlinear curve / no attack, release or makeup"};
        return info[d.param];
    }
    if (UnitKind(d.kind) == UnitKind::Fold) {
        if (d.param == FoldUnit::P_MODE) return "0 fold / 50 wrap / 100 diode / continuous morph";
        if (d.param == FoldUnit::P_DRIVE) return fold_focus_modern()?".25-8x pre-shape / signed raw means attenuation":"1-8x pre-shape / wave includes bias, trim and mix";
        if (d.param == FoldUnit::P_BIAS) return "offset in full-scale units / subtraction is not dc cut";
        if (d.param == FoldUnit::P_TRIM) return "wet gain in db / mute at zero / before dry-wet mix";
        if (d.param == FoldUnit::P_SMOOTH) return "rounds nonlinear corners / not a post filter";
        if (d.param == FoldUnit::P_TONE) return "100hz-16khz + off / wet lowpass, not an alias cure";
        return fold_focus_modern()?"0 exact dry / 100 wet / actual reset reference":"0 exact dry / 100 near-wet q15 / fixed reference sine";
    }
    if (UnitKind(d.kind) == UnitKind::Filter) {
        if (frame.filter_type == trackr::dsp::FilterType::Off) return "off / exact dry; drive + morph are inactive";
        if (d.param == FilterUnit::P_CUTOFF) {
            if (frame.edit > filter_response_model::F_MAX || frame.filter_live > filter_response_model::F_MAX)
                return "last 15% held at coefficient ceiling / sound unchanged";
            return "nominal svf hz, not -3db / drag left: lower frequency";
        }
        if (d.param == FilterUnit::P_RES) return "emphasizes cutoff / model excludes state saturation";
        if (d.param == FilterUnit::P_DRIVE) return "1-4x before soft clip / curve is post-drive, not output";
        return "pass to input-minus-pass / middle: half post-drive input";
    }
    if (UnitKind(d.kind) == UnitKind::Delay) {
        if (!frame.delay_ready) return "buffer unavailable / model is not audio";
        if (frame.modern_delay) {
            if (frame.delay.clearing) return "clear: wet fade / erase history / return";
            if (frame.delay.limited) return "sync interval limited to 4s / ! in model";
            if (frame.delay.hold_amount) return "hold ramps 10ms / filtered + saturated, not freeze";
            if (frame.delay.crossfading) return "clean crossfade / model shows destination only";
            if (d.param == DelayUnit::P_MODE) return "clean: 20ms crossfade / tape: moving read";
            if (d.param == DelayUnit::P_TIME && frame.delay.sync) return "free time retained / sync uses division + feel";
            if (d.param == DelayUnit::P_TONE) return "feedback lowpass / diagram excludes tone + sat";
            if (d.param == DelayUnit::P_SYNC || d.param == DelayUnit::P_DIV || d.param == DelayUnit::P_FEEL)
                return "sync follows applied bpm / ! means 4s limit";
        }
        if (frame.delay_hold) return "hold: input shut / filtered + saturated, not freeze";
        if (d.param == DelayUnit::P_HOLD) return "select hold / drag right on, left off";
        if (d.param == DelayUnit::P_SAT) return "record drive / soft clip remains at zero";
        if (d.param == DelayUnit::P_WOW) return "wow + flutter / applied time includes wobble";
        return "linear echo model / not captured audio";
    }
    if (UnitKind(d.kind) == UnitKind::Vca) {
        if (frame.gain_only) return "gain only / attack and decay are inactive";
        if (d.param == VcaUnit::P_GAIN) return "linear gain / before envelope, not output";
        if (d.param == VcaUnit::P_ATTACK && d.effective < 128)
            return "near zero: instant attack on trigger";
        return "nominal time / dsp uses integer envelope steps";
    }
    if (d.param == ChorusUnit::P_RATE) return "two mono copies / sine lfos offset 1/4 cycle";
    if (d.param == ChorusUnit::P_DEPTH) return "excursion above 3ms / same source, two copies";
    return "0 dry / 100 wet / normalized two-voice mix";
}
} // namespace

bool module_inspector_supports(UnitKind kind) {
    if (kind == UnitKind::Paul) return true;
    if (kind == UnitKind::Scratch) return true;
    if (kind == UnitKind::Inst) return true;
    if (kind == UnitKind::Glitch) return true;
    if (kind == UnitKind::Arp) return true;
    if (kind == UnitKind::Mouth) return true;
    if (kind == UnitKind::Comp) return true;
    if (kind == UnitKind::Fract) return true;
    if (kind == UnitKind::Bird) return true;
    if (kind == UnitKind::Shift) return true;
    return kind == UnitKind::Osc || kind == UnitKind::Grain || kind == UnitKind::Motor || kind == UnitKind::SamplePlayer || kind == UnitKind::Vca || kind == UnitKind::Chorus || kind == UnitKind::Delay || kind == UnitKind::Filter || kind == UnitKind::Fold || kind == UnitKind::Bitcrush || kind == UnitKind::Tilt || kind == UnitKind::Ring || kind == UnitKind::Strain || kind == UnitKind::MicIn || kind == UnitKind::Recorder || kind == UnitKind::Noise;
}
int module_inspector_selected(UnitKind kind) {
    if (kind == UnitKind::Paul) return paul_focus_selected();
    if (kind == UnitKind::Scratch) return scratch_focus_selected();
    if (kind == UnitKind::Inst) return inst_focus_selected();
    if (kind == UnitKind::Glitch) return glitch_focus_selected();
    if (kind == UnitKind::Arp) return arp_focus_selected();
    if (kind == UnitKind::Mouth) return mouth_focus_selected();
    if (kind == UnitKind::Comp) return comp_focus_selected();
    if (kind == UnitKind::Fract) return fract_focus_selected();
    if (kind == UnitKind::Bird) return bird_focus_selected();
    if (kind == UnitKind::Shift) return shift_focus_selected();
    if (kind == UnitKind::Osc) return osc_focus_selected();
    if (kind == UnitKind::Grain) return grain_focus_selected();
    if (kind == UnitKind::SamplePlayer) return play_focus_selected();
    if (kind == UnitKind::Vca) return vca_focus_selected();
    if (kind == UnitKind::Chorus) return chorus_focus_selected();
    if (kind == UnitKind::Delay) return delay_focus_selected();
    if (kind == UnitKind::Filter) return filter_focus_selected();
    if (kind == UnitKind::Fold) return fold_focus_selected();
    if (kind == UnitKind::Bitcrush) return bit_focus_selected();
    if (kind == UnitKind::Tilt) return tilt_focus_selected();
    if (kind == UnitKind::Ring) return ring_focus_selected();
    if (kind == UnitKind::Strain) return strain_focus_selected();
    if (kind == UnitKind::Motor) return motor_focus_selected();
    if (kind == UnitKind::Noise) return noise_focus_selected();
    if (kind == UnitKind::Recorder) return recorder_focus_selected();
    if (kind == UnitKind::MicIn) return mic_focus_selected();
    return -1;
}
void module_inspector_select(UnitKind kind, int param) {
    if (kind == UnitKind::Paul) { paul_focus_select(param); return; }
    if (kind == UnitKind::Scratch) { scratch_focus_select(param); return; }
    if (kind == UnitKind::Inst) { inst_focus_select(param); return; }
    if (kind == UnitKind::Glitch) { glitch_focus_select(param); return; }
    if (kind == UnitKind::Arp) { arp_focus_select(param); return; }
    if (kind == UnitKind::Mouth) { mouth_focus_select(param); return; }
    if (kind == UnitKind::Comp) { comp_focus_select(param); return; }
    if (kind == UnitKind::Fract) { fract_focus_select(param); return; }
    if (kind == UnitKind::Bird) { bird_focus_select(param); return; }
    if (kind == UnitKind::Shift) { shift_focus_select(param); return; }
    if (kind == UnitKind::Osc) { osc_focus_select(param); return; }
    if (kind == UnitKind::Grain) { grain_focus_select(param); return; }
    if (kind == UnitKind::SamplePlayer) { play_focus_select(param); return; }
    if (kind == UnitKind::Vca) { vca_focus_select(param); return; }
    if (kind == UnitKind::Delay) { delay_focus_select(param); return; }
    if (kind == UnitKind::Filter) { filter_focus_select(param); return; }
    if (kind == UnitKind::Fold) { fold_focus_select(param); return; }
    if (kind == UnitKind::Bitcrush) { bit_focus_select(param); return; }
    if (kind == UnitKind::Tilt) { tilt_focus_select(param); return; }
    if (kind == UnitKind::Ring) { ring_focus_select(param); return; }
    if (kind == UnitKind::Strain) { strain_focus_select(param); return; }
    if (kind == UnitKind::Motor) { motor_focus_select(param); return; }
    if (kind == UnitKind::Noise) { noise_focus_select(param); return; }
    if (kind == UnitKind::Recorder) { recorder_focus_select(param); return; }
    if (kind == UnitKind::MicIn) { mic_focus_select(param); return; }
    if (kind == UnitKind::Chorus && param >= 0 && param < ChorusUnit::P_COUNT) chorus_focus_select(param);
}
void module_inspector_value_text(char* out, size_t size, UnitKind kind, int param, q15 value, bool flexible_vca, bool modern_delay, bool modern_tilt, bool legacy_tilt_zero, bool modern_play, bool quantize_play) {
    if (!out || !size) return;
    if (kind == UnitKind::Paul) { paul_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Scratch) { scratch_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Inst) { inst_readout::value_text(out,size,param,value); return; }
    if (kind == UnitKind::Glitch) { glitch_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Arp) { arp_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Mouth) { mouth_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Comp) { comp_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Fract) { fract_focus_value_text(out,size,param,value); return; }
    if (kind == UnitKind::Bird) {
        if (param<0 || param>=BirdUnit::P_COUNT) {std::snprintf(out,size,"--");return;}
        if (param==BirdUnit::P_PITCH)
            std::snprintf(out,size,"%uhz",unsigned((BirdUnit::pitch_millihz(value)+500)/1000));
        else if (param==BirdUnit::P_SWEEP) {
            const int cents=BirdUnit::sweep_cents(value),v=std::abs(cents);
            std::snprintf(out,size,"%c%d.%02dst",cents<0?'-':'+',v/100,v%100);
        } else if (param==BirdUnit::P_TIME)
            std::snprintf(out,size,"%ums",unsigned((BirdUnit::duration_samples(value)+16)/32));
        else if (param==BirdUnit::P_TRILL) {
            const unsigned hz10=(BirdUnit::trill_millihz(value)+50)/100;
            std::snprintf(out,size,"%u.%uhz",hz10/10,hz10%10);
        } else if (param==BirdUnit::P_CONTOUR) {
            const int v=std::max(-32767,int(value));
            std::snprintf(out,size,"%+d%%",v*100/32767);
        } else std::snprintf(out,size,"%u%%",unsigned((uint32_t(uni(value))*100+16383)/32767));
        return;
    }
    if (kind == UnitKind::SamplePlayer) { play_readout::value_text(out,size,param,value,modern_play,quantize_play); return; }
    const int count = kind == UnitKind::Chorus ? ChorusUnit::P_COUNT : kind == UnitKind::Shift ? ShiftUnit::P_COUNT : kind == UnitKind::Osc ? OscUnit::P_COUNT : kind == UnitKind::Grain ? GrainUnit::P_COUNT : kind == UnitKind::Motor ? MotorUnit::P_COUNT : kind == UnitKind::Noise ? NoiseUnit::P_COUNT : kind == UnitKind::Ring ? (ring_focus_modern()?RingUnit::P_COUNT:RingUnit::LEGACY_COUNT) : kind == UnitKind::Strain ? (strain_focus_modern()?StrainUnit::P_COUNT:StrainUnit::LEGACY_COUNT) : kind == UnitKind::Tilt ? (modern_tilt?6:5) : kind == UnitKind::Bitcrush ? (bit_focus_modern()?BitUnit::P_COUNT:BitUnit::LEGACY_COUNT) : kind == UnitKind::Fold ? (fold_focus_modern()?FoldUnit::P_COUNT:FoldUnit::LEGACY_COUNT) : kind == UnitKind::Filter ? 4 : kind == UnitKind::Delay ? (modern_delay?DelayUnit::P_COUNT:6) : kind == UnitKind::Vca && flexible_vca ? VcaUnit::P_COUNT : 3;
    if (!module_inspector_supports(kind) || param < 0 || param >= count) {
        std::snprintf(out, size, "--"); return;
    }
    if (kind == UnitKind::Tilt) {tilt_response_model::value_text(out,size,param,value,modern_tilt,legacy_tilt_zero);return;}
    if (kind == UnitKind::Shift) {
        if (param == ShiftUnit::P_SHIFT) {
            const int mhz=ShiftUnit::shift_millihz(value);
            const unsigned magnitude=unsigned(mhz<0?-mhz:mhz);
            if(!mhz) std::snprintf(out,size,"0hz");
            else if(magnitude<1000) std::snprintf(out,size,"%c0.%03uhz",mhz<0?'-':'+',magnitude);
            else if(magnitude<100000) std::snprintf(out,size,"%c%u.%02uhz",mhz<0?'-':'+',magnitude/1000,(magnitude%1000)/10);
            else std::snprintf(out,size,"%c%uhz",mhz<0?'-':'+',(magnitude+500)/1000);
        } else if(param==ShiftUnit::P_FEEDBACK) {
            const unsigned permille=ShiftUnit::feedback_permille(value);
            std::snprintf(out,size,"%u.%u%%",permille/10,permille%10);
        } else std::snprintf(out,size,"%u%%",unsigned((uint32_t(uni(value))*100+16383)/32767));
        return;
    }
    const uint64_t v = uni(value);
    if (kind == UnitKind::Osc) {
        // Generic scalars use the new-card clean law. Actual OSC contexts use
        // osc_readout with their captured/persisted mode and quantize bit.
        if(osc_readout::value_text(out,size,param,value,true,false))return;
        if(param==OscUnit::P_FOLD)std::snprintf(out,size,"%.2fx",1.0+double(v)*6.0/32767.0);
        else if(param==OscUnit::P_SUB)std::snprintf(out,size,"%u%% / -1",unsigned((v*100+16383)/32767));
        else if(param==OscUnit::P_DRIFT)std::snprintf(out,size,"+/-%uct",unsigned((v*27+16383)/32767));
        else if(param==OscUnit::P_GAIN) {
            if(!v)std::snprintf(out,size,"mute");else std::snprintf(out,size,"%.1fdb",20.0*std::log10(double(v)/32768.0));
        } else {
            static const int anchors[]={0,10922,21845,32767};
            static const char* names[]={"sine","tri","saw","pulse"};
            for(int i=0;i<4;++i)if(int(v)==anchors[i]){std::snprintf(out,size,"%s",names[i]);return;}
            const int segment=v<10922?0:v<21845?1:2;
            const int percent=(int(v)-anchors[segment])*100/(anchors[segment+1]-anchors[segment]);
            std::snprintf(out,size,"%s > %s %d%%",names[segment],names[segment+1],percent);
        }
        return;
    }
    if (kind == UnitKind::Grain) {
        if(param==GrainUnit::P_SCAN) {
            if(!value)std::snprintf(out,size,"hold");
            else std::snprintf(out,size,"%+.4fx",double(GrainUnit::scan_increment_q16(value))/65536.0);
        } else if(param==GrainUnit::P_PITCH) {
            const int raw=value < -32767 ? -32767 : int(value);
            const int increment=std::max(1024,std::abs(raw)*8)*(raw<0?-1:1);
            std::snprintf(out,size,"%+.2fx",double(increment)/65536.0);
        } else if(param==GrainUnit::P_GAIN) {
            if(!v)std::snprintf(out,size,"mute");
            else {const int tenth=int(std::lround(200.0*std::log10(double(v)/32768.0)));std::snprintf(out,size,"%.1fdb",double(tenth)/10.0);}
        } else if(param==GrainUnit::P_SIZE)std::snprintf(out,size,"%ums",unsigned((GrainUnit::grain_length_frames(value)+16)/32));
        else if(param==GrainUnit::P_DENS)std::snprintf(out,size,"%.1f/s",double(ENGINE_SR)/GrainUnit::spawn_interval_frames(value));
        else if(param==GrainUnit::P_VOICES)std::snprintf(out,size,"%u",unsigned(GrainUnit::voice_limit(value)));
        else if(param==GrainUnit::P_PITCH_DEV)std::snprintf(out,size,"%.1fst",double(v)*12/32767);
        else if(param==GrainUnit::P_SPRAY)std::snprintf(out,size,"%.1f%%",double(v)*50/32768);
        else if(param==GrainUnit::P_ATTACK || param==GrainUnit::P_RELEASE)std::snprintf(out,size,"%.1f%%",double(v)*50/32768);
        else std::snprintf(out,size,"%.1f%%",double(v)*100/32768);
        return;
    }
    if (kind == UnitKind::Motor) {
        motor_focus_value_text(out,size,param,value,motor_focus_note_mode());return;
    }
    if (kind == UnitKind::Noise) {
        if(noise_focus_modern()){noise_focus_value_text(out,size,param,value);return;}
        if(param==NoiseUnit::P_COLOUR) {
            static const char* names[]={"white","pink","crackle","metal"};
            std::snprintf(out,size,"%s",names[v*4/32768]);
        } else if(param==NoiseUnit::P_MODE) {
            static const char* names[]={"auto","hit","drone"};
            std::snprintf(out,size,"%s",names[NoiseUnit::mode_index(value)]);
        } else if(param==NoiseUnit::P_ATTACK) {
            const uint32_t frames=NoiseUnit::attack_frames(value);
            const unsigned ms=unsigned((uint64_t(frames)*1000+ENGINE_SR/2)/ENGINE_SR);
            if(!frames)std::snprintf(out,size,"0ms");
            else if(ms<1000)std::snprintf(out,size,"%ums",ms);
            else decimal(out,size,(uint64_t(frames)*100+ENGINE_SR/2)/ENGINE_SR,2,"s");
        } else if(param==NoiseUnit::P_BODY && !v)std::snprintf(out,size,"full");
        else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
        return;
    }
    if (kind == UnitKind::Recorder) {
        if(param==RecUnit::P_LEN) {
            const uint64_t frames=1024+((v*(uint64_t(ENGINE_SR)*20-1024))>>15);
            if(frames*1000<ENGINE_SR*100u)std::snprintf(out,size,"%lums",(unsigned long)((frames*1000+ENGINE_SR/2)/ENGINE_SR));
            else decimal(out,size,(frames*100+ENGINE_SR/2)/ENGINE_SR,2,"s");
        } else std::snprintf(out,size,"%s",param==RecUnit::P_MON?(v>16383?"on":"off"):"command");
        return;
    }
    if (kind == UnitKind::MicIn) {
        if(param==MicUnit::P_GAIN) {
            if(!v)std::snprintf(out,size,"silent");
            else std::snprintf(out,size,"%+.1fdb",20.0*std::log10(double(v)/8192.0));
        } else if(param==MicUnit::P_MON)std::snprintf(out,size,"%s",value>16384?"open":"mute");
        else {static const char* cuts[]={"off","80hz","160hz"};std::snprintf(out,size,"%s",cuts[MicUnit::low_cut_mode(value)]);}
    } else if (kind == UnitKind::Strain) {
        strain_focus_value_text(out,size,param,value);
    } else if (kind == UnitKind::Ring) {
        if(ring_focus_modern()){ring_focus_value_text(out,size,param,value);return;}
        if(param==RingUnit::P_FREQ) {
            const double hz=ring_reference::hz(value);char number[16];
            const int n=std::snprintf(number,sizeof number,"%.2f",hz>=1000?hz/1000:hz);
            int end=n-1;
            while(end>0 && number[end]=='0')number[end--]='\0';
            if(end>0 && number[end]=='.')number[end]='\0';
            std::snprintf(out,size,"%s%s",number,hz>=1000?"khz":"hz");
        } else if(param==RingUnit::P_FEEDBACK) {
            const int raw=value < -32767 ? -32767 : int(value);
            const int amount=(std::abs(raw)*100+16383)/32767*(raw<0?-1:1);
            if(amount)std::snprintf(out,size,"%+d%%",amount);
            else std::snprintf(out,size,"0%%");
        } else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
    } else if (kind == UnitKind::Bitcrush) {
        if(bit_focus_modern()){bit_focus_value_text(out,size,param,value);return;}
        if(param==BitUnit::P_BITS)std::snprintf(out,size,"%ubits",unsigned(3+((v*13)>>15)));
        else if(param==BitUnit::P_RATE) {
            const unsigned hold=1+unsigned((32767-v)*31/32767);
            std::snprintf(out,size,"%uhz",unsigned((32000+hold/2)/hold));
        } else if(param==BitUnit::P_JITTER)
            std::snprintf(out,size,"-%u/+%usmp",unsigned(v*128/1048576),unsigned(v*127/1048576));
        else if(param==BitUnit::P_DRIFT) {
            const int raw=value < -32767 ? -32767 : int(value);
            std::snprintf(out,size,"%+.1fhz",double(raw)*32000.0/8388608.0);
        } else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
    } else if (kind == UnitKind::Fold) {
        fold_focus_value_text(out,size,param,value);
    } else if (kind == UnitKind::Filter) {
        if (param == FilterUnit::P_CUTOFF) {
            const float hz=filter_response_model::nominal_hz(value);
            if (hz<10.0f) std::snprintf(out,size,"%.1fhz",double(hz));
            else std::snprintf(out,size,"%uhz",unsigned(hz+0.5f));
        } else if (param == FilterUnit::P_DRIVE)
            std::snprintf(out,size,"%.1fx",1.0+double(v)*3.0/32767.0);
        else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
    } else if (kind == UnitKind::Delay) {
        if (param == DelayUnit::P_TIME)
            std::snprintf(out, size, "%ums", unsigned(((modern_delay?delay_time::free_frames(value):delay_echo_model::time_frames(value)) + 16) / 32));
        else if (param == DelayUnit::P_TONE)
            std::snprintf(out,size,"%uHz",unsigned(delay_time::tone_hz(value)));
        else if (param == DelayUnit::P_SYNC)
            std::snprintf(out,size,"%s",value>16384?"sync":"free");
        else if (param == DelayUnit::P_MODE)
            std::snprintf(out,size,"%s",value>16384?"clean":"tape");
        else if (param == DelayUnit::P_DIV) {
            static const char* notes[]={"1/64","1/32","1/16","1/8","1/4","1/2","1/1","2/1"};
            std::snprintf(out,size,"%s",notes[delay_time::division(value)]);
        } else if (param == DelayUnit::P_FEEL) {
            static const char* feels[]={"straight","dotted","triplet"};
            std::snprintf(out,size,"%s",feels[delay_time::feel(value)]);
        } else if (param == DelayUnit::P_HOLD)
            std::snprintf(out, size, "%s", value > 16384 ? "on" : "off");
        else std::snprintf(out, size, "%u%%", unsigned((v * 100 + 16383) / 32767));
    } else if (kind == UnitKind::Vca && flexible_vca && param >= VcaUnit::P_SYNC) {
        if(param==VcaUnit::P_RETRIGGER){static const char* names[]={"soft","reset","ignore"};
            std::snprintf(out,size,"%s",names[vca_timing::retrigger(value)]);}
        else std::snprintf(out,size,"%s",param==VcaUnit::P_SYNC?(vca_timing::enabled(value)?"sync":"free"):(vca_timing::enabled(value)?"cycle":"one shot"));
    } else if (kind == UnitKind::Vca && flexible_vca && param == VcaUnit::P_DUCK) {
        std::snprintf(out, size, "%s", value >= 16384 ? "duck" : "open");
    } else if (kind == UnitKind::Vca && flexible_vca && (param == VcaUnit::P_A_CURVE || param == VcaUnit::P_D_CURVE)) {
        const int raw = value < -32767 ? -32767 : value;
        std::snprintf(out, size, "%+d bend", raw * 100 / 32767);
    } else if (kind == UnitKind::Vca && flexible_vca &&
               (param == VcaUnit::P_ATTACK || param == VcaUnit::P_DECAY || param == VcaUnit::P_HOLD)) {
        const uint32_t samples = vca_envelope::time_frames(value, param == VcaUnit::P_HOLD);
        std::snprintf(out, size, "%lums", (unsigned long)((samples + 16) / 32));
    } else if (kind == UnitKind::Vca && !flexible_vca && param != VcaUnit::P_GAIN) {
        // Nominal quadratic time, not measured envelope length: integer ramp
        // increments/ceil decay in DSP can change the actual traversal time.
        const uint64_t samples = ((((v * v) >> 15) * 32000) >> 15) + 1;
        decimal(out, size, (samples * 100000 + ENGINE_SR / 2) / ENGINE_SR, 2, "ms");
    } else if (kind == UnitKind::Chorus && param == ChorusUnit::P_RATE) {
        const uint64_t increment = 5000 + v * 20;
        decimal(out, size, (increment * ENGINE_SR * 1000 + (1ULL << 31)) >> 32, 3, "Hz");
    } else if (kind == UnitKind::Chorus && param == ChorusUnit::P_SPREAD) {
        // Signed storage is centred on the historical zero-filled 90-degree slot.
        const int raw=std::max(-32767,int(value));
        std::snprintf(out,size,"%d deg",((raw+32767)*90+16383)/32767);
    } else if (kind == UnitKind::Chorus && param == ChorusUnit::P_DEPTH) {
        // The DSP uses >>15, not /32767: excursion spans 16..315 samples.
        const uint64_t samples = 16 + ((v * 300) >> 15);
        decimal(out, size, (samples * 100000 + ENGINE_SR / 2) / ENGINE_SR, 2, "ms");
    } else {
        // VCA gain is linear Q15, not the lower legacy -24..0 dB label.
        decimal(out, size, (v * 1000 + 16383) / 32767, 1, "%");
    }
}

void module_inspector_capture() {
    frame = {};
    paul_focus_capture();
    scratch_focus_capture();
    inst_focus_capture();
    glitch_focus_capture();
    arp_focus_capture();
    mouth_focus_capture();
    comp_focus_capture();
    fract_focus_capture();
    vca_focus_capture();
    filter_focus_capture();
    fold_focus_capture();
    bit_focus_capture();
    tilt_focus_capture();
    ring_focus_capture();
    bird_focus_capture();
    shift_focus_capture();
    strain_focus_capture();
    mic_focus_capture();
    recorder_focus_capture();
    noise_focus_capture();
    motor_focus_capture();
    grain_focus_capture();
    osc_focus_capture();
    if (!g_eng || !g_audio || g_overlay != Overlay::Editor ||
        g_edit_chain < 0 || g_edit_chain >= NUM_CHAINS ||
        g_edit_slot < 0 || g_edit_slot >= UNITS_PER_CHAIN) return;
    platform::AudioOut::Guard guard(*g_audio);
    const auto& chain = g_eng->chains[g_edit_chain];
    const Unit* unit = chain.units[g_edit_slot];
    if (!unit || !module_inspector_supports(unit->kind) || unit->kind == UnitKind::Paul || unit->kind == UnitKind::Scratch || unit->kind == UnitKind::Inst || unit->kind == UnitKind::Glitch || unit->kind == UnitKind::Arp || unit->kind == UnitKind::Mouth || unit->kind == UnitKind::Shift || unit->kind == UnitKind::Comp || unit->kind == UnitKind::Fract || unit->kind == UnitKind::Bird || unit->kind == UnitKind::Vca || unit->kind == UnitKind::SamplePlayer) return;
    if ((unit->kind == UnitKind::Osc || unit->kind == UnitKind::Grain || unit->kind == UnitKind::Motor || unit->kind == UnitKind::Filter || unit->kind == UnitKind::Fold || unit->kind == UnitKind::Bitcrush || unit->kind == UnitKind::Tilt || unit->kind == UnitKind::Ring || unit->kind == UnitKind::Strain || unit->kind == UnitKind::MicIn || unit->kind == UnitKind::Recorder || unit->kind == UnitKind::Noise) && g_view != View::Chains &&
        !(g_view == View::Sequencer && g_seq_lock_armed)) return;
    if(unit->kind==UnitKind::Delay || unit->kind==UnitKind::Chorus)return; // each has one dedicated shared dual-screen capture
    const int selected = module_inspector_selected(unit->kind);
    if (selected < 0 || selected >= unit->num_params) return;
    g_eng->capture_param_inspector(g_edit_chain, g_edit_slot, selected, frame.detail);
    if (!frame.detail.valid) return;
    frame.lens = false; // L is a lower overlay; keep the ordinary upper composition.
    frame.lock = g_seq_lock_armed && g_seq_lock_chain == g_edit_chain &&
        g_seq_lock_step >= 0 && g_seq_lock_step < SEQ_STEPS;
    frame.lock_full = frame.lock && g_eng->pattern.steps[g_edit_chain][g_seq_lock_step].lock_count >= SEQ_LOCKS;
    frame.edit = frame.lock ? seq_display_value(g_edit_slot, selected, frame.detail.base) : frame.detail.base;
    if((unit->kind==UnitKind::Motor||unit->kind==UnitKind::Noise||unit->kind==UnitKind::Bitcrush||unit->kind==UnitKind::Strain||unit->kind==UnitKind::Ring||unit->kind==UnitKind::Fold) && frame.lock){
        const int b=sequencer_edit_bank(),bank=b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;
        const auto& step=g_eng->sequence_pattern(bank).steps[g_edit_chain][g_seq_lock_step];
        frame.lock_full=step.lock_count>=SEQ_LOCKS;
        frame.edit=frame.detail.base;
        for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
            if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==selected){frame.edit=step.locks[i].value;break;}
    }
    frame.mute = chain.mute;
    if(unit->kind==UnitKind::Osc) {
        const auto& osc=static_cast<const OscUnit&>(*unit);
        frame.osc_morph=osc.shape_morph;
        // A dormant legacy SHAPE value is not the active discrete waveform.
        if(!osc.shape_morph && selected==OscUnit::P_SHAPE)
            frame.edit=frame.detail.effective=OscUnit::shape_anchor(osc.wave);
    }
    if(unit->kind==UnitKind::Grain){
        const auto& grain=*static_cast<const GrainUnit*>(unit);
        frame.grain_sync=grain.clock_sync;frame.grain_clean=grain.clean;
        frame.grain_hann=grain.clean&&grain.window==GrainUnit::Window::Hann;
    }
    if(unit->kind==UnitKind::Noise) {
        const auto& noise=*static_cast<const NoiseUnit*>(unit);
        const auto noise_value=[&](int p){
            if(frame.lock){const int b=sequencer_edit_bank(),bank=b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;
                const auto& step=g_eng->sequence_pattern(bank).steps[g_edit_chain][g_seq_lock_step];
                for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
                    if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)return step.locks[i].value;}
            return frame.lock?unit->params[p].base:noise.modern()?noise.control_value(p):unit->params[p].effective;
        };
        frame.noise_colour=noise_value(NoiseUnit::P_COLOUR);
        frame.noise_mode=noise_value(NoiseUnit::P_MODE);
        frame.noise_decay=noise_value(NoiseUnit::P_DECAY);
    }
    if(unit->kind==UnitKind::Tilt) {
        const auto& tilt=*static_cast<const TiltUnit*>(unit);
        frame.modern_tilt=tilt.modern();
        if(tilt.extended())frame.detail.effective=tilt.control_value(selected);
        for(int p=0;p<6;++p){
            frame.tilt_values[p]=p<unit->num_params?tilt.control_value(p):tilt_eq::TRIM_UNITY;
            if(frame.lock&&p<unit->num_params){
                frame.tilt_values[p]=unit->params[p].base;
                const int b=sequencer_edit_bank(),bank=b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;
                const auto& step=g_eng->sequence_pattern(bank).steps[g_edit_chain][g_seq_lock_step];
                for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
                    if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==p)frame.tilt_values[p]=step.locks[i].value;
            }
        }
        frame.legacy_tilt_zero=!frame.modern_tilt && unit->params[TiltUnit::P_RANGE].effective==0;
        frame.legacy_tilt_base_zero=!frame.modern_tilt && unit->params[TiltUnit::P_RANGE].base==0;
        frame.legacy_tilt_edit_zero=!frame.modern_tilt && (frame.lock?frame.tilt_values[TiltUnit::P_RANGE]:unit->params[TiltUnit::P_RANGE].base)==0;
    }
    if (unit->kind == UnitKind::Fold) {
        const auto& fold=*static_cast<const FoldUnit*>(unit);
        frame.detail.effective=fold.control_value(selected);
        frame.fold_mode=frame.lock?unit->params[FoldUnit::P_MODE].base:fold.control_value(FoldUnit::P_MODE);
        if(frame.lock){const int b=sequencer_edit_bank(),bank=b>=0&&b<NUM_PATTERNS?b:g_eng->active_pattern;
            const auto& step=g_eng->sequence_pattern(bank).steps[g_edit_chain][g_seq_lock_step];
            for(int i=0;i<std::min(int(step.lock_count),SEQ_LOCKS);++i)
                if(step.locks[i].slot==g_edit_slot&&step.locks[i].param==FoldUnit::P_MODE){frame.fold_mode=step.locks[i].value;break;}}
    }
    if (unit->kind == UnitKind::Filter) {
        const auto& filter=*static_cast<const FilterUnit*>(unit);
        frame.filter_type=filter.type;
        frame.filter_live=selected==FilterUnit::P_CUTOFF?q15(filter.visual_cutoff()):
                          selected==FilterUnit::P_RES?q15(filter.visual_resonance()):frame.detail.effective;
    }
    if (unit->kind == UnitKind::Vca)
        frame.gain_only = unit->params[VcaUnit::P_ATTACK].effective < 128 &&
                          unit->params[VcaUnit::P_DECAY].effective < 128;
    for (int i = 0; i < UNITS_PER_CHAIN; ++i) {
        const Unit* at = chain.units[i];
        frame.kinds[i] = uint8_t(at ? at->kind : UnitKind::Empty);
        frame.bypassed[i] = at && at->bypass;
    }
}

namespace {
// OSC-only presentation of the shared frozen inspector. Other modules retain
// their exact layout; no controller, parameter law or live-engine access here.
// Words and existing note/numeric glyphs use the same Gossip-derived face.
// This only replaces OSC's non-numeric 2x native fallback; no other hero changes.
void osc_word_hero(const char* value) {
    int x=14;const char* at=value;
    for(int word=0;word<2;++word){
        char label[6]={};int n=0;
        while(*at>='a'&&*at<='z'&&n<5)label[n++]=*at++;
        m::heading(x,84,label,m::ink());x+=m::heading_width(label)+12;
        if(!*at)return;
        if(word==0){
            ui::line(x,100,x+10,106,m::spot(),2);ui::line(x+10,106,x,112,m::spot(),2);
            x+=22;at+=3; // formatter's exact " > " separator
        } else {
            const auto result=inspector_gothic::draw_numeric(x,116,at+1,m::ink(),m::spot());
            ui::text(result.end_x+5,108,result.suffix,m::ghost());
        }
    }
}
void osc_inspector() {
    const auto& d=frame.detail;
    static const char* names[]={"pitch","width","gain","fold","sub","drift","shape"};
    static const char* layers[]={"base","step lock","audition","performance","temp"};
    const bool shape=d.param==OscUnit::P_SHAPE;
    const bool legacy=shape&&!frame.osc_morph;
    const q15 shown=legacy?frame.edit:d.temp?d.control:frame.edit;
    const char* label=legacy?"legacy":frame.lock?"edit lock":d.temp?"temporary":"base";
    char value[32],live[32],text[80];
    format(shown,value,sizeof value);
    format(d.effective,live,sizeof live);
    ui::text(14,module_heading::kDetailY,names[d.param],m::spot());right(386,module_heading::kDetailY,label,m::ghost());
    const bool numeric=(value[0]>='0'&&value[0]<='9') ||
        ((value[0]=='-'||value[0]=='+')&&value[1]>='0'&&value[1]<='9');
    if(numeric) {
        int width=0;
        for(const char* at=value;*at;++at) {
            if(const auto* glyph=inspector_gothic::digit(*at))width+=glyph->width+2;
            else if(*at=='.')width+=6;
            else if(*at=='-'||*at=='+')width+=18;
            else break;
        }
        const auto result=inspector_gothic::draw_numeric(14,124,value,m::ink(),m::spot(),width*2<=210?2:1);
        ui::text(result.end_x+8,115,result.suffix,m::ghost());
    } else if(shape || std::strcmp(value,"mute")==0)osc_word_hero(value);
    else if(d.param==OscUnit::P_PITCH){
        char note[4]={};int n=0;while(value[n]&&value[n]!=' '&&n<3){note[n]=value[n];++n;}
        note_display::melodic(note,m::ink(),m::spot(),4,20);
        ui::text(108,108,value+n,m::ghost());
    } else if(d.param==OscUnit::P_DRIFT){
        ui::text(14,108,"+/-",m::spot());
        const auto result=inspector_gothic::draw_numeric(44,116,value+3,m::ink(),m::spot());
        ui::text(result.end_x+5,108,result.suffix,m::ghost());
    }

    // Do not repeat a rounded value. A live difference/layer is explicit only
    // when relevant; the rail below distinguishes edit from applied position.
    if(std::strcmp(value,live)!=0) {
        std::snprintf(text,sizeof text,"live  %s%s%s",live,d.layer?" / ":"",d.layer?layers[d.layer<5?d.layer:0]:"");
        ui::text(14,140,text,m::spot());
    } else if(d.layer && !d.temp) {
        std::snprintf(text,sizeof text,"via %s",layers[d.layer<5?d.layer:0]);ui::text(14,140,text,m::ghost());
    }
    constexpr int x=14,w=372,y=165;
    const auto px=[](q15 raw){return 14+uni(raw)*372/32767;};
    const int edit=px(shown),effective=px(d.effective);
    module_fader::horizontal(x,x+w,y,edit,effective,
        {!legacy && d.source!=ModSource::None && d.mod_enabled,false,frame.bypassed[d.slot]||frame.mute});
    if(shape) {
        ui::text(14,179,"sine",m::ghost());ui::text(129,179,"tri",m::ghost());
        ui::text(253,179,"saw",m::ghost());right(386,179,"pulse",m::ghost());
    } else {
        format(0,text,sizeof text);ui::text(14,179,text,m::ghost());
        format(32767,text,sizeof text);right(386,179,text,m::ghost());
    }
    if(d.temp) {
        format(d.base,value,sizeof value);std::snprintf(text,sizeof text,"return  %s",value);
        ui::text(14,194,text,m::ghost());
    } else if(effective!=edit) ui::text(14,194,"hollow edit / solid live",m::ghost());
    ui::line(14,205,386,205,m::branch());
    if(legacy)std::snprintf(text,sizeof text,"legacy wave / morph off%s",d.source!=ModSource::None && !d.mod_enabled?" / link muted":"");
    else if(d.source==ModSource::None)std::snprintf(text,sizeof text,"mod off");
    else {
        char source[16];
        if(d.source==ModSource::EnvFollow)std::snprintf(source,sizeof source,"env ch%d",d.env_chain+1);
        else std::snprintf(source,sizeof source,"sg.%c",'a'+mod_source_sigil(d.source));
        std::snprintf(text,sizeof text,"%s > %s  %+d%%%s",source,names[d.param],int(d.depth)*100/32767,
            !d.mod_enabled?" / muted":d.clip_low||d.clip_high?" / range clamps":"");
    }
    ui::text(14,214,text,d.source==ModSource::None || !d.mod_enabled?m::ghost():m::spot());
    const char* status=frame.lock_full?"lock bank full / existing locks editable":
        frame.bypassed[d.slot]?"bypassed":frame.mute?"chain muted":d.temp?"release l / restore stored base":
        frame.lens?"b / return to osc":legacy?"touch shape to enable morph":
        d.param==OscUnit::P_PW?"width acts on the pulse component":
        d.param==OscUnit::P_DRIFT?"bounded pitch wander / model stays fixed":"wave below: reset model, not live audio";
    ui::text(m::kLeft,m::kTextY,status,m::ghost());
}

// MOTOR-only presentation; shared capture, formatting and all other modules stay intact.
void motor_inspector() {
    const auto& d=frame.detail;
    static const char* names[]={"rate","jitter","res","load","gain","source", "width", "hard sync", "jitter speed", "note", "mode"};
    static const char* layers[]={"base","step lock","audition","performance","temp"};
    const bool source=d.param==MotorUnit::P_SOURCE;
    const bool word=source||d.param==MotorUnit::P_NOTE||d.param==MotorUnit::P_MODE;
    const q15 shown=d.temp?d.control:frame.edit;
    char value[24],live[24],text[80];format(shown,value,sizeof value);format(d.effective,live,sizeof live);
    ui::text(14,module_heading::kDetailY,names[d.param],m::spot());right(386,module_heading::kDetailY,frame.lock?"edit lock":d.temp?"temporary":"base",m::ghost());
    if(word)m::heading(14,84,value,m::ink());
    else {
        int width=0;
        for(const char* at=value;*at;++at){const auto* glyph=inspector_gothic::digit(*at);if(!glyph)break;width+=glyph->width+2;}
        const auto result=inspector_gothic::draw_numeric(14,124,value,m::ink(),m::spot(),width*2<=210?2:1);
        ui::text(result.end_x+8,115,result.suffix,m::ghost());
    }
    if(std::strcmp(value,live)!=0){
        std::snprintf(text,sizeof text,"live  %s%s%s",live,d.layer?" / ":"",d.layer?layers[d.layer<5?d.layer:0]:"");
        ui::text(14,140,text,m::spot());
    } else if(d.layer&&!d.temp){
        std::snprintf(text,sizeof text,"via %s",layers[d.layer<5?d.layer:0]);ui::text(14,140,text,m::ghost());
    }
    const auto px=[source](q15 raw){return source?14+std::min(2,uni(raw)*3/32768)*186:14+uni(raw)*372/32767;};
    const int edit=px(shown),effective=px(d.effective);
    module_fader::horizontal(14,386,165,edit,effective,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypassed[d.slot]||frame.mute});
    if(source){ui::text(14,179,"saw",m::ghost());ui::text(185,179,"pulse",m::ghost());right(386,179,"mix",m::ghost());}
    else {format(0,text,sizeof text);ui::text(14,179,text,m::ghost());format(32767,text,sizeof text);right(386,179,text,m::ghost());}
    if(d.temp){format(d.base,value,sizeof value);std::snprintf(text,sizeof text,"return  %s",value);ui::text(14,194,text,m::ghost());}
    else if(edit!=effective)ui::text(14,194,"hollow edit / solid live",m::ghost());
    ui::line(14,205,386,205,m::branch());
    if(d.source==ModSource::None)std::snprintf(text,sizeof text,"mod off");
    else {
        char route[16];
        if(d.source==ModSource::EnvFollow)std::snprintf(route,sizeof route,"env ch%d",d.env_chain+1);
        else std::snprintf(route,sizeof route,"sg.%c",'a'+mod_source_sigil(d.source));
        std::snprintf(text,sizeof text,"%s > %s  %+d%%%s",route,names[d.param],int(d.depth)*100/32767,
                      !d.mod_enabled?" / muted":d.clip_low||d.clip_high?" / range clamps":"");
    }
    ui::text(14,214,text,d.source==ModSource::None || !d.mod_enabled?m::ghost():m::spot());
    static const char* notes[]={"free rate / native reset model", "speed wander / native reset model",
        "resonator / native reset model", "two detuned saws / native reset model",
        "post-resonator gain / native reset model", "core source / native reset model",
        "pulse/mix duty / saw inactive", "slave/master ratio / not tempo", "seeded target update rate",
        "absolute root note / arp passive", "free or note / arp passive"};
    const char* status=frame.lock_full?"lock bank full / existing locks editable":
        frame.bypassed[d.slot]?"bypassed / model is not output audio":frame.mute?"chain muted / model is not output audio":
        d.temp?"release l / restore stored base":frame.lens?"b / return to motor":notes[d.param];
    ui::text(m::kLeft,m::kTextY,status,m::ghost());
}
} // namespace

bool module_inspector_draw_top() {
    if (paul_focus_draw_top()) return true;
    if (scratch_focus_draw_top()) return true;
    if (fold_focus_draw_top()) return true;
    if (strain_focus_draw_top()) return true;
    if (ring_focus_draw_top()) return true;
    if (bit_focus_draw_top()) return true;
    if (noise_focus_draw_top()) return true;
    if (motor_focus_draw_top()) return true;
    if (mic_focus_draw_top()) return true;
    if (recorder_focus_draw_top()) return true;
    if (grain_focus_draw_top()) return true;
    if (osc_focus_draw_top()) return true;
    if (inst_focus_draw_top()) return true;
    if (glitch_focus_draw_top()) return true;
    if (arp_focus_draw_top()) return true;
    if (mouth_focus_draw_top()) return true;
    if (comp_focus_draw_top()) return true;
    if (fract_focus_draw_top()) return true;
    if (bird_focus_draw_top()) return true;
    if (shift_focus_draw_top()) return true;
    if (play_focus_draw_top()) return true;
    if (vca_focus_draw_top()) return true;
    if (filter_focus_draw_top()) return true;
    if (delay_focus_draw_top()) return true;
    if (chorus_focus_draw_top()) return true;
    if (tilt_focus_draw_top()) return true;
    const auto& d = frame.detail;
    if (!d.valid) return false;
    const bool osc = UnitKind(d.kind) == UnitKind::Osc;
    const bool delay = UnitKind(d.kind) == UnitKind::Delay;
    const bool filter = UnitKind(d.kind) == UnitKind::Filter;
    const bool fold = UnitKind(d.kind) == UnitKind::Fold;
    const bool tilt = UnitKind(d.kind) == UnitKind::Tilt;
    const bool ring = UnitKind(d.kind) == UnitKind::Ring;
    const bool strain = UnitKind(d.kind) == UnitKind::Strain;
    const bool mic = UnitKind(d.kind) == UnitKind::MicIn;
    const bool rec = UnitKind(d.kind) == UnitKind::Recorder;
    const bool noise = UnitKind(d.kind) == UnitKind::Noise;
    const bool gran = UnitKind(d.kind) == UnitKind::Grain;
    const q15 live_value = filter ? frame.filter_live : d.effective;
    m::paper(400);
    // Fallback inspector uses the same private module identity as dedicated pages.
    // Keep the actual six-slot route below it, including selected and bypass marks.
    module_heading::header(UnitKind(d.kind),m::spot());
    const int path_y = module_heading::kRouteY;
    int x = 14;
    for (int i = 0; i < UNITS_PER_CHAIN; ++i) {
        if (UnitKind(frame.kinds[i]) == UnitKind::Empty) continue;
        if (x > 14) { ui::text(x - 9, path_y, "/", m::ghost()); }
        const char* name = m::module_name(UnitKind(frame.kinds[i]));
        const int w = ui::text_w(name);
        ui::text(x, path_y, name, i == d.slot ? m::spot() : m::ghost());
        if (i == d.slot) ui::line(x, module_heading::kRouteUnderlineY, x + w - 2, module_heading::kRouteUnderlineY, m::spot());
        if (frame.bypassed[i]) ui::line(x, path_y + 4, x + w - 2, path_y + 4, m::ghost());
        x += w + 12;
    }
    char text[80], value[24];
    std::snprintf(text, sizeof text, "ch%02d / %02d", d.chain + 1, d.slot + 1);
    right(m::right_edge(400), m::kHeaderTextY, text, m::ghost());
    if(osc) { osc_inspector();return true; }
    if(UnitKind(d.kind)==UnitKind::Motor) { motor_inspector();return true; }
    ui::text(14, module_heading::kDetailY, title(UnitKind(d.kind), d.param), m::spot());
    const char* hero_label = frame.lock ? "edit lock" : d.temp ? "temporary" : "base";
    ui::text(14, 64, hero_label, m::ghost());
    format(d.temp ? d.control : frame.edit, value, sizeof value,!d.temp); hero(value);
    format(d.base, value, sizeof value,2);
    std::snprintf(text, sizeof text, "stored base %s", value);
    ui::text(14, 166, text, m::ghost());
    if (!delay) ui::line(239, 48, 239, 211, m::branch());
    ui::text(254, 46, filter ? (d.param<2?"smoothed":"effective") : (osc || delay || fold || tilt || ring || strain || mic || rec || noise || gran) ? "effective" : "live", m::ghost());
    format(live_value, value, sizeof value); ui::text(254, 60, value, m::spot());
    format(d.control, value, sizeof value);
    std::snprintf(text, sizeof text, "control %s", value); ui::text(254, 78, text, m::ink());
    format(d.target, value, sizeof value);
    std::snprintf(text, sizeof text, "target %s", value); ui::text(254, 92, text, m::ghost());
    if (d.source == ModSource::None) std::snprintf(text, sizeof text, "mod off");
    else if (d.source == ModSource::EnvFollow) std::snprintf(text, sizeof text, "env ch%d", d.env_chain + 1);
    else std::snprintf(text, sizeof text, "sg.%c", 'a' + mod_source_sigil(d.source));
    ui::text(254, 113, text, d.source == ModSource::None ? m::ink() : d.mod_enabled ? m::spot() : m::ghost());
    std::snprintf(text, sizeof text, "%s %+d%%", d.source != ModSource::None && !d.mod_enabled ? "muted" : "depth", int(d.depth) * 100 / 32767);
    ui::text(254, 127, text, m::ghost());
    ui::text(254, 148, d.clip_low || d.clip_high ? "range / clips" : "potential range", m::ghost());
    format(d.range_min, value, sizeof value); ui::text(254, 162, value, m::ink());
    format(d.range_max, value, sizeof value); ui::text(254, 175, value, m::ink());

    // Uniform legal raw-parameter scale. The connector is instantaneous edit/live
    // displacement; potential range remains separately labelled in native units.
    const bool bipolar=(gran || fold || tilt || ring || strain || (noise && noise_focus_modern()) || UnitKind(d.kind)==UnitKind::Bitcrush) && d.bipolar;
    const int extent=(mic || rec || noise)?170:210,rail_end=14+extent;
    auto px = [bipolar,extent](q15 v) {
        const int raw=bipolar?(v < -32767 ? -32767 : int(v)):uni(v);
        return 14+(raw+(bipolar?32767:0))*extent/(bipolar?65534:32767);
    };
    const int base = px(frame.edit), live = px(live_value);
    const int neutral=mic && d.param==MicUnit::P_GAIN?px(8192):bipolar?px(0):-1;
    module_fader::horizontal(14,rail_end,184,base,live,
        {d.source!=ModSource::None && d.mod_enabled,false,frame.bypassed[d.slot]||frame.mute},neutral);
    if (d.clip_low) ui::line(11, 178, 11, 190, m::spot());
    if (d.clip_high) ui::line(rail_end+3, 178, rail_end+3, 190, m::spot());
    if (fold && d.param==FoldUnit::P_MODE) {
        ui::text(14,199,"fold",m::ghost());ui::text(105,199,"wrap",m::ghost());right(224,199,"diode",m::ghost());
    } else {
        format(bipolar?-32767:0, value, sizeof value,1); ui::text(14, 199, value, m::ghost());
        format(32767, value, sizeof value,1); right(rail_end, 199, value, m::ghost());
        if(mic && d.param==MicUnit::P_GAIN)ui::text(px(8192),199,"0",m::ghost());
        if(mic && d.param==MicUnit::P_LOW_CUT)ui::text(92,199,"80",m::ghost());
    }
    if (delay) {
        // Last processed read diagnostic, rounded to ms, NOT mapped effective TIME.
        // Bypass/mute/unavailable cannot claim a currently applied read head.
        if (!frame.delay_ready || (frame.modern_delay && !frame.delay.ready) || frame.bypassed[d.slot] || frame.mute)
            std::snprintf(text, sizeof text, "applied --");
        else if (frame.modern_delay) {
            if (frame.delay.crossfading) std::snprintf(text,sizeof text,"xf %u>%ums",
                unsigned((frame.delay.read_a_q8+4096)/8192),unsigned((frame.delay.read_b_q8+4096)/8192));
            else std::snprintf(text,sizeof text,"applied ~%ums",unsigned((frame.delay.read_a_q8+4096)/8192));
        } else if (d.param == DelayUnit::P_TIME)
            std::snprintf(text, sizeof text, "applied ~%ums", unsigned((frame.delay_frames + 16) / 32));
        else if (d.param == DelayUnit::P_FEEDBACK && frame.delay_hold)
            std::snprintf(text, sizeof text, "hold fb ~99.5%%");
        else std::snprintf(text, sizeof text, "last hold %s", frame.delay_hold ? "on" : "off");
        ui::text(254, 191, text, m::ghost());
    } else ui::text(254, 191, "hollow / solid", m::ghost());
    static const char* layers[] = {"base", "step lock", "audition", "performance", "temp"};
    std::snprintf(text, sizeof text, "via %s", layers[d.layer < 5 ? d.layer : 0]);
    ui::text(254, 205, text, d.layer ? m::spot() : m::ghost());
    if (delay && frame.modern_delay) {
        std::snprintf(text,sizeof text,"%s / %s / bpm %.1f / aim %ums%s",
            frame.delay.sync?"sync":"free",frame.delay.clean?"clean":"tape",
            double(frame.delay.bpm_q16)/65536.0,unsigned((frame.delay.target_frames+16)/32),frame.delay.limited?"!":"");
        ui::text(14,217,text,m::ghost());
    } else if (tilt) {
        if(frame.lens)std::snprintf(text,sizeof text,"mod / b: return to tilt");
        else if(!frame.modern_tilt)std::snprintf(text,sizeof text,(frame.lock?frame.legacy_tilt_edit_zero:frame.legacy_tilt_zero)?"legacy coefficient law / model, not audio":"legacy stepped shelves / model, not audio");
        else if(d.param==TiltUnit::P_TRIM)std::snprintf(text,sizeof text,"linear eq / fixed scale / not captured audio");
        else {
            uint32_t low,high;tilt_eq::shelf_gains(frame.tilt_values[0],frame.tilt_values[4],true,low,high);
            std::snprintf(text,sizeof text,"shelf targets %+.2f / %+.2fdb",20.0*std::log10(double(low)/65536.0),20.0*std::log10(double(high)/65536.0));
        }
        ui::text(14,217,text,m::ghost());
    } else if (osc) {
        ui::text(14,217,frame.lens?"mod / b: return to osc":frame.lock?
            "lock draft / normalized reset model, not audio":"two-cycle reset model / fixed scale / not live pcm",m::ghost());
    } else if (gran) {
        ui::text(14,217,frame.lens?"mod / b: return to gran":frame.lock?
            "lock draft / waveform and heads remain actual":"solid: next start / hollow: stored / lines: grains",m::ghost());
    } else if (rec) {
        ui::text(14,217,recorder_focus_status(),m::ghost());
    } else if (mic) {
        ui::text(14,217,frame.lens?"mod / b: return to mic":
            frame.lock?"lock draft does not replace raw input history":"input relief is before gain / cut / source mute",m::ghost());
    } else if (strain) {
        ui::text(14,217,frame.lens?"mod / b: return to strain":
            frame.lock?"lock reset 32ms reference / not live audio":"reset 32ms burst + silence / not live audio",m::ghost());
    } else if (ring) {
        ui::text(14,217,frame.lens?"mod / b: return to ring":
            frame.lock?"lock reset 32ms reference / not live audio":"reset 32ms reference / not live audio",m::ghost());
    } else if (UnitKind(d.kind)==UnitKind::Bitcrush) {
        ui::text(14,217,frame.lens?"mod / b: return to bit":"fixed triangle / reset-state model, not live audio",m::ghost());
    } else if (fold) {
        const char* shape=frame.fold_mode==0?"fold":frame.fold_mode==16384?"wrap":frame.fold_mode==32767?"diode":
                          frame.fold_mode<16384?"fold > wrap":"wrap > diode";
        if(frame.lens)std::snprintf(text,sizeof text,"mod / b: return to fold");
        else if(frame.bypassed[d.slot])std::snprintf(text,sizeof text,"bypassed / dry reference, not captured audio");
        else std::snprintf(text,sizeof text,"%s / %s test model, not audio",shape,frame.lock?"lock draft":frame.mute?"muted":"effective");
        ui::text(14,217,text,m::ghost());
    } else if (filter) {
        static const char* modes[]={"low pass / removes upper frequencies", "high pass / removes lower frequencies",
                                    "band pass / keeps a frequency region", "notch / subtracts band from input", "off / exact dry"};
        ui::text(14,217,frame.lens?"mod / b: return to filter":modes[int(frame.filter_type)],m::ghost());
    } else ui::text(14, 217, delay ? (frame.lens ? "mod / b: return to module" : "dry tick / echo taps / edit model") :
             frame.lens ? "mod / b: return to module" : "select below / hold l or tap mod", m::ghost());
    ui::text(m::kLeft, m::kTextY, explanation(), m::ghost());
    return true;
}
} // namespace seance::platform::ui3ds
