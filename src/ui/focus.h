// focus.h - which part of the main screen takes the keys. Pure.
// Omni (the search bar, default), Preview (scrolls the open page), Graph
// (camera keys even with text in the bar). Tab cycles, Esc steps back to Omni.
#pragma once

namespace wc {

enum class Focus { Omni, Preview, Graph };

// Omni -> Preview -> Graph -> Omni; Preview only while one is open.
inline Focus next_focus(Focus f, bool pv_open) {
    switch (f) {
        case Focus::Omni: return pv_open ? Focus::Preview : Focus::Graph;
        case Focus::Preview: return Focus::Graph;
        case Focus::Graph: return Focus::Omni;
    }
    return Focus::Omni;
}

}  // namespace wc
