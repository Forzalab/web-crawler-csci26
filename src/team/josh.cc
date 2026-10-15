#include "team/josh.h"

#include <iostream>
using namespace std;

// TODO(Josh): download the page at `url` and put its links in page.links.
//   Example: the page has <a href="https://x.com/about">  ->  push_back("https://x.com/about")
//   It worked -> set page.ok = true.  Anything went wrong -> leave page.ok = false and return.
//   Do not sleep here; the crawler already waits between pages.
JoshPage josh_get_links(string url) {
    JoshPage page;
    if (url == "") return page;  // nothing to download

    // your code here

    return page;
}
