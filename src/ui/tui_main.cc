// tui_main.cc - interactive front end. PLACEHOLDER: Tony designs the layout later.
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "ui/front_ends.h"

namespace wc {

int tui_main(int, char**) {
    using namespace ftxui;
    auto screen = ScreenInteractive::Fullscreen();
    auto view = Renderer([] {
        return vbox({
                   text("CSCI 26 Web Crawler") | bold | center,
                   separator(),
                   text("layout TBD") | center,
                   filler(),
                   text("q: quit") | dim | center,
               }) |
               border;
    });
    auto app = CatchEvent(view, [&](Event e) {
        if (e == Event::Character('q') || e == Event::Escape) {
            screen.Exit();
            return true;
        }
        return false;
    });
    screen.Loop(app);
    return 0;
}

}  // namespace wc
