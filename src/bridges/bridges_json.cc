#include "bridges/bridges_json.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <vector>

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

std::string color(const Rgba& c) {
    return "[" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
           num(c.a / 255.0) + "]";
}

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
        out += "{\"color\":" + color(kHop[std::clamp(n.depth, 0, 6)]) + ",\"shape\":\"" +
               (rescued ? "diamond" : "circle") + "\",\"size\":10.0,\"name\":" + quote(name) + "}";
    }
    out += "],\"links\":[";

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

    for (size_t k = 0; k < links.size(); ++k) {
        const Link& l = links[k];
        if (k) out += ",";
        out += "{\"color\":" + color(l.seen ? kBackEdge : kTreeEdge) + ",\"thickness\":" +
               num(l.seen ? 4.5 : 1.0) + ",\"source\":\"" + std::to_string(l.src) +
               "\",\"target\":\"" + std::to_string(l.dst) + "\"}";
    }
    return out + "]}";
}

}  // namespace wc
