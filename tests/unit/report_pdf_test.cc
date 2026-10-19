// report_pdf_test.cc - unit tests for src/ui/report_pdf (summary.pdf writer). No network.
// Run: build/wc_report_pdf_test [OUT.pdf]   (exit 1 if any check fails)
// OUT.pdf: also writes the rehearsal-sized sample (1000 nodes, 177 failures) for a render check.
#include <fstream>
#include <iostream>
#include <string>

#include "ui/report_pdf.h"

using namespace wc;

static int failures = 0, checks = 0;

#define CHECK_EQ(got, want)                                                                         \
    do {                                                                                            \
        ++checks;                                                                                   \
        auto g_ = (got);                                                                            \
        auto w_ = (want);                                                                           \
        if (!(g_ == w_)) {                                                                          \
            ++failures;                                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #got "\n  got:  " << g_     \
                      << "\n  want: " << w_ << "\n";                                                \
        }                                                                                           \
    } while (0)
#define CHECK(c) CHECK_EQ(bool(c), true)

// Like the en.wikipedia.org BFS rehearsal: 1000 nodes, 177 failures, long URLs.
static ReportSummary rehearsal() {
    ReportSummary r;
    r.host = "en.wikipedia.org", r.mode = "BFS", r.url = "https://en.wikipedia.org/wiki/Wars_of_the_Roses";
    r.started_utc = "2026-10-19 01:59 UTC", r.started_pt = "2026-10-18 18:59 PDT", r.machine = "kerney-box";
    r.git_sha = "e19bbe7";
    r.depth = 3, r.size = 1000, r.nodes = 1000, r.edges = 49329, r.fetches = 1000, r.failed = 177;
    r.blocked = 175, r.robots = 122, r.bot_wall = 53, r.cache_misses = 1000, r.wall_ms = 562702;
    r.depth_hist = {1, 999, 0, 0};
    r.labels = {{"dead", 2}, {"robots", 122}, {"429", 0}, {"redirect-loop", 0},
                {"tls", 0},  {"timeout", 0},  {"bot-wall", 53}, {"other", 0}};
    for (int i = 0; i < 177; ++i)
        r.failures.push_back({"https://en.wikipedia.org/w/index.php?title=Special:WhatLinksHere/Wars_of_the_Roses_(" +
                                  std::to_string(i) + ")&limit=500&namespace=0&hidelinks=1&hidetrans=1",
                              i < 122 ? 0 : 403, i < 122 ? "robots" : "bot-wall"});
    return r;
}

// Structure: header, every xref offset points at "N 0 obj", startxref points at "xref", ends %%EOF.
static void check_structure(const std::string& pdf) {
    CHECK_EQ(pdf.substr(0, 9), std::string("%PDF-1.4\n"));
    CHECK_EQ(pdf.substr(pdf.size() - 6), std::string("%%EOF\n"));
    size_t sx = pdf.rfind("startxref\n");
    CHECK(sx != std::string::npos);
    size_t xref = std::stoul(pdf.substr(sx + 10));
    CHECK_EQ(pdf.substr(xref, 5), std::string("xref\n"));
    size_t p = pdf.find('\n', xref + 5);  // "0 N"
    int n = std::stoi(pdf.substr(xref + 7, p - xref - 7));
    CHECK(n >= 8);
    p += 1 + 20;  // skip the free entry; each entry is 20 bytes
    for (int i = 1; i < n; ++i, p += 20) {
        size_t off = std::stoul(pdf.substr(p, 10));
        CHECK_EQ(pdf.substr(off, std::to_string(i).size() + 6), std::to_string(i) + " 0 obj");
    }
    CHECK_EQ(pdf.substr(p, 7), std::string("trailer"));
    // /Length matches the stream bytes.
    size_t ls = pdf.find("/Length ");
    size_t len = std::stoul(pdf.substr(ls + 8));
    size_t s = pdf.find("stream\n", ls) + 7;
    CHECK_EQ(pdf.substr(s + len, 9), std::string("endstream"));
}

int main(int argc, char** argv) {
    CHECK_EQ(pdf_text("a(b)\\c"), std::string("a\\(b\\)\\\\c"));
    CHECK_EQ(pdf_text("caf\xc3\xa9\tx"), std::string("caf???x"));
    CHECK_EQ(clip("abcdef", 6), std::string("abcdef"));
    CHECK_EQ(clip("abcdefg", 6), std::string("abc..."));
    CHECK_EQ(wall_time(12345), std::string("12.3 s"));
    CHECK_EQ(wall_time(562702), std::string("9:22"));
    CHECK_EQ(pacific_time(1792375145), std::string("2026-10-18 18:59 PDT"));  // 2026-10-19T01:59:05Z
    CHECK_EQ(pacific_time(1794128400), std::string("2026-11-08 01:00 PST"));  // 2026-11-08T09:00Z (Nov 1st Sunday = 1st)
    CHECK_EQ(pacific_time(1772964000), std::string("2026-03-08 03:00 PDT"));  // 2026-03-08T10:00Z (2nd Sunday)

    ReportSummary r = rehearsal();
    std::string pdf = summary_pdf(r);
    check_structure(pdf);
    CHECK(pdf.find("(en.wikipedia.org - BFS crawl)") != std::string::npos);
    CHECK(pdf.find("Wars_of_the_Roses_\\(0\\)") != std::string::npos);  // escaped, then clipped
    CHECK(pdf.find("... 165 more in failures.tsv") != std::string::npos);
    CHECK(pdf.find("not uploaded") != std::string::npos);
    CHECK(pdf.find("/Annots") == std::string::npos);
    CHECK(pdf.find("1.00 0.00 0.00 rg") != std::string::npos);  // d0 bar red

    ReportSummary up = r;
    up.bridges_url = "https://bridges-cs.herokuapp.com/assignments/1234567/tony";
    std::string pdf2 = summary_pdf(up);
    check_structure(pdf2);
    CHECK(pdf2.find("/URI (https://bridges-cs.herokuapp.com/assignments/1234567/tony)") != std::string::npos);

    ReportSummary empty;
    check_structure(summary_pdf(empty));

    if (argc > 1) std::ofstream(argv[1], std::ios::binary) << pdf;
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
