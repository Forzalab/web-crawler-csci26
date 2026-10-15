#include "adapters/josh_fetcher.h"

// WIRE: #include "<Josh's header>"

namespace wc {

std::optional<std::vector<std::string>> JoshFetcher::links(const std::string& url) {
    // WIRE: call Josh's fetch here and convert to our contract:
    //   - links in page order, duplicates kept
    //   - any failure (exception, bad status, not HTML) -> return std::nullopt
    //   e.g.  try { return fetch(url); } catch (...) { return std::nullopt; }
    // Keep this under ~30 lines; a bigger mismatch is a contract talk on Discord.
    (void)url;
    return std::nullopt;  // not wired yet: every fetch fails
}

}  // namespace wc
