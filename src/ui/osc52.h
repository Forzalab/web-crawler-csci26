// osc52.h - copy to the user's clipboard via the terminal (works over SSH).
// Pure: builds the escape string; caller writes it to the tty.
#pragma once

#include <string>

namespace wc {

std::string base64(const std::string& in);
std::string osc52(const std::string& text);  // "\x1b]52;c;<b64>\x07"

}  // namespace wc
