// net_test.cc - unit tests for src/net (URL resolve, link extraction, HTTP
// parsing, redirects, routing). No network: the fetcher gets a fake transport.
// Run: build/wc_net_test   (exit 1 if any check fails)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

#include "mocks/mock_fetcher.h"
#include "net/cache.h"
#include "net/gateway.h"
#include "net/html_links.h"
#include "net/robots.h"
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

static void test_with_scheme() {
    CHECK_EQ(with_scheme("espn.com"), "https://espn.com");
    CHECK_EQ(with_scheme("  espn.com/a?b "), "https://espn.com/a?b");
    CHECK_EQ(with_scheme("http://espn.com"), "http://espn.com");
    CHECK_EQ(with_scheme("https://espn.com"), "https://espn.com");
    CHECK_EQ(with_scheme("localhost:8080"), "https://localhost:8080");
    CHECK_EQ(parse_url(with_scheme("espn.com"))->host, "espn.com");
}

static void test_pick_start() {
    int calls = 0;
    auto fail_with = [&](const std::string& error, int status) {
        return [&calls, error, status](const std::string&) {
            ++calls;
            FetchResult r;
            r.error = error;
            r.status = status;
            return r;
        };
    };
    CHECK_EQ(pick_start("espn.com", fail_with("tls: handshake", 0)), "http://espn.com");
    CHECK_EQ(pick_start("espn.com/a", fail_with("dns: no such host", 0)), "http://espn.com/a");
    CHECK_EQ(pick_start("espn.com", fail_with("http: 404", 404)), "https://espn.com");
    CHECK_EQ(pick_start("espn.com", fail_with("", 200)), "https://espn.com");
    CHECK_EQ(calls, 4);
    CHECK_EQ(pick_start("http://espn.com", fail_with("tls: x", 0)), "http://espn.com");
    CHECK_EQ(pick_start("https://espn.com", fail_with("tls: x", 0)), "https://espn.com");
    CHECK_EQ(calls, 4);  // a typed scheme is never probed
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
    f.use_robots(false);

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

static void test_cache() {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("wc_net_test_" + std::to_string(getpid()));
    fs::remove_all(dir);
    PageCache cache(dir.string());

    FetchResult ok;
    ok.url = "http://a.com/";
    ok.final_url = "https://a.com/";
    ok.status = 200;
    ok.redirects = 1;
    ok.content_type = "text/html";
    ok.body = std::string("<a href=x>\n\nline\0bin", 21);
    cache.store(ok);
    CHECK_EQ(ok.fetched_at.empty(), false);
    auto hit = cache.load("http://a.com/");
    CHECK_EQ(hit.has_value(), true);
    if (hit) {
        CHECK_EQ(hit->body, ok.body);
        CHECK_EQ(hit->final_url, ok.final_url);
        CHECK_EQ(hit->status, 200);
        CHECK_EQ(hit->redirects, 1);
        CHECK_EQ(hit->content_type, ok.content_type);
        CHECK_EQ(hit->error, std::string(""));
        CHECK_EQ(hit->fetched_at, ok.fetched_at);
        CHECK_EQ(hit->from_cache, true);
    }
    CHECK_EQ(cache.load("http://other/").has_value(), false);

    FetchResult nf = ok;
    nf.url = "http://nf/";
    nf.status = 404;
    nf.error = "http: 404";
    cache.store(nf);
    hit = cache.load("http://nf/");
    CHECK_EQ(hit ? hit->error + "|" + hit->body : std::string("<miss>"), std::string("http: 404|"));  // body dropped

    FetchResult down;
    down.url = "http://down/";
    down.error = "timeout: read";
    cache.store(down);
    CHECK_EQ(cache.load("http://down/").has_value(), false);  // status 0 never stored

    std::ofstream(cache.path_for("http://forged/")) << "WC-CACHE 1\nurl: http://else/\nstatus: 200\n\n";
    CHECK_EQ(cache.load("http://forged/").has_value(), false);  // url mismatch = miss

    int calls = 0;
    bool net_up = true;
    HttpsFetcher f([&](const Url&) {
        ++calls;
        HttpResponse r;
        if (!net_up) {
            r.error = "connect: down";
            return r;
        }
        r.status = 200;
        r.headers["content-type"] = "text/html";
        r.body = "<a href=/p>";
        return r;
    });
    f.use_robots(false);
    f.use_cache(&cache);
    auto l = f.links("http://site/");
    CHECK_EQ(calls, 1);
    CHECK_EQ(f.last().from_cache, false);
    net_up = false;
    l = f.links("http://site/");
    CHECK_EQ(calls, 1);  // hit: no network
    CHECK_EQ(f.last().from_cache, true);
    CHECK_EQ(l ? join(*l) : std::string("<none>"), std::string("http://site/p\n"));

    f.use_cache(&cache, /*offline=*/true);
    CHECK_EQ(f.links("http://site/").has_value(), true);
    CHECK_EQ(f.links("http://never/").has_value(), false);
    CHECK_EQ(f.last().error, std::string("offline: not cached"));
    CHECK_EQ(calls, 1);

    net_up = true;
    f.use_cache(&cache, false, /*refresh=*/true);
    f.links("http://site/");
    CHECK_EQ(calls, 2);  // refresh always goes live
    CHECK_EQ(f.last().from_cache, false);

    fs::remove_all(dir);
}

struct RecordingSleeper : Sleeper {
    std::vector<int> ms;
    void sleep_ms(int t) override { ms.push_back(t); }
};

static std::string ints(const std::vector<int>& v) {
    std::string s;
    for (int x : v) s += std::to_string(x) + " ";
    return s;
}

static void test_robots_parse() {
    RobotsRules r = parse_robots(
        "Disallow: /before-any-agent\n"
        "User-agent: *\n"
        "Disallow: /private   # comment\n"
        "Allow: /private/ok\n"
        "Disallow: /*.pdf$\n"
        "disallow: /search\n"
        "Disallow: /*/edit\n"
        "Allow: /tie\n"
        "Disallow: /tie\n"
        "Crawl-delay: 2\n"
        "\n"
        "User-agent: Googlebot\n"
        "Disallow: /\n");
    CHECK_EQ(r.allowed("/"), true);
    CHECK_EQ(r.allowed("/before-any-agent"), true);
    CHECK_EQ(r.allowed("/private/x"), false);
    CHECK_EQ(r.allowed("/private/ok/1"), true);  // longer Allow wins
    CHECK_EQ(r.allowed("/doc.pdf"), false);
    CHECK_EQ(r.allowed("/doc.pdf?x=1"), true);   // '$' anchors the end
    CHECK_EQ(r.allowed("/search?q=a"), false);
    CHECK_EQ(r.allowed("/searching"), false);    // plain prefix match
    CHECK_EQ(r.allowed("/a/b/edit"), false);
    CHECK_EQ(r.allowed("/edit"), true);
    CHECK_EQ(r.allowed("/tie"), true);           // same length: Allow wins
    CHECK_EQ(r.crawl_delay_ms, 2000);

    r = parse_robots("User-agent: *\nDisallow: /\n\nUser-agent: CSCI26-WebCrawler/0.1\nDisallow:\n");
    CHECK_EQ(r.allowed("/x"), true);             // our own (empty) group beats "*"
    r = parse_robots("User-agent: a\nUser-agent: *\nDisallow: /x\nUser-agent: b\nDisallow: /y\n");
    CHECK_EQ(r.allowed("/x/1"), false);          // grouped agents share rules
    CHECK_EQ(r.allowed("/y"), true);
    CHECK_EQ(parse_robots("").allowed("/anything"), true);
    RobotsRules all;
    all.block_all = true;
    CHECK_EQ(all.allowed("/"), false);
}

static void test_polite_fetch() {
    std::map<std::string, std::vector<HttpResponse>> web;  // url -> responses, last one repeats
    auto add = [&](const std::string& url, int status, std::map<std::string, std::string> h, std::string body = "") {
        web[url].push_back(resp(status, std::move(h), std::move(body)));
    };
    const std::map<std::string, std::string> html = {{"content-type", "text/html"}};
    add("https://r.com/robots.txt", 200, {{"content-type", "text/plain"}}, "User-agent: *\nDisallow: /no\nCrawl-delay: 1\n");
    add("https://r.com/yes", 200, html, "<a href=/no>");
    add("http://n.com/robots.txt", 404, html);
    add("http://n.com/", 200, html, "<a href=/a>");
    add("http://five.com/robots.txt", 503, {});
    add("http://busy.com/robots.txt", 404, {});
    add("http://busy.com/", 429, {{"retry-after", "3"}});
    add("http://busy.com/", 200, html, "<a href=/b>");
    add("http://jam.com/robots.txt", 404, {});
    add("http://jam.com/", 429, {});
    add("http://down.com/robots.txt", 503, {});  // never reached
    add("http://js.com/robots.txt", 404, {});
    add("http://js.com/", 200, html, "<noscript>Please enable JavaScript.</noscript>");
    add("http://five.com/x", 200, html);

    std::vector<std::string> log;
    std::map<std::string, size_t> served;
    HttpsFetcher f([&](const Url& u) {
        std::string k = u.str();
        log.push_back(k);
        if (k.rfind("http://down.com", 0) == 0) {
            HttpResponse r;
            r.error = "dns: unknown host";
            return r;
        }
        auto it = web.find(k);
        if (it == web.end()) return resp(404, {});
        size_t i = std::min(served[k]++, it->second.size() - 1);
        return it->second[i];
    });
    RecordingSleeper sl;
    f.use_sleeper(&sl);

    CHECK_EQ(f.links("https://r.com/no").has_value(), false);
    CHECK_EQ(f.label_of("https://r.com/no"), std::string("ROBOTS"));
    CHECK_EQ(f.last().error, std::string("robots: disallowed by robots.txt"));
    CHECK_EQ(join(log), std::string("https://r.com/robots.txt\n"));  // the page itself never requested
    CHECK_EQ(f.links("https://r.com/yes").has_value(), true);
    CHECK_EQ(log.size(), size_t(2));                 // robots.txt fetched once per origin
    CHECK_EQ(ints(sl.ms), std::string("1000 "));     // Crawl-delay paces later r.com requests

    sl.ms.clear();
    CHECK_EQ(f.links("http://n.com/").has_value(), true);  // robots 404 = allow all
    CHECK_EQ(f.label_of("http://n.com/"), std::string("OK"));
    CHECK_EQ(f.links("http://five.com/x").has_value(), false);  // robots 5xx = disallow all
    CHECK_EQ(f.last().error, std::string("robots: robots.txt unreachable"));
    CHECK_EQ(f.links("http://down.com/").has_value(), false);  // robots dns error = page's own error
    CHECK_EQ(f.label_of("http://down.com/"), std::string("DNS"));
    CHECK_EQ(ints(sl.ms), std::string(""));

    CHECK_EQ(f.links("http://busy.com/").has_value(), true);  // 429 + Retry-After: 3 -> wait, retry
    CHECK_EQ(ints(sl.ms), std::string("3000 "));
    f.links("http://busy.com/again");
    CHECK_EQ(ints(sl.ms), std::string("3000 3000 "));  // host stays paced after a 429

    sl.ms.clear();
    CHECK_EQ(f.links("http://jam.com/").has_value(), false);  // 429 forever: 2 retries then give up
    CHECK_EQ(f.label_of("http://jam.com/"), std::string("HTTP_429"));
    CHECK_EQ(ints(sl.ms), std::string("1000 2000 "));

    CHECK_EQ(f.links("http://js.com/").has_value(), true);
    CHECK_EQ(f.label_of("http://js.com/"), std::string("JS_SHELL"));
    CHECK_EQ(f.label_of("http://never.com/"), std::string(""));

    // Redirects: every hop must pass the target's robots.txt (other host or same host).
    add("https://a.com/robots.txt", 404, {});
    add("https://a.com/go", 301, {{"location", "https://b.com/private"}});
    add("https://b.com/robots.txt", 200, {{"content-type", "text/plain"}}, "User-agent: *\nDisallow: /private\n");
    add("https://a.com/self", 302, {{"location", "/hidden"}});
    add("https://c.com/robots.txt", 200, {{"content-type", "text/plain"}}, "User-agent: *\nDisallow: /hidden\n");
    add("https://c.com/self", 302, {{"location", "/hidden"}});
    add("http://rr.com/robots.txt", 301, {{"location", "/robots-new"}});  // robots.txt itself redirects
    add("http://rr.com/robots-new", 200, {{"content-type", "text/plain"}}, "User-agent: *\nDisallow: /x\n");
    add("http://rr.com/y", 200, html, "<a href=/x>");
    log.clear();
    CHECK_EQ(f.links("https://a.com/go").has_value(), false);
    CHECK_EQ(f.label_of("https://a.com/go"), std::string("ROBOTS"));
    CHECK_EQ(f.last().final_url, std::string("https://b.com/private"));
    CHECK_EQ(join(log), std::string("https://a.com/robots.txt\nhttps://a.com/go\nhttps://b.com/robots.txt\n"));  // b.com/private never requested
    CHECK_EQ(f.links("https://c.com/self").has_value(), false);  // same-host hop into a disallowed path
    CHECK_EQ(f.label_of("https://c.com/self"), std::string("ROBOTS"));
    CHECK_EQ(f.links("http://rr.com/y").has_value(), true);      // no recursion; redirected robots.txt is used
    CHECK_EQ(f.links("http://rr.com/x").has_value(), false);
    CHECK_EQ(f.label_of("http://rr.com/x"), std::string("ROBOTS"));

    FetchResult r;
    r.error = "timeout: read";
    CHECK_EQ(status_label(r), std::string("TIMEOUT"));
    r.error = "not_html: image/png";
    CHECK_EQ(status_label(r), std::string("NOT_HTML"));
    r.error = "http: 503";
    r.status = 503;
    CHECK_EQ(status_label(r), std::string("HTTP_503"));
    r.error = "offline: not cached";
    CHECK_EQ(status_label(r), std::string("OFFLINE"));
}

// 16 threads on one fetcher (the TUI's parallel crawl): a paced host (Crawl-delay) gets
// one request at a time and exactly one pace sleep per request; an unpaced host runs in
// parallel; robots.txt is fetched once per origin.
static void test_parallel_fetch() {
    std::mutex mu;
    std::map<std::string, int> asked, in_flight, max_in_flight;
    HttpsFetcher f([&](const Url& u) {
        std::string k = u.str(), h = u.host;
        {
            std::lock_guard<std::mutex> lk(mu);
            ++asked[k];
            max_in_flight[h] = std::max(max_in_flight[h], ++in_flight[h]);
        }
        for (int i = 0; i < 40 && h == "u.com"; ++i) {  // wait (<= 200 ms) for a second request to overlap
            {
                std::lock_guard<std::mutex> lk(mu);
                if (max_in_flight[h] > 1) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            std::lock_guard<std::mutex> lk(mu);
            --in_flight[h];
        }
        if (u.path == "/robots.txt")
            return h == "p.com" ? resp(200, {{"content-type", "text/plain"}}, "User-agent: *\nCrawl-delay: 1\n") : resp(404, {});
        return resp(200, {{"content-type", "text/html"}}, "<a href=/x>");
    });
    struct LockedSleeper : Sleeper {
        std::mutex mu;
        std::vector<int> ms;
        void sleep_ms(int t) override {
            std::lock_guard<std::mutex> lk(mu);
            ms.push_back(t);
        }
    } sl;
    f.use_sleeper(&sl);
    std::atomic<int> ok{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 16; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < 5; ++i)
                for (const char* host : {"p.com", "u.com"})
                    ok += f.links("http://" + std::string(host) + "/" + std::to_string(t) + "-" + std::to_string(i)).has_value();
        });
    for (auto& t : ts) t.join();
    CHECK_EQ(ok.load(), 160);
    CHECK_EQ(asked["http://p.com/robots.txt"], 1);
    CHECK_EQ(asked["http://u.com/robots.txt"], 1);
    CHECK_EQ(max_in_flight["p.com"], 1);              // paced: one at a time
    CHECK_EQ(max_in_flight["u.com"] > 1, true);       // unpaced: parallel
    CHECK_EQ(sl.ms.size(), size_t(80));               // one pace sleep per p.com page, none for u.com
    CHECK_EQ(std::count(sl.ms.begin(), sl.ms.end(), 1000), 80L);
    CHECK_EQ(f.label_of("http://u.com/15-4"), std::string("OK"));
}

// Many threads storing / loading the cache at once: every load is whole, no .tmp left.
static void test_cache_parallel() {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("wc_net_test_par_" + std::to_string(getpid()));
    fs::remove_all(dir);
    PageCache cache(dir.string());
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 16; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < 30; ++i) {
                FetchResult r;
                r.url = "https://c.com/" + std::to_string(i % 3 == 0 ? 0 : t * 100 + i);  // page 0: shared by all
                r.final_url = r.url;
                r.status = 200;
                r.content_type = "text/html";
                r.body = std::string(1000 + i, 'x');
                cache.store(r);
                auto hit = cache.load(r.url);
                if (!hit || hit->body.size() < 1000 || hit->body.find_first_not_of('x') != std::string::npos) ++bad;
            }
        });
    for (auto& t : ts) t.join();
    CHECK_EQ(bad.load(), 0);
    int tmp = 0;
    for (const auto& e : fs::directory_iterator(dir)) tmp += e.path().extension() == ".tmp";
    CHECK_EQ(tmp, 0);
    fs::remove_all(dir);
}

// Fake gateway on 127.0.0.1: answers each connection with the next canned reply, keeps the raw requests.
struct FakeGateway {
    int fd = -1, port = 0;
    std::vector<std::string> replies, requests;
    std::thread th;
    explicit FakeGateway(std::vector<std::string> r) : replies(std::move(r)) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        socklen_t n = sizeof a;
        getsockname(fd, reinterpret_cast<sockaddr*>(&a), &n);
        port = ntohs(a.sin_port);
        listen(fd, 4);
        th = std::thread([this] {
            for (const auto& reply : replies) {
                int c = accept(fd, nullptr, nullptr);
                if (c < 0) return;
                std::string req;
                char buf[4096];
                while (true) {  // headers, then Content-Length bytes of body
                    size_t he = req.find("\r\n\r\n"), cl = req.find("Content-Length: ");
                    if (he != std::string::npos && cl != std::string::npos &&
                        req.size() >= he + 4 + std::stoul(req.substr(cl + 16)))
                        break;
                    long got = recv(c, buf, sizeof buf, 0);
                    if (got <= 0) break;
                    req.append(buf, size_t(got));
                }
                requests.push_back(req);
                send(c, reply.data(), reply.size(), MSG_NOSIGNAL);
                close(c);
            }
        });
    }
    const std::vector<std::string>& wait() {  // every reply sent: the requests are complete
        if (th.joinable()) th.join();
        return requests;
    }
    ~FakeGateway() {
        wait();
        close(fd);
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port) + "/api/bridges"; }
};

static std::string http_reply(int status, const std::string& json) {
    return "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\nContent-Length: " +
           std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" + json;
}

static void test_gateway() {
    CHECK_EQ(json_string(R"({"ok":true,"url":"http:\/\/a.org\/x \"q\"","sub":0})", "url"), std::string("http://a.org/x \"q\""));
    CHECK_EQ(json_string(R"({"ok":false, "error" : "assignment must be 1..99"})", "error"), std::string("assignment must be 1..99"));
    CHECK_EQ(json_string(R"({"ok":true})", "url"), std::string(""));
    std::string graph = R"({"visual":"GraphAdjacencyList","nodes":[],"links":[]})";
    HttpClient http;
    {
        FakeGateway gw({http_reply(200, R"({"ok":true,"url":"http://assignments.bridgesuncc.org/assignments/26/tony","assignment":26,"sub":3})"),
                        http_reply(400, R"({"ok":false,"error":"assignment must be 1..99"})"),
                        http_reply(502, "not json")});
        GatewayReply r = gateway_upload(http, gw.url(), "26", 3, graph, "tok");
        CHECK_EQ(r.error, std::string(""));
        CHECK_EQ(r.url, std::string("http://assignments.bridgesuncc.org/assignments/26/tony"));
        r = gateway_upload(http, gw.url(), "", 0, graph);
        CHECK_EQ(r.error, std::string("assignment must be 1..99"));
        CHECK_EQ(r.url, std::string(""));
        r = gateway_upload(http, gw.url(), "26", 0, graph);
        CHECK_EQ(r.error, std::string("gateway: HTTP 502"));
        const std::string& q = gw.wait().at(0);
        CHECK_EQ(q.rfind("POST /api/bridges?assignment=26&sub=3 HTTP/1.1\r\n", 0), size_t(0));
        CHECK_EQ(q.find("\r\nContent-Type: application/json\r\n") != std::string::npos, true);
        CHECK_EQ(q.find("\r\nContent-Length: " + std::to_string(graph.size()) + "\r\n") != std::string::npos, true);
        CHECK_EQ(q.find("\r\nX-Gateway-Token: tok\r\n") != std::string::npos, true);
        CHECK_EQ(q.substr(q.find("\r\n\r\n") + 4), graph);
        CHECK_EQ(gw.requests.at(1).find("X-Gateway-Token"), std::string::npos);
    }
    GatewayReply down = gateway_upload(http, "http://127.0.0.1:1/api", "26", 0, graph);  // nothing listens on port 1
    CHECK_EQ(down.error.rfind("connect:", 0), size_t(0));
}

int main() {
    test_parse();
    test_with_scheme();
    test_pick_start();
    test_resolve();
    test_links();
    test_http_parse();
    test_fetcher();
    test_router();
    test_cache();
    test_robots_parse();
    test_polite_fetch();
    test_parallel_fetch();
    test_cache_parallel();
    test_gateway();
    std::cout << (failures ? "FAILED " : "OK ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
