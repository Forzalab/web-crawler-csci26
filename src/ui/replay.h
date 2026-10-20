// replay.h - animate a finished crawl as if live. Pure: f(graph, ms).
// Fetch k starts at k*step_ms. Its edges (recording order) grow over
// edge_ms; a NEW edge's child node appears when its edge lands.
// Big crawls would take minutes that way, so a schedule that ends after
// budget_ms becomes a wave instead: one BFS level at a time, every fetch of
// the level at once, siblings landing `batch` at a time. Never over budget_ms.
// A Mode::Hybrid crawl keeps its fetch order inside a level (the trie sweep).
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
    double ms = 0;              // content time of this frame (volcano effects in views::graph)
};

struct ReplayTiming { int step_ms = 120; int edge_ms = 250; int budget_ms = 8000; int batch = 3; };

// The whole schedule, computed once per graph (replay_at is then cheap per frame).
struct ReplayPlan {
    std::vector<int> from, to;   // per edge: node ids (-1 if absent)
    std::vector<double> start;   // per edge: start ms (-1 = `from` never fetched, never animates)
    double edge_ms = 0;          // grow time of every edge
    double end = 0;              // done at this ms
    int64_t total_ms = 0;        // ceil(end) = min(the per-fetch schedule, budget_ms)
};

ReplayPlan replay_plan(const CrawlGraph& g, ReplayTiming t = {});
ReplayFrame replay_at(const CrawlGraph& g, const ReplayPlan& p, int64_t elapsed_ms);

ReplayFrame replay_at(const CrawlGraph& g, int64_t elapsed_ms, ReplayTiming t = {});

// When the last edge lands (ms of content time): replay_at(g, replay_ms(g)).done.
int64_t replay_ms(const CrawlGraph& g, ReplayTiming t = {});

// Replay clock: wall ms since the replay began -> content ms for replay_at.
// Starts at ~0.3x so the first fetches land one by one, accelerates smoothly
// over the first <= 4 s, then runs faster than 1x so it still ends at total_ms
// (anim::warp). Monotonic. --no-anim: skip it and pass a huge elapsed instead.
int64_t replay_clock(int64_t wall_ms, int64_t total_ms);

}  // namespace wc
