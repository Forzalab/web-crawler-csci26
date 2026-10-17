// html_text.h - HTML -> readable lines for the preview panel. Pure.
// Drops script/style/noscript/svg/head. h1-h6 -> "# ", li -> "- ",
// <img alt=x> -> "[IMG x]", p/br/div/tr -> line breaks, entities decoded,
// whitespace collapsed, no blank-line runs > 1. Lines NOT wrapped (FTXUI wraps).
#pragma once

#include <string>
#include <vector>

namespace wc {

std::vector<std::string> html_to_lines(const std::string& html);

}  // namespace wc
