// crawl.h - part #2 entry point (Tony).
#pragma once

#include <string>

#include "core/graph.h"
#include "core/link_source.h"
#include "core/sleeper.h"

namespace wc {

struct CrawlConfig {
    Mode mode = Mode::BFS;
    int maxDepth = 3;    // spec default
    int maxSize = 1000;  // spec default, max NODES in the graph
    int delay_ms = 100;  // spec: usleep 0.1 s between requests
};

// Contract: see the header comment in crawl.cc.
CrawlGraph crawl(const std::string& start, const CrawlConfig& cfg,
                 LinkSource& src, Sleeper& sleeper);

}  // namespace wc
