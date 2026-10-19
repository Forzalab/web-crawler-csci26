// theme.h - one accent (orange), white content, grey chrome. Swiss.
#pragma once

#include <ftxui/screen/color.hpp>

#include "core/graph.h"
#include "ui/anim.h"

namespace wc::theme {

// Plain RGB first (anim::mix fades between them), then the FTXUI colours.
namespace rgb {
inline constexpr anim::Rgb kAccent{255, 140, 0}, kAccentDim{170, 90, 0}, kText{240, 240, 240},
    kChrome{110, 110, 110}, kFaint{60, 60, 60}, kError{220, 60, 50};
}  // namespace rgb
inline ftxui::Color to_color(anim::Rgb c) { return ftxui::Color::RGB(uint8_t(c.r), uint8_t(c.g), uint8_t(c.b)); }

inline const ftxui::Color kAccent = to_color(rgb::kAccent);
inline const ftxui::Color kAccentDim = to_color(rgb::kAccentDim);
inline const ftxui::Color kText = to_color(rgb::kText);
inline const ftxui::Color kChrome = to_color(rgb::kChrome);
inline const ftxui::Color kFaint = to_color(rgb::kFaint);
inline const ftxui::Color kError = to_color(rgb::kError);  // bad input only (URL step)

ftxui::Color depth_color(int depth);       // 0 white, 1 orange, 2 dim orange, 3+ greys

// Lava ramp: depth 0 white-hot -> yellow -> orange -> red -> crimson -> violet ash at max_depth.
// Exact endpoints, luminance never rises with depth. max_depth <= 0 -> white-hot.
anim::Rgb depth_rgb(int depth, int max_depth);
// The ramp as a colour, `cool` in [0,1] from white-hot (0, just landed) to its layer colour (1).
// NO_COLOR set -> depth_color(depth): the old look, so headless snapshots stay put.
ftxui::Color depth_color(int depth, int max_depth, double cool = 1);
bool no_color();  // NO_COLOR is set
const char* glyph(int shape, Status s);    // shape = site rank: ◆ ● ■ ▲ ★ ✚, -1 ·, ✕ failed

}  // namespace wc::theme
