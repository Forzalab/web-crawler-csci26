// report_main.cc - `wc --report URL`: one headless crawl -> a run dir for the report (D2/D3).
// Same fetch stack as the TUI (RoutedFetcher + PageCache + robots), but the spec path:
// sequential crawl() with a real 0.1 s sleep between requests. Cache on, so a rerun replays.
//   --bfs | --dfs   --depth N (3)   --size N (1000)   --out DIR (runs/<host>-<bfs|dfs>/)
//   --cache DIR (.wc-cache)   --offline (cache only)   --upload (needs WC_GATEWAY_URL)
//   --pdf-link: share summary.pdf for 24 h via the gateway (WC_PDF_URL, else WC_GATEWAY_URL)
// WC_FIXTURE=file: fake web instead of the network (tests/report/), no sleeps.
// Writes DIR/stats.json, DIR/failures.tsv, DIR/graph.json, DIR/summary.pdf (+ DIR/bridges_url.txt on --upload,
// DIR/pdf_url.txt on --pdf-link).
// stdout: one summary line (+ "PDF (24 h): <url>" on --pdf-link). stderr: one line per fetch.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <map>
#include <random>

#include "adapters/josh_fetcher.h"
#include "bridges/bridges_json.h"
#include "core/crawl.h"
#include "net/cache.h"
#include "net/gateway.h"
#include "net/https_fetcher.h"
#include "net/url.h"
#include "ui/fixture_source.h"
#include "ui/front_ends.h"
#include "ui/omni_query.h"
#include "ui/report_pdf.h"

namespace wc {

namespace {

struct Outcome {
    std::string url, label, error, kind;  // kind: "" = fine, else the failures.tsv label
    int status = 0;
    bool ok = false, tls = false, cached = false;  // tls: our fetcher answered (status + cache known)
};

// Failure / block kind of one fetch. bot-wall = a page that loaded but hides its links
// (403, a 202 with no links, JS_SHELL). "" = a normal page.
std::string kind_of(const Outcome& o, size_t links) {
    const std::string& l = o.label;
    if (l == "ROBOTS") return "robots";
    if (l == "HTTP_429") return "429";
    if (l == "REDIRECTS") return "redirect-loop";
    if (l == "TLS") return "tls";
    if (l == "TIMEOUT") return "timeout";
    if (l == "JS_SHELL" || l == "HTTP_403" || (o.ok && o.status == 202 && links == 0)) return "bot-wall";
    if (o.ok) return "";
    if (l == "HTTP_404" || l == "HTTP_410" || l == "DNS" || l == "CONNECT" || l == "FAILED") return "dead";
    return "other";
}

// Notes every fetch's outcome, prints one progress line per fetch to stderr.
class ReportTap : public LinkSource {
public:
    ReportTap(LinkSource& inner, const HttpsFetcher* tls) : inner_(inner), tls_(tls) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override {
        auto r = inner_.links(url);
        Outcome o;
        o.url = url;
        o.ok = r.has_value();
        if (tls_) {
            FetchResult last = tls_->last();
            o.tls = last.url == url;  // else Josh answered (http://)
            if (o.tls) {
                o.status = last.status;
                o.error = last.error;
                o.cached = last.from_cache;
                o.label = status_label(last, r ? long(r->size()) : -1);
            }
        }
        if (!o.tls) o.label = o.ok ? "OK" : "FAILED";
        o.kind = kind_of(o, r ? r->size() : 0);
        log.push_back(o);
        std::cerr << "[report " << log.size() << "] " << o.label << (o.tls ? o.cached ? " cache=hit" : " cache=miss" : "")
                  << " links=" << (r ? long(r->size()) : -1) << ' ' << url << '\n';
        return r;
    }
    std::vector<Outcome> log;  // fetch order

private:
    LinkSource& inner_;
    const HttpsFetcher* tls_;
};

std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out + '"';
}

std::string tsv(std::string s) {  // one field: no tabs / newlines
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

std::string utc(std::time_t t, const char* fmt) {
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

std::string slurp(const std::string& path) {  // "" if missing
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A live run's wall time + start, kept by --offline replays (the report wants the live numbers).
// From DIR/stats.json when it holds live numbers: written by a live run, or carried by an earlier replay.
struct LiveTimes {
    long wall_ms = -1;  // -1 = none
    std::time_t started = 0;
};
LiveTimes live_times(const std::string& stats_path) {
    const std::string s = slurp(stats_path);
    LiveTimes t;
    const bool replay = s.find("\"replay\": true") != std::string::npos;
    size_t w = s.find("\"wall_ms\": ");
    if (s.empty() || w == std::string::npos || (replay && json_string(s, "wall_source") != "live")) return t;
    std::tm tm{};
    if (!strptime(json_string(s, "started_at").c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm)) return t;
    t.started = timegm(&tm);
    t.wall_ms = std::atol(s.c_str() + w + 11);
    return t;
}

}  // namespace

int report_main(int argc, char** argv) {
    std::string url, out, cache_dir = ".wc-cache";
    CrawlConfig cfg;
    bool upload = false, offline = false, pdf_link = false;
    for (int i = 1; i < argc; ++i) {
        bool has_arg = i + 1 < argc;
        if (!std::strcmp(argv[i], "--report") && has_arg) url = argv[++i];
        else if (!std::strcmp(argv[i], "--bfs")) cfg.mode = Mode::BFS;
        else if (!std::strcmp(argv[i], "--dfs")) cfg.mode = Mode::DFS;
        else if (!std::strcmp(argv[i], "--depth") && has_arg) cfg.maxDepth = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--size") && has_arg) cfg.maxSize = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && has_arg) out = argv[++i];
        else if (!std::strcmp(argv[i], "--cache") && has_arg) cache_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--upload")) upload = true;
        else if (!std::strcmp(argv[i], "--offline")) offline = true;
        else if (!std::strcmp(argv[i], "--pdf-link")) pdf_link = true;
    }
    if (url.empty() || cfg.maxDepth < 0 || cfg.maxSize < 1) {
        std::cerr << "usage: wc --report URL [--bfs|--dfs] [--depth 3] [--size 1000] [--out DIR] [--cache DIR] [--offline] [--upload] [--pdf-link]\n";
        return 2;
    }
    const std::string mode = cfg.mode == Mode::BFS ? "bfs" : "dfs";
    if (out.empty()) {
        auto u = parse_url(url);
        out = "runs/" + (u && !u->host.empty() ? u->host : std::string("site")) + "-" + mode;
    }
    const char* gateway = std::getenv("WC_GATEWAY_URL");
    if (upload && !(gateway && *gateway)) {
        std::cerr << "--upload needs WC_GATEWAY_URL\n";
        return 2;
    }
    const std::string pdf_ep = pdf_endpoint(std::getenv("WC_PDF_URL"), gateway);
    if (pdf_link && pdf_ep.empty()) {
        std::cerr << "--pdf-link needs WC_PDF_URL, or WC_GATEWAY_URL ending in /api/bridges\n";
        return 2;
    }
    const char* token = std::getenv("WC_GATEWAY_TOKEN");

    JoshFetcher josh;
    HttpsFetcher tls;
    PageCache cache(cache_dir);
    tls.use_cache(&cache, offline, false);
    tls.use_robots(true);
    RoutedFetcher routed(josh, tls);
    auto fixture = fixture_source_from_env();
    LinkSource& inner = fixture ? *fixture : offline ? static_cast<LinkSource&>(tls) : routed;  // offline: never Josh's network
    ReportTap tap(inner, fixture ? nullptr : &tls);
    RealSleeper real;
    NoSleeper none;
    Sleeper& sleeper = fixture ? static_cast<Sleeper&>(none) : real;

    const LiveTimes live = offline ? live_times(out + "/stats.json") : LiveTimes{};  // before this run rewrites it
    std::time_t started = std::time(nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    CrawlGraph g = crawl(url, cfg, tap, sleeper);
    long wall_ms = long(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    const bool carried = live.wall_ms >= 0;  // replay over a live run: report the live time + start
    if (carried) wall_ms = live.wall_ms, started = live.started;
    const std::string started_at = utc(started, "%Y-%m-%dT%H:%M:%SZ");

    int failed = 0, hits = 0, misses = 0;
    std::map<std::string, int> blocked_by{{"robots", 0}, {"429", 0}, {"bot-wall", 0}};
    for (const Fetch& f : g.fetches) failed += !f.ok;
    for (const Outcome& o : tap.log) {
        if (o.tls) (o.cached ? hits : misses)++;
        if (auto it = blocked_by.find(o.kind); it != blocked_by.end()) ++it->second;
    }
    int blocked = 0;
    for (const auto& [k, n] : blocked_by) blocked += n;
    std::map<int, int> hist;
    for (const Node& n : g.nodes) ++hist[n.depth];

    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    if (ec) {
        std::cerr << "cannot create " << out << ": " << ec.message() << '\n';
        return 1;
    }

    std::ofstream s(out + "/stats.json");
    s << "{\n  \"url\": " << json_str(url) << ",\n  \"mode\": \"" << to_string(cfg.mode) << "\",\n  \"depth\": " << cfg.maxDepth
      << ",\n  \"size\": " << cfg.maxSize << ",\n  \"nodes\": " << g.nodes.size() << ",\n  \"edges\": " << g.edges.size()
      << ",\n  \"fetches\": " << g.fetches.size() << ",\n  \"failed\": " << failed << ",\n  \"blocked\": " << blocked
      << ",\n  \"blocked_by\": {";
    const char* sep = "";
    for (const auto& [k, n] : blocked_by) s << sep << json_str(k) << ": " << n, sep = ", ";
    s << "},\n  \"depth_histogram\": {";
    sep = "";
    for (const auto& [d, n] : hist) s << sep << '"' << d << "\": " << n, sep = ", ";
    s << "},\n  \"wall_ms\": " << wall_ms << ",\n  \"cache\": {\"hits\": " << hits << ", \"misses\": " << misses
      << "},\n  \"started_at\": \"" << started_at << "\",\n  \"replay\": " << (offline ? "true" : "false")
      << ",\n  \"wall_source\": \"" << (offline && !carried ? "replay" : "live") << "\"\n}\n";

    std::ofstream f(out + "/failures.tsv");
    f << "url\tstatus\terror\tlabel\n";
    for (const Outcome& o : tap.log)
        if (!o.kind.empty()) f << tsv(o.url) << '\t' << o.status << '\t' << tsv(o.error.empty() ? o.label : o.error) << '\t' << o.kind << '\n';

    const std::string json = bridges_json(bridges_subset(g));
    std::ofstream(out + "/graph.json") << json << '\n';

    std::string link;
    if (upload) {
        HttpOptions opt;
        opt.timeout_ms = 20000;  // the gateway waits up to 15 s for BRIDGES
        HttpClient http(opt);
        const char* sub = std::getenv("WC_BRIDGES_SUB");
        const std::string a = session_assignment(std::getenv("BRIDGES_ASSIGNMENT"), std::random_device{}());
        GatewayReply r = gateway_upload(http, gateway, a, sub ? std::atoi(sub) : 0, json, token ? token : "");
        if (!r.error.empty()) {
            std::cerr << "BRIDGES: " << r.error << '\n';
        } else {
            link = r.url;
            std::ofstream(out + "/bridges_url.txt") << link << '\n';
        }
    }

    ReportSummary sum;
    auto u = parse_url(url);
    sum.host = u && !u->host.empty() ? u->host : url;
    sum.mode = to_string(cfg.mode), sum.url = url;
    sum.depth = cfg.maxDepth, sum.size = cfg.maxSize, sum.nodes = int(g.nodes.size()), sum.edges = int(g.edges.size());
    sum.fetches = int(g.fetches.size()), sum.failed = failed, sum.blocked = blocked;
    sum.robots = blocked_by["robots"], sum.too_many = blocked_by["429"], sum.bot_wall = blocked_by["bot-wall"];
    sum.cache_hits = hits, sum.cache_misses = misses, sum.wall_ms = wall_ms;
    if (carried) sum.wall_note = "(live run, " + utc(started, "%Y-%m-%d %H:%M UTC") + ")";
    else if (offline) sum.wall_note = "(cache replay, not live)";
    sum.bridges_jpg = slurp(out + "/bridges.jpg"), sum.app_jpg = slurp(out + "/app.jpg");  // scripts/report_shots.sh
    const std::string pdf = summary_pdf(sum);
    std::ofstream(out + "/summary.pdf", std::ios::binary) << pdf;
    std::string pdf_url;
    if (pdf_link) {
        HttpOptions opt;
        opt.timeout_ms = 20000;
        HttpClient http(opt);
        GatewayReply r = pdf_upload(http, pdf_ep, pdf, sum.host + "-" + mode, token ? token : "");
        if (!r.error.empty()) {
            std::cerr << "PDF link: " << r.error << " (local copy: " << out << "/summary.pdf)\n";
        } else {
            pdf_url = r.url;
            std::ofstream(out + "/pdf_url.txt") << pdf_url << '\n';
        }
    }

    std::cout << "report " << to_string(cfg.mode) << ' ' << url << " depth=" << cfg.maxDepth << " size=" << cfg.maxSize
              << " nodes=" << g.nodes.size() << " edges=" << g.edges.size() << " fetches=" << g.fetches.size()
              << " failed=" << failed << " blocked=" << blocked << " cache=" << hits << '/' << misses << " wall_ms=" << wall_ms
              << " -> " << out << (link.empty() ? "" : " bridges=" + link) << '\n';
    if (!pdf_url.empty()) std::cout << "PDF (24 h): " << pdf_url << '\n';  // own line: easy to tap / copy
    return (upload && link.empty()) || (pdf_link && pdf_url.empty()) ? 1 : 0;
}

}  // namespace wc
