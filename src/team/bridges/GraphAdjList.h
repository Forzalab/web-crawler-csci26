// GraphAdjList.h - our BRIDGES shim: GraphAdjList<K, E1, E2>, same names as bridges-cxx, none of its code.
// Node ids in the JSON = order of addVertex(); links keep the order of their first addEdge().
// addEdge() returns the edge (bridges-cxx returns void), so addEdge(a, b)->getVisualizer() works.
// The same edge again = still one edge: new data, same visualizer (the last style set wins).
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Color.h"
#include "DataStructure.h"

namespace bridges {
namespace datastructure {

class ElementVisualizer {
public:
    void setColor(const Color& c) { color_ = c; }
    void setShape(Shape s) { shape_ = s; }
    void setSize(double s) { size_ = s; }
    Color getColor() const { return color_; }
    Shape getShape() const { return shape_; }
    double getSize() const { return size_; }

private:
    Color color_;
    Shape shape_ = CIRCLE;
    double size_ = 10.0;
};

class LinkVisualizer {
public:
    void setColor(const Color& c) { color_ = c; }
    void setThickness(double t) { thickness_ = t; }
    Color getColor() const { return color_; }
    double getThickness() const { return thickness_; }

private:
    Color color_;
    double thickness_ = 1.0;
};

template <typename E>
class Element {
public:
    explicit Element(const E& v = E()) : value_(v) {}
    ElementVisualizer* getVisualizer() { return &vis_; }
    const ElementVisualizer* getVisualizer() const { return &vis_; }
    void setLabel(const std::string& l) { label_ = l; }
    const std::string& getLabel() const { return label_; }
    E& getValue() { return value_; }
    void setValue(const E& v) { value_ = v; }

private:
    E value_;
    std::string label_;
    ElementVisualizer vis_;
};

template <typename K, typename E2>
class Edge {
public:
    Edge(const K& from, const K& to, const E2& v) : from_(from), to_(to), value_(v) {}
    LinkVisualizer* getVisualizer() { return &vis_; }
    const LinkVisualizer* getVisualizer() const { return &vis_; }
    const K& getFrom() const { return from_; }
    const K& getTo() const { return to_; }
    E2& getEdgeData() { return value_; }
    void setEdgeData(const E2& v) { value_ = v; }

private:
    K from_, to_;
    E2 value_;
    LinkVisualizer vis_;
};

template <typename K, typename E1 = K, typename E2 = E1>
class GraphAdjList : public DataStructure {
public:
    // Same key again: new data, label and style kept.
    void addVertex(const K& k, const E1& e = E1()) {
        auto [it, added] = vertices_.try_emplace(k, e);
        if (added) order_.push_back(k);
        else it->second.setValue(e);
    }
    void addNode(const K& k, const E1& e = E1()) { addVertex(k, e); }

    Element<E1>* getVertex(const K& k) {
        auto it = vertices_.find(k);
        return it == vertices_.end() ? nullptr : &it->second;
    }
    Element<E1>* getNode(const K& k) { return getVertex(k); }

    Edge<K, E2>* addEdge(const K& from, const K& to, const E2& e = E2()) {
        auto [it, added] = edges_.try_emplace(std::make_pair(from, to), from, to, e);
        if (added) edge_order_.push_back(it->first);
        else it->second.setEdgeData(e);
        return &it->second;
    }
    LinkVisualizer* getLinkVisualizer(const K& from, const K& to) {
        auto it = edges_.find(std::make_pair(from, to));
        return it == edges_.end() ? nullptr : it->second.getVisualizer();
    }

    // Links whose ends are not vertices are left out.
    std::string nodes_links_json() const override {
        std::map<K, size_t> id;
        std::string out = "\"nodes\":[";
        for (size_t i = 0; i < order_.size(); ++i) {
            const Element<E1>& v = vertices_.at(order_[i]);
            const ElementVisualizer& vis = *v.getVisualizer();
            id[order_[i]] = i;
            if (i) out += ",";
            out += "{\"color\":" + json_color(vis.getColor()) + ",\"shape\":\"" + shape_name(vis.getShape()) +
                   "\",\"size\":" + json_num(vis.getSize()) + ",\"name\":" + json_quote(v.getLabel()) + "}";
        }
        out += "],\"links\":[";
        const char* sep = "";
        for (const auto& key : edge_order_) {
            auto s = id.find(key.first), t = id.find(key.second);
            if (s == id.end() || t == id.end()) continue;
            const LinkVisualizer& vis = *edges_.at(key).getVisualizer();
            out += sep;
            sep = ",";
            out += "{\"color\":" + json_color(vis.getColor()) + ",\"thickness\":" + json_num(vis.getThickness()) +
                   ",\"source\":\"" + std::to_string(s->second) + "\",\"target\":\"" + std::to_string(t->second) + "\"}";
        }
        return out + "]";
    }

private:
    std::map<K, Element<E1>> vertices_;  // std::map: element pointers stay valid
    std::vector<K> order_;
    std::map<std::pair<K, K>, Edge<K, E2>> edges_;
    std::vector<std::pair<K, K>> edge_order_;
};

}  // namespace datastructure
}  // namespace bridges
