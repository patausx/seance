#include "ui_chains.h"

namespace seance::platform::ui3ds {
// The passive upper half uses exactly the same frozen column coordinates/DTO
// as the touchscreen, including selection, drag preview and disclosure blur.
void chains_context_draw_top() { chains_draw_surface(true); }
} // namespace seance::platform::ui3ds
