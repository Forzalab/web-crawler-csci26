// crawl_test.cc - regression tests for src/core/crawl (BFS queue drain).
// Run: build/wc_crawl_test   (exit 1 if any check fails)
//
// Bug: the TUI core-dumped on a phone (Termux) crawling wikipedia.com, depth 10, size 100.
// crawl_bfs does `todo.pop_front(); ... url = todo.front();`. Once the last url is
// popped, front() reads an empty deque (UB): it copies a std::string from a slot that was
// never constructed. glibc often hands back zeroed memory, so the pipe tests pass by luck;
// Android's allocator (and ASan) do not, and the copy blows up.
// This binary fills every new allocation with 0xBE (like ASan) so the bug shows every time.
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "core/crawl.h"

// Poison fresh heap memory: reading an unconstructed object now gives garbage, not zeros.
void* operator new(std::size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    std::memset(p, 0xBE, n);
    return p;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // our new is malloc: free is the match
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#pragma GCC diagnostic pop

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

// A tiny fake web. A url that is not listed fails.
struct Web : LinkSource {
    std::map<std::string, std::vector<std::string>> pages;
    std::optional<std::vector<std::string>> links(const std::string& url) override {
        auto it = pages.find(url);
        if (it == pages.end()) return std::nullopt;
        return it->second;
    }
};

// Runs crawl() and turns an escaped exception into "THREW: ..." (crawl must never throw).
static std::string run(const std::string& start, CrawlConfig cfg, Web& web, CrawlGraph& out) {
    NoSleeper sl;
    try {
        out = crawl(start, cfg, web, sl);
        return "ok";
    } catch (const std::exception& e) {
        return std::string("THREW: ") + e.what();
    }
}

static std::string u(const std::string& path) { return "https://en.wikipedia.org/wiki/" + path; }

// Whole site fits under maxSize: the queue empties before the size cap (the phone case).
static void test_bfs_queue_drains() {
    Web web;
    std::vector<std::string> kids;
    for (int i = 0; i < 20; ++i) kids.push_back(u("Page_" + std::to_string(i)));
    web.pages[u("Main_Page")] = kids;
    for (const auto& k : kids) web.pages[k] = {u("Main_Page"), kids[0]};  // cross-links, no new pages
    CrawlConfig cfg;
    cfg.maxDepth = 10;
    cfg.maxSize = 100;
    CrawlGraph g;
    CHECK_EQ(run(u("Main_Page"), cfg, web, g), std::string("ok"));
    CHECK_EQ(g.nodes.size(), size_t(21));
    CHECK_EQ(g.fetches.size(), size_t(21));
}

// Start page fails (DNS, TLS, 404...): the queue is empty right after the first pop.
static void test_bfs_start_fails() {
    Web web;
    CrawlConfig cfg;
    cfg.maxDepth = 10;
    cfg.maxSize = 100;
    CrawlGraph g;
    CHECK_EQ(run("https://wikipedia.com", cfg, web, g), std::string("ok"));
    CHECK_EQ(g.nodes.size(), size_t(1));
    CHECK_EQ(g.fetches.size(), size_t(1));
}

// Last page in the queue sits at maxDepth (not fetched): the depth-cut branch drains it.
static void test_bfs_last_at_max_depth() {
    Web web;
    web.pages["https://a.example.org/start"] = {"https://a.example.org/leaf-page"};
    CrawlConfig cfg;
    cfg.maxDepth = 1;
    cfg.maxSize = 100;
    CrawlGraph g;
    CHECK_EQ(run("https://a.example.org/start", cfg, web, g), std::string("ok"));
    CHECK_EQ(g.nodes.size(), size_t(2));
    CHECK_EQ(g.fetches.size(), size_t(1));
}

// DFS keeps the size cap like BFS: a new page is skipped once g holds maxSize nodes.
// Tree: A -> B C, B -> D E, C -> F G.
static void test_dfs_size_cap() {
    Web web;
    web.pages["A"] = {"B", "C"};
    web.pages["B"] = {"D", "E"};
    web.pages["C"] = {"F", "G"};
    for (const char* leaf : {"D", "E", "F", "G"}) web.pages[leaf] = {};
    CrawlConfig cfg;
    cfg.mode = Mode::DFS;
    cfg.maxSize = 1;
    CrawlGraph g;
    CHECK_EQ(run("A", cfg, web, g), std::string("ok"));
    CHECK_EQ(g.nodes.size(), size_t(1));
    CHECK_EQ(g.fetches.size(), size_t(1));
    CHECK_EQ(g.edges.size(), size_t(0));

    cfg.maxSize = 4;
    CHECK_EQ(run("A", cfg, web, g), std::string("ok"));
    std::string nodes, edges;
    for (const auto& n : g.nodes) nodes += n.url;
    for (const auto& e : g.edges) edges += e.from + "-" + e.to + " ";
    CHECK_EQ(nodes, std::string("ABDE"));
    CHECK_EQ(edges, std::string("A-B B-D B-E "));
}

int main() {
    test_bfs_queue_drains();
    test_bfs_start_fails();
    test_bfs_last_at_max_depth();
    test_dfs_size_cap();
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
