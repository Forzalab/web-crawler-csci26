// matrix.cc - see matrix.h
#include "ui/matrix.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>

namespace wc::matrix {
namespace {

// Half-width katakana (U+FF66..U+FF9D, one column each) + digits + a little latin.
const char* const kUnicode[] = {
    "ｦ", "ｧ", "ｨ", "ｩ", "ｪ", "ｫ", "ｬ", "ｭ", "ｮ", "ｯ", "ｱ", "ｲ", "ｳ", "ｴ", "ｵ", "ｶ", "ｷ", "ｸ", "ｹ", "ｺ",
    "ｻ", "ｼ", "ｽ", "ｾ", "ｿ", "ﾀ", "ﾁ", "ﾂ", "ﾃ", "ﾄ", "ﾅ", "ﾆ", "ﾇ", "ﾈ", "ﾉ", "ﾊ", "ﾋ", "ﾌ", "ﾍ", "ﾎ",
    "ﾏ", "ﾐ", "ﾑ", "ﾒ", "ﾓ", "ﾔ", "ﾕ", "ﾖ", "ﾗ", "ﾘ", "ﾙ", "ﾚ", "ﾛ", "ﾜ", "ﾝ", "0", "1", "2", "3", "4",
    "5", "6", "7", "8", "9", "Z", ":", ".", "=", "*", "+", "<", ">", "|"};
const char* const kAscii[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "C", "E", "F", "H", "K", "M", "N", "R", "T",
    "W", "X", "Z", "a", "c", "e", "k", "s", "x", "z", ":", ".", "=", "*", "+", "<", ">", "|", "/", "$"};

uint32_t mix(uint32_t seed, uint32_t a, uint32_t b = 0, uint32_t c = 0) {
    uint32_t h = seed ^ 0x9e3779b9u;
    for (uint32_t v : {a, b, c}) {
        h ^= v + 0x7f4a7c15u + (h << 6) + (h >> 2);
        h ^= h >> 16, h *= 0x85ebca6bu, h ^= h >> 13, h *= 0xc2b2ae35u, h ^= h >> 16;
    }
    return h;
}

const char* pick(Glyphs g, uint32_t r) {
    if (g == Glyphs::Unicode) return kUnicode[r % std::size(kUnicode)];
    return kAscii[r % std::size(kAscii)];
}

class RainNode : public ftxui::Node {
public:
    RainNode(uint32_t seed, uint64_t frame, Glyphs g, ftxui::Color c) : seed_(seed), frame_(frame), g_(g) {
        const auto black = ftxui::Color::RGB(0, 0, 0);
        level_[4] = ftxui::Color::Interpolate(0.55f, c, ftxui::Color::RGB(255, 255, 255));
        level_[3] = c;
        level_[2] = ftxui::Color::Interpolate(0.45f, c, black);
        level_[1] = ftxui::Color::Interpolate(0.72f, c, black);
    }

    void ComputeRequirement() override {
        requirement_ = {};
        requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const ftxui::Box b = ftxui::Box::Intersection(box_, screen.stencil);
        const int h = box_.y_max - box_.y_min + 1;
        for (int y = b.y_min; y <= b.y_max; ++y) {
            for (int x = b.x_min; x <= b.x_max; ++x) {
                const RainCell c = cell_at(seed_, frame_, x - box_.x_min, y - box_.y_min, h, g_);
                if (!c.level) continue;
                auto& px = screen.CellAt(x, y);
                px.character = c.glyph;  // <= 3 bytes: small-string, no allocation
                px.foreground_color = level_[c.level];
                px.bold = c.level == 4;
                px.dim = c.level == 1;
            }
        }
    }

private:
    uint32_t seed_;
    uint64_t frame_;
    Glyphs g_;
    ftxui::Color level_[5];
};

}  // namespace

Glyphs detect_glyphs() {
    if (std::getenv("NO_COLOR")) return Glyphs::Off;
    for (const char* var : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const char* v = std::getenv(var);
        if (!v || !*v) continue;
        return (std::strstr(v, "UTF-8") || std::strstr(v, "utf8") || std::strstr(v, "UTF8") ||
                std::strstr(v, "utf-8"))
                   ? Glyphs::Unicode
                   : Glyphs::Ascii;
    }
    return Glyphs::Ascii;
}

// Each column is a drop: head row advances one row every `slow` frames,
// a tail of `len` rows follows, then a gap; then the next drop (new cycle).
// Only ~40% of drops are lit, so the rain stays sparse behind text.
RainCell cell_at(uint32_t seed, uint64_t frame, int x, int y, int h, Glyphs g) {
    if (g == Glyphs::Off || h <= 0 || x < 0 || y < 0 || y >= h) return {};
    const uint32_t r = mix(seed, uint32_t(x));
    const uint64_t slow = 1 + r % 3;
    const uint64_t len = 3 + (r >> 4) % uint32_t(std::max(4, h / 2));
    const uint64_t gap = 2 + (r >> 12) % uint32_t(h + 1);
    const uint64_t period = uint64_t(h) + len + gap;
    const uint64_t t = frame / slow + (r >> 20) % period;
    const uint64_t cycle = t / period, head = t % period;
    if (mix(seed, uint32_t(x), uint32_t(cycle), 1) % 5 >= 2) return {};
    if (head < uint64_t(y)) return {};
    const uint64_t d = head - uint64_t(y);
    if (d >= len) return {};
    RainCell c;
    c.level = d == 0 ? 4 : d * 4 < len ? 3 : d * 2 < len ? 2 : 1;
    // Head flickers every step; the tail keeps its glyph, with a rare swap.
    uint32_t gr = mix(seed, uint32_t(x), uint32_t(y), uint32_t(cycle) * 2 + 7);
    const uint32_t flick = mix(seed, uint32_t(x) ^ 0xabcdu, uint32_t(y), uint32_t(frame / 6));
    if (c.level == 4 || flick % 12 == 0) gr = flick;
    c.glyph = pick(g, gr);
    return c;
}

RainCell MatrixRain::cell(int x, int y) const {
    if (x < 0 || x >= w_) return {};
    return cell_at(seed_, frame_, x, y, h_, glyphs_);
}

ftxui::Element MatrixRain::render(ftxui::Color c) const {
    if (glyphs_ == Glyphs::Off) return ftxui::emptyElement();
    return std::make_shared<RainNode>(seed_, frame_, glyphs_, c);
}

}  // namespace wc::matrix
