// report_pdf_test.cc - unit tests for src/ui/report_pdf (summary.pdf writer). No network.
// Run: build/wc_report_pdf_test [OUT.pdf [BRIDGES.jpg APP.jpg]]   (exit 1 if any check fails)
// OUT.pdf: also writes the rehearsal-sized sample for a render check (with the two JPEGs if given).
#include <fstream>
#include <iostream>
#include <iterator>
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

// Like the en.wikipedia.org BFS rehearsal.
static ReportSummary rehearsal() {
    ReportSummary r;
    r.host = "en.wikipedia.org", r.mode = "BFS", r.url = "https://en.wikipedia.org/wiki/Wars_of_the_Roses";
    r.depth = 3, r.size = 1000, r.nodes = 1000, r.edges = 49329, r.fetches = 1000, r.failed = 177;
    r.blocked = 175, r.robots = 122, r.bot_wall = 53, r.cache_misses = 1000, r.wall_ms = 562702;
    return r;
}

// SOI, APP0 (16 bytes), SOF0 32x16 RGB, EOI: enough for jpeg_info and the PDF (no viewer needs to decode it).
static std::string tiny_jpeg(int comps = 3) {
    std::string j = "\xFF\xD8\xFF\xE0";
    j += std::string("\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 16);
    j += std::string("\xFF\xC0\x00\x11\x08\x00\x10\x00\x20", 9) + char(comps);
    j += std::string(9, '\x01') + "\xFF\xD9";
    return j;
}

static std::string slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Structure: header, every xref offset points at "N 0 obj", startxref points at "xref", ends %%EOF,
// every stream's /Length matches its bytes.
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
    for (size_t ls = pdf.find("/Length "); ls != std::string::npos && ls < xref; ls = pdf.find("/Length ", ls + 1)) {
        size_t len = std::stoul(pdf.substr(ls + 8));
        size_t s = pdf.find("stream\n", ls) + 7;
        const std::string after = pdf.substr(s + len, 10);  // "endstream" or "\nendstream" (EOL before it is allowed)
        CHECK(after.rfind("endstream", 0) == 0 || after == "\nendstream");
    }
}

int main(int argc, char** argv) {
    CHECK_EQ(pdf_text("a(b)\\c"), std::string("a\\(b\\)\\\\c"));
    CHECK_EQ(pdf_text("caf\xc3\xa9\tx"), std::string("caf???x"));
    CHECK_EQ(clip("abcdef", 6), std::string("abcdef"));
    CHECK_EQ(clip("abcdefg", 6), std::string("abc..."));
    CHECK_EQ(wall_time(12345), std::string("12.3 s"));
    CHECK_EQ(wall_time(562702), std::string("9:22"));

    int w = 0, h = 0, c = 0;
    CHECK(jpeg_info(tiny_jpeg(), w, h, c));
    CHECK_EQ(w, 32);
    CHECK_EQ(h, 16);
    CHECK_EQ(c, 3);
    CHECK(!jpeg_info("\x89PNG\r\n", w, h, c));
    CHECK(!jpeg_info(tiny_jpeg(4), w, h, c));  // CMYK: not embedded
    CHECK(!jpeg_info("", w, h, c));

    ReportSummary r = rehearsal();
    std::string pdf = summary_pdf(r);  // no pictures
    check_structure(pdf);
    CHECK(pdf.find("(en.wikipedia.org)") != std::string::npos);
    CHECK(pdf.find("(BFS crawl, depth 3, size 1000)") != std::string::npos);
    CHECK(pdf.find("(no screenshot \\(scripts/report_shots.sh\\))") != std::string::npos);
    CHECK(pdf.find("/XObject") == std::string::npos);

    ReportSummary pics = r;
    pics.bridges_jpg = tiny_jpeg(), pics.app_jpg = tiny_jpeg(1);
    std::string pdf2 = summary_pdf(pics);
    check_structure(pdf2);
    CHECK(pdf2.find("/XObject << /Im1 8 0 R /Im2 9 0 R >>") != std::string::npos);
    CHECK(pdf2.find("/Width 32 /Height 16 /ColorSpace /DeviceRGB") != std::string::npos);
    CHECK(pdf2.find("/ColorSpace /DeviceGray") != std::string::npos);
    CHECK(pdf2.find("/Im2 Do") != std::string::npos);
    CHECK(pdf2.find("no screenshot") == std::string::npos);

    ReportSummary half = r;
    half.app_jpg = tiny_jpeg();  // only our app: one image, BRIDGES box says why
    std::string pdf3 = summary_pdf(half);
    check_structure(pdf3);
    CHECK(pdf3.find("/XObject << /Im1 8 0 R >>") != std::string::npos);
    CHECK(pdf3.find("no screenshot") != std::string::npos);

    check_structure(summary_pdf(ReportSummary{}));

    if (argc > 1) {
        ReportSummary out = r;
        if (argc > 3) out.bridges_jpg = slurp(argv[2]), out.app_jpg = slurp(argv[3]);
        std::ofstream(argv[1], std::ios::binary) << summary_pdf(out);
    }
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
