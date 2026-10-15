// josh_fetcher.h - plugs Josh's josh_get_links() (src/team/josh.h) into the crawler.
// Teammates never need to open this file.
#pragma once

#include "core/link_source.h"

namespace wc {

class JoshFetcher : public LinkSource {
public:
    std::optional<std::vector<std::string>> links(const std::string& url) override;
};

}  // namespace wc
