#include "adapters/josh_fetcher.h"

#include "team/josh.h"

namespace wc {

// JoshPage -> LinkSource contract: ok == false (or anything thrown) means the fetch failed.
std::optional<std::vector<std::string>> JoshFetcher::links(const std::string& url) {
    try {
        JoshPage page = josh_get_links(url);
        if (!page.ok) return std::nullopt;
        return page.links;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace wc
