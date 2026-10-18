// tree.cc - see tree.h
#include "core/tree.h"

namespace wc {

std::vector<int> tree_parents(const CrawlGraph& g) {
    std::vector<int> parent(g.nodes.size(), -1);
    for (const Edge& e : g.edges) {
        if (e.seen) continue;
        int to = g.id_of(e.to), from = g.id_of(e.from);
        if (to <= 0 || from < 0 || from >= to || parent[size_t(to)] != -1) continue;
        parent[size_t(to)] = from;
    }
    return parent;
}

std::vector<int> subtree_leaves(const std::vector<int>& parent) {
    const int n = int(parent.size());
    std::vector<int> leaves(n, 0);
    std::vector<bool> has_kid(n, false);
    for (int i = 1; i < n; ++i) has_kid[size_t(parent[size_t(i)] >= 0 ? parent[size_t(i)] : 0)] = true;
    // Parents have smaller ids, so a descending sweep sees every kid before its parent.
    for (int i = n - 1; i >= 0; --i) {
        if (!has_kid[size_t(i)]) leaves[size_t(i)] += 1;
        if (i > 0) leaves[size_t(parent[size_t(i)] >= 0 ? parent[size_t(i)] : 0)] += leaves[size_t(i)];
    }
    return leaves;
}

}  // namespace wc
