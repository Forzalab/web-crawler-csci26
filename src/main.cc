// main.cc - stdin redirected (tests, `wc < file`) -> pipe mode; terminal -> TUI.
// --pipe / --tui override the auto-detect. --links URL = one real fetch, then exit.
// --report URL = one headless crawl into a run dir (stats.json, failures.tsv, graph.json).
#include <unistd.h>

#include <cstring>

#include "ui/front_ends.h"

int main(int argc, char** argv) {
    bool pipe = !isatty(0);
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--pipe")) pipe = true;
        if (!std::strcmp(argv[i], "--tui")) pipe = false;
        if (!std::strcmp(argv[i], "--links")) return wc::links_main(argc, argv);
        if (!std::strcmp(argv[i], "--report")) return wc::report_main(argc, argv);
    }
    return pipe ? wc::pipe_main(argc, argv) : wc::tui_main(argc, argv);
}
