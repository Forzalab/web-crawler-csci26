// logan.h - Logan's part (#3): draw the crawl on BRIDGES.
// Logan: you only need to edit logan.cc. Leave this file as is so everything still builds.
#pragma once

#include <string>
#include <vector>
using namespace std;

// One web page the crawler found.
struct LoganNode {
    string url;
    int depth = 0;          // 0 = start page, 1 = one click away, 2 = two clicks away...
    bool failed = false;    // true = the page could not be downloaded
};

// One link between two pages. from/to are positions in the nodes vector.
struct LoganEdge {
    int from = 0;
    int to = 0;
    bool back_link = false; // true = `to` was already found before (draw it bold)
};

void logan_draw(const vector<LoganNode>& nodes, const vector<LoganEdge>& edges);
