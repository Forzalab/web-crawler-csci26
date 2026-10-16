#include "team/josh.h"

#include <iostream>
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

    // your code here

    return page;
}
