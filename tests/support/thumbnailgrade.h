// THE THUMBNAIL GRADE ON PAPER (SRGB-ENCODE-1): what secondaryfx::apply's fixed
// exposure, the shipped film curve and the sRGB display encode make of a flat
// LINEAR radiance — the code a tile's flat backdrop must read. One text for the
// preview suites that check a tile is a picture and not the linear instrument.
#pragma once

#include "irisgl/document/scenegraph/cameralens.h"

#include <algorithm>
#include <cmath>

namespace thumbgrade {

inline double oetf(double v)
{
    v = std::min(std::max(v, 0.0), 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

/// The display code a flat radiance `linear` develops to at `stops` of
/// document exposure (the chain's fixed form: exp(E - 2) / 0.18, the grey card).
/// The display code of channel `ch` for a LINEAR colour at `stops` (the shader's
/// whole chain: the fixed exposure, the film on the colour, the encode).
inline double codeRGB(const double linear[3], int ch, float stops)
{
    const double e = double(iris::lens::exposureStopsToChain(stops));
    const double m = std::exp(e - 2.0) / 0.18;
    const double x[3] = { linear[0] * m, linear[1] * m, linear[2] * m };
    double o[3];
    iris::lens::filmRGB(x, o);   // the one C++ transcription of the shader's film
    return 255.0 * oetf(o[ch]);
}

/// THE TILE'S CHANNEL `ch` against the instrument's colour (8-bit linear codes),
/// a half code either way of each instrument channel.
inline bool matchesRGB(int got, const int instrument[3], int ch, float stops, double *lo = nullptr,
                       double *hi = nullptr)
{
    double a = 1e9, b = -1e9;
    for (int k = 0; k < 8; ++k) {
        double lin[3];
        for (int c = 0; c < 3; ++c)
            lin[c] = std::max(0.0, instrument[c] + ((k >> c) & 1 ? 0.5 : -0.5)) / 255.0;
        const double v = codeRGB(lin, ch, stops);
        a = std::min(a, v); b = std::max(b, v);
    }
    if (lo) *lo = a;
    if (hi) *hi = b;
    return got >= a - 1.0 && got <= b + 1.0;
}


}   // namespace thumbgrade
