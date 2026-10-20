// stream_fetch.cc - spec #1 fetcher: tcp::iostream + readlib readline() (see stream_fetch.h).
// Networking is only Boost's tcp::iostream; HTML link parsing and URL resolving reuse ours.
// readlib: third_party/readlib/read.h, from https://github.com/ShakaUVM/read (GPL-3.0, see its LICENSE).
#include "net/stream_fetch.h"

#ifdef WC_HAVE_BOOST
#include <boost/beast/core/tcp_stream.hpp>

#include <cctype>
#include <chrono>
#include <exception>
#include <thread>

#include "../../third_party/readlib/read.h"
#include "net/html_links.h"
#include "net/url.h"
#endif

namespace wc {

#ifdef WC_HAVE_BOOST

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// readline() keeps the '\r' of "\r\n"; drop it.
std::string chomp(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

}  // namespace

bool stream_fetch_built() { return true; }

std::vector<std::string> stream_get_links(const std::string& url, bool* ok) {
    *ok = false;
    try {
        auto u = parse_url(url);
        int resets = 0;
        for (int hop = 0; u && u->scheme == "http" && hop <= 5; ++hop) {
            std::string host = u->host;  // [::1] -> ::1 for the resolver
            if (host.size() > 2 && host.front() == '[') host = host.substr(1, host.size() - 2);

            boost::asio::ip::tcp::iostream stream;
            stream.expires_after(std::chrono::seconds(10));  // a silent server must not hang the crawl
            stream.connect(host, std::to_string(u->port));
            if (!stream) return {};
            stream << "GET " << u->target() << " HTTP/1.1\r\nHost: " << u->host_port()
                   << "\r\nConnection: close\r\n\r\n" << std::flush;

            // Status line: "HTTP/1.1 200 OK"
            std::string status_line = chomp(readline(stream));
            if (!stream && stream.error() == boost::asio::error::connection_reset && resets < 3) {
                // Reset before any reply (busy server, full accept queue): GET is safe to resend.
                std::this_thread::sleep_for(std::chrono::milliseconds(50 << resets++));
                --hop;
                continue;
            }
            if (!stream || status_line.rfind("HTTP/", 0) != 0) return {};
            const int status = std::stoi(status_line.substr(status_line.find(' ') + 1));

            // Headers until the blank line. readline() skips blank lines, so peek for it.
            std::string location, type;
            bool chunked = false;
            while (stream && stream.peek() != '\r' && stream.peek() != '\n') {
                std::string line = chomp(readline(stream));
                size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string name = lower(line.substr(0, colon));
                size_t v = line.find_first_not_of(' ', colon + 1);
                std::string value = v == std::string::npos ? "" : line.substr(v);
                if (name == "location") location = value;
                else if (name == "content-type") type = lower(value);
                else if (name == "transfer-encoding") chunked = lower(value).find("chunked") != std::string::npos;
            }
            if (!stream) return {};

            if (status >= 300 && status < 400) {  // follow Location (a redirect to https -> u fails the loop test)
                if (location.empty()) return {};
                u = resolve_url(*u, location);
                continue;
            }
            if (status < 200 || status >= 300) return {};
            if (!type.empty() && type.find("html") == std::string::npos) return {};

            std::string body;
            if (chunked) {  // "<hex size>\r\n<data>\r\n" ... "0\r\n\r\n"
                while (true) {
                    std::string size_line = readline(stream);
                    if (!stream) return {};
                    size_t n = std::stoul(size_line, nullptr, 16);
                    if (n == 0) break;
                    std::string chunk(n, '\0');
                    if (!stream.read(chunk.data(), std::streamsize(n))) return {};
                    body += chunk;
                }
            } else {  // Connection: close, so the body ends at EOF
                while (true) {
                    std::string line = readline(stream);
                    if (!stream) break;
                    body += line + '\n';
                }
                if (stream.error() && stream.error() != boost::asio::error::eof) return {};  // timeout, reset
            }
            *ok = true;
            return extract_links(body, *u);
        }
    } catch (const std::exception&) {  // bad number in the reply, resolver error...
    }
    return {};
}

#else  // built without Boost: spec #1 fetcher not available

bool stream_fetch_built() { return false; }

std::vector<std::string> stream_get_links(const std::string&, bool* ok) {
    *ok = false;
    return {};
}

#endif

std::optional<std::vector<std::string>> StreamFetcher::links(const std::string& url) {
    bool ok = false;
    auto l = stream_get_links(url, &ok);
    if (!ok) return std::nullopt;
    std::lock_guard<std::mutex> lock(mu_);
    last_ok_ = url;
    return l;
}

std::string StreamFetcher::last_ok() const {
    std::lock_guard<std::mutex> lock(mu_);
    return last_ok_;
}

}  // namespace wc
