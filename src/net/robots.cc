#include "net/robots.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace wc {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r"), e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

// Does `pat` ('*' wildcards, optional trailing '$') match a prefix of `s`
// (all of `s` when anchored)? dp[j] = pattern so far matches s[0..j).
bool match(const std::string& p, const std::string& s) {
    bool anchored = !p.empty() && p.back() == '$';
    size_t n = anchored ? p.size() - 1 : p.size(), m = s.size();
    std::vector<char> dp(m + 1, 0), next(m + 1);
    dp[0] = 1;
    for (size_t i = 0; i < n; ++i) {
        std::fill(next.begin(), next.end(), 0);
        if (p[i] == '*') {
            char any = 0;
            for (size_t j = 0; j <= m; ++j) next[j] = any = any || dp[j];
        } else {
            for (size_t j = 0; j < m; ++j) next[j + 1] = dp[j] && s[j] == p[i];
        }
        dp.swap(next);
    }
    return anchored ? dp[m] : std::find(dp.begin(), dp.end(), 1) != dp.end();
}

}  // namespace

bool RobotsRules::allowed(const std::string& target) const {
    if (block_all) return false;
    long best = -1;
    bool allow = true;
    for (const auto& r : rules) {
        long len = static_cast<long>(r.pattern.size());
        if ((len > best || (len == best && r.allow)) && match(r.pattern, target)) {
            best = len;
            allow = r.allow;
        }
    }
    return allow;
}

RobotsRules parse_robots(const std::string& text) {
    struct Group {
        std::vector<std::string> agents;
        RobotsRules rules;
    };
    std::vector<Group> groups;
    bool in_agents = false;  // consecutive User-agent lines share one group
    size_t start = 0;
    while (start < text.size() && start < 500 * 1024) {  // RFC: parse at least 500 KiB
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        line = trim(line.substr(0, line.find('#')));
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, colon))), value = trim(line.substr(colon + 1));

        if (key == "user-agent") {
            if (!in_agents) groups.emplace_back();
            groups.back().agents.push_back(lower(value.substr(0, value.find('/'))));
            in_agents = true;
        } else if (!groups.empty() && (key == "allow" || key == "disallow")) {
            in_agents = false;
            if (!value.empty()) groups.back().rules.rules.push_back({value, key == "allow"});  // "Disallow:" = no rule
        } else if (!groups.empty() && key == "crawl-delay") {
            in_agents = false;
            groups.back().rules.crawl_delay_ms = static_cast<int>(std::atof(value.c_str()) * 1000);
        }
    }

    RobotsRules out;
    for (const char* want : {kRobotsToken, "*"}) {
        bool found = false;
        for (const auto& g : groups) {
            if (std::find(g.agents.begin(), g.agents.end(), want) == g.agents.end()) continue;
            found = true;
            out.rules.insert(out.rules.end(), g.rules.rules.begin(), g.rules.rules.end());
            out.crawl_delay_ms = std::max(out.crawl_delay_ms, g.rules.crawl_delay_ms);
        }
        if (found) break;  // our own group (even an empty one) wins over "*"
    }
    return out;
}

}  // namespace wc
