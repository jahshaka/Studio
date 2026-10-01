// THE THUMBNAIL GRADE ON PAPER (SRGB-ENCODE-1): what secondaryfx::apply's fixed
// exposure, the shipped film curve and the sRGB display encode make of a flat
// LINEAR radiance — the code a tile's flat backdrop must read. One text for the
// preview suites that check a tile is a picture and not the linear instrument.
#pragma once

#include "irisgl/document/scenegraph/cameralens.h"

#include <algorithm>
#include <cmath>

namespace thumbgrade {

inline double film(double x)
{
    const double A = 0.22, B = 0.3, C = 0.10, D = 0.20, E = 0.01, F = 0.30, W = 11.2;
    const auto h = [&](double v) { return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F; };
    return (h(x) / h(W) - 0.5) * 1.25 + 0.5 + 0.11;
}

inline double oetf(double v)
{
    v = std::min(std::max(v, 0.0), 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

/// The display code a flat radiance `linear` develops to at `stops` of
/// document exposure (the chain's fixed form: exp(E - 2) / 0.18, the grey card).
inline double code(double linear, float stops)
{
    const double e = double(iris::lens::exposureStopsToChain(stops));
    return 255.0 * oetf(film(linear * std::exp(e - 2.0) / 0.18));
}

}   // namespace thumbgrade
