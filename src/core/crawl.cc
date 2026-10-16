// ============================================================================
// TODO(Tony): crawl the web from `start`. Put every page and link in g.
//   Mode BFS: use a queue.  Mode DFS: use recursion. Visit children in link order.
//   Get the links of a page: src.links(url).
//     It gives a list of links  -> the fetch is OK.
//     It gives std::nullopt     -> the fetch FAILED. Record it. Do not stop the crawl.
//   Record each fetch:  g.record_fetch(url, ok).  One call for each src.links() call.
//   Wait between fetches: sleeper.sleep_ms(cfg.delay_ms). Do not wait before the first fetch.
//
//   Rules (all of them are tested):
//     D1  Add a page to g when you find it. Do not wait until you take it out.
//     D2  A link to a page that is already in g -> record it as a SEEN edge.
//     D3  A link from a page to itself -> SEEN edge too.
//     D4  A page at depth == cfg.maxDepth goes in g. Do not fetch it.
//     D5  g holds a maximum of cfg.__size pages. Count pages that you found.
//     D6  Depth of a page = smallest number of hops from start.
//     D8  Sleep count = fetch count - 1.
//     D9  start is not in the web -> 1 failed fetch. start stays in g at depth 0.
//     Record each edge (from, to) one time only, at the first time you see it.
//     Never throw. Never loop over a hash map to choose the order.
//
//   Test input (a fake web, typed into stdin):
//     line 1:  MODE START MAXDEPTH MAXSIZE
//     line 2:  P = number of pages
//     P lines: URL K LINK1 ... LINKK      (K = -1 -> this page fails)
//     A URL with no line -> this page fails too.
//
//   Example (tests/crawl/inputfile2):      Expected output (outputfile2):
//     BFS A 5 10                             MODE BFS
//     4                                      FETCH 0 0 A OK
//     A 1 B                                  FETCH 1 1 B OK
//     B 1 C                                  FETCH 2 2 C OK
//     C 1 D                                  FETCH 3 3 D OK
//     D 0                                    NODE 0 0 A FETCHED
//                                            NODE 1 1 B FETCHED
//                                            NODE 2 2 C FETCHED
//                                            NODE 3 3 D FETCHED
//                                            EDGE A B NEW
//                                            EDGE B C NEW
//                                            EDGE C D NEW
//                                            NODES 4  EDGES 3  FETCHES 4  FAILED 0  SLEEPS 3
//
//   Run one test:   make && build/wc < tests/crawl/inputfile2
//   Run all tests:  make && tests/run.sh


/*
CrawlGraph crawl(const std::string& start,   // "A" or "https://x.com"
                 const CrawlConfig& cfg,     // the 4 settings
                 LinkSource& src,            // the fake/real web
                 Sleeper& sleeper)           // the politeness delay

cfg:
Mode mode      Mode::BFS or Mode::DFS
int  maxDepth  default 3
int  maxSize   default 1000   (max nodes)
int  delay_ms  default 100

src.links(url):
in:   const std::string& url
out:  std::optional<std::vector<std::string>>
        has value  -> fetch OK. The vector = links in page order. Can be empty.
        std::nullopt -> fetch FAILED.

sleeper
in: int ms     out: void                 


TODO: return
Mode mode
vector<Node>  nodes     Node  { string url; int depth; Status status; }
vector<Edge>  edges     Edge  { string from, to; bool seen; }
vector<Fetch> fetches   Fetch { string url; int depth; bool ok; }
Status = Unfetched | Fetched | Failed

---- HELPERS ----
bool has(const string& url)
int  id_of(const string& url)                        // -1 if absent
int  add_node(const string& url, int depth)          // returns id. Check has() first.
bool has_edge(const string& from, const string& to)
void add_edge(const string& from, const string& to, bool seen)
void record_fetch(const string& url, bool ok)        // adds a Fetch AND sets the node's status
*/

#include "core/crawl.h"

namespace wc {

CrawlGraph crawl(const std::string& start, const CrawlConfig& cfg,
                 LinkSource& src, Sleeper& sleeper) {
    // TODO(Tony): fetch with src.links(url)
    // TODO(Tony): sleeper.sleep_ms(cfg.delay_ms) between fetches

    CrawlGraph g;
    g.mode = cfg.mode;
    g.add_node(start, 0);
    
    // run from root
    if (g.mode == Mode::BFS) return Crawler::crawl_bfs(start, cfg, src, sleeper, g);
    else if (g.mode == Mode::DFS) return Crawler::crawl_dfs(start, cfg, src, sleeper, g);    

    // default failure: only start node goes here - wait for testcaee behavior
    g.record_fetch(start, false);
    return g;
}

CrawlGraph& Crawler::crawl_bfs(const std::string& root_url, CrawlConfig cfg, LinkSource& src, Sleeper& sleeper, CrawlGraph& g) {
    // on

    return g;
}

CrawlGraph& Crawler::crawl_dfs(const std::string& root_url, CrawlConfig cfg, LinkSource& src, Sleeper& sleeper, CrawlGraph& g) {
    // one link many branches, so crawl parebt first, then pricess children, run func on EACH children.

    // max depth check - compare global vs local
    if (cfg.__depth >= cfg.maxDepth || cfg.__size >= cfg.maxSize)
        return g;

    g.record_fetch(root_url, true);

    // root doesnt sleep
    if (cfg.__depth > 0)
        sleeper.sleep_ms(cfg.delay_ms);
    
    auto children = src.links(root_url);
    
    // return if childrbe nit found
    if (children == std::nullopt) {
        g.record_fetch(root_url, false);
        return g;
    }

    auto& children_vec = *children;

    // setup config for child crawl
    cfg.__depth++;

    for (const auto& child_url : children_vec) {
       bool root_child_connected_already = g.has_edge(root_url, child_url);
       bool url_exist_already = g.has(child_url);
       const int& new_child_depth = cfg.__depth;

       // chexk if a CONNECTION existed and record accordingly       
       bool seen = (root_child_connected_already || url_exist_already);
       g.add_edge(root_url, child_url, seen);

       // now onto the nodes...
       if (url_exist_already) {
           int index = g.id_of(child_url);
           int& curr_child_depth = g.nodes[index].depth;
           curr_child_depth = std::min(curr_child_depth, new_child_depth);
           continue;
       }

       g.add_node(child_url, new_child_depth);
       
       cfg.__size = g.nodes.size(); // dfs bookkeeping, query capacity after each child is crawled
       
       g = Crawler::crawl_dfs(child_url, cfg, src, sleeper, g); // update graph with new state
    }
    
    return g;
}

}  // namespace wc
