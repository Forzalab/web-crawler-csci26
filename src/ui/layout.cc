// layout.cc - see layout.h
#include "ui/layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace wc {

Layout radial_layout(const CrawlGraph& g) {
    const int n = static_cast<int>(g.nodes.size());
    Layout l;
    l.pos.assign(n, Pt{});
    l.parent.assign(n, -1);
    if (n == 0) return l;

    // Tree: first NEW edge into each node. Require from-id < to-id (discovery
    // order) so the tree is acyclic even on odd input.
    for (const Edge& e : g.edges) {
        if (e.seen) continue;
        int to = g.id_of(e.to), from = g.id_of(e.from);
        if (to <= 0 || from < 0 || from >= to || l.parent[to] != -1) continue;
        l.parent[to] = from;
    }

    // Children in id order; orphans (non-root, no parent) hang off root's wedge.
    std::vector<std::vector<int>> kids(n);
    for (int i = 1; i < n; ++i) kids[l.parent[i] >= 0 ? l.parent[i] : 0].push_back(i);

    // Leaf counts: parents have smaller ids, so a descending sweep suffices.
    std::vector<int> leaves(n, 0);
    for (int i = n - 1; i >= 0; --i) {
        if (kids[i].empty()) leaves[i] = 1;
        for (int c : kids[i]) leaves[i] += leaves[c];
    }

    const double kTau = 2.0 * M_PI;
    std::vector<double> lo(n, 0.0), span(n, 0.0);
    span[0] = kTau;
    for (int i = 0; i < n; ++i) {  // ascending: parent wedge known before kids
        int total = 0;
        for (int c : kids[i]) total += leaves[c];
        double a = lo[i];
        for (int c : kids[i]) {
            span[c] = total > 0 ? span[i] * leaves[c] / total : 0.0;
            lo[c] = a;
            a += span[c];
        }
        if (i == 0) continue;
        double ang = lo[i] + span[i] / 2.0, r = g.nodes[i].depth;
        l.pos[i] = Pt{r * std::cos(ang), r * std::sin(ang)};
    }
    return l;
}

View fit(const Layout& l, int w, int h) {
    View v;
    if (l.pos.empty()) {
        v.ox = w / 2.0;
        v.oy = h / 2.0;
        return v;
    }
    double minx = l.pos[0].x, maxx = minx, miny = l.pos[0].y, maxy = miny;
    for (const Pt& p : l.pos) {
        minx = std::min(minx, p.x), maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y), maxy = std::max(maxy, p.y);
    }
    const double m = 0.08, bw = maxx - minx, bh = maxy - miny;
    const double aw = w * (1 - 2 * m), ah = h * (1 - 2 * m);
    double s = 1.0;
    if (bw > 0 && bh > 0) s = std::min(aw / bw, ah / bh);
    else if (bw > 0) s = aw / bw;
    else if (bh > 0) s = ah / bh;
    if (!(s > 0)) s = 1.0;
    v.scale = s;
    v.ox = w / 2.0 - (minx + maxx) / 2.0 * s;
    v.oy = h / 2.0 - (miny + maxy) / 2.0 * s;
    return v;
}

std::string short_label(const std::string& url) {
    std::string s = url;
    size_t p = s.find("://");
    if (p != std::string::npos) s = s.substr(p + 3);
    size_t end = s.find_first_of("/?#");
    if (end != std::string::npos) s = s.substr(0, end);
    size_t at = s.rfind('@');
    if (at != std::string::npos) s = s.substr(at + 1);
    if (!s.empty() && s[0] == '[') {  // IPv6 literal: keep brackets, drop port
        size_t rb = s.find(']');
        if (rb != std::string::npos) s = s.substr(0, rb + 1);
    } else {
        size_t c = s.find(':');
        if (c != std::string::npos) s = s.substr(0, c);
    }
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (s.rfind("www.", 0) == 0) s = s.substr(4);
    return s;
}

}  // namespace wc
