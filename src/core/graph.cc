#include "core/graph.h"

#include <deque>

namespace wc {

namespace {
std::string edge_key(const std::string& from, const std::string& to) {
    return from + '\n' + to;  // '\n' never appears inside a url token
}
}  // namespace

bool CrawlGraph::has(const std::string& url) const { return index_.count(url) > 0; }

int CrawlGraph::id_of(const std::string& url) const {
    auto it = index_.find(url);
    return it == index_.end() ? -1 : it->second;
}

int CrawlGraph::add_node(const std::string& url, int depth) {
    int id = static_cast<int>(nodes.size());
    nodes.push_back({url, depth, Status::Unfetched});
    index_[url] = id;
    return id;
}

bool CrawlGraph::has_edge(const std::string& from, const std::string& to) const {
    return edge_index_.count(edge_key(from, to)) > 0;
}

void CrawlGraph::add_edge(const std::string& from, const std::string& to, bool seen) {
    edge_index_[edge_key(from, to)] = static_cast<int>(edges.size());
    out_[from].push_back(static_cast<int>(edges.size()));
    edges.push_back({from, to, seen});
}

void CrawlGraph::update_node(const std::string& url, int depth) {
    int id = id_of(url);
    if (id < 0 || depth >= nodes[id].depth) return;
    nodes[id].depth = depth;
    std::deque<int> work{id};  // only strictly smaller depths re-enqueue, so cycles end
    while (!work.empty()) {
        const Node& p = nodes[work.front()];
        work.pop_front();
        auto it = out_.find(p.url);
        if (it == out_.end()) continue;
        int d = p.depth + 1;
        for (int e : it->second) {
            int c = id_of(edges[e].to);
            if (c < 0 || d >= nodes[c].depth) continue;
            nodes[c].depth = d;
            work.push_back(c);
        }
    }
}

void CrawlGraph::record_fetch(const std::string& url, bool ok) {
    int id = id_of(url);
    int depth = id < 0 ? 0 : nodes[id].depth;
    fetches.push_back({url, depth, ok});
    if (id >= 0) nodes[id].status = ok ? Status::Fetched : Status::Failed;
}

const char* to_string(Mode m) { return m == Mode::BFS ? "BFS" : m == Mode::DFS ? "DFS" : "HYBRID"; }

const char* to_string(Status s) {
    switch (s) {
        case Status::Fetched: return "FETCHED";
        case Status::Failed: return "FAILED";
        default: return "UNFETCHED";
    }
}

}  // namespace wc
