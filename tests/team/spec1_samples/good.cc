// spec1 sample (text fixture for tests/team/spec1_selftest.sh, not built):
// minimal josh_get_links() in the professor's demo style, with a timeout.
// Expected: S1-S8 all PASS.
#include "team/josh.h"

#include <boost/beast/core/tcp_stream.hpp>
#include <chrono>
#include <print>

#include "/public/read.h"
#include "net/html_links.h"
#include "net/url.h"
using namespace std;

JoshPage josh_get_links(string url) {
    JoshPage page;
    auto u = wc::parse_url(url);
    if (!u || u->scheme != "http") return page;  // plain http only, like the demo

    boost::asio::ip::tcp::iostream stream;
    stream.expires_after(chrono::seconds(10));
    stream.connect(u->host, "http");
    if (!stream) return page;

    print(stream, "GET {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n", u->path, u->host);

    string status = readline(stream);  // HTTP/1.1 200 OK
    if (status.find(" 200") == string::npos) return page;
    while (stream) {                    // headers end at an empty line
        string line = readline(stream);
        if (line.empty() || line == "\r") break;
    }
    string body;
    while (stream) body += readline(stream) + "\n";

    page.links = wc::extract_links(body, *u);
    page.ok = true;
    return page;
}
