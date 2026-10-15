// graph.h - the crawl result. Pure data: no I/O, no network, no deps.
// Everything that prints walks these VECTORS (discovery order), never the
// hash map, so output is deterministic (tests run every binary twice).
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace wc {

enum class Mode { BFS, DFS };

enum class Status { Unfetched, Fetched, Failed };

struct Node {
    std::string url;
    int depth = 0;               // shortest hops from start (decision D6)
    Status status = Status::Unfetched;
};

struct Edge {
    std::string from, to;
    bool seen = false;           // true = `to` was already in the graph (SEEN), false = NEW
};

struct Fetch {                   // one call to LinkSource::links(), in call order
    std::string url;
    int depth = 0;
    bool ok = false;
};

struct CrawlGraph {
    Mode mode = Mode::BFS;
    std::vector<Node> nodes;     // id = index = discovery order, start = 0
    std::vector<Edge> edges;     // recording order
    std::vector<Fetch> fetches;  // fetch order

    bool has(const std::string& url) const;
    int id_of(const std::string& url) const;            // -1 if absent
    int add_node(const std::string& url, int depth);    // returns new id; caller checks has() first
    bool has_edge(const std::string& from, const std::string& to) const;
    void add_edge(const std::string& from, const std::string& to, bool seen);
    void record_fetch(const std::string& url, bool ok); // appends Fetch + sets node status

private:
    std::unordered_map<std::string, int> index_;        // url -> nodes[] position (lookup only)
    std::unordered_map<std::string, int> edge_index_;   // "from\nto" -> edges[] position
};

const char* to_string(Mode m);
const char* to_string(Status s);

}  // namespace wc
