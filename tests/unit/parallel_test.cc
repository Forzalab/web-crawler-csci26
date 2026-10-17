// parallel_test.cc - resume_crawl (src/core/crawl_parallel.cc): the parallel BFS must
// build exactly the graph of the sequential crawl_bfs, stop fast on cancel, and a
// resume after any stop must finish the same crawl.
// Run from the repo root: build/wc_parallel_test   (exit 1 if any check fails)
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/crawl.h"
#include "mocks/mock_fetcher.h"

using namespace wc;
using Clock = std::chrono::steady_clock;

static int failures = 0, checks = 0;

#define CHECK(cond, ctx)                                                                \
    do {                                                                                \
        ++checks;                                                                       \
        if (!(cond)) {                                                                  \
            ++failures;                                                                 \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "  " << (ctx) << "\n"; \
        }                                                                               \
    } while (0)

static std::atomic<int> in_flight{0};  // every Jitter call; main waits for 0 (detached fetches)

// Thread-safe fake web: sleeps a per-url pseudo-random 0..max_ms (results land out of
// order), counts calls, and can raise `stop` on its k-th call.
struct Jitter : LinkSource {
    MockFetcher web;
    int max_ms;
    std::atomic<int> calls{0};
    std::atomic<int> stop_at{0};  // 0 = never (atomic: detached fetches still read it)
    std::atomic<bool> stop{false};
    Jitter(FakeWeb w, int ms) : web(std::move(w)), max_ms(ms) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override {
        ++in_flight;
        int n = ++calls;
        if (stop_at && n == stop_at) stop = true;
        if (max_ms > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(std::hash<std::string>{}(url) % (max_ms * 1000 + 1)));
        auto r = web.links(url);
        --in_flight;
        return r;
    }
};
static std::vector<std::unique_ptr<Jitter>> keep;  // outlive detached fetch threads
static Jitter& jitter(FakeWeb w, int ms) {
    keep.push_back(std::make_unique<Jitter>(std::move(w), ms));
    return *keep.back();
}

static std::string dump(const CrawlGraph& g) {
    std::ostringstream o;
    for (const auto& n : g.nodes) o << "N " << n.url << ' ' << n.depth << ' ' << to_string(n.status) << '\n';
    for (const auto& e : g.edges) o << "E " << e.from << ' ' << e.to << ' ' << e.seen << '\n';
    for (const auto& f : g.fetches) o << "F " << f.url << ' ' << f.depth << ' ' << f.ok << '\n';
    return o.str();
}

static CrawlGraph sequential(const std::string& start, CrawlConfig cfg, const FakeWeb& w) {
    MockFetcher m(w);
    NoSleeper s;
    cfg.workers = 1;
    return crawl(start, cfg, m, s);
}

// Seeded cyclic web: n pages, 0..8 links each (back links, self links, dupes),
// every 23rd page fails, some links point at pages that are not listed (fail too).
static FakeWeb cyclic_web(int n, unsigned seed) {
    FakeWeb w;
    auto page = [](int i) { return "https://c" + std::to_string(i % 7) + ".example/p" + std::to_string(i); };
    for (int i = 0; i < n; ++i) {
        if (i % 23 == 22) {
            w[page(i)] = std::nullopt;
            continue;
        }
        std::vector<std::string> ls;
        int k = int((seed = seed * 1103515245u + 12345u) >> 16) % 9;
        for (int j = 0; j < k; ++j) {
            int t = int((seed = seed * 1103515245u + 12345u) >> 16) % (n + 20);  // >= n: not listed
            ls.push_back(page(t));
        }
        w[page(i)] = ls;
    }
    return w;
}
static const std::string kStart = "https://c0.example/p0";

static void test_goldens() {
    int bfs = 0;
    for (int i = 0; i < 100; ++i) {
        std::ifstream in("tests/crawl/inputfile" + std::to_string(i));
        if (!in) continue;
        PipeInput p;
        if (!parse_pipe_input(in, p).empty() || p.cfg.mode != Mode::BFS) continue;
        ++bfs;
        CrawlConfig cfg = p.cfg;
        cfg.workers = 16;
        NoSleeper s;
        CrawlGraph par = crawl(p.start, cfg, jitter(p.web, 2), s);
        CHECK(dump(par) == dump(sequential(p.start, p.cfg, p.web)), "inputfile" + std::to_string(i));
        CHECK(s.calls == 0, "parallel never uses the global sleeper");
    }
    CHECK(bfs >= 10, "BFS golden inputs found (run from the repo root): " + std::to_string(bfs));
}

static void test_cyclic() {
    FakeWeb w = cyclic_web(500, 26);
    for (auto [d, n] : {std::pair{10, 300}, std::pair{10, 1000}, std::pair{3, 1000}, std::pair{0, 10}, std::pair{5, 0}}) {
        CrawlConfig cfg;
        cfg.maxDepth = d, cfg.maxSize = n, cfg.workers = 16;
        NoSleeper s;
        CrawlGraph par = crawl(kStart, cfg, jitter(w, 1), s);
        CHECK(dump(par) == dump(sequential(kStart, cfg, w)), "depth " + std::to_string(d) + " size " + std::to_string(n));
    }
    // Smallest parallel run, no jitter.
    CrawlConfig cfg;
    cfg.maxDepth = 10, cfg.maxSize = 400, cfg.workers = 2;
    NoSleeper s;
    CHECK(dump(crawl(kStart, cfg, jitter(w, 0), s)) == dump(sequential(kStart, cfg, w)), "workers 2");
}

// Cancel mid-crawl: returns within 1 s with pages left; resume finishes the same crawl.
static void test_cancel_fast() {
    FakeWeb w = cyclic_web(500, 7);
    Jitter& slow = jitter(w, 100);  // up to 100 ms per fetch
    std::atomic<bool> cancel{false};
    CrawlConfig cfg;
    cfg.maxDepth = 10, cfg.maxSize = 1000, cfg.workers = 16, cfg.cancel = &cancel;
    Clock::time_point at;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        at = Clock::now();
        cancel = true;
    });
    NoSleeper s;
    CrawlGraph g = crawl(kStart, cfg, slow, s);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - at).count();
    stopper.join();
    CHECK(ms < 1000, "cancel took " + std::to_string(ms) + " ms");
    CHECK(pending(g, cfg.maxDepth) > 0, "pages left after cancel");
    cancel = false;
    CHECK(dump(resume_crawl(g, cfg, slow)) == dump(sequential(kStart, cfg, w)), "resume after cancel");
}

// Stop when the source sees call k (k = 1 is the start page), then resume: same graph.
static void test_resume_at_k() {
    FakeWeb w = cyclic_web(500, 99);
    CrawlConfig cfg;
    cfg.maxDepth = 10, cfg.maxSize = 1000, cfg.workers = 16;
    std::string want = dump(sequential(kStart, cfg, w));
    for (int k : {1, 2, 5, 17}) {
        Jitter& src = jitter(w, 2);
        src.stop_at = k;
        CrawlConfig c = cfg;
        c.cancel = &src.stop;
        NoSleeper s;
        CrawlGraph g = crawl(kStart, c, src, s);
        if (k == 17) CHECK(pending(g, cfg.maxDepth) > 0, "stopped early at call 17");
        src.stop = false;
        CHECK(dump(resume_crawl(g, c, src)) == want, "resume after stop at call " + std::to_string(k));
    }
    // Stop twice (calls 5, then 10 more), then resume to the end.
    Jitter& src = jitter(w, 2);
    CrawlConfig c = cfg;
    c.cancel = &src.stop;
    src.stop_at = 5;
    NoSleeper s;
    CrawlGraph g = crawl(kStart, c, src, s);
    src.stop = false;
    src.stop_at = src.calls + 10;
    g = resume_crawl(g, c, src);
    CHECK(pending(g, cfg.maxDepth) > 0, "second stop left pages");
    src.stop = false;
    src.stop_at = 0;
    g = resume_crawl(g, c, src);
    CHECK(dump(g) == want, "stop twice then resume");
    // Resume of a finished crawl: no fetches, same graph.
    Jitter& idle = jitter(w, 0);
    CHECK(dump(resume_crawl(g, c, idle)) == want && idle.calls == 0, "resume finished = no-op");
    CHECK(pending(g, cfg.maxDepth) == 0, "nothing pending at the end");
}

int main() {
    test_goldens();
    test_cyclic();
    test_cancel_fast();
    test_resume_at_k();
    while (in_flight > 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));  // detached fetches
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
