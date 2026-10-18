// tree.h - the crawl's spanning tree, shared by the radial layout, the graph's
// level of detail and the BRIDGES subset. Pure: no I/O, no deps.
// Tree = the first NEW edge into each node, from a smaller id (discovery
// order), so it is acyclic even on odd input.
#pragma once

#include <vector>

#include "core/graph.h"

namespace wc {

// Per node id: its tree parent, -1 for the root and for orphans (no NEW edge in).
std::vector<int> tree_parents(const CrawlGraph& g);

// Per node id: leaves in its subtree (a leaf counts 1). Orphans hang off the root.
std::vector<int> subtree_leaves(const std::vector<int>& parent);

}  // namespace wc
