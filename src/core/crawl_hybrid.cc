// crawl_hybrid.cc - see crawl_hybrid.h
#include "core/crawl_hybrid.h"

#include <algorithm>
#include <cctype>

namespace wc {

std::vector<std::string> trie_key(const std::string& url) {
    size_t p = url.find("://");
    p = p == std::string::npos ? 0 : p + 3;
    std::string rest = url.substr(p, url.find('#', p) - p);
    size_t q = rest.find('?');
    std::string query = q == std::string::npos ? "" : rest.substr(q);
    rest = rest.substr(0, q);
    size_t slash = rest.find('/');
    std::string host = rest.substr(0, slash), path = slash == std::string::npos ? "" : rest.substr(slash);
    for (char& c : host) c = char(std::tolower(static_cast<unsigned char>(c)));

    std::vector<std::string> key;
    auto split = [](const std::string& s, char sep, std::vector<std::string>& out) {
        for (size_t i = 0; i <= s.size();) {
            size_t j = std::min(s.find(sep, i), s.size());
            if (j > i) out.push_back(s.substr(i, j - i));
            i = j + 1;
        }
    };
    split(host, '.', key);
    std::reverse(key.begin(), key.end());
    split(path, '/', key);
    if (!query.empty()) key.push_back(query);
    return key;
}

std::vector<std::string> hybrid_order(std::vector<std::string> urls) {
    // Lexicographic compare of key vectors = pre-order DFS of the trie with sorted children.
    std::vector<std::pair<std::vector<std::string>, std::string>> keyed;
    keyed.reserve(urls.size());
    for (auto& u : urls) keyed.emplace_back(trie_key(u), std::move(u));
    std::sort(keyed.begin(), keyed.end());
    urls.clear();
    for (auto& k : keyed) urls.push_back(std::move(k.second));
    return urls;
}

CrawlGraph crawl_hybrid(const std::string& start, const CrawlConfig& cfg, LinkSource& src, Sleeper& sleeper) {
    CrawlGraph g;
    g.mode = Mode::Hybrid;
    g.add_node(start, 0);
    std::vector<std::string> layer{start};
    bool first = true;
    for (int depth = 0; !layer.empty() && depth < cfg.maxDepth; ++depth) {  // D4: depth == maxDepth stays unfetched
        std::vector<std::string> next;
        for (const std::string& url : hybrid_order(std::move(layer))) {
            if (cfg.cancel && *cfg.cancel) return g;
            if (!first) sleeper.sleep_ms(cfg.delay_ms);  // D8: never before the root
            first = false;
            auto links = src.links(url);
            g.record_fetch(url, links.has_value());
            if (!links) continue;
            for (const std::string& child : *links) {
                bool seen = g.has(child);
                if (!seen && int(g.nodes.size()) >= cfg.maxSize) continue;  // D5, crawl_bfs rule
                if (!g.has_edge(url, child)) g.add_edge(url, child, seen);
                if (seen) continue;
                g.add_node(child, depth + 1);  // D6: first layer to reach it = fewest hops
                next.push_back(child);
            }
        }
        layer = std::move(next);
    }
    return g;
}

}  // namespace wc
