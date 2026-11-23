// core.colour_qt_parity — iris::SrgbColor against QColor (Rgb spec), BIT FOR BIT (NOQT-DOCUMENT-1 stage 3, ruling 3).
//
// The document's colours left QColor for iris::SrgbColor; a saved scene's bytes may not move (ruling 2), so the new
// type must store, reduce and answer EXACTLY what QColor did. QColor is linked here only as the REFERENCE (as
// math.parity links it for the math types). What the type carries, each compared over its whole domain:
//   * the 8-bit constructor (every value 0..255, and the out-of-range ones -> invalid) - its 16-bit store;
//   * every 16-bit channel value (65,536): the 8-bit reduction red()..alpha(), redF()..alphaF() bit for bit, and the
//     editor's edge both ways (iris::toQColor / iris::toSrgbColor) - the same QColor back;
//   * 100,000 seeded floats in 0..1 (plus 0 and 1): fromRgbF's 16-bit store and its F accessors;
//   * the invalid state: what it reads (alpha 1, colour 0) and that it round-trips;
//   * linearOf: the decode of the 8-bit value the picker showed (core/color.h's table) - equal on both.
// name() and the "#rrggbb" parse are NOT carried by the type (no caller in the document, the mirror or the importer:
// the lead's ruling 2026-11-23): they stay QColor's, at the editor's edge.
#include <QColor>
#include <QRgba64>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

#include "irisgl/document/srgbcolor.h"
#include "irisgl/document/qt/colour.h"

static int failures = 0;
static long compared = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

/// Everything the type answers, against QColor's answer. Reports the first difference of each kind once.
static bool same(const iris::SrgbColor &s, const QColor &q)
{
    const QRgba64 v = q.rgba64();
    if (s.isValid() != q.isValid()) return false;
    if (s.isValid() && (s.red16() != v.red() || s.green16() != v.green() || s.blue16() != v.blue() ||
                        s.alpha16() != v.alpha()))
        return false;
    if (s.red() != q.red() || s.green() != q.green() || s.blue() != q.blue() || s.alpha() != q.alpha()) return false;
    if (!sameBits(s.redF(), q.redF()) || !sameBits(s.greenF(), q.greenF()) || !sameBits(s.blueF(), q.blueF()) ||
        !sameBits(s.alphaF(), q.alphaF()))
        return false;
    const iris::LinearColor ls = iris::linearOf(s);
    const float *lut = iris::srgbToLinearTable();
    if (!sameBits(ls.r, lut[q.red()]) || !sameBits(ls.g, lut[q.green()]) || !sameBits(ls.b, lut[q.blue()]))
        return false;
    // the editor's edge, both ways: the same QColor back, the same SrgbColor back. AN INVALID COLOUR crosses as THE
    // invalid colour (QColor() / SrgbColor(): alpha 1): the alpha 0 an out-of-range QColor(-1, ...) keeps is not
    // carried across (document/qt/colour.h says so).
    if (!s.isValid()) return !iris::toQColor(s).isValid() && !iris::toSrgbColor(q).isValid();
    if (iris::toQColor(s) != q || iris::toSrgbColor(q) != s) return false;
    return true;
}

int main()
{
    // ---- the 8-bit constructor -------------------------------------------------------------------------------
    {
        int bad = 0;
        for (int x = -1; x <= 256; ++x) {
            ++compared;
            if (!same(iris::SrgbColor(x, 255 - x, x / 2, 255 - x / 3), QColor(x, 255 - x, x / 2, 255 - x / 3))) {
                if (!bad++) std::printf("    first difference: the 8-bit channel %d\n", x);
            }
        }
        CHECK(bad == 0, "the 8-bit constructor (-1..256: the out-of-range ones invalid) stores what QColor stores");
    }
    // ---- every 16-bit channel value --------------------------------------------------------------------------
    {
        int bad = 0;
        for (int v = 0; v <= 0xffff; ++v) {
            ++compared;
            const std::uint16_t c = std::uint16_t(v), o = std::uint16_t(0xffff - v);
            if (!same(iris::SrgbColor::fromRgba64(c, o, c, o), QColor::fromRgba64(c, o, c, o))) {
                if (!bad++) std::printf("    first difference: the 16-bit channel %d\n", v);
            }
        }
        CHECK(bad == 0, "every 16-bit channel value (65,536): the 8-bit reduction, the F accessors, linearOf and the "
                        "edge both ways equal QColor's");
    }
    // ---- 100,000 seeded floats (and the ends) through fromRgbF ----------------------------------------------
    {
        std::mt19937 rng(0x5eedc010u);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        int bad = 0;
        const float ends[] = { 0.0f, 1.0f, 0.5f, 1.0f / 65535.0f, 0.5f / 65535.0f };
        for (float e : ends) {
            ++compared;
            if (!same(iris::SrgbColor::fromRgbF(e, e, e, e), QColor::fromRgbF(e, e, e, e))) ++bad;
        }
        for (int i = 0; i < 100000; ++i) {
            const float r = unit(rng), g = unit(rng), b = unit(rng), a = unit(rng);
            ++compared;
            if (!same(iris::SrgbColor::fromRgbF(r, g, b, a), QColor::fromRgbF(r, g, b, a))) {
                if (!bad++) std::printf("    first difference: fromRgbF(%.9g, %.9g, %.9g, %.9g)\n", r, g, b, a);
            }
        }
        CHECK(bad == 0, "100,000 seeded floats and the ends: fromRgbF stores and answers what QColor's does");
        ++compared;
        CHECK(!iris::SrgbColor::fromRgbF(0.5f, 0.5f, 0.5f, 1.5f).isValid() &&
              !QColor::fromRgbF(0.5f, 0.5f, 0.5f, 1.5f).isValid(),
              "an alpha outside 0..1 is invalid on both");
    }
    // ---- the invalid state -----------------------------------------------------------------------------------
    {
        ++compared;
        CHECK(same(iris::SrgbColor(), QColor()), "the invalid colour reads what QColor's does (alpha 1, colour 0) and "
                                                 "round-trips the edge");
    }
    // ---- the control -----------------------------------------------------------------------------------------
    std::printf("    values compared: %ld\n", compared);
    CHECK(compared == 258 + 65536 + 5 + 100000 + 1 + 1, "the control: every value above was compared");

    if (failures) std::printf("\ncore.colour_qt_parity: FAILED (%d)\n", failures);
    else std::printf("\ncore.colour_qt_parity: PASSED\n");
    std::fflush(stdout);
    return failures ? 1 : 0;
}
