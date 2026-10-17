// omni_query.h - the omnibar grammar. Pure.
//   wiki                 fuzzy match on url
//   depth:2 depth:<3     depth filter (= < >)
//   status:failed        fetched | failed | unfetched
//   wiki status:failed   mix freely
//   /bridges /help /quit verbs (first token starts with '/')
#pragma once

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

}  // namespace wc
