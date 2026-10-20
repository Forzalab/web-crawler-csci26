// stream_fetch.h - assignment spec #1, the way Kerney's demo does it: pull one page
// over boost::asio::ip::tcp::iostream, read the reply with readlib's readline(),
// return the URLs found. Emergency fallback for Josh's josh_get_links().
// http:// only (an https URL or a redirect to https = ok false, our TLS fetcher
// takes it). Built without Boost: always ok false. Thread safe (no globals).
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/link_source.h"

namespace wc {

// Links on the page at `url` (page order, resolved, repeats kept). *ok = false on any
// failure: bad URL, https, connect, 10 s timeout, non-2xx, >5 redirects, not HTML.
std::vector<std::string> stream_get_links(const std::string& url, bool* ok);

bool stream_fetch_built();  // false = compiled without Boost (the stub)

// LinkSource for RoutedFetcher. Remembers the last URL it answered (links_main logs via=stream).
class StreamFetcher : public LinkSource {
public:
    std::optional<std::vector<std::string>> links(const std::string& url) override;
    std::string last_ok() const;  // "" = it has not answered anything yet

private:
    mutable std::mutex mu_;
    std::string last_ok_;
};

}  // namespace wc
