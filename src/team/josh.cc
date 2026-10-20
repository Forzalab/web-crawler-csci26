#include "team/josh.h"

#include <cctype>
#include <iostream>

#include "net/html_links.h"
#include "net/http.h"
#include "net/url.h"
using namespace std;

// TODO(Josh): download the page at `url` and put its links in page.links.
//   Example: the page has <a href="https://x.com/about">  ->  push_back("https://x.com/about")
//   It worked -> set page.ok = true.  Anything went wrong -> leave page.ok = false and return.
//   Do not sleep here; the crawler already waits between pages.
//
//   Input:   string url               one full web address
//   Output:  JoshPage  (see josh.h)
//              bool           ok      true = page loaded, false = it failed
//              vector<string> links   every link on the page, top to bottom
//
//   Example 1 (page loads):
//     url  = "https://x.com"
//     page = { ok = true,  links = { "https://x.com/about", "https://y.org" } }
//   Example 2 (page loads, has no links):
//     page = { ok = true,  links = { } }
//   Example 3 (404, timeout, not HTML):
//     page = { ok = false, links = { } }
JoshPage josh_get_links(string url) {
    JoshPage page;
    if (url == "") return page;  // nothing to download

    auto u = wc::parse_url(url);
    if (!u) return page;  // not a valid http(s) address

    wc::HttpClient client;
    wc::HttpResponse r = client.get(*u);
    if (!r.error.empty()) return page;                 // dns / connect / tls / timeout / io ...
    if (r.status < 200 || r.status > 299) return page;  // 404, 301 redirect, 500 ...

    // Only HTML pages count (a missing Content-Type is treated as HTML).
    auto ct = r.headers.find("content-type");
    if (ct != r.headers.end()) {
        string type = ct->second;
        for (char& c : type) c = (char)tolower((unsigned char)c);
        if (type.find("html") == string::npos) return page;
    }

    page.links = wc::extract_links(r.body, *u);
    page.ok = true;
    return page;
}
