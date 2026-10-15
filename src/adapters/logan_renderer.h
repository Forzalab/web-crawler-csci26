// logan_renderer.h - adapter for part #3 (Logan: draw the graph on BRIDGES).
// Logan's own signature and any bridges headers may only appear in
// logan_renderer.cc next to `// WIRE:`.
#pragma once

#include "core/renderer.h"

namespace wc {

class LoganRenderer : public Renderer {
public:
    void render(const CrawlGraph& g) override;
};

}  // namespace wc
