#include "logan.h"
#include "Bridges.h"
#include "GraphAdjList.h"
#include "Color.h"
#include <iostream>
using namespace std;
using namespace bridges;

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
	string username = "";//insert you username here
	string API_key = "";//insert your API key
	bridges::GraphAdjList<int, LoganNode, LoganEdge> graph;
	for(int i = 0; i < nodes.size(); i++) {
		graph.addNode(i, nodes.at(i));
		if(nodes.at(i).depth == 0) {
			graph.getNode(i)->getVisualizer()->setColor(Color("red"));
		}
		else if(nodes.at(i).depth == 1) {
			graph.getNode(i)->getVisualizer()->setColor(Color("orange"));
		}
		else if(nodes.at(i).depth == 2) {
			graph.getNode(i)->getVisualizer()->setColor(Color("yellow"));
		}
		else if(nodes.at(i).depth == 3) {
			graph.getNode(i)->getVisualizer()->setColor(Color("green"));
		}
		else {
			graph.getNode(i)->getVisualizer()->setColor(Color("blue"));
		}
		if(nodes.at(i).failed) {
			graph.getNode(i)->getVisualizer()->setShape(Shape::TRIANGLE);
		}
		graph.getNode(i)->setLabel(nodes.at(i).url);
	}

	for(const auto& edge : edges) {
		graph.addEdge(edge.from, edge.to, edge);
		if(edge.back_link) {
		graph.addEdge(edge.from, edge.to, edge)->getVisualizer()->setThickness(5.0F);
		}
	}
	Bridges bridges(6742069, username, API_key);
	bridges.setDataStructure(&graph);
	bridges.visualize();

}
