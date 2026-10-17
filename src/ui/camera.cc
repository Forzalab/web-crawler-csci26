// camera.cc - see camera.h
#include "ui/camera.h"

#include <cmath>

namespace wc {

View apply(const Camera& c, const View& fit, int w, int h) {
    // The world point fit puts at the canvas center stays there; pan shifts it.
    double cx = w / 2.0, cy = h / 2.0;
    View v;
    v.scale = fit.scale * c.zoom;
    v.ox = cx + (c.px * fit.scale + fit.ox - cx) * c.zoom;
    v.oy = cy + (c.py * fit.scale + fit.oy - cy) * c.zoom;
    return v;
}

Camera ease(const Camera& cur, const Camera& target, double dt_s) {
    double k = dt_s > 0 ? 1 - std::exp(-dt_s / 0.08) : 0;
    Camera r;
    r.zoom = cur.zoom + (target.zoom - cur.zoom) * k;
    r.px = cur.px + (target.px - cur.px) * k;
    r.py = cur.py + (target.py - cur.py) * k;
    return r;
}

}  // namespace wc
