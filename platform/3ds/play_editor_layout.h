#pragma once
// Private PLAY inline geometry. Half-open native-pixel rectangles; header rand
// owns 228..252. A part label is selection, never a musical SLCE write.
namespace seance::platform::ui3ds::play_editor_layout {
struct Box { int x0,y0,x1,y1; constexpr bool contains(int x,int y)const{return x>=x0&&x<x1&&y>=y0&&y<y1;} };
namespace precision {
inline constexpr int left=14, width=292, columns=372;
inline constexpr Box source{76,8,220,32},files{252,8,306,32};
constexpr Box tool(int i){return {14+i*58,36,68+i*58,60};}
inline constexpr Box mode{218,36,306,60};
inline constexpr Box labels{14,60,306,84},strip{14,84,306,108};
inline constexpr Box fine{14,108,94,132},rate{100,108,204,132};
inline constexpr Box zoom_out{214,108,242,132},zoom_fit{246,108,276,132},zoom_in{282,108,306,132};
inline constexpr Box shot{14,132,76,156},gate{80,132,152,156},loop{166,132,248,156},reverse{258,132,306,156};
inline constexpr Box pitch{14,156,146,186},gain{174,156,306,186};
inline constexpr Box part_pitch{14,186,146,216},part_reverse{174,186,306,216};
constexpr Box footer(int i){return {14+i*102,216,102+i*102,240};}
// The old sheet name survives only as a private contextual-access alias; no
// tools/sound/edit/choose/add controls exist on the inline surface.
inline constexpr Box tools=files;
} // namespace precision
} // namespace seance::platform::ui3ds::play_editor_layout
