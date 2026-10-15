// logan_renderer.h - plugs Logan's logan_draw() (src/team/logan.h) into the crawler.
// Teammates never need to open this file.
#pragma once

#include "core/renderer.h"

namespace wc {

class LoganRenderer : public Renderer {
public:
    void render(const CrawlGraph& g) override;
};

}  // namespace wc
