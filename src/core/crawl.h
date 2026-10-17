// crawl.h - part #2 entry point (Tony).
#pragma once

#include <atomic>
#include <string>
#include <deque>

#include "core/graph.h"
#include "core/link_source.h"
#include "core/sleeper.h"

namespace wc {

struct CrawlConfig {
    Mode mode = Mode::BFS;
    int maxDepth = 3;    // spec default
    int maxSize = 1000;  // spec default, max NODES in the graph
    int delay_ms = 100;  // spec: usleep 0.1 s between requests
    int __depth = 0; // internal use
    int __size = 0;
    int workers = 1;                            // > 1 and BFS: parallel fetches, no global delay (TUI). 1 = spec path
    const std::atomic<bool>* cancel = nullptr;  // parallel only: set true -> return the partial graph now
};

// Contract: see the header comment in crawl.cc.
CrawlGraph crawl(const std::string& start, const CrawlConfig& cfg,
                 LinkSource& src, Sleeper& sleeper);

// Parallel BFS (crawl_parallel.cc): cfg.workers fetch threads run ahead, results are
// committed in node-id order with crawl_bfs's own rules, so the graph is the one the
// sequential crawl builds. Continues g: fetches every pending page (see pending()).
// After a cancel, calling it again with the returned graph finishes the same crawl.
// The source must be thread safe. Politeness is the source's job (per host).
CrawlGraph resume_crawl(CrawlGraph g, const CrawlConfig& cfg, LinkSource& src);

// Pages left to fetch: Unfetched and depth < maxDepth.
int pending(const CrawlGraph& g, int maxDepth);

class Crawler {
public:
    // these will ignore CrawlConfig::mode
    // returning ref. mgiht be a trap waiting to bloom yay
    static CrawlGraph& crawl_bfs(const std::string& root_url, CrawlConfig cfg, LinkSource& src, Sleeper& sleeper, CrawlGraph& g);
    static CrawlGraph& crawl_dfs(const std::string& root_url, CrawlConfig cfg, LinkSource& src, Sleeper& sleeper, CrawlGraph& g);
};

}  // namespace wc
