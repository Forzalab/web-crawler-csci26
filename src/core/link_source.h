// link_source.h - the ONLY thing crawl() knows about "the web".
// Real: JoshFetcher (adapters/). Tests/pipe mode: MockFetcher (mocks/).
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace wc {

struct LinkSource {
    virtual ~LinkSource() = default;
    // Outgoing links of `url` in page order (duplicates kept, crawl() dedupes).
    // std::nullopt = fetch FAILED (404, timeout, not HTML...). An empty vector
    // = fetch OK, page has no links. Never throws.
    virtual std::optional<std::vector<std::string>> links(const std::string& url) = 0;
};

}  // namespace wc
