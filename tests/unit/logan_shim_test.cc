// logan_shim_test.cc - our BRIDGES shim (src/team/bridges/) driven the way Logan's PR #45 logan.cc does.
// No network: checks Bridges::to_json(); visualize() only with WC_GATEWAY_URL unset.
// Run: build/wc_logan_shim_test   (exit 1 if any check fails)
#include <cstdlib>
#include <iostream>

#include "Bridges.h"
#include "Color.h"
#include "GraphAdjList.h"
#include "team/logan.h"

using namespace bridges;

static int failures = 0, checks = 0;

#define CHECK_EQ(got, want)                                                                         \
    do {                                                                                            \
        ++checks;                                                                                   \
        auto g_ = (got);                                                                            \
        auto w_ = (want);                                                                           \
        if (!(g_ == w_)) {                                                                          \
            ++failures;                                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #got "\n  got:  " << g_     \
                      << "\n  want: " << w_ << "\n";                                                \
        }                                                                                           \
    } while (0)

static const std::string kHead =
    "{\"visual\":\"GraphAdjacencyList\",\"title\":\"Web crawl (Logan)\",\"description\":\"\",\"map_overlay\":false,"
    "\"map\":\"\",\"element_label_flag\":false,\"link_label_flag\":false,\"coord_system_type\":\"cartesian\",";

// Logan's calls (PR #45 logan.cc), JSON instead of visualize().
static std::string logan_json(const vector<LoganNode>& nodes, const vector<LoganEdge>& edges) {
    bridges::GraphAdjList<int, LoganNode, LoganEdge> graph;
    for (int i = 0; i < int(nodes.size()); i++) {
        graph.addNode(i, nodes.at(i));
        if (nodes.at(i).depth == 0) graph.getNode(i)->getVisualizer()->setColor(Color("red"));
        else if (nodes.at(i).depth == 1) graph.getNode(i)->getVisualizer()->setColor(Color("orange"));
        else if (nodes.at(i).depth == 2) graph.getNode(i)->getVisualizer()->setColor(Color("yellow"));
        else if (nodes.at(i).depth == 3) graph.getNode(i)->getVisualizer()->setColor(Color("green"));
        else graph.getNode(i)->getVisualizer()->setColor(Color("blue"));
        if (nodes.at(i).failed) graph.getNode(i)->getVisualizer()->setShape(Shape::TRIANGLE);
        graph.getNode(i)->setLabel(nodes.at(i).url);
    }
    for (const auto& edge : edges) {
        graph.addEdge(edge.from, edge.to, edge);
        if (edge.back_link) graph.addEdge(edge.from, edge.to, edge)->getVisualizer()->setThickness(5.0F);
    }
    Bridges bridges(6742069, "", "");
    bridges.setDataStructure(&graph);
    return bridges.to_json();
}

static void test_logan_example() {  // the example in logan.cc's TODO
    std::string got = logan_json({{"A", 0, false}, {"B", 1, false}, {"C", 1, true}},
                                 {{0, 1, false}, {0, 2, false}, {1, 0, true}});
    CHECK_EQ(got, kHead +
                      "\"nodes\":["
                      "{\"color\":[255,0,0,1.0],\"shape\":\"circle\",\"size\":10.0,\"name\":\"A\"},"
                      "{\"color\":[255,165,0,1.0],\"shape\":\"circle\",\"size\":10.0,\"name\":\"B\"},"
                      "{\"color\":[255,165,0,1.0],\"shape\":\"triangle\",\"size\":10.0,\"name\":\"C\"}],"
                      "\"links\":["
                      "{\"color\":[70,130,180,1.0],\"thickness\":1.0,\"source\":\"0\",\"target\":\"1\"},"
                      "{\"color\":[70,130,180,1.0],\"thickness\":1.0,\"source\":\"0\",\"target\":\"2\"},"
                      "{\"color\":[70,130,180,1.0],\"thickness\":5.0,\"source\":\"1\",\"target\":\"0\"}]}");
}

static void test_depth_colors() {  // 2 yellow, 3 green, 4+ blue
    std::string got = logan_json({{"y", 2, false}, {"g", 3, false}, {"b", 7, false}}, {});
    CHECK_EQ(got, kHead +
                      "\"nodes\":["
                      "{\"color\":[255,255,0,1.0],\"shape\":\"circle\",\"size\":10.0,\"name\":\"y\"},"
                      "{\"color\":[0,128,0,1.0],\"shape\":\"circle\",\"size\":10.0,\"name\":\"g\"},"
                      "{\"color\":[0,0,255,1.0],\"shape\":\"circle\",\"size\":10.0,\"name\":\"b\"}],"
                      "\"links\":[]}");
}

static void test_real_api() {  // addVertex / getVertex / getLinkVisualizer, Color(r,g,b,a), setSize, quoting
    GraphAdjList<std::string, int, int> g;
    g.addVertex("a", 1);
    g.addVertex("b", 2);
    g.getVertex("a")->setLabel("say \"hi\"\n");
    g.getVertex("b")->getVisualizer()->setSize(25);
    g.getVertex("b")->getVisualizer()->setShape(Shape::STAR);
    g.getVertex("b")->getVisualizer()->setColor(Color(0, 0, 0, 51));
    g.addEdge("a", "b");
    g.addEdge("a", "gone");  // no such vertex: left out
    g.getLinkVisualizer("a", "b")->setColor(Color("grey"));
    CHECK_EQ(g.getVertex("nope") == nullptr, true);
    CHECK_EQ(g.getLinkVisualizer("b", "a") == nullptr, true);
    CHECK_EQ(g.nodes_links_json(), std::string(
                                       "\"nodes\":["
                                       "{\"color\":[70,130,180,1.0],\"shape\":\"circle\",\"size\":10.0,\"name\":\"say \\\"hi\\\"\\n\"},"
                                       "{\"color\":[0,0,0,0.2],\"shape\":\"star\",\"size\":25.0,\"name\":\"\"}],"
                                       "\"links\":["
                                       "{\"color\":[128,128,128,1.0],\"thickness\":1.0,\"source\":\"0\",\"target\":\"1\"}]"));
}

static void test_no_gateway() {  // visualize() without WC_GATEWAY_URL: stderr line, no crash
    unsetenv("WC_GATEWAY_URL");
    GraphAdjList<int> g;
    g.addVertex(0);
    Bridges b(1, "", "");
    b.setDataStructure(&g);
    b.visualize();
    CHECK_EQ(true, true);
}

int main() {
    test_logan_example();
    test_depth_colors();
    test_real_api();
    test_no_gateway();
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
