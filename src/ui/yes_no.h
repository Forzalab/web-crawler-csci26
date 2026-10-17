// yes_no.h - the one rule for every yes/no prompt in the TUI.
// y/Y = yes and n/N = no, answered at once (no Enter). Enter keeps the default.
#pragma once

#include <optional>

namespace wc {

// key = the typed character, or '\n' for Enter. nullopt = not an answer, ignore it.
std::optional<bool> yes_no_answer(char key, bool default_yes);

}  // namespace wc
