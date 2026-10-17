// theme.cc - see theme.h
#include "ui/theme.h"

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

const char* glyph(int depth, Status s) {
    if (s == Status::Failed) return "✕";
    switch (depth) {
        case 0: return "◆";
        case 1: return "◆";
        case 2: return "•";
        default: return "·";
    }
}

}  // namespace wc::theme
