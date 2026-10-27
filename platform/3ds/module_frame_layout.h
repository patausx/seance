#pragma once

// Fixed module chrome in native LCD pixels. These envelopes describe title and
// caption placement, NOT exclusive bands, clipping regions or touch dead zones.
// Some editors intentionally have wide graphs or padded targets near the edges.
namespace seance::platform::ui3ds::module_frame_layout {
struct Rect {
    int x, y, w, h;
    constexpr int right() const { return x + w; }
    constexpr int bottom() const { return y + h; }
};
constexpr int kTopWidth = 400;
constexpr int kBottomWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kLeft = 14;
constexpr int kRightInset = 14;
constexpr int kTopInset = 8;
constexpr int kBottomInset = 4;
constexpr int kHeaderHeight = 26;
constexpr int kFooterHeight = 12;
constexpr int kTextHeight = 8;
constexpr int kTextAdvance = 6;
constexpr int kHeaderTextY = 14; // also fits the existing half-open y<22 targets
constexpr int kHeaderMarkY = 26;
constexpr int kFooterTextY = 228;
constexpr int kCaptionGap = 8;
// Fixed, rather than computed from today's glyphs: adding a label must fit the
// contract, not silently move every breadcrumb and parameter heading.
constexpr int kTitleMaxWidth = 57;
constexpr int kTitleMaxHeight = 25;
constexpr int kRouteY = 35;
constexpr int kRouteCrossY = 39;
constexpr int kRouteUnderlineY = 45;
constexpr int kDetailY = 49;
constexpr int right_edge(int width) { return width - kRightInset; }
constexpr Rect frame(int width) {
    return {kLeft, kTopInset, width - kLeft - kRightInset,
            kScreenHeight - kTopInset - kBottomInset};
}
constexpr Rect header(int width) {
    return {kLeft, kTopInset, width - kLeft - kRightInset, kHeaderHeight};
}
constexpr Rect footer(int width) {
    return {kLeft, kScreenHeight - kBottomInset - kFooterHeight,
            width - kLeft - kRightInset, kFooterHeight};
}
static_assert(frame(kBottomWidth).w == 292 && frame(kTopWidth).w == 372);
static_assert(frame(kBottomWidth).h == 228 && frame(kTopWidth).h == 228);
static_assert(kHeaderTextY + kTextHeight <= 22);
static_assert(kHeaderMarkY < header(kBottomWidth).bottom());
static_assert(kTopInset + kTitleMaxHeight + 2 == kRouteY);
static_assert(kFooterTextY + kTextHeight == footer(kBottomWidth).bottom());
} // namespace seance::platform::ui3ds::module_frame_layout
