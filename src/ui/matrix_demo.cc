// matrix_demo.cc - hidden full-screen preview: WC_MATRIX_DEMO=1 ./build/wc
// Keys: q / Esc quit, o toggle green <-> orange (advanced mode).
#include <atomic>
#include <chrono>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "ui/matrix.h"
#include "ui/theme.h"

namespace wc::matrix {

int demo() {
    using namespace ftxui;
    auto screen = ScreenInteractive::Fullscreen();
    MatrixRain rain(0x5eedu);
    bool orange = false;
    auto label = [&] {
        return hbox({text(" MATRIX ") | bold | color(orange ? theme::kAccent : kGreen),
                     text("  o colour · q quit ") | color(theme::kChrome)}) |
               border | color(theme::kChrome) | center;
    };
    auto ui = Renderer([&] { return dbox({rain.render(orange ? theme::kAccent : kGreen), label()}); });
    ui = CatchEvent(ui, [&](Event e) {
        if (e == Event::Character('q') || e == Event::Escape) return screen.Exit(), true;
        if (e == Event::Character('o')) return orange = !orange, true;
        return false;
    });
    std::atomic<bool> run{true};
    std::thread clock([&] {
        while (run) {
            std::this_thread::sleep_for(std::chrono::milliseconds(70));
            screen.Post([&] { rain.tick(); });
            screen.PostEvent(Event::Custom);
        }
    });
    screen.Loop(ui);
    run = false;
    clock.join();
    return 0;
}

}  // namespace wc::matrix
