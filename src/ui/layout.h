// layout.h - where each node sits on screen. Pure: no FTXUI, no I/O.
// Radial tree: root at (0,0), ring radius = depth, each node gets a slice
// (wedge) of its parent's angle, sized by subtree. Tree = NEW edges only.
// Computed once on the FINISHED graph (replay), so positions never jump.
#pragma once

#include <string>
#include <vector>

#include "core/graph.h"

namespace wc {

struct Pt { double x = 0, y = 0; };

struct Layout {
    std::vector<Pt> pos;      // per node id, world units (ring r = depth)
    std::vector<int> parent;  // per node id, -1 for root (tree edge source)
};

Layout radial_layout(const CrawlGraph& g);

struct View { double scale = 1, ox = 0, oy = 0; };  // screen = world*scale + o

// Fit every node inside a w x h canvas (canvas sub-pixels) with a margin.
View fit(const Layout& l, int w, int h);

// "https://www.google.com/maps?x" -> "google.com" (host, no www., no port)
std::string short_label(const std::string& url);

// Graph label: host if url's host differs from root's, else "/" + last
// non-empty path segment ("/domains"); root (or bare path) -> host.
// Capped at 14 columns, "…" on the last one.
std::string node_label(const std::string& url, const std::string& root_url);

}  // namespace wc
