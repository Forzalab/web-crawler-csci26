// replay.h - animate a finished crawl as if live. Pure: f(graph, ms).
// Fetch k starts at k*step_ms. Its edges (recording order) grow over
// edge_ms; a NEW edge's child node appears when its edge lands.
#pragma once

#include <cstdint>
#include <vector>

#include "core/graph.h"

namespace wc {

struct ReplayFrame {
    std::vector<float> node_t;  // per node id: 0 hidden .. 1 fully shown
    std::vector<float> edge_t;  // per edge id: 0 not started .. 1 landed
    int depth = 0;              // max depth among shown nodes
    int size = 0;               // shown node count
    bool done = false;          // everything landed
};

struct ReplayTiming { int step_ms = 120; int edge_ms = 250; };

ReplayFrame replay_at(const CrawlGraph& g, int64_t elapsed_ms, ReplayTiming t = {});

}  // namespace wc
