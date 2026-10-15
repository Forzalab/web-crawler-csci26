// josh.h - Josh's part (#1): download one web page and list its links.
// Josh: you only need to edit josh.cc. Leave this file as is so everything still builds.
#pragma once

#include <string>
#include <vector>
using namespace std;

// What josh_get_links() gives back.
struct JoshPage {
    bool ok = false;        // true = the page loaded. false = it failed (404, timeout, not HTML...)
    vector<string> links;   // every link on the page, top to bottom (repeats are fine)
};

JoshPage josh_get_links(string url);
