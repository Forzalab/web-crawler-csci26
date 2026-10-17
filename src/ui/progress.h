// progress.h - what the loading screen shows while crawl() runs on its thread.
// crawl() hands back the graph only at the end, so ProgressTap sits between
// crawl() and the real LinkSource and notes every fetch as it happens. The UI
// thread reads a copy (snapshot) under the mutex; the graph itself is never
// touched until the worker is done. Pure: no FTXUI.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/link_source.h"

namespace wc {

struct FetchLine {
    std::string url;
    int depth = 0;
    bool ok = false;
};

struct Progress {
    int pages = 0, ok = 0, failed = 0;  // fetches so far (pages = ok + failed)
    int links = 0;                      // links seen on fetched pages (dupes kept)
    int depth = 0;                      // deepest page fetched
    std::vector<FetchLine> recent;      // last n fetches, oldest first
};

class ProgressTap : public LinkSource {
public:
    explicit ProgressTap(LinkSource& inner) : inner_(inner) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override;
    // Counters plus the newest `last_n` fetches (all of them if last_n == 0).
    Progress snapshot(size_t last_n = 0) const;

private:
    LinkSource& inner_;
    mutable std::mutex mu_;
    Progress all_;  // recent = every fetch, in order
    std::unordered_map<std::string, int> depth_of_;  // first fetch = depth 0, its links = 1, ...
};

// Rotating one-liner for the loading text, a new one every `period_ms`.
std::string loading_line(int64_t elapsed_ms, int period_ms = 2000);

}  // namespace wc
