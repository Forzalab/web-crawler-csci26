// https_fetcher.h - our own fetch path (TLS owner = Claude, decision 19:04).
// Fetches http:// and https://, follows redirects (301/302/303/307/308, max 5),
// keeps only 2xx HTML, returns its <a href> links. Implements LinkSource.
// Polite by default: robots.txt (RFC 9309, per origin, once per run; checked
// again on every redirect hop), 429 /
// 503 Retry-After retries, per-host pacing after a 429 or a Crawl-delay.
// Thread safe (the TUI crawls with many threads): a paced host gets one request at a
// time, unpaced hosts run fully parallel; robots.txt is fetched once per origin.
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/link_source.h"
#include "core/sleeper.h"
#include "net/http.h"
#include "net/robots.h"
#include "net/url.h"

namespace wc {

struct FetchResult {
    std::string url;           // as asked
    std::string final_url;     // after redirects ("" if the url did not parse)
    int status = 0;            // last HTTP status, 0 = none
    int redirects = 0;
    std::string content_type;
    std::string body;
    std::string error;         // "" = OK page. Else "<kind>: detail", kind = bad_url | http |
                               // redirects | not_html | offline | robots | the HttpResponse kinds
    std::string fetched_at;    // UTC ISO-8601, set when stored in / loaded from the cache
    bool from_cache = false;
};

// Short status for TUI / report / BRIDGES: OK, ROBOTS, HTTP_404, TLS, TIMEOUT, DNS,
// CONNECT, IO, TOO_LARGE, BAD_RESPONSE, REDIRECTS, NOT_HTML, BAD_URL, OFFLINE, or
// JS_SHELL (OK page with `links` == 0 that mentions JavaScript: probably needs a browser).
std::string status_label(const FetchResult& r, long links = -1);

// The start URL a person typed. A scheme is kept as typed. A bare domain tries https://
// through `probe`; no HTTP response at all (TLS, CONNECT, DNS, TIMEOUT, IO, ...) switches
// to http:// (one stderr line). An HTTP status is an answer: stays https.
std::string pick_start(const std::string& typed, const std::function<FetchResult(const std::string&)>& probe);

class PageCache;

class HttpsFetcher : public LinkSource {
public:
    using Transport = std::function<HttpResponse(const Url&)>;
    static constexpr int kMaxRedirects = 5;
    static constexpr int kMaxRetries = 2;         // per request, on 429 or 503 + Retry-After
    static constexpr int kMaxWaitMs = 30000;      // one Retry-After wait, capped
    static constexpr int kMaxHostDelayMs = 5000;  // per-host pacing (after a 429 / Crawl-delay), capped

    explicit HttpsFetcher(HttpOptions opt = {});  // real network (HttpClient)
    explicit HttpsFetcher(Transport t);           // tests: fake network
    HttpsFetcher(const HttpsFetcher&) = delete;
    HttpsFetcher& operator=(const HttpsFetcher&) = delete;

    // Cache: hit -> no network. offline -> a miss fails with "offline: not cached".
    // refresh -> always fetch live and overwrite. nullptr = no cache (default).
    void use_cache(const PageCache* cache, bool offline = false, bool refresh = false);
    void use_robots(bool on) { robots_on_ = on; }  // default on
    void use_sleeper(Sleeper* s) { sleeper_ = s; }  // default RealSleeper (tests: fake)

    // html = false: any 2xx content type is OK (robots.txt).
    FetchResult fetch(const std::string& url, bool html = true);
    std::optional<std::vector<std::string>> links(const std::string& url) override;
    FetchResult last() const;                            // result of the latest links()
    std::string label_of(const std::string& url) const;  // status_label of links(url); "" = never asked
    // true = answer this url here, not via Josh: robots.txt blocks it,
    // it is cached (so replays match) or we are offline. RoutedFetcher asks first.
    bool handles_itself(const std::string& url);

private:
    FetchResult fetch_raw(const std::string& url);
    FetchResult fetch_live(const std::string& url);
    std::optional<FetchResult> robots_block(const std::string& url, const Url& u);
    void slow_host(const std::string& host, int ms);
    std::mutex& gate(std::unordered_map<std::string, std::unique_ptr<std::mutex>>& gates, const std::string& key);

    std::shared_ptr<HttpClient> client_;
    Transport get_;
    FetchResult last_;
    const PageCache* cache_ = nullptr;
    bool offline_ = false, refresh_ = false, robots_on_ = true;
    RealSleeper real_sleeper_;
    Sleeper* sleeper_ = &real_sleeper_;
    // Lock order: robots gate -> host gate -> mu_. mu_ is never held over I/O or a sleep.
    mutable std::mutex mu_;  // guards the maps below and last_
    std::unordered_map<std::string, RobotsRules> robots_;  // origin -> rules, this run
    std::unordered_map<std::string, int> host_delay_ms_;  // host -> sleep before each request
    std::unordered_map<std::string, std::string> labels_;  // asked url -> status_label
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> robots_gate_;  // origin: one robots.txt fetch
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> host_gate_;    // paced host: one request at a time
};

// https:// -> tls. http:// -> josh first; if Josh fails (or the site 308s to https) -> tls.
// Robots, cache and offline still apply: an HttpsFetcher tls that handles_itself(url) answers.
class RoutedFetcher : public LinkSource {
public:
    RoutedFetcher(LinkSource& josh, LinkSource& tls) : josh_(josh), tls_(tls) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override;

private:
    LinkSource& josh_;
    LinkSource& tls_;
};

}  // namespace wc
