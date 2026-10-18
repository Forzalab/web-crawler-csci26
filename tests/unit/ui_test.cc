// ui_test.cc - unit tests for src/ui/layout, replay, camera, html_text (pure TUI logic).
// Run: build/wc_ui_test   (exit 1 if any check fails)
#include <algorithm>
#include <cmath>
#include <iostream>

#include "core/graph.h"
#include "core/tree.h"
#include "ui/anim.h"
#include "ui/camera.h"
#include "ui/focus.h"
#include "ui/html_text.h"
#include "ui/layout.h"
#include "ui/progress.h"
#include "ui/replay.h"
#include "ui/scene.h"

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
    const std::string root = "https://www.iana.org/";
    CHECK_EQ(node_label(root, root), std::string("iana.org"));
    CHECK_EQ(node_label("https://www.iana.org/domains", root), std::string("/domains"));
    CHECK_EQ(node_label("https://iana.org/a/b/?x=1", root), std::string("/b"));
    CHECK_EQ(node_label("https://www.iana.org/?q", root), std::string("iana.org"));
    CHECK_EQ(node_label("https://www.icann.org/x", root), std::string("icann.org"));
    CHECK_EQ(node_label("https://iana.org/abcdefghijklmnopqrstu", root), std::string("/abcdefghijkl\u2026"));
    CHECK_EQ(node_label("https://a-very-long-host-name.org/", root), std::string("a-very-long-h\u2026"));
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

    // Small crawl: well under the 8 s budget, so the per-fetch schedule is kept as is.
    // Edges: home->a b c (fetch 0), a->a1 a2 (fetch 1), b->home a1 (fetch 2), c->z (fetch 3).
    ReplayPlan p = replay_plan(r);
    const double want[] = {0, 62.5, 125, 120, 182.5, 240, 302.5, 360};
    bool starts = p.start.size() == 8;
    for (size_t i = 0; starts && i < 8; ++i) starts = near(p.start[i], want[i]);
    CHECK_EQ(starts, true);
    CHECK_EQ(p.edge_ms, 250.0);
    CHECK_EQ(p.end, 610.0);
    CHECK_EQ(p.total_ms, int64_t(610));
    CHECK_EQ(p.from[6], 2);
    CHECK_EQ(p.to[6], 4);
    CHECK_EQ(replay_ms(r), int64_t(610));
    // Unfetched source: never animates.
    CHECK_EQ(replay_plan(g).start.size(), size_t(7));
    CHECK_EQ(replay_at(r, p, 609).done, false);
    CHECK_EQ(replay_at(r, p, 610).done, true);

    // Big crawl: 1000-node 10-ary tree (node i -> 10i+1 .. 10i+10), fetches 0..99.
    // Per fetch it would take 100*120 ms+: too long, so it becomes a 3-level wave in 8 s.
    CrawlGraph tree;
    auto u = [](int i) { return "https://t.test/" + std::to_string(i); };
    tree.add_node(u(0), 0);
    for (int i = 0; i < 100; ++i) {
        for (int c = 10 * i + 1; c <= 10 * i + 10 && c < 1000; ++c) {
            tree.add_node(u(c), tree.nodes[size_t(i)].depth + 1);
            tree.add_edge(u(i), u(c), false);
        }
        tree.record_fetch(u(i), true);
    }
    CHECK_EQ(tree.nodes.size(), size_t(1000));
    ReplayPlan w = replay_plan(tree);
    CHECK_EQ(w.total_ms, int64_t(8000));
    CHECK_EQ(replay_ms(tree), int64_t(8000));
    CHECK_EQ(replay_at(tree, w, 7999).done, false);
    CHECK_EQ(replay_at(tree, w, 8000).done, true);
    CHECK_EQ(replay_at(tree, w, 8000).size, 1000);
    // 3 levels x 4 batches (10 kids, 3 per batch): gap = (8000 - 3*250) / 12 ms.
    const double gap = (8000.0 - 750) / 12;
    CHECK_EQ(near(w.start[0], 0) && near(w.start[2], 0) && near(w.start[3], gap) && near(w.start[9], 3 * gap), true);
    CHECK_EQ(near(w.start[10], 4 * gap + 250), true);       // node 1's first kid: level 1 starts
    CHECK_EQ(near(w.start[13], 5 * gap + 250), true);       // node 1's 4th kid: second batch
    CHECK_EQ(near(w.start[110], 8 * gap + 500), true);      // node 11's first kid: level 2
    CHECK_EQ(near(w.start[20], w.start[10]), true);         // node 2: same level, same start
    // Every parent has fully landed before an edge leaves it.
    std::vector<double> landed(tree.nodes.size(), 0.0);
    for (size_t i = 0; i < tree.edges.size(); ++i) landed[size_t(w.to[i])] = w.start[i] + w.edge_ms;
    bool parent_first = true;
    for (size_t i = 0; i < tree.edges.size(); ++i) parent_first = parent_first && landed[size_t(w.from[i])] <= w.start[i];
    CHECK_EQ(parent_first, true);
    // Mid-way the depth-1 ring is complete and part of depth 2 is in.
    ReplayFrame mid = replay_at(tree, w, int64_t(4 * gap + 500 + 1));  // level 1's first batch has landed
    CHECK_EQ(mid.depth, 2);
    CHECK_EQ(mid.size < 1000, true);

    // Deep chain: 80 levels x 250 ms > 8 s even with no gaps: squeezed, still 8 s, still in order.
    CrawlGraph chain;
    chain.add_node(u(0), 0);
    for (int i = 1; i <= 80; ++i) {
        chain.add_node(u(i), i);
        chain.add_edge(u(i - 1), u(i), false);
        chain.record_fetch(u(i - 1), true);
    }
    ReplayPlan c = replay_plan(chain);
    CHECK_EQ(c.total_ms, int64_t(8000));
    CHECK_EQ(near(c.edge_ms, 100), true);  // 250 * 8000 / (80 * 250)
    CHECK_EQ(near(c.start[79], 7900), true);
    CHECK_EQ(replay_at(chain, c, 8000).size, 81);
    CHECK_EQ(replay_at(chain, c, 7999).size, 80);
}

static void test_anim() {
    using namespace wc::anim;
    // Endpoints exact, clamped outside [0,1], monotonic in between.
    for (auto fn : {ease_in, ease_out, smoothstep}) {
        CHECK_EQ(fn(0.0), 0.0);
        CHECK_EQ(fn(1.0), 1.0);
        CHECK_EQ(fn(-3.0), 0.0);
        CHECK_EQ(fn(7.0), 1.0);
        bool mono = true;
        for (int i = 1; i <= 100; ++i) mono = mono && fn(i / 100.0) >= fn((i - 1) / 100.0);
        CHECK_EQ(mono, true);
    }
    CHECK_EQ(ease_in(0.5) < 0.5, true);    // slow start
    CHECK_EQ(ease_out(0.5) > 0.5, true);   // fast start
    CHECK_EQ(near(smoothstep(0.5), 0.5), true);
    CHECK_EQ(ramp(0, 400), 0.0);
    CHECK_EQ(ramp(200, 400), 0.5);
    CHECK_EQ(ramp(9999, 400), 1.0);
    CHECK_EQ(ramp(-50, 400), 0.0);
    CHECK_EQ(ramp(0, 0), 1.0);  // no duration = instant (--no-anim path)
    // Colour ramp: exact endpoints, clamped, midpoint between.
    const Rgb a{60, 60, 60}, b{240, 140, 0};
    CHECK_EQ(mix(a, b, 0.0) == a, true);
    CHECK_EQ(mix(a, b, 1.0) == b, true);
    CHECK_EQ(mix(a, b, -1.0) == a, true);
    CHECK_EQ(mix(a, b, 2.0) == b, true);
    Rgb m = mix(a, b, 0.5);
    CHECK_EQ(m.r == 150 && m.g == 100 && m.b == 30, true);
    // Warp: 0 -> 0, ends on time, monotonic, slow start, faster than 1x at the end.
    for (double total : {300.0, 1800.0, 6000.0, 60000.0}) {
        CHECK_EQ(warp(0, total), 0.0);
        CHECK_EQ(std::abs(warp(total, total) - total) < 1e-6, true);
        bool mono = true;
        double prev = 0;
        for (double w = 0; w <= total * 1.5; w += total / 500) {
            double v = warp(w, total);
            mono = mono && v >= prev;
            prev = v;
        }
        CHECK_EQ(mono, true);
        const double dt = total / 1000;
        CHECK_EQ(warp(dt, total) / dt < 0.35, true);                        // ~0.3x at first
        CHECK_EQ((warp(total, total) - warp(total - dt, total)) / dt > 1.0, true);  // >1x at the end
    }
    CHECK_EQ(warp(250, 0), 250.0);  // no total: identity
}

static void test_replay_clock() {
    CrawlGraph r = fake();
    r.add_edge("https://c.com/", "https://z.com/", false);
    r.record_fetch("https://c.com/", true);
    const int64_t total = replay_ms(r);
    CHECK_EQ(total, int64_t(610));
    CHECK_EQ(replay_at(r, total).done, true);
    CHECK_EQ(replay_at(r, total - 1).done, false);
    // Schedule: monotonic, first fetch clearly slower than linear, done on time (<= +20%).
    int64_t prev = 0;
    bool mono = true;
    int64_t done_at = -1, first_at = -1;  // wall ms: replay done / fetch 1's first edge lands
    for (int64_t w = 0; w <= total * 2; ++w) {
        int64_t c = replay_clock(w, total);
        mono = mono && c >= prev;
        prev = c;
        ReplayFrame f = replay_at(r, c);
        if (first_at < 0 && f.size >= 2) first_at = w;
        if (done_at < 0 && f.done) done_at = w;
    }
    CHECK_EQ(mono, true);
    CHECK_EQ(replay_clock(0, total), int64_t(0));
    const int64_t linear_first = 250;  // fetch 0's first edge lands at edge_ms
    // Tiny replay (< 1 s): the slow phase is short, but the first node still lands later than linear.
    CHECK_EQ(first_at >= linear_first * 13 / 10, true);
    CHECK_EQ(done_at > 0 && done_at <= total * 12 / 10, true);
    CHECK_EQ(done_at, int64_t(610));
    // A long crawl (the 8 s budget): the first steps are slow, the whole replay still ends on time.
    const int64_t big = 8000;
    CHECK_EQ(replay_clock(big, big), big);
    int64_t w1 = 0;
    while (replay_clock(w1, big) < 120) ++w1;  // wall time for the first fetch step
    CHECK_EQ(w1 >= 300, true);
    int64_t w_last = big;
    while (replay_clock(w_last, big) > big - 120) --w_last;  // last fetch step
    CHECK_EQ(big - w_last < 120, true);
}

static void test_camera() {
    View f{3, 10, -5};
    const int w = 200, h = 100;
    View v = apply(Camera{}, f, w, h);
    CHECK_EQ(near(v.scale, 3) && near(v.ox, 10) && near(v.oy, -5), true);

    // zoom 2: the world point fit puts at the center stays at the center
    double wx = (w / 2.0 - f.ox) / f.scale, wy = (h / 2.0 - f.oy) / f.scale;
    View z = apply(Camera{2, 0, 0}, f, w, h);
    CHECK_EQ(near(z.scale, 6), true);
    CHECK_EQ(near(wx * z.scale + z.ox, w / 2.0) && near(wy * z.scale + z.oy, h / 2.0), true);

    // pan moves the world: point (wx - px) lands at center
    View p = apply(Camera{2, 1.5, -2}, f, w, h);
    CHECK_EQ(near((wx - 1.5) * p.scale + p.ox, w / 2.0) && near((wy + 2) * p.scale + p.oy, h / 2.0), true);

    Camera cur{1, 0, 0}, tgt{4, 10, -10};
    bool no_over = true;
    for (int k = 0; k < 200; ++k) {
        cur = ease(cur, tgt, 1.0 / 60);
        no_over = no_over && cur.zoom <= tgt.zoom && cur.px <= tgt.px && cur.py >= tgt.py;
    }
    CHECK_EQ(no_over, true);
    CHECK_EQ(std::fabs(cur.zoom - 4) < 1e-6 && std::fabs(cur.px - 10) < 1e-6, true);
    Camera big = ease(Camera{1, 0, 0}, tgt, 100);  // huge dt: lands, never past
    CHECK_EQ(big.zoom <= 4 && big.px <= 10, true);
    Camera still = ease(Camera{2, 1, 1}, tgt, 0);
    CHECK_EQ(still.zoom, 2.0);
}

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& l : v) s += l + "|";
    return s;
}

static void test_html_text() {
    CHECK_EQ(join(html_to_lines("<html><head><title>T</title></head><body>"
                                "<SCRIPT>var x = '<p>no</p>';</script><style>p{}</style>hi</body>")),
             std::string("hi|"));
    CHECK_EQ(join(html_to_lines("<H2 class=x>Title</h2><p>Body  text\n here</p>")),
             std::string("# Title||Body text here|"));
    CHECK_EQ(join(html_to_lines("<ul><li>x</li><li> y <b>z</b></li></ul>")), std::string("- x|- y z|"));
    CHECK_EQ(join(html_to_lines("see <img src=a.png alt=\"cat\"> now")), std::string("see [IMG cat] now|"));
    CHECK_EQ(join(html_to_lines("<a href='/x'>link &amp; more</a>")), std::string("link & more|"));
    CHECK_EQ(join(html_to_lines("&lt;a&gt; &quot;q&quot; &#39;s&#39; a&nbsp;b &#65;&#x42; &bogus; &#x263A;")),
             std::string("<a> \"q\" 's' a b AB &bogus; &#x263A;|"));
    CHECK_EQ(join(html_to_lines("a<br><br><br><br>b<p></p><p></p><div></div><p>c</p>")),
             std::string("a||b||c|"));
    CHECK_EQ(join(html_to_lines("<p>x</p><pre>\n  int  a;\n\n  b &lt; c\n</pre>y")),
             std::string("x||  int  a;||  b < c||y|"));
    std::string big;
    for (int k = 0; k < 3000; ++k) big += "line<br>";
    CHECK_EQ(html_to_lines(big).size(), size_t(2000));
    CHECK_EQ(html_to_lines("").size(), size_t(0));
}

static void test_sites() {
    CHECK_EQ(site_of("https://pti.icann.org/x"), std::string("icann.org"));
    CHECK_EQ(site_of("https://www.iana.org/"), std::string("iana.org"));
    CHECK_EQ(site_of("http://localhost:8080/"), std::string("localhost"));
    CrawlGraph g;
    g.add_node("https://a.org/", 0);
    g.add_node("https://b.org/1", 1);
    g.add_node("https://b.org/2", 1);
    g.add_node("https://x.a.org/", 1);
    g.add_node("https://c.org/", 1);
    for (int i = 1; i < 5; ++i) g.add_edge(g.nodes[0].url, g.nodes[i].url, false);
    Layout l = radial_layout(g);
    CHECK_EQ(l.sites.size(), size_t(3));
    CHECK_EQ(l.shape[0], 0);  // a.org: 2 nodes, seen first
    CHECK_EQ(l.shape[3], 0);  // subdomain shares the shape
    CHECK_EQ(l.shape[1], 1);  // b.org: 2 nodes, seen second
    CHECK_EQ(l.shape[4], 2);
}

// Same-site siblings sit side by side: kids grouped by their site's first appearance.
static void test_layout_site_groups() {
    CrawlGraph g;
    g.add_node("https://r.org/", 0);
    const char* kids[] = {"https://ext.org/1", "https://r.org/a", "https://ext.org/2", "https://r.org/b",
                          "https://ext.org/3", "https://r.org/c"};
    for (const char* k : kids) {
        g.add_node(k, 1);
        g.add_edge(g.nodes[0].url, k, false);
    }
    Layout l = radial_layout(g);
    // Angle order around the root: r.org (seen first, it is the root's site) then ext.org.
    std::vector<int> ids = {1, 2, 3, 4, 5, 6};
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
        auto ang = [&](int i) { double t = std::atan2(l.pos[i].y, l.pos[i].x); return t < 0 ? t + 2 * M_PI : t; };
        return ang(a) < ang(b);
    });
    std::string sites;
    for (int i : ids) sites += site_of(g.nodes[size_t(i)].url) + " ";
    CHECK_EQ(sites, std::string("r.org r.org r.org ext.org ext.org ext.org "));
    CHECK_EQ(subtree_leaves(l.parent)[0], 6);
    CHECK_EQ(tree_parents(g)[4], 0);
}

// Level of detail: draw order, budgets, clusters.
static void test_scene_lod() {
    // r -> a b ; a -> a1 ; b -> b1 ; b1 -> x y z ; SEEN: a->b1, x->a, y->a, a1->z
    CrawlGraph g;
    auto u = [](const std::string& p) { return "https://s.test/" + p; };
    g.add_node(u(""), 0);
    auto kid = [&](const std::string& from, const std::string& to, int d) {
        g.add_node(u(to), d);
        g.add_edge(u(from), u(to), false);
    };
    kid("", "a", 1), kid("", "b", 1), kid("a", "a1", 2), kid("b", "b1", 2);
    kid("b1", "x", 3), kid("b1", "y", 3), kid("b1", "z", 3);
    for (auto [f, t] : {std::pair{"a", "b1"}, {"x", "a"}, {"y", "a"}, {"a1", "z"}}) g.add_edge(u(f), u(t), true);
    Scene s = make_scene(g);
    std::string order;
    for (int e : s.order) order += std::to_string(e) + " ";
    // Tree to depth <= 1 by leaves (b's 3 first), deeper tree by leaves, then SEEN by in-degree.
    CHECK_EQ(order, std::string("1 0 3 2 4 5 6 8 9 7 10 "));
    CHECK_EQ(s.n_always, 2);
    CHECK_EQ(s.n_tree, 7);
    std::string leaves, indeg;
    for (int v : s.leaves) leaves += std::to_string(v);
    for (int v : s.indeg) indeg += std::to_string(v);
    CHECK_EQ(leaves, std::string("41313111"));  // r a b a1 b1 x y z
    CHECK_EQ(indeg, std::string("03112112"));
    CHECK_EQ(s.clusters.size(), size_t(0));
    CHECK_EQ(lod_budget(80, 24, 1), 48);
    CHECK_EQ(lod_budget(40, 10, 1), 24);  // floor
    CHECK_EQ(lod_budget(200, 60, 1), 300);
    CHECK_EQ(lod_budget(80, 24, 4), 192);
    CHECK_EQ(lod_budget(200, 60, 0.1), 75);    // zoom clamped to 0.25
    CHECK_EQ(lod_budget(200, 60, 32), 4800);   // ... and to 16
    CHECK_EQ(lod_label_depth(1), 1);
    CHECK_EQ(lod_label_depth(2.49), 1);
    CHECK_EQ(lod_label_depth(2.5), 2);
    CHECK_EQ(lod_label_depth(4.99), 2);
    CHECK_EQ(lod_label_depth(5) > 100, true);

    // Clusters: r.org -> 7 ext.org kids (one with a kid) + 5 other.org kids + 2 r.org kids.
    CrawlGraph c;
    c.add_node("https://r.org/", 0);
    for (int i = 0; i < 7; ++i) c.add_node("https://ext.org/" + std::to_string(i), 1);
    for (int i = 0; i < 5; ++i) c.add_node("https://other.org/" + std::to_string(i), 1);
    for (int i = 0; i < 2; ++i) c.add_node("https://r.org/" + std::to_string(i), 1);
    for (size_t i = 1; i < c.nodes.size(); ++i) c.add_edge(c.nodes[0].url, c.nodes[i].url, false);
    for (int i = 0; i < 6; ++i) {  // ext.org/0 -> 6 more.org kids: inside the cluster, never nested
        c.add_node("https://more.org/" + std::to_string(i), 2);
        c.add_edge(c.nodes[1].url, c.nodes.back().url, false);
    }
    Scene cs = make_scene(c);
    CHECK_EQ(cs.clusters.size(), size_t(1));  // other.org: 5 < 6; r.org: the parent's own site
    CHECK_EQ(cs.clusters[0].label, std::string("ext.org ×13"));
    CHECK_EQ(cs.clusters[0].parent, 0);
    CHECK_EQ(cs.clusters[0].members.size(), size_t(7));
    CHECK_EQ(cs.cluster_of[1], 0);
    CHECK_EQ(cs.cluster_of[15], 0);   // more.org/0, a member's kid
    CHECK_EQ(cs.cluster_of[8], -1);   // other.org/0
    CHECK_EQ(near(std::hypot(cs.clusters[0].pos.x, cs.clusters[0].pos.y), 1), true);  // on the members' ring
}

static void test_focus() {
    CHECK_EQ(next_focus(Focus::Omni, true) == Focus::Preview, true);
    CHECK_EQ(next_focus(Focus::Omni, false) == Focus::Graph, true);  // no preview: skip it
    CHECK_EQ(next_focus(Focus::Preview, true) == Focus::Graph, true);
    CHECK_EQ(next_focus(Focus::Graph, true) == Focus::Omni, true);
    CHECK_EQ(next_focus(Focus::Graph, false) == Focus::Omni, true);
}

// A fake web: home -> a, b ; a -> a1 ; b fails. The tap must pass results through untouched.
struct FakeWeb : LinkSource {
    std::optional<std::vector<std::string>> links(const std::string& url) override {
        if (url == "https://home.com/") return std::vector<std::string>{"https://a.com/", "https://b.com/"};
        if (url == "https://a.com/") return std::vector<std::string>{"https://a.com/1", "https://home.com/"};
        if (url == "https://a.com/1") return std::vector<std::string>{};
        return std::nullopt;
    }
};

static void test_progress() {
    FakeWeb web;
    ProgressTap tap(web);
    CHECK_EQ(tap.snapshot().fetched, 0);
    CHECK_EQ(tap.snapshot().found, 0);
    CHECK_EQ(tap.links("https://home.com/")->size(), size_t(2));
    CHECK_EQ(tap.links("https://a.com/")->size(), size_t(2));
    CHECK_EQ(tap.links("https://b.com/").has_value(), false);
    CHECK_EQ(tap.links("https://a.com/1")->size(), size_t(0));
    CHECK_EQ(tap.links("https://nowhere.net/").has_value(), false);  // never a link: keeps the last depth
    Progress p = tap.snapshot();
    CHECK_EQ(p.fetched, 5);
    CHECK_EQ(p.ok, 3);
    CHECK_EQ(p.failed, 2);
    CHECK_EQ(p.found, 5);  // home, a, b, a/1, nowhere (home again from a: not new)
    CHECK_EQ(p.depth, 2);
    CHECK_EQ(p.recent.size(), size_t(5));
    CHECK_EQ(p.recent[0].url, std::string("https://home.com/"));
    CHECK_EQ(p.recent[0].depth, 0);
    CHECK_EQ(p.recent[1].depth, 1);
    CHECK_EQ(p.recent[2].ok, false);
    CHECK_EQ(p.recent[3].depth, 2);
    CHECK_EQ(p.recent[4].depth, 2);
    // Trimmed: the newest `last_n`, order kept; counters untouched.
    Progress t = tap.snapshot(2);
    CHECK_EQ(t.recent.size(), size_t(2));
    CHECK_EQ(t.recent[0].url, std::string("https://a.com/1"));
    CHECK_EQ(t.recent[1].url, std::string("https://nowhere.net/"));
    CHECK_EQ(t.fetched, 5);
    CHECK_EQ(tap.snapshot(99).recent.size(), size_t(5));
    CHECK_EQ(url_path("https://a.org/x/y?q"), std::string("/x/y?q"));
    CHECK_EQ(url_path("https://a.org"), std::string("/"));
}

static void test_loading_line() {
    std::string a = loading_line(0), b = loading_line(1999), c = loading_line(2000);
    CHECK_EQ(a.empty(), false);
    CHECK_EQ(a, b);
    CHECK_EQ(a == c, false);
    CHECK_EQ(loading_line(0, 500), a);
    CHECK_EQ(loading_line(500, 500), c);
    CHECK_EQ(loading_line(-5), a);
    // Rotates through every line, then wraps back to the first.
    int n = 1;
    while (loading_line(int64_t(n) * 2000) != a && n < 100) ++n;
    CHECK_EQ(n >= 15 && n <= 25, true);
    CHECK_EQ(loading_line(int64_t(n + 1) * 2000), c);
    for (int i = 0; i < n; ++i) CHECK_EQ(loading_line(int64_t(i) * 2000).size() <= 40, true);
}

int main() {
    test_progress();
    test_loading_line();
    test_sites();
    test_layout();
    test_layout_site_groups();
    test_scene_lod();
    test_focus();
    test_fit();
    test_short_label();
    test_replay();
    test_anim();
    test_replay_clock();
    test_camera();
    test_html_text();
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
