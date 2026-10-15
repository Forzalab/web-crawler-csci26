// links_main.cc - `wc --links URL`: one real fetch, links on stdout, status on
// stderr. Smoke test for the fetch path (Josh adapter + our TLS fetcher).
#include <cstring>
#include <iostream>

#include "adapters/josh_fetcher.h"
#include "net/https_fetcher.h"
#include "ui/front_ends.h"

namespace wc {

int links_main(int argc, char** argv) {
    const char* url = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--links")) url = argv[i + 1];
    if (!url) {
        std::cerr << "usage: wc --links URL\n";
        return 2;
    }
    JoshFetcher josh;
    HttpsFetcher tls;
    RoutedFetcher src(josh, tls);
    auto links = src.links(url);

    const FetchResult& r = tls.last();
    if (r.url == url)
        std::cerr << "[fetch] via=tls status=" << r.status << " redirects=" << r.redirects << " final=" << r.final_url
                  << " type=" << r.content_type << " bytes=" << r.body.size()
                  << (r.error.empty() ? "" : " error=" + r.error) << '\n';
    else
        std::cerr << "[fetch] via=josh\n";
    if (!links) return 1;
    for (const auto& l : *links) std::cout << l << '\n';
    std::cerr << "[fetch] links=" << links->size() << '\n';
    return 0;
}

}  // namespace wc
