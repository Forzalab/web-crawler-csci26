// url.h - parse http(s) URLs and resolve relative links (RFC 3986 §5.2).
// Pure string code: no I/O.
#pragma once

#include <optional>
#include <string>

namespace wc {

struct Url {
    std::string scheme;  // "http" or "https" (lowercase)
    std::string host;    // lowercase; IPv6 keeps its [brackets]
    int port = 0;        // always set (80 / 443 when the URL has none)
    std::string path;    // starts with "/"
    std::string query;   // without the '?', may be empty

    std::string target() const;     // path[?query]  (the HTTP request line)
    std::string host_port() const;  // host[:port]   (port only if not default)
    std::string str() const;        // normalized, no fragment
};

// Absolute http/https URL only. Fragment dropped. nullopt = not one.
std::optional<Url> parse_url(const std::string& s);

// `ref` (href, Location header...) against `base`. Fragment dropped.
// nullopt = empty after trimming, not http/https (mailto:, javascript:...), or bad.
std::optional<Url> resolve_url(const Url& base, const std::string& ref);

}  // namespace wc
