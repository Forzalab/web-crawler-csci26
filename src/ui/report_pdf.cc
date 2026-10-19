#include "ui/report_pdf.h"

#include <algorithm>
#include <cstdio>
#include <vector>

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

// One page content stream.
class Page {
public:
    void text(double x, double y, Font f, double size, const std::string& s, double gray = 0) {
        out_ += num(gray) + " g BT /F" + std::to_string(f + 1) + ' ' + num(size) + " Tf " + num(x) + ' ' + num(y) +
                " Td (" + pdf_text(s) + ") Tj ET\n";
    }
    void line(double x1, double y1, double x2, double y2) {
        out_ += "q 0.6 G 0.75 w " + num(x1) + ' ' + num(y1) + " m " + num(x2) + ' ' + num(y2) + " l S Q\n";
    }
    void frame(double x, double y, double w, double h) {
        out_ += "q 0.75 G 0.5 w " + num(x) + ' ' + num(y) + ' ' + num(w) + ' ' + num(h) + " re S Q\n";
    }
    void image(const std::string& name, double x, double y, double w, double h) {
        out_ += "q " + num(w) + " 0 0 " + num(h) + ' ' + num(x) + ' ' + num(y) + " cm /" + name + " Do Q\n";
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

bool jpeg_info(const std::string& j, int& w, int& h, int& comps) {
    auto u8 = [&](size_t i) { return static_cast<unsigned char>(j[i]); };
    if (j.size() < 4 || u8(0) != 0xFF || u8(1) != 0xD8) return false;
    for (size_t i = 2; i + 9 < j.size();) {
        if (u8(i) != 0xFF) return false;
        const int m = u8(i + 1);
        if (m == 0xFF) {  // fill byte
            ++i;
            continue;
        }
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {  // SOFn
            h = u8(i + 5) << 8 | u8(i + 6);
            w = u8(i + 7) << 8 | u8(i + 8);
            comps = u8(i + 9);
            return w > 0 && h > 0 && (comps == 1 || comps == 3);
        }
        i += 2 + size_t(u8(i + 2) << 8 | u8(i + 3));
    }
    return false;
}

std::string summary_pdf(const ReportSummary& r) {
    Page pg;
    double y = 742;

    // Site.
    pg.text(kL, y, kBold, 20, clip(r.host, 40));
    y -= 18;
    pg.text(kL, y, kMono, 9, clip(r.url, 94));
    y -= 14;
    pg.text(kL, y, kSans, 10, r.mode + " crawl, depth " + std::to_string(r.depth) + ", size " + std::to_string(r.size));
    y -= 10;
    pg.line(kL, y, kR, y);

    // Stats.
    auto stat = [&](double x, const std::string& k, const std::string& v) {
        pg.text(x, y, kSans, 11, k, 0.35);
        pg.text(x + 80, y, kBold, 11, v);
    };
    const double c2 = 316;
    y -= 20;
    stat(kL, "Fetched", std::to_string(r.fetches));
    stat(c2, "Failed", std::to_string(r.failed));
    y -= 16;
    stat(kL, "Nodes", std::to_string(r.nodes));
    stat(c2, "Edges", std::to_string(r.edges));
    y -= 16;
    stat(kL, "Blocked", std::to_string(r.blocked) + "   (robots.txt " + std::to_string(r.robots) + ", HTTP 429 " +
                            std::to_string(r.too_many) + ", bot wall " + std::to_string(r.bot_wall) + ")");
    y -= 16;
    stat(kL, "Wall time", wall_time(r.wall_ms) + (r.wall_note.empty() ? "" : "   " + r.wall_note));
    y -= 16;
    stat(kL, "Cache", std::to_string(r.cache_hits) + " hits / " + std::to_string(r.cache_misses) + " misses");
    y -= 12;
    pg.line(kL, y, kR, y);

    // Pictures: each fitted into a 512 x 260 box, aspect kept, centred.
    std::vector<std::string> xobj;
    auto picture = [&](const std::string& caption, const std::string& jpg) {
        constexpr double bw = kR - kL, bh = 260;
        y -= 20;
        pg.text(kL, y, kBold, 12, caption);
        y -= 6 + bh;
        int w = 0, h = 0, comps = 0;
        if (!jpeg_info(jpg, w, h, comps)) {
            pg.frame(kL, y, bw, bh);
            pg.text(kL + 12, y + bh / 2, kSans, 10, "no screenshot (scripts/report_shots.sh)", 0.5);
            return;
        }
        const double s = std::min(bw / w, bh / h), iw = w * s, ih = h * s, ix = kL + (bw - iw) / 2, iy = y + (bh - ih) / 2;
        const std::string name = "Im" + std::to_string(xobj.size() + 1);
        pg.image(name, ix, iy, iw, ih);
        pg.frame(ix, iy, iw, ih);
        xobj.push_back("<< /Type /XObject /Subtype /Image /Width " + std::to_string(w) + " /Height " + std::to_string(h) +
                       " /ColorSpace /" + (comps == 3 ? "DeviceRGB" : "DeviceGray") +
                       " /BitsPerComponent 8 /Filter /DCTDecode /Length " + std::to_string(jpg.size()) + " >>\nstream\n" +
                       jpg + "\nendstream");
    };
    picture("BRIDGES", r.bridges_jpg);
    picture("Our app", r.app_jpg);

    // Objects: 1 catalog, 2 pages, 3 page, 4 contents, 5-7 fonts, 8.. images.
    std::string images;
    for (size_t i = 0; i < xobj.size(); ++i) images += " /Im" + std::to_string(i + 1) + ' ' + std::to_string(8 + i) + " 0 R";
    std::vector<std::string> obj = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + num(kW) + ' ' + num(kH) +
            "] /Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R >>" +
            (images.empty() ? "" : " /XObject <<" + images + " >>") + " >> /Contents 4 0 R >>",
        "<< /Length " + std::to_string(pg.str().size()) + " >>\nstream\n" + pg.str() + "endstream",
    };
    for (const char* f : kFontName)
        obj.push_back(std::string("<< /Type /Font /Subtype /Type1 /BaseFont /") + f + " /Encoding /WinAnsiEncoding >>");
    obj.insert(obj.end(), xobj.begin(), xobj.end());

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
