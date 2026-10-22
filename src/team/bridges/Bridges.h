// Bridges.h - our BRIDGES shim (src/team/bridges/): Logan's logan.cc builds against this,
// not bridges-cxx (no license; we never ship it). Same names, none of its code.
// visualize() posts through our gateway/ (it holds the BRIDGES keys), so user + key are ignored:
//   WC_GATEWAY_URL (needed), WC_GATEWAY_TOKEN, WC_BRIDGES_SUB (0); assignment = the ctor's number.
//   OK -> stdout "BRIDGES (logan): <url>". Else one line on stderr, no throw.
#pragma once

#include <string>

#include "Color.h"
#include "DataStructure.h"

namespace bridges {
using namespace bridges::datastructure;  // so `using namespace bridges;` sees Color, Shape, ...

class Bridges {
public:
    Bridges(int assignment, const std::string& user, const std::string& key);
    void setDataStructure(DataStructure* ds) { ds_ = ds; }
    // The whole "GraphAdjacencyList" JSON (same header keys as src/bridges/bridges_json.cc).
    std::string to_json() const;
    void visualize();

private:
    int assignment_;
    DataStructure* ds_ = nullptr;
    std::string title_ = "Web crawl (Logan)";
};

}  // namespace bridges
