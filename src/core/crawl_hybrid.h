// crawl_hybrid.h - third traversal (Claude): breadth-first layers, trie order inside a layer.
// After Zhou & Hansen, "Combining Breadth-First and Depth-First Strategies in Searching
// for Treewidth", IJCAI-09, pp. 640-645 (https://www.ijcai.org/Proceedings/09/Papers/112.pdf):
// the outer loop is breadth-first, layer by layer; a depth-first walk of a prefix tree over
// the layer only decides the order of the expansions inside it, so similar pages (same site,
// same path) are fetched back to back.
#pragma once

#include <string>
#include <vector>

#include "core/crawl.h"

namespace wc {

// Same rules as crawl() (crawl.cc header D1-D9): layer d is fetched completely before
// layer d+1. Size cap as crawl_bfs: a new page is skipped once g holds cfg.maxSize pages,
// so with a cap the trie order decides which last-layer pages make it in.
// cfg.cancel set -> returns the graph so far. cfg.mode and cfg.workers are ignored.
CrawlGraph crawl_hybrid(const std::string& start, const CrawlConfig& cfg, LinkSource& src, Sleeper& sleeper);

// Trie key of a url: host labels reversed (com, wowcube, www), then path segments, then
// "?query" as a last segment. Scheme and fragment do not count; the host is lower-cased.
std::vector<std::string> trie_key(const std::string& url);

// The layer in trie DFS order: children visited in lexicographic order, a prefix before
// its subtree. Same as sorting by trie_key; equal keys tie-break on the full url. Pure.
std::vector<std::string> hybrid_order(std::vector<std::string> urls);

}  // namespace wc
