// test_bridges_json.cc - golden tests for src/bridges/bridges_json.cc.
// Builds small CrawlGraphs by hand (no crawl(), no network), serializes them,
// compares with tests/bridges/golden/<case>.json byte for byte.
//   build/test_bridges_json [golden_dir]            check (exit 1 on any FAIL)
//   build/test_bridges_json [golden_dir] --update   rewrite the golden files
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "bridges/bridges_json.h"

using namespace wc;

namespace {

CrawlGraph graph(const std::vector<std::pair<std::string, int>>& nodes,
                 const std::vector<std::tuple<std::string, std::string, bool>>& edges) {
    CrawlGraph g;
    for (auto& [url, d] : nodes) g.add_node(url, d);
    for (auto& [a, b, seen] : edges) g.add_edge(a, b, seen);
    return g;
}

struct Case {
    std::string name;
    std::function<std::string()> make;
};

std::vector<Case> cases() {
    return {
        {"empty", [] { return bridges_json(CrawlGraph{}); }},
        {"chain_roygbiv", [] {  // depth 0..7: 7 colors, depth 7 clamps to violet
             std::vector<std::pair<std::string, int>> n;
             std::vector<std::tuple<std::string, std::string, bool>> e;
             for (int d = 0; d <= 7; ++d) {
                 n.push_back({"http://x/" + std::to_string(d), d});
                 if (d) e.push_back({"http://x/" + std::to_string(d - 1), "http://x/" + std::to_string(d), false});
             }
             return bridges_json(graph(n, e));
         }},
        {"edges_cleanup", [] {  // unsorted discovery, back-link, self-loop, duplicate, dangling
             auto g = graph({{"http://z", 0}, {"http://a", 1}, {"http://m", 1}},
                            {{"http://z", "http://m", false},
                             {"http://z", "http://a", false},
                             {"http://a", "http://z", true},      // back-link: thick
                             {"http://m", "http://m", true},      // self-loop: dropped
                             {"http://a", "http://z", true},      // duplicate: dropped
                             {"http://m", "http://gone", false}}); // unknown node: dropped
             return bridges_json(g);
         }},
        {"meta_escape", [] {  // title + description + hover text escaping, rescued node
             auto g = graph({{"http://h/", 0}, {"http://h/q?a=\"1\"&b=\\", 1}},
                            {{"http://h/", "http://h/q?a=\"1\"&b=\\", false}});
             BridgesMeta m;
             m.title = "Crawl \"h\"";
             m.description = "line1\nline2\ttab";
             m.page_titles["http://h/"] = "Caf\xC3\xA9 \x01 bad\xFF byte";
             m.rescued.insert("http://h/q?a=\"1\"&b=\\");
             return bridges_json(g, m);
         }},
        {"pretty", [] {  // TUI look: sites, sizes, hub, hover cards, location window, rainbow, link styles
             auto g = graph({{"http://r/", 0}, {"http://r/a", 1}, {"http://x/b", 1}, {"http://y/c", 2}, {"http://r/d", 2}},
                            {{"http://r/", "http://r/a", false},
                             {"http://r/", "http://x/b", false},
                             {"http://x/b", "http://y/c", false},
                             {"http://r/a", "http://r/d", false},
                             {"http://x/b", "http://r/a", true},   // SEEN: thin grey
                             {"http://y/c", "http://r/a", true},
                             {"http://r/d", "http://r/", true}});
             g.record_fetch("http://r/", true);
             g.record_fetch("http://r/a", true);
             g.record_fetch("http://x/b", false);  // failed: full colour; c, d unfetched: alpha 0.35
             BridgesMeta m;
             m.pretty = true;
             m.site_rank = {{"http://r/", 0}, {"http://r/a", 0}, {"http://r/d", 0}, {"http://x/b", 1}, {"http://y/c", -1}};
             // Layout: ring = depth. Scaled so the farthest coordinate (1.9) lands on 500.
             m.location = {{"http://r/", {0, 0}}, {"http://r/a", {1, 0}}, {"http://x/b", {-1, 0}},
                           {"http://y/c", {-1.6, 1.2}}, {"http://r/d", {0.6, -1.9}}};
             m.labels = {{"http://r/a", "/a"}};          // hub: "★ /a" first in its hover card
             m.page_titles = {{"http://r/", "Home"}};
             return bridges_json(g, m);
         }},
    };
}

int checks = 0, subset_fail = 0;
void expect(bool ok, const std::string& what) {
    ++checks;
    if (!ok) ++subset_fail, std::cout << "FAIL subset: " << what << '\n';
}
std::string ids(const CrawlGraph& g) {
    std::string s;
    for (const Node& n : g.nodes) s += n.url.substr(n.url.rfind('/') + 1) + " ";
    return s;
}

// Rainbow highway and hubs: deterministic choices.
void path_hub_tests() {
    auto u = [](const std::string& p) { return "http://p/" + p; };
    CrawlGraph g;
    g.add_node(u("r"), 0);
    auto kid = [&](const std::string& f, const std::string& t, int d) { g.add_node(u(t), d), g.add_edge(u(f), u(t), false); };
    kid("r", "a", 1), kid("r", "b", 1), kid("a", "a1", 2), kid("b", "b1", 2), kid("b", "b2", 2);
    auto path = [&] {
        std::string s;
        for (int i : rainbow_path(g)) s += g.nodes[size_t(i)].url.substr(9) + " ";
        return s;
    };
    expect(path() == "r a a1 ", "depth tie, no in-links: lowest id wins: " + path());
    g.add_edge(u("a1"), u("b2"), true);  // b2 now has 2 in-links: the tie goes to it
    expect(path() == "r b b2 ", "depth tie: most in-links at the end: " + path());
    kid("a1", "z", 3);
    expect(path() == "r a a1 z ", "deepest wins: " + path());
    expect(rainbow_path(CrawlGraph{}).empty(), "empty graph: no path");
    // Hubs: >= 2 in-links, most first, ties by id, never the root, at most k.
    for (const char* f : {"a", "b1", "z"}) g.add_edge(u(f), u("b"), true);  // b: 4 in-links
    g.add_edge(u("z"), u("a1"), true);                                       // a1: 2
    g.add_edge(u("b1"), u("r"), true), g.add_edge(u("z"), u("r"), true);     // root: 2, not a hub
    auto hubs = [&](int k) {
        std::string s;
        for (int i : top_hubs(g, k)) s += g.nodes[size_t(i)].url.substr(9) + " ";
        return s;
    };
    expect(hubs(10) == "b a1 b2 ", "hubs: " + hubs(10));
    expect(hubs(2) == "b a1 ", "top 2: " + hubs(2));
}

// bridges_subset: the most prominent nodes, parents kept, deterministic.
void subset_tests() {
    // Tree: r -> a b c ; a -> a1 a2 a3 ; b -> b1 ; a1 -> x ; SEEN into b1 from a2, a3, x.
    auto u = [](const std::string& p) { return "http://s/" + p; };
    CrawlGraph g;
    g.add_node(u("r"), 0);
    auto kid = [&](const std::string& f, const std::string& t, int d) { g.add_node(u(t), d), g.add_edge(u(f), u(t), false); };
    kid("r", "a", 1), kid("r", "b", 1), kid("r", "c", 1);
    kid("a", "a1", 2), kid("a", "a2", 2), kid("a", "a3", 2), kid("b", "b1", 2), kid("a1", "x", 3);
    for (const char* f : {"a2", "a3", "x"}) g.add_edge(u(f), u("b1"), true);
    expect(bridges_subset(g).nodes.size() == g.nodes.size() && bridges_subset(g).edges.size() == g.edges.size(), "within caps: unchanged");
    expect(bridges_json(bridges_subset(g)) == bridges_json(g), "within caps: same bytes");
    // 6 nodes: r a b c, then b1 (in-links 4: score 4*4+1+4*1 = 21) before a1 (4+2+4 = 10).
    CrawlGraph s = bridges_subset(g, 6);
    expect(ids(s) == "r a b c a1 b1 ", "kept ids: " + ids(s));  // id order kept
    // 5 nodes: b1 fits (parent b kept), a1 does not.
    expect(ids(bridges_subset(g, 5)) == "r a b c b1 ", "kept ids at 5: " + ids(bridges_subset(g, 5)));
    // x (depth 3) needs a1 too: chain of 2 fits at 7 only after b1, a1 -> 7 = r a b c b1 a1 + one of a2 a3 x.
    CrawlGraph s7 = bridges_subset(g, 7);
    bool parents = true;
    for (const Edge& e : g.edges)
        if (!e.seen && s7.has(e.to)) parents = parents && s7.has(e.from);
    expect(parents, "every kept node's parent kept: " + ids(s7));
    expect(bridges_json(bridges_subset(g, 7)) == bridges_json(s7), "deterministic");
    // max_edges = 3: all 8 tree edges stay, no SEEN edge fits.
    CrawlGraph e3 = bridges_subset(g, 1000, 3);
    int seen = 0;
    for (const Edge& e : e3.edges) seen += e.seen;
    expect(e3.nodes.size() == 9 && e3.edges.size() == 8 && seen == 0, "max_edges=3 keeps every tree edge");
    // Star of 1500: root + the first 999 kids (all tie).
    CrawlGraph star;
    star.add_node("http://star/", 0);
    for (int i = 1; i <= 1500; ++i) {
        star.add_node("http://star/" + std::to_string(i), 1);
        star.add_edge("http://star/", "http://star/" + std::to_string(i), false);
    }
    CrawlGraph st = bridges_subset(star);
    expect(st.nodes.size() == 1000 && st.nodes[1].url == "http://star/1" && st.nodes[999].url == "http://star/999" &&
               st.edges.size() == 999, "star of 1500 -> root + 1..999");
}

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = "tests/bridges/golden";
    bool update = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--update")) update = true;
        else dir = argv[i];
    }

    int fail = 0;
    for (const Case& c : cases()) {
        std::string path = dir + "/" + c.name + ".json";
        std::string got = c.make() + "\n";
        if (update) {
            std::ofstream(path, std::ios::binary) << got;
            std::cout << "WROTE " << path << '\n';
            continue;
        }
        bool ok = got == slurp(path) && got == c.make() + "\n";  // also: same bytes twice
        std::cout << (ok ? "PASS " : "FAIL ") << c.name << '\n';
        if (!ok) {
            ++fail;
            std::cout << "  got:  " << got;
        }
    }

    // Same nodes discovered in a different order -> identical bytes.
    auto a = graph({{"http://b", 0}, {"http://a", 1}}, {{"http://b", "http://a", false}});
    auto b = graph({{"http://a", 1}, {"http://b", 0}}, {{"http://b", "http://a", false}});
    bool same = bridges_json(a) == bridges_json(b);
    std::cout << (same ? "PASS " : "FAIL ") << "order_independent\n";
    if (!same) ++fail;

    subset_tests();
    path_hub_tests();
    std::cout << (subset_fail ? "FAIL " : "PASS ") << "subset (" << checks - subset_fail << "/" << checks << ")\n";
    fail += subset_fail;

    if (!update) std::cout << "SUMMARY: " << fail << " failed\n";
    return fail ? 1 : 0;
}
