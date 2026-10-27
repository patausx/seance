#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace seance::platform::frame_profile {

constexpr size_t Capacity = 192; // > three seconds at the unchanged 60 Hz cap
constexpr uint64_t WindowUs = 3000000, RefreshUs = 250000;
enum Metric : size_t { Prep, Ui, Gpu, Wait, Frame, MetricCount };
struct Stats { uint32_t avg = 0, p95 = 0, max = 0; };
struct Summary {
    Stats metric[MetricCount]{};
    uint64_t missed_window = 0, missed_total = 0;
    size_t samples = 0;
};

inline uint32_t duration(uint64_t us) {
    return uint32_t(std::min(us, uint64_t(std::numeric_limits<uint32_t>::max())));
}

// One cycle: previous fenced begin -> UI submission -> prepare next -> wait ->
// next fenced begin. Thus prep + ui + wait = frame, without counting vblank wait
// as CPU work. GPU is the PREVIOUS completed Citro3D queue, sampled at this fence.
class Ring {
public:
    void reset() {
        head_ = count_ = 0;
        have_begin_ = have_end_ = published_ = false;
        missed_total_ = last_publish_ = 0;
        summary_ = {};
    }
    void begin(uint64_t now, uint64_t requested, uint32_t vblank, uint32_t gpu_us) {
        if (have_begin_ && have_end_ && requested >= end_ && now >= requested) {
            Sample sample{};
            sample.at = now;
            sample.us[Prep] = duration(requested - end_);
            sample.us[Ui] = duration(end_ - begin_);
            sample.us[Gpu] = gpu_us;
            sample.us[Wait] = duration(now - requested);
            sample.us[Frame] = duration(now - begin_);
            // Unsigned subtraction also handles the 32-bit counter wrapping.
            const uint32_t elapsed = vblank - vblank_;
            sample.missed = elapsed > 1 ? elapsed - 1 : 0;
            missed_total_ += sample.missed;
            if (count_ == Capacity) { head_ = (head_ + 1) % Capacity; --count_; }
            samples_[(head_ + count_) % Capacity] = sample;
            ++count_;
            expire(now);
        }
        begin_ = now;
        vblank_ = vblank;
        have_begin_ = true;
        have_end_ = false;
    }
    void end(uint64_t now) {
        if (!have_begin_ || now < begin_) return;
        end_ = now;
        have_end_ = true;
    }
    // Returns true only when the cached display needs reformatting (max 4 Hz).
    bool refresh(uint64_t now) {
        if (published_ && now >= last_publish_ && now - last_publish_ < RefreshUs) return false;
        expire(now);
        summary_ = {};
        summary_.samples = count_;
        summary_.missed_total = missed_total_;
        for (size_t i = 0; i < count_; ++i)
            summary_.missed_window += samples_[(head_ + i) % Capacity].missed;
        for (size_t m = 0; m < MetricCount && count_; ++m) {
            uint64_t sum = 0;
            for (size_t i = 0; i < count_; ++i) {
                const uint32_t value = samples_[(head_ + i) % Capacity].us[m];
                scratch_[i] = value;
                sum += value;
            }
            std::sort(scratch_, scratch_ + count_); // bounded introsort, no allocation
            summary_.metric[m] = {uint32_t(sum / count_),
                scratch_[(95 * count_ + 99) / 100 - 1], scratch_[count_ - 1]};
        }
        last_publish_ = now;
        published_ = true;
        return true;
    }
    const Summary& summary() const { return summary_; }
private:
    struct Sample { uint64_t at; uint32_t us[MetricCount]; uint32_t missed; };
    void expire(uint64_t now) {
        while (count_ && now >= samples_[head_].at && now - samples_[head_].at >= WindowUs) {
            head_ = (head_ + 1) % Capacity;
            --count_;
        }
    }
    Sample samples_[Capacity]{};
    uint32_t scratch_[Capacity]{};
    Summary summary_{};
    size_t head_ = 0, count_ = 0;
    uint64_t begin_ = 0, end_ = 0, last_publish_ = 0, missed_total_ = 0;
    uint32_t vblank_ = 0;
    bool have_begin_ = false, have_end_ = false, published_ = false;
};
static_assert(sizeof(Ring) < 8192, "profiler storage must stay bounded");

} // namespace seance::platform::frame_profile
