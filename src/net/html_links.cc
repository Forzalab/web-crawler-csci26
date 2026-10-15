#include "net/html_links.h"

#include <cctype>

namespace wc {

namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)); }

bool iequal_at(const std::string& s, size_t i, const char* word) {
    for (; *word; ++word, ++i)
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != *word) return false;
    return true;
}

// The common entities that show up inside hrefs. Others stay as they are.
std::string decode_entities(std::string s) {
    static const std::pair<const char*, const char*> ents[] = {
        {"&amp;", "&"}, {"&#38;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}};
    for (const auto& [from, to] : ents) {
        std::string f = from;
        for (size_t p = s.find(f); p != std::string::npos; p = s.find(f, p + 1)) s.replace(p, f.size(), to);
    }
    return s;
}

// Value of attribute `name` inside one tag body (text between "<a" and ">").
bool find_attr(const std::string& tag, const char* name, std::string& value) {
    size_t n = std::char_traits<char>::length(name);
    for (size_t i = 0; i < tag.size(); ++i) {
        if (!iequal_at(tag, i, name) || (i > 0 && !is_space(tag[i - 1]))) continue;
        size_t j = i + n;
        while (j < tag.size() && is_space(tag[j])) ++j;
        if (j >= tag.size() || tag[j] != '=') continue;
        ++j;
        while (j < tag.size() && is_space(tag[j])) ++j;
        if (j >= tag.size()) return false;
        if (tag[j] == '"' || tag[j] == '\'') {
            size_t end = tag.find(tag[j], j + 1);
            if (end == std::string::npos) return false;
            value = tag.substr(j + 1, end - j - 1);
        } else {
            size_t end = j;
            while (end < tag.size() && !is_space(tag[end])) ++end;
            value = tag.substr(j, end - j);
        }
        return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> extract_links(const std::string& html, const Url& base) {
    std::vector<std::string> out;
    for (size_t i = html.find('<'); i != std::string::npos; i = html.find('<', i + 1)) {
        if (iequal_at(html, i, "<!--")) {  // skip comments: links inside are dead
            i = html.find("-->", i);
            if (i == std::string::npos) break;
            continue;
        }
        if (!iequal_at(html, i, "<a") || i + 2 >= html.size() || !is_space(html[i + 2])) continue;
        size_t end = html.find('>', i);
        if (end == std::string::npos) break;
        std::string href;
        if (find_attr(html.substr(i + 2, end - i - 2), "href", href)) {
            size_t s = href.find_first_not_of(" \t\r\n");
            if (s != std::string::npos && href[s] != '#')  // "#top" = same page, not a link
                if (auto u = resolve_url(base, decode_entities(href))) out.push_back(u->str());
        }
        i = end;
    }
    return out;
}

}  // namespace wc
