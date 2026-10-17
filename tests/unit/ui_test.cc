// ui_test.cc - unit tests for src/ui/layout + src/ui/replay (pure TUI logic).
// Run: build/wc_ui_test   (exit 1 if any check fails)
#include <cmath>
#include <iostream>

#include "core/graph.h"
#include "ui/layout.h"
#include "ui/replay.h"

using namespace wc;

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

// home -> a, b, c ; a -> a1, a2 ; b -> home (SEEN), a1 (SEEN) ; orphan node "z"
static CrawlGraph fake() {
    CrawlGraph g;
    g.add_node("https://home.com/", 0);
    for (const char* u : {"https://a.com/", "https://b.com/", "https://c.com/"}) {
        g.add_node(u, 1);
        g.add_edge("https://home.com/", u, false);
    }
    g.record_fetch("https://home.com/", true);
    for (const char* u : {"https://a.com/1", "https://a.com/2"}) {
        g.add_node(u, 2);
        g.add_edge("https://a.com/", u, false);
    }
    g.record_fetch("https://a.com/", true);
    g.add_edge("https://b.com/", "https://home.com/", true);
    g.add_edge("https://b.com/", "https://a.com/1", true);
    g.record_fetch("https://b.com/", true);
    g.add_node("https://z.com/", 2);  // no NEW edge into it
    return g;
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static void test_layout() {
    CrawlGraph g = fake();
    Layout l1 = radial_layout(g), l2 = radial_layout(g);
    CHECK_EQ(l1.pos.size(), g.nodes.size());
    bool same = true;
    for (size_t i = 0; i < l1.pos.size(); ++i)
        same = same && l1.pos[i].x == l2.pos[i].x && l1.pos[i].y == l2.pos[i].y;
    CHECK_EQ(same, true);
    CHECK_EQ(l1.parent[0], -1);
    CHECK_EQ(l1.parent[1], 0);
    CHECK_EQ(l1.parent[4], 1);
    CHECK_EQ(l1.parent[5], 1);
    CHECK_EQ(l1.parent[6], -1);  // orphan
    CHECK_EQ(near(l1.pos[0].x, 0) && near(l1.pos[0].y, 0), true);
    for (size_t i = 0; i < g.nodes.size(); ++i)
        CHECK_EQ(near(std::hypot(l1.pos[i].x, l1.pos[i].y), g.nodes[i].depth), true);
    // all positions distinct
    bool distinct = true;
    for (size_t i = 0; i < l1.pos.size(); ++i)
        for (size_t j = i + 1; j < l1.pos.size(); ++j)
            if (near(l1.pos[i].x, l1.pos[j].x) && near(l1.pos[i].y, l1.pos[j].y)) distinct = false;
    CHECK_EQ(distinct, true);
}

static void test_fit() {
    CrawlGraph g = fake();
    Layout l = radial_layout(g);
    const int w = 200, h = 100;
    View v = fit(l, w, h);
    bool inside = true;
    for (const Pt& p : l.pos) {
        double sx = p.x * v.scale + v.ox, sy = p.y * v.scale + v.oy;
        inside = inside && sx >= 0 && sx <= w && sy >= 0 && sy <= h;
    }
    CHECK_EQ(inside, true);

    CrawlGraph one;
    one.add_node("https://solo.com/", 0);
    View v1 = fit(radial_layout(one), 80, 40);
    CHECK_EQ(near(v1.ox, 40) && near(v1.oy, 20), true);
}

static void test_short_label() {
    CHECK_EQ(short_label("https://www.google.com/maps?x"), std::string("google.com"));
    CHECK_EQ(short_label("http://user:pw@Example.COM:8080/a/b"), std::string("example.com"));
    CHECK_EQ(short_label("WWW.Foo.org"), std::string("foo.org"));
    CHECK_EQ(short_label("https://sub.site.io?q=1#f"), std::string("sub.site.io"));
    CHECK_EQ(short_label("https://[::1]:443/x"), std::string("[::1]"));
}

static void test_replay() {
    CrawlGraph g = fake();
    ReplayFrame f0 = replay_at(g, 0);
    CHECK_EQ(f0.size, 1);
    CHECK_EQ(f0.depth, 0);
    CHECK_EQ(f0.node_t[0], 1.0f);
    CHECK_EQ(f0.done, false);

    ReplayFrame end = replay_at(g, 1000000);
    CHECK_EQ(end.done, true);
    CHECK_EQ(end.size, int(g.nodes.size()) - 1);  // orphan z is never reached
    CHECK_EQ(end.depth, 2);

    // fully-reachable graph: every node shows
    CrawlGraph r = fake();
    r.add_edge("https://c.com/", "https://z.com/", false);
    r.record_fetch("https://c.com/", true);
    ReplayFrame rend = replay_at(r, 1000000);
    CHECK_EQ(rend.done, true);
    CHECK_EQ(rend.size, int(r.nodes.size()));

    int max_depth = 0;
    for (const Node& n : r.nodes) max_depth = std::max(max_depth, n.depth);
    int prev = 0;
    bool mono = true, depth_ok = true, done_once = false, done_stable = true;
    for (int64_t ms = 0; ms <= 2000; ms += 10) {
        ReplayFrame f = replay_at(r, ms);
        mono = mono && f.size >= prev;
        prev = f.size;
        depth_ok = depth_ok && f.depth <= max_depth;
        if (done_once && !f.done) done_stable = false;
        done_once = done_once || f.done;
    }
    CHECK_EQ(mono, true);
    CHECK_EQ(depth_ok, true);
    CHECK_EQ(done_stable, true);

    // timing: fetch 1 (a.com) starts at step_ms; its 2nd edge lags by edge_ms/4
    ReplayTiming t{100, 200};
    ReplayFrame m = replay_at(r, 100 + 100, t);
    CHECK_EQ(m.edge_t[3], 0.5f);   // a -> a1
    CHECK_EQ(m.edge_t[4], 0.25f);  // a -> a2
    CHECK_EQ(m.node_t[4], 0.5f);
}

int main() {
    test_layout();
    test_fit();
    test_short_label();
    test_replay();
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
