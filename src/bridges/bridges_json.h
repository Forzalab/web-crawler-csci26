// bridges_json.h - our own BRIDGES "GraphAdjacencyList" JSON writer.
// No bridges-cxx code (it has no license). The format matches what the
// bridges-cxx client posts: header keys, then "nodes" and "links".
// Pure string out: no network, no post. The gateway (Q06) does the post.
//
// Deterministic: nodes sorted by URL (id = sorted position), links sorted
// by (source id, target id). Same graph in -> same bytes out.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/graph.h"

namespace wc {

struct BridgesMeta {
    std::string title = "Web crawl";
    std::string description;
    std::unordered_map<std::string, std::string> page_titles;  // url -> <title>, optional
    std::unordered_set<std::string> rescued;                   // urls a fallback provider fetched
    // TUI only (pipe --bridges never sets these, so its bytes never change):
    bool pretty = false;                                       // the look below
    std::unordered_map<std::string, int> site_rank;            // url -> site rank (0 = most pages), pretty only
    std::unordered_map<std::string, std::pair<double, double>> location;  // url -> layout x, y (any unit), pretty only
    std::unordered_map<std::string, std::string> labels;       // url -> short label (hub names), pretty only
};

// Node:  color = ROYGBIV by depth (>= 6 stays violet), name = url ("\n" + title if known).
//        rescued -> shape "diamond" + " · via fallback" in the hover text.
// Link:  ROYGBIV by source depth. NEW edge alpha 0.9, thickness 2. SEEN (back-link) alpha 0.6, thickness 3.5.
//        Self-loops and duplicate edges are dropped.
// Pretty: site constellations: colour + shape by site rank (shapes diamond circle square triangle
//        star cross, then wye), alpha 0.35 if unfetched; the root is a red star, size 60.
//        Size clamp(6 + 4*sqrt(in-links), 6, 60); the top 10 hubs (top_hubs) at least 45, their
//        hover card starts with "★ <label>" (BRIDGES has no per-node label flag, only a global one).
//        Hover card: [title] url, then "depth N · K in-links · fetched|failed|unfetched".
//        location: the layout scaled into +-500, with coord_system_type "window" and window
//        [-550, 550, -550, 550], so BRIDGES pins the nodes instead of running its force layout.
//        Links: rainbow highway (rainbow_path) thickness 6, red -> violet by hop. Other NEW links:
//        target depth colour, alpha 0.8, thickness 1.5. SEEN: source depth colour, alpha 0.5, thickness 2.5.
std::string bridges_json(const CrawlGraph& g, const BridgesMeta& meta = {});

// Per node id: in-links (edges in, self-loops not counted).
std::vector<int> in_links(const CrawlGraph& g);
// The longest tree path, root first: to the deepest node, ties by most in-links, then lowest id.
std::vector<int> rainbow_path(const CrawlGraph& g);
// Up to k non-root nodes with >= 2 in-links, most first, ties by id.
std::vector<int> top_hubs(const CrawlGraph& g, int k = 10);

// At most max_nodes nodes and max_edges edges, the most prominent ones: the root and
// every depth-1 page (the best of them if they alone overflow), then the rest by
// score = 4*in-links + subtree leaves + 4*(max depth - depth), ties by id; each pick
// brings its tree-parent chain, or is skipped if the chain does not fit. Edges among
// kept nodes: every tree edge, then the others in recording order up to max_edges.
// Node order (ids) kept. Within both caps: g unchanged. Pure and deterministic.
CrawlGraph bridges_subset(const CrawlGraph& g, int max_nodes = 1000, int max_edges = 10000);

}  // namespace wc
