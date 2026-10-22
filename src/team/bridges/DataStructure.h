// DataStructure.h - our BRIDGES shim: what Bridges::setDataStructure() takes.
#pragma once

#include <string>

#include "Color.h"

namespace bridges {
namespace datastructure {

class DataStructure {
public:
    virtual ~DataStructure() = default;
    // The "nodes" and "links" members of the BRIDGES JSON, no braces: "nodes":[...],"links":[...]
    virtual std::string nodes_links_json() const = 0;
};

// JSON bits (Bridges.cc): a string literal, a number ("10" -> "10.0"), a colour [r,g,b,a].
std::string json_quote(const std::string& s);
std::string json_num(double x);
std::string json_color(const Color& c);

}  // namespace datastructure
}  // namespace bridges
