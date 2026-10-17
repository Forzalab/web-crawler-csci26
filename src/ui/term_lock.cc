// term_lock.cc - see term_lock.h.
#include "ui/term_lock.h"

#include <cerrno>
#include <iostream>
#include <unistd.h>

namespace wc {

FrameSink::int_type FrameSink::overflow(int_type c) {
    if (c != traits_type::eof()) buf_ += traits_type::to_char_type(c);
    return traits_type::not_eof(c);
}

std::streamsize FrameSink::xsputn(const char* s, std::streamsize n) {
    buf_.append(s, size_t(n));
    return n;
}

int FrameSink::sync() {
    std::string out;
    out.reserve(buf_.size() + 16);
    for (char c : buf_)
        if (c != '\0') out += c;  // FTXUI ends every flush with a NUL (an Emscripten marker)
    buf_.clear();
    if (out.empty()) return 0;
    out = "\x1b[?2026h" + out + "\x1b[?2026l";
    for (size_t off = 0; off < out.size();) {  // one write; loop only on a short write
        ssize_t w = ::write(fd_, out.data() + off, out.size() - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        off += size_t(w);
    }
    return 0;
}

TermLock::TermLock() : sink_(STDOUT_FILENO) {
    std::cout.flush();
    old_ = std::cout.rdbuf(&sink_);
}

TermLock::~TermLock() {
    std::cout.flush();
    std::cout.rdbuf(old_);
}

}  // namespace wc
