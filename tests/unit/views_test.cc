// views_test.cc - headless renders of src/ui/views (state in, Element out).
// Run: NO_COLOR=1 build/wc_views_test   (exit 1 if any check fails)
// WC_VIEWS_DUMP=1 also prints a 100x30 main screen for eyeballing.
// WC_VIEWS_BENCH=1 also times graph frames on a ~1000-node / 11k-edge graph (no asserts).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "ui/camera.h"
#include "ui/matrix.h"
#include "ui/theme.h"
#include "ui/views.h"

using namespace wc;
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

#define CHECK_HAS(out, sub) CHECK((out).find(sub) != std::string::npos, out)
#define CHECK_LACKS(out, sub) CHECK((out).find(sub) == std::string::npos, out)

static std::string render(Element e, int w = 80, int h = 24) {
    auto s = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(s, e);
    std::string raw = s.ToString(), out;  // drop SGR escapes: assert on text only
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

struct Fake {
    CrawlGraph g;
    Layout l;
    ReplayFrame f;
};

// The view takes a shared Scene: the fake's graph + hand-placed layout.
static std::shared_ptr<const Scene> scene(const Fake& k) { return std::make_shared<const Scene>(make_scene(k.g, k.l)); }

// root -> {google, github}; google -> maps (depth 2) -> deep (3); github -> google (SEEN)
static Fake fake_graph() {
    Fake k;
    k.g.add_node("https://www.example.org/", 0);
    k.g.add_node("https://www.google.com/maps?x", 1);
    k.g.add_node("https://github.com:443/a", 1);
    k.g.add_node("https://maps.test/b", 2);
    k.g.add_node("https://deep.test/c", 3);
    k.g.add_edge(k.g.nodes[0].url, k.g.nodes[1].url, false);
    k.g.add_edge(k.g.nodes[0].url, k.g.nodes[2].url, false);
    k.g.add_edge(k.g.nodes[1].url, k.g.nodes[3].url, false);
    k.g.add_edge(k.g.nodes[3].url, k.g.nodes[4].url, false);
    k.g.add_edge(k.g.nodes[2].url, k.g.nodes[1].url, true);
    k.g.record_fetch(k.g.nodes[0].url, true);
    k.g.record_fetch(k.g.nodes[2].url, false);
    k.l.pos = {{0, 0}, {-1, 0}, {1, 0}, {-1.6, 1}, {-1.2, 1.8}};
    k.l.parent = {-1, 0, 0, 1, 3};
    k.f.node_t = {1, 1, 1, 1, 1};
    k.f.edge_t = {1, 1, 1, 1, 1};
    k.f.depth = 3;
    k.f.size = 5;
    k.f.done = true;
    return k;
}

static void test_theme() {
    CHECK(std::string(theme::glyph(0, Status::Fetched)) == "◆", "");
    CHECK(std::string(theme::glyph(1, Status::Unfetched)) == "●", "");  // shape = site rank
    CHECK(std::string(theme::glyph(5, Status::Fetched)) == "✚", "");
    CHECK(std::string(theme::glyph(6, Status::Fetched)) == "·", "");
    CHECK(std::string(theme::glyph(-1, Status::Fetched)) == "·", "");
    CHECK(std::string(theme::glyph(0, Status::Failed)) == "✕", "");
    CHECK(theme::depth_color(1) == theme::kAccent, "");
    CHECK(theme::depth_color(0) == theme::kText, "");
}

// True if label text touches an edge dot or a glyph: a run of 2+ label chars right next
// to a braille cell or a ◆ (the smoke stick-grep, in C++).
static bool sticks(const std::string& out) {
    std::vector<char32_t> cps;
    for (size_t i = 0; i < out.size();) {
        unsigned char b = out[i];
        int n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? b : b & (0x7F >> n);
        for (int k = 1; k < n && i + k < out.size(); ++k) cp = (cp << 6) | (out[i + k] & 0x3F);
        cps.push_back(cp);
        i += n;
    }
    auto word = [](char32_t c) { return (c < 128 && std::isalnum(int(c))) || c == '/' || c == '.' || c == '_' || c == '-'; };
    auto dot = [](char32_t c) { return (c >= 0x2800 && c <= 0x28FF) || c == 0x25C6; };
    for (size_t i = 0; i + 2 < cps.size(); ++i) {
        if (dot(cps[i]) && word(cps[i + 1]) && word(cps[i + 2])) return true;
        if (word(cps[i]) && word(cps[i + 1]) && dot(cps[i + 2])) return true;
    }
    return false;
}

static void test_graph() {
    Fake k = fake_graph();
    View v{30, 80, 30};  // canvas 160x96 sub-pixels at 80x24
    std::string out = render(views::graph(scene(k), k.f, v));
    CHECK_HAS(out, "google.com");
    CHECK_HAS(out, "example.org");
    CHECK_LACKS(out, "maps.test");  // depth 2: glyph only
    CHECK_HAS(out, "✕");            // github fetch failed
    CHECK_LACKS(out, "deep.test");
    CHECK_HAS(out, "·");
    CHECK_LACKS(out, "www.");
    CHECK(!sticks(out), out);

    k.f.node_t = {1, 0, 0, 0, 0};  // only root shown, edges not started
    k.f.edge_t = {0, 0, 0, 0, 0};
    out = render(views::graph(scene(k), k.f, v));
    CHECK_HAS(out, "example.org");
    CHECK_LACKS(out, "google.com");
}

static std::string line_of(const std::string& out, int row) {
    size_t b = 0;
    for (int i = 0; i < row && b != std::string::npos; ++i) b = out.find('\n', b) + 1;
    return b == std::string::npos ? "" : out.substr(b, out.find('\n', b) - b);
}

// Same-site crawl: path labels, root centred under its glyph, overlaps skipped.
static void test_graph_same_site() {
    Fake k;
    k.g.add_node("https://www.iana.org/", 0);
    k.g.add_node("https://www.iana.org/domains", 1);
    k.g.add_node("https://www.iana.org/about/", 1);  // same spot as /domains
    k.g.add_node("https://www.icann.org/x", 1);
    for (int i = 1; i < 4; ++i) k.g.add_edge(k.g.nodes[0].url, k.g.nodes[i].url, false);
    k.l.pos = {{0, 0}, {1, -1}, {1, -1}, {-1.5, -1}};
    k.l.parent = {-1, 0, 0, 0};
    k.l.shape = {0, 0, 0, 1};
    k.l.sites = {"iana.org", "icann.org"};
    k.f.node_t = {1, 1, 1, 1};
    k.f.edge_t = {1, 1, 1};
    k.f.done = true;
    std::string out = render(views::graph(scene(k), k.f, View{20, 80, 48}));
    CHECK_HAS(out, "/domains");
    CHECK_LACKS(out, "/about");     // collides with /domains, which came first
    CHECK_HAS(out, "icann.org");    // other host: host label
    // root glyph at col 40 row 12; "iana.org" (8 cols) centred under it: cols 36..43
    std::string under = line_of(out, 13);
    CHECK(under.find("iana.org") != std::string::npos, out);
    CHECK(line_of(out, 12).find("◆") != std::string::npos, out);
    CHECK_HAS(out, "●");                   // icann.org: second site, second shape
    CHECK_HAS(line_of(out, 23), "· other");  // legend on the bottom row
    size_t p = under.find("iana.org"), cols = 0;
    for (size_t i = 0; i < p; ++i) cols += (static_cast<unsigned char>(under[i]) & 0xC0) != 0x80;
    CHECK(cols == 36, out);
    CHECK(!sticks(out), out);
}

// A label is padded with a blank cell on each side: an edge running under it never
// touches the text, it stops one cell short.
static void test_label_pad() {
    Fake k;
    k.g.add_node("https://www.iana.org/", 0);
    k.g.add_node("https://www.iana.org/domains", 1);
    k.g.add_node("https://far.test/x", 2);
    k.g.add_edge(k.g.nodes[0].url, k.g.nodes[1].url, false);
    k.g.add_edge(k.g.nodes[0].url, k.g.nodes[2].url, false);
    k.g.add_edge(k.g.nodes[2].url, k.g.nodes[0].url, true);
    k.l.pos = {{-3, 0}, {-1, 0}, {3, 0}};  // root -> far runs straight through /domains' row
    k.l.parent = {-1, 0, 0};
    k.f.node_t = {1, 1, 1};
    k.f.edge_t = {1, 1, 1};
    k.f.done = true;
    std::string out = render(views::graph(scene(k), k.f, View{10, 80, 48}));
    std::string row = line_of(out, 12);
    CHECK(row.find(" /domains ") != std::string::npos, out);
    CHECK(row.find("· /domains ⠁") != std::string::npos, out);  // glyph, pad, label, pad, then the edge
    CHECK(!sticks(out), out);
}

// Non-root labels keep rows +-1 clear of other labels; the root's label is exempt.
static void test_label_halo() {
    Fake k;
    k.g.add_node("https://r.test/", 0);
    k.g.add_node("https://r.test/aaa", 1);
    k.g.add_node("https://r.test/bbb", 1);  // one row under /aaa, same column: no label
    k.g.add_node("https://r.test/ccc", 1);  // two rows under /aaa: label kept
    for (int i = 1; i < 4; ++i) k.g.add_edge(k.g.nodes[0].url, k.g.nodes[i].url, false);
    k.l.pos = {{0, 0}, {2, -0.4}, {2, 0}, {2, 0.4}};  // scale 10: rows 11, 12, 13 -> /bbb sits between
    k.l.parent = {-1, 0, 0, 0};
    k.f.node_t = {1, 1, 1, 1};
    k.f.edge_t = {1, 1, 1};
    k.f.done = true;
    std::string out = render(views::graph(scene(k), k.f, View{10, 40, 48}));
    CHECK_HAS(out, "/aaa");
    CHECK_LACKS(out, "/bbb");
    CHECK_HAS(out, "/ccc");
    CHECK(!sticks(out), out);
    // Root label right under the root glyph, a depth-1 label one row below it: both shown.
    Fake r;
    r.g.add_node("https://r.test/", 0);
    r.g.add_node("https://r.test/ddd", 1);
    r.g.add_edge(r.g.nodes[0].url, r.g.nodes[1].url, false);
    r.l.pos = {{0, 0}, {-0.6, 0.8}};  // root glyph row 12, its label row 13, /ddd row 14
    r.l.parent = {-1, 0};
    r.f.node_t = {1, 1};
    r.f.edge_t = {1};
    r.f.done = true;
    out = render(views::graph(scene(r), r.f, View{10, 80, 48}));
    CHECK_HAS(line_of(out, 13), "r.test");
    CHECK_HAS(line_of(out, 14), "/ddd");
}

// Cross-link (seen) edges: drawn up to 40 nodes, skipped beyond.
static void test_seen_edges_cap() {
    auto build = [](int extra) {
        Fake k;
        k.g.add_node("https://r.test/", 0);
        k.g.add_node("https://a.test/", 2);  // depth 2: no label in the way
        k.g.add_node("https://b.test/", 2);
        k.g.add_edge(k.g.nodes[0].url, k.g.nodes[1].url, false);
        k.g.add_edge(k.g.nodes[0].url, k.g.nodes[2].url, false);
        k.g.add_edge(k.g.nodes[1].url, k.g.nodes[2].url, true);
        k.l.pos = {{0, 0}, {-2, 2}, {2, 2}};
        k.l.parent = {-1, 0, 0};
        for (int i = 0; i < extra; ++i) {
            k.g.add_node("https://x" + std::to_string(i) + ".test/", 2);
            k.l.pos.push_back({0, 0});
            k.l.parent.push_back(0);
        }
        k.f.node_t.assign(k.g.nodes.size(), 0);
        k.f.node_t[0] = k.f.node_t[1] = k.f.node_t[2] = 1;
        k.f.edge_t = {1, 1, 1};
        k.f.done = true;
        return k;
    };
    auto mid = [](Fake& k) {  // row 10 (y=2), cols 32..48: only the seen edge passes there
        std::string row = line_of(render(views::graph(scene(k), k.f, View{10, 80, 20})), 10);
        size_t b = 0, col = 0;
        while (b < row.size() && col < 32) col += (static_cast<unsigned char>(row[b++]) & 0xC0) != 0x80;
        size_t e = b;
        while (e < row.size() && col < 48) col += (static_cast<unsigned char>(row[e++]) & 0xC0) != 0x80;
        return row.substr(b, e - b);
    };
    Fake small = build(0), big = build(38);  // 3 and 41 nodes
    std::string s = mid(small), l = mid(big);
    CHECK(s.find_first_not_of(' ') != std::string::npos, s);
    CHECK(l.find_first_not_of(' ') != std::string::npos, l);  // cross-links always on
}

static void test_boss_bar() {
    std::string out = render(views::boss_bar(2, 3, 40, 200, false, false), 80, 2);
    CHECK_HAS(out, "DEPTH");
    CHECK_HAS(out, "2/3");
    CHECK_HAS(out, "LINKS 40/200");
    CHECK_LACKS(out, "PAGES");
    CHECK_HAS(out, "▰");
    CHECK_HAS(out, "─");  // rule under the bar
    CHECK(out.find("─") >= out.find("\n"), out);  // ...exactly one: row 2 only
    CHECK_LACKS(out, "LIMIT");
    out = render(views::boss_bar(3, 3, 200, 200, true, false), 80, 2);
    CHECK_HAS(out, "LIMIT");
    CHECK_HAS(out, "3/3");
}

static void test_omnibar() {
    std::string out =
        render(views::omnibar(text("goo"), {"google.com", "github.com"}, 0, "gle.com"), 80, 5);
    CHECK_HAS(out, "› google.com");
    CHECK_HAS(out, "╭");  // rounded border round the input line  // input + ghost on one line
    CHECK_HAS(out, "▌ google.com");  // selected
    // drop up: results above the input, results[0] nearest it
    CHECK(out.find("github.com") < out.find("▌"), out);
    CHECK(out.find("▌") < out.find("›"), out);
    // centred, half width at 80 cols: border spans cols 20..59
    size_t top = out.find("╭");
    size_t bol = out.rfind('\n', top) + 1;  // npos+1 == 0 on the first line
    CHECK(top - bol == 20, out);
    CHECK(out.substr(bol, 20).find_first_not_of(' ') == std::string::npos, out);
    out = render(views::omnibar(text(""), {}, -1, "x"), 200, 3);  // capped at 60 on wide screens
    size_t a = out.find("╭"), b = out.find("╮");
    CHECK(a != std::string::npos && b != std::string::npos, out);
    CHECK((b - a - 3) / 3 + 2 == 60, out);  // UTF-8 ─ is 3 bytes
}

// Omnibar = white text on a black background, with a border. Colours are checked on the
// Screen cells; the NO_COLOR render (plain text) must still show border + text.
static void test_omnibar_placeholder() {
    std::string out = render(views::omnibar(text(""), {}, -1, "Type to find a page.\nExample: example.com"), 80, 4);
    CHECK_HAS(out, "› Type to find a page.");
    CHECK_HAS(out, "Example: example.com");
    CHECK(out.find("Type to find") < out.find("Example"), out);
    CHECK(out.find("Example") < out.find("╰"), out);  // inside the box
}

static void test_omnibar_colors() {
    auto bar = views::omnibar(text("goo"), {}, -1, "");
    Screen s = Screen::Create(Dimension::Fixed(80), Dimension::Fixed(3));
    Render(s, bar);
    int x = 0;  // first cell of "goo" on the input row (middle row)
    for (; x < 80 && s.PixelAt(x, 1).character != "g"; ++x) {}
    CHECK(x < 80, s.ToString());
    CHECK(s.PixelAt(x, 1).background_color == Color::Black, s.ToString());
    CHECK(s.PixelAt(x, 1).foreground_color == Color::White, s.ToString());
    CHECK(s.PixelAt(x, 0).background_color == Color::Black, s.ToString());  // border row too
    std::string out = render(views::omnibar(text("goo"), {}, -1, ""), 80, 3);
    CHECK_HAS(out, "│› goo");
    CHECK_HAS(out, "╰");
    // any other input gets the same shared style (URL bar / prompts)
    std::string url = render(views::input_box(text("example.com")), 30, 3);
    CHECK_HAS(url, "╭");
    CHECK_HAS(url, "│example.com");
    CHECK_HAS(url, "╰");
    Screen s2 = Screen::Create(Dimension::Fixed(30), Dimension::Fixed(3));
    Render(s2, views::input_box(text("50")));
    CHECK(s2.PixelAt(1, 1).background_color == Color::Black, s2.ToString());
    CHECK(s2.PixelAt(1, 1).foreground_color == Color::White, s2.ToString());
}

// The REAL Input (focused, as in the TUI) with the shared transform: white on black, never
// inverted, placeholder or typed. FTXUI's default transform inverts it (black on white).
static void test_input_colors() {
    InputOption opt;
    opt.transform = [](InputState st) { return views::input_text(std::move(st.element), st.is_placeholder); };
    for (std::string content : {std::string(), std::string("12")}) {
        opt.placeholder = "Paste a URL.";
        Component in = Input(&content, opt);
        Screen s = Screen::Create(Dimension::Fixed(40), Dimension::Fixed(3));
        Render(s, views::field("Max depth [3]", in->Render(), 14));
        std::string out = s.ToString();
        int x0 = 0;  // box spans from the first border cell to the last
        while (x0 < 40 && s.PixelAt(x0, 1).character != "│") ++x0;
        CHECK(x0 < 40, out);
        for (int y = 0; y < 3; ++y)
            for (int x = x0; x < 40 && x < x0 + 18; ++x) {
                const auto& p = s.PixelAt(x, y);
                CHECK(!p.inverted && p.background_color == Color::Black, out);
            }
        int x = x0;
        while (x < 40 && s.PixelAt(x, 1).character != (content.empty() ? "P" : "1")) ++x;
        CHECK(x < 40 && s.PixelAt(x, 1).foreground_color == Color::White, out);
    }
}

// Label on the box's text row (not on its border), "› " in every field, one block.
static void test_field_align() {
    std::string out = render(views::field("Max depth [3]", text("5"), 8), 30, 3);
    CHECK(line_of(out, 0).find("Max") == std::string::npos, out);
    CHECK(line_of(out, 1).find("Max depth [3] │› 5") != std::string::npos, out);
    CHECK(line_of(out, 0).find("╭") == line_of(out, 1).find("│"), out);
    out = render(views::field("", text("example.com"), 20), 30, 3);
    CHECK(line_of(out, 1).rfind("│› example.com", 0) == 0, out);
    // Omnibar: the ghost trails the real (xflex) Input, no gap in between.
    std::string empty;
    Component in = Input(&empty, InputOption{});
    out = render(views::omnibar(in->Render(), {}, -1, "Type to find a page."), 80, 3);
    CHECK_HAS(out, "│› Type to find a page.");
}

static void test_toast_too_small() {
    std::string out = render(views::toast("Copied 12 URLs"), 20, 3);  // exactly its width
    CHECK_HAS(out, "│ ✓ Copied 12 URLs │");
    CHECK_HAS(out, "╭");
    out = render(views::too_small(60, 20));
    CHECK_HAS(out, "Enlarge terminal to 80×24 (now 60×20)");
}

static std::vector<std::string> page_lines() {
    return {"# Google Maps", "Find local businesses, view maps and get directions.",
            "- Directions", "- Satellite", "[IMG logo.png]", "Footer text"};
}

static void test_preview() {
    std::string out = render(views::preview("maps.google.com", page_lines(), 0, 1.0f, false), 80, 16);
    CHECK(line_of(out, 0).find("PREVIEW · maps.google.com") != std::string::npos, out);
    CHECK(line_of(out, 0).find("╭") != std::string::npos, out);
    CHECK_HAS(out, "Google Maps");
    CHECK_LACKS(out, "# Google");
    CHECK_HAS(out, "- Directions");
    CHECK_HAS(out, "[IMG");
    CHECK_HAS(out, "Esc close");
    std::string top = line_of(out, 0);
    size_t a = top.find("╭"), b = top.find("╮");
    auto cols = [](const std::string& s) {
        size_t n = 0;
        for (unsigned char ch : s) n += (ch & 0xC0) != 0x80;
        return n;
    };
    CHECK(b != std::string::npos && cols(top.substr(a, b - a)) + 1 <= 60, out);
    out = render(views::preview("maps.google.com", page_lines(), 1, 0.0f, false), 80, 16);
    CHECK_LACKS(out, "Google Maps");
    CHECK_HAS(out, "Find local");
    out = render(views::preview("maps.google.com", page_lines(), 0, 0.5f, true), 100, 16);
    top = line_of(out, 0);
    a = top.find("╭"), b = top.find("╮");
    CHECK(b != std::string::npos && cols(top.substr(a, b - a)) + 1 > 60, out);
}

static void test_coach() {
    std::string out = render(views::coach(1, 3, "Type a URL to start"), 40, 4);
    CHECK_HAS(out, "1/3  Type a URL to start");
    CHECK_HAS(out, "Tab next · Esc skip");
    CHECK_HAS(out, "╭");
}

// Foreground colour of the cell where `needle` starts (first row that holds it).
static Color fg_of(Element e, const std::string& needle, int w = 80, int h = 24) {
    auto s = Screen::Create(Dimension::Fixed(w), Dimension::Fixed(h));
    Render(s, e);
    for (int y = 0; y < h; ++y) {
        std::string row;
        std::vector<int> col_of;  // byte -> x
        for (int x = 0; x < w; ++x) {
            for (size_t k = 0; k < s.CellAt(x, y).character.size(); ++k) col_of.push_back(x);
            row += s.CellAt(x, y).character;
        }
        if (size_t at = row.find(needle); at != std::string::npos) return s.CellAt(col_of[at], y).foreground_color;
    }
    return Color::Default;
}

static void test_loading_fade(views::LoadingState st) {
    st.progress.recent = {{"https://a.org/old", 0, true, 1000}, {"https://b.org/mid", 1, true, 2000},
                          {"https://c.org/new", 1, true, 3000}};
    const Color kMid = Color::RGB(180, 180, 180);  // the two lines above the newest
    auto at = [&](int64_t now, const char* who, int rows = 0) {
        views::LoadingState s = st;
        s.fade = true, s.now_ms = now, s.rows = rows;
        return fg_of(views::loading(s, emptyElement()), who);
    };
    // Newest line: faint when it lands, full white after the 400 ms fade, in between mid-way.
    CHECK(at(3000, "c.org/new") == theme::kFaint, "fade-in starts faint");
    CHECK(at(3400, "c.org/new") == theme::kText, "fade-in ends at full colour");
    CHECK(at(3200, "c.org/new") != theme::kFaint && at(3200, "c.org/new") != theme::kText, "mid fade");
    // List not full: the oldest line keeps its grey; full (rows == 3): it fades out to faint.
    CHECK(at(3400, "a.org/old") == kMid, "not full: oldest keeps its grey");
    CHECK(at(3400, "a.org/old", 3) == theme::kFaint, "full: oldest faded out before it scrolls");
    CHECK(at(3000, "a.org/old", 3) == kMid, "fade-out starts when the newest lands");
    // fade off (--no-anim / NO_COLOR): final colours at once, whatever the clock says.
    st.fade = false;
    CHECK(fg_of(views::loading(st, emptyElement()), "c.org/new") == theme::kText, "no-anim: static");
    st.rows = 3;
    CHECK(fg_of(views::loading(st, emptyElement()), "a.org/old") == kMid, "no-anim: no fade-out");
}

static void test_loading() {
    views::LoadingState st;
    st.host = "iana.org";
    st.line = "untangling the web…";
    st.tick = 3;
    st.pulse = 0.5f;
    st.progress.fetched = 3, st.progress.ok = 2, st.progress.failed = 1, st.progress.found = 40, st.progress.depth = 1;
    st.progress.recent = {{"https://www.iana.org/", 0, true}, {"https://www.iana.org/domains", 1, true},
                          {"https://ext.com/very/long/path/that/goes/on/and/on/and/on/forever/more", 1, false}};
    std::string out = render(views::loading(st, emptyElement()), 80, 24);
    CHECK_HAS(out, "⠸ untangling the web…");
    CHECK_HAS(out, "crawling iana.org");
    CHECK_HAS(out, "fetched 3");
    CHECK_HAS(out, "links 40");
    CHECK_LACKS(out, "pages");
    CHECK_HAS(out, "fail 1");
    CHECK_HAS(out, "depth 1");
    CHECK_HAS(out, "✓ iana.org/");
    CHECK_HAS(out, "✓ iana.org/domains");
    CHECK_HAS(out, "✗ ext.com/very/long");
    CHECK_HAS(out, "Esc stop · Ctrl-C quit");
    CHECK_HAS(out, "╭");
    // Newest at the bottom, and the long line is clipped to the box (<= 64 inside the border).
    CHECK(out.find("✓ iana.org/domains") < out.find("✗ ext.com"), out);
    CHECK_LACKS(out, "more/and");
    // Phone width: nothing wraps, every row still there.
    st.cols = 50;
    out = render(views::loading(st, emptyElement()), 50, 20);
    CHECK_HAS(out, "untangling the web…");
    CHECK_HAS(out, "✗ ext.com");
    CHECK_HAS(out, "Esc stop · Ctrl-C quit");
    size_t tl = out.find("╭");
    CHECK(tl != std::string::npos && out.find("╮", tl) < out.find('\n', tl), out);  // right edge on screen
    st.cols = 80;
    // The background layer sits behind the content.
    out = render(views::loading(st, text("BACKDROP") | size(WIDTH, EQUAL, 80)), 80, 24);
    CHECK_HAS(out, "BACKDROP");
    // Matrix rain behind it (glyphs forced: this test runs under NO_COLOR): the rain
    // shows around the box, but the box is opaque, so every text row reads clean.
    for (auto [w, h] : {std::pair{120, 40}, std::pair{50, 40}}) {
        st.cols = w;
        matrix::MatrixRain rain(0x5eedu, matrix::Glyphs::Ascii);
        rain.set_frame(500);
        out = render(views::loading(st, rain.render(matrix::kGreenDim) | flex), w, h);
        CHECK_HAS(out, "│ ⠸ untangling the web…");
        CHECK_HAS(out, "│   crawling iana.org ");
        CHECK_HAS(out, "│ fetched 3  links 40  fail 1  depth 1 ");
        CHECK_HAS(out, "│ ✓ iana.org/domains ");
        CHECK_HAS(out, "Esc stop · Ctrl-C quit");
        size_t top = out.find("╭");
        CHECK(top != std::string::npos && out.substr(0, top).find_first_not_of(" \n") != std::string::npos, out);
        // Every box row (border to border) is byte-identical to the render with no background.
        std::string plain = render(views::loading(st, emptyElement()), w, h);
        auto box_rows = [](const std::string& s) {
            std::vector<std::string> rows;
            size_t at = 0;
            while (at < s.size()) {
                size_t nl = s.find('\n', at);
                if (nl == std::string::npos) nl = s.size();
                std::string line = s.substr(at, nl - at);
                size_t l = std::string::npos, r = 0;
                for (const char* e : {"│", "├", "╭", "╰"}) l = std::min(l, line.find(e));
                for (const char* e : {"│", "┤", "╮", "╯"})
                    if (size_t k = line.rfind(e); k != std::string::npos) r = std::max(r, k + 3);
                if (l != std::string::npos && r > l) rows.push_back(line.substr(l, r - l));
                at = nl + 1;
            }
            return rows;
        };
        auto with_rain = box_rows(out), without = box_rows(plain);
        CHECK(with_rain.size() >= 9 && with_rain == without, out + "\n---\n" + plain);
    }
    st.cols = 80;
    // links never shows more than Max links (the graph stops there; found keeps counting).
    st.max_links = 25;
    CHECK_HAS(render(views::loading(st, emptyElement()), 80, 24), "links 25 ");
    st.max_links = 0;
    test_loading_fade(st);
    CHECK(views::loading_rows(40) == 32, "loading_rows(40)");
    CHECK(views::loading_rows(5) == 1, "loading_rows(5)");
}

static void dump_main() {
    Fake k = fake_graph();
    View v{36, 110, 24};
    auto main = vbox({
        views::boss_bar(2, 3, 140, 200, false, false),
        views::graph(scene(k), k.f, v) | flex,
        views::omnibar(text(""), {}, -1, "type to search · /bridges"),
    });
    main = dbox({main, hbox({filler(), views::preview("maps.google.com", page_lines(), 0, 1.0f,
                                                      false)})});
    std::cout << render(main, 100, 30) << "\n";
}

// ms per graph frame at 200x60 on a 10-ary tree of 1000 nodes + 10k SEEN cross-links.
static void bench() {
    CrawlGraph g;
    auto u = [](int i) { return "https://s" + std::to_string(i % 13) + ".test/p" + std::to_string(i); };
    g.add_node(u(0), 0);
    for (int i = 1; i < 1000; ++i) {
        g.add_node(u(i), g.nodes[size_t((i - 1) / 10)].depth + 1);
        g.add_edge(u((i - 1) / 10), u(i), false);
    }
    uint32_t x = 12345;
    while (g.edges.size() < 999 + 10000) {
        x = x * 1664525u + 1013904223u;
        int a = int((x >> 8) % 1000), b = int((x >> 20) % 1000);
        if (!g.has_edge(u(a), u(b))) g.add_edge(u(a), u(b), true);
    }
    for (int i = 0; i < 1000; ++i) g.record_fetch(u(i), true);
    auto sc = std::make_shared<const Scene>(make_scene(g));
    const Layout& l = sc->lay;
    const int w = 200, h = 60, frames = 50;
    using Ms = std::chrono::duration<double, std::milli>;
    auto t0 = std::chrono::steady_clock::now();
    ReplayFrame f;
    for (int i = 0; i < frames; ++i) f = replay_at(sc->g, sc->plan, 1000000);
    double replay = Ms(std::chrono::steady_clock::now() - t0).count() / frames;
    std::cout << "bench: " << g.nodes.size() << " nodes, " << g.edges.size() << " edges, " << w << "x" << h << "\n";
    std::cout << "  replay_at       " << replay << " ms/frame\n";
    for (double zoom : {1.0, 4.0}) {
        View v = apply(Camera{zoom, 0, 0}, fit(l, w * 2, (h - 5) * 4), w * 2, (h - 5) * 4);
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) render(views::graph(sc, f, v, zoom), w, h - 5);
        std::cout << "  graph zoom " << zoom << "    " << Ms(std::chrono::steady_clock::now() - t0).count() / frames << " ms/frame\n";
    }
}

int main() {
    test_theme();
    test_graph();
    test_graph_same_site();
    test_label_pad();
    test_label_halo();
    test_seen_edges_cap();
    test_boss_bar();
    test_omnibar();
    test_omnibar_colors();
    test_input_colors();
    test_field_align();
    test_omnibar_placeholder();
    test_toast_too_small();
    test_preview();
    test_coach();
    test_loading();
    if (std::getenv("WC_VIEWS_DUMP")) dump_main();
    if (std::getenv("WC_VIEWS_BENCH")) bench();
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << "\n";
    return failures ? 1 : 0;
}
