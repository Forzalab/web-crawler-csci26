// progress.cc - see progress.h
#include "ui/progress.h"

#include <algorithm>
#include <iterator>

#include "ui/anim.h"

namespace wc {

namespace {

// Short, witty, nothing offensive. Keep each under ~40 columns (phone terminals).
const char* const kLines[] = {
    "untangling the web…",
    "asking robots.txt nicely…",
    "following links so you don't have to…",
    "counting hops from home…",
    "politely knocking on servers…",
    "16 fetchers, one polite crawler…",
    "dusting off old hyperlinks…",
    "reading HTML so you won't…",
    "sorting NEW from SEEN…",
    "spinning a tiny web of our own…",
    "herding anchor tags…",
    "breadth first, questions later…",
    "waiting on a slow server, probably…",
    "collecting 404s like stamps…",
    "measuring the internet in hops…",
    "peeking behind the <a href>…",
    "warming up the cache…",
    "respecting the crawl delay…",
    "Esc stops, /resume picks it up…",
    "mapping the neighbourhood…",
};

}  // namespace

std::optional<std::vector<std::string>> ProgressTap::links(const std::string& url) {
    auto r = inner_.links(url);
    std::lock_guard<std::mutex> lk(mu_);
    // First fetch is the start page (depth 0). A url never seen as a link
    // (the fetcher changed it) keeps the depth of the page before it: BFS never goes back up.
    int d = all_.recent.empty() ? 0 : all_.recent.back().depth;
    if (auto it = depth_of_.find(url); it != depth_of_.end()) d = it->second;
    depth_of_.emplace(url, d);
    all_.recent.push_back({url, d, r.has_value(), anim::now_ms()});
    ++all_.pages;
    (r ? all_.ok : all_.failed)++;
    all_.depth = std::max(all_.depth, d);
    if (r) {
        all_.links += int(r->size());
        for (const auto& l : *r) {  // parallel crawls land out of order: keep the smallest depth
            auto [it, fresh] = depth_of_.emplace(l, d + 1);
            if (!fresh) it->second = std::min(it->second, d + 1);
        }
    }
    return r;
}

Progress ProgressTap::snapshot(size_t last_n) const {
    std::lock_guard<std::mutex> lk(mu_);
    Progress p = all_;
    if (last_n && p.recent.size() > last_n) p.recent.erase(p.recent.begin(), p.recent.end() - last_n);
    return p;
}

std::string loading_line(int64_t elapsed_ms, int period_ms) {
    size_t n = std::size(kLines);
    int64_t i = std::max<int64_t>(0, elapsed_ms) / std::max(1, period_ms);
    return kLines[size_t(i % int64_t(n))];
}

}  // namespace wc
