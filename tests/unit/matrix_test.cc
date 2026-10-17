// matrix_test.cc - src/ui/matrix: determinism, resize safety, bounds, sparsity.
// Run: build/wc_matrix_test   (exit 1 if any check fails)
// WC_MATRIX_DUMP=1 also prints a 60x16 frame for eyeballing.
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
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

static long lit_cells(MatrixRain& r, int w, int h, std::set<int>* cols = nullptr) {
    long lit = 0;
    for (uint64_t f = 0; f < 200; f += 10) {
        r.set_frame(f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (r.cell(x, y).level) {
                    ++lit;
                    if (cols) cols->insert(x);
                }
    }
    return lit;
}

static void test_intensity() {
    const int w = 120, h = 40;
    // Default is 1.0, and 1.0 is exactly today's rain (cell for cell).
    MatrixRain def(2026, Glyphs::Unicode), one(2026, Glyphs::Unicode);
    CHECK(def.intensity() == 1.0f, std::to_string(def.intensity()));
    one.set_intensity(1.0f);
    def.resize(w, h), one.resize(w, h);
    bool same = true;
    for (uint64_t f : {0ull, 13ull, 250ull, 9999ull}) {
        def.set_frame(f), one.set_frame(f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                RainCell a = def.cell(x, y), b = one.cell(x, y);
                RainCell c = cell_at(2026, f, x, y, h, Glyphs::Unicode);  // old signature
                same &= a.level == b.level && a.glyph == b.glyph && a.level == c.level && a.glyph == c.glyph;
            }
    }
    CHECK(same, "intensity 1 must equal the default rain");
    one.set_intensity(-2.0f);
    CHECK(one.intensity() == 0.0f, "clamp low");
    one.set_intensity(5.0f);
    CHECK(one.intensity() == 1.0f, "clamp high");

    // 0 -> sparse: a few columns, far fewer lit cells; columns only join as it rises.
    std::set<int> cols0, cols1;
    MatrixRain r(2026, Glyphs::Unicode);
    r.resize(w, h);
    r.set_intensity(0.0f);
    long lit0 = lit_cells(r, w, h, &cols0);
    r.set_intensity(1.0f);
    long lit1 = lit_cells(r, w, h, &cols1);
    CHECK(lit0 > 0, "intensity 0 still shows something");
    CHECK(cols0.size() >= 1 && cols0.size() <= 16, std::to_string(cols0.size()) + " columns at 0");
    CHECK(lit0 * 8 < lit1, std::to_string(lit0) + " vs " + std::to_string(lit1));
    long prev = 0;
    bool grows = true;
    for (float i : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        r.set_intensity(i);
        std::set<int> c;
        lit_cells(r, w, h, &c);
        grows &= long(c.size()) >= prev;
        prev = long(c.size());
    }
    CHECK(grows, "active columns never shrink as intensity rises");
    // Glyph variety: fewer distinct glyphs at 0.
    auto variety = [&](float i) {
        std::set<std::string> gl;
        r.set_intensity(i);
        for (uint64_t f = 0; f < 400; f += 5) {
            r.set_frame(f);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (const char* g = r.cell(x, y).glyph) gl.insert(g);
        }
        return gl.size();
    };
    size_t v0 = variety(0.0f), v1 = variety(1.0f);
    CHECK(v0 <= 16 && v1 > 3 * v0, std::to_string(v0) + " vs " + std::to_string(v1));
    // Deterministic at any intensity: pure function of (seed, frame, intensity).
    for (float i : {0.0f, 0.37f, 0.8f}) {
        MatrixRain a(7, Glyphs::Ascii), b(7, Glyphs::Ascii);
        a.set_intensity(i), b.set_intensity(i);
        a.set_frame(321), b.set_frame(321);
        CHECK(render(a.render(), 60, 20) == render(b.render(), 60, 20), "deterministic at " + std::to_string(i));
    }
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
    test_intensity();
    test_off_and_detect();
    if (std::getenv("WC_MATRIX_DUMP")) std::cout << rain_at(1, 300, 60, 16, Glyphs::Unicode) << "\n";
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << " matrix checks\n";
    return failures ? 1 : 0;
}
