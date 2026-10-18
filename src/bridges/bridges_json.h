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
};

// Node:  color = ROYGBIV by depth (>= 6 stays violet), name = url ("\n" + title if known).
//        rescued -> shape "diamond" + " · via fallback" in the hover text.
// Link:  NEW edge -> steelblue, thickness 1.  SEEN edge (back-link) -> dark, thickness 4.5.
//        Self-loops and duplicate edges are dropped.
// Pretty: node alpha 0.35 if unfetched; size 60 for the root, else clamp(6 + 4*sqrt(in-links), 6, 60);
//        shape by site rank: diamond circle square triangle star cross, then wye.
//        NEW link: its target's depth colour, alpha 0.8, thickness 1.5. SEEN: grey, alpha 0.3, 0.5.
std::string bridges_json(const CrawlGraph& g, const BridgesMeta& meta = {});

// At most max_nodes nodes and max_edges edges, the most prominent ones: the root and
// every depth-1 page (the best of them if they alone overflow), then the rest by
// score = 4*in-links + subtree leaves + 4*(max depth - depth), ties by id; each pick
// brings its tree-parent chain, or is skipped if the chain does not fit. Edges among
// kept nodes: every tree edge, then the others in recording order up to max_edges.
// Node order (ids) kept. Within both caps: g unchanged. Pure and deterministic.
CrawlGraph bridges_subset(const CrawlGraph& g, int max_nodes = 1000, int max_edges = 10000);

}  // namespace wc
