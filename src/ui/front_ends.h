// front_ends.h - the two front ends main() picks between.
#pragma once

namespace wc {

// Defaults a person gets in the TUI prompts and in --report (the spec 1000 is for pipe mode).
constexpr int kDefaultDepth = 3;
constexpr int kDefaultLinks = 50;

int pipe_main(int argc, char** argv);  // stdin not a tty: A3 report on stdout
int tui_main(int argc, char** argv);   // stdin is a tty: FTXUI screen
int links_main(int argc, char** argv); // --links URL: one real fetch (smoke test)
int report_main(int argc, char** argv); // --report URL: headless crawl -> run dir (report)

}  // namespace wc
