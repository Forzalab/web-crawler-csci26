// sleeper.h - the 0.1 s politeness delay, injected so tests never really sleep.
#pragma once

namespace wc {

struct Sleeper {
    virtual ~Sleeper() = default;
    virtual void sleep_ms(int ms) = 0;
};

// Real delay (usleep). Used by the TUI / real crawls only.
struct RealSleeper : Sleeper {
    void sleep_ms(int ms) override;
};

// Counts instead of sleeping. Pipe mode prints `calls` as SLEEPS.
struct NoSleeper : Sleeper {
    int calls = 0;
    void sleep_ms(int) override { ++calls; }
};

}  // namespace wc
