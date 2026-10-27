#pragma once
#include "../../core/synth/sampler.h"
#include <cstdint>

namespace seance::platform::ui3ds::pcm_envelope {
constexpr int PROBES = 8;
// Caller holds the sample/audio guard. Fixed work and stack bounds irrespective
// of file length; these are sampled extrema, not a full-file peak measurement.
// Gain is applied before 8-bit projection so quiet PCM is not quantized away.
template<int Columns>
uint16_t capture(const trackr::synth::Sample& sample, int8_t (&low)[Columns],
                 int8_t (&high)[Columns], uint32_t& reads) {
    static_assert(Columns > 1 && Columns <= 352, "bounded waveform");
    int16_t minima[Columns], maxima[Columns];
    reads = 0;
    const uint32_t frames = sample.playable_frames();
    if (frames < 2 || (sample.channels != 1 && sample.channels != 2)) {
        for (int x=0;x<Columns;++x) low[x]=high[x]=0;
        return 256;
    }
    int peak = 0;
    for (int x=0;x<Columns;++x) {
        int lo=32767, hi=-32768;
        for (int n=0;n<PROBES;++n) {
            const uint32_t at=(uint32_t)((uint64_t)(x*PROBES+n)*(frames-1)/(Columns*PROBES-1));
            for (int c=0;c<sample.channels;++c) {
                const int value=sample.data[(size_t)at*sample.channels+c]; ++reads;
                if (value<lo)lo=value;
                if (value>hi)hi=value;
            }
        }
        minima[x]=(int16_t)lo; maxima[x]=(int16_t)hi;
        if (-lo>peak)peak=-lo;
        if (hi>peak)peak=hi;
    }
    // Leave loud material at unity; show quieter material at up to 64x. This is
    // NOT loudness normalization, playback gain, or an animated fake waveform.
    uint32_t gain=peak ? 24576u*256u/(uint32_t)peak : 256u;
    if (gain<256)gain=256;
    if (gain>16384)gain=16384;
    for (int x=0;x<Columns;++x) {
        int lo=(int32_t)minima[x]*(int32_t)gain/65536;
        int hi=(int32_t)maxima[x]*(int32_t)gain/65536;
        if (lo< -128)lo=-128;
        if (hi>127)hi=127;
        low[x]=(int8_t)lo; high[x]=(int8_t)hi;
    }
    return (uint16_t)gain;
}
} // namespace seance::platform::ui3ds::pcm_envelope
