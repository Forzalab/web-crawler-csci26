// graph_test.cc - unit tests for src/core/graph (update_node depth push, D6).
// Run: build/wc_graph_test   (exit 1 if any check fails)
#include <iostream>

#include "core/graph.h"

using namespace wc;

static int failures = 0, checks = 0;

#define CHECK_EQ(got, want)                                                                         \
    do {                                                                                            \
        ++checks;                                                                                   \
        auto g_ = (got);                                                                            \
        auto w_ = (want);                                                                           \
        if (!(g_ == w_)) {                                                                          \
            ++failures;                                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #got "\n  got:  " << g_     \
                      << "\n  want: " << w_ << "\n";                                                \
        }                                                                                           \
    } while (0)

static int depth(const CrawlGraph& g, const std::string& url) { return g.nodes[g.id_of(url)].depth; }

static void test_chain_shortcut() {  // DFS found post2 at 3; blog->post2 later says 2
    CrawlGraph g;
    const char* urls[] = {"home", "blog", "post1", "post2", "post3"};
    for (int d = 0; d < 5; ++d) g.add_node(urls[d], d);
    for (int d = 1; d < 5; ++d) g.add_edge(urls[d - 1], urls[d], false);
    g.record_fetch("home", true);
    g.record_fetch("blog", true);
    g.add_edge("blog", "post2", true);
    g.update_node("post2", 2);
    CHECK_EQ(depth(g, "home"), 0);
    CHECK_EQ(depth(g, "blog"), 1);
    CHECK_EQ(depth(g, "post1"), 2);  // not downstream of post2
    CHECK_EQ(depth(g, "post2"), 2);
    CHECK_EQ(depth(g, "post3"), 3);  // pushed
    CHECK_EQ(g.fetches.size(), size_t(2));
    CHECK_EQ(g.fetches[1].depth, 1);  // fetches are history, untouched
    CHECK_EQ(g.edges.size(), size_t(5));
    CHECK_EQ(g.nodes[3].url, std::string("post2"));  // order unchanged
    CHECK_EQ(std::string(to_string(g.nodes[0].status)), std::string("FETCHED"));

    g.update_node("post2", 2);  // not smaller: no-op
    g.update_node("post2", 5);  // larger: no-op
    g.update_node("nope", 0);   // absent: no-op
    CHECK_EQ(depth(g, "post2"), 2);
    CHECK_EQ(g.nodes.size(), size_t(5));
}

static void test_cycle_terminates() {
    CrawlGraph g;
    g.add_node("a", 0);
    g.add_node("b", 5);
    g.add_node("c", 6);
    g.add_edge("b", "c", false);
    g.add_edge("c", "b", true);      // cycle
    g.add_edge("c", "c", true);      // self-loop
    g.add_edge("c", "gone", false);  // unknown node: skipped
    g.update_node("b", 1);
    CHECK_EQ(depth(g, "a"), 0);
    CHECK_EQ(depth(g, "b"), 1);
    CHECK_EQ(depth(g, "c"), 2);
}

int main() {
    test_chain_shortcut();
    test_cycle_terminates();
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
