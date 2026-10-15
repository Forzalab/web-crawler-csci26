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
    };
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

    if (!update) std::cout << "SUMMARY: " << fail << " failed\n";
    return fail ? 1 : 0;
}
