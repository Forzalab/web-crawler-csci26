// report_pdf.h - DIR/summary.pdf for `wc --report`: one US Letter page, no dependencies.
// Site, stats, then two pictures of the graph: BRIDGES and our app (JPEGs from scripts/report_shots.sh).
// Hand-written PDF 1.4: base-14 fonts (Helvetica, Helvetica-Bold, Courier), text, lines, JPEG images
// (DCTDecode: the JPEG bytes go in as they are). Pure: struct in, PDF bytes out (tests/unit/report_pdf_test.cc).
#pragma once

#include <string>

namespace wc {

struct ReportSummary {
    std::string host, mode, url;  // mode "BFS" | "DFS"
    int depth = 0, size = 0, nodes = 0, edges = 0, fetches = 0, failed = 0;
    int blocked = 0, robots = 0, too_many = 0, bot_wall = 0, cache_hits = 0, cache_misses = 0;
    long wall_ms = 0;
    std::string wall_note;            // "(live run, ...)" | "(cache replay, not live)" | "" (live run)
    std::string bridges_jpg, app_jpg;  // JPEG file bytes; "" = no picture
    std::string bridges_caption = "BRIDGES", app_caption = "Our app";  // box titles (DIR/<box>_caption.txt)
};

std::string summary_pdf(const ReportSummary& r);

// Helpers (exposed for the unit test).
std::string pdf_text(const std::string& s);          // PDF string body: escape ( ) \, non-ASCII -> '?'
std::string clip(const std::string& s, size_t max);  // > max chars -> first max-3 + "..."
std::string wall_time(long ms);                      // "12.3 s" under a minute, else "m:ss"
// JPEG pixel size + colour components from its SOF marker; false if not a baseline/progressive JPEG.
bool jpeg_info(const std::string& jpg, int& w, int& h, int& comps);

}  // namespace wc
