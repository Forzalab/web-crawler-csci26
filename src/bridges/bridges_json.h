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
};

// Node:  color = ROYGBIV by depth (>= 6 stays violet), name = url ("\n" + title if known).
//        rescued -> shape "diamond" + " · via fallback" in the hover text.
// Link:  NEW edge -> steelblue, thickness 1.  SEEN edge (back-link) -> dark, thickness 4.5.
//        Self-loops and duplicate edges are dropped.
std::string bridges_json(const CrawlGraph& g, const BridgesMeta& meta = {});

}  // namespace wc
