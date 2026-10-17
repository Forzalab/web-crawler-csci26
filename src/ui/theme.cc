// theme.cc - see theme.h
#include "ui/theme.h"

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

const char* glyph(int shape, Status s) {
    static const char* kGlyph[] = {"◆", "●", "■", "▲", "★", "✚"};  // kShapes
    if (s == Status::Failed) return "✕";
    return shape >= 0 && shape < int(std::size(kGlyph)) ? kGlyph[shape] : "·";
}

}  // namespace wc::theme
