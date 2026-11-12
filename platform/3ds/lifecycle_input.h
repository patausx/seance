#pragma once
#include <cstdint>

namespace seance::platform {
// UI-thread input authority. The callback publishes an epoch, never UI pointers.
// No-sleep input passes through unchanged. After interruption, held-only bits
// cannot acquire authority; each quarantined bit must lift before a fresh DOWN.
class LifecycleInput {
public:
    bool observe(uint32_t epoch) {
        if (epoch == epoch_) return false;
        epoch_ = epoch;
        interrupted_ = true;
        blocked_ |= physical_held_;
        permitted_ = 0;
        return true;
    }
    void filter(uint32_t& down, uint32_t& held, uint32_t& up) {
        physical_held_ = held;
        if (!interrupted_) return;
        // Also quarantine unknown held-only inputs first reported after wake.
        blocked_ |= held & ~(down | permitted_);
        const uint32_t blocked = blocked_;
        blocked_ &= held;
        const uint32_t previous = permitted_;
        permitted_ = (permitted_ | (down & ~blocked)) & held;
        down &= ~blocked;
        held &= permitted_;
        up &= previous & ~blocked;
    }
private:
    uint32_t epoch_ = 0, physical_held_ = 0, blocked_ = 0, permitted_ = 0;
    bool interrupted_ = false;
};
} // namespace seance::platform
