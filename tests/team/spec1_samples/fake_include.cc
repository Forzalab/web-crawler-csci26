// spec1 sample (text fixture for tests/team/spec1_selftest.sh, not built):
// the spec #1 pieces appear only in comments and strings; the real work is HttpClient.
// Expected: S1-S5 FAIL, S6 FAIL, S7 WARN, S8 PASS.
#include "team/josh.h"

// #include <boost/beast/core/tcp_stream.hpp>
// #include "/public/read.h"
/*
#include <boost/asio.hpp>
boost::asio::ip::tcp::iostream stream(hostname, "http");
string line = readline(stream);
stream.expires_after(chrono::seconds(10));
*/
#include "net/html_links.h"
#include "net/http.h"
#include "net/url.h"
using namespace std;

const char* kUses = "#include <boost/beast/core/tcp_stream.hpp> tcp::iostream s; readline(s);";
const char* kInc = "#include \"/public/read.h\"";

JoshPage josh_get_links(string url) {
    JoshPage page;
    auto u = wc::parse_url(url);
    if (!u) return page;
    // print(stream, "GET {} HTTP/1.1\r\n", path);
    wc::HttpClient client;
    wc::HttpResponse r = client.get(*u);
    if (!r.error.empty() || r.status != 200) return page;
    page.links = wc::extract_links(r.body, *u);
    page.ok = true;
    return page;
}
