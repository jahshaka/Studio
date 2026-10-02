// THE ONE FALLOFF OF A POINT OR SPOT LIGHT, in C++ (IMAGE-1) — the closed form the
// suites hold the picture to. The renderer's own statement is the fork's JahBrdf
// piece (`jahLightAttenuation`), which every shader consumer calls:
//
//     saturate(1 - (d / range)^4)^2 / max(d^2, sourceRadius^2)
//
// (Karis 2013, "Real Shading in Unreal Engine 4", eq. 9; the source clamp is
// Frostbite's). It lives in tests/support because nothing in the engine or the
// host computes a light's falloff on the CPU.
#pragma once

inline double lightFalloff(double d, double range, double sourceRadius) {
    const double x = range > 0.0 ? d / range : 1.0;
    double w = 1.0 - x * x * x * x;
    w = w < 0.0 ? 0.0 : (w > 1.0 ? 1.0 : w);
    const double r2 = sourceRadius * sourceRadius;
    const double d2 = d * d;
    return (w * w) / (d2 > r2 ? d2 : r2);
}
