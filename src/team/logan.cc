#include "team/logan.h"

#include <iostream>
using namespace std;

// TODO(Logan): draw the graph on BRIDGES.
//   nodes.at(i) = one page. Color it by depth (0 red, 1 orange, 2 yellow, 3 green, ...).
//   edges.at(i) = one link: nodes.at(e.from) -> nodes.at(e.to). back_link == true -> draw it bold.
//
//   Input:   const vector<LoganNode>& nodes   (see logan.h)
//              string url       the page address
//              int    depth     0 = start page, 1 = one click away, ...
//              bool   failed    true = the page did not download
//            const vector<LoganEdge>& edges   (see logan.h)
//              int  from        position in nodes
//              int  to          position in nodes
//              bool back_link   true = `to` was found before
//   Output:  nothing (void). The picture on BRIDGES is the result.
//
//   Example:
//     nodes = { {"A", 0, false},  {"B", 1, false},  {"C", 1, true} }
//     edges = { {0, 1, false},  {0, 2, false},  {1, 0, true} }
//     Draw: A red. B orange. C orange (failed).
//           A -> B.  A -> C.  B -> A bold (back link).
void logan_draw(const vector<LoganNode>& nodes, const vector<LoganEdge>& edges) {
    cerr << "[logan_draw] not written yet: " << nodes.size() << " nodes, "
         << edges.size() << " edges" << endl;
}
