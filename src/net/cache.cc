#include "net/cache.h"

#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>

namespace wc {

namespace {

// Stable across compilers and runs (std::hash is not).
std::string fnv1a_hex(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string now_utc() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Header values are one line each: no newlines allowed in.
std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

}  // namespace

PageCache::PageCache(std::string dir, std::string source) : dir_(std::move(dir) + "/" + std::move(source)) {}

std::string PageCache::path_for(const std::string& url) const { return dir_ + "/" + fnv1a_hex(url) + ".page"; }

std::optional<FetchResult> PageCache::load(const std::string& url) const {
    std::ifstream in(path_for(url), std::ios::binary);
    std::string line;
    if (!in || !std::getline(in, line) || line != "WC-CACHE 1") return std::nullopt;
    std::map<std::string, std::string> h;
    while (std::getline(in, line) && !line.empty()) {
        size_t c = line.find(':');
        if (c == std::string::npos) return std::nullopt;
        std::string v = line.substr(c + 1);
        if (!v.empty() && v[0] == ' ') v.erase(0, 1);
        h[line.substr(0, c)] = v;
    }
    if (h["url"] != url) return std::nullopt;  // hash collision or foreign file
    FetchResult r;
    try {
        r.url = url;
        r.final_url = h["final_url"];
        r.status = std::stoi(h["status"]);
        r.redirects = std::stoi(h["redirects"]);
        r.content_type = h["content_type"];
        r.error = h["error"];
        r.fetched_at = h["fetched_at"];
        size_t n = std::stoull(h["bytes"]);
        r.body.resize(n);
        if (n && !in.read(r.body.data(), static_cast<std::streamsize>(n))) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    r.from_cache = true;
    return r;
}

void PageCache::store(FetchResult& r) const {
    if (r.status == 0) return;  // dns/connect/timeout...: worth retrying next run
    if (r.fetched_at.empty()) r.fetched_at = now_utc();
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::string ct;
    for (char c : r.content_type) ct += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool keep = r.error.empty() && (ct.empty() || ct.find("html") != std::string::npos || ct.find("text/plain") != std::string::npos);
    const std::string& body = keep ? r.body : std::string();
    static std::atomic<unsigned> seq{0};  // unique tmp per store: threads / processes never share one
    std::string path = path_for(r.url), tmp = path + "." + std::to_string(getpid()) + "-" + std::to_string(seq++) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << "WC-CACHE 1\n"
            << "url: " << one_line(r.url) << "\n"
            << "final_url: " << one_line(r.final_url) << "\n"
            << "status: " << r.status << "\n"
            << "redirects: " << r.redirects << "\n"
            << "content_type: " << one_line(r.content_type) << "\n"
            << "error: " << one_line(r.error) << "\n"
            << "fetched_at: " << r.fetched_at << "\n"
            << "bytes: " << body.size() << "\n\n"
            << body;
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    std::filesystem::rename(tmp, path, ec);  // atomic: readers never see half a file
    if (ec) std::filesystem::remove(tmp, ec);
}

}  // namespace wc
