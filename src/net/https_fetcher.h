// https_fetcher.h - our own fetch path (TLS owner = Claude, decision 19:04).
// Fetches http:// and https://, follows redirects (301/302/303/307/308, max 5),
// keeps only 2xx HTML, returns its <a href> links. Implements LinkSource.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/link_source.h"
#include "net/http.h"
#include "net/url.h"

namespace wc {

struct FetchResult {
    std::string url;           // as asked
    std::string final_url;     // after redirects ("" if the url did not parse)
    int status = 0;            // last HTTP status, 0 = none
    int redirects = 0;
    std::string content_type;
    std::string body;
    std::string error;         // "" = OK HTML page. Else "<kind>: detail", kind = bad_url |
                               // http | redirects | not_html | offline | the HttpResponse kinds
    std::string fetched_at;    // UTC ISO-8601, set when stored in / loaded from the cache
    bool from_cache = false;
};

class PageCache;

class HttpsFetcher : public LinkSource {
public:
    using Transport = std::function<HttpResponse(const Url&)>;
    static constexpr int kMaxRedirects = 5;

    explicit HttpsFetcher(HttpOptions opt = {});  // real network (HttpClient)
    explicit HttpsFetcher(Transport t);           // tests: fake network

    // Cache: hit -> no network. offline -> a miss fails with "offline: not cached".
    // refresh -> always fetch live and overwrite. nullptr = no cache (default).
    void use_cache(const PageCache* cache, bool offline = false, bool refresh = false);

    FetchResult fetch(const std::string& url);
    std::optional<std::vector<std::string>> links(const std::string& url) override;
    const FetchResult& last() const { return last_; }  // result of the latest links()

private:
    FetchResult fetch_live(const std::string& url);

    std::shared_ptr<HttpClient> client_;
    Transport get_;
    FetchResult last_;
    const PageCache* cache_ = nullptr;
    bool offline_ = false, refresh_ = false;
};

// https:// -> tls. http:// -> josh first; if Josh fails (or is not wired yet,
// or the site 308s to https) -> tls. Josh's plug-in takes over http:// once it works.
class RoutedFetcher : public LinkSource {
public:
    RoutedFetcher(LinkSource& josh, LinkSource& tls) : josh_(josh), tls_(tls) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override;

private:
    LinkSource& josh_;
    LinkSource& tls_;
};

}  // namespace wc
