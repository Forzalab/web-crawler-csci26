// robots.h - robots.txt rules (RFC 9309). Pure string code: no I/O.
#pragma once

#include <string>
#include <vector>

namespace wc {

// Product token we match in "User-agent:" lines (case-insensitive). Same name as kUserAgent.
inline constexpr const char* kRobotsToken = "csci26-webcrawler";

struct RobotsRules {
    struct Rule {
        std::string pattern;  // path prefix; '*' = any run of chars, trailing '$' = end of URL
        bool allow = false;
    };
    std::vector<Rule> rules;
    int crawl_delay_ms = 0;   // "Crawl-delay:" (not in the RFC, honoured anyway)
    bool block_all = false;   // robots.txt unreachable (5xx...) -> RFC: assume complete disallow

    // `target` = path[?query]. Longest matching pattern wins; on a tie Allow wins.
    // No matching rule = allowed.
    bool allowed(const std::string& target) const;
};

// Our group if one names kRobotsToken, else the "*" group(s), else allow all.
RobotsRules parse_robots(const std::string& text);

}  // namespace wc
