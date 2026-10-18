#include "bridges/bridges_json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/tree.h"

namespace wc {

namespace {

struct Rgba { int r, g, b, a; };

// ROYGBIV, CSS values (spec #3: color by hops from start).
const Rgba kHop[] = {
    {255, 0, 0, 255}, {255, 165, 0, 255}, {255, 255, 0, 255}, {0, 128, 0, 255},
    {0, 0, 255, 255}, {75, 0, 130, 255},  {238, 130, 238, 255},
};
const Rgba kTreeEdge = {70, 130, 180, 255};  // steelblue (BRIDGES default)
const Rgba kBackEdge = {0, 0, 0, 204};       // black, alpha 0.8

// Shortest round-trip form; "10" -> "10.0" so the output looks like the client's.
std::string num(double x) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, x);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s;
}

std::string color(const Rgba& c, double alpha) {
    return "[" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," + num(alpha) + "]";
}
std::string color(const Rgba& c) { return color(c, c.a / 255.0); }

// Pretty mode: shape per site rank (most pages first), the rest share the last one.
const char* const kSiteShape[] = {"diamond", "circle", "square", "triangle", "star", "cross", "wye"};
const Rgba kGrey = {128, 128, 128, 255};

// Length of a valid UTF-8 sequence starting at s[i], 0 if invalid.
size_t utf8_len(const std::string& s, size_t i) {
    unsigned char c = s[i];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (n == 0 || i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k)
        if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x2) return 0;
    return n;
}

// JSON string literal. Bad UTF-8 bytes (scraped titles) become U+FFFD,
// so the server never sees invalid JSON.
std::string quote(const std::string& s) {
    std::string out = "\"";
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        size_t n = utf8_len(s, i);
        if (n == 0) { out += "\xEF\xBF\xBD"; ++i; continue; }
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else if (c < 0x20) { char u[8]; std::snprintf(u, sizeof u, "\\u%04x", c); out += u; }
        else out.append(s, i, n);
        i += n;
    }
    return out + "\"";
}

}  // namespace

std::string bridges_json(const CrawlGraph& g, const BridgesMeta& meta) {
    // id = position in URL-sorted order
    std::vector<int> order(g.nodes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return g.nodes[a].url < g.nodes[b].url; });
    std::unordered_map<std::string, int> id;
    for (size_t k = 0; k < order.size(); ++k) id[g.nodes[order[k]].url] = static_cast<int>(k);

    struct Link { int src, dst; bool seen; };
    std::vector<Link> links;
    for (const Edge& e : g.edges) {
        auto s = id.find(e.from), t = id.find(e.to);
        if (s == id.end() || t == id.end() || s->second == t->second) continue;
        links.push_back({s->second, t->second, e.seen});
    }
    std::stable_sort(links.begin(), links.end(), [](const Link& a, const Link& b) {
        return a.src != b.src ? a.src < b.src : a.dst < b.dst;
    });
    links.erase(std::unique(links.begin(), links.end(),
                            [](const Link& a, const Link& b) { return a.src == b.src && a.dst == b.dst; }),
                links.end());
    std::vector<int> indeg(order.size(), 0);  // pretty: node size
    for (const Link& l : links) ++indeg[size_t(l.dst)];

    std::string out = "{\"visual\":\"GraphAdjacencyList\",\"title\":" + quote(meta.title) +
                      ",\"description\":" + quote(meta.description) +
                      ",\"map_overlay\":false,\"map\":\"\",\"element_label_flag\":false,"
                      "\"link_label_flag\":false,\"coord_system_type\":\"cartesian\",\"nodes\":[";

    for (size_t k = 0; k < order.size(); ++k) {
        const Node& n = g.nodes[order[k]];
        bool rescued = meta.rescued.count(n.url) > 0;
        std::string name = n.url;
        if (auto t = meta.page_titles.find(n.url); t != meta.page_titles.end() && !t->second.empty())
            name += "\n" + t->second;
        if (rescued) name += " \xC2\xB7 via fallback";
        if (k) out += ",";
        const Rgba& hop = kHop[std::clamp(n.depth, 0, 6)];
        if (!meta.pretty) {
            out += "{\"color\":" + color(hop) + ",\"shape\":\"" + (rescued ? "diamond" : "circle") +
                   "\",\"size\":10.0,\"name\":" + quote(name) + "}";
            continue;
        }
        auto r = meta.site_rank.find(n.url);
        int rank = r == meta.site_rank.end() || r->second < 0 ? 6 : std::min(r->second, 6);
        double size = order[k] == 0 ? 60.0 : std::clamp(std::round(60 + 40 * std::sqrt(double(indeg[k]))) / 10, 6.0, 60.0);  // 0.1 steps
        out += "{\"color\":" + color(hop, n.status == Status::Unfetched ? 0.35 : 1.0) + ",\"shape\":\"" +
               kSiteShape[rank] + "\",\"size\":" + num(size) + ",\"name\":" + quote(name) + "}";
    }
    out += "],\"links\":[";

    for (size_t k = 0; k < links.size(); ++k) {
        const Link& l = links[k];
        if (k) out += ",";
        std::string look;
        if (!meta.pretty)
            look = color(l.seen ? kBackEdge : kTreeEdge) + ",\"thickness\":" + num(l.seen ? 4.5 : 1.0);
        else if (l.seen)
            look = color(kGrey, 0.3) + ",\"thickness\":" + num(0.5);
        else
            look = color(kHop[std::clamp(g.nodes[size_t(order[size_t(l.dst)])].depth, 0, 6)], 0.8) + ",\"thickness\":" + num(1.5);
        out += "{\"color\":" + look + ",\"source\":\"" + std::to_string(l.src) + "\",\"target\":\"" +
               std::to_string(l.dst) + "\"}";
    }
    return out + "]}";
}

CrawlGraph bridges_subset(const CrawlGraph& g, int max_nodes, int max_edges) {
    const int n = int(g.nodes.size());
    if (n <= max_nodes && int(g.edges.size()) <= max_edges) return g;
    const std::vector<int> parent = tree_parents(g), leaves = subtree_leaves(parent);
    std::vector<int> indeg(size_t(n), 0);
    int max_depth = 0;
    for (const Edge& e : g.edges)
        if (int t = g.id_of(e.to); t >= 0) ++indeg[size_t(t)];
    for (const Node& v : g.nodes) max_depth = std::max(max_depth, v.depth);
    auto score = [&](int i) { return 4 * indeg[size_t(i)] + leaves[size_t(i)] + 4 * (max_depth - g.nodes[size_t(i)].depth); };
    auto best_first = [&](std::vector<int>& ids) {
        std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return score(a) > score(b); });
    };
    std::vector<bool> keep(size_t(n), false);
    int kept = 0;
    // Pick i with its parent chain (up to the first kept node), if it all fits.
    auto pick = [&](int i) {
        std::vector<int> chain;
        for (int v = i; v >= 0 && !keep[size_t(v)]; v = parent[size_t(v)]) chain.push_back(v);
        if (chain.empty() || kept + int(chain.size()) > max_nodes) return;
        for (int v : chain) keep[size_t(v)] = true;
        kept += int(chain.size());
    };
    std::vector<int> first_hop, rest;
    for (int i = 1; i < n; ++i) (g.nodes[size_t(i)].depth <= 1 ? first_hop : rest).push_back(i);
    if (n > 0) pick(0);
    best_first(first_hop);
    for (int i : first_hop) pick(i);
    best_first(rest);
    for (int i : rest) pick(i);

    CrawlGraph s;
    s.mode = g.mode;
    for (int i = 0; i < n; ++i)
        if (keep[size_t(i)]) s.add_node(g.nodes[size_t(i)].url, g.nodes[size_t(i)].depth), s.nodes.back().status = g.nodes[size_t(i)].status;
    for (const Fetch& f : g.fetches)
        if (int i = g.id_of(f.url); i >= 0 && keep[size_t(i)]) s.fetches.push_back(f);
    // Edges among kept nodes: tree edges always, the others while there is room.
    std::vector<bool> take(g.edges.size(), false);
    int room = max_edges;
    for (size_t e = 0; e < g.edges.size(); ++e) {
        int a = g.id_of(g.edges[e].from), b = g.id_of(g.edges[e].to);
        bool in = a >= 0 && b >= 0 && keep[size_t(a)] && keep[size_t(b)];
        if (in && !g.edges[e].seen && b > 0 && parent[size_t(b)] == a) take[e] = true, --room;
    }
    for (size_t e = 0; e < g.edges.size() && room > 0; ++e) {
        int a = g.id_of(g.edges[e].from), b = g.id_of(g.edges[e].to);
        if (!take[e] && a >= 0 && b >= 0 && keep[size_t(a)] && keep[size_t(b)]) take[e] = true, --room;
    }
    for (size_t e = 0; e < g.edges.size(); ++e)
        if (take[e]) s.add_edge(g.edges[e].from, g.edges[e].to, g.edges[e].seen);
    return s;
}

}  // namespace wc
