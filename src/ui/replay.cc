// replay.cc - see replay.h
#include "ui/replay.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace wc {

ReplayFrame replay_at(const CrawlGraph& g, int64_t elapsed_ms, ReplayTiming t) {
    const int n = static_cast<int>(g.nodes.size());
    ReplayFrame f;
    f.node_t.assign(n, 0.0f);
    f.edge_t.assign(g.edges.size(), 0.0f);
    if (n > 0) f.node_t[0] = 1.0f;

    // url -> first fetch index (a url fetched twice animates once).
    std::unordered_map<std::string, int> fetch_of;
    fetch_of.reserve(g.fetches.size());
    for (int k = 0; k < static_cast<int>(g.fetches.size()); ++k) fetch_of.emplace(g.fetches[k].url, k);
    std::vector<int> sibling(g.fetches.size(), 0);

    const double ms = static_cast<double>(elapsed_ms);
    double last_end = 0.0;
    for (size_t i = 0; i < g.edges.size(); ++i) {
        const Edge& e = g.edges[i];
        auto it = fetch_of.find(e.from);
        if (it == fetch_of.end()) continue;  // from never fetched: stays 0
        int k = it->second;
        double start = double(k) * t.step_ms + sibling[k]++ * (t.edge_ms / 4.0);
        double end = start + std::max(0, t.edge_ms);
        last_end = std::max(last_end, end);
        double et;
        if (t.edge_ms <= 0) et = ms >= start ? 1.0 : 0.0;
        else et = std::clamp((ms - start) / t.edge_ms, 0.0, 1.0);
        f.edge_t[i] = static_cast<float>(et);
        if (!e.seen) {
            int to = g.id_of(e.to);
            if (to > 0) f.node_t[to] = std::max(f.node_t[to], f.edge_t[i]);
        }
    }

    for (int i = 0; i < n; ++i) {
        if (f.node_t[i] < 1.0f) continue;
        ++f.size;
        f.depth = std::max(f.depth, g.nodes[i].depth);
    }
    f.done = ms >= last_end;
    return f;
}

}  // namespace wc
