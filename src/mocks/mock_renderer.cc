#include "mocks/mock_renderer.h"

namespace wc {

void MockRenderer::render(const CrawlGraph& g) {
    if (fmt_ == Fmt::Dot) dot(g);
    else text(g);
}

void MockRenderer::text(const CrawlGraph& g) {
    int failed = 0;
    out_ << "MODE " << to_string(g.mode) << '\n';
    for (size_t k = 0; k < g.fetches.size(); ++k) {
        const Fetch& f = g.fetches[k];
        if (!f.ok) ++failed;
        out_ << "FETCH " << k << ' ' << f.depth << ' ' << f.url << (f.ok ? " OK" : " FAIL") << '\n';
    }
    for (size_t id = 0; id < g.nodes.size(); ++id) {
        const Node& n = g.nodes[id];
        out_ << "NODE " << id << ' ' << n.depth << ' ' << n.url << ' ' << to_string(n.status) << '\n';
    }
    for (const Edge& e : g.edges)
        out_ << "EDGE " << e.from << ' ' << e.to << (e.seen ? " SEEN" : " NEW") << '\n';
    out_ << "NODES " << g.nodes.size() << '\n'
         << "EDGES " << g.edges.size() << '\n'
         << "FETCHES " << g.fetches.size() << '\n'
         << "FAILED " << failed << '\n'
         << "SLEEPS " << (sleeper_ ? sleeper_->calls : 0) << '\n';
}

void MockRenderer::dot(const CrawlGraph& g) {
    // ROYGBIV by depth (spec #3); deeper than 6 stays violet.
    static const char* kColor[] = {"red", "orange", "yellow", "green", "blue", "indigo", "violet"};
    out_ << "digraph crawl {\n";
    for (size_t id = 0; id < g.nodes.size(); ++id) {
        const Node& n = g.nodes[id];
        int c = n.depth < 7 ? n.depth : 6;
        out_ << "  n" << id << " [label=\"" << n.url << "\" style=filled fillcolor=" << kColor[c] << "];\n";
    }
    for (const Edge& e : g.edges)
        out_ << "  n" << g.id_of(e.from) << " -> n" << g.id_of(e.to)
             << (e.seen ? " [penwidth=3]" : "") << ";\n";
    out_ << "}\n";
}

}  // namespace wc
