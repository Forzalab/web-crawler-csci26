// front_ends.h - the two front ends main() picks between.
#pragma once

namespace wc {

int pipe_main(int argc, char** argv);  // stdin not a tty: A3 report on stdout
int tui_main(int argc, char** argv);   // stdin is a tty: FTXUI screen
int links_main(int argc, char** argv); // --links URL: one real fetch (smoke test)

}  // namespace wc
