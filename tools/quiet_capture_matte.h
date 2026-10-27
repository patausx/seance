#pragma once
// Capture-only helpers. No runtime changes and no test suite execution.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace quiet_capture {
constexpr uint32_t paper = 0xff242424u;
constexpr uint32_t probe = 0xff890ed9u; // deliberately non-palette RGB(217,14,137)
inline bool first_rect = true, probe_pass = false;
inline int screen_width = 320;
inline void begin(int width, bool matte) {
    screen_width = width; probe_pass = matte; first_rect = true;
}
inline uint32_t shade(float x, float y, float w, float h, uint32_t color) {
    if (!first_rect) return color;
    first_rect = false;
    if (x != 0 || y != 0 || w != screen_width || h != 240 || color != paper)
        throw std::runtime_error("capture expects the current graphite full-screen first paper rectangle");
    return probe_pass ? probe : color;
}
inline void save(const std::filesystem::path& directory, const std::string& name,
                 int width, const std::vector<uint32_t>& normal,
                 const std::vector<uint32_t>& matte) {
    if (normal.size() != size_t(width * 240) || normal.size() != matte.size())
        throw std::runtime_error("unexpected raster dimensions");
    std::ofstream image(directory / (name + ".ppm"), std::ios::binary);
    std::ofstream mask(directory / (name + "_paper_mask.pgm"), std::ios::binary);
    image << "P6\n" << width << " 240\n255\n";
    mask << "P5\n" << width << " 240\n255\n";
    for (size_t i = 0; i < normal.size(); ++i) {
        const auto p = normal[i];
        const char rgb[] = {char(p), char(p >> 8), char(p >> 16)};
        image.write(rgb, 3);
        // Both passes must retain the exact substrate. A paper-colored opaque
        // UI fill has matte==paper, not probe, and is therefore protected.
        mask.put((p == paper && matte[i] == probe) ? char(255) : char(0));
    }
    if (!image || !mask) throw std::runtime_error("could not save native capture");
}
} // namespace quiet_capture
