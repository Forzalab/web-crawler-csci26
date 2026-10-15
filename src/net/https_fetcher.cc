#include "net/https_fetcher.h"

#include <algorithm>
#include <cctype>

#include "net/html_links.h"

namespace wc {

HttpsFetcher::HttpsFetcher(HttpOptions opt)
    : client_(std::make_shared<HttpClient>(std::move(opt))),
      get_([c = client_](const Url& u) { return c->get(u); }) {}

HttpsFetcher::HttpsFetcher(Transport t) : get_(std::move(t)) {}

FetchResult HttpsFetcher::fetch(const std::string& url) {
    FetchResult r;
    r.url = url;
    auto u = parse_url(url);
    if (!u) {
        r.error = "bad_url: " + url;
        return r;
    }
    while (true) {
        r.final_url = u->str();
        HttpResponse resp = get_(*u);
        r.status = resp.status;
        if (!resp.error.empty()) {
            r.error = resp.error;
            return r;
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
        u = next;
        ++r.redirects;
    }
    if (r.status < 200 || r.status > 299) {
        r.error = "http: " + std::to_string(r.status);
        return r;
    }
    std::string ct;
    for (char c : r.content_type) ct += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!ct.empty() && ct.find("html") == std::string::npos) r.error = "not_html: " + r.content_type;
    return r;
}

std::optional<std::vector<std::string>> HttpsFetcher::links(const std::string& url) {
    last_ = fetch(url);
    if (!last_.error.empty()) return std::nullopt;
    return extract_links(last_.body, *parse_url(last_.final_url));
}

std::optional<std::vector<std::string>> RoutedFetcher::links(const std::string& url) {
    if (url.size() >= 8 && std::equal(url.begin(), url.begin() + 8, "https://",
                                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }))
        return tls_.links(url);
    if (auto r = josh_.links(url)) return r;
    return tls_.links(url);
}

}  // namespace wc
