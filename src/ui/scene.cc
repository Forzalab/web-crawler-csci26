// scene.cc - see scene.h
#include "ui/scene.h"

#include <utility>

namespace wc {

Scene make_scene(const CrawlGraph& g) { return make_scene(g, radial_layout(g)); }

Scene make_scene(const CrawlGraph& g, Layout lay) {
    Scene s;
    s.g = g;
    s.lay = std::move(lay);
    s.plan = replay_plan(g);
    return s;
}

}  // namespace wc
