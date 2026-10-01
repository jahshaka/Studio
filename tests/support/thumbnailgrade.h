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
/// THE FILM ON A COLOUR (IMAGE-1): Unreal's FilmToneMap as the shader runs it —
/// sRGB -> ACEScg, the RRT's glow and red modifier, the pre/post desaturation and
/// the curve per channel — so a saturated subject (a red cube, a red avatar) is
/// held to the shader's arithmetic, not to the grey curve applied channel-wise.
inline void filmRGB(const double in[3], double out[3])
{
    static const double S2A[9] = { 0.6131486203, 0.3394883591, 0.0473630206, 0.0702074147, 0.9163424763,
                                   0.0134501090, 0.0206231422, 0.1095899890, 0.8697868689 };
    static const double A2S[9] = { 1.7050473375, -0.6217891459, -0.0832581917, -0.1302575067, 1.1408060644,
                                   -0.0105485577, -0.0240032831, -0.1289688126, 1.1529720957 };
    static const double A1T0[9] = { 0.6954522414, 0.1406786965, 0.1638690622, 0.0447945634, 0.8596711185,
                                    0.0955343182, -0.0055258826, 0.0040252103, 1.0015006723 };
    static const double A0T1[9] = { 1.4514393161, -0.2365107469, -0.2149285693, -0.0765537734, 1.1762296998,
                                    -0.0996759264, 0.0083161484, -0.0060324498, 0.9977163014 };
    static const double Y[3] = { 0.2722287168, 0.6740817658, 0.0536895174 };
    const auto mul = [](const double m[9], const double v[3], double o[3]) {
        for (int r = 0; r < 3; ++r) o[r] = m[r * 3] * v[0] + m[r * 3 + 1] * v[1] + m[r * 3 + 2] * v[2];
    };
    double ap1[3], c0[3];
    mul(S2A, in, ap1);
    mul(A1T0, ap1, c0);
    const double mi = std::min({ c0[0], c0[1], c0[2] }), ma = std::max({ c0[0], c0[1], c0[2] });
    const double sat = (std::max(ma, 1e-10) - std::max(mi, 1e-10)) / std::max(ma, 1e-2);
    const double chroma = std::sqrt(std::max(0.0, c0[2] * (c0[2] - c0[1]) + c0[1] * (c0[1] - c0[0]) +
                                                     c0[0] * (c0[0] - c0[2])));
    const double yc = (c0[0] + c0[1] + c0[2] + 1.75 * chroma) / 3.0;
    const double sx = (sat - 0.4) / 0.2;
    const double tt = std::max(1.0 - std::fabs(0.5 * sx), 0.0);
    const double s = 0.5 * (1.0 + (sx > 0 ? 1.0 : sx < 0 ? -1.0 : 0.0) * (1.0 - tt * tt));
    const double gIn = 0.05 * s, mid = 0.08;
    const double glow = yc <= 2.0 / 3.0 * mid ? gIn : yc >= 2.0 * mid ? 0.0 : gIn * (mid / yc - 0.5);
    for (double &v : c0) v *= 1.0 + glow;
    double hue = 0.0;
    if (!(c0[0] == c0[1] && c0[1] == c0[2])) {
        hue = 57.2957795131 * std::atan2(1.7320508076 * (c0[1] - c0[2]), 2.0 * c0[0] - c0[1] - c0[2]);
        if (hue < 0.0) hue += 360.0;
    }
    const double ch = hue > 180.0 ? hue - 360.0 : hue;
    double hw = std::min(std::max(1.0 - std::fabs(2.0 * ch / 135.0), 0.0), 1.0);
    hw = hw * hw * (3.0 - 2.0 * hw);
    hw *= hw;
    c0[0] += hw * sat * (0.03 - c0[0]) * (1.0 - 0.82);
    double w[3];
    mul(A0T1, c0, w);
    for (double &v : w) v = std::max(v, 0.0);
    double l = w[0] * Y[0] + w[1] * Y[1] + w[2] * Y[2];
    for (double &v : w) v = l + (v - l) * 0.96;
    double t[3];
    for (int k = 0; k < 3; ++k) t[k] = double(iris::lens::filmCurve(float(w[k])));
    l = t[0] * Y[0] + t[1] * Y[1] + t[2] * Y[2];
    for (double &v : t) v = std::max(l + (v - l) * 0.93, 0.0);
    mul(A2S, t, out);
    for (int k = 0; k < 3; ++k) out[k] = std::max(out[k], 0.0);
}

/// The display code of channel `ch` for a LINEAR colour at `stops` (the shader's
/// whole chain: the fixed exposure, the film on the colour, the encode).
inline double codeRGB(const double linear[3], int ch, float stops)
{
    const double e = double(iris::lens::exposureStopsToChain(stops));
    const double m = std::exp(e - 2.0) / 0.18;
    const double x[3] = { linear[0] * m, linear[1] * m, linear[2] * m };
    double o[3];
    filmRGB(x, o);
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
