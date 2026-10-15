// ============================================================================
//  TODO(Tony): BFS + DFS traversal goes here
// ============================================================================
//
//  This file is Tony's handcode (#2). Everything else in the repo is
//  scaffolding around it. The stub below compiles and returns a graph with
//  only the start node, so pipe mode prints well-formed (wrong) output and
//  tests/run.sh FAILs cleanly until the real traversal lands.
//
//  CrawlGraph crawl(start, cfg, src, sleeper)
//
//  Inputs
//    start    start URL, non-empty
//    cfg      mode (BFS | DFS), maxDepth >= 0, maxSize >= 1, delay_ms
//    src      src.links(url) -> page links in order, or std::nullopt = fetch failed
//    sleeper  sleeper.sleep_ms(cfg.delay_ms) = the politeness delay
//
//  Output (CrawlGraph, see graph.h; tests read exactly these fields)
//    g.mode      = cfg.mode
//    g.nodes     discovery order, nodes[0] = start at depth 0
//    g.edges     recording order, NEW (discovered `to`) or SEEN (`to` already known)
//    g.fetches   one entry per src.links() call, in call order
//    Helpers: g.has(), g.add_node(), g.has_edge(), g.add_edge(), g.record_fetch().
//
//  Decisions in force (areas/web-crawler.md, DECISIONS)
//    D1  mark a node visited when it is pushed (discovered), not when popped
//    D2  record SEEN edges (edge to a node already in the graph)
//    D3  a self-loop is a SEEN edge
//    D4  a node at depth == maxDepth is recorded but NOT fetched
//    D5  maxSize counts discovered nodes
//    D6  node depth = shortest hops from start
//    D7  DFS = recursive, children in link order
//    D8  sleep BETWEEN fetches (sleeps = fetches - 1)
//    D9  start URL not in the web = 1 failed fetch
//    Each distinct edge (from, to) is recorded once, at its first occurrence.
//    Never throws. Never iterate a hash map to decide order.
//
//  Check yourself:  make && tests/run.sh
// ============================================================================

#include "core/crawl.h"

namespace wc {

CrawlGraph crawl(const std::string& start, const CrawlConfig& cfg,
                 LinkSource& src, Sleeper& sleeper) {
    (void)src;      // TODO(Tony): fetch with src.links(url)
    (void)sleeper;  // TODO(Tony): sleeper.sleep_ms(cfg.delay_ms) between fetches

    CrawlGraph g;
    g.mode = cfg.mode;
    g.add_node(start, 0);
    return g;
}

}  // namespace wc
