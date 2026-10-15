#include "adapters/logan_renderer.h"

#include "team/logan.h"

namespace wc {

// CrawlGraph -> plain vectors: node position = node id, edges point at positions.
void LoganRenderer::render(const CrawlGraph& g) {
    std::vector<LoganNode> nodes;
    for (const Node& n : g.nodes)
        nodes.push_back({n.url, n.depth, n.status == Status::Failed});

    std::vector<LoganEdge> edges;
    for (const Edge& e : g.edges) {
        int from = g.id_of(e.from), to = g.id_of(e.to);
        if (from >= 0 && to >= 0) edges.push_back({from, to, e.seen});
    }
    logan_draw(nodes, edges);
}

}  // namespace wc
