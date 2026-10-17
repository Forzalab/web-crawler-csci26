// omni_query.cc - see omni_query.h
#include "ui/omni_query.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace wc {

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

Query parse_query(const std::string& line) {
    Query q;
    std::istringstream in(lower(line));
    std::string tok;
    bool first = true;
    while (in >> tok) {
        if (first && tok[0] == '/') {
            q.verb = tok.substr(1);
        } else if (starts_with(tok, "depth:") && tok.size() > 6) {
            std::string v = tok.substr(6);
            char op = '=';
            if (v[0] == '<' || v[0] == '>' || v[0] == '=') op = v[0], v = v.substr(1);
            if (!v.empty() && std::all_of(v.begin(), v.end(), ::isdigit)) {
                q.depth_op = op;
                q.depth = std::stoi(v);
            } else {
                q.text += (q.text.empty() ? "" : " ") + tok;
            }
        } else if (tok == "status:fetched") {
            q.status = Status::Fetched;
        } else if (tok == "status:failed") {
            q.status = Status::Failed;
        } else if (tok == "status:unfetched") {
            q.status = Status::Unfetched;
        } else {
            q.text += (q.text.empty() ? "" : " ") + tok;
        }
        first = false;
    }
    return q;
}

static bool subsequence(const std::string& hay, const std::string& needle) {
    size_t j = 0;
    for (char c : hay)
        if (j < needle.size() && c == needle[j]) ++j;
    return j == needle.size();
}

std::vector<int> match(const CrawlGraph& g, const Query& q, size_t limit) {
    struct Hit { int rank, depth, id; };
    std::vector<Hit> hits;
    for (int id = 0; id < (int)g.nodes.size(); ++id) {
        const Node& n = g.nodes[id];
        if (q.status && n.status != *q.status) continue;
        if (q.depth_op == '=' && n.depth != q.depth) continue;
        if (q.depth_op == '<' && n.depth >= q.depth) continue;
        if (q.depth_op == '>' && n.depth <= q.depth) continue;
        int rank = 0;
        if (!q.text.empty()) {
            std::string url = lower(n.url);
            if (url.find(q.text) != std::string::npos) rank = 0;
            else if (subsequence(url, q.text)) rank = 1;
            else continue;
        }
        hits.push_back({rank, n.depth, id});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.depth != b.depth) return a.depth < b.depth;
        return a.id < b.id;
    });
    std::vector<int> out;
    for (size_t i = 0; i < hits.size() && i < limit; ++i) out.push_back(hits[i].id);
    return out;
}

std::string complete(const std::string& line) {
    static const char* verbs[] = {"/bridges", "/help", "/quit"};
    static const char* filters[] = {"depth:", "status:fetched", "status:failed", "status:unfetched"};
    if (line.empty() || std::isspace((unsigned char)line.back())) return "";
    size_t cut = line.find_last_of(' ');
    size_t start = cut == std::string::npos ? 0 : cut + 1;
    std::string tok = lower(line.substr(start));
    std::string head = line.substr(0, start);
    if (start == 0 && tok[0] == '/') {
        for (const char* v : verbs)
            if (starts_with(v, tok) && tok != v) return head + v;
        return "";
    }
    if (starts_with(tok, "status:") || starts_with("status:", tok)) {
        for (const char* f : filters)
            if (starts_with(f, tok) && tok != f && starts_with(f, "status:")) {
                // "stat" -> "status:" first; full value once the colon is typed
                if (tok.size() < 7) return head + "status:";
                return head + f;
            }
        return "";
    }
    if (starts_with("depth:", tok) && tok != "depth:") return head + "depth:";
    return "";
}

}  // namespace wc
