// tui_main.cc - interactive front end. One step on screen at a time:
// URL -> Visualize? -> Mode (Hybrid / BFS / DFS, Enter = Hybrid) -> Max depth -> Max links -> crawl (live loading screen) -> graph + omnibar.
// URL step: an invalid URL (valid_start) shows a red line; Enter / Tab do nothing on it.
// Flags: --no-anim (or NO_COLOR) jumps the replay to the end, no glow, no fades/ramps, no back-link pulses.
// Focus (ui/focus.h): Tab cycles search bar -> preview (if open) -> graph; Esc steps back.
// Graph keys (graph focused, or an empty omnibar): arrows pan, + - zoom (or wheel), 0 fit.
// Preview keys (preview focused): j/k or arrows scroll, PgUp/PgDn 10 lines, m max.
// Zoom keys that also work mid-typing (Termux extra-keys row): PgUp in, PgDn out.
// Every frame is one bracketed write (ui/term_lock.h): no row-by-row redraw.
// Enter on a result opens its page preview (cache, else live fetch).
// Crawl: kWorkers parallel fetches (polite per host). Esc on the loading screen stops it
// and shows the graph so far; /resume crawls on into the same graph (BFS only).
// DFS: the spec path, sequential crawl_dfs with the 0.1 s delay. Hybrid: crawl_hybrid (BFS layers,
// trie order inside a layer), sequential with the 0.1 s delay, Esc stops it like DFS.
// /bridges uploads the graph through gateway/ when WC_GATEWAY_URL is set (WC_BRIDGES_SUB = 0,
// WC_GATEWAY_TOKEN optional). Assignment: BRIDGES_ASSIGNMENT, else a random 7-digit id picked
// once per session. Once uploaded, a finished /resume re-uploads to the same assignment + sub,
// so the same link shows the bigger graph.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <thread>

#include <ftxui/component/animation.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include "adapters/josh_fetcher.h"
#include "bridges/bridges_json.h"
#include "core/crawl.h"
#include "core/crawl_hybrid.h"
#include "net/cache.h"
#include "net/gateway.h"
#include "net/https_fetcher.h"
#include "net/url.h"
#include "ui/anim.h"
#include "ui/camera.h"
#include "ui/fixture_source.h"
#include "ui/focus.h"
#include "ui/front_ends.h"
#include "ui/html_text.h"
#include "ui/layout.h"
#include "ui/matrix.h"
#include "ui/omni_query.h"
#include "ui/osc52.h"
#include "ui/progress.h"
#include "ui/replay.h"
#include "ui/scene.h"
#include "ui/term_lock.h"
#include "ui/theme.h"
#include "ui/views.h"
#include "ui/yes_no.h"

namespace wc {

using namespace ftxui;
namespace th = wc::theme;
using Clock = std::chrono::steady_clock;

namespace {

// Zoom keys, shown under the omnibar and in /help. + - 0 Home need an empty omnibar.
constexpr const char* kZoomHint = "zoom + - 0 · PgUp PgDn";
constexpr const char* kGraphHint = "arrows pan · + - zoom · 0 fit · Tab next";  // graph focused

constexpr int kWorkers = 16;  // parallel fetches; pacing per host is the fetcher's (robots, Crawl-delay, 429)

// Onboarding coach marks, first run only. Copy lives here so it is easy to rewrite.
const std::vector<std::string> kCoach = {
    "Type to find a page.",
    "Enter opens it. Esc closes.",
    "/bridges to share.",
};

enum class Step { Url, Visualize, Mode, Depth, Links, Crawling, Main, Summary };

bool settled(const Camera& a, const Camera& b) {
    return std::abs(a.zoom - b.zoom) < 1e-3 && std::abs(a.px - b.px) < 1e-3 && std::abs(a.py - b.py) < 1e-3;
}

int64_t ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

std::string seen_path() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.wc_tui_seen";
}

// assignment: the session's id ("" = BRIDGES_ASSIGNMENT, else "<N>").
std::string bridges_url(const std::string& assignment) {
    if (const char* u = std::getenv("WC_BRIDGES_URL")) return u;
    return bridges_view_url(assignment.empty() ? std::getenv("BRIDGES_ASSIGNMENT") : assignment.c_str(),
                            std::getenv("BRIDGES_USERNAME"));
}

// Centered prompt column: question, the live input, a dim footer.
// The blank top row balances the footer row, so the body is centred on the whole screen.
Element step_screen(Element body, const std::string& footer) {
    return vbox({text(""), filler(), hcenter(std::move(body)), filler(),
                 hcenter(text(footer) | color(th::kChrome) | dim)}) |
           flex;
}

// typed: the live text; not empty and not valid_start -> a red line under the field.
Element url_screen(const std::vector<std::string>& chips, Element input, const std::string& typed) {
    Elements chip_row;
    for (const auto& c : chips) {
        chip_row.push_back(text("[ " + short_label(c) + " ✕ ] ") | color(th::kAccent));
    }
    Element bad = typed.empty() || valid_start(typed) ? text("") : text("not a URL: " + typed) | color(th::kError);
    return step_screen(vbox({hbox(std::move(chip_row)), views::field("", std::move(input), 50), std::move(bad)}),
                       "Enter crawl · Tab add URL · Esc quit");
}

Element prompt_screen(const std::string& q, Element input, const std::string& footer = "Enter accept default · Esc quit") {
    return step_screen(views::field(q, std::move(input), 8), footer);
}

// Mode prompt: Hybrid first, in the lava ramp with a slow glow and a "★ recommended" tag;
// BFS / DFS plain and dim. anim = false (--no-anim / NO_COLOR): no glow. NO_COLOR: no colours.
Element mode_screen(Element input, bool anim) {
    const std::string word = "[h]ybrid";
    const double glow = anim ? 0.5 + 0.5 * std::sin(anim::now_ms() / 1000.0 * 2 * M_PI / 1.6) : 1;
    const bool paint = !th::no_color();
    Elements hy;
    for (size_t i = 0; i < word.size(); ++i) {
        anim::Rgb c = anim::mix(th::depth_rgb(int(i), int(word.size()) + 2), th::rgb::kText, 0.4 * glow);
        Element ch = text(std::string(1, word[i])) | bold;
        hy.push_back(paint ? std::move(ch) | color(th::to_color(c)) : std::move(ch));
    }
    Element tag = text(" ★ recommended");
    auto plain = [&](const std::string& s) { return paint ? text(s) | color(th::kChrome) | dim : text(s) | dim; };
    return vbox({views::field("Mode?", std::move(input), 8), text(""),
                 hbox({hbox(std::move(hy)), paint ? std::move(tag) | color(th::kAccent) : std::move(tag), text("    "),
                       plain("[b]fs"), text("   "), plain("[d]fs")})});
}

Element summary_screen(const CrawlGraph& g) {
    int ok = 0, bad = 0, un = 0, maxd = 0;
    for (const auto& n : g.nodes) {
        (n.status == Status::Fetched ? ok : n.status == Status::Failed ? bad : un)++;
        maxd = std::max(maxd, n.depth);
    }
    auto row = [](const std::string& k, int v) {
        return hbox({text(k) | color(th::kChrome) | size(WIDTH, EQUAL, 12), text(std::to_string(v)) | bold | color(th::kText)});
    };
    return step_screen(vbox({text((g.nodes.empty() ? "?" : short_label(g.nodes[0].url)) + " · " + to_string(g.mode)) | bold | color(th::kAccent), text(""),
                             row("links", int(g.nodes.size())), row("edges", int(g.edges.size())),
                             row("fetched", ok), row("failed", bad), row("unfetched", un),
                             row("max depth", maxd)}),
                       "Esc quit");
}

}  // namespace

int tui_main(int argc, char** argv) {
    if (const char* d = std::getenv("WC_MATRIX_DEMO"); d && *d == '1') return matrix::demo();
    bool no_anim = std::getenv("NO_COLOR") != nullptr;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-anim")) no_anim = true;
    bool first_run = !std::ifstream(seen_path()).good();

    TermLock term_lock;  // before `screen`: outlives it, so FTXUI's restore bytes go out too
    auto screen = ScreenInteractive::Fullscreen();  // alt screen, mouse on, cursor hidden while drawing
    Step step = Step::Url;
    CrawlConfig cfg;
    cfg.maxDepth = kDefaultDepth;
    cfg.maxSize = kDefaultLinks;
    cfg.workers = kWorkers;
    bool visualize = first_run;

    // URL step. MVP: only chips[0] (or the typed URL) is crawled.
    // TODO: crawl every chip.
    std::vector<std::string> chips;
    std::string url_text, prompt_text, omni_text;
    int prompt_cursor = 0, omni_cursor = 0;
    InputOption base;  // shared by every input: views::input_text look, one line
    base.transform = [](InputState s) { return views::input_text(std::move(s.element), s.is_placeholder); };
    base.multiline = false;
    InputOption url_opt = base, prompt_opt = base, omni_opt = base;
    url_opt.placeholder = "Paste a URL. Enter to crawl.";
    omni_opt.placeholder = "";  // ghost carries the hint (main_screen)
    prompt_opt.cursor_position = &prompt_cursor;
    omni_opt.cursor_position = &omni_cursor;
    auto url_in = Input(&url_text, url_opt);
    auto prompt_in = Input(&prompt_text, prompt_opt);
    auto omni_in = Input(&omni_text, omni_opt);
    int tab = 0;
    auto inputs = Container::Tab({url_in, prompt_in, omni_in}, &tab);

    // Crawl: the worker owns the fetchers and graph until done; main thread joins.
    JoshFetcher josh;
    HttpsFetcher tls;
    PageCache cache(".wc-cache");
    tls.use_cache(&cache, false, false);
    tls.use_robots(true);
    RoutedFetcher routed(josh, tls);
    RealSleeper sleeper;
    NoSleeper no_sleep;
    auto fixture = fixture_source_from_env();  // WC_FIXTURE=file: fake web, no network (tests/smoke)
    LinkSource& source = fixture ? *fixture : static_cast<LinkSource&>(routed);
    Sleeper& pause = fixture ? static_cast<Sleeper&>(no_sleep) : sleeper;
    ProgressTap tap(source);  // the loading screen reads tap.snapshot(); g stays the worker's until done
    std::string start;
    CrawlGraph g;
    std::thread worker, ticker, pulser;  // pulser: ~11 fps on the graph screen, for the back-link pulses
    std::atomic<bool> crawl_done{false}, quitting{false}, stop_crawl{false};
    cfg.cancel = &stop_crawl;  // Esc on the loading screen
    bool resumed = false;      // last crawl was a /resume: no replay
    bool stopped_once = false;  // a stop leaves fetches running on detached threads: _Exit at the end
    int tick = 0;
    matrix::MatrixRain rain;  // loading-screen backdrop, one frame per ticker tick
    Clock::time_point crawl_t0;

    // Main screen state.
    std::shared_ptr<const Scene> scene;  // g + layout + replay plan, rebuilt once per finished crawl
    Clock::time_point t0, toast_at, flash_depth_at, flash_size_at;
    bool flashed_depth = false, flashed_size = false, bridges_shown = false;
    std::string toast_text;
    std::vector<int> hits;
    int selected = 0;

    // Camera: cam eases toward target every frame; auto-fit once, 2 s after replay + idle.
    Camera cam, cam_target;
    Clock::time_point last_frame, last_input, replay_done_at;
    bool replay_done = false, auto_fitted = false, hint_shown = false;
    int coach_step = -1;  // index into kCoach; -1 = not showing

    // Preview: one fetch thread at a time; gen drops stale deliveries.
    bool pv_open = false, pv_max = false;
    int pv_scroll = 0, pv_gen = 0;
    std::string pv_title;
    std::vector<std::string> pv_lines;
    std::thread pv_worker;
    std::atomic<bool> pv_busy{false};

    // Focus: who takes the keys on the main screen; focus_at drives the glow + flash.
    Focus focus = Focus::Omni;
    Clock::time_point focus_at;

    // BRIDGES upload: one thread at a time; shared = uploaded once this session.
    const char* gateway = std::getenv("WC_GATEWAY_URL");
    std::string share_url;  // the gateway's answer; "" = the env-built link (bridges_url)
    std::string assignment;  // picked once (BRIDGES_ASSIGNMENT, else random 7 digits), every upload reuses it
    bool shared = false;
    std::thread up_worker;
    std::atomic<bool> up_busy{false};

    auto set_focus = [&](Focus f) {
        focus = f;
        focus_at = Clock::now();
        tab = f == Focus::Omni ? 2 : 1;  // the omnibar shows its caret only while focused
    };
    auto toast = [&](const std::string& s) {
        toast_text = s;
        toast_at = Clock::now();
    };
    auto set_omni = [&](const std::string& s) {
        omni_text = s;
        omni_cursor = int(s.size());
    };
    auto refresh_hits = [&] {
        Query q = parse_query(omni_text);
        bool idle = q.verb.empty() && q.text.empty() && q.depth_op == 0 && !q.status;
        hits = q.verb.empty() && !idle ? match(g, q) : std::vector<int>{};  // empty query: ghost only
        selected = std::clamp(selected, 0, std::max(0, int(hits.size()) - 1));
    };
    auto session_id = [&]() -> const std::string& {
        if (assignment.empty()) assignment = session_assignment(std::getenv("BRIDGES_ASSIGNMENT"), std::random_device{}());
        return assignment;
    };
    // Before the first upload the fallback link shows the id that upload will use, never "<N>".
    auto view_url = [&] {
        return !share_url.empty() ? share_url : bridges_url(gateway && *gateway ? session_id() : "");
    };
    // refresh = re-upload after a /resume: quiet until it lands.
    // Big crawls go up as the most prominent 1000 links (bridges_subset), with a note saying so.
    auto upload = [&](bool refresh) {
        if (up_busy) return toast("still uploading…");
        if (up_worker.joinable()) up_worker.join();
        up_busy = true;
        CrawlGraph part = bridges_subset(g);
        const bool cut = part.nodes.size() < g.nodes.size();
        const std::string note = cut ? std::to_string(part.nodes.size()) + " of " + std::to_string(g.nodes.size()) + " links" : "";
        BridgesMeta meta;
        meta.pretty = true;
        meta.title = (g.nodes.empty() ? std::string("web") : short_label(g.nodes[0].url)) + " crawl" + (cut ? " · " + note : "");
        if (cut) meta.description = note + " shown — most prominent (root, first hop, most linked-to).";
        // Site ranks, our radial layout (BRIDGES pins located nodes: spread, not one force-layout clump)
        // and short labels (hub names), all from the scene; titles from the page cache, in the worker.
        for (size_t i = 0; i < g.nodes.size() && i < scene->lay.pos.size(); ++i) {
            const std::string& url = g.nodes[i].url;
            meta.site_rank[url] = scene->lay.shape[i];
            meta.location[url] = {scene->lay.pos[i].x, scene->lay.pos[i].y};
            meta.labels[url] = node_label(url, g.nodes[0].url);
        }
        if (!refresh) toast(cut ? "uploading " + note + " to BRIDGES…" : "uploading to BRIDGES…");
        up_worker = std::thread([&, refresh, note, a = session_id(), part = std::move(part), meta = std::move(meta)]() mutable {
            for (const Node& n : part.nodes)  // titles we already have on disk: no new fetches
                if (auto hit = cache.load(n.url); hit && !hit->body.empty())
                    if (std::string t = html_title(hit->body); !t.empty()) meta.page_titles[n.url] = t;
            const std::string json = bridges_json(part, meta);
            HttpOptions opt;
            opt.timeout_ms = 20000;  // the gateway waits up to 15 s for BRIDGES
            HttpClient http(opt);
            const char *sub = std::getenv("WC_BRIDGES_SUB"), *tok = std::getenv("WC_GATEWAY_TOKEN");
            GatewayReply r = gateway_upload(http, gateway, a, sub ? std::atoi(sub) : 0, json, tok ? tok : "");
            screen.Post([&, refresh, r, note] {  // note by value: this thread (and its copy) is gone when it runs
                if (!r.error.empty()) return toast("BRIDGES: " + r.error);
                share_url = r.url;
                shared = true;
                if (refresh) return toast(note.empty() ? "BRIDGES updated" : "BRIDGES updated · " + note);
                if (!note.empty()) toast("BRIDGES: " + note);
                bridges_shown = true;
            });
            up_busy = false;
            screen.PostEvent(Event::Custom);
        });
    };

    auto begin_prompt = [&](Step s, const std::string& def) {
        step = s;
        prompt_text = def;
        prompt_cursor = int(def.size());
        tab = 1;
    };
    auto start_crawl = [&](bool resume) {
        step = Step::Crawling;
        crawl_t0 = Clock::now();
        crawl_done = stop_crawl = false;
        resumed = resume;
        worker = std::thread([&, resume, base = resume ? g : CrawlGraph{}]() mutable {
            CrawlGraph r = resume                      ? resume_crawl(std::move(base), cfg, tap)
                           : cfg.mode == Mode::Hybrid ? crawl_hybrid(start, cfg, tap, pause)
                                                      : crawl(start, cfg, tap, pause);
            screen.Post([&, r = std::move(r)]() mutable {
                g = std::move(r);
                crawl_done = true;
            });
            screen.PostEvent(Event::Custom);
        });
        ticker = std::thread([&] {
            while (!crawl_done && !quitting) {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));  // ~12 fps loading screen
                screen.PostEvent(Event::Custom);
            }
        });
    };
    auto finish_crawl = [&] {
        worker.join();
        ticker.join();
        std::ofstream(seen_path()) << "1\n";
        int left = pending(g, cfg.maxDepth);
        if (stop_crawl && left > 0) {
            stopped_once = true;
            toast("stopped · " + std::to_string(left) + " left · /resume");
        }
        if (!visualize) {
            step = Step::Summary;
            return;
        }
        scene = std::make_shared<const Scene>(make_scene(g));
        t0 = last_frame = last_input = Clock::now();
        step = Step::Main;
        if (!no_anim && !pulser.joinable())
            pulser = std::thread([&] {
                while (!quitting) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(90));
                    screen.PostEvent(Event::Custom);
                }
            });
        set_focus(Focus::Omni);
        refresh_hits();
        if (resumed && shared) upload(true);  // same assignment + sub: the old link shows the new graph
    };

    auto open_preview = [&](const Node& n) {
        pv_open = true;
        pv_scroll = 0;
        set_focus(Focus::Preview);
        pv_title = short_label(n.url) + url_path(n.url);
        int gen = ++pv_gen;
        if (auto hit = cache.load(n.url); hit && !hit->body.empty()) {
            pv_lines = html_to_lines(hit->body);
            return;
        }
        pv_lines = {"loading…"};
        if (pv_busy) return pv_lines.assign({"busy: still fetching the last page"});  // retry Enter later
        if (pv_worker.joinable()) pv_worker.join();
        pv_busy = true;
        pv_worker = std::thread([&, gen, url = n.url] {
            FetchResult r = tls.fetch(url);  // crawl is over: the worker is free
            std::vector<std::string> ls = r.error.empty() ? html_to_lines(r.body) : std::vector<std::string>{"✕ " + r.error};
            if (ls.empty()) ls = {"(no text)"};
            screen.Post([&, gen, ls = std::move(ls)]() mutable {
                if (gen == pv_gen) pv_lines = std::move(ls);
            });
            pv_busy = false;
            screen.PostEvent(Event::Custom);
        });
    };
    auto close_preview = [&] {
        pv_open = pv_max = false;
        ++pv_gen;
        set_focus(Focus::Omni);
    };

    auto main_screen = [&]() -> Element {
        // Slow first fetches, then faster; ends when the old linear replay did.
        int64_t el = no_anim || resumed ? INT64_MAX / 4 : replay_clock(ms_since(t0), scene->plan.total_ms);
        ReplayFrame f = replay_at(scene->g, scene->plan, el);
        if (!flashed_depth && f.depth >= cfg.maxDepth) flashed_depth = true, flash_depth_at = Clock::now();
        if (!flashed_size && f.size >= cfg.maxSize) flashed_size = true, flash_size_at = Clock::now();
        auto flashing = [&](bool on, Clock::time_point at) {
            int64_t d = ms_since(at);
            return on && d < 600 && (d / 150) % 2 == 0;
        };
        bool fd = flashing(flashed_depth, flash_depth_at), fs = flashing(flashed_size, flash_size_at);
        bool flash_live = (flashed_depth && ms_since(flash_depth_at) < 600) || (flashed_size && ms_since(flash_size_at) < 600);
        bool toast_live = !toast_text.empty() && ms_since(toast_at) < 1500;

        // Replay done: start coach marks (first run), then auto-fit once after 2 s idle.
        if (f.done && !replay_done) {
            replay_done = true;
            replay_done_at = Clock::now();
            if (first_run) coach_step = 0;
        }
        bool fit_wait = replay_done && !auto_fitted;
        if (fit_wait && ms_since(std::max(replay_done_at, last_input)) >= 2000) {
            auto_fitted = true, fit_wait = false;
            cam_target = Camera{};
            if (coach_step < 0 && !hint_shown) hint_shown = true, toast("/bridges to share the graph"), toast_live = true;
        }
        double dt = std::chrono::duration<double>(Clock::now() - last_frame).count();
        last_frame = Clock::now();
        bool cam_moving = !settled(cam, cam_target);
        cam = cam_moving && !no_anim ? ease(cam, cam_target, std::min(dt, 0.1)) : cam_target;
        // Focus look: the preview or graph glows while it has the keys (the search bar, the
        // resting state, stays bright and still); every change flashes toward white for 300 ms.
        const int64_t since_focus = ms_since(focus_at);
        float glow = no_anim || focus == Focus::Omni ? 1.0f : float(0.5 + 0.5 * std::cos(since_focus / 1000.0 * 2 * M_PI / 1.2));
        float flash = no_anim ? 0.0f : float(1.0 - anim::ramp(since_focus, 300));
        auto border = [&](Focus who) { return views::focus_border(focus == who, glow, flash); };
        if (!f.done || flash_live || toast_live || fit_wait || cam_moving || flash > 0 || (focus != Focus::Omni && !no_anim))
            animation::RequestAnimationFrame();

        std::vector<std::string> rows;
        if (bridges_shown) {
            rows.push_back(view_url() + "   [c] copy");
        } else {
            for (int id : hits) {
                const Node& n = g.nodes[id];
                const auto& shape = scene->lay.shape;
                rows.push_back(std::string(th::glyph(size_t(id) < shape.size() ? shape[id] : -1, n.status)) + " " + node_label(n.url, g.nodes[0].url) + "  d" + std::to_string(n.depth));
            }
        }
        std::string ghost = complete(omni_text);
        ghost = ghost.size() > omni_text.size() ? ghost.substr(omni_text.size()) : "";
        if (omni_text.empty()) ghost = "Type to find a page.\nExample: " + example_label(g);

        if (focus == Focus::Graph) ghost += "\n" + std::string(kGraphHint);
        else if (omni_text.empty()) ghost += "\n" + std::string(kZoomHint);
        int extra = int(std::count(ghost.begin(), ghost.end(), '\n'));  // placeholder lines: rows in the box
        // Canvas sub-pixels: 2 per col, 4 per row, minus boss bar (2) + omnibar (3 + results).
        auto ts = Terminal::Size();
        int rows_left = std::max(1, ts.dimy - 2 - 3 - int(rows.size()) - extra);
        View v = apply(cam, fit(scene->lay, ts.dimx * 2, rows_left * 4), ts.dimx * 2, rows_left * 4);

        Element body = vbox({views::boss_bar(f.depth, cfg.maxDepth, f.size, cfg.maxSize, fd, fs, border(Focus::Graph)),
                             views::graph(scene, f, v, cam.zoom, no_anim ? -1 : anim::now_ms()) | flex,
                             views::omnibar(omni_in->Render(), rows, bridges_shown ? 0 : selected, ghost, border(Focus::Omni))});
        // Overlays, back to front: preview (right, v-centered), coach (above omnibar), toast (top right, under the boss bar).
        if (pv_open) {
            pv_scroll = std::clamp(pv_scroll, 0, std::max(0, int(pv_lines.size()) - 1));
            Element p = views::preview(pv_title, pv_lines, pv_scroll, border(Focus::Preview), pv_max);
            p = pv_max ? p | flex : p | size(HEIGHT, LESS_THAN, ts.dimy * 7 / 10);
            body = dbox({body, pv_max ? p : hbox({filler(), vbox({filler(), p, filler()}), text(" ")})  /* 1-col gap: borders join */});
        }
        if (!pv_open && coach_step >= 0 && coach_step < int(kCoach.size())) {  // never over the preview
            Element c = views::coach(coach_step + 1, int(kCoach.size()), kCoach[coach_step]);
            body = dbox({body, vbox({filler(), hbox({filler(), c, filler()}),
                                     emptyElement() | size(HEIGHT, EQUAL, 3 + extra + int(rows.size()))})});
        }
        if (toast_live)  // under the boss bar (2 rows), right edge in line with its LINKS meter
            body = dbox({body, vbox({emptyElement() | size(HEIGHT, EQUAL, 2), hbox({filler(), views::toast(toast_text), text(" ")}), filler()})});
        return body;
    };

    // Loading screen: spinner, rotating one-liner, live counters, a checklist of
    // fetches as they land, border breathing over 1.6 s. Behind it: dim green matrix
    // rain (none under --no-anim / NO_COLOR; orange is reserved for advanced mode).
    // Progressive: checklist lines fade in/out; the rain starts as a few slow columns
    // and ramps to full density/speed/variety over ~8 s.
    auto crawling_screen = [&](Dimensions ts) -> Element {
        int64_t ms = ms_since(crawl_t0);
        views::LoadingState st;
        st.host = short_label(start) + " · " + to_string(cfg.mode);
        st.line = loading_line(ms);
        st.tick = tick;
        st.cols = ts.dimx;
        st.pulse = no_anim ? 1.0f : float(0.5 + 0.5 * std::sin(ms / 1000.0 * 2 * M_PI / 1.6));
        st.rows = views::loading_rows(ts.dimy);
        st.max_links = cfg.maxSize;
        st.progress = tap.snapshot(size_t(st.rows));
        st.fade = !no_anim;
        st.now_ms = anim::now_ms();
        Element bg = emptyElement();
        if (!no_anim) {
            rain.resize(ts.dimx, ts.dimy);
            rain.set_intensity(float(anim::smoothstep(anim::ramp(ms, 8000))));
            bg = rain.render(matrix::kGreenDim) | flex;
        }
        return views::loading(st, std::move(bg));
    };

    auto view = Renderer(inputs, [&]() -> Element {
        auto ts = Terminal::Size();
        // Only the graph needs the room: prompts, loading and summary fit a phone (~50 cols).
        if (step == Step::Main && (ts.dimx < 80 || ts.dimy < 24)) return views::too_small(ts.dimx, ts.dimy);
        switch (step) {
            case Step::Url: return url_screen(chips, url_in->Render(), url_text);
            case Step::Visualize:
                return prompt_screen(first_run ? "Visualize? [Y/n]" : "Visualize? [y/N]", prompt_in->Render(),
                                     "y yes · n no · Enter default · Esc quit");
            case Step::Mode:
                if (!no_anim) animation::RequestAnimationFrame();  // the hybrid glow
                return step_screen(mode_screen(prompt_in->Render(), !no_anim),
                                   "h Hybrid (recommended) · b BFS · d DFS · Enter Hybrid · Esc quit");
            case Step::Depth: return prompt_screen("Max depth [" + std::to_string(kDefaultDepth) + "]", prompt_in->Render());
            case Step::Links: return prompt_screen("Max links [" + std::to_string(kDefaultLinks) + "]", prompt_in->Render());
            case Step::Crawling: return crawling_screen(ts);
            case Step::Summary: return summary_screen(g);
            case Step::Main: return main_screen();
        }
        return text("");
    });

    auto on_omni_enter = [&] {
        Query q = parse_query(omni_text);
        if (q.verb == "quit") return screen.Exit();
        if (q.verb == "help") {
            toast("wiki · depth:<3 · status:failed · /bridges /resume /help /quit · " + std::string(kZoomHint) + " · Tab focus");
            return set_omni("");
        }
        if (q.verb == "resume") {
            set_omni("");
            refresh_hits();
            if (g.mode != Mode::BFS) return toast("/resume: BFS only");  // resume_crawl is BFS order
            if (pending(g, cfg.maxDepth) == 0) return toast("nothing to resume");
            close_preview();
            return start_crawl(true);
        }
        if (q.verb == "bridges") {
            if (gateway && *gateway) return upload(false);
            toast("set WC_GATEWAY_URL to upload");
            return void(bridges_shown = true);
        }
        if (q.verb == "close") {
            close_preview();
            return set_omni(""), refresh_hits();
        }
        if (!q.verb.empty()) return toast("unknown /" + q.verb);
        if (selected < int(hits.size())) open_preview(g.nodes[hits[selected]]);
    };

    // Every yes/no prompt goes through this: Event -> yes_no_answer (src/ui/yes_no.h).
    auto yes_no_key = [](const Event& e, bool default_yes) {
        char k = e == Event::Return ? '\n' : e.is_character() && e.input().size() == 1 ? e.input()[0] : 0;
        return yes_no_answer(k, default_yes);
    };

    auto app = CatchEvent(view, [&](Event e) {
        if (e == Event::Custom) {
            if (step == Step::Crawling) {
                ++tick;
                rain.tick();
                if (crawl_done) finish_crawl();
            }
            return false;
        }
        if (step == Step::Main) last_input = Clock::now();
        if (e == Event::Escape) {
            // Main: Esc peels one layer: the focused preview, graph focus, omnibar text, then
            // coach, then preview, then quit.
            if (step == Step::Main && focus == Focus::Preview && pv_open) return close_preview(), true;
            if (step == Step::Main && focus == Focus::Graph) return set_focus(Focus::Omni), true;
            if (step == Step::Main && (bridges_shown || !omni_text.empty())) {
                bridges_shown = false;
                set_omni("");
                refresh_hits();
                return true;
            }
            if (step == Step::Main && coach_step >= 0) return coach_step = -1, true;
            if (step == Step::Main && pv_open) return close_preview(), true;
            if (step == Step::Crawling) return stop_crawl = true, true;  // graph so far; Ctrl-C quits
            screen.Exit();
            return true;
        }
        auto int_or = [](const std::string& s, int d) {
            try { return s.empty() ? d : std::max(0, std::stoi(s)); } catch (...) { return d; }
        };
        switch (step) {
            case Step::Url:
                if (e == Event::Tab && !url_text.empty() && valid_start(url_text)) {
                    chips.push_back(url_text);
                    url_text.clear();
                    return true;
                }
                if (e == Event::Return) {
                    start = !chips.empty() ? chips[0] : url_text;
                    if (start.empty() || !valid_start(start)) return true;
                    start = pick_start(start, [&](const std::string& u) { return tls.fetch(u); });  // probe fills the cache
                    begin_prompt(Step::Visualize, "");
                    return true;
                }
                return false;
            case Step::Visualize:
                if (auto a = yes_no_key(e, first_run)) {  // y/n answers at once, Enter = default
                    visualize = *a;
                    begin_prompt(Step::Mode, "");
                }
                return true;  // other keys are not typed into the box
            case Step::Mode:  // h/b/d answer at once, Enter = Hybrid
                if (e == Event::Return || e == Event::Character('h') || e == Event::Character('H')) cfg.mode = Mode::Hybrid;
                else if (e == Event::Character('b') || e == Event::Character('B')) cfg.mode = Mode::BFS;
                else if (e == Event::Character('d') || e == Event::Character('D')) cfg.mode = Mode::DFS;
                else return true;  // ignored, like Visualize
                begin_prompt(Step::Depth, "");
                return true;
            case Step::Depth:
                if (e == Event::Return) {
                    cfg.maxDepth = int_or(prompt_text, kDefaultDepth);
                    begin_prompt(Step::Links, "");
                    return true;
                }
                return false;
            case Step::Links:
                if (e == Event::Return) {
                    cfg.maxSize = std::max(1, int_or(prompt_text, kDefaultLinks));
                    start_crawl(false);
                    return true;
                }
                return false;
            case Step::Crawling:
            case Step::Summary: return true;
            case Step::Main: break;
        }
        // Main: omnibar keys.
        if (bridges_shown && e == Event::Character('c')) {
            std::cout << osc52(view_url());
            std::cout.flush();
            toast("copy sent");
            bridges_shown = false;
            set_omni("");
            refresh_hits();
            return true;
        }
        // Coach: Tab steps through, past the last one closes it.
        if (coach_step >= 0 && e == Event::Tab) {
            if (++coach_step >= int(kCoach.size())) coach_step = -1;
            return true;
        }
        auto zoom = [&](double k) { cam_target.zoom = std::clamp(cam_target.zoom * k, 0.25, 16.0); };
        auto camera_key = [&](const Event& k) {
            double pan = 0.25 / cam_target.zoom;  // world units (ring = 1), same feel at any zoom
            if (k == Event::Character('+') || k == Event::Character('=')) return zoom(1.25), true;
            if (k == Event::Character('-') || k == Event::Character('_')) return zoom(1 / 1.25), true;
            if (k == Event::Character('0') || k == Event::Home) return cam_target = Camera{}, true;
            if (k == Event::ArrowLeft) return cam_target.px += pan, true;
            if (k == Event::ArrowRight) return cam_target.px -= pan, true;
            if (k == Event::ArrowUp) return cam_target.py += pan, true;
            if (k == Event::ArrowDown) return cam_target.py -= pan, true;
            return false;
        };
        // Tab: complete in the search bar if there is something to complete, else next focus.
        if (e == Event::Tab) {
            std::string c = complete(omni_text);
            if (focus == Focus::Omni && !c.empty() && c != omni_text) return set_omni(c), refresh_hits(), true;
            return set_focus(next_focus(focus, pv_open)), true;
        }
        if (e.is_mouse() && e.mouse().button == Mouse::WheelUp) return zoom(1.25), true;
        if (e.is_mouse() && e.mouse().button == Mouse::WheelDown) return zoom(1 / 1.25), true;
        bool empty = omni_text.empty() && !bridges_shown, key = !e.is_mouse();  // the mouse never moves focus
        if (key && focus == Focus::Preview && pv_open) {  // the preview scrolls; any other key types into the bar
            if (e == Event::Character('j') || e == Event::ArrowDown) return ++pv_scroll, true;
            if (e == Event::Character('k') || e == Event::ArrowUp) return pv_scroll = std::max(0, pv_scroll - 1), true;
            if (e == Event::PageDown) return pv_scroll += 10, true;
            if (e == Event::PageUp) return pv_scroll = std::max(0, pv_scroll - 10), true;
            if (e == Event::Character('m')) return pv_max = !pv_max, true;
            if (e == Event::ArrowLeft || e == Event::ArrowRight) return true;
            set_focus(Focus::Omni);
        }
        // Camera keys: graph focused (even with text in the bar), or an empty bar.
        if (key && (focus == Focus::Graph || empty)) {
            if (camera_key(e)) return true;
            if (focus == Focus::Graph) set_focus(Focus::Omni);  // any other key: back to typing
        }
        if (e == Event::PageUp) return zoom(1.25), true;  // kZoomHint: work while typing too
        if (e == Event::PageDown) return zoom(1 / 1.25), true;
        if (e == Event::ArrowUp) return selected = std::min(selected + 1, std::max(0, int(hits.size()) - 1)), true;  // results drop up
        if (e == Event::ArrowDown) return selected = std::max(selected - 1, 0), true;
        if (e == Event::Return) {
            on_omni_enter();
            return true;
        }
        if (bridges_shown) bridges_shown = false;
        inputs->OnEvent(e);
        refresh_hits();
        return true;  // swallow the rest (unknown keys / escape sequences): nothing else sees them
    });

    screen.Loop(app);
    quitting = true;
    if (ticker.joinable()) ticker.join();
    if (pulser.joinable()) pulser.join();
    std::cout.flush();  // every _Exit below skips TermLock's destructor
    if (stopped_once) std::_Exit(0);  // detached crawl fetches may still hold the fetchers
    if (pv_busy || up_busy) std::_Exit(0);  // a preview fetch / upload is mid-network
    if (pv_worker.joinable()) pv_worker.join();
    if (up_worker.joinable()) up_worker.join();
    if (worker.joinable()) {
        if (!crawl_done) std::_Exit(0);  // Esc mid-crawl: the worker still holds our fetchers
        worker.join();
    }
    return 0;
}

}  // namespace wc
