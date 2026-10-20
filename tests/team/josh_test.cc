// josh_test.cc - 12 checks for josh_get_links() (src/team/josh.h).
// Run: make josh   (or build/josh_test [path/to/josh_fixture.py])
//
// Every check runs twice: once on Josh's josh_get_links(), once on our own fetcher
// (wc::HttpsFetcher, what `wc --links` uses; robots off, no cache) with the same
// expectations. Cases 1-9 use a local fake web (tests/team/josh_fixture.py, python3,
// 127.0.0.1). Case 10 is LIVE (real sites); it is skipped when offline or when
// JOSH_TEST_OFFLINE=1. Cases 11-12 run in a child process with a deadline, so a hang
// or a crash shows up as FAIL instead of freezing or killing this program.
// Exit code 0 = every non-skipped josh check passed.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "net/https_fetcher.h"
#include "team/josh.h"

#ifndef JOSH_FIXTURE
#define JOSH_FIXTURE "tests/team/josh_fixture.py"
#endif

namespace {

struct Got {
    bool ok = false;
    std::vector<std::string> links;
    std::string threw;  // non-empty = the call threw
    double secs = 0;
};

using Subject = std::function<JoshPage(const std::string&)>;

std::string base;  // http://127.0.0.1:PORT

constexpr double kSlow = 5.0;  // one page taking longer than this = would stall the crawl

Got call(const Subject& s, const std::string& url) {
    Got g;
    auto t0 = std::chrono::steady_clock::now();
    try {
        JoshPage p = s(url);
        g.ok = p.ok;
        g.links = p.links;
    } catch (const std::exception& e) {
        g.threw = e.what();
    } catch (...) {
        g.threw = "unknown exception";
    }
    g.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return g;
}

std::string show(const std::vector<std::string>& v, size_t max = 4) {
    std::string s = "{";
    for (size_t i = 0; i < v.size() && i < max; ++i) s += (i ? ", " : "") + v[i];
    if (v.size() > max) s += ", ... (" + std::to_string(v.size()) + " total)";
    return s + "}";
}

std::string secs(double d) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f s", d);
    return b;
}

// Common failure text for "the call itself went wrong" (threw / too slow), else "".
std::string broke(const std::string& url, const Got& g) {
    if (!g.threw.empty()) return url + " threw (" + g.threw + "), the crawler expects ok=false, not an exception";
    if (g.secs > kSlow) return url + " took " + secs(g.secs) + ", one page must not stall the crawl";
    return "";
}

// Page must load with exactly these links, in this order.
std::string want_links(const Subject& s, const std::string& url, const std::vector<std::string>& want,
                       const std::string& what) {
    Got g = call(s, url);
    if (auto b = broke(url, g); !b.empty()) return b;
    if (!g.ok) return url + " " + what + ", got ok=false, crawler expected ok=true with " + show(want);
    if (g.links != want) return url + " " + what + ", got " + show(g.links) + ", expected " + show(want);
    return "";
}

std::string want_fail(const Subject& s, const std::string& url, const std::string& what) {
    Got g = call(s, url);
    if (auto b = broke(url, g); !b.empty()) return b;
    if (g.ok)
        return url + " " + what + ", got ok=true with " + std::to_string(g.links.size()) +
               " links, crawler expected ok=false";
    return "";
}

// ---- the 12 cases. "" = PASS, "SKIP ..." = skipped, anything else = FAIL reason ----

std::string c1_basic(const Subject& s) {
    return want_links(s, base + "/basic", {"http://a.example/one", "https://b.example/two", "http://a.example/one"},
                      "plain page with 3 links (one repeated)");
}

std::string c2_relative(const Subject& s) {
    return want_links(s, base + "/dir/sub/rel",
                      {base + "/dir/sub/page2", base + "/dir/up", base + "/root", base + "/dir/sub/rel?q=1",
                       "http://other.example/p"},
                      "relative hrefs (page2, ../up, /root, ?q=1, //host)");
}

std::string c3_nolinks(const Subject& s) {
    return want_links(s, base + "/nolinks", {}, "loaded page with no links");
}

std::string c4_http_errors(const Subject& s) {
    if (auto r = want_fail(s, base + "/missing", "answered 404"); !r.empty()) return r;
    return want_fail(s, base + "/broken", "answered 500");
}

std::string c5_not_html(const Subject& s) {
    if (auto r = want_fail(s, base + "/image.png", "is image/png"); !r.empty()) return r;
    return want_fail(s, base + "/doc.pdf", "is application/pdf");
}

std::string c6_charset_chunked_big(const Subject& s) {
    if (auto r = want_links(s, base + "/charset-chunked", {base + "/c1", base + "/c2"},
                            "is text/html; charset=UTF-8, chunked");
        !r.empty())
        return r;
    std::string url = base + "/big";
    Got g = call(s, url);
    if (auto b = broke(url, g); !b.empty()) return b;
    if (!g.ok) return url + " 1 MB page with 20000 links, got ok=false, crawler expected all of them";
    if (g.links.size() != 20000 || g.links.front() != base + "/big/0" || g.links.back() != base + "/big/19999")
        return url + " 1 MB page with 20000 links, got " + std::to_string(g.links.size()) + " links " + show(g.links);
    return "";
}

std::string c7_redirect_308(const Subject& s) {
    return want_links(s, base + "/old/308", {base + "/new/next", "https://c.example/x"},
                      "answered 308 (like http://wowcubedocs.com/), links must come from the final page");
}

std::string c8_redirect_chain(const Subject& s) {
    if (auto r = want_links(s, base + "/chain/a", {base + "/chain/done"}, "answered 301 then 302");
        !r.empty())
        return r;
    return want_fail(s, base + "/loop/a", "redirects in a loop forever");
}

std::string c9_dead_ends(const Subject& s) {
    // a port nobody listens on
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    ::bind(fd, (sockaddr*)&a, sizeof a);
    ::getsockname(fd, (sockaddr*)&a, &len);
    std::string closed = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port)) + "/";
    ::close(fd);
    for (auto& [url, what] : std::vector<std::pair<std::string, std::string>>{
             {closed, "has no server (connection refused)"},
             {"not a url", "is not a URL"},
             {"ftp://example.com/file", "is not http/https"},
             {"http://", "has no host"},
             {"", "is empty"}}) {
        std::string shown = url.empty() ? "\"\"" : url;
        Got g = call(s, url);
        if (auto b = broke(shown, g); !b.empty()) return b;
        if (g.ok) return shown + " " + what + ", got ok=true, crawler expected ok=false";
    }
    return "";
}

bool online() {
    if (const char* e = std::getenv("JOSH_TEST_OFFLINE"); e && *e && std::string(e) != "0") return false;
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("en.wikipedia.org", "443", &hints, &res) != 0) return false;
    int fd = ::socket(res->ai_family, SOCK_STREAM, 0);
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    bool ok = ::connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    ::close(fd);
    freeaddrinfo(res);
    return ok;
}

std::string c10_live(const Subject& s) {
    static const bool on = online();
    if (!on) return "SKIP no direct internet (or JOSH_TEST_OFFLINE=1)";
    struct L {
        std::string url;
        size_t min;
        std::string what;
    };
    for (const L& l : {L{"https://en.wikipedia.org/wiki/Wars_of_the_Roses", 100, "https demo start page"},
                       L{"http://wowcubedocs.com/", 1, "answers 308 to https"}}) {
        Got g = call(s, l.url);
        if (!g.threw.empty()) return l.url + " threw (" + g.threw + ")";
        if (g.secs > 20) return l.url + " took " + secs(g.secs) + ", the demo would sit there";
        if (!g.ok) return l.url + " (" + l.what + ") got ok=false, crawler expected its links";
        if (g.links.size() < l.min)
            return l.url + " (" + l.what + ") got " + std::to_string(g.links.size()) + " links, expected at least " +
                   std::to_string(l.min);
    }
    return "";
}

// Runs fn in a child process. Child crashed or not back within `secs` -> FAIL text.
std::string isolated(const std::function<std::string()>& fn, int secs_limit, const std::string& what) {
    int p[2];
    if (pipe(p) != 0) return "could not create a pipe";
    std::fflush(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        std::string r = fn();
        (void)!write(p[1], r.data(), r.size());
        _exit(0);
    }
    close(p[1]);
    std::string out;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(secs_limit);
    bool hung = false;
    for (;;) {
        int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
        if (left <= 0) { hung = true; break; }
        pollfd pf{p[0], POLLIN, 0};
        if (poll(&pf, 1, left) <= 0) continue;
        char buf[512];
        ssize_t n = read(p[0], buf, sizeof buf);
        if (n <= 0) break;  // child closed the pipe: done (or died)
        out.append(buf, size_t(n));
    }
    close(p[0]);
    if (hung) kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
    if (hung) return what + ": not back after " + std::to_string(secs_limit) + " s, the crawl would hang here";
    if (WIFSIGNALED(st)) return what + ": crashed (signal " + std::to_string(WTERMSIG(st)) + "), the whole crawler would die";
    return out;
}

std::string c11_silent_server(const Subject& s) {
    // Accepts the connection (kernel backlog) but never answers a single byte.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    ::bind(fd, (sockaddr*)&a, sizeof a);
    ::listen(fd, 16);
    ::getsockname(fd, (sockaddr*)&a, &len);
    std::string url = "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port)) + "/";
    std::string r = isolated([&] {
        Got g = call(s, url);
        if (!g.threw.empty()) return url + " threw (" + g.threw + "), the crawler expects ok=false, not an exception";
        if (g.ok) return url + " never answered, got ok=true, crawler expected ok=false";
        return std::string();
    }, 15, url + " connects but never answers");
    ::close(fd);
    return r;
}

std::string c12_parallel(const Subject& s) {
    // The TUI crawls with 16 threads at once, all calling the same function.
    const std::vector<std::string> urls = {base + "/basic", base + "/dir/sub/rel", base + "/charset-chunked",
                                           base + "/chain/end"};
    std::vector<Got> ref;
    for (const auto& u : urls) ref.push_back(call(s, u));
    return isolated([&] {
        constexpr int kThreads = 16, kRounds = 4;
        std::vector<std::string> bad(kThreads);
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t)
            ts.emplace_back([&, t] {
                for (int k = 0; k < kRounds && bad[size_t(t)].empty(); ++k) {
                    size_t i = size_t(t + k) % urls.size();
                    Got g = call(s, urls[i]);
                    if (!g.threw.empty()) bad[size_t(t)] = urls[i] + " threw (" + g.threw + ") with 16 threads at once";
                    else if (g.ok != ref[i].ok || g.links != ref[i].links)
                        bad[size_t(t)] = urls[i] + " with 16 threads at once gave " + (g.ok ? show(g.links) : "ok=false") +
                                         ", alone it gave " + (ref[i].ok ? show(ref[i].links) : "ok=false");
                }
            });
        for (auto& t : ts) t.join();
        for (const auto& b : bad)
            if (!b.empty()) return b;
        return std::string();
    }, 30, "16 threads calling it at once");
}

// ---- fixture server ----

pid_t start_fixture(const std::string& script) {
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        execlp("python3", "python3", script.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(p[1]);
    std::string line;
    char c;
    while (read(p[0], &c, 1) == 1 && c != '\n') line += c;
    close(p[0]);
    if (line.empty() || line.find_first_not_of("0123456789") != std::string::npos) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        return -1;
    }
    base = "http://127.0.0.1:" + line;
    return pid;
}

std::string pad(std::string s, size_t n) {
    if (s.size() < n) s.append(n - s.size(), ' ');
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    std::string script = argc > 1 ? argv[1] : JOSH_FIXTURE;
    pid_t fixture = start_fixture(script);
    if (fixture < 0) {
        std::cerr << "could not start the fake web server (needs python3): " << script << "\n";
        return 2;
    }

    wc::HttpsFetcher ours_fetcher;  // what `wc --links` uses, minus robots.txt and cache
    ours_fetcher.use_robots(false);
    Subject josh = [](const std::string& u) { return josh_get_links(u); };
    Subject ours = [&](const std::string& u) {
        JoshPage p;
        if (auto l = ours_fetcher.links(u)) {
            p.ok = true;
            p.links = *l;
        }
        return p;
    };

    struct Case {
        const char* name;
        std::string (*fn)(const Subject&);
    };
    const Case cases[] = {
        {"links in page order", c1_basic},
        {"relative links", c2_relative},
        {"page with 0 links", c3_nolinks},
        {"404 / 500", c4_http_errors},
        {"image / pdf", c5_not_html},
        {"charset, chunked, big", c6_charset_chunked_big},
        {"308 redirect", c7_redirect_308},
        {"301->302 chain, loop", c8_redirect_chain},
        {"dead or bad URLs", c9_dead_ends},
        {"LIVE wikipedia, wowcube", c10_live},
        {"silent server (no hang)", c11_silent_server},
        {"16 threads at once", c12_parallel},
    };

    std::cout << "josh_get_links() vs our fetcher, fake web at " << base << "\n\n";
    std::cout << pad("case", 30) << pad("josh", 6) << "ours\n";
    int josh_fail = 0, ours_fail = 0, josh_pass = 0, ours_pass = 0;
    auto word = [](const std::string& r) { return r.empty() ? "PASS" : r.rfind("SKIP", 0) == 0 ? "SKIP" : "FAIL"; };
    int n = 0;
    for (const Case& c : cases) {
        ++n;
        std::string rj = c.fn(josh);
        std::string ro = c.fn(ours);
        std::string w1 = word(rj), w2 = word(ro);
        std::cout << pad((n < 10 ? " " : "") + std::to_string(n) + " " + c.name, 30) << pad(w1, 6) << w2 << "\n";
        if (w1 == "FAIL") std::cout << "     FAIL " << n << " josh: " << rj << "\n";
        if (w1 == "SKIP") std::cout << "     " << rj << "\n";
        if (w2 == "FAIL") std::cout << "     FAIL " << n << " ours: " << ro << "\n";
        josh_fail += w1 == "FAIL";
        josh_pass += w1 == "PASS";
        ours_fail += w2 == "FAIL";
        ours_pass += w2 == "PASS";
    }
    std::cout << "\njosh: " << josh_pass << " PASS, " << josh_fail << " FAIL   ours: " << ours_pass << " PASS, "
              << ours_fail << " FAIL\n";

    kill(fixture, SIGTERM);
    waitpid(fixture, nullptr, 0);
    return josh_fail == 0 ? 0 : 1;
}
