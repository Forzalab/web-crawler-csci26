// ui_test.cc - unit tests for src/ui/layout, replay, camera, html_text (pure TUI logic).
// Run: build/wc_ui_test   (exit 1 if any check fails)
#include <cmath>
#include <iostream>

#include "core/graph.h"
#include "ui/anim.h"
#include "ui/camera.h"
#include "ui/html_text.h"
#include "ui/layout.h"
#include "ui/progress.h"
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
    // A long crawl (500 fetches): the first steps are slow, the whole replay still ends on time.
    const int64_t big = 500 * 120 + 250;
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
    CHECK_EQ(tap.snapshot().pages, 0);
    CHECK_EQ(tap.links("https://home.com/")->size(), size_t(2));
    CHECK_EQ(tap.links("https://a.com/")->size(), size_t(2));
    CHECK_EQ(tap.links("https://b.com/").has_value(), false);
    CHECK_EQ(tap.links("https://a.com/1")->size(), size_t(0));
    CHECK_EQ(tap.links("https://nowhere.net/").has_value(), false);  // never a link: keeps the last depth
    Progress p = tap.snapshot();
    CHECK_EQ(p.pages, 5);
    CHECK_EQ(p.ok, 3);
    CHECK_EQ(p.failed, 2);
    CHECK_EQ(p.links, 4);
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
    CHECK_EQ(t.pages, 5);
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
