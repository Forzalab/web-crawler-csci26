// layout.cc - see layout.h
#include "ui/layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

#include "core/tree.h"

namespace wc {

Layout radial_layout(const CrawlGraph& g) {
    const int n = static_cast<int>(g.nodes.size());
    Layout l;
    l.pos.assign(n, Pt{});
    if (n == 0) return l;
    l.parent = tree_parents(g);  // first NEW edge into each node (core/tree.h)

    // Sites by first appearance: same-site siblings sit side by side (clusters, legend).
    std::vector<std::string> site(n);
    std::unordered_map<std::string, int> first;
    std::vector<int> site_first(n);
    for (int i = 0; i < n; ++i) {
        site[i] = site_of(g.nodes[i].url);
        site_first[i] = first.emplace(site[i], int(first.size())).first->second;
    }
    // Children in id order, then grouped by site (stable); orphans hang off root's wedge.
    std::vector<std::vector<int>> kids(n);
    for (int i = 1; i < n; ++i) kids[l.parent[i] >= 0 ? l.parent[i] : 0].push_back(i);
    for (auto& k : kids)
        std::stable_sort(k.begin(), k.end(), [&](int a, int b) { return site_first[a] < site_first[b]; });
    const std::vector<int> leaves = subtree_leaves(l.parent);

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
    // Shapes: one per site, most nodes first, ties by first appearance.
    std::vector<std::pair<std::string, int>> count(first.size());  // (site, nodes), first-seen order
    for (int i = 0; i < n; ++i) count[site_first[i]].first = site[i], ++count[site_first[i]].second;
    std::vector<int> order(count.size());
    for (size_t k = 0; k < order.size(); ++k) order[k] = int(k);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return count[a].second > count[b].second; });
    std::vector<int> rank(count.size(), -1);
    for (int r = 0; r < int(order.size()) && r < kShapes; ++r) {
        rank[order[r]] = r;
        l.sites.push_back(count[order[r]].first);
    }
    l.shape.resize(n);
    for (int i = 0; i < n; ++i) l.shape[i] = rank[site_first[i]];
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

std::string url_path(const std::string& url) {
    size_t s = url.find("://");
    size_t p = url.find('/', s == std::string::npos ? 0 : s + 3);
    return p == std::string::npos ? "/" : url.substr(p);
}

std::string node_label(const std::string& url, const std::string& root_url) {
    std::string host = short_label(url), lab = host;
    if (url != root_url && host == short_label(root_url)) {
        std::string s = url;
        size_t p = s.find("://");
        if (p != std::string::npos) s = s.substr(p + 3);
        size_t slash = s.find('/'), q = s.find_first_of("?#");
        std::string path;
        if (slash != std::string::npos && (q == std::string::npos || slash < q))
            path = s.substr(slash, q == std::string::npos ? std::string::npos : q - slash);
        while (!path.empty() && path.back() == '/') path.pop_back();
        size_t last = path.rfind('/');
        if (last != std::string::npos && last + 1 < path.size()) lab = path.substr(last);
    }
    const size_t kMax = 14;
    if (lab.size() > kMax) lab = lab.substr(0, kMax - 1) + "\u2026";
    return lab;
}

}  // namespace wc

std::string wc::site_of(const std::string& url) {
    std::string h = short_label(url);
    size_t last = h.rfind('.');
    if (last == std::string::npos || last == 0) return h;
    size_t prev = h.rfind('.', last - 1);
    return prev == std::string::npos ? h : h.substr(prev + 1);
}
