// omni_query.h - the omnibar grammar. Pure.
//   wiki                 fuzzy match on url
//   depth:2 depth:<3     depth filter (= < >)
//   status:failed        fetched | failed | unfetched
//   wiki status:failed   mix freely
//   /bridges /resume /help /quit   verbs (first token starts with '/')
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/graph.h"

namespace wc {

struct Query {
    std::string verb;             // "bridges" for "/bridges", else empty
    std::string text;             // leftover words, lowercased, space-joined
    char depth_op = 0;            // 0 none, '=', '<', '>'
    int depth = 0;
    std::optional<Status> status;
};

Query parse_query(const std::string& line);

// Node ids matching q, best first (substring beats subsequence, then shallower).
std::vector<int> match(const CrawlGraph& g, const Query& q, size_t limit = 8);

// Ghost-text completion for the current line ("/br" -> "/bridges", "stat" -> "status:"),
// empty if none.
std::string complete(const std::string& line);

// Where a BRIDGES graph is viewed: <server>/assignments/<N>/<user>. Null/empty
// assignment or user -> the literal "<N>" / "<user>". Never touches API keys.
std::string bridges_view_url(const char* assignment, const char* user);

// The BRIDGES assignment for one TUI session: env (BRIDGES_ASSIGNMENT) when set, else a random
// 7-digit id, uniform in [kAssignLo, kAssignHi], drawn from a generator seeded with seed (the TUI
// passes std::random_device). The TUI picks it once and reuses it for every upload.
inline constexpr int kAssignLo = 1000000, kAssignHi = 9999999;
std::string session_assignment(const char* env, std::uint32_t seed);

// A real page from the graph to show as the omnibar example: a depth-1 node's
// short label, else the start page, else "google.com" (empty graph).
std::string example_label(const CrawlGraph& g);

}  // namespace wc
