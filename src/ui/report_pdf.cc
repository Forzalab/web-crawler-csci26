#include "ui/report_pdf.h"

#include <algorithm>
#include <cstdio>

#include "bridges/bridges_json.h"

namespace wc {

namespace {

// US Letter in points; text block 50..562.
constexpr double kW = 612, kH = 792, kL = 50, kR = 562;
enum Font { kSans, kBold, kMono };
const char* const kFontName[] = {"Helvetica", "Helvetica-Bold", "Courier"};

std::string num(double x) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", x);
    return buf;
}

// Width of a digits-only string ('d' too): every Helvetica digit is 556/1000 em.
double digits_width(const std::string& s, double size) { return double(s.size()) * 0.556 * size; }

// One page content stream.
class Page {
public:
    void text(double x, double y, Font f, double size, const std::string& s) {
        out_ += "BT /F" + std::to_string(f + 1) + ' ' + num(size) + " Tf " + num(x) + ' ' + num(y) + " Td (" +
                pdf_text(s) + ") Tj ET\n";
    }
    void gray_text(double x, double y, Font f, double size, const std::string& s, double gray) {
        out_ += num(gray) + " g\n";
        text(x, y, f, size, s);
        out_ += "0 g\n";
    }
    // Filled rect with a thin grey outline (yellow stays visible on white paper).
    void bar(double x, double y, double w, double h, const std::array<int, 3>& rgb) {
        out_ += "q " + num(rgb[0] / 255.0) + ' ' + num(rgb[1] / 255.0) + ' ' + num(rgb[2] / 255.0) + " rg 0.4 G 0.5 w " +
                num(x) + ' ' + num(y) + ' ' + num(w) + ' ' + num(h) + " re B Q\n";
    }
    void line(double x1, double y1, double x2, double y2, double gray = 0.6) {
        out_ += "q " + num(gray) + " G 0.75 w " + num(x1) + ' ' + num(y1) + " m " + num(x2) + ' ' + num(y2) + " l S Q\n";
    }
    const std::string& str() const { return out_; }

private:
    std::string out_;
};

}  // namespace

std::string pdf_text(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '(' || c == ')' || c == '\\') out += '\\', out += char(c);
        else if (c < 0x20 || c > 0x7e) out += '?';
        else out += char(c);
    }
    return out;
}

std::string clip(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    return s.substr(0, max < 3 ? 0 : max - 3) + "...";
}

std::string wall_time(long ms) {
    char buf[32];
    if (ms < 60000) std::snprintf(buf, sizeof buf, "%.1f s", ms / 1000.0);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld", ms / 60000, ms / 1000 % 60);
    return buf;
}

std::string pacific_time(std::time_t t) {
    std::tm u{};
    gmtime_r(&t, &u);
    // DST: 2nd Sunday of March 02:00 PST (10:00 UTC) .. 1st Sunday of November 02:00 PDT (09:00 UTC).
    auto sunday_utc = [&](int mon, int nth, int hour) {
        std::tm d{};
        d.tm_year = u.tm_year, d.tm_mon = mon, d.tm_mday = 1;
        std::time_t first = timegm(&d);
        gmtime_r(&first, &d);
        d.tm_mday = 1 + (7 - d.tm_wday) % 7 + 7 * (nth - 1);
        d.tm_hour = hour;
        return timegm(&d);
    };
    const bool dst = t >= sunday_utc(2, 2, 10) && t < sunday_utc(10, 1, 9);
    std::time_t local = t - (dst ? 7 : 8) * 3600;
    std::tm p{};
    gmtime_r(&local, &p);
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &p);
    return std::string(buf) + (dst ? " PDT" : " PST");
}

std::string summary_pdf(const ReportSummary& r) {
    Page pg;
    double y = 742;

    // 1. Title, start URL, when / where / settings.
    pg.text(kL, y, kBold, 18, clip(r.host, 40) + " - " + r.mode + " crawl");
    y -= 18;
    pg.text(kL, y, kMono, 9, "Start: " + clip(r.url, 87));
    y -= 14;
    pg.text(kL, y, kSans, 10, "Started " + r.started_utc + "  (" + r.started_pt + ")    Machine: " + clip(r.machine, 30));
    y -= 14;
    pg.text(kL, y, kSans, 10,
            "Settings: depth " + std::to_string(r.depth) + ", size " + std::to_string(r.size) + ", 0.1 s delay between requests");
    y -= 10;
    pg.line(kL, y, kR, y);

    // 2. Stat block: two columns, then the blocked breakdown on its own row.
    auto stat = [&](double x, const std::string& k, const std::string& v) {
        pg.gray_text(x, y, kSans, 11, k, 0.35);
        pg.text(x + 80, y, kBold, 11, v);
    };
    const double c2 = 316;
    y -= 20;
    stat(kL, "Nodes", std::to_string(r.nodes));
    stat(c2, "Edges", std::to_string(r.edges));
    y -= 17;
    stat(kL, "Fetches", std::to_string(r.fetches));
    stat(c2, "Failed", std::to_string(r.failed));
    y -= 17;
    stat(kL, "Cache", std::to_string(r.cache_hits) + " hits / " + std::to_string(r.cache_misses) + " misses");
    y -= 17;
    stat(kL, "Wall time", wall_time(r.wall_ms) + (r.wall_note.empty() ? "" : "   " + r.wall_note));
    y -= 17;
    stat(kL, "Blocked", std::to_string(r.blocked) + "   (robots.txt " + std::to_string(r.robots) + ", HTTP 429 " +
                            std::to_string(r.too_many) + ", bot wall " + std::to_string(r.bot_wall) + ")");
    y -= 12;
    pg.line(kL, y, kR, y);

    // 3. Depth histogram, one bar per depth (0 bars included), BRIDGES ROYGBIV colours.
    y -= 20;
    pg.text(kL, y, kBold, 12, "Nodes per depth");
    const double base = y - 150, top = y - 22;  // bar area; count labels sit above each bar
    const int n = std::max<int>(1, int(r.depth_hist.size()));
    const int most = std::max(1, r.depth_hist.empty() ? 0 : *std::max_element(r.depth_hist.begin(), r.depth_hist.end()));
    const double slot = (kR - kL) / n, bw = std::min(slot * 0.7, 90.0);
    for (int d = 0; d < n; ++d) {
        const int c = d < int(r.depth_hist.size()) ? r.depth_hist[size_t(d)] : 0;
        const double cx = kL + slot * (d + 0.5);
        const double h = c ? std::max(3.0, (top - base - 12) * c / most) : 0;  // 1 of 999 still shows
        if (c) pg.bar(cx - bw / 2, base, bw, h, hop_rgb(d));
        const std::string cs = std::to_string(c), ds = "d" + std::to_string(d);
        const double fs = std::min(9.0, slot * 0.9 / (0.556 * double(std::max(cs.size(), ds.size()))));
        if (fs >= 4) {
            pg.text(cx - digits_width(cs, fs) / 2, base + h + 3, kSans, fs, cs);
            pg.text(cx - digits_width(ds, fs) / 2, base - 11, kSans, fs, ds);
        }
    }
    pg.line(kL, base, kR, base, 0.3);
    y = base - 26;
    pg.line(kL, y, kR, y);

    // 4. Failure labels, then the first rows of failures.tsv.
    y -= 20;
    pg.text(kL, y, kBold, 12, "Failures and blocks (" + std::to_string(r.failures.size()) + " rows in failures.tsv)");
    for (size_t i = 0; i < r.labels.size(); ++i) {
        if (i % 4 == 0) y -= 16;
        const double x = kL + double(i % 4) * 128;
        pg.gray_text(x, y, kSans, 10, r.labels[i].first, 0.35);
        pg.text(x + 72, y, kBold, 10, std::to_string(r.labels[i].second));
    }
    y -= 20;
    constexpr size_t kRows = 12;
    if (r.failures.empty()) {
        pg.text(kL, y, kSans, 10, "No failures.");
    } else {
        pg.gray_text(kL, y, kBold, 8, "status", 0.35);
        pg.gray_text(kL + 34, y, kBold, 8, "label", 0.35);
        pg.gray_text(kL + 100, y, kBold, 8, "url", 0.35);
        for (size_t i = 0; i < std::min(kRows, r.failures.size()); ++i) {
            const auto& f = r.failures[i];
            y -= 10;
            pg.text(kL, y, kMono, 7.5, std::to_string(f.status));
            pg.text(kL + 34, y, kMono, 7.5, clip(f.label, 13));
            pg.text(kL + 100, y, kMono, 7.5, clip(f.url, 91));
        }
        if (r.failures.size() > kRows) {
            y -= 12;
            pg.gray_text(kL, y, kSans, 8, "... " + std::to_string(r.failures.size() - kRows) + " more in failures.tsv", 0.35);
        }
    }

    // 5. BRIDGES link (clickable) or how to get one.
    y -= 26;
    std::string annot;
    if (!r.bridges_url.empty()) {
        pg.text(kL, y, kBold, 11, "BRIDGES:");
        const std::string shown = clip(r.bridges_url, 80);
        pg.text(kL + 56, y, kMono, 9, shown);
        annot = "<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [" + num(kL + 56) + ' ' + num(y - 3) + ' ' +
                num(kL + 56 + 5.4 * double(shown.size())) + ' ' + num(y + 9) + "] /A << /S /URI /URI (" +
                pdf_text(r.bridges_url) + ") >> >>";
    } else {
        pg.text(kL, y, kSans, 10, "BRIDGES: not uploaded (rerun with --upload; the cache replays it)");
    }

    // 6. Footer.
    pg.line(kL, 62, kR, 62);
    pg.gray_text(kL, 50, kSans, 8,
                 "Polite crawl: robots.txt, honest User-Agent, 0.1 s between requests. Bot walls reported, not evaded.", 0.35);
    if (!r.git_sha.empty()) {
        const std::string b = "build " + clip(r.git_sha, 10);
        pg.gray_text(kR - 4.8 * double(b.size()), 50, kMono, 8, b, 0.35);  // Courier: 0.6 em per char
    }

    // Objects: 1 catalog, 2 pages, 3 page, 4 contents, 5-7 fonts, 8 link (optional).
    std::vector<std::string> obj = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(kW) + ' ' + num(kH) +
            "] /Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R >> >> /Contents 4 0 R" +
            (annot.empty() ? "" : " /Annots [8 0 R]") + " >>",
        "<< /Length " + std::to_string(pg.str().size()) + " >>\nstream\n" + pg.str() + "endstream",
    };
    for (const char* f : kFontName)
        obj.push_back(std::string("<< /Type /Font /Subtype /Type1 /BaseFont /") + f + " /Encoding /WinAnsiEncoding >>");
    if (!annot.empty()) obj.push_back(annot);

    std::string pdf = "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
    std::vector<size_t> at;
    for (size_t i = 0; i < obj.size(); ++i) {
        at.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n";
    }
    const size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : at) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(obj.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) +
           "\n%%EOF\n";
    return pdf;
}

}  // namespace wc
