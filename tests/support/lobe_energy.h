// THE LOBE'S ENERGY ON THE CPU (MATERIAL-ENERGY-1 stages 0-1): ONE code path for engine.lobe_energy (the law) and for the
// owner's before/after picture (tests/support/lobe_picture.cpp) - the shipped text, compiled; the coupled diffuse the spec
// rules (Form B), stated once; the hemisphere quadrature that integrates either.
//
// THE SHIPPED TEXT: the fork's JahBrdf, JahDiffuseAlbedo and JahSpecularAlbedo pieces (irisgl/thirdparty/ogre-next/
// Samples/Media/Hlms/Pbs/Any), compiled as C++ through tests/support/glsl_as_cpp.h (namespace jahglsl); the table they
// read through JAH_SPEC_ALBEDO( k ) is irisgl's PhotonSpecularAlbedo.h - the shipped floats, or (P1) a copy whose 32
// hemispherical rows are re-derived as the exact moment of its own bilinear directional table (exactMomentTable()).
//
// FORM B (the owner's ruling, ledger 4453; the spec's "After"): per channel, relative to kD = rho_d / pi as
// jahDisneyDiffuse is,
//     g( mu_i, mu_o ) = ( 1 - Es( mu_i ) ) ( 1 - Es( mu_o ) ) / ( 1 - Es-bar ),   Es = kS ( F0 A + B ),  Es-bar = kS ( F0 A-bar + B-bar )
// Its cosine-weighted integral over the light's hemisphere is ( 1 - Es( mu_o ) ) ( 1 - M ) / ( 1 - Es-bar ), M = 2 int Es mu dmu
// - so the furnace holds EXACTLY when Es-bar = M: the reason the rows are re-derived (P1). A TEST-SIDE STATEMENT until stage
// 3 ships the text; stage 3 deletes it and compiles the shipped one here.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "photon/voxel/PhotonSpecularAlbedo.h"
#include "glsl_as_cpp.h"

namespace lobeenergy {
using Ogre::PhotonSpecularAlbedo::kFloats;
using Ogre::PhotonSpecularAlbedo::kTable;
constexpr std::size_t kRows = Ogre::PhotonSpecularAlbedo::kDirectionalFloats;   // the first hemispherical float
/// The table the compiled text reads: the shipped floats unless a caller points it at another copy.
inline const float *gTable = kTable;
}   // namespace lobeenergy

#define JAH_SPEC_ALBEDO(k) (::lobeenergy::gTable[(k)])
namespace jahglsl {
#include "jah_lobe_pieces.h"
}

namespace lobeenergy {

constexpr double kPi = 3.14159265358979323846;

/// One colour channel of a material, as BRDF_Default sees it: rho_d = pi kD (the diffuse albedo, after the metallic
/// workflow's ( 1 - metalness )), F0, kS (the specular colour); Schlick's F90 is 1.
struct Channel {
    double rhoD, F0, kS;
};
/// The metallic workflow (800.PixelShader_piece_ps.any SampleSpecularMap): F0 = lerp( 0.04, rho, m ), rho_d = rho ( 1 - m ).
inline Channel metallic(double rho, double metalness)
{
    return { rho * (1.0 - metalness), 0.04 + (rho - 0.04) * metalness, 1.0 };
}

/// The shipped directional and hemispherical reads (JahSpecularAlbedo), on whichever table gTable names.
inline void specAlbedo(double v, double r, double &A, double &B)
{
    float a = 0.0f, b = 0.0f;
    jahglsl::jahSpecularAlbedo(float(v), float(r), a, b);
    A = a;
    B = b;
}
inline void specAlbedoHemi(double r, double &A, double &B)
{
    float a = 0.0f, b = 0.0f;
    jahglsl::jahSpecularAlbedoHemi(float(r), a, b);
    A = a;
    B = b;
}
inline double es(const Channel &c, double v, double r)
{
    double A, B;
    specAlbedo(v, r, A, B);
    return c.kS * (c.F0 * A + B);
}
inline double esBar(const Channel &c, double r)
{
    double A, B;
    specAlbedoHemi(r, A, B);
    return c.kS * (c.F0 * A + B);
}

/// P1: the 32 hemispherical rows as the EXACT MOMENT of the bilinear directional table. Along v the read is the
/// piecewise-linear interpolant of the nodes a_i at v_i = i h, h = 1 / 31 (jahSpecularAlbedo: x = 31 v, i0 = min( int x, 30 )),
/// so on cell i, with v = h ( i + t ), A = a_i ( 1 - t ) + a_( i+1 ) t:
///     2 int_cell A v dv = 2 h^2 [ a_i int ( 1 - t )( i + t ) dt + a_( i+1 ) int t ( i + t ) dt ]
///                       = 2 h^2 [ a_i ( i / 2 + 1 / 6 ) + a_( i+1 ) ( i / 2 + 1 / 3 ) ]
/// summed over the 31 cells (a constant 1 gives 2 h^2 x 480.5 = 1). Along r the read lerps rows j and j + 1 with the same
/// weight jahSpecularAlbedoHemi lerps the moments, and the moment is linear in the nodes: so Es-bar = M at EVERY r and,
/// Es being linear in F0 and kS, for every F0 and kS. Computed in double, stored as the table's floats.
inline std::array<float, kFloats> exactMomentTable()
{
    std::array<float, kFloats> t{};
    std::copy(kTable, kTable + kFloats, t.begin());
    const double h = 1.0 / 31.0;
    for (int j = 0; j < 32; ++j)
        for (int ch = 0; ch < 2; ++ch) {   // A then B
            double m = 0.0;
            for (int i = 0; i < 31; ++i) {
                const double ai = kTable[2 * (32 * j + i) + ch], an = kTable[2 * (32 * j + i + 1) + ch];
                m += ai * (i / 2.0 + 1.0 / 6.0) + an * (i / 2.0 + 1.0 / 3.0);
            }
            t[kRows + 2 * j + ch] = float(2.0 * h * h * m);
        }
    return t;
}
/// Points gTable at a table for a scope (the row and the picture switch between the shipped rows and the derived ones).
struct UseTable {
    const float *saved;
    explicit UseTable(const float *t) : saved(gTable) { gTable = t; }
    ~UseTable() { gTable = saved; }
};

/// FORM B's lobe relative to kD (see the header): `norm` is its normaliser, 1 - Es-bar as ruled (formBNorm); the row's
/// negative controls pass others.
inline double formBNorm(const Channel &c, double r) { return 1.0 - esBar(c, r); }
inline double formB(const Channel &c, double mui, double muo, double r, double norm)
{
    return (1.0 - es(c, mui, r)) * (1.0 - es(c, muo, r)) / norm;
}
/// ...and its directional albedo as the environment term will state it (the integral of the lobe when Es-bar = M).
inline double formBAlbedo(const Channel &c, double muo, double r) { return 1.0 - es(c, muo, r); }

/// TODAY's lobe, the shipped text: the normalised Disney diffuse relative to kD, and its closed-form directional albedo.
inline double today(double NdotL, double NdotV, double VdotH, double r)
{
    return jahglsl::jahDisneyDiffuse(float(NdotL), float(NdotV), float(VdotH), float(r));
}
inline double todayAlbedo(double muo, double r) { return jahglsl::jahDiffuseAlbedo(float(muo), float(r)); }

/// A diffuse lobe's directional albedo BY DIRECT QUADRATURE over the light's hemisphere, (1 / pi) int g mu_i dw_i, the
/// view at cos `muo` (N = +z, V in the xz plane): `g( NdotL, NdotV, VdotH )`. Gauss-Legendre, 5 nodes in each of
/// 31 x `split` cells of mu_i aligned with the table's knots, so a lobe piecewise-polynomial in mu_i of degree <= 8 per
/// cell is integrated EXACTLY (Form B: the table's linear Es times mu, degree 2; today's Disney: degree 8 after the
/// azimuth); the azimuth by 4 midpoints, exact for a trigonometric polynomial of degree <= 3 (the lobes: <= 2 - Disney's
/// k^2 in V.L). `split` 2 against 1 is the row's convergence check.
template <class Lobe>
double albedoQuad(Lobe g, double muo, int split = 1)
{
    static const double x5[5] = { -0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640 };
    static const double w5[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665,
                                  0.2369268850561891 };
    const int nPhi = 4;
    const double so = std::sqrt(std::max(0.0, 1.0 - muo * muo));
    const int cells = 31 * split;
    const double h = 1.0 / cells;
    double sum = 0.0;
    for (int k = 0; k < cells; ++k)
        for (int q = 0; q < 5; ++q) {
            const double c = h * (k + 0.5 + 0.5 * x5[q]);
            const double s = std::sqrt(std::max(0.0, 1.0 - c * c));
            double ring = 0.0;
            for (int p = 0; p < nPhi; ++p) {
                const double ph = 2.0 * kPi * (p + 0.5) / nPhi;
                const double vl = so * s * std::cos(ph) + muo * c;              // V.L
                const double vdh = std::min(1.0, std::sqrt(std::max(0.0, 0.5 * (1.0 + vl))));   // V.H = sqrt( ( 1 + V.L ) / 2 )
                ring += g(c, muo, vdh);
            }
            sum += 0.5 * w5[q] * h * c * ring / nPhi;
        }
    return 2.0 * sum;   // (1 / pi) x 2 pi x the mean over the azimuth
}

}   // namespace lobeenergy
