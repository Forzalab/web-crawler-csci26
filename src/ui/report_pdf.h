// report_pdf.h - DIR/summary.pdf for `wc --report`: one US Letter page, no dependencies.
// Hand-written PDF 1.4: base-14 fonts (Helvetica, Helvetica-Bold, Courier), text, filled
// rects, lines, one URI link. Pure: struct in, PDF bytes out (unit test: tests/unit/report_pdf_test.cc).
#pragma once

#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace wc {

struct ReportSummary {
    std::string host, mode, url;           // mode "BFS" | "DFS"
    std::string started_utc, started_pt;   // e.g. "2026-10-19 01:59 UTC", "2026-10-18 18:59 PDT"
    std::string machine, bridges_url, git_sha;  // empty = unknown / not uploaded
    int depth = 0, size = 0, nodes = 0, edges = 0, fetches = 0, failed = 0;
    int blocked = 0, robots = 0, too_many = 0, bot_wall = 0, cache_hits = 0, cache_misses = 0;
    long wall_ms = 0;
    std::vector<int> depth_hist;                       // [d] = nodes at depth d, d0..maxDepth
    std::vector<std::pair<std::string, int>> labels;   // failure label -> count, print order
    struct Row { std::string url; int status = 0; std::string label; };
    std::vector<Row> failures;                         // failures.tsv rows, fetch order (first 12 shown)
};

std::string summary_pdf(const ReportSummary& r);

// Helpers (exposed for the unit test).
std::string pdf_text(const std::string& s);              // PDF string body: escape ( ) \, non-ASCII -> '?'
std::string clip(const std::string& s, size_t max);      // > max chars -> first max-3 + "..."
std::string wall_time(long ms);                          // "12.3 s" under a minute, else "m:ss"
std::string pacific_time(std::time_t t);                 // "YYYY-MM-DD HH:MM PDT|PST" (US DST rule)

}  // namespace wc
