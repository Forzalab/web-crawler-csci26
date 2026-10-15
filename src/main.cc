// main.cc - stdin redirected (tests, `wc < file`) -> pipe mode; terminal -> TUI.
// --pipe / --tui override the auto-detect.
#include <unistd.h>

#include <cstring>

#include "ui/front_ends.h"

int main(int argc, char** argv) {
    bool pipe = !isatty(0);
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--pipe")) pipe = true;
        if (!std::strcmp(argv[i], "--tui")) pipe = false;
    }
    return pipe ? wc::pipe_main(argc, argv) : wc::tui_main(argc, argv);
}
