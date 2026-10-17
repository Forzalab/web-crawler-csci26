// matrix_test.cc - src/ui/matrix: determinism, resize safety, bounds, sparsity.
// Run: build/wc_matrix_test   (exit 1 if any check fails)
// WC_MATRIX_DUMP=1 also prints a 60x16 frame for eyeballing.
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>

#include "ui/matrix.h"
#include "ui/theme.h"

using namespace wc::matrix;
using namespace ftxui;

static int failures = 0, checks = 0;

#define CHECK(cond, ctx)                                                                \
    do {                                                                                \
        ++checks;                                                                       \
        if (!(cond)) {                                                                  \
            ++failures;                                                                 \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"      \
                      << (ctx) << "\n";                                                 \
        }                                                                               \
    } while (0)

static std::string strip(const std::string& raw) {  // drop SGR escapes: text only
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\x1b' && i + 1 < raw.size() && raw[i + 1] == '[') {
            i += 2;
            while (i < raw.size() && !(raw[i] >= '@' && raw[i] <= '~')) ++i;
        } else {
            out += raw[i];
        }
    }
    return out;
}

static std::string render(Element e, int w, int h) {
    auto s = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(s, e);
    return strip(s.ToString());
}

static std::string rain_at(uint32_t seed, uint64_t frame, int w, int h, Glyphs g = Glyphs::Ascii) {
    MatrixRain r(seed, g);
    r.set_frame(frame);
    return render(r.render(), w, h);
}

static void test_determinism() {
    std::string a = rain_at(42, 100, 50, 20), b = rain_at(42, 100, 50, 20);
    CHECK(a == b, "same seed + frame must render identically");
    CHECK(a != rain_at(43, 100, 50, 20), "different seed should differ");
    CHECK(a != rain_at(42, 101, 50, 20) || a != rain_at(42, 103, 50, 20), "frames should move");

    MatrixRain r1(7, Glyphs::Unicode), r2(7, Glyphs::Unicode);
    r1.resize(80, 24), r2.resize(80, 24);
    for (int i = 0; i < 37; ++i) r1.tick();
    r2.set_frame(37);
    bool same = true;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 80; ++x) {
            RainCell c1 = r1.cell(x, y), c2 = r2.cell(x, y);
            same &= c1.level == c2.level && (c1.glyph == c2.glyph);
        }
    CHECK(same, "tick() x37 == set_frame(37)");
    CHECK(r1.frame() == 37, std::to_string(r1.frame()));
}

static void test_cells_match_render() {  // the Node draws exactly cell_at()
    MatrixRain r(99, Glyphs::Ascii);
    r.resize(40, 12);
    r.set_frame(250);
    auto s = Screen::Create(Dimension::Fixed(40), Dimension::Fixed(12));
    Render(s, r.render());
    int bad = 0;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 40; ++x) {
            RainCell c = r.cell(x, y);
            const std::string& got = s.CellAt(x, y).character;
            if (c.level ? got != c.glyph : (got != " " && !got.empty())) ++bad;
        }
    CHECK(bad == 0, std::to_string(bad) + " cells differ");
}

static void test_resize_safety() {
    const int sizes[][2] = {{0, 0}, {1, 1}, {50, 40}, {200, 60}, {3, 0}, {0, 3}};
    for (Glyphs g : {Glyphs::Unicode, Glyphs::Ascii}) {
        MatrixRain r(1234, g);
        for (auto& wh : sizes) {
            int w = wh[0], h = wh[1];
            r.resize(w, h);
            CHECK(r.width() == w && r.height() == h, "resize");
            bool ok = true;
            for (uint64_t f : {0ull, 1ull, 77ull, 1000003ull, 1ull << 40}) {
                r.set_frame(f);
                for (int y = -2; y < h + 2; ++y)
                    for (int x = -2; x < w + 2; ++x) {
                        RainCell c = r.cell(x, y);
                        bool in = x >= 0 && y >= 0 && x < w && y < h;
                        ok &= c.level >= 0 && c.level <= 4 && (c.level == 0) == (c.glyph == nullptr);
                        ok &= in || c.level == 0;
                        if (c.glyph) ok &= string_width(c.glyph) == 1;  // single-column glyphs
                        if (c.glyph && g == Glyphs::Ascii) ok &= std::strlen(c.glyph) == 1 && c.glyph[0] > ' ' && c.glyph[0] < 127;
                    }
                if (w > 0 && h > 0) render(r.render(wc::theme::kAccent), w, h);  // must not crash
            }
            CHECK(ok, "bad cell at " + std::to_string(w) + "x" + std::to_string(h));
        }
        r.resize(-5, -5);
        CHECK(r.width() == 0 && r.height() == 0, "negative resize clamps to 0");
    }
}

static void test_no_out_of_bounds() {  // rain in a 5x3 box: nothing outside it is touched
    for (uint64_t f = 0; f < 120; f += 7) {
        MatrixRain r(5, Glyphs::Ascii);
        r.set_frame(f);
        auto e = vbox({text(""), hbox({text("  "), r.render() | size(WIDTH, EQUAL, 5) | size(HEIGHT, EQUAL, 3)})});
        auto s = Screen::Create(Dimension::Fixed(20), Dimension::Fixed(8));
        Render(s, e);
        bool ok = true;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 20; ++x) {
                bool in = x >= 2 && x < 7 && y >= 1 && y < 4;
                const std::string& ch = s.CellAt(x, y).character;
                if (!in) ok &= ch == " " || ch.empty();
            }
        CHECK(ok, strip(s.ToString()));
    }
    // Behind text: content drawn after the rain wins.
    MatrixRain r(5, Glyphs::Ascii);
    std::string out = render(dbox({r.render(), text("HELLO")}), 30, 5);
    CHECK(out.find("HELLO") != std::string::npos, out);
}

static void test_sparse_and_alive() {
    MatrixRain r(2026, Glyphs::Unicode);
    r.resize(120, 40);
    long lit = 0, heads = 0, total = 0;
    for (uint64_t f = 0; f < 200; f += 10) {
        r.set_frame(f);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 120; ++x) {
                RainCell c = r.cell(x, y);
                lit += c.level > 0, heads += c.level == 4, ++total;
            }
    }
    double frac = double(lit) / double(total);
    CHECK(frac > 0.03 && frac < 0.40, "lit fraction " + std::to_string(frac));
    CHECK(heads > 0, "no heads");
}

static void test_off_and_detect() {
    MatrixRain off(1, Glyphs::Off);
    off.resize(30, 10);
    std::string out = render(off.render(), 30, 10);
    bool blank = out.find_first_not_of(" \r\n") == std::string::npos;
    CHECK(blank, out);
    CHECK(off.cell(3, 3).level == 0, "Off has no cells");

    const char* saved = std::getenv("NO_COLOR");
    std::string keep = saved ? saved : "";
    setenv("NO_COLOR", "1", 1);
    CHECK(detect_glyphs() == Glyphs::Off, "NO_COLOR -> Off");
    unsetenv("NO_COLOR");
    setenv("LC_ALL", "en_US.UTF-8", 1);
    CHECK(detect_glyphs() == Glyphs::Unicode, "UTF-8 -> Unicode");
    setenv("LC_ALL", "C", 1);
    CHECK(detect_glyphs() == Glyphs::Ascii, "C -> Ascii");
    unsetenv("LC_ALL");
    if (saved) setenv("NO_COLOR", keep.c_str(), 1);
}

int main() {
    test_determinism();
    test_cells_match_render();
    test_resize_safety();
    test_no_out_of_bounds();
    test_sparse_and_alive();
    test_off_and_detect();
    if (std::getenv("WC_MATRIX_DUMP")) std::cout << rain_at(1, 300, 60, 16, Glyphs::Unicode) << "\n";
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << " matrix checks\n";
    return failures ? 1 : 0;
}
