// term_lock.h - no tearing: one write(2) per frame, wrapped in synchronized output.
// FTXUI builds each frame into one string and flushes std::cout once per frame
// (ScreenInteractive::Draw -> TerminalFlush). TermLock swaps std::cout's buffer so
// each flush becomes exactly one write of  CSI ?2026 h  <frame>  CSI ?2026 l  to fd 1:
// the terminal shows the whole frame at once, never row by row (terminals without
// mode 2026 ignore it). Alt screen, hidden cursor, mouse capture and terminal restore
// on quit / Ctrl-C / crash signals are FTXUI's (ScreenInteractive::Fullscreen).
#pragma once

#include <streambuf>
#include <string>

namespace wc {

class FrameSink : public std::streambuf {
public:
    explicit FrameSink(int fd) : fd_(fd) {}
    ~FrameSink() override { sync(); }

protected:
    int_type overflow(int_type c) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int sync() override;  // flush = one bracketed write

private:
    int fd_;
    std::string buf_;
};

// RAII: std::cout -> FrameSink(STDOUT) while alive; restores the old buffer.
class TermLock {
public:
    TermLock();
    ~TermLock();
    TermLock(const TermLock&) = delete;
    TermLock& operator=(const TermLock&) = delete;

private:
    FrameSink sink_;
    std::streambuf* old_;
};

}  // namespace wc
