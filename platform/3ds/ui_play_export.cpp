#include "ui_play_export.h"
#include "ui_state.h"
#include "sample_ownership.h"
#include "../../core/synth/wav_writer.h"
#include "../../core/synth/wav_loader.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace seance::platform::ui3ds {
namespace {
using Bank = trackr::synth::SampleBank;
using Sample = trackr::synth::Sample;
using Provenance = trackr::synth::SampleProvenance;
constexpr uint32_t BLOCK = 2048;
constexpr int32_t HEADROOM = 29204; // round(32767 * 10^(-1/20)); common stereo gain
char notice[128];
const char* tell(const char* text) { std::snprintf(notice,sizeof notice,"%s",text); return notice; }
struct Context {
    Engine* engine = nullptr;
    platform::AudioOut* audio = nullptr;
    PlayUnit* unit = nullptr;
    int chain = -1, unit_slot = -1, source = -1, target = -1;
    uint32_t generation = 0, target_generation = 0, frames = 0, first = 0, end = 0;
    const q15* allocation = nullptr;
    size_t elements = 0;
    uint8_t channels = 0;
    q15 bases[PlayUnit::P_COUNT] = {}, depths[PlayUnit::P_COUNT] = {};
    ModSource routes[PlayUnit::P_COUNT] = {};
    bool modern = false, quantize = false, bypass = false, stale = false;
    int root = 60;
    uint32_t sample_loop_start = 0, sample_loop_end = 0;
    bool sample_reversed = false, exact_native_endpoint = false;
    uint32_t chops[Sample::MAX_CHOPS] = {};
    uint16_t slice_reverse = 0, slice_speed_q8[Sample::MAX_CHOPS] = {};
    int8_t slice_pitch_semitones[Sample::MAX_CHOPS] = {};
    Provenance provenance;
    PlayEditState edit;
    PlayWindow fade_window;
    PlayExportOptions options;
    int32_t dc[2] = {};
    int32_t peak = 0;
};
bool free_slot(const Engine& engine,int slot) {
    const auto& bank=Bank::instance();const auto& s=bank.slot(slot);const auto& p=bank.provenance(slot);
    return s.data.empty() && p.origin==trackr::synth::SampleOrigin::Runtime && !p.missing && !p.path[0] &&
        !sample_ownership::count(engine,slot,g_clip_unit) && engine.bounce_snapshot().slot!=slot;
}
bool ui_context() {
    return g_eng && g_audio && g_view==View::Chains && g_overlay==Overlay::Editor &&
        g_edit_chain>=0 && g_edit_chain<NUM_CHAINS && g_edit_slot>=0 && g_edit_slot<UNITS_PER_CHAIN;
}
bool valid(const Context& c) {
    if (!ui_context() || g_eng!=c.engine || g_audio!=c.audio || g_edit_chain!=c.chain || g_edit_slot!=c.unit_slot ||
        c.engine->chains[c.chain].units[c.unit_slot]!=c.unit || c.engine->play_editor_busy() || c.engine->bounce_snapshot().active()) return false;
    // Pointer identity is checked before dereferencing the captured unit.
    const auto& u=*c.unit;const auto& b=Bank::instance();
    if(u.sample_slot!=c.source || b.generation(c.source)!=c.generation || !u.edit_source_matches() ||
        u.modern()!=c.modern || u.pitch_quantize!=c.quantize || u.bypass!=c.bypass || !play_edit_equal(u.edit_state(),c.edit)) return false;
    for(int p=0;p<PlayUnit::P_COUNT;++p)
        if(u.params[p].base!=c.bases[p] || u.params[p].mod_depth!=c.depths[p] || u.params[p].src!=c.routes[p]) return false;
    const auto&s=b.slot(c.source);const auto&p=b.provenance(c.source);
    return s.channels==c.channels && s.playable_frames()==c.frames && s.data.data()==c.allocation && s.data.size()==c.elements &&
        s.root_note==c.root && s.loop_start==c.sample_loop_start && s.loop_end==c.sample_loop_end && s.reversed==c.sample_reversed &&
        s.exact_native_endpoint==c.exact_native_endpoint &&
        !std::memcmp(s.chops,c.chops,sizeof c.chops) && s.slice_reverse_mask==c.slice_reverse &&
        !std::memcmp(s.slice_speed_q8,c.slice_speed_q8,sizeof c.slice_speed_q8) &&
        !std::memcmp(s.slice_pitch_semitones,c.slice_pitch_semitones,sizeof c.slice_pitch_semitones) &&
        p.origin==c.provenance.origin && p.missing==c.provenance.missing && !std::strcmp(p.path,c.provenance.path);
}
bool raw(Context& c,uint32_t first,uint32_t count,int16_t* output) {
    if(count>BLOCK || first>c.end-c.first || count>c.end-c.first-first)return false;
    const bool reverse=c.options.reverse && !c.options.detach;
    const uint32_t source=reverse?c.end-first-count:c.first+first;
    {
        platform::AudioOut::Guard guard(*c.audio);
        if(!valid(c)){c.stale=true;return false;}
        std::memcpy(output,Bank::instance().slot(c.source).data.data()+(size_t)source*c.channels,
                    (size_t)count*c.channels*sizeof(int16_t));
    }
    // Reverse/analysis/process never run under the audio guard.
    if(reverse)for(uint32_t a=0;a<count/2;++a)for(unsigned ch=0;ch<c.channels;++ch)
        std::swap(output[(size_t)a*c.channels+ch],output[(size_t)(count-a-1)*c.channels+ch]);
    return true;
}
int32_t transformed(const Context& c,uint32_t output_frame,unsigned channel,int16_t sample) {
    int32_t value=(int32_t)sample-c.dc[channel];
    if(c.options.bake && !c.options.detach && c.edit.enabled) {
        const auto&w=c.fade_window;
        const uint32_t at=c.options.reverse?c.end-1-output_frame:c.first+output_frame;
        uint32_t envelope=32767;
        if(!w.valid || at<w.start || at>=w.end) return 0;
        if(w.fade_in && at-w.start<w.fade_in)envelope=(uint32_t)((uint64_t)(at-w.start)*32767/w.fade_in);
        if(w.fade_out && w.end-1-at<w.fade_out)envelope=std::min<uint32_t>(envelope,(uint32_t)((uint64_t)(w.end-1-at)*32767/w.fade_out));
        value=(int32_t)((int64_t)value*envelope/32767);
    }
    return value; // retain >16-bit DC headroom until the final normalize/clip
}
bool processed(void* opaque,uint32_t first,uint32_t count,int16_t* output) {
    auto&c=*static_cast<Context*>(opaque);
    if(!raw(c,first,count,output))return false;
    if(c.options.detach)return true;
    for(uint32_t f=0;f<count;++f)for(unsigned ch=0;ch<c.channels;++ch) {
        const size_t at=(size_t)f*c.channels+ch;
        int64_t value=transformed(c,first+f,ch,output[at]);
        if(c.options.normalize && c.peak)value=value*HEADROOM/c.peak;
        output[at]=(int16_t)std::max<int64_t>(-32768,std::min<int64_t>(32767,value));
    }
    return true;
}
// Preserve PLAY's exclusive final+fixed-temp placeholder policy, but carry the
// descriptor/token into the writer instead of closing then reopening with wb.
bool reserve_path(char (&path)[192], fileio::NewFileReservation& reservation) {
    for(unsigned sequence=1;sequence<=9999;++sequence) {
        const int n=std::snprintf(path,sizeof path,"%s/play_%04u.wav",RECORDING_DIR,sequence);
        if(n<0 || n>=(int)sizeof path)return false;
        const auto result=reservation.reserve(path);
        if(result==fileio::ReserveResult::Ok)return true;
        if(result!=fileio::ReserveResult::Occupied)return false;
    }
    return false;
}
const char* retained(const char* path,const char* reason) {
    const char* name=std::strrchr(path,'/');name=name?name+1:path;
    std::snprintf(notice,sizeof notice,"%s saved / %s",name,reason);return notice;
}
} // namespace

const char* play_export_copy(PlayUnit& play,const PlayExportOptions& options) {
    if(!ui_context())return tell("editor unavailable / no changes");
    if(options.crop && options.detach)return tell("choose cut or detach / no changes");
    Context c;c.options=options;
    if(options.crop)c.options.normalize=c.options.dc=c.options.bake=c.options.reverse=false;
    {
        platform::AudioOut::Guard guard(*g_audio);
        if(g_eng->chains[g_edit_chain].units[g_edit_slot]!=&play || g_eng->play_editor_busy() || g_eng->bounce_snapshot().active())
            return tell("finish recording / editor changed");
        c.engine=g_eng;c.audio=g_audio;c.unit=&play;c.chain=g_edit_chain;c.unit_slot=g_edit_slot;c.source=play.sample_slot;
        if(c.source<0 || c.source>=trackr::synth::SAMPLE_BANK_SIZE || !play.edit_source_matches())return tell("source missing or changed / no changes");
        const auto&b=Bank::instance();const auto&s=b.slot(c.source);
        c.frames=s.playable_frames();c.channels=s.channels;c.first=options.detach?0:options.first;c.end=options.detach?c.frames:options.end;
        if((c.channels!=1 && c.channels!=2) || c.first>=c.end || c.end>c.frames || c.end-c.first<2 || c.end-c.first>INT_MAX)
            return tell("invalid source selection / no changes");
        for(int slot=0;slot<trackr::synth::SAMPLE_BANK_SIZE;++slot)if(free_slot(*g_eng,slot)){c.target=slot;break;}
        if(c.target<0)return tell("no empty unreferenced sample slot");
        const uint64_t elements=(uint64_t)(c.end-c.first)*c.channels;
        if(elements>SIZE_MAX || !b.can_replace(c.target,(size_t)elements) || elements*4u>trackr::synth::WAV_DECODE_WORKSPACE_BYTES)
            return tell("copy exceeds memory budget / no changes");
        c.generation=b.generation(c.source);c.target_generation=b.generation(c.target);
        c.allocation=s.data.data();c.elements=s.data.size();c.root=s.root_note;
        c.sample_loop_start=s.loop_start;c.sample_loop_end=s.loop_end;c.sample_reversed=s.reversed;
        c.exact_native_endpoint=s.exact_native_endpoint;
        std::memcpy(c.chops,s.chops,sizeof c.chops);c.slice_reverse=s.slice_reverse_mask;c.provenance=b.provenance(c.source);
        std::memcpy(c.slice_speed_q8,s.slice_speed_q8,sizeof c.slice_speed_q8);
        std::memcpy(c.slice_pitch_semitones,s.slice_pitch_semitones,sizeof c.slice_pitch_semitones);
        c.edit=play.edit_state();c.modern=play.modern();c.quantize=play.pitch_quantize;c.bypass=play.bypass;
        for(int p=0;p<PlayUnit::P_COUNT;++p){c.bases[p]=play.params[p].base;c.routes[p]=play.params[p].src;c.depths[p]=play.params[p].mod_depth;}
        c.fade_window=play.projected_window(c.bases[PlayUnit::P_START],c.bases[PlayUnit::P_LEN],0);
        if(options.crop && (!c.modern || !c.fade_window.valid || c.first!=c.fade_window.start || c.end!=c.fade_window.end))
            return tell("cut needs modern whole trim / no changes");
        if(options.crop && c.first==0 && c.end==c.frames)return tell("already whole source / no changes");
    }
    int16_t scratch[BLOCK*2];
    const uint32_t count=c.end-c.first;
    if(c.options.dc && !c.options.detach) {
        int64_t sums[2]={};
        for(uint32_t first=0;first<count;) {
            const uint32_t n=std::min<uint32_t>(BLOCK,count-first);
            if(!raw(c,first,n,scratch))return tell("source changed / no changes");
            for(uint32_t f=0;f<n;++f)for(unsigned ch=0;ch<c.channels;++ch)sums[ch]+=scratch[(size_t)f*c.channels+ch];
            first+=n;
        }
        for(unsigned ch=0;ch<c.channels;++ch)c.dc[ch]=(int32_t)(sums[ch]/count);
    }
    if(c.options.normalize && !c.options.detach) {
        for(uint32_t first=0;first<count;) {
            const uint32_t n=std::min<uint32_t>(BLOCK,count-first);
            if(!raw(c,first,n,scratch))return tell("source changed / no changes");
            for(uint32_t f=0;f<n;++f)for(unsigned ch=0;ch<c.channels;++ch) {
                const int32_t v=transformed(c,first+f,ch,scratch[(size_t)f*c.channels+ch]);
                c.peak=std::max<int32_t>(c.peak,v<0?-v:v);
            }
            first+=n;
        }
    }
    char path[192];
    fileio::NewFileReservation reservation;
    if(!reserve_path(path,reservation))return tell("sd error / no unused wav filename");
    const auto result=trackr::synth::save_wav_stream(path,c.channels,count,ENGINE_SR,scratch,BLOCK,processed,&c,&reservation);
    if(result!=trackr::synth::WavSaveResult::Ok) {
        reservation.discard(); // never gain cleanup authority from path spelling
        return tell(c.stale?"source changed / export cancelled":"sd write failed / edits unchanged");
    }
    // Successful WAV is retained even if a later decode/commit cannot proceed.
    Sample decoded;
    trackr::synth::WavDecodeLimits limits;limits.workspace_bytes=trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    limits.preserve_native_endpoint=true;
    const auto loaded=trackr::synth::load_wav_to_sample(path,decoded,ENGINE_SR,(int)count,limits);
    if(loaded!=trackr::synth::WavLoadResult::Ok || decoded.playable_frames()!=count || decoded.channels!=c.channels)
        return retained(path,"slot reload failed");
    decoded.root_note=c.root;
    if(c.options.detach) {
        std::memcpy(decoded.chops,c.chops,sizeof c.chops);decoded.slice_reverse_mask=c.slice_reverse;
        std::memcpy(decoded.slice_speed_q8,c.slice_speed_q8,sizeof c.slice_speed_q8);
        std::memcpy(decoded.slice_pitch_semitones,c.slice_pitch_semitones,sizeof c.slice_pitch_semitones);
        decoded.loop_start=c.sample_loop_start;decoded.loop_end=c.sample_loop_end;decoded.reversed=c.sample_reversed;
    }
    // A new source retains independent stable-ID part metadata. Raw PCM copy
    // never pretends to bake musical pitch, stretch or the chain DSP into a WAV.
    if(!c.options.detach) {
        for(int id=0;id<Sample::MAX_CHOPS;++id) {
            const uint32_t a=c.chops[id];if(a==UINT32_MAX || a>=c.frames)continue;
            uint32_t b=c.frames;for(auto p:c.chops)if(p>a && p<b)b=p;
            const uint32_t left=std::max(a,c.first),right=std::min(b,c.end);
            if(left>=right || right-left<2)continue;
            // Crop/forward copy keeps the intersecting left edge at zero.
            decoded.chops[id]=c.options.reverse?c.end-right:left-c.first;
            decoded.slice_speed_q8[id]=c.slice_speed_q8[id];
            decoded.slice_pitch_semitones[id]=c.slice_pitch_semitones[id];
            decoded.slice_reverse_mask|=c.slice_reverse&uint16_t(1u<<id);
        }
    }
    PlayEditState cropped;
    if(c.options.crop) {
        decoded.reversed=c.sample_reversed;
        const uint32_t la=std::max(c.sample_loop_start,c.first),lb=std::min(c.sample_loop_end,c.end);
        if(la<lb && lb-la>=2){decoded.loop_start=la-c.first;decoded.loop_end=lb-c.first;}
        cropped.enabled=true;cropped.source_frames=cropped.end=count;
        cropped.fade_in=c.fade_window.fade_in;cropped.fade_out=c.fade_window.fade_out;
        cropped.loop_end=count;
        if(c.fade_window.edited) {
            if(c.fade_window.loop_end>c.fade_window.loop_start+1) {
                cropped.loop_start=c.fade_window.loop_start-c.first;cropped.loop_end=c.fade_window.loop_end-c.first;
                cropped.crossfade=c.fade_window.crossfade;
            }
        }
        if(!play_edit_valid(cropped))return retained(path,"invalid cut geometry; slot unchanged");
    }
    {
        platform::AudioOut::Guard guard(*c.audio);
        auto&b=Bank::instance();
        if(!valid(c) || b.generation(c.target)!=c.target_generation || !free_slot(*c.engine,c.target))
            return retained(path,"context changed; slot unchanged");
        if(!b.can_replace(c.target,decoded.data.size()))return retained(path,"slot memory budget changed");
        b.swap_sample_unchecked(c.target,decoded,trackr::synth::SampleOrigin::Recording,path);
        if(c.options.detach || c.options.crop) {
            c.engine->stop_play_preview();c.unit->reset();c.unit->sample_slot=c.target;
            if(c.options.crop) {
                c.unit->params[PlayUnit::P_START].base=0;
                c.unit->params[PlayUnit::P_LEN].base=32767;
                // Re-resolve in the new source domain without clearing locks,
                // TEMP or modulation as Param::snap() would do.
                c.unit->params[PlayUnit::P_START].slew_ready=false;
                c.unit->params[PlayUnit::P_LEN].slew_ready=false;
                // Geometry, modern flag and new source length were preflighted.
                c.unit->set_edit_state(cropped,b.generation(c.target));
            } else c.unit->bind_edit_source(); // exact same playable length
        }
    }
    mark_project_mutated();
    std::snprintf(notice,sizeof notice,c.options.crop?"cut to slot %02d / original kept":c.options.detach?"detached to slot %02d / original kept":"copy saved to slot %02d / original kept",c.target+1);
    return notice;
}
} // namespace seance::platform::ui3ds
