// anim.h - the one easing / fade / time-warp helper shared by the loading
// checklist (fades), the matrix rain (intensity ramp) and the replay clock
// (slow start, then faster). Pure: no FTXUI, no state; t is clamped to [0,1].
// --no-anim / NO_COLOR: callers pass t = 1 (or skip the call) -> static look.
#pragma once

#include <cstdint>

namespace wc::anim {

double clamp01(double t);
double ease_in(double t);     // t^2: slow start
double ease_out(double t);    // 1-(1-t)^2: fast start, soft landing (fades)
double smoothstep(double t);  // 3t^2-2t^3: soft at both ends (intensity ramps)

// Linear progress of something that started `elapsed_ms` ago and lasts `dur_ms`.
// dur <= 0 -> 1 (instant). Negative elapsed -> 0.
double ramp(int64_t elapsed_ms, int64_t dur_ms);

// Colour ramp, plain RGB so it stays testable without FTXUI (views: Color::RGB(c.r, c.g, c.b)).
struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};
Rgb mix(Rgb from, Rgb to, double t);  // t=0 -> from, t=1 -> to (exact endpoints)

// Accelerating time warp: wall time -> content time. Speed starts at `slow`
// (e.g. 0.3x), climbs smoothly (smoothstep) over the first `window_ms` of wall
// time, then holds a top speed chosen so that warp(total_ms) == total_ms:
// the whole thing still ends on time. Monotonic, warp(0) = 0, continuous speed.
// Past total_ms it keeps the top speed. total_ms <= 0 -> identity.
double warp(double wall_ms, double total_ms, double slow = 0.3, double window_ms = 4000);

// Steady-clock milliseconds: the one clock fades are measured on.
int64_t now_ms();

}  // namespace wc::anim
