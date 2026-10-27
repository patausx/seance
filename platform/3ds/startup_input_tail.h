#pragma once
#include <cstdint>

namespace seance::platform::ui3ds {
// A transition owns only the keys already down, never unrelated future input.
// Release edges belong to that same tail; a later fresh press is live again.
class StartupInputTail {
public:
    void capture(std::uint32_t down, std::uint32_t held) { blocked_ = down | held; }
    void filter(std::uint32_t& down, std::uint32_t& held, std::uint32_t& up) {
        const std::uint32_t blocked = blocked_;
        blocked_ &= held;
        down &= ~blocked;
        held &= ~blocked;
        up &= ~blocked;
    }
private:
    std::uint32_t blocked_ = 0;
};
} // namespace seance::platform::ui3ds
