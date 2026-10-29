#pragma once
// ONE CODE AT A LEVEL (gi.photon_window, PHOTON-7 10-4; shared since REDS-1 by gi.screen_integrate): the display's 8-bit
// sRGB step at linear radiance L - the WIDTH of the code L falls in (the encode's own quantum there; never the distance
// to the next code's edge, which shrinks to 0 just below it) - what a user can see change at that level, derived from
// the encoding, never picked.
#include <algorithm>
#include <cmath>

namespace enginetest {
inline double srgbEncode(double L) { return L <= 0.0031308 ? 12.92 * L : 1.055 * std::pow(L, 1.0 / 2.4) - 0.055; }
inline double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
inline double oneCodeAt(double L)
{
    const double code = std::min(std::floor(srgbEncode(std::max(L, 0.0)) * 255.0), 254.0);
    return srgbDecode((code + 1.0) / 255.0) - srgbDecode(code / 255.0);
}
}   // namespace enginetest
