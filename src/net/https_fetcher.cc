#include "net/https_fetcher.h"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "net/cache.h"
#include "net/html_links.h"

namespace wc {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

std::string status_label(const FetchResult& r, long links) {
    if (r.error.empty())
        return links == 0 && lower(r.body).find("javascript") != std::string::npos ? "JS_SHELL" : "OK";
    std::string kind = r.error.substr(0, r.error.find(':'));
    if (kind == "http") return "HTTP_" + std::to_string(r.status);
    for (char& c : kind) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return kind;
}

std::string pick_start(const std::string& typed, const std::function<FetchResult(const std::string&)>& probe) {
    std::string https = with_scheme(typed);
    if (typed.find("://") != std::string::npos) return https;  // typed scheme: never probed
    std::string label = status_label(probe(https));
    if (label != "TLS" && label != "CONNECT" && label != "DNS" && label != "TIMEOUT" && label != "IO") return https;
    std::string bare = https.substr(std::string("https://").size());
    std::cerr << "[start] https failed (" << label << "), trying http://" << bare << '\n';
    return "http://" + bare;
}

HttpsFetcher::HttpsFetcher(HttpOptions opt)
    : client_(std::make_shared<HttpClient>(std::move(opt))),
      get_([c = client_](const Url& u) { return c->get(u); }) {}

HttpsFetcher::HttpsFetcher(Transport t) : get_(std::move(t)) {}

void HttpsFetcher::use_cache(const PageCache* cache, bool offline, bool refresh) {
    cache_ = cache;
    offline_ = offline;
    refresh_ = refresh;
}

std::string HttpsFetcher::label_of(const std::string& url) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = labels_.find(url);
    return it == labels_.end() ? "" : it->second;
}

FetchResult HttpsFetcher::last() const {
    std::lock_guard<std::mutex> lk(mu_);
    return last_;
}

void HttpsFetcher::slow_host(const std::string& host, int ms) {
    std::lock_guard<std::mutex> lk(mu_);
    int& d = host_delay_ms_[host];
    d = std::max(d, std::min(ms, kMaxHostDelayMs));
}

std::mutex& HttpsFetcher::gate(std::unordered_map<std::string, std::unique_ptr<std::mutex>>& gates, const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& g = gates[key];
    if (!g) g = std::make_unique<std::mutex>();
    return *g;  // never erased: the reference stays valid
}

FetchResult HttpsFetcher::fetch(const std::string& url, bool html) {
    FetchResult r = fetch_raw(url);
    if (html && r.error.empty()) {
        std::string ct = lower(r.content_type);
        if (!ct.empty() && ct.find("html") == std::string::npos) r.error = "not_html: " + r.content_type;
    }
    return r;
}

// robots -> cache -> offline -> network. Robots runs first so an offline replay
// labels pages the same way the live run did (robots.txt itself is cached too).
FetchResult HttpsFetcher::fetch_raw(const std::string& url) {
    if (robots_on_)
        if (auto u = parse_url(url))
            if (auto blocked = robots_block(url, *u)) return *blocked;
    if (cache_ && !refresh_)
        if (auto hit = cache_->load(url)) return *hit;
    if (offline_) {
        FetchResult r;
        r.url = url;
        r.error = "offline: not cached";
        return r;
    }
    FetchResult r = fetch_live(url);
    if (cache_) cache_->store(r);
    return r;
}

bool HttpsFetcher::handles_itself(const std::string& url) {
    if (offline_) return true;
    auto u = parse_url(url);
    if (!u) return false;
    if (robots_on_ && robots_block(url, *u)) return true;
    return cache_ && !refresh_ && cache_->load(url).has_value();
}

// nullopt = go ahead. Else the result to return instead of fetching.
std::optional<FetchResult> HttpsFetcher::robots_block(const std::string& url, const Url& u) {
    if (u.path == "/robots.txt") return std::nullopt;
    FetchResult r;
    r.url = url;
    r.final_url = u.str();
    std::string origin = u.scheme + "://" + u.host_port();
    std::lock_guard<std::mutex> once(gate(robots_gate_, origin));  // first fetch of it: the others wait
    std::unique_lock<std::mutex> lk(mu_);
    auto it = robots_.find(origin);
    if (it == robots_.end()) {
        lk.unlock();
        FetchResult rr = fetch(origin + "/robots.txt", /*html=*/false);
        if (rr.status == 0 && !starts_with(rr.error, "offline")) {
            r.error = rr.error;  // dns/connect/timeout: the page would fail the same way. Not memoized.
            return r;
        }
        RobotsRules rules;  // default: allow all (4xx, or not in the offline cache)
        if (rr.error.empty()) rules = parse_robots(rr.body);
        else if (!(rr.status >= 400 && rr.status < 500) && !starts_with(rr.error, "offline"))
            rules.block_all = true;  // 5xx, redirect loop, too large...: RFC 9309 says assume disallow
        if (rules.crawl_delay_ms > 0) slow_host(u.host_port(), rules.crawl_delay_ms);
        lk.lock();
        it = robots_.emplace(origin, std::move(rules)).first;
    }
    if (it->second.allowed(u.target())) return std::nullopt;
    r.error = it->second.block_all ? "robots: robots.txt unreachable" : "robots: disallowed by robots.txt";
    return r;
}

FetchResult HttpsFetcher::fetch_live(const std::string& url) {
    FetchResult r;
    r.url = url;
    auto u = parse_url(url);
    if (!u) {
        r.error = "bad_url: " + url;
        return r;
    }
    const bool robots_txt = u->path == "/robots.txt";  // its own redirects skip robots (no recursion)
    int retries = 0;
    bool waited = false;  // just slept a Retry-After: skip the host pacing once
    while (true) {
        r.final_url = u->str();
        HttpResponse resp;
        {
            std::optional<int> delay;  // paced host: one request at a time, `delay` ms before each
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (auto d = host_delay_ms_.find(u->host_port()); d != host_delay_ms_.end()) delay = d->second;
            }
            std::unique_lock<std::mutex> one;
            if (delay) one = std::unique_lock<std::mutex>(gate(host_gate_, u->host_port()));
            if (!waited && delay) sleeper_->sleep_ms(*delay);
            resp = get_(*u);
        }
        waited = false;
        r.status = resp.status;
        if (!resp.error.empty()) {
            r.error = resp.error;
            return r;
        }
        auto ra = resp.headers.find("retry-after");
        if ((r.status == 429 || (r.status == 503 && ra != resp.headers.end())) && retries < kMaxRetries) {
            int wait = 1000 << retries;  // 1 s, 2 s when the header is missing or an HTTP-date
            if (ra != resp.headers.end() && !ra->second.empty() && ra->second.size() <= 5 &&
                ra->second.find_first_not_of("0123456789") == std::string::npos)
                wait = std::stoi(ra->second) * 1000;
            wait = std::min(wait, kMaxWaitMs);
            slow_host(u->host_port(), wait);
            sleeper_->sleep_ms(wait);
            ++retries;
            waited = true;
            continue;
        }
        bool redirect = r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308;
        if (!redirect) {
            r.content_type = resp.headers.count("content-type") ? resp.headers["content-type"] : "";
            r.body = std::move(resp.body);
            break;
        }
        if (r.redirects == kMaxRedirects) {
            r.error = "redirects: more than " + std::to_string(kMaxRedirects);
            return r;
        }
        auto loc = resp.headers.find("location");
        auto next = loc == resp.headers.end() ? std::nullopt : resolve_url(*u, loc->second);
        if (!next) {
            r.error = "redirects: bad or missing Location";
            return r;
        }
        ++r.redirects;
        if (robots_on_ && !robots_txt)  // redirect target (maybe another host) must pass its own robots.txt
            if (auto blocked = robots_block(url, *next)) {
                r.final_url = blocked->final_url;
                r.error = blocked->error;
                return r;
            }
        u = next;
    }
    if (r.status < 200 || r.status > 299) r.error = "http: " + std::to_string(r.status);
    return r;
}

std::optional<std::vector<std::string>> HttpsFetcher::links(const std::string& url) {
    FetchResult r = fetch(url);
    std::optional<std::vector<std::string>> out;
    if (r.error.empty()) out = extract_links(r.body, *parse_url(r.final_url));
    std::string label = status_label(r, out ? static_cast<long>(out->size()) : -1);
    std::lock_guard<std::mutex> lk(mu_);
    labels_[url] = std::move(label);
    last_ = std::move(r);
    return out;
}

std::optional<std::vector<std::string>> RoutedFetcher::links(const std::string& url) {
    if (url.size() >= 8 && std::equal(url.begin(), url.begin() + 8, "https://",
                                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
        return tls_.links(url);
    if (auto* t = dynamic_cast<HttpsFetcher*>(&tls_); t && t->handles_itself(url)) return tls_.links(url);
    if (auto r = josh_.links(url)) return r;
    return tls_.links(url);
}

}  // namespace wc
