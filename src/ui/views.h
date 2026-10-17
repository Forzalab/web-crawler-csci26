// views.h - state in, Element out. No input handling, no timers here,
// so every view renders headless (tests/unit/ui_test.cc snapshots).
#pragma once

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "core/graph.h"
#include "ui/layout.h"
#include "ui/replay.h"

namespace wc::views {

// The graph, drawn on an ftxui::Canvas that fills the given box.
ftxui::Element graph(const CrawlGraph& g, const Layout& l, const ReplayFrame& f, View v);

// Top bar: two meters. flash = limit just hit (caller pulses it 1-2x).
ftxui::Element boss_bar(int depth, int max_depth, int size, int max_size,
                        bool flash_depth, bool flash_size);

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

}  // namespace wc::views
