// matrix.h - "matrix rain" background layer: columns of falling glyphs with a
// bright head and a fading tail. Deterministic: every cell is a pure function
// of (seed, frame, x, y, height), so frames can be unit-tested and replayed.
//
// Usage (draw it FIRST in a dbox so content paints over it):
//   wc::matrix::MatrixRain rain(seed);        // tick() once per animation frame
//   dbox({rain.render(), content})            // green
//   dbox({rain.render(wc::theme::kAccent), content})   // orange: advanced mode
//
// render() fills whatever box it is given (no min size, safe at 0x0) and only
// writes the cells that hold a glyph, so it never erases what is under it.
// Cost: O(w*h) per frame, no allocations beyond the one Element node.
#pragma once

#include <cstdint>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace wc::matrix {

enum class Glyphs { Unicode, Ascii, Off };

// NO_COLOR set -> Off (render() draws nothing); UTF-8 locale -> Unicode
// (half-width katakana, single-column), else Ascii.
Glyphs detect_glyphs();

inline const ftxui::Color kGreen = ftxui::Color::RGB(0, 255, 70);
// Behind text (loading screen): dark enough that content painted over it stays readable.
inline const ftxui::Color kGreenDim = ftxui::Color::RGB(0, 110, 35);

// level: 0 empty, 1 faint tail, 2 mid tail, 3 bright tail, 4 head.
struct RainCell {
    const char* glyph = nullptr;  // static storage; nullptr when level == 0
    int level = 0;
};

// The pure core. Out-of-range (x<0, y<0, y>=h, h<=0) -> empty cell.
RainCell cell_at(uint32_t seed, uint64_t frame, int x, int y, int h, Glyphs g);

class MatrixRain {
public:
    explicit MatrixRain(uint32_t seed = 0x5eedu, Glyphs g = detect_glyphs()) : seed_(seed), glyphs_(g) {}

    void resize(int w, int h) { w_ = w < 0 ? 0 : w, h_ = h < 0 ? 0 : h; }  // only used by cell()
    void tick() { ++frame_; }
    void set_frame(uint64_t f) { frame_ = f; }

    uint64_t frame() const { return frame_; }
    int width() const { return w_; }
    int height() const { return h_; }
    Glyphs glyphs() const { return glyphs_; }

    // Cell of the last resize() grid; empty when out of bounds.
    RainCell cell(int x, int y) const;

    // Element that rains over its own box (size of the box = size of the rain).
    ftxui::Element render(ftxui::Color c = kGreen) const;

private:
    uint32_t seed_;
    Glyphs glyphs_;
    uint64_t frame_ = 0;
    int w_ = 0, h_ = 0;
};

// Hidden full-screen preview (WC_MATRIX_DEMO=1 ./build/wc), q quits.
// Defined in matrix_demo.cc, linked into the wc binary only.
int demo();

}  // namespace wc::matrix
