// views_test.cc - headless renders of src/ui/views (state in, Element out).
// Run: NO_COLOR=1 build/wc_views_test   (exit 1 if any check fails)
// WC_VIEWS_DUMP=1 also prints a 100x30 main screen for eyeballing.
#include <cstdlib>
#include <iostream>
#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

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
    CHECK(std::string(theme::glyph(1, Status::Unfetched)) == "◆", "");
    CHECK(std::string(theme::glyph(2, Status::Fetched)) == "•", "");
    CHECK(std::string(theme::glyph(5, Status::Fetched)) == "·", "");
    CHECK(std::string(theme::glyph(0, Status::Failed)) == "✕", "");
    CHECK(theme::depth_color(1) == theme::kAccent, "");
    CHECK(theme::depth_color(0) == theme::kText, "");
}

static void test_graph() {
    Fake k = fake_graph();
    View v{30, 80, 30};  // canvas 160x96 sub-pixels at 80x24
    std::string out = render(views::graph(k.g, k.l, k.f, v));
    CHECK_HAS(out, "google.com");
    CHECK_HAS(out, "example.org");
    CHECK_LACKS(out, "maps.test");  // depth 2: glyph only
    CHECK_HAS(out, "•");
    CHECK_HAS(out, "✕");            // github fetch failed
    CHECK_LACKS(out, "deep.test");
    CHECK_HAS(out, "·");
    CHECK_LACKS(out, "www.");

    k.f.node_t = {1, 0, 0, 0, 0};  // only root shown, edges not started
    k.f.edge_t = {0, 0, 0, 0, 0};
    out = render(views::graph(k.g, k.l, k.f, v));
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
    k.f.node_t = {1, 1, 1, 1};
    k.f.edge_t = {1, 1, 1};
    k.f.done = true;
    std::string out = render(views::graph(k.g, k.l, k.f, View{20, 80, 48}));
    CHECK_HAS(out, "/domains");
    CHECK_LACKS(out, "/about");     // collides with /domains, which came first
    CHECK_HAS(out, "icann.org");    // other host: host label
    // root glyph at col 40 row 12; "iana.org" (8 cols) centred under it: cols 36..43
    std::string under = line_of(out, 13);
    CHECK(under.find("iana.org") != std::string::npos, out);
    CHECK(line_of(out, 12).find("◆") != std::string::npos, out);
    size_t p = under.find("iana.org"), cols = 0;
    for (size_t i = 0; i < p; ++i) cols += (static_cast<unsigned char>(under[i]) & 0xC0) != 0x80;
    CHECK(cols == 36, out);
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
        std::string row = line_of(render(views::graph(k.g, k.l, k.f, View{10, 80, 20})), 10);
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
    CHECK_HAS(out, "PAGES");
    CHECK_HAS(out, "40/200");
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

static void test_toast_too_small() {
    std::string out = render(views::toast("Copied 12 URLs"), 30, 3);
    CHECK_HAS(out, "✓ Copied 12 URLs");
    CHECK_HAS(out, "╭");
    out = render(views::too_small(60, 20));
    CHECK_HAS(out, "Enlarge terminal to 80×24 (now 60×20)");
}

static void dump_main() {
    Fake k = fake_graph();
    View v{36, 110, 24};
    auto main = vbox({
        views::boss_bar(2, 3, 140, 200, false, false),
        views::graph(k.g, k.l, k.f, v) | flex,
        views::omnibar(text(""), {}, -1, "type to search · /bridges"),
    });
    std::cout << render(main, 100, 30) << "\n";
}

int main() {
    test_theme();
    test_graph();
    test_graph_same_site();
    test_seen_edges_cap();
    test_boss_bar();
    test_omnibar();
    test_toast_too_small();
    if (std::getenv("WC_VIEWS_DUMP")) dump_main();
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << "\n";
    return failures ? 1 : 0;
}
