// josh.cc - spec #1, written by Claude in Josh's slot. Josh's bcb2fbc (merged) fetched via our
// wc::HttpClient, which spec #1 forbids (S6: networking must be boost tcp stream + readlib), so
// his status / Content-Type checks live on below in this hand-rolled version.
// Kerney's demo shape: boost tcp::iostream + readlib readline(), 10 s timeout, <=5 redirects,
// chunked, HTML only. http:// only: https (or a redirect to it) -> ok false, RoutedFetcher
// then asks our TLS fetcher. Built without Boost: always ok false. No globals (thread safe).
// readlib: third_party/readlib/read.h, from https://github.com/ShakaUVM/read (GPL-3.0, see its LICENSE).
#include "team/josh.h"

#ifdef WC_HAVE_BOOST
#include <boost/beast/core/tcp_stream.hpp>

#include <cctype>
#include <chrono>
#include <exception>
#include <thread>

#include "../../third_party/readlib/read.h"
#include "net/html_links.h"  // extract_links (parsing only, no networking)
#include "net/url.h"         // parse_url, resolve_url
#endif

using namespace std;

#ifdef WC_HAVE_BOOST

namespace {

string lower(string s) {
    for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
    return s;
}

// readline() keeps the '\r' of "\r\n"; drop it.
string chomp(string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

}  // namespace

JoshPage josh_get_links(string url) {
    JoshPage page;
    if (url == "") return page;  // nothing to download
    try {
        auto u = wc::parse_url(url);
        int resets = 0;
        for (int hop = 0; u && u->scheme == "http" && hop <= 5; ++hop) {
            string host = u->host;  // [::1] -> ::1 for the resolver
            if (host.size() > 2 && host.front() == '[') host = host.substr(1, host.size() - 2);

            boost::asio::ip::tcp::iostream stream;
            stream.expires_after(chrono::seconds(10));  // a silent server must not hang the crawl
            stream.connect(host, to_string(u->port));
            if (!stream) return page;
            stream << "GET " << u->target() << " HTTP/1.1\r\nHost: " << u->host_port()
                   << "\r\nConnection: close\r\n\r\n" << flush;

            // Status line: "HTTP/1.1 200 OK"
            string status_line = chomp(readline(stream));
            if (!stream && stream.error() == boost::asio::error::connection_reset && resets < 3) {
                // Reset before any reply (busy server, full accept queue): GET is safe to resend.
                this_thread::sleep_for(chrono::milliseconds(50 << resets++));
                --hop;
                continue;
            }
            if (!stream || status_line.rfind("HTTP/", 0) != 0) return page;
            const int status = stoi(status_line.substr(status_line.find(' ') + 1));

            // Headers until the blank line. readline() skips blank lines, so peek for it.
            string location, type;
            bool chunked = false;
            while (stream && stream.peek() != '\r' && stream.peek() != '\n') {
                string line = chomp(readline(stream));
                size_t colon = line.find(':');
                if (colon == string::npos) continue;
                string name = lower(line.substr(0, colon));
                size_t v = line.find_first_not_of(' ', colon + 1);
                string value = v == string::npos ? "" : line.substr(v);
                if (name == "location") location = value;
                else if (name == "content-type") type = lower(value);
                else if (name == "transfer-encoding") chunked = lower(value).find("chunked") != string::npos;
            }
            if (!stream) return page;

            if (status >= 300 && status < 400) {  // follow Location (a redirect to https -> u fails the loop test)
                if (location.empty()) return page;
                u = wc::resolve_url(*u, location);
                continue;
            }
            if (status < 200 || status >= 300) return page;
            if (!type.empty() && type.find("html") == string::npos) return page;

            string body;
            if (chunked) {  // "<hex size>\r\n<data>\r\n" ... "0\r\n\r\n"
                while (true) {
                    string size_line = readline(stream);
                    if (!stream) return page;
                    size_t n = stoul(size_line, nullptr, 16);
                    if (n == 0) break;
                    string chunk(n, '\0');
                    if (!stream.read(chunk.data(), streamsize(n))) return page;
                    body += chunk;
                }
            } else {  // Connection: close, so the body ends at EOF
                while (true) {
                    string line = readline(stream);
                    if (!stream) break;
                    body += line + '\n';
                }
                if (stream.error() && stream.error() != boost::asio::error::eof) return page;  // timeout, reset
            }
            page.links = wc::extract_links(body, *u);
            page.ok = true;
            return page;
        }
    } catch (const exception&) {  // bad number in the reply, resolver error...
    }
    return page;
}

#else  // built without Boost (apt install libboost-dev): spec #1 fetcher not available

JoshPage josh_get_links(string) { return JoshPage(); }

#endif
