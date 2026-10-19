// theme.cc - see theme.h
#include "ui/theme.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace wc::theme {

ftxui::Color depth_color(int depth) {
    switch (depth) {
        case 0: return kText;
        case 1: return kAccent;
        case 2: return kAccentDim;
        case 3: return ftxui::Color::RGB(150, 150, 150);
        default: return kChrome;
    }
}

anim::Rgb depth_rgb(int depth, int max_depth) {
    static const anim::Rgb kLava[] = {{255, 248, 230}, {255, 220, 60}, {255, 140, 0},
                                      {230, 50, 20},   {190, 25, 55},  {85, 48, 105}};
    constexpr int kSeg = int(std::size(kLava)) - 1;
    if (max_depth <= 0 || depth <= 0) return kLava[0];
    if (depth >= max_depth) return kLava[kSeg];
    const double x = double(depth) / max_depth * kSeg;
    const int i = std::min(kSeg - 1, int(x));
    return anim::mix(kLava[i], kLava[i + 1], x - i);
}

bool no_color() {
    static const bool off = std::getenv("NO_COLOR") != nullptr;  // read once: called per edge per frame
    return off;
}

ftxui::Color depth_color(int depth, int max_depth, double cool) {
    if (no_color()) return depth_color(depth);
    return to_color(anim::mix(depth_rgb(0, max_depth), depth_rgb(depth, max_depth), cool));
}

const char* glyph(int shape, Status s) {
    static const char* kGlyph[] = {"◆", "●", "■", "▲", "★", "✚"};  // kShapes
    if (s == Status::Failed) return "✕";
    return shape >= 0 && shape < int(std::size(kGlyph)) ? kGlyph[shape] : "·";
}

}  // namespace wc::theme
