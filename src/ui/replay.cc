// replay.cc - see replay.h
#include "ui/replay.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include "ui/anim.h"

namespace wc {

ReplayPlan replay_plan(const CrawlGraph& g, ReplayTiming t) {
    ReplayPlan p;
    const size_t ne = g.edges.size();
    p.from.resize(ne);
    p.to.resize(ne);
    p.start.assign(ne, -1.0);
    p.edge_ms = std::max(0, t.edge_ms);
    // url -> first fetch index (a url fetched twice animates once).
    std::unordered_map<std::string, int> fetch_of;
    fetch_of.reserve(g.fetches.size());
    for (int k = 0; k < static_cast<int>(g.fetches.size()); ++k) fetch_of.emplace(g.fetches[k].url, k);
    std::vector<int> fetch(ne, -1), sib(ne, 0), sibling(g.fetches.size(), 0);
    for (size_t i = 0; i < ne; ++i) {
        p.from[i] = g.id_of(g.edges[i].from);
        p.to[i] = g.id_of(g.edges[i].to);
        auto it = fetch_of.find(g.edges[i].from);
        if (it == fetch_of.end()) continue;
        fetch[i] = it->second;
        sib[i] = sibling[size_t(it->second)]++;
        // Per fetch: fetch k at k*step, siblings edge_ms/4 apart.
        p.start[i] = double(it->second) * t.step_ms + sib[i] * (p.edge_ms / 4.0);
        p.end = std::max(p.end, p.start[i] + p.edge_ms);
    }
    if (p.end > t.budget_ms) {
        // Wave: level L = depth of the fetching page. M[L] = most batches any of its fetches has.
        const int batch = std::max(1, t.batch);
        auto level = [&](size_t i) { return p.from[i] >= 0 ? g.nodes[size_t(p.from[i])].depth : 0; };
        std::vector<int> M;
        for (size_t i = 0; i < ne; ++i) {
            if (fetch[i] < 0) continue;
            size_t L = size_t(level(i));
            if (M.size() <= L) M.resize(L + 1, 0);
            M[L] = std::max(M[L], sib[i] / batch + 1);
        }
        int nlev = 0, sum_m = 0;
        for (int m : M) nlev += m > 0, sum_m += m;
        const double E = p.edge_ms;
        const double gap = sum_m > 0 ? std::max(0.0, t.budget_ms - nlev * E) / sum_m : 0.0;
        std::vector<double> T(M.size(), 0.0);  // level start: the previous level has landed
        for (size_t L = 1; L < M.size(); ++L) T[L] = T[L - 1] + (M[L - 1] > 0 ? M[L - 1] * gap + E : 0.0);
        double last = 0;
        for (size_t i = 0; i < ne; ++i) {
            if (fetch[i] < 0) continue;
            p.start[i] = T[size_t(level(i))] + (sib[i] / batch) * gap;
            last = std::max(last, p.start[i] + E);
        }
        // Too many levels for the budget even with no gaps: squeeze starts and grow time alike.
        if (last > t.budget_ms) {
            const double k = t.budget_ms / last;
            for (double& s : p.start)
                if (s >= 0) s *= k;
            p.edge_ms *= k;
        }
        p.end = t.budget_ms;
    }
    p.total_ms = static_cast<int64_t>(std::ceil(p.end));  // done at exactly this ms
    return p;
}

ReplayFrame replay_at(const CrawlGraph& g, const ReplayPlan& p, int64_t elapsed_ms) {
    const int n = static_cast<int>(g.nodes.size());
    ReplayFrame f;
    f.node_t.assign(n, 0.0f);
    f.edge_t.assign(g.edges.size(), 0.0f);
    if (n > 0) f.node_t[0] = 1.0f;

    const double ms = static_cast<double>(elapsed_ms);
    for (size_t i = 0; i < g.edges.size() && i < p.start.size(); ++i) {
        const double start = p.start[i];
        if (start < 0) continue;  // from never fetched: stays 0
        double et;
        if (p.edge_ms <= 0) et = ms >= start ? 1.0 : 0.0;
        else et = std::clamp((ms - start) / p.edge_ms, 0.0, 1.0);
        f.edge_t[i] = static_cast<float>(et);
        if (!g.edges[i].seen) {
            int to = p.to[i];
            if (to > 0) f.node_t[to] = std::max(f.node_t[to], f.edge_t[i]);
        }
    }

    for (int i = 0; i < n; ++i) {
        if (f.node_t[i] < 1.0f) continue;
        ++f.size;
        f.depth = std::max(f.depth, g.nodes[i].depth);
    }
    f.done = ms >= p.end;
    return f;
}

ReplayFrame replay_at(const CrawlGraph& g, int64_t elapsed_ms, ReplayTiming t) {
    return replay_at(g, replay_plan(g, t), elapsed_ms);
}

int64_t replay_ms(const CrawlGraph& g, ReplayTiming t) { return replay_plan(g, t).total_ms; }

int64_t replay_clock(int64_t wall_ms, int64_t total_ms) {
    // Rounded to nearest: warp(total) == total, so the content clock lands on total_ms on time.
    return static_cast<int64_t>(anim::warp(double(wall_ms), double(total_ms), 0.3, 4000) + 0.5);
}

}  // namespace wc
