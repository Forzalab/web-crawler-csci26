// Color.h - our BRIDGES shim (src/team/bridges/): same names as bridges-cxx, none of its code.
// Color("red") or Color(r, g, b[, a]); unknown names fall back to steelblue (BRIDGES default).
#pragma once

#include <string>

namespace bridges {
namespace datastructure {

// Node shapes, BRIDGES spelling in shape_name().
enum Shape { CIRCLE, SQUARE, DIAMOND, CROSS, TRIANGLE, STAR, WYE };

inline const char* shape_name(Shape s) {
    static const char* const names[] = {"circle", "square", "diamond", "cross", "triangle", "star", "wye"};
    return names[s];
}

class Color {
public:
    Color() : Color(70, 130, 180) {}
    Color(int r, int g, int b, int a = 255) : r_(r), g_(g), b_(b), a_(a) {}
    Color(const std::string& name) : Color() {
        struct Named { const char* name; int r, g, b; };
        static const Named table[] = {
            {"red", 255, 0, 0},      {"orange", 255, 165, 0}, {"yellow", 255, 255, 0}, {"green", 0, 128, 0},
            {"blue", 0, 0, 255},     {"indigo", 75, 0, 130},  {"violet", 238, 130, 238}, {"purple", 128, 0, 128},
            {"black", 0, 0, 0},      {"white", 255, 255, 255}, {"grey", 128, 128, 128}, {"gray", 128, 128, 128},
            {"steelblue", 70, 130, 180},
        };
        for (const Named& n : table)
            if (name == n.name) r_ = n.r, g_ = n.g, b_ = n.b;
    }
    int getRed() const { return r_; }
    int getGreen() const { return g_; }
    int getBlue() const { return b_; }
    int getAlpha() const { return a_; }

private:
    int r_, g_, b_, a_;
};

}  // namespace datastructure
}  // namespace bridges
