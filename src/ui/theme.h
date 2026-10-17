// theme.h - one accent (orange), white content, grey chrome. Swiss.
#pragma once

#include <ftxui/screen/color.hpp>

#include "core/graph.h"

namespace wc::theme {

inline const ftxui::Color kAccent = ftxui::Color::RGB(255, 140, 0);
inline const ftxui::Color kAccentDim = ftxui::Color::RGB(170, 90, 0);
inline const ftxui::Color kText = ftxui::Color::RGB(240, 240, 240);
inline const ftxui::Color kChrome = ftxui::Color::RGB(110, 110, 110);
inline const ftxui::Color kFaint = ftxui::Color::RGB(60, 60, 60);

ftxui::Color depth_color(int depth);       // 0 white, 1 orange, 2 dim orange, 3+ greys
const char* glyph(int shape, Status s);    // shape = site rank: ◆ ● ■ ▲ ★ ✚, -1 ·, ✕ failed

}  // namespace wc::theme
