#include "adapters/logan_renderer.h"

#include <iostream>

// WIRE: #include "<Logan's header>"   (bridges headers live here only)

namespace wc {

void LoganRenderer::render(const CrawlGraph& g) {
    // WIRE: convert CrawlGraph to whatever Logan's function takes and call it.
    //   g.nodes  -> vertices (label = url, color = depth ROYGBIV)
    //   g.edges  -> directed edges (seen == true -> bold back-link)
    // BRIDGES_USER / BRIDGES_KEY come from the environment (.env, gitignored).
    std::cerr << "[LoganRenderer] not wired yet: " << g.nodes.size() << " nodes, "
              << g.edges.size() << " edges\n";
}

}  // namespace wc
