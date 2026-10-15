// renderer.h - where a finished CrawlGraph goes.
// Real: LoganRenderer (adapters/, BRIDGES). Tests/pipe mode: MockRenderer (mocks/).
#pragma once

#include "core/graph.h"

namespace wc {

struct Renderer {
    virtual ~Renderer() = default;
    virtual void render(const CrawlGraph& g) = 0;
};

}  // namespace wc
