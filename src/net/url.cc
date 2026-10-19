#include "net/url.h"

#include <cctype>
#include <vector>

namespace wc {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Trim ends, drop tabs/newlines inside (browsers do the same), cut the #fragment.
std::string clean(const std::string& s) {
    std::string out;
    for (char c : s)
        if (c != '\t' && c != '\n' && c != '\r') out += c;
    size_t b = out.find_first_not_of(' '), e = out.find_last_not_of(' ');
    out = b == std::string::npos ? "" : out.substr(b, e - b + 1);
    return out.substr(0, out.find('#'));
}

// Raw spaces / control / non-ASCII bytes would break the request line.
std::string encode_unsafe(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (c <= 0x20 || c >= 0x7f) {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// RFC 3986 §5.2.4. `in` starts with '/'.
std::string remove_dots(const std::string& in) {
    std::vector<std::string> segs, out;
    size_t i = 1;
    while (true) {
        size_t j = in.find('/', i);
        segs.push_back(in.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    for (size_t k = 0; k < segs.size(); ++k) {
        bool last = k + 1 == segs.size();
        if (segs[k] == "." || segs[k] == "..") {
            if (segs[k] == ".." && !out.empty()) out.pop_back();
            if (last) out.push_back("");  // "a/.." -> "a/" keeps the slash
            continue;
        }
        out.push_back(segs[k]);
    }
    std::string path;
    for (const auto& s : out) path += "/" + s;
    return path.empty() ? "/" : path;
}

// Split "path?query" into the Url (path normalized, both encoded).
void set_path_query(Url& u, const std::string& pq) {
    size_t q = pq.find('?');
    std::string path = pq.substr(0, q);
    u.path = remove_dots(encode_unsafe(path.empty() ? "/" : path));
    u.query = q == std::string::npos ? "" : encode_unsafe(pq.substr(q + 1));
}

}  // namespace

std::string Url::target() const { return query.empty() ? path : path + "?" + query; }

std::string Url::host_port() const {
    bool dflt = (scheme == "http" && port == 80) || (scheme == "https" && port == 443);
    return dflt ? host : host + ":" + std::to_string(port);
}

std::string Url::str() const { return scheme + "://" + host_port() + target(); }

std::optional<Url> parse_url(const std::string& raw) {
    std::string s = clean(raw);
    size_t sep = s.find("://");
    if (sep == std::string::npos) return std::nullopt;
    Url u;
    u.scheme = lower(s.substr(0, sep));
    if (u.scheme == "http") u.port = 80;
    else if (u.scheme == "https") u.port = 443;
    else return std::nullopt;

    size_t start = sep + 3;
    size_t end = s.find_first_of("/?", start);
    std::string auth = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (size_t at = auth.rfind('@'); at != std::string::npos) auth = auth.substr(at + 1);  // user:pass@

    size_t colon = std::string::npos;
    if (!auth.empty() && auth[0] == '[') {  // [IPv6]:port
        size_t rb = auth.find(']');
        if (rb == std::string::npos) return std::nullopt;
        u.host = auth.substr(0, rb + 1);
        if (rb + 1 < auth.size()) {
            if (auth[rb + 1] != ':') return std::nullopt;
            colon = rb + 1;
        }
    } else {
        colon = auth.find(':');
        u.host = auth.substr(0, colon);
    }
    if (u.host.empty()) return std::nullopt;
    u.host = lower(u.host);
    if (colon != std::string::npos && colon + 1 < auth.size()) {
        std::string p = auth.substr(colon + 1);
        if (p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
        u.port = std::stoi(p);
        if (u.port < 1 || u.port > 65535) return std::nullopt;
    }
    set_path_query(u, end == std::string::npos ? "" : s.substr(end));
    return u;
}

std::string with_scheme(const std::string& s) {
    std::string t = clean(s);
    return t.find("://") == std::string::npos ? "https://" + t : t;
}

std::optional<Url> resolve_url(const Url& base, const std::string& raw) {
    std::string ref = clean(raw);
    if (ref.empty()) return base;

    // scheme:...  (scheme = ALPHA *(ALPHA / DIGIT / "+" / "-" / ".") before any / ? )
    size_t colon = ref.find(':');
    if (colon != std::string::npos && colon > 0 && std::isalpha(static_cast<unsigned char>(ref[0])) &&
        ref.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.") == colon)
        return parse_url(ref);  // nullopt for mailto:, javascript:, ftp:...

    if (ref.rfind("//", 0) == 0) return parse_url(base.scheme + ":" + ref);

    Url u = base;
    size_t q = ref.find('?');
    std::string path = ref.substr(0, q);
    if (path.empty()) {                       // "?x"
        set_path_query(u, base.path + ref);
    } else if (path[0] == '/') {              // "/x"
        set_path_query(u, ref);
    } else {                                  // "x", "../x"
        set_path_query(u, base.path.substr(0, base.path.rfind('/') + 1) + ref);
    }
    return u;
}

}  // namespace wc
