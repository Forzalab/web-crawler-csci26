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
                               // http | redirects | not_html | the HttpResponse kinds
};

class HttpsFetcher : public LinkSource {
public:
    using Transport = std::function<HttpResponse(const Url&)>;
    static constexpr int kMaxRedirects = 5;

    explicit HttpsFetcher(HttpOptions opt = {});  // real network (HttpClient)
    explicit HttpsFetcher(Transport t);           // tests: fake network

    FetchResult fetch(const std::string& url);
    std::optional<std::vector<std::string>> links(const std::string& url) override;
    const FetchResult& last() const { return last_; }  // result of the latest links()

private:
    std::shared_ptr<HttpClient> client_;
    Transport get_;
    FetchResult last_;
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
