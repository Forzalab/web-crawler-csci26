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
// Pretty mode: node colour per site rank (most pages first), the rest grey. No red: red is the root.
const Rgba kSite[] = {{31, 119, 180, 255}, {44, 160, 44, 255}, {148, 103, 189, 255}, {255, 127, 14, 255},
                      {23, 190, 207, 255}, {227, 119, 194, 255}, {127, 127, 127, 255}};

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

std::vector<int> in_links(const CrawlGraph& g) {
    std::vector<int> in(g.nodes.size(), 0);
    for (const Edge& e : g.edges)
        if (int t = g.id_of(e.to); t >= 0 && e.from != e.to && g.has(e.from)) ++in[size_t(t)];
    return in;
}

std::vector<int> rainbow_path(const CrawlGraph& g) {
    if (g.nodes.empty()) return {};
    const std::vector<int> parent = tree_parents(g), in = in_links(g);
    std::vector<int> hops(g.nodes.size(), -1);  // tree hops from the root, -1 = not under it
    hops[0] = 0;
    int end = 0;
    for (size_t i = 1; i < g.nodes.size(); ++i) {
        if (parent[i] < 0 || hops[size_t(parent[i])] < 0) continue;  // parents have smaller ids
        hops[i] = hops[size_t(parent[i])] + 1;
        if (std::make_pair(hops[i], in[i]) > std::make_pair(hops[size_t(end)], in[size_t(end)])) end = int(i);
    }
    std::vector<int> path;
    for (int v = end; v >= 0; v = parent[size_t(v)]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<int> top_hubs(const CrawlGraph& g, int k) {
    const std::vector<int> in = in_links(g);
    std::vector<int> ids;
    for (size_t i = 1; i < g.nodes.size(); ++i)
        if (in[i] >= 2) ids.push_back(int(i));
    std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return in[size_t(a)] > in[size_t(b)]; });
    if (int(ids.size()) > k) ids.resize(size_t(std::max(0, k)));
    return ids;
}

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

    // Pretty only: in-links, the rainbow highway (hop on it per node id, -1 = off it), hubs,
    // and the layout scaled into a +-500 window (BRIDGES pins located nodes in "window" mode).
    std::vector<int> in, hop_on(g.nodes.size(), -1);
    std::unordered_set<int> hubs;
    double scale = 0;
    if (meta.pretty) {
        in = in_links(g);
        std::vector<int> path = rainbow_path(g);
        for (size_t h = 0; h < path.size(); ++h) hop_on[size_t(path[h])] = int(h);
        for (int h : top_hubs(g)) hubs.insert(h);
        double far = 0;
        for (const Node& n : g.nodes)
            if (auto p = meta.location.find(n.url); p != meta.location.end())
                far = std::max({far, std::abs(p->second.first), std::abs(p->second.second)});
        if (!meta.location.empty()) scale = far > 0 ? 500 / far : 1;
    }

    std::string out = "{\"visual\":\"GraphAdjacencyList\",\"title\":" + quote(meta.title) +
                      ",\"description\":" + quote(meta.description) +
                      ",\"map_overlay\":false,\"map\":\"\",\"element_label_flag\":false,"
                      "\"link_label_flag\":false,\"coord_system_type\":" +
                      (scale > 0 ? "\"window\",\"window\":[-550.0,550.0,-550.0,550.0]" : "\"cartesian\"") + ",\"nodes\":[";

    for (size_t k = 0; k < order.size(); ++k) {
        const int i = order[k];
        const Node& n = g.nodes[size_t(i)];
        bool rescued = meta.rescued.count(n.url) > 0;
        auto t = meta.page_titles.find(n.url);
        const std::string title = t != meta.page_titles.end() ? t->second : "";
        if (k) out += ",";
        if (!meta.pretty) {
            std::string name = n.url;
            if (!title.empty()) name += "\n" + title;
            if (rescued) name += " \xC2\xB7 via fallback";
            out += "{\"color\":" + color(kHop[std::clamp(n.depth, 0, 6)]) + ",\"shape\":\"" +
                   (rescued ? "diamond" : "circle") + "\",\"size\":10.0,\"name\":" + quote(name) + "}";
            continue;
        }
        // Site constellations: colour + shape by site rank; the root is a big red star.
        auto r = meta.site_rank.find(n.url);
        int rank = r == meta.site_rank.end() || r->second < 0 ? 6 : std::min(r->second, 6);
        const bool root = i == 0, hub = hubs.count(i) > 0;
        double size = std::round(60 + 40 * std::sqrt(double(in[size_t(i)]))) / 10;  // 6 + 4*sqrt, 0.1 steps
        size = root ? 60.0 : std::clamp(size, hub ? 45.0 : 6.0, 60.0);
        std::string col = root ? color(kHop[0], 1.0) : color(kSite[rank], n.status == Status::Unfetched ? 0.35 : 1.0);
        std::string loc;
        if (auto p = meta.location.find(n.url); scale > 0 && p != meta.location.end())
            loc = ",\"location\":[" + num(std::round(p->second.first * scale * 10) / 10) + "," +
                  num(std::round(p->second.second * scale * 10) / 10) + "]";
        // Hover card: [★ label] [title] url, then depth · in-links · status.
        std::string name;
        if (hub) {
            auto l = meta.labels.find(n.url);
            name = "\xE2\x98\x85 " + (l != meta.labels.end() ? l->second : n.url) + "\n";
        }
        if (!title.empty()) name += title + "\n";
        name += n.url + (rescued ? " \xC2\xB7 via fallback" : "") + "\ndepth " + std::to_string(n.depth) + " \xC2\xB7 " +
                std::to_string(in[size_t(i)]) + " in-links \xC2\xB7 " +
                (n.status == Status::Fetched ? "fetched" : n.status == Status::Failed ? "failed" : "unfetched");
        out += "{\"color\":" + col + loc + ",\"shape\":\"" + (root ? "star" : kSiteShape[rank]) + "\",\"size\":" +
               num(size) + ",\"name\":" + quote(name) + "}";
    }
    out += "],\"links\":[";

    for (size_t k = 0; k < links.size(); ++k) {
        const Link& l = links[k];
        if (k) out += ",";
        const int a = order[size_t(l.src)], b = order[size_t(l.dst)];
        std::string look;
        if (!meta.pretty)
            look = color(l.seen ? kBackEdge : kTreeEdge) + ",\"thickness\":" + num(l.seen ? 4.5 : 1.0);
        else if (!l.seen && hop_on[size_t(b)] > 0 && hop_on[size_t(a)] == hop_on[size_t(b)] - 1)  // rainbow highway
            look = color(kHop[std::min(hop_on[size_t(a)], 6)], 1.0) + ",\"thickness\":" + num(6.0);
        else if (l.seen)
            look = color(kGrey, 0.3) + ",\"thickness\":" + num(0.5);
        else
            look = color(kHop[std::clamp(g.nodes[size_t(b)].depth, 0, 6)], 0.8) + ",\"thickness\":" + num(1.5);
        out += "{\"color\":" + look + ",\"source\":\"" + std::to_string(l.src) + "\",\"target\":\"" +
               std::to_string(l.dst) + "\"}";
    }
    return out + "]}";
}

CrawlGraph bridges_subset(const CrawlGraph& g, int max_nodes, int max_edges) {
    const int n = int(g.nodes.size());
    if (n <= max_nodes && int(g.edges.size()) <= max_edges) return g;
    const std::vector<int> parent = tree_parents(g), leaves = subtree_leaves(parent);
    const std::vector<int> indeg = in_links(g);
    int max_depth = 0;
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
