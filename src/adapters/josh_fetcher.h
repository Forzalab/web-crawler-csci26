// josh_fetcher.h - adapter for part #1 (Josh: fetch a page, return its URLs).
// Josh's own signature may only appear in josh_fetcher.cc next to `// WIRE:`.
#pragma once

#include "core/link_source.h"

namespace wc {

class JoshFetcher : public LinkSource {
public:
    std::optional<std::vector<std::string>> links(const std::string& url) override;
};

}  // namespace wc
