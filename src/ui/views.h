// views.h - state in, Element out. No input handling, no timers here,
// so every view renders headless (tests/unit/ui_test.cc snapshots).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/graph.h"
#include "ui/layout.h"
#include "ui/progress.h"
#include "ui/replay.h"
#include "ui/scene.h"
#include "ui/theme.h"

namespace wc::views {

// The graph, drawn on an ftxui::Canvas that fills the given box. zoom = the
// camera's (1 = fit): far out (< 0.6) edges get sparser dots. pulse_ms = anim::now_ms()
// lets a few SEEN edges carry traffic pulses once f.done (redraw ~10 fps); -1 = off.
ftxui::Element graph(std::shared_ptr<const Scene> scene, ReplayFrame f, View v, double zoom = 1,
                     int64_t pulse_ms = -1);

// Border of a focusable part (omnibar, preview, the boss-bar rule for the graph).
// Unfocused: faint grey. Focused: glows dim orange <-> orange with glow in [0,1],
// pulled toward white by flash in [0,1] (1 = focus just arrived, fades over 300 ms).
ftxui::Color focus_border(bool focused, float glow, float flash);

// Top bar: two meters. flash = limit just hit (caller pulses it 1-2x).
// rule = colour of the line under it (focus_border of the graph).
ftxui::Element boss_bar(int depth, int max_depth, int size, int max_size,
                        bool flash_depth, bool flash_size, ftxui::Color rule = theme::kFaint);

// The one look for every text input: white on black, rounded border (NO_COLOR: border + plain text).
ftxui::Element input_box(ftxui::Element input, ftxui::Color border = theme::kAccent);

// Every Input's InputOption::transform: white text, dim placeholder, never inverted
// (FTXUI's default inverts the focused input: black on white). The real terminal cursor marks the caret.
ftxui::Element input_text(ftxui::Element input, bool placeholder);

// One prompt field: "label ╭› input╮", label on the box's text row (not its border), orange "› ",
// input `width` cols. Empty label = box only. Centre the result as one block.
ftxui::Element field(const std::string& label, ftxui::Element input, int width);

// Bottom bar: `input` is the live Input component's Element. Results drop up.
ftxui::Element omnibar(ftxui::Element input, const std::vector<std::string>& results,
                       int selected, const std::string& ghost, ftxui::Color border = theme::kAccent);

ftxui::Element toast(const std::string& text);   // corner overlay, dbox it on top
ftxui::Element too_small(int w, int h);          // < 80x24 guard

// Floating page preview. border = focus_border(...); lines from html_to_lines;
// "# " lines render as headings.
ftxui::Element preview(const std::string& title, const std::vector<std::string>& lines,
                       int scroll, ftxui::Color border, bool maximized);

// Onboarding coach mark: "1/3  text" + dim "Tab next · Esc skip".
ftxui::Element coach(int step, int total, const std::string& text);

// Crawl loading screen: spinner + rotating `line`, counters, then a checklist
// of the fetches in p.recent (oldest first, so the newest sits at the bottom).
// Give it `loading_rows(h)` fetches: as many as fit a terminal h rows tall.
// pulse in [0,1] breathes the border (1 = bright; pass 1 with --no-anim).
// `background` is drawn behind everything (hook for a later effect layer;
// pass emptyElement() for none).
struct LoadingState {
    std::string host;   // short_label(start)
    std::string line;   // loading_line(ms)
    int tick = 0;       // spinner frame
    float pulse = 1;
    int cols = 80;      // terminal width: the box is min(64, cols - 2) wide
    int max_links = 0;  // "links" counter cap (Max links = graph size); 0 = no cap
    Progress progress;
    // Checklist fades: a new line fades in (faint -> its colour, 400 ms, ease-out,
    // timed from FetchLine::at_ms); with a full list the oldest (top) line fades
    // out over 400 ms since the newest landed, before it scrolls off.
    // fade = false (--no-anim / NO_COLOR): every line at its final colour.
    bool fade = false;
    int64_t now_ms = 0;  // anim::now_ms()
    int rows = 0;        // list capacity (loading_rows); 0 = never treat it as full
};
int loading_rows(int term_h);
ftxui::Element loading(const LoadingState& s, ftxui::Element background);

}  // namespace wc::views
