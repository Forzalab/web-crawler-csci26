// scene.cc - see scene.h
#include "ui/scene.h"

#include <algorithm>
#include <climits>
#include <tuple>
#include <cmath>
#include <utility>

#include "core/tree.h"

namespace wc {

namespace {

// Clusters: per parent (id order, never inside another cluster), every other
// site with >= kClusterMin kids. Members and their descendants fold into it.
void find_clusters(Scene& s, const std::vector<std::vector<int>>& kids) {
    const CrawlGraph& g = s.g;
    s.cluster_of.assign(g.nodes.size(), -1);
    for (size_t p = 0; p < g.nodes.size(); ++p) {
        if (s.cluster_of[p] >= 0) continue;
        const std::string own = site_of(g.nodes[p].url);
        std::vector<std::pair<std::string, std::vector<int>>> by_site;  // first-seen order
        for (int k : kids[p]) {
            std::string site = site_of(g.nodes[size_t(k)].url);
            if (site == own) continue;
            auto it = std::find_if(by_site.begin(), by_site.end(), [&](const auto& b) { return b.first == site; });
            if (it == by_site.end()) by_site.push_back({site, {k}});
            else it->second.push_back(k);
        }
        for (auto& [site, members] : by_site) {
            if (int(members.size()) < kClusterMin) continue;
            Cluster c;
            c.parent = int(p);
            c.members = members;
            const int idx = int(s.clusters.size());
            std::vector<int> stack = members;
            double cx = 0, cy = 0, r = 0;
            for (int m : members) {
                const Pt& q = s.lay.pos[size_t(m)];
                double len = std::hypot(q.x, q.y);
                if (len > 0) cx += q.x / len, cy += q.y / len;
                r += len;
            }
            while (!stack.empty()) {
                int n = stack.back();
                stack.pop_back();
                s.cluster_of[size_t(n)] = idx;
                ++c.count;
                for (int k : kids[size_t(n)]) stack.push_back(k);
            }
            r /= double(members.size());
            double a = std::atan2(cy, cx);
            c.pos = Pt{r * std::cos(a), r * std::sin(a)};
            c.label = site + " ×" + std::to_string(c.count);
            s.clusters.push_back(std::move(c));
        }
    }
}

}  // namespace

Scene make_scene(const CrawlGraph& g) { return make_scene(g, radial_layout(g)); }

Scene make_scene(const CrawlGraph& g, Layout lay) {
    Scene s;
    s.g = g;
    s.lay = std::move(lay);
    s.plan = replay_plan(g);
    const size_t n = g.nodes.size();
    if (s.lay.parent.size() != n) s.lay.parent = tree_parents(g);
    s.leaves = subtree_leaves(s.lay.parent);
    s.indeg.assign(n, 0);
    std::vector<std::vector<int>> kids(n);
    for (size_t i = 1; i < n; ++i)
        if (s.lay.parent[i] >= 0) kids[size_t(s.lay.parent[i])].push_back(int(i));
    std::vector<int> tree, rest;
    for (size_t e = 0; e < g.edges.size(); ++e) {
        int a = s.plan.from[e], b = s.plan.to[e];
        if (b >= 0) ++s.indeg[size_t(b)];
        bool is_tree = !g.edges[e].seen && b > 0 && a >= 0 && s.lay.parent[size_t(b)] == a;
        (is_tree ? tree : rest).push_back(int(e));
    }
    auto deep = [&](int e) { return g.nodes[size_t(s.plan.to[size_t(e)])].depth > 1; };
    std::sort(tree.begin(), tree.end(), [&](int x, int y) {
        int lx = s.leaves[size_t(s.plan.to[size_t(x)])], ly = s.leaves[size_t(s.plan.to[size_t(y)])];
        return std::make_tuple(deep(x), -lx, x) < std::make_tuple(deep(y), -ly, y);
    });
    auto in = [&](int e) { int b = s.plan.to[size_t(e)]; return b >= 0 ? s.indeg[size_t(b)] : 0; };
    std::sort(rest.begin(), rest.end(), [&](int x, int y) { return std::make_pair(-in(x), x) < std::make_pair(-in(y), y); });
    s.n_tree = int(tree.size());
    s.n_always = int(std::count_if(tree.begin(), tree.end(), [&](int e) { return !deep(e); }));
    s.order = std::move(tree);
    s.order.insert(s.order.end(), rest.begin(), rest.end());
    find_clusters(s, kids);
    return s;
}

int lod_budget(int cols, int rows, double zoom) {
    return int(std::max(24, cols * rows / 40) * std::clamp(zoom, 0.25, 16.0));
}

int lod_label_depth(double zoom) { return zoom < 2.5 ? 1 : zoom < 5 ? 2 : INT_MAX; }

}  // namespace wc
