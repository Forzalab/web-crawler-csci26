// osc52.cc - see osc52.h
#include "ui/osc52.h"

namespace wc {

std::string base64(const std::string& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out += k[v >> 18 & 63];
        out += k[v >> 12 & 63];
        out += k[v >> 6 & 63];
        out += k[v & 63];
    }
    size_t rest = in.size() - i;
    if (rest) {
        unsigned v = (unsigned char)in[i] << 16 | (rest == 2 ? (unsigned char)in[i + 1] << 8 : 0);
        out += k[v >> 18 & 63];
        out += k[v >> 12 & 63];
        out += rest == 2 ? k[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

std::string osc52(const std::string& text) { return "\x1b]52;c;" + base64(text) + "\x07"; }

}  // namespace wc
