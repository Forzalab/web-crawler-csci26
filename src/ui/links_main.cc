// links_main.cc - `wc --links URL`: one real fetch, links on stdout, status on
// stderr. Smoke test for the fetch path (Josh adapter + our TLS fetcher).
// Cache flags: --cache DIR (default .wc-cache), --no-cache, --offline, --refresh.
#include <cstring>
#include <iostream>

#include "adapters/josh_fetcher.h"
#include "net/cache.h"
#include "net/https_fetcher.h"
#include "ui/front_ends.h"

namespace wc {

int links_main(int argc, char** argv) {
    const char* url = nullptr;
    std::string cache_dir = ".wc-cache";
    bool use_cache = true, offline = false, refresh = false;
    for (int i = 1; i < argc; ++i) {
        bool has_arg = i + 1 < argc;
        if (!std::strcmp(argv[i], "--links") && has_arg) url = argv[++i];
        else if (!std::strcmp(argv[i], "--cache") && has_arg) cache_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--no-cache")) use_cache = false;
        else if (!std::strcmp(argv[i], "--offline")) offline = true;
        else if (!std::strcmp(argv[i], "--refresh")) refresh = true;
    }
    if (!url || (offline && !use_cache)) {
        std::cerr << "usage: wc --links URL [--cache DIR | --no-cache] [--offline] [--refresh]\n";
        return 2;
    }
    JoshFetcher josh;
    HttpsFetcher tls;
    PageCache cache(cache_dir);
    if (use_cache) tls.use_cache(&cache, offline, refresh);
    RoutedFetcher routed(josh, tls);
    LinkSource& src = offline ? static_cast<LinkSource&>(tls) : routed;  // offline: never touch Josh's network
    auto links = src.links(url);

    const FetchResult& r = tls.last();
    if (r.url == url)
        std::cerr << "[fetch] via=tls status=" << r.status << " redirects=" << r.redirects << " final=" << r.final_url
                  << " type=" << r.content_type << " bytes=" << r.body.size()
                  << " cache=" << (!use_cache ? "off" : r.from_cache ? "hit" : "miss")
                  << (r.error.empty() ? "" : " error=" + r.error) << '\n';
    else
        std::cerr << "[fetch] via=josh\n";
    if (!links) return 1;
    for (const auto& l : *links) std::cout << l << '\n';
    std::cerr << "[fetch] links=" << links->size() << '\n';
    return 0;
}

}  // namespace wc
