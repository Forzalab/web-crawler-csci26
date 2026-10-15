// cache.h - disk cache of fetch results, keyed by (source, url). Reruns and
// --offline replay read it instead of the network, so report crawls are
// reproducible and stay off live sites. No expiry: delete the dir (or use
// --refresh) to fetch again.
//
// Layout: <dir>/<source>/<16 hex FNV-1a of url>.page, one file per url:
//   WC-CACHE 1
//   url: <asked url>
//   final_url: ...
//   status: 200
//   redirects: 1
//   content_type: text/html; charset=utf-8
//   error: <"" or label>
//   fetched_at: 2026-10-15T05:00:00Z
//   bytes: <N>
//   <blank line, then exactly N body bytes>
// Body is kept only for OK HTML / text/plain (robots.txt) pages. Transport failures (status 0) are not stored.
#pragma once

#include <optional>
#include <string>

#include "net/https_fetcher.h"

namespace wc {

class PageCache {
public:
    PageCache(std::string dir, std::string source = "tls");

    std::optional<FetchResult> load(const std::string& url) const;  // nullopt = miss / unreadable
    void store(FetchResult& r) const;                               // sets fetched_at; no-op for status 0
    std::string path_for(const std::string& url) const;

private:
    std::string dir_;  // <dir>/<source>
};

}  // namespace wc
