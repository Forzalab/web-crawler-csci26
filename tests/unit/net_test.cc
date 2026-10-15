// net_test.cc - unit tests for src/net (URL resolve, link extraction, HTTP
// parsing, redirects, routing). No network: the fetcher gets a fake transport.
// Run: build/wc_net_test   (exit 1 if any check fails)
#include <iostream>
#include <map>

#include "mocks/mock_fetcher.h"
#include "net/html_links.h"
#include "net/http.h"
#include "net/https_fetcher.h"
#include "net/url.h"

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

static std::string res(const std::string& base, const std::string& ref) {
    auto u = resolve_url(*parse_url(base), ref);
    return u ? u->str() : "<none>";
}

static std::string parsed(const std::string& s) {
    auto u = parse_url(s);
    return u ? u->str() : "<none>";
}

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += x + "\n";
    return s;
}

static void test_parse() {
    CHECK_EQ(parsed("HTTP://Ex.COM"), std::string("http://ex.com/"));
    CHECK_EQ(parsed("http://h:80/a"), std::string("http://h/a"));
    CHECK_EQ(parsed("https://h:443/a?x=1#frag"), std::string("https://h/a?x=1"));
    CHECK_EQ(parsed("http://h:8080?q"), std::string("http://h:8080/?q"));
    CHECK_EQ(parsed("http://user:pw@h/x"), std::string("http://h/x"));
    CHECK_EQ(parsed("http://[::1]:8080/a"), std::string("http://[::1]:8080/a"));
    CHECK_EQ(parsed("  http://h/a b/./c/../d  "), std::string("http://h/a%20b/d"));
    CHECK_EQ(parsed("ftp://h/"), std::string("<none>"));
    CHECK_EQ(parsed("http://"), std::string("<none>"));
    CHECK_EQ(parsed("http://h:99999/"), std::string("<none>"));
    CHECK_EQ(parsed("h.com/x"), std::string("<none>"));
    CHECK_EQ(parse_url("https://h:8443/p?q")->target(), std::string("/p?q"));
}

static void test_resolve() {  // RFC 3986 §5.4.1 (fragments dropped on purpose)
    const std::string b = "http://a/b/c/d;p?q";
    const std::map<std::string, std::string> cases = {
        {"g", "http://a/b/c/g"},       {"./g", "http://a/b/c/g"},        {"g/", "http://a/b/c/g/"},
        {"/g", "http://a/g"},          {"//g", "http://g/"},             {"?y", "http://a/b/c/d;p?y"},
        {"g?y", "http://a/b/c/g?y"},   {"#s", "http://a/b/c/d;p?q"},     {"g#s", "http://a/b/c/g"},
        {";x", "http://a/b/c/;x"},     {"", "http://a/b/c/d;p?q"},       {".", "http://a/b/c/"},
        {"./", "http://a/b/c/"},       {"..", "http://a/b/"},            {"../g", "http://a/b/g"},
        {"../..", "http://a/"},        {"../../g", "http://a/g"},        {"../../../g", "http://a/g"},
        {"/./g", "http://a/g"},        {"g.", "http://a/b/c/g."},        {"g/../h", "http://a/b/c/h"},
        {"g;x=1/../y", "http://a/b/c/y"}, {"mailto:x@y.z", "<none>"},   {"javascript:void(0)", "<none>"},
        {"HTTPS://Other.org:443/z", "https://other.org/z"},
    };
    for (const auto& [ref, want] : cases) CHECK_EQ(res(b, ref), want);
    CHECK_EQ(res("https://ex.com/a/", "//cdn.ex.com/x"), std::string("https://cdn.ex.com/x"));
}

static void test_links() {
    const std::string html =
        "<A HREF=\"/abs\">x</a> <a class=x href='rel.html#frag'>y</a> <a href=bare>z</a>\n"
        "<a href=\"mailto:m@x\">m</a><a href=\"javascript:void(0)\">j</a><a href=\"#top\">t</a>\n"
        "<!-- <a href=\"/dead\"> --> <a href=\"/abs\"> <a data-href=\"/no\"> <abbr href=\"/no2\">\n"
        "<a href=\"?q=1&amp;b=2\"> <a\n  href = \"//cdn.ex.com/x\"> <a name=x> <a href=\"\">";
    CHECK_EQ(join(extract_links(html, *parse_url("https://ex.com/dir/page.html"))),
             std::string("https://ex.com/abs\n"
                         "https://ex.com/dir/rel.html\n"
                         "https://ex.com/dir/bare\n"
                         "https://ex.com/abs\n"
                         "https://ex.com/dir/page.html?q=1&b=2\n"
                         "https://cdn.ex.com/x\n"));
}

static void test_http_parse() {
    auto r = parse_http_response("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 5\r\n\r\nhelloEXTRA");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.error, std::string(""));
    CHECK_EQ(r.body, std::string("hello"));
    CHECK_EQ(r.headers["content-type"], std::string("text/html"));

    r = parse_http_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6;ext=1\r\n world\r\n0\r\n\r\n");
    CHECK_EQ(r.body, std::string("hello world"));
    CHECK_EQ(r.error, std::string(""));

    r = parse_http_response("HTTP/1.0 301 Moved\r\nLocation: https://x/\r\n\r\n");
    CHECK_EQ(r.status, 301);
    CHECK_EQ(r.headers["location"], std::string("https://x/"));

    CHECK_EQ(parse_http_response("garbage").error.rfind("bad_response", 0), size_t(0));
    CHECK_EQ(parse_http_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nff\r\nshort").error.rfind("bad_response", 0),
             size_t(0));
}

static HttpResponse resp(int status, std::map<std::string, std::string> headers, std::string body = "") {
    HttpResponse r;
    r.status = status;
    r.headers = std::move(headers);
    r.body = std::move(body);
    return r;
}

static void test_fetcher() {
    std::map<std::string, HttpResponse> web = {
        {"http://a.com/", resp(308, {{"location", "https://a.com/"}})},
        {"https://a.com/", resp(301, {{"location", "/home"}})},
        {"https://a.com/home", resp(200, {{"content-type", "text/html; charset=utf-8"}}, "<a href=\"x\">")},
        {"http://loop/", resp(302, {{"location", "http://loop/"}})},
        {"http://nf/", resp(404, {{"content-type", "text/html"}})},
        {"http://img/", resp(200, {{"content-type", "image/png"}})},
        {"http://noloc/", resp(301, {})},
        {"http://notype/", resp(200, {}, "<a href=/y>")},
    };
    int calls = 0;
    HttpsFetcher f([&](const Url& u) {
        ++calls;
        auto it = web.find(u.str());
        if (it != web.end()) return it->second;
        HttpResponse r;
        r.error = "dns: unknown host";
        return r;
    });

    auto l = f.links("http://a.com/");
    CHECK_EQ(l.has_value(), true);
    if (l) CHECK_EQ(join(*l), std::string("https://a.com/x\n"));
    CHECK_EQ(f.last().redirects, 2);
    CHECK_EQ(f.last().final_url, std::string("https://a.com/home"));
    CHECK_EQ(f.last().status, 200);

    calls = 0;
    CHECK_EQ(f.links("http://loop/").has_value(), false);
    CHECK_EQ(f.last().error.rfind("redirects:", 0), size_t(0));
    CHECK_EQ(calls, HttpsFetcher::kMaxRedirects + 1);

    CHECK_EQ(f.links("http://nf/").has_value(), false);
    CHECK_EQ(f.last().error, std::string("http: 404"));
    CHECK_EQ(f.links("http://img/").has_value(), false);
    CHECK_EQ(f.last().error, std::string("not_html: image/png"));
    CHECK_EQ(f.links("http://noloc/").has_value(), false);
    CHECK_EQ(f.last().error.rfind("redirects:", 0), size_t(0));
    CHECK_EQ(f.links("http://gone/").has_value(), false);
    CHECK_EQ(f.last().error, std::string("dns: unknown host"));
    CHECK_EQ(f.links("not a url").has_value(), false);
    CHECK_EQ(f.last().error.rfind("bad_url", 0), size_t(0));
    l = f.links("http://notype/");
    CHECK_EQ(l.has_value() ? join(*l) : std::string("<none>"), std::string("http://notype/y\n"));
}

static void test_router() {
    MockFetcher josh(FakeWeb{{"http://j/", std::vector<std::string>{"http://j/1"}}});
    MockFetcher tls(FakeWeb{{"https://t/", std::vector<std::string>{"https://t/1"}},
                            {"http://k/", std::vector<std::string>{"https://k/1"}}});
    RoutedFetcher r(josh, tls);

    CHECK_EQ(join(*r.links("https://t/")), std::string("https://t/1\n"));
    CHECK_EQ(josh.call_log.size(), size_t(0));  // https never goes to Josh
    CHECK_EQ(join(*r.links("http://j/")), std::string("http://j/1\n"));
    CHECK_EQ(tls.call_log.size(), size_t(1));   // Josh answered, tls not asked
    CHECK_EQ(join(*r.links("http://k/")), std::string("https://k/1\n"));  // Josh failed -> tls
    CHECK_EQ(r.links("http://none/").has_value(), false);
}

int main() {
    test_parse();
    test_resolve();
    test_links();
    test_http_parse();
    test_fetcher();
    test_router();
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
