// camera.h - zoom/pan on top of fit(). Pure.
#pragma once

#include "ui/layout.h"

namespace wc {

struct Camera {
    double zoom = 1;          // 1 = fit; >1 = closer
    double px = 0, py = 0;    // pan, world units, applied before zoom
};

// Compose: zoom about the canvas center (w x h sub-pixels), then pan.
View apply(const Camera& c, const View& fit, int w, int h);

// Exponential ease of cur toward target; dt in seconds, ~200ms feel.
Camera ease(const Camera& cur, const Camera& target, double dt_s);

}  // namespace wc
