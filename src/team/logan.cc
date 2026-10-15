#include "team/logan.h"

#include <iostream>
using namespace std;

// TODO(Logan): draw the graph on BRIDGES.
//   nodes.at(i) = one page. Color it by depth (0 red, 1 orange, 2 yellow, 3 green, ...).
//   edges.at(i) = one link: nodes.at(e.from) -> nodes.at(e.to). back_link == true -> draw it bold.
void logan_draw(const vector<LoganNode>& nodes, const vector<LoganEdge>& edges) {
    cerr << "[logan_draw] not written yet: " << nodes.size() << " nodes, "
         << edges.size() << " edges" << endl;
}
