// anim.cc - see anim.h
#include "ui/anim.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace wc::anim {

double clamp01(double t) { return t != t ? 0.0 : std::clamp(t, 0.0, 1.0); }  // NaN -> 0
double ease_in(double t) { t = clamp01(t); return t * t; }
double ease_out(double t) { t = 1.0 - clamp01(t); return 1.0 - t * t; }
double smoothstep(double t) { t = clamp01(t); return t * t * (3.0 - 2.0 * t); }

double ramp(int64_t elapsed_ms, int64_t dur_ms) {
    if (dur_ms <= 0) return 1.0;
    return clamp01(double(elapsed_ms) / double(dur_ms));
}

Rgb mix(Rgb a, Rgb b, double t) {
    t = clamp01(t);
    auto lerp = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b)};
}

double warp(double wall, double total, double slow, double window) {
    if (total <= 0 || wall <= 0) return std::max(0.0, total <= 0 ? wall : 0.0);
    slow = std::clamp(slow, 0.01, 1.0);
    const double a = std::max(1e-6, std::min(window, 0.6 * total));  // keep a cruise phase
    // Content covered: slow*t + (top-slow)*A*S(t/A) with S(u) = u^3 - u^4/2
    // (the integral of smoothstep), and S(1) = 1/2. Solve warp(total) == total for top.
    const double top = (total - slow * a / 2) / (total - a / 2);
    if (wall <= a) {
        const double u = wall / a;
        return slow * wall + (top - slow) * a * (u * u * u - u * u * u * u / 2);
    }
    return slow * a + (top - slow) * a / 2 + top * (wall - a);
}

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace wc::anim
