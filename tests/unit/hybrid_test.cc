// hybrid_test.cc - crawl_hybrid (BFS layers, trie order inside a layer), the lava ramp,
// and the hybrid replay wave. Run: build/wc_hybrid_test   (exit 1 if any check fails)
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/crawl_hybrid.h"
#include "ui/replay.h"
#include "ui/theme.h"

using namespace wc;

static int failures = 0, checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                        \
    } while (0)

// A tiny fake web. A url that is not listed fails.
struct Web : LinkSource {
    std::map<std::string, std::vector<std::string>> pages;
    std::optional<std::vector<std::string>> links(const std::string& url) override {
        auto it = pages.find(url);
        if (it == pages.end()) return std::nullopt;
        return it->second;
    }
};

static std::vector<std::string> fetched(const CrawlGraph& g) {
    std::vector<std::string> v;
    for (const auto& f : g.fetches) v.push_back(f.url);
    return v;
}

// Root links to two sites out of trie order; each has a path tree below.
static Web two_sites() {
    Web w;
    w.pages["https://r.org/"] = {"https://www.b.com/x", "https://a.com/z", "https://www.b.com/", "https://a.com/a"};
    w.pages["https://www.b.com/x"] = {"https://www.b.com/x/1"};
    w.pages["https://a.com/z"] = {"https://a.com/z/1", "https://r.org/"};
    w.pages["https://www.b.com/"] = {"https://www.b.com/1"};
    w.pages["https://a.com/a"] = {"https://a.com/a/1", "https://a.com/a"};
    return w;
}

static void test_order() {
    CHECK((trie_key("https://www.WowCube.com/docs/a?x=1#top") ==
           std::vector<std::string>{"com", "wowcube", "www", "docs", "a", "?x=1"}));
    CHECK((trie_key("A") == std::vector<std::string>{"a"}));  // fake-web names: one label
    CHECK((hybrid_order({"https://www.b.com/x", "https://a.com/z", "https://www.b.com/", "https://a.com/a/b",
                         "https://a.com/a", "http://a.com/a"}) ==
           std::vector<std::string>{"http://a.com/a", "https://a.com/a", "https://a.com/a/b", "https://a.com/z",
                                    "https://www.b.com/", "https://www.b.com/x"}));
}

static void test_crawl() {
    Web w = two_sites();
    CrawlConfig cfg;
    NoSleeper s;
    CrawlGraph g = crawl_hybrid("https://r.org/", cfg, w, s);
    CHECK(g.mode == Mode::Hybrid);
    // Layer 1 in trie order (a.com before www.b.com, prefix before subtree).
    std::vector<std::string> f = fetched(g);
    CHECK(f.size() == size_t(1 + 4 + 4));
    CHECK((std::vector<std::string>(f.begin(), f.begin() + 5) ==
           std::vector<std::string>{"https://r.org/", "https://a.com/a", "https://a.com/z", "https://www.b.com/",
                                    "https://www.b.com/x"}));
    // Layers: no depth d+1 fetch before every depth d fetch.
    bool layered = true;
    for (size_t i = 1; i < g.fetches.size(); ++i) layered = layered && g.fetches[i - 1].depth <= g.fetches[i].depth;
    CHECK(layered);
    // Shortest hops (D6).
    CHECK(g.nodes[size_t(g.id_of("https://a.com/a/1"))].depth == 2);
    // SEEN edges: back to root, self link (D2, D3).
    CHECK(g.has_edge("https://a.com/z", "https://r.org/") && g.has_edge("https://a.com/a", "https://a.com/a"));
    for (const Edge& e : g.edges)
        if (e.to == "https://r.org/" || e.from == e.to) CHECK(e.seen);
    // Root not slept (D8).
    CHECK(s.calls == int(g.fetches.size()) - 1);
    // Determinism.
    NoSleeper s2;
    CrawlGraph h = crawl_hybrid("https://r.org/", cfg, w, s2);
    CHECK(fetched(h) == f);
    CHECK(h.nodes.size() == g.nodes.size() && h.edges.size() == g.edges.size());
    bool same = true;
    for (size_t i = 0; i < g.nodes.size() && same; ++i) same = g.nodes[i].url == h.nodes[i].url;
    for (size_t i = 0; i < g.edges.size() && same; ++i)
        same = g.edges[i].from == h.edges[i].from && g.edges[i].to == h.edges[i].to && g.edges[i].seen == h.edges[i].seen;
    CHECK(same);
}

static void test_rules() {
    Web w = two_sites();
    NoSleeper s;
    // Size cap: trie order picks the last-layer pages. 6 nodes = root + 4 + a.com/a/1.
    CrawlConfig cap;
    cap.maxSize = 6;
    CrawlGraph g = crawl_hybrid("https://r.org/", cap, w, s);
    CHECK(g.nodes.size() == size_t(6));
    CHECK(g.has("https://a.com/a/1") && !g.has("https://www.b.com/x/1"));
    // D9: start not in the web -> 1 failed fetch, start stays at depth 0.
    CrawlGraph bad = crawl_hybrid("https://nowhere.test/", CrawlConfig{}, w, s);
    CHECK(bad.fetches.size() == size_t(1) && !bad.fetches[0].ok && bad.nodes.size() == size_t(1));
    // maxDepth 0: nothing fetched; maxDepth 1: only the root.
    CrawlConfig d0;
    d0.maxDepth = 0;
    CHECK(crawl_hybrid("https://r.org/", d0, w, s).fetches.empty());
    CrawlConfig d1;
    d1.maxDepth = 1;
    CrawlGraph one = crawl_hybrid("https://r.org/", d1, w, s);
    CHECK(one.fetches.size() == size_t(1) && one.nodes.size() == size_t(5));
    // Cancel: graph so far (here: the root only, never fetched).
    std::atomic<bool> stop{true};
    CrawlConfig c;
    c.cancel = &stop;
    CrawlGraph cut = crawl_hybrid("https://r.org/", c, w, s);
    CHECK(cut.fetches.empty() && cut.nodes.size() == size_t(1));
}

// WCAG relative luminance; contrast vs black = (L + 0.05) / 0.05.
static double lum(anim::Rgb c) {
    auto lin = [](int v) { double x = v / 255.0; return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

static void test_ramp() {
    CHECK((theme::depth_rgb(0, 5) == anim::Rgb{255, 250, 235}));
    CHECK((theme::depth_rgb(5, 5) == anim::Rgb{120, 210, 255}));
    CHECK((theme::depth_rgb(9, 5) == anim::Rgb{120, 210, 255}));
    CHECK((theme::depth_rgb(3, 0) == anim::Rgb{255, 250, 235}));
    // Projector: every ring >= 5:1 on pure black; neighbouring rings clearly apart.
    bool bright = true, apart = true;
    for (int m = 1; m <= 12; ++m)
        for (int d = 0; d <= m; ++d) {
            anim::Rgb c = theme::depth_rgb(d, m);
            bright = bright && (lum(c) + 0.05) / 0.05 >= 5;
            if (d > 0 && m <= 6) {
                anim::Rgb p = theme::depth_rgb(d - 1, m);
                apart = apart && std::abs(c.r - p.r) + std::abs(c.g - p.g) + std::abs(c.b - p.b) >= 75;  // tightest: max 6, rings 2-3 (orange, 80)
            }
        }
    CHECK(bright);
    CHECK(apart);
    // Chrome text too.
    CHECK((lum(theme::rgb::kChrome) + 0.05) / 0.05 >= 5 && (lum(theme::rgb::kAccentDim) + 0.05) / 0.05 >= 5);
}

// Big hybrid crawl replays as a wave; inside a level, fetch order = start order (the sweep).
static void test_wave() {
    CrawlGraph g;
    g.mode = Mode::Hybrid;
    auto u = [](int i) { return "https://t.test/" + std::to_string(i); };
    g.add_node(u(0), 0);
    for (int i = 0; i < 100; ++i) {
        for (int c = 10 * i + 1; c <= 10 * i + 10 && c < 1000; ++c) {
            g.add_node(u(c), g.nodes[size_t(i)].depth + 1);
            g.add_edge(u(i), u(c), false);
        }
        g.record_fetch(u(i), true);
    }
    ReplayPlan p = replay_plan(g);
    CHECK(p.total_ms == 8000);
    CHECK(replay_at(g, p, 8000).size == 1000);
    // First edge of fetch k (k = 1..10, level 1) starts strictly after fetch k-1's.
    bool sweep = true;
    for (int k = 2; k <= 10; ++k) sweep = sweep && p.start[size_t(10 * k)] > p.start[size_t(10 * (k - 1))];
    CHECK(sweep);
    // Parents still land before their kids leave.
    std::vector<double> landed(g.nodes.size(), 0.0);
    for (size_t i = 0; i < g.edges.size(); ++i) landed[size_t(p.to[i])] = p.start[i] + p.edge_ms;
    bool parent_first = true;
    for (size_t i = 0; i < g.edges.size(); ++i) parent_first = parent_first && landed[size_t(p.from[i])] <= p.start[i] + 1e-9;
    CHECK(parent_first);
}

int main() {
    test_order();
    test_crawl();
    test_rules();
    test_ramp();
    test_wave();
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " hybrid checks\n";
    return failures ? 1 : 0;
}
