// tui_main.cc - interactive front end. One step on screen at a time:
// URL -> Visualize? -> Max depth -> Max pages -> crawl (live loading screen) -> graph + omnibar.
// Flags: --no-anim (or NO_COLOR) jumps the replay to the end, no glow.
// Graph keys (empty omnibar): arrows pan, + - zoom (or wheel), 0 fit.
// Enter on a result opens its page preview (cache, else live fetch).
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#include <ftxui/component/animation.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include "adapters/josh_fetcher.h"
#include "core/crawl.h"
#include "net/cache.h"
#include "net/https_fetcher.h"
#include "ui/camera.h"
#include "ui/fixture_source.h"
#include "ui/front_ends.h"
#include "ui/html_text.h"
#include "ui/layout.h"
#include "ui/omni_query.h"
#include "ui/osc52.h"
#include "ui/progress.h"
#include "ui/replay.h"
#include "ui/theme.h"
#include "ui/views.h"

namespace wc {

using namespace ftxui;
namespace th = wc::theme;
using Clock = std::chrono::steady_clock;

namespace {

// Onboarding coach marks, first run only. Copy lives here so it is easy to rewrite.
const std::vector<std::string> kCoach = {
    "Type to find a page.",
    "Enter opens it. Esc closes.",
    "/bridges to share.",
};

enum class Step { Url, Visualize, Depth, Pages, Crawling, Main, Summary };

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

std::string bridges_url() {
    if (const char* u = std::getenv("WC_BRIDGES_URL")) return u;
    // TODO: real BRIDGES user/assignment once posting is wired.
    return "https://bridges-cs.herokuapp.com/assignments/1/<user>";
}

// Centered prompt column: question, the live input, a dim footer.
Element step_screen(Element body, const std::string& footer) {
    return vbox({filler(), hcenter(std::move(body)), filler(),
                 hcenter(text(footer) | color(th::kChrome) | dim)}) |
           flex;
}

Element url_screen(const std::vector<std::string>& chips, Element input) {
    Elements chip_row;
    for (const auto& c : chips) {
        chip_row.push_back(text("[ " + short_label(c) + " ✕ ] ") | color(th::kAccent));
    }
    return step_screen(vbox({hbox(std::move(chip_row)), hbox({text("› ") | bold | color(th::kAccent),
                                                                 std::move(input) | size(WIDTH, EQUAL, 50)})}),
                       "Enter crawl · Tab add URL · Esc quit");
}

Element prompt_screen(const std::string& q, Element input) {
    return step_screen(hbox({text(q + "  ") | bold | color(th::kText), std::move(input) | size(WIDTH, EQUAL, 8)}),
                       "Enter accept default · Esc quit");
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
    return step_screen(vbox({text(g.nodes.empty() ? "?" : short_label(g.nodes[0].url)) | bold | color(th::kAccent), text(""),
                             row("pages", int(g.nodes.size())), row("links", int(g.edges.size())), row("fetched", ok),
                             row("failed", bad), row("unfetched", un), row("max depth", maxd)}),
                       "Esc quit");
}

}  // namespace

int tui_main(int argc, char** argv) {
    bool no_anim = std::getenv("NO_COLOR") != nullptr;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-anim")) no_anim = true;
    bool first_run = !std::ifstream(seen_path()).good();

    auto screen = ScreenInteractive::Fullscreen();
    Step step = Step::Url;
    CrawlConfig cfg;
    cfg.maxSize = 50;
    bool visualize = first_run;

    // URL step. MVP: only chips[0] (or the typed URL) is crawled.
    // TODO: crawl every chip.
    std::vector<std::string> chips;
    std::string url_text, prompt_text, omni_text;
    int prompt_cursor = 0, omni_cursor = 0;
    InputOption url_opt, prompt_opt, omni_opt;
    url_opt.placeholder = "Paste a URL. Enter to crawl.";
    omni_opt.placeholder = "";  // ghost carries the hint (main_screen)
    url_opt.multiline = prompt_opt.multiline = omni_opt.multiline = false;
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
    std::thread worker, ticker;
    std::atomic<bool> crawl_done{false}, quitting{false};
    int tick = 0;
    Clock::time_point crawl_t0;

    // Main screen state.
    Layout lay;
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
    Clock::time_point pv_at;
    std::thread pv_worker;
    std::atomic<bool> pv_busy{false};

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

    auto begin_prompt = [&](Step s, const std::string& def) {
        step = s;
        prompt_text = def;
        prompt_cursor = int(def.size());
        tab = 1;
    };
    auto start_crawl = [&] {
        step = Step::Crawling;
        crawl_t0 = Clock::now();
        worker = std::thread([&] {
            CrawlGraph r = crawl(start, cfg, tap, pause);
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
        if (!visualize) {
            step = Step::Summary;
            return;
        }
        lay = radial_layout(g);
        t0 = last_frame = last_input = Clock::now();
        step = Step::Main;
        tab = 2;
        refresh_hits();
    };

    auto open_preview = [&](const Node& n) {
        pv_open = true;
        pv_scroll = 0;
        pv_at = Clock::now();
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
    };

    auto main_screen = [&]() -> Element {
        int64_t el = no_anim ? INT64_MAX / 4 : ms_since(t0);
        ReplayFrame f = replay_at(g, el);
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
        float glow = no_anim ? 1.0f : float(0.5 + 0.5 * std::sin(ms_since(pv_at) / 1000.0 * 2 * M_PI / 1.2));
        if (!f.done || flash_live || toast_live || fit_wait || cam_moving || (pv_open && !no_anim))
            animation::RequestAnimationFrame();

        std::vector<std::string> rows;
        if (bridges_shown) {
            rows.push_back(bridges_url() + "   [c] copy");
        } else {
            for (int id : hits) {
                const Node& n = g.nodes[id];
                rows.push_back(std::string(th::glyph(size_t(id) < lay.shape.size() ? lay.shape[id] : -1, n.status)) + " " + node_label(n.url, g.nodes[0].url) + "  d" + std::to_string(n.depth));
            }
        }
        std::string ghost = complete(omni_text);
        ghost = ghost.size() > omni_text.size() ? ghost.substr(omni_text.size()) : "";
        if (omni_text.empty()) ghost = "type to search \u00b7 /bridges";

        // Canvas sub-pixels: 2 per col, 4 per row, minus boss bar (2) + omnibar (3 + results).
        auto ts = Terminal::Size();
        int rows_left = std::max(1, ts.dimy - 2 - 3 - int(rows.size()));
        View v = apply(cam, fit(lay, ts.dimx * 2, rows_left * 4), ts.dimx * 2, rows_left * 4);

        Element body = vbox({views::boss_bar(f.depth, cfg.maxDepth, f.size, cfg.maxSize, fd, fs),
                             views::graph(g, lay, f, v) | flex,
                             views::omnibar(omni_in->Render(), rows, bridges_shown ? 0 : selected, ghost)});
        // Overlays, back to front: preview (right, v-centered), coach (above omnibar), toast.
        if (pv_open) {
            pv_scroll = std::clamp(pv_scroll, 0, std::max(0, int(pv_lines.size()) - 1));
            Element p = views::preview(pv_title, pv_lines, pv_scroll, glow, pv_max);
            p = pv_max ? p | flex : p | size(HEIGHT, LESS_THAN, ts.dimy * 7 / 10);
            body = dbox({body, pv_max ? p : hbox({filler(), vbox({filler(), p, filler()}), text(" ")})  /* 1-col gap: borders join */});
        }
        if (!pv_open && coach_step >= 0 && coach_step < int(kCoach.size())) {  // never over the preview
            Element c = views::coach(coach_step + 1, int(kCoach.size()), kCoach[coach_step]);
            body = dbox({body, vbox({filler(), hbox({filler(), c, filler()}),
                                     emptyElement() | size(HEIGHT, EQUAL, 3 + int(rows.size()))})});
        }
        if (toast_live)
            body = dbox({body, vbox({hbox({filler(), views::toast(toast_text)}), filler()})});
        return body;
    };

    // Loading screen: spinner, rotating one-liner, live counters, a checklist of
    // fetches as they land, border breathing over 1.6 s. Background layer empty for now.
    auto crawling_screen = [&](Dimensions ts) -> Element {
        int64_t ms = ms_since(crawl_t0);
        views::LoadingState st;
        st.host = short_label(start);
        st.line = loading_line(ms);
        st.tick = tick;
        st.cols = ts.dimx;
        st.pulse = no_anim ? 1.0f : float(0.5 + 0.5 * std::sin(ms / 1000.0 * 2 * M_PI / 1.6));
        st.progress = tap.snapshot(size_t(views::loading_rows(ts.dimy)));
        return views::loading(st, emptyElement());
    };

    auto view = Renderer(inputs, [&]() -> Element {
        auto ts = Terminal::Size();
        // Only the graph needs the room: prompts, loading and summary fit a phone (~50 cols).
        if (step == Step::Main && (ts.dimx < 80 || ts.dimy < 24)) return views::too_small(ts.dimx, ts.dimy);
        switch (step) {
            case Step::Url: return url_screen(chips, url_in->Render());
            case Step::Visualize: return prompt_screen(first_run ? "Visualize? [Y/n]" : "Visualize? [y/N]", prompt_in->Render());
            case Step::Depth: return prompt_screen("Max depth [3]", prompt_in->Render());
            case Step::Pages: return prompt_screen("Max pages [50]", prompt_in->Render());
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
            toast("wiki · depth:<3 · status:failed · /bridges /help /quit");
            return set_omni("");
        }
        if (q.verb == "bridges") return void(bridges_shown = true);
        if (q.verb == "close") {
            close_preview();
            return set_omni(""), refresh_hits();
        }
        if (!q.verb.empty()) return toast("unknown /" + q.verb);
        if (selected < int(hits.size())) open_preview(g.nodes[hits[selected]]);
    };

    auto app = CatchEvent(view, [&](Event e) {
        if (e == Event::Custom) {
            if (step == Step::Crawling) {
                ++tick;
                if (crawl_done) finish_crawl();
            }
            return false;
        }
        if (step == Step::Main) last_input = Clock::now();
        if (e == Event::Escape) {
            // Main: Esc peels one layer: omnibar text, then coach, then preview, then quit.
            if (step == Step::Main && (bridges_shown || !omni_text.empty())) {
                bridges_shown = false;
                set_omni("");
                refresh_hits();
                return true;
            }
            if (step == Step::Main && coach_step >= 0) return coach_step = -1, true;
            if (step == Step::Main && pv_open) return close_preview(), true;
            screen.Exit();
            return true;
        }
        auto int_or = [](const std::string& s, int d) {
            try { return s.empty() ? d : std::max(0, std::stoi(s)); } catch (...) { return d; }
        };
        switch (step) {
            case Step::Url:
                if (e == Event::Tab && !url_text.empty()) {
                    chips.push_back(url_text);
                    url_text.clear();
                    return true;
                }
                if (e == Event::Return) {
                    start = !chips.empty() ? chips[0] : url_text;
                    if (start.empty()) return true;
                    if (start.find("://") == std::string::npos) start = "https://" + start;
                    begin_prompt(Step::Visualize, "");
                    return true;
                }
                return false;
            case Step::Visualize:
                if (e == Event::Return) {
                    if (!prompt_text.empty()) visualize = prompt_text[0] == 'y' || prompt_text[0] == 'Y';
                    begin_prompt(Step::Depth, "");
                    return true;
                }
                return false;
            case Step::Depth:
                if (e == Event::Return) {
                    cfg.maxDepth = int_or(prompt_text, 3);
                    begin_prompt(Step::Pages, "");
                    return true;
                }
                return false;
            case Step::Pages:
                if (e == Event::Return) {
                    cfg.maxSize = std::max(1, int_or(prompt_text, 50));
                    start_crawl();
                    return true;
                }
                return false;
            case Step::Crawling:
            case Step::Summary: return true;
            case Step::Main: break;
        }
        // Main: omnibar keys.
        if (bridges_shown && e == Event::Character('c')) {
            std::cout << osc52(bridges_url());
            std::cout.flush();
            toast("copy sent ✓");
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
        // Camera + preview keys, only while the omnibar is empty (else they are typing).
        bool empty = omni_text.empty() && !bridges_shown;
        auto zoom = [&](double k) { cam_target.zoom = std::clamp(cam_target.zoom * k, 0.25, 16.0); };
        double pan = 0.25 / cam_target.zoom;  // world units (ring = 1), same feel at any zoom
        if (e.is_mouse() && e.mouse().button == Mouse::WheelUp) return zoom(1.25), true;
        if (e.is_mouse() && e.mouse().button == Mouse::WheelDown) return zoom(1 / 1.25), true;
        if (pv_open && e == Event::PageDown) return pv_scroll += 10, true;
        if (pv_open && e == Event::PageUp) return pv_scroll = std::max(0, pv_scroll - 10), true;
        if (empty) {
            if (e == Event::Character('+') || e == Event::Character('=')) return zoom(1.25), true;
            if (e == Event::Character('-')) return zoom(1 / 1.25), true;
            if (e == Event::Character('0')) return cam_target = Camera{}, true;
            if (e == Event::ArrowLeft) return cam_target.px += pan, true;
            if (e == Event::ArrowRight) return cam_target.px -= pan, true;
            if (e == Event::ArrowUp) return cam_target.py += pan, true;
            if (e == Event::ArrowDown) return cam_target.py -= pan, true;
            if (pv_open && e == Event::Character('j')) return ++pv_scroll, true;
            if (pv_open && e == Event::Character('k')) return pv_scroll = std::max(0, pv_scroll - 1), true;
            if (pv_open && e == Event::Character('m')) return pv_max = !pv_max, true;
        }
        if (e == Event::ArrowUp) return selected = std::min(selected + 1, std::max(0, int(hits.size()) - 1)), true;  // results drop up
        if (e == Event::ArrowDown) return selected = std::max(selected - 1, 0), true;
        if (e == Event::Tab) {
            std::string c = complete(omni_text);
            if (!c.empty()) set_omni(c);
            refresh_hits();
            return true;
        }
        if (e == Event::Return) {
            on_omni_enter();
            return true;
        }
        if (bridges_shown) bridges_shown = false;
        bool handled = inputs->OnEvent(e);
        refresh_hits();
        return handled;
    });

    screen.Loop(app);
    quitting = true;
    if (ticker.joinable()) ticker.join();
    if (pv_busy) std::_Exit(0);  // a preview fetch is mid-network and holds `tls`
    if (pv_worker.joinable()) pv_worker.join();
    if (worker.joinable()) {
        if (!crawl_done) std::_Exit(0);  // Esc mid-crawl: the worker still holds our fetchers
        worker.join();
    }
    return 0;
}

}  // namespace wc
