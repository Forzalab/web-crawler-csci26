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
Status = Unfetched | Fetched | ,aFailed

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
    if (cfg.workers > 1 && cfg.mode == Mode::BFS) return resume_crawl(std::move(g), cfg, src);  // TUI only
    
    // run from root
    if (g.mode == Mode::BFS) return Crawler::crawl_bfs(start, cfg, src, sleeper, g);
    else if (g.mode == Mode::DFS) return Crawler::crawl_dfs(start, cfg, src, sleeper, g);    

    // default failure: only start node goes here - wait for testcaee behavior
    g.record_fetch(start, false);
    return g;
}

CrawlGraph& Crawler::crawl_bfs(const std::string& root_url, CrawlConfig cfg, LinkSource& src, Sleeper& sleeper, CrawlGraph& g) {
    
    std::deque<std::string> todo;
    std::unordered_map<std::string, bool> processed;
    
    std::string url = root_url;
    todo.push_back(url);    

    cfg.__size = 1; //init

            
    while (!todo.empty() && cfg.__size <= cfg.maxSize) {
        // ---- processing section ----
        // READ THE ACTUAL DEPTH OF EACH NODE, FUCKER
        url = todo.front();
        todo.pop_front();

        int id = g.id_of(url);
        auto root_node = g.nodes[id];
        
        if (root_node.depth >= cfg.maxDepth) {
            processed.insert({url, true});
            continue;
        }
        
        auto children = src.links(url);

        if (url != root_url)
            sleeper.sleep_ms(cfg.delay_ms);
                
        // return if childrbe nit found
        if (children == std::nullopt) {
            g.record_fetch(url, false);
            processed.insert({url, false}); // fail state eqse
            continue;
        }

        g.record_fetch(url, true);
        
        auto& children_vec = *children;
        for (const auto& child_url : children_vec) {
            bool seen_edge = g.has_edge(url, child_url);
            bool seen_node = g.has(child_url);

            if (cfg.__size >= cfg.maxSize && !seen_node) continue; //only concern nodes
            
            // edge
            if (!seen_edge)
                g.add_edge(url, child_url, seen_node);
                            
            // node
            if (seen_node) continue; // dont add ezisting nodes

            g.add_node(child_url, root_node.depth + 1);
            todo.push_back(child_url);
            cfg.__size++;
        }

        // endloop incr
        processed.insert({url, true});
    }

    return g;
}

CrawlGraph& Crawler::crawl_dfs(const std::string& root_url, CrawlConfig cfg, LinkSource& src, Sleeper& sleeper, CrawlGraph& g) {
    // one link many branches, so crawl parebt first, then pricess children, run func on EACH children.


    if (cfg.cancel && *cfg.cancel) return g;  // TUI Esc: the graph so far (null = spec path)

    // max depth check - compare global vs local
    if (cfg.__depth >= cfg.maxDepth)
        return g;

    // root doesnt sleep
    if (cfg.__depth > 0)
        sleeper.sleep_ms(cfg.delay_ms);
    
    auto children = src.links(root_url);
        // return if childrbe nit found
    if (children == std::nullopt) {
        g.record_fetch(root_url, false);
        return g;
    }

    g.record_fetch(root_url, true);
    
    auto& children_vec = *children;

    // setup config for child crawl
    cfg.__depth++;

    for (const auto& child_url : children_vec) {
       bool seen_edge = g.has_edge(root_url, child_url);
       bool seen_node = g.has(child_url);
       const int& new_child_depth = cfg.__depth;

       if (!seen_node && int(g.nodes.size()) >= cfg.maxSize) continue; // size cap, same rule as crawl_bfs

       // chexk if a CONNECTION existed and record accordingly       
       // record once, bcs claude spec lol
    //       bool seen_node = (seen_edge || url_exist_already);
       if (!seen_edge)
           g.add_edge(root_url, child_url, seen_node);

       // now onto the nodes...
       if (seen_node) {
            // push pripagation + min serking
            int index = g.id_of(child_url);
            int stored_child_depth = g.nodes[index].depth;
            int true_depth = std::min(stored_child_depth, new_child_depth);
            g.update_node(child_url, true_depth);
            continue;
       } // skip node-adding ritual for seen nodes

       g.add_node(child_url, new_child_depth);

       g = Crawler::crawl_dfs(child_url, cfg, src, sleeper, g); // update graph with new state
    }
    
    return g;
}

}  // namespace wc
