// views.cc - see views.h
#include "ui/views.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/node.hpp>

#include "ui/anim.h"
#include "ui/theme.h"

namespace wc::views {

using namespace ftxui;
namespace th = wc::theme;

namespace {

// Dotted line: every other sub-pixel along the major axis.
void dotted(Canvas& c, int x1, int y1, int x2, int y2, const Color& col) {
    int n = std::max(std::abs(x2 - x1), std::abs(y2 - y1));
    for (int i = 0; i <= n; i += 2) {
        double t = n ? double(i) / n : 0;
        c.DrawPoint(int(std::lround(x1 + (x2 - x1) * t)), int(std::lround(y1 + (y2 - y1) * t)),
                    true, col);
    }
}

// "DEPTH 2/3 ▰▰▰▱" - orange fill, dark-grey unfilled; LIMIT tag at max.
Element meter(const char* label, int v, int max, bool flash) {
    const int kSeg = 10;
    double r = max > 0 ? std::clamp(double(v) / max, 0.0, 1.0) : 0.0;
    bool at_limit = max > 0 && v >= max;
    int on = int(std::lround(r * kSeg));
    Color c = at_limit ? th::kAccent : Color::Interpolate(float(0.4 + 0.6 * r), th::kAccentDim,
                                                          th::kAccent);
    std::string bar_on, bar_off;
    for (int i = 0; i < kSeg; ++i) (i < on ? bar_on : bar_off) += "▰";
    Elements row = {
        text(label) | color(th::kChrome),
        text(" " + std::to_string(v) + "/" + std::to_string(max) + " ") | bold | color(th::kText),
        text(bar_on) | color(c),
        text(bar_off) | color(th::kFaint),
    };
    if (at_limit) {
        Element lim = text(" LIMIT ") | bold | color(th::kAccent);
        if (flash) lim = text(" LIMIT ") | bold | color(Color::Black) | bgcolor(th::kAccent);
        row.push_back(text(" "));
        row.push_back(lim);
    }
    return hbox(std::move(row));
}

// Fills its row, but lays the child out centred at min(max_w, width/2).
class CenterCap : public ftxui::Node {
public:
    CenterCap(Element child, int max_w) : ftxui::Node({std::move(child)}), max_w_(max_w) {}
    void ComputeRequirement() override {
        ftxui::Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
        requirement_.flex_grow_x = 1;
        requirement_.flex_shrink_x = 0;
    }
    void SetBox(Box box) override {
        ftxui::Node::SetBox(box);
        int full = box.x_max - box.x_min + 1;
        int w = std::max(std::min(max_w_, full / 2), std::min(full, requirement_.min_x));
        box.x_min += (full - w) / 2;
        box.x_max = box.x_min + w - 1;
        children_[0]->SetBox(box);
    }

private:
    int max_w_;
};

}  // namespace

Element graph(const CrawlGraph& g, const Layout& l, const ReplayFrame& f, View v) {
    // Copies: the lambda runs at render time, after the caller's frame may be gone.
    Element cv = canvas([g, l, f, v](Canvas& c) {
        auto sx = [&](int id) { return l.pos[id].x * v.scale + v.ox; };
        auto sy = [&](int id) { return l.pos[id].y * v.scale + v.oy; };
        auto shown = [&](int id) {
            return id >= 0 && id < int(l.pos.size()) && id < int(f.node_t.size());
        };
        // Faint dotted ring at radius 1 around the root.
        if (shown(0) && f.node_t[0] > 0) {
            int r = int(std::lround(v.scale));
            for (int i = 0; r > 2 && i < r * 6; i += 2) {
                double a = 2 * M_PI * i / (r * 6);
                c.DrawPoint(int(std::lround(sx(0) + r * std::cos(a))),
                            int(std::lround(sy(0) + r * std::sin(a))), true, th::kFaint);
            }
        }
        // Edges next, so nodes and labels sit on top. Cross-links (seen) always
        // drawn: the dense core is the look Tony wants (decision 2026-10-17).
        for (size_t e = 0; e < g.edges.size() && e < f.edge_t.size(); ++e) {
            float t = std::clamp(f.edge_t[e], 0.0f, 1.0f);
            if (t <= 0) continue;
            int a = g.id_of(g.edges[e].from), b = g.id_of(g.edges[e].to);
            if (!shown(a) || !shown(b)) continue;
            int x1 = int(std::lround(sx(a))), y1 = int(std::lround(sy(a)));
            int x2 = int(std::lround(sx(a) + (sx(b) - sx(a)) * t));
            int y2 = int(std::lround(sy(a) + (sy(b) - sy(a)) * t));
            Color col = g.edges[e].seen ? th::kFaint : th::depth_color(std::max(1, g.nodes[b].depth));
            dotted(c, x1, y1, x2, y2, col);
        }
        // Nodes: glyph (+ label for depth < 2), snapped to cells. Labels go in
        // id order (shallow first); one that would overlap a glyph or an
        // earlier label is skipped, its own glyph still drawn.
        const std::string root_url = g.nodes.empty() ? "" : g.nodes[0].url;
        // One byte per cell: what sits there. Off-canvas cells count as free (clipped anyway).
        enum : char { kFree, kHalo, kSolid, kLabel };  // kSolid = glyph or root label
        const int gw = c.width() / 2, gh = c.height() / 4;
        std::vector<char> grid(size_t(gw) * size_t(gh), kFree);
        auto at = [&](int col, int row) -> char {
            return col >= 0 && row >= 0 && col < gw && row < gh ? grid[size_t(row) * gw + col] : char(kFree);
        };
        auto mark = [&](int col, int row, char v) {
            if (col >= 0 && row >= 0 && col < gw && row < gh) grid[size_t(row) * gw + col] = std::max(grid[size_t(row) * gw + col], v);
        };
        auto cols = [](const std::string& s) {
            int n = 0;
            for (unsigned char ch : s) n += (ch & 0xC0) != 0x80;
            return n;
        };
        // A label of w cols at `col` needs [col-1, col+w] free: the two pads are drawn as
        // spaces, wiping any edge dots, so text never touches a dot or glyph. A halo label
        // also needs no other label on rows +-1 and keeps them for itself.
        auto place = [&](int col, int row, int w, bool halo) {
            for (int i = col - 1; i <= col + w; ++i) {
                if (at(i, row) != kFree) return false;
                if (halo && (at(i, row - 1) == kLabel || at(i, row + 1) == kLabel)) return false;
            }
            for (int i = col - 1; i <= col + w; ++i) {
                mark(i, row, halo ? kLabel : kSolid);
                if (halo) mark(i, row - 1, kHalo), mark(i, row + 1, kHalo);
            }
            return true;
        };
        auto drawn = [&](size_t id) { return shown(int(id)) && f.node_t[id] >= 1.0f; };
        auto cx_of = [&](size_t id) { return int(std::lround(sx(int(id)))) / 2 * 2; };
        auto cy_of = [&](size_t id) { return int(std::lround(sy(int(id)))) / 4 * 4; };
        // Pass 1: glyphs, each claiming its cell so no label paints over one.
        for (size_t id = 0; id < g.nodes.size(); ++id) {
            if (!drawn(id)) continue;
            const Node& n = g.nodes[id];
            int x = cx_of(id), y = cy_of(id);
            Color col = n.status == Status::Failed ? th::kChrome : th::depth_color(n.depth);
            bool root = id == 0;
            int shape = id < l.shape.size() ? l.shape[id] : -1;
            c.DrawText(x, y, th::glyph(shape, n.status), [col, root](Cell& p) {
                p.foreground_color = col;
                p.bold = root;
            });
            mark(x / 2, y / 4, kSolid);
        }
        // Pass 2: labels, root + depth 1 only, in id order.
        for (size_t id = 0; id < g.nodes.size(); ++id) {
            if (!drawn(id) || g.nodes[id].depth > 1) continue;
            int x = cx_of(id), y = cy_of(id);
            bool root = id == 0;
            std::string lab = node_label(g.nodes[id].url, root_url);
            int w = cols(lab);
            auto white = [root](Cell& p) {
                p.foreground_color = th::kText;
                p.bold = root;
            };
            // Root: centred under the glyph, no halo. Others: one col right of the glyph.
            int col = root ? std::max(0, x / 2 - w / 2) : x / 2 + 2, row = y / 4 + (root ? 1 : 0);
            if (place(col, row, w, !root)) c.DrawText((col - 1) * 2, row * 4, " " + lab + " ", white);
        }
    });
    if (l.sites.size() < 2) return cv;
    // Legend, bottom-left: shape -> site. Shape = site, color = depth.
    Elements legend{text(" ")};
    for (size_t r = 0; r < l.sites.size(); ++r)
        legend.push_back(hbox({text(th::glyph(int(r), Status::Fetched)) | color(th::kText),
                               text(" " + l.sites[r] + "   ") | color(th::kChrome)}));
    legend.push_back(text("· other") | color(th::kChrome));
    return dbox({cv, vbox({filler(), hbox(std::move(legend))})});
}

Element boss_bar(int depth, int max_depth, int size, int max_size, bool flash_depth,
                 bool flash_size) {
    return vbox({
        hbox({
            text(" "),
            meter("DEPTH", depth, max_depth, flash_depth),
            filler(),
            meter("LINKS", size, max_size, flash_size),
            text(" "),
        }),
        separatorLight() | color(th::kFaint),
    });
}

Element input_box(Element input) {
    // White on black incl. the border; NO_COLOR keeps border + plain text.
    return std::move(input) | color(Color::White) | borderStyled(ROUNDED, th::kAccent) | bgcolor(Color::Black);
}

Element input_text(Element input, bool placeholder) {
    input = std::move(input) | color(Color::White);
    return placeholder ? std::move(input) | dim : input;
}

Element field(const std::string& label, Element input, int width) {
    Element box = input_box(hbox({text("› ") | bold | color(th::kAccent), std::move(input) | size(WIDTH, EQUAL, width)}));
    if (label.empty()) return box;
    return hbox({text(label + " ") | bold | color(th::kText) | vcenter, std::move(box)});  // vcenter: border rows above/below
}

Element omnibar(Element input, const std::vector<std::string>& results, int selected,
                const std::string& ghost) {
    Elements rows;
    // Drop up: results[0] sits right above the input line.
    for (int i = int(results.size()) - 1; i >= 0; --i) {
        if (i == selected)
            rows.push_back(hbox({text("▌") | color(th::kAccent),
                                 text(" " + results[i]) | bold | color(th::kText), filler()}));
        else
            rows.push_back(hbox({text("  " + results[i]) | color(th::kChrome), filler()}));
    }
    // Ghost: first line trails the input; further lines (placeholder) sit dim inside the box.
    std::string first = ghost.substr(0, ghost.find('\n'));
    // Input never grows (Input is xflex): the ghost sits right after the typed text, not mid-box.
    Elements box = {hbox({text("› ") | bold | color(th::kAccent), std::move(input) | xflex_factor(0, 1), text(first) | color(th::kChrome), filler()})};
    for (size_t p = ghost.find('\n'); p != std::string::npos; p = ghost.find('\n', p + 1))
        box.push_back(text("  " + ghost.substr(p + 1, ghost.find('\n', p + 1) - p - 1)) | color(th::kChrome) | dim);
    rows.push_back(input_box(vbox(std::move(box))));
    // Centred column, <= 60 cols and <= half the screen; results share it.
    return std::make_shared<CenterCap>(vbox(std::move(rows)), 60);
}

Element toast(const std::string& t) {
    return hbox({text(" ✓ ") | bold | color(th::kAccent), text(t + " ") | color(th::kText)}) |
           borderStyled(ROUNDED, th::kChrome) | clear_under;  // opaque over the graph
}

Element too_small(int w, int h) {
    return vbox({
               filler(),
               hcenter(text("Enlarge terminal to 80×24 (now " + std::to_string(w) + "×" +
                            std::to_string(h) + ")") |
                       color(th::kText)),
               filler(),
           }) |
           flex;
}

Element preview(const std::string& title, const std::vector<std::string>& lines, int scroll,
                float glow, bool maximized) {
    Color edge = Color::Interpolate(std::clamp(glow, 0.0f, 1.0f), th::kAccentDim, th::kAccent);
    Elements body;
    for (size_t i = size_t(std::max(0, scroll)); i < lines.size(); ++i) {
        const std::string& ln = lines[i];
        if (ln.rfind("# ", 0) == 0)
            body.push_back(paragraph(ln.substr(2)) | bold | color(th::kText));
        else if (ln.rfind("[IMG", 0) == 0)
            body.push_back(paragraph(ln) | dim | color(th::kAccent));
        else
            body.push_back(paragraph(ln) | color(th::kText));
    }
    Element content = vbox({
        vbox(std::move(body)) | flex,
        text("j/k scroll · m max · Esc close") | dim | color(th::kChrome),
    });
    // color() paints the border; every child above sets its own colour on top.
    Element panel = window(text(" PREVIEW · " + title + " ") | bold | color(th::kAccent),
                           std::move(content), ROUNDED) |
                    color(edge) | clear_under;  // opaque over the graph
    // Vendored Size::SetBox keeps value+2 cols (x_min + value + 1), so 58 -> 60 wide.
    return maximized ? panel : panel | size(WIDTH, LESS_THAN, 58);
}

Element coach(int step, int total, const std::string& t) {
    return vbox({
               hbox({text(std::to_string(step) + "/" + std::to_string(total)) | bold |
                         color(th::kAccent),
                     text("  " + t) | color(th::kText)}),
               text("Tab next · Esc skip") | dim | color(th::kChrome),
           }) |
           borderStyled(ROUNDED, th::kFaint) | clear_under;  // opaque over the graph
}

int loading_rows(int term_h) {
    // Rows taken by chrome: border 2, title 2, counters 1, rule 1, footer 1, blank top row 1.
    return std::max(1, term_h - 8);
}

Element loading(const LoadingState& s, Element background) {
    Color edge = Color::Interpolate(std::clamp(s.pulse, 0.0f, 1.0f), th::kAccentDim, th::kAccent);
    static const char* spin[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const Progress& p = s.progress;
    auto count = [](const char* k, int v) {
        return hbox({text(k) | color(th::kChrome), text(std::to_string(v) + " ") | bold | color(th::kText)});
    };
    Elements rows = {
        hbox({text(" "), text(spin[s.tick % 10]) | color(th::kAccent), text(" " + s.line) | color(th::kText)}),
        text("   crawling " + s.host) | color(th::kChrome),
        hbox({count(" fetched ", p.fetched), count(" links ", s.max_links > 0 ? std::min(p.found, s.max_links) : p.found),
              count(" fail ", p.failed), count(" depth ", p.depth)}),
        separatorLight() | color(th::kFaint),
    };
    // Checklist: newest at the bottom, brightest; older lines step down toward the chrome grey.
    // Fades (s.fade): each line rises from faint as it lands; a full list sinks its top line.
    using anim::Rgb;
    const Rgb mid{180, 180, 180};
    const int kFadeMs = 400;
    int n = int(p.recent.size());
    const bool full = s.rows > 0 && n >= s.rows;
    const double out_t = s.fade && full && n > 1 ? anim::ramp(s.now_ms - p.recent.back().at_ms, kFadeMs) : 0.0;
    for (int i = 0; i < n; ++i) {
        const FetchLine& f = p.recent[size_t(i)];
        Rgb c = i == n - 1 ? th::rgb::kText : i >= n - 3 ? mid : th::rgb::kChrome;
        Rgb mark = f.ok ? th::rgb::kAccent : th::rgb::kChrome;
        if (s.fade) {
            double in = anim::ease_out(anim::ramp(s.now_ms - f.at_ms, kFadeMs));
            double vis = i == 0 ? in * (1.0 - anim::ease_in(out_t)) : in;
            c = anim::mix(th::rgb::kFaint, c, vis), mark = anim::mix(th::rgb::kFaint, mark, vis);
        }
        rows.push_back(hbox({text(f.ok ? " ✓ " : " ✗ ") | color(th::to_color(mark)),
                             text(short_label(f.url) + url_path(f.url)) | color(th::to_color(c))}));
    }
    // Fixed width (no wobble as longer labels land), every line clipped to it; border adds 2.
    int w = std::clamp(s.cols - 4, 20, 62);
    // clear_under: the box is opaque, so a background layer (rain) never shows through the text.
    Element box = vbox(std::move(rows)) | size(WIDTH, EQUAL, w) | borderStyled(ROUNDED, edge) | clear_under;
    // Blank top row balances the footer row (same rule as the prompt screens).
    Element content = vbox({text(""), filler(), hcenter(box), filler(), hcenter(text("Esc stop · Ctrl-C quit") | color(th::kChrome) | dim)}) | flex;
    return dbox({std::move(background), std::move(content)});
}

}  // namespace wc::views
