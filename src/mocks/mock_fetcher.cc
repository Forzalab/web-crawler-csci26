#include "mocks/mock_fetcher.h"

#include <charconv>
#include <iostream>

namespace wc {

namespace {

const std::string kEof = "ERROR unexpected end of input";

// "Readable as int" = what `cin >> int` accepts (leading number).
bool to_int(const std::string& tok, int& v) {
    const char* b = tok.data();
    const char* e = b + tok.size();
    if (b != e && *b == '+') ++b;
    auto [p, ec] = std::from_chars(b, e, v);
    return ec == std::errc() && p != b;
}

}  // namespace

std::string parse_pipe_input(std::istream& in, PipeInput& out) {
    std::string tok;
    if (!(in >> tok)) return "ERROR empty input";
    if (tok == "BFS") out.cfg.mode = Mode::BFS;
    else if (tok == "DFS") out.cfg.mode = Mode::DFS;
    else return "ERROR bad mode " + tok;

    if (!(in >> out.start)) return kEof;

    if (!(in >> tok)) return kEof;
    if (!to_int(tok, out.cfg.maxDepth) || out.cfg.maxDepth < 0) return "ERROR bad maxDepth";
    if (!(in >> tok)) return kEof;
    if (!to_int(tok, out.cfg.maxSize) || out.cfg.maxSize < 1) return "ERROR bad maxSize";

    int pages = 0;
    if (!(in >> tok)) return kEof;
    if (!to_int(tok, pages) || pages < 0) return "ERROR bad page count";

    for (int i = 0; i < pages; ++i) {
        std::string url;
        if (!(in >> url)) return kEof;
        if (out.web.count(url)) return "ERROR duplicate page " + url;
        int k = 0;
        if (!(in >> tok)) return kEof;
        if (!to_int(tok, k) || k < -1) return "ERROR bad link count for " + url;
        if (k == -1) {
            out.web[url] = std::nullopt;
            continue;
        }
        std::vector<std::string> links(k);
        for (auto& link : links)
            if (!(in >> link)) return kEof;
        out.web[url] = std::move(links);
    }
    if (in >> tok) return "ERROR trailing input";
    out.cfg.delay_ms = 0;  // pipe mode never really sleeps; NoSleeper counts calls
    return "";
}

std::optional<std::vector<std::string>> MockFetcher::links(const std::string& url) {
    call_log.push_back(url);
    auto it = web_.find(url);
    bool listed = it != web_.end();
    bool ok = listed && it->second.has_value();
    if (!listed) ++misses;
    if (!ok) ++failures;
    if (verbose)
        std::cerr << "[mock] fetch " << url << (ok ? " OK" : listed ? " FAIL" : " FAIL(miss)") << '\n';
    if (!ok) return std::nullopt;
    return it->second;
}

}  // namespace wc
