// html_text.cc - see html_text.h
#include "ui/html_text.h"

#include <cctype>

namespace wc {

namespace {

const size_t kMaxLines = 2000;

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)); }
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool iequal_at(const std::string& s, size_t i, const std::string& word) {
    if (i + word.size() > s.size()) return false;
    for (size_t k = 0; k < word.size(); ++k)
        if (lower(s[i + k]) != word[k]) return false;
    return true;
}

// Case-insensitive find of `word` (already lowercase) from `from`.
size_t ifind(const std::string& s, const std::string& word, size_t from) {
    for (size_t i = from; i + word.size() <= s.size(); ++i)
        if (iequal_at(s, i, word)) return i;
    return std::string::npos;
}

// Decode one entity at s[i] == '&'. Sets len consumed; -1 if not recognised.
int decode_entity(const std::string& s, size_t i, size_t& len) {
    size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) return -1;
    std::string name = s.substr(i + 1, semi - i - 1);
    len = semi - i + 1;
    static const std::pair<const char*, int> named[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '}};
    for (const auto& [n, c] : named)
        if (name == n) return c;
    if (name.size() < 2 || name[0] != '#') return -1;
    bool hex = name[1] == 'x' || name[1] == 'X';
    size_t start = hex ? 2 : 1;
    if (start >= name.size()) return -1;
    long v = 0;
    for (size_t k = start; k < name.size(); ++k) {
        char c = lower(name[k]);
        int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                : (hex && c >= 'a' && c <= 'f')            ? c - 'a' + 10
                                                           : -1;
        if (d < 0 || v > 0x10FFFF) return -1;
        v = v * (hex ? 16 : 10) + d;
    }
    if (v == 160) return ' ';
    return (v >= 32 && v < 127) || v == '\n' || v == '\t' ? int(v) : -1;
}

std::string decode_all(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        size_t len = 0;
        int c = s[i] == '&' ? decode_entity(s, i, len) : -1;
        if (c < 0) {
            out += s[i];
        } else {
            out += char(c);
            i += len - 1;
        }
    }
    return out;
}

bool find_attr(const std::string& tag, const std::string& name, std::string& value) {
    for (size_t i = 1; i < tag.size(); ++i) {
        if (!is_space(tag[i - 1]) || !iequal_at(tag, i, name)) continue;
        size_t j = i + name.size();
        while (j < tag.size() && is_space(tag[j])) ++j;
        if (j >= tag.size() || tag[j] != '=') continue;
        ++j;
        while (j < tag.size() && is_space(tag[j])) ++j;
        if (j >= tag.size()) return false;
        size_t end;
        if (tag[j] == '"' || tag[j] == '\'') {
            end = tag.find(tag[j], j + 1);
            if (end == std::string::npos) end = tag.size();
            ++j;
        } else {
            end = j;
            while (end < tag.size() && !is_space(tag[end]) && tag[end] != '>' && tag[end] != '/') ++end;
        }
        value = decode_all(tag.substr(j, end - j));
        return true;
    }
    return false;
}

struct Out {
    std::vector<std::string> lines;
    std::string cur;
    size_t prefix = 0;  // length of "# " / "- " at the start of cur
    bool space = false;

    bool full() const { return lines.size() >= kMaxLines; }

    void push(std::string l, bool keep_blank = false) {
        if (full()) return;
        if (l.empty() && !keep_blank && (lines.empty() || lines.back().empty())) return;
        lines.push_back(std::move(l));
    }
    // End the current line; `force` emits it even when empty (<br>).
    void brk(bool force = false) {
        while (!cur.empty() && is_space(cur.back())) cur.pop_back();
        if (cur.size() > prefix || force) push(cur.size() > prefix ? cur : "");
        cur.clear();
        prefix = 0;
        space = false;
    }
    void para() {
        brk();
        push("");
    }
    void start(const char* p) {
        brk();
        cur = p;
        prefix = cur.size();
    }
    void text(char c) {
        if (is_space(c)) {
            space = true;
            return;
        }
        if (space && cur.size() > prefix) cur += ' ';
        space = false;
        cur += c;
    }
    void word(const std::string& w) {
        for (char c : w) text(c);
    }
};

bool one_of(const std::string& n, std::initializer_list<const char*> set) {
    for (const char* s : set)
        if (n == s) return true;
    return false;
}

}  // namespace

std::vector<std::string> html_to_lines(const std::string& html) {
    Out o;
    bool pre = false;
    size_t i = 0;
    while (i < html.size() && !o.full()) {
        char c = html[i];
        if (c == '&') {
            size_t len = 0;
            int d = decode_entity(html, i, len);
            if (d >= 0) {
                if (!pre) o.text(char(d));
                else o.cur += char(d);
                i += len;
                continue;
            }
        }
        if (c != '<') {
            if (pre) {
                if (c == '\n') {
                    o.push(o.cur, true);
                    o.cur.clear();
                } else if (c != '\r') {
                    o.cur += c;
                }
            } else {
                o.text(c);
            }
            ++i;
            continue;
        }
        if (html.compare(i, 4, "<!--") == 0) {
            size_t e = html.find("-->", i + 4);
            i = e == std::string::npos ? html.size() : e + 3;
            continue;
        }
        size_t j = i + 1;
        bool close = j < html.size() && html[j] == '/';
        if (close) ++j;
        bool bang = j < html.size() && (html[j] == '!' || html[j] == '?');
        if (!bang && (j >= html.size() || !std::isalpha(static_cast<unsigned char>(html[j])))) {
            if (pre) o.cur += c;
            else o.text(c);
            ++i;
            continue;
        }
        // Tag end: first '>' outside quotes.
        size_t e = j;
        char q = 0;
        for (; e < html.size(); ++e) {
            if (q) {
                if (html[e] == q) q = 0;
            } else if (html[e] == '"' || html[e] == '\'') {
                q = html[e];
            } else if (html[e] == '>') {
                break;
            }
        }
        std::string tag = html.substr(i, e - i);
        i = e < html.size() ? e + 1 : html.size();
        if (bang) continue;
        std::string name;
        while (j < html.size() && j < e && std::isalnum(static_cast<unsigned char>(html[j]))) name += lower(html[j++]);

        if (!close && one_of(name, {"script", "style", "noscript", "svg", "head", "title"})) {
            size_t end = ifind(html, "</" + name, i);
            if (end == std::string::npos) break;
            size_t gt = html.find('>', end);
            i = gt == std::string::npos ? html.size() : gt + 1;
            continue;
        }
        if (name == "pre") {
            o.para();
            pre = !close;
            if (pre && i < html.size() && html[i] == '\r') ++i;
            if (pre && i < html.size() && html[i] == '\n') ++i;
            continue;
        }
        if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
            if (close) o.para();
            else {
                o.para();
                o.start("# ");
            }
        } else if (name == "li") {
            if (close) o.brk();
            else o.start("- ");
        } else if (name == "br") {
            if (pre) {
                o.push(o.cur, true);
                o.cur.clear();
            } else {
                o.brk(true);
            }
        } else if (name == "img" && !close) {
            std::string alt;
            if (find_attr(tag, "alt", alt) && !alt.empty()) {
                if (pre) o.cur += "[IMG " + alt + "]";
                else {
                    if (o.cur.size() > o.prefix) o.space = true;
                    o.word("[IMG " + alt + "]");
                }
            }
        } else if (one_of(name, {"p", "ul", "ol", "table", "blockquote", "hr", "section", "article", "header",
                                 "footer", "nav", "main", "aside", "form", "dl"})) {
            o.para();
        } else if (one_of(name, {"div", "tr", "dt", "dd", "caption", "figcaption"})) {
            o.brk();
        } else if (one_of(name, {"td", "th"})) {
            o.space = true;
        }
    }
    if (!o.full()) o.brk();
    while (!o.lines.empty() && o.lines.back().empty()) o.lines.pop_back();
    if (o.lines.size() > kMaxLines) o.lines.resize(kMaxLines);
    return o.lines;
}

}  // namespace wc
