// views.h - state in, Element out. No input handling, no timers here,
// so every view renders headless (tests/unit/ui_test.cc snapshots).
#pragma once

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/graph.h"
#include "ui/layout.h"
#include "ui/progress.h"
#include "ui/replay.h"

namespace wc::views {

// The graph, drawn on an ftxui::Canvas that fills the given box.
ftxui::Element graph(const CrawlGraph& g, const Layout& l, const ReplayFrame& f, View v);

// Top bar: two meters. flash = limit just hit (caller pulses it 1-2x).
ftxui::Element boss_bar(int depth, int max_depth, int size, int max_size,
                        bool flash_depth, bool flash_size);

// The one look for every text input: white on black, rounded border (NO_COLOR: border + plain text).
ftxui::Element input_box(ftxui::Element input);

// Bottom bar: `input` is the live Input component's Element. Results drop up.
ftxui::Element omnibar(ftxui::Element input, const std::vector<std::string>& results,
                       int selected, const std::string& ghost);

ftxui::Element toast(const std::string& text);   // corner overlay, dbox it on top
ftxui::Element too_small(int w, int h);          // < 80x24 guard

// Floating page preview. glow in [0,1] = focus pulse phase (orange border
// brightness); lines from html_to_lines; "# " lines render as headings.
ftxui::Element preview(const std::string& title, const std::vector<std::string>& lines,
                       int scroll, float glow, bool maximized);

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
    Progress progress;
};
int loading_rows(int term_h);
ftxui::Element loading(const LoadingState& s, ftxui::Element background);

}  // namespace wc::views
