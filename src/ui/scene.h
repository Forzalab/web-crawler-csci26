// scene.h - everything the graph view needs that only changes when the graph
// does: the finished crawl, its layout, its replay plan and its level of
// detail. Built once per crawl (finish_crawl, also after /resume) and shared
// with the render lambda, so a frame never copies the graph. Pure: no FTXUI.
//
// Level of detail, highways first (like a map): tree edges into big subtrees
// draw before small ones, SEEN back-links only once every visible tree edge
// fits the budget (and then only kSeenShare of what is left), which grows with
// screen size and zoom in (zoom out keeps the fit budget).
#pragma once

#include <string>
#include <vector>

#include "core/graph.h"
#include "ui/layout.h"
#include "ui/replay.h"

namespace wc {

// >= kClusterMin kids of one site (Layout::site) under a parent of another site fold into one
// glyph + "site ×count" while zoomed out (zoom < kClusterZoom).
constexpr int kClusterMin = 6;
constexpr double kClusterZoom = 2;

struct Cluster {
    int parent = -1;           // node id whose kids these are
    std::vector<int> members;  // the kids, id order
    int count = 0;             // members + all their descendants
    Pt pos;                    // world: members' mean direction, on their ring
    std::string label;         // "ext.org ×7"
};

struct Scene {
    CrawlGraph g;
    Layout lay;
    ReplayPlan plan;              // plan.from / plan.to: edge endpoints as node ids
    std::vector<int> leaves;      // per node: leaves in its subtree
    std::vector<int> indeg;       // per node: edges in (NEW + SEEN)
    std::vector<int> order;       // edge ids, draw order (see make_scene)
    int n_always = 0;             // order[0, n_always): tree edges to depth <= 1, never culled
    int n_tree = 0;               // order[0, n_tree): tree edges; the rest are SEEN (and odd NEW)
    std::vector<Cluster> clusters;
    std::vector<int> cluster_of;  // per node: cluster index it folds into, -1 = none
    // Volcano replay (views::graph): when each node lands (content ms, -1 never / root),
    // when each depth ring starts erupting (first edge into it, -1 none), deepest node.
    std::vector<double> land;
    std::vector<double> ring_start;
    int max_depth = 0;
};

// order = tree edges by (child depth > 1, child leaves desc, id), then the
// other edges by (indeg[to] desc, id).
Scene make_scene(const CrawlGraph& g);
Scene make_scene(const CrawlGraph& g, Layout lay);  // given layout (tests)

// Edges drawn past n_always: max(24, cols*rows/40) * clamp(zoom, 1, 16). cols x rows = canvas cells.
// Zooming out never shrinks it: edges get shorter, the screen holds more of them.
int lod_budget(int cols, int rows, double zoom);
// SEEN back-links get this share of the budget the tree edges left (grey edges stay sparse).
constexpr double kSeenShare = 0.25;
// Deepest labelled depth: < 2.5 -> 1, < 5 -> 2, else every depth.
int lod_label_depth(double zoom);

}  // namespace wc
