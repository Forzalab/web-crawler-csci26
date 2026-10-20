// spec1 sample (text fixture for tests/team/spec1_selftest.sh, not built):
// same idea as good.cc, written with aliases: namespace alias, a type alias for the
// stream, the request built in a variable first, read<string>() and expires_from_now.
// Expected: S1-S8 all PASS.
#include "team/josh.h"

#include <boost/asio.hpp>
#include <chrono>
#include <read.h>
#include <sstream>

#include "net/html_links.h"
#include "net/url.h"
using namespace std;
namespace net = boost::asio;
using Sock = net::ip::tcp::iostream;

JoshPage josh_get_links(string url) {
    JoshPage page;
    auto u = wc::parse_url(url);
    if (!u) return page;
    Sock s;
    s.expires_from_now(chrono::seconds(10));
    s.connect(u->host, "80");
    string req = "GET " + u->path + " HTTP/1.0\r\nHost: " + u->host + "\r\n\r\n";
    s << req << flush;
    string status = readline(s);
    if (status.find(" 200") == string::npos) return page;
    string body, word;
    while (s) {
        word = read<string>(s);
        body += word + " ";
    }
    for (const string& l : wc::extract_links(body, *u)) page.links.push_back(l);
    page.ok = true;
    return page;
}
