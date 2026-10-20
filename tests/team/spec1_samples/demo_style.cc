// spec1 sample (text fixture for tests/team/spec1_selftest.sh, not built):
// the professor's demo turned into josh_get_links(), no timeout.
// Expected: S1-S6 and S8 PASS, S7 WARN.
#include "team/josh.h"

#include <boost/beast/core/tcp_stream.hpp>
#include <print>

#include "/public/read.h"
#include "net/html_links.h"
#include "net/url.h"
using namespace std;

JoshPage josh_get_links(string url) {
    JoshPage page;
    auto u = wc::parse_url(url);
    if (!u) return page;
    string hostname = u->host, path = u->path;

    boost::asio::ip::tcp::iostream stream(hostname, "http");
    print(stream, "GET {} HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n", path, hostname);
    string body;
    while (stream) {
        string line = readline(stream);
        body += line + "\n";
    }
    if (body.find(" 200") == string::npos) return page;

    page.links = wc::extract_links(body, *u);
    page.ok = true;
    return page;
}
