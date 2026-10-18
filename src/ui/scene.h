// scene.h - everything the graph view needs that only changes when the graph
// does: the finished crawl, its layout and its replay plan. Built once per
// crawl (finish_crawl, also after /resume) and shared with the render lambda,
// so a frame never copies the graph. Pure: no FTXUI.
#pragma once

#include "core/graph.h"
#include "ui/layout.h"
#include "ui/replay.h"

namespace wc {

struct Scene {
    CrawlGraph g;
    Layout lay;
    ReplayPlan plan;  // plan.from / plan.to: edge endpoints as node ids
};

Scene make_scene(const CrawlGraph& g);
Scene make_scene(const CrawlGraph& g, Layout lay);  // given layout (tests)

}  // namespace wc
