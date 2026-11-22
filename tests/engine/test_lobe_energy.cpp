// engine.lobe_energy (MATERIAL-ENERGY-1 stage 1) - THE LOBE RETURNS NO MORE LIGHT THAN IT RECEIVES. CPU only: the fork's
// shipped JahBrdf / JahDiffuseAlbedo / JahSpecularAlbedo text COMPILED as C++ (tests/support/glsl_as_cpp.h, through the
// engine's own unwrap step), the table it reads #included (irisgl PhotonSpecularAlbedo.h); tests/support/lobe_energy.h.
//
// THE QUANTITY. A surface lit by a uniform white sky of radiance 1 (the WHITE FURNACE) returns toward a view at cosine mu
// its TOTAL DIRECTIONAL ALBEDO A( mu ) = int ( f_d + f_s ) mu_i dw_i. The specular part is Es( mu ) = kS ( F0 A + B ), the
// GGX lobe's own integral (the table); the diffuse part is rho_d D( mu ), D the diffuse lobe's integral relative to kD.
// THE LAW (P2, every metalness): energy the specular does not reflect is what the diffuse may scatter, so a coupled lobe has
//     A( mu ) = rho_d + ( 1 - rho_d ) Es( mu )        (= 1 in the furnace, rho_d = 1; = Es <= 1 for a metal, rho_d = 0)
// which holds iff D( mu ) = 1 - Es( mu ). FORM B (lobe_energy.h) gives D = ( 1 - Es( mu ) ) ( 1 - M ) / ( 1 - Es-bar ),
// M = 2 int Es mu dmu: exact iff the table's hemispherical rows ARE the moment M of its own bilinear directional table -
// P1's re-derived rows (exactMomentTable(); the shipped rows move by <= 1.64e-4 and stage 3 ships them).
//
// THE LAW IN TWO PARTS (the lead's ruling 2026-11-22):
// (L1) THE TABLE IS THE LOBE'S INTEGRAL: Es_tab against enginetest::ggxSpecularAlbedo, an independent quadrature of the same
//      GGX lobe. ONLY AT mu >= 0.001 - NOT A LOOSENED BAR: the reference floors the view cosine at 1e-3 itself
//      (enginetesthelpers.h, ggxSpecularAlbedo's first line), so at mu = 0 it evaluates mu = 0.001 and there is nothing
//      independent to compare with; mu = 0 is L2's. PRINTED, GATED AT STAGE 3 (the lead's ruling A, 2026-11-22): the
//      table's own stated bound is FALSE today (0.65 % at v >= 0.2 from the bilinear read in r at low roughness; up to 6 %
//      absolute in the first v cell, where the low-roughness albedo dips steeply at grazing); stage 3 rebuilds the table so
//      a true bound holds by construction, and gates L1 at that derived bound - never at a bar read off today's data.
// (L2) THE COUPLING IS EXACT: with the table's own Es (whatever its accuracy), the diffuse lobe integrated by DIRECT
//      quadrature over the light's hemisphere (albedoQuad: exact for these lobes, see there) gives A = rho_d + ( 1 - rho_d ) Es
//      at EVERY grid point, mu = 0 included (P3), within the float rounding of the shipped reads:
//        u = 2^-24. A bilinear read rounds 3 times in each of its 2 mix levels (6u of entries <= 1) and its cell position
//        x = 31 v once (<= 2^-20 at x < 32, times the steepest cell step 0.14: <= 3u): |dA|, |dB| <= 9u, so
//        |dEs| <= kS ( F0 + 1 ) 9u <= 18u; the quadrature averages reads, |dQ| <= 18u; a stored row rounds once and the
//        hemispherical lerp 3 times, |d( 1 - Es-bar )| <= 8u. D = ( 1 - Es( mu ) ) Q / ( 1 - Es-bar ), so
//          eps2 = rho_d [ 18u + ( 1 - Es( mu ) ) 26u / ( 1 - Es-bar ) ]
//        (largest at the near-mirror F0 1, kS 1, r 0, where 1 - Es-bar = 4.4e-5 amplifies the rounding of Es near 1 - the
//        fp32 GPU has the same). The quadrature's convergence: split 2 against 1 on a sub-grid, within the same eps2.
// THE GRID: r 0, 0.05 ... 1; mu 0, 0.001, 1/64 ... 1; the materials: the specular workflow at rho_d 1 (F0 0.02 0.04 0.08 0.9
// 0.6 0.3 1, kS 1 and 0.5 - the coloured F0 ( 0.9, 0.6, 0.3 ) is its three channels) and the metallic workflow (rho 1 0.9 0.6
// 0.3, metalness 0 0.5 1). The coupling is per channel, so a scalar channel is the whole law.
//
// RED FIRST - TODAY's SHIPPED LOBE, A NAMED NEGATIVE ARM (it must BREAK the law; the row's red-first evidence, kept in
// spikes/material-energy-1/stage1/): the Frostbite-renormalised Disney diffuse (JahBrdf jahDisneyDiffuse) integrated the same
// way: A = 1.1155 at rho_d 1, F0 0.04, r 1, mu 0 (its closed form jahDiffuseAlbedo + the table; 1.1138 at mu 0.001; 0.698
// head-on). WHEN STAGE 3 SHIPS FORM B THIS ARM FLIPS: delete it then (and compile Form B from the shipped text here).
// NEGATIVE CONTROLS (kept): Form B normalised by 1 - Es( 1 ) instead of 1 - Es-bar, and with no normaliser, must break L2
// (the preflight: by 4.58 % and 8.24 % at F0 0.04); Form B on the SHIPPED rows breaks it at F0 1 (+50.2 % at grazing, r 0;
// a floor max( 1 - Es-bar, 1e-3 ) makes that a 10.7 % LOSS: P1).
// GUARDS: f( L, V ) = f( V, L ) exactly (Form B, today's diffuse, the shipped specular); kS 0 -> Form B = rho / pi exactly
// (today's Disney ignores kS: an arm); Es <= 1 (a metal returns at most what it receives).
// PRINTED, NOT GATED (the lead's rulings 3 and 4): an area light's coupling at its centre direction against the exact
// ( 1 - Es )-weighted rectangle, and at the form-factor vector's direction (ruling 3 reversed by its own 1 % trigger: P8 (a)
// takes the form-factor vector, -43 % -> -3.2 % for a horizon-clipped wide light; -6.2 % stays at grazing, r 0); the
// coupling's roughness - jahLobePerceptualRoughness against the material's (specular AA) and against a sphere light's
// widened alpha.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "lobe_energy.h"
#include "enginetesthelpers.h"

using namespace lobeenergy;

static int gFail = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++gFail;
}

namespace {
const double kU = 1.0 / 16777216.0;   // 2^-24

struct Mat {
    const char *name;
    Channel c;
};
std::vector<Mat> materials()
{
    static char names[64][48];
    std::vector<Mat> m;
    int n = 0;
    for (double kS : { 1.0, 0.5 })
        for (double F0 : { 0.02, 0.04, 0.08, 0.9, 0.6, 0.3, 1.0 }) {
            std::snprintf(names[n], sizeof names[n], "specular F0 %.2f kS %.1f", F0, kS);
            m.push_back({ names[n++], { 1.0, F0, kS } });
        }
    for (double rho : { 1.0, 0.9, 0.6, 0.3 })
        for (double mt : { 0.0, 0.5, 1.0 }) {
            std::snprintf(names[n], sizeof names[n], "metallic rho %.1f metalness %.1f", rho, mt);
            m.push_back({ names[n++], metallic(rho, mt) });
        }
    return m;
}
std::vector<double> rGrid()
{
    std::vector<double> g;
    for (int j = 0; j <= 20; ++j) g.push_back(0.05 * j);
    return g;
}
std::vector<double> muGrid()
{
    std::vector<double> g = { 0.0, 0.001 };
    for (int k = 1; k <= 64; ++k) g.push_back(k / 64.0);
    return g;
}
double eps2(const Channel &c, double mu, double r)
{
    return c.rhoD * (18.0 * kU + (1.0 - es(c, mu, r)) * 26.0 * kU / formBNorm(c, r));
}
// A( mu ) with the diffuse by direct quadrature: Form B at normaliser `norm` (the table gTable names)
double totalFormB(const Channel &c, double mu, double r, double norm, int split = 1)
{
    const double D = albedoQuad([&](double nl, double nv, double) { return formB(c, nl, nv, r, norm); }, mu, split);
    return c.rhoD * D + es(c, mu, r);
}
double totalToday(const Channel &c, double mu, double r)
{
    const double D = albedoQuad([&](double nl, double nv, double vh) { return today(nl, nv, vh, r); }, mu);
    return c.rhoD * D + es(c, mu, r);
}
double closedForm(const Channel &c, double mu, double r) { return c.rhoD + (1.0 - c.rhoD) * es(c, mu, r); }

struct Worst {
    double dev = 0.0, ratio = 0.0, r = 0.0, mu = 0.0;
    const char *mat = "";
    int infinite = 0;   // a normaliser of 0 (the negative controls' 1 - Es( 1 ) at the mirror) divides by zero
    void see(double d, double bar, double rr, double mm, const char *name)
    {
        if (!std::isfinite(d)) { ++infinite; return; }
        if (std::fabs(d) / bar > ratio) { ratio = std::fabs(d) / bar; dev = d; r = rr; mu = mm; mat = name; }
    }
    void seeAbs(double d, double rr, double mm, const char *name)
    {
        if (std::fabs(d) > std::fabs(dev)) { dev = d; r = rr; mu = mm; mat = name; }
    }
};
}   // namespace

int main()
{
    const auto mats = materials();
    const auto rs = rGrid();
    const auto mus = muGrid();
    const auto derived = exactMomentTable();
    const Channel plaster{ 1.0, 0.04, 1.0 };
    char line[512];

    // ---- RED FIRST: today's shipped lobe, the ground read's numbers (the shim's proof)
    const double t0 = todayAlbedo(0.0, 1.0) + es(plaster, 0.0, 1.0);
    const double t1 = todayAlbedo(0.001, 1.0) + es(plaster, 0.001, 1.0);
    const double tn = todayAlbedo(1.0, 1.0) + es(plaster, 1.0, 1.0);
    std::printf("-- the shipped text, compiled: TODAY's total albedo at rho_d 1, F0 0.04, r 1: %.4f at mu 0, %.4f at mu 0.001, "
                "%.4f head-on\n", t0, t1, tn);
    check(std::fabs(t0 - 1.1155) < 5e-5 && std::fabs(t1 - 1.1138) < 5e-5 && std::fabs(tn - 0.698) < 5e-4,
          "the compiled shipped text reproduces the ground read's python evaluation (1.1155 / 1.1138 / 0.698)");

    // ---- P1: the hemispherical rows as the exact moment of the bilinear directional table
    double moveA = 0.0, moveB = 0.0, moveEs = 0.0;
    for (int j = 0; j < 32; ++j) {
        const double dA = derived[kRows + 2 * j] - kTable[kRows + 2 * j], dB = derived[kRows + 2 * j + 1] - kTable[kRows + 2 * j + 1];
        moveA = std::max(moveA, std::fabs(dA));
        moveB = std::max(moveB, std::fabs(dB));
        moveEs = std::max(moveEs, std::fabs(0.04 * dA + dB));
    }
    std::printf("-- P1: the 32 re-derived rows move by at most %.3g (A-bar), %.3g (B-bar), %.3g (Es-bar at F0 0.04)\n", moveA,
                moveB, moveEs);
    check(moveA < 2.2e-3 && moveB < 2.2e-3, "P1: every row moves inside the table's stated 0.22 % (the re-derivation is a "
                                            "correction of the rows, not a new table)");

    UseTable useDerived(derived.data());

    // ---- L2: FORM B meets the closed form on the whole grid
    Worst l2;
    double l2Abs = 0.0;
    for (const auto &m : mats)
        for (double r : rs) {
            const double nb = formBNorm(m.c, r);
            for (double mu : mus) {
                const double d = totalFormB(m.c, mu, r, nb) - closedForm(m.c, mu, r);
                if (m.c.rhoD > 0.0) l2.see(d, eps2(m.c, mu, r), r, mu, m.name);
                l2Abs = std::max(l2Abs, std::fabs(d));
            }
        }
    std::snprintf(line, sizeof line, "L2: FORM B's total albedo = rho_d + (1 - rho_d) Es at every point (%zu materials x %zu r "
                  "x %zu mu, mu 0 included): worst |dev| %.3g; worst dev/eps2 %.3f (%+.3g at %s, r %.2f, mu %.4f)", mats.size(),
                  rs.size(), mus.size(), l2Abs, l2.ratio, l2.dev, l2.mat, l2.r, l2.mu);
    check(l2.ratio <= 1.0, line);
    // the quadrature's convergence: split 2 against split 1
    Worst conv;
    for (const Channel &c : { plaster, Channel{ 1.0, 1.0, 1.0 }, Channel{ 1.0, 0.9, 0.5 } })
        for (double r : { 0.0, 0.25, 0.5, 0.75, 1.0 })
            for (double mu : { 0.0, 0.001, 0.25, 0.5, 1.0 }) {
                const double nb = formBNorm(c, r);
                conv.see(totalFormB(c, mu, r, nb, 2) - totalFormB(c, mu, r, nb, 1), eps2(c, mu, r), r, mu, "");
            }
    std::snprintf(line, sizeof line, "L2: the quadrature has converged - 62 cells against 31 differ by %.3g at most (dev/eps2 "
                  "%.3f)", std::fabs(conv.dev), conv.ratio);
    check(conv.ratio <= 1.0, line);

    // ---- L1: the table against the independent quadrature of the lobe (mu >= 0.001: the reference's own floor)
    {
        std::printf("-- L1 PRINTED, GATED AT STAGE 3: the table's stated bound (PhotonSpecularAlbedo.h: 0.22 %% at v >= 0.2, "
                    "0.07 %% x cos) is FALSE today; stage 3 rebuilds the table so a true bound holds by construction, then gates "
                    "this. |Es_tab - Es_ref|, Es_ref = ggxSpecularAlbedo n 128:\n");
        const double F0s[3] = { 0.02, 0.04, 1.0 };
        double rel02[3] = {}, abs02[3] = {}, absMid[3] = {}, absCell0[3] = {}, absCos = 0.0;
        double rRel[3] = {}, mRel[3] = {}, rC0[3] = {}, mC0[3] = {};
        for (double r : rs)
            for (double mu : mus) {
                if (mu < 0.001) continue;
                double Ar, Br, At, Bt;
                enginetest::ggxSpecularAlbedo(mu, r, Ar, Br, 128);
                specAlbedo(mu, r, At, Bt);
                for (int f = 0; f < 3; ++f) {
                    const double er = F0s[f] * Ar + Br, d = std::fabs((F0s[f] * At + Bt) - er);
                    if (mu >= 0.2) {
                        if (d / er > rel02[f]) { rel02[f] = d / er; rRel[f] = r; mRel[f] = mu; }
                        abs02[f] = std::max(abs02[f], d);
                    } else if (mu >= 1.0 / 31.0) {
                        absMid[f] = std::max(absMid[f], d);
                    } else if (d > absCell0[f]) {
                        absCell0[f] = d; rC0[f] = r; mC0[f] = mu;
                    }
                    absCos = std::max(absCos, d * mu);
                }
            }
        for (int f = 0; f < 3; ++f)
            std::printf("   F0 %.2f: v >= 0.2 worst %.3f %% relative (r %.2f, mu %.3f), %.2g absolute; 1/31 <= v < 0.2 %.2g "
                        "absolute; THE FIRST v CELL (v < 1/31) %.4f absolute (r %.2f, mu %.4f)\n", F0s[f], 100.0 * rel02[f],
                        rRel[f], mRel[f], abs02[f], absMid[f], absCell0[f], rC0[f], mC0[f]);
        // THE REFERENCE's OWN CONVERGENCE, per F0 and band: its half-vector sampler converges about as 1 / sqrt( n ) at
        // grazing views and, with F0 -> 1, slowly even at mu 0.25 (spikes/material-energy-1/stage1/
        // l1_reference_convergence.txt) - where it moves, the number above is the reference's error, not the table's.
        double conv[3][2] = {};
        for (double r : { 0.0, 0.25, 0.5, 0.75, 1.0 })
            for (double mu : { 0.001, 0.0156, 0.25, 0.5, 1.0 }) {
                double A1, B1, A2, B2;
                enginetest::ggxSpecularAlbedo(mu, r, A1, B1, 128);
                enginetest::ggxSpecularAlbedo(mu, r, A2, B2, 256);
                for (int f = 0; f < 3; ++f) {
                    double &c = conv[f][mu >= 0.2 ? 0 : 1];
                    c = std::max(c, std::fabs((F0s[f] * A1 + B1) - (F0s[f] * A2 + B2)));
                }
            }
        std::printf("   worst |error| x mu %.2g; THE REFERENCE's own move, n 128 against 256 (v >= 0.2 | v < 0.2): F0 0.02 %.2g | %.2g, "
                    "F0 0.04 %.2g | %.2g, F0 1 %.2g | %.2g\n", absCos, conv[0][0], conv[0][1], conv[1][0], conv[1][1], conv[2][0],
                    conv[2][1]);
    }

    // ---- TODAY's SHIPPED LOBE (the named negative arm): it BREAKS the law
    {
        Worst up, down, upP, downP;
        double closedVsQuad = 0.0;
        for (const auto &m : mats) {
            if (m.c.rhoD <= 0.0) continue;
            const bool isPlaster = m.c.rhoD == 1.0 && m.c.F0 == 0.04 && m.c.kS == 1.0;
            for (double r : rs)
                for (double mu : mus) {
                    const double d = totalToday(m.c, mu, r) - closedForm(m.c, mu, r);
                    if (d > up.dev) up = { d, 0.0, r, mu, m.name };
                    if (d < down.dev) down = { d, 0.0, r, mu, m.name };
                    if (isPlaster && d > upP.dev) upP = { d, 0.0, r, mu, m.name };
                    if (isPlaster && d < downP.dev) downP = { d, 0.0, r, mu, m.name };
                }
        }
        std::printf("-- TODAY at white plaster (rho_d 1, F0 0.04, kS 1): the furnace reads %+.2f %% at r %.2f, mu %.4f and %+.2f %% "
                    "at r %.2f, mu %.4f\n", 100.0 * upP.dev, upP.r, upP.mu, 100.0 * downP.dev, downP.r, downP.mu);
        for (double r : rs)
            for (double mu : mus)
                closedVsQuad = std::max(closedVsQuad, std::fabs(todayAlbedo(mu, r) - albedoQuad(
                    [&](double nl, double nv, double vh) { return today(nl, nv, vh, r); }, mu)));
        std::snprintf(line, sizeof line, "TODAY's shipped lobe BREAKS the law (red first): it CREATES %+.2f %% (%s, r %.2f, mu "
                      "%.4f) and loses %+.2f %% (%s, r %.2f, mu %.4f); its closed form is its own integral within %.2g - "
                      "IF THIS FAILS, STAGE 3 SHIPPED THE COUPLED LOBE: DELETE THIS ARM",
                      100.0 * up.dev, up.mat, up.r, up.mu, 100.0 * down.dev, down.mat, down.r, down.mu, closedVsQuad);
        check(up.dev > 0.1 && up.dev > 1000.0 * eps2(plaster, up.mu, up.r) && down.dev < -0.25, line);
        const double lam = today(0.5, 0.5, std::sqrt(0.5 * (1.0 + 0.5 * 0.5 + 0.75)), 1.0);
        std::snprintf(line, sizeof line, "TODAY's shipped lobe ignores kS: at kS 0, r 1 its diffuse is %.4f x rho / pi, not 1 "
                      "(Form B: 1 exactly) - DELETE WITH THE ARM AT STAGE 3", lam);
        check(std::fabs(lam - 1.0) > 0.01, line);
    }

    // ---- NEGATIVE CONTROLS: the wrong normaliser, no normaliser, the shipped rows
    {
        Worst wrong, none, wrong04, none04;
        for (const auto &m : mats) {
            if (m.c.rhoD <= 0.0) continue;
            for (double r : rs) {
                const double n1 = 1.0 - es(m.c, 1.0, r);
                for (double mu : mus) {
                    const double bar = eps2(m.c, mu, r), cf = closedForm(m.c, mu, r);
                    const double dw = totalFormB(m.c, mu, r, n1) - cf, dn = totalFormB(m.c, mu, r, 1.0) - cf;
                    wrong.see(dw, bar, r, mu, m.name);
                    none.see(dn, bar, r, mu, m.name);
                    if (m.c.F0 == 0.04 && m.c.kS == 1.0 && m.c.rhoD == 1.0) {
                        wrong04.seeAbs(dw, r, mu, m.name);
                        none04.seeAbs(dn, r, mu, m.name);
                    }
                }
            }
        }
        std::snprintf(line, sizeof line, "negative control: Form B normalised by 1 - Es(1) BREAKS L2 (dev/eps2 %.0f, and %d points "
                      "divide by 1 - Es(1) = 0; at F0 0.04 the furnace is off by %.2f %%, r %.2f, mu %.4f)", wrong.ratio, wrong.infinite, 100.0 * std::fabs(wrong04.dev), wrong04.r,
                      wrong04.mu);
        check(wrong.ratio > 1.0, line);
        std::snprintf(line, sizeof line, "negative control: Form B with no normaliser BREAKS L2 (dev/eps2 %.0f; at F0 0.04 the "
                      "furnace is off by %.2f %%, r %.2f, mu %.4f)", none.ratio, 100.0 * std::fabs(none04.dev), none04.r, none04.mu);
        check(none.ratio > 1.0, line);

        UseTable useShipped(kTable);
        const Channel mirror{ 1.0, 1.0, 1.0 };
        const double nOld = formBNorm(mirror, 0.0);
        const double aOld = totalFormB(mirror, 0.001, 0.0, nOld), aFloor = totalFormB(mirror, 0.001, 0.0, std::max(nOld, 1e-3));
        Worst dielectric;
        for (const auto &m : mats) {
            if (m.c.rhoD <= 0.0 || m.c.F0 > 0.9) continue;
            for (double r : rs)
                for (double mu : mus) dielectric.seeAbs(totalFormB(m.c, mu, r, formBNorm(m.c, r)) - closedForm(m.c, mu, r), r, mu, m.name);
        }
        std::snprintf(line, sizeof line, "negative control: Form B on the SHIPPED rows BREAKS the furnace at F0 1, kS 1, r 0, "
                      "mu 0.001: A = %.3f (%+.1f %%; 1 - Es-bar %.2g there); a floor max(1 - Es-bar, 1e-3) makes it %.3f (%+.1f %%); "
                      "to F0 0.9 the shipped rows are off by %.3g at most", aOld, 100.0 * (aOld - 1.0), nOld, aFloor,
                      100.0 * (aFloor - 1.0), std::fabs(dielectric.dev));
        check(aOld - 1.0 > 0.4 && aFloor - 1.0 < -0.05, line);
    }
    {   // what a floor would cost on the derived rows (stage 3's choice): printed
        const Channel mirror{ 1.0, 1.0, 1.0 };
        double f3 = 0.0, f6 = 0.0;
        for (double r : rs) {
            const double nb = formBNorm(mirror, r);
            for (double mu : mus) {
                f3 = std::max(f3, std::fabs(totalFormB(mirror, mu, r, std::max(nb, 1e-3)) - 1.0));
                f6 = std::max(f6, std::fabs(totalFormB(mirror, mu, r, std::max(nb, 1e-6)) - 1.0));
            }
        }
        std::printf("-- the floor on the derived rows (F0 1, kS 1; for stage 3): max(1 - Es-bar, 1e-3) costs the furnace %.3g, "
                    "1e-6 costs %.3g; 1 - Es-bar is %.3g at its smallest (r 0)\n", f3, f6, formBNorm(mirror, 0.0));
    }

    // ---- GUARDS: reciprocity, the Lambert limit, Es <= 1
    {
        bool recip = true;
        double specWorst = 0.0;
        const jahglsl::vec3 N(0.0f, 0.0f, 1.0f);
        for (const Channel &c : { plaster, Channel{ 0.45, 0.47, 1.0 } })
            for (double r : { 0.0, 0.3, 1.0 })
                for (double a : { 0.05, 0.4, 0.9 })
                    for (double b : { 0.2, 0.7, 1.0 }) {
                        recip = recip && formB(c, a, b, r, formBNorm(c, r)) == formB(c, b, a, r, formBNorm(c, r));
                        const float sa = float(std::sqrt(1.0 - a * a)), sb = float(std::sqrt(1.0 - b * b));
                        const jahglsl::vec3 L(sa, 0.0f, float(a)), V(-0.6f * sb, 0.8f * sb, float(b));
                        const jahglsl::vec3 H = jahglsl::normalize(L + V);
                        const float vh = jahglsl::dot(V, H);
                        recip = recip && today(a, b, vh, r) == today(b, a, vh, r);
                        const jahglsl::vec3 s1 = jahglsl::jahSpecularLobe(N, L, V, float(r), jahglsl::vec3(float(c.F0)), 1.0f);
                        const jahglsl::vec3 s2 = jahglsl::jahSpecularLobe(N, V, L, float(r), jahglsl::vec3(float(c.F0)), 1.0f);
                        // the shipped specular takes V.H where the swapped call takes L.H - equal, rounded apart by <= 6u;
                        // Schlick's F moves by <= 5 x 6u absolute, so <= 30u / F0 of F >= F0; H, D and lv + ll are shared
                        // or swapped exactly (+ 2u for the products)
                        specWorst = std::max(specWorst, double(std::fabs(s1.x - s2.x) / std::fabs(s1.x)));
                        recip = recip && std::fabs(s1.x - s2.x) <= (32.0 * kU / c.F0 + 2.0 * kU) * std::fabs(s1.x);
                    }
        std::snprintf(line, sizeof line, "f(L, V) = f(V, L): Form B and today's diffuse EXACTLY; the shipped specular "
                      "(jahSpecularLobe) to its fp32 rounding (worst %.2g relative, bar 32u / F0 + 2u)", specWorst);
        check(recip, line);
        bool lambert = true;
        for (double r : rs)
            for (double a : { 0.0, 0.3, 1.0 }) lambert = lambert && formB({ 1.0, 0.04, 0.0 }, a, 0.7, r, formBNorm({ 1.0, 0.04, 0.0 }, r)) == 1.0;
        check(lambert, "kS 0: Form B is rho / pi exactly (the lobe relative to kD is 1 at every r and angle)");
        double maxAB = 0.0;
        for (std::size_t k = 0; k < kRows; k += 2) maxAB = std::max(maxAB, double(kTable[k]) + double(kTable[k + 1]));
        std::snprintf(line, sizeof line, "Es <= 1: every directional node has A + B <= 1 + 2u (max %.9f): a metal (rho_d 0, A = Es) "
                      "returns at most what it receives", maxAB);
        check(maxAB <= 1.0 + 2.0 * kU, line);
    }

    // ---- PRINTED (ruling 3): an area light's coupling at its centre direction against the exact (1 - Es)-weighted rectangle
    {
        std::printf("-- RULING 3 (printed): an area light's DIFFUSE coupled at ONE direction against the exact (1 - Es)-weighted "
                    "rectangle (a square at 1 m facing the point, the horizon clipped), the error in %% of the light's irradiance, "
                    "worst over F0 0.04 / 0.9 / 1 x r 0 ... 1 (the centre: ruling 3; the form-factor vector's direction):\n");
        bool over = false;
        for (double widthDeg : { 60.0, 10.0 })
            for (double muc : { 0.1, 0.5, 1.0 }) {
                double worstC = 0.0, worstF = 0.0, rC = 0.0, f0C = 0.0;
                for (const Channel &c : { plaster, Channel{ 1.0, 0.9, 1.0 }, Channel{ 1.0, 1.0, 1.0 } })
                    for (double r : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
                        const double sc = std::sqrt(std::max(0.0, 1.0 - muc * muc));
                        const double cx = sc, cz = muc, e1x = muc, e1z = -sc;   // e1 in the plane of N and the centre, e2 = y
                        const double h = std::tan(0.5 * widthDeg * kPi / 180.0);
                        const int n = 128;
                        double ff = 0.0, w = 0.0, fx = 0.0, fy = 0.0, fz = 0.0;
                        for (int i = 0; i < n; ++i)
                            for (int k = 0; k < n; ++k) {
                                const double u = h * (2.0 * (i + 0.5) / n - 1.0), v = h * (2.0 * (k + 0.5) / n - 1.0);
                                const double px = cx + u * e1x, py = v, pz = cz + u * e1z;
                                const double d = std::sqrt(px * px + py * py + pz * pz);
                                const double mu = pz / d;
                                if (!(mu > 0.0)) continue;
                                const double dw = (2.0 * h / n) * (2.0 * h / n) / (d * d * d);
                                ff += mu * dw;
                                w += (1.0 - es(c, mu, r)) * mu * dw;
                                fx += px / d * dw;
                                fy += py / d * dw;
                                fz += mu * dw;
                            }
                        const double muF = fz / std::sqrt(fx * fx + fy * fy + fz * fz);
                        const double eC = ((1.0 - es(c, muc, r)) * ff - w) / ff, eF = ((1.0 - es(c, muF, r)) * ff - w) / ff;
                        if (std::fabs(eC) > std::fabs(worstC)) { worstC = eC; rC = r; f0C = c.F0; }
                        if (std::fabs(eF) > std::fabs(worstF)) worstF = eF;
                    }
                over = over || std::fabs(worstC) > 0.01;
                std::printf("   %2.0f deg wide, centre mu %.1f: centre %+8.3f %% (F0 %.2f, r %.2f) | form-factor vector %+8.3f %%\n",
                            widthDeg, muc, 100.0 * worstC, f0C, rC, 100.0 * worstF);
            }
        if (over) std::printf("   the centre is over 1 %% on this grid: RULED P8 (a) - the area light takes the FORM-FACTOR VECTOR; its "
                              "residual at grazing on a smooth dielectric is a later item (the coupling over the solid angle)\n");
    }

    // ---- PRINTED (ruling 4): the roughness the coupling reads
    {
        double aa = 0.0;
        for (const Channel &c : { plaster, Channel{ 1.0, 1.0, 1.0 } })
            for (auto rr : { std::make_pair(0.1, 0.3), std::make_pair(0.25, 0.5), std::make_pair(0.5, 0.7) })
                for (double mu : mus) aa = std::max(aa, std::fabs(es(c, mu, rr.second) - es(c, mu, rr.first)));
        std::printf("-- RULING 4 (printed): specular AA - a coupling at the material's roughness instead of "
                    "jahLobePerceptualRoughness (the lobe's: 0.1 -> 0.3, 0.25 -> 0.5, 0.5 -> 0.7) misses the furnace by up to "
                    "%.3f %%\n", 100.0 * aa);
        std::printf("   a sphere light (the shipped jahSphereLightWiden; its widened, renormalised lobe's reflected energy by the "
                    "reference) against a coupling at jahLobePerceptualRoughness | at its widened alpha, worst over F0 0.04 / 1, "
                    "r 0.05 / 0.2 / 0.5, mu 0.3 / 0.6 / 1:\n");
        for (double size : { 0.01, 0.1, 0.3 }) {
            double atLobe = 0.0, atWide = 0.0;
            for (const Channel &c : { plaster, Channel{ 1.0, 1.0, 1.0 } })
                for (double r : { 0.05, 0.2, 0.5 })
                    for (double mu : { 0.3, 0.6, 1.0 }) {
                        const float alpha = std::max(float(r * r), 0.001f);
                        float wide = alpha;
                        const double norm = jahglsl::jahSphereLightWiden(alpha, float(size), 1.0f, float(mu), wide);
                        double A, B;
                        enginetest::ggxSpecularAlbedo(mu, std::sqrt(double(wide)), A, B, 128);
                        const double eSphere = norm * c.kS * (c.F0 * A + B);
                        const double eL = eSphere - es(c, mu, r), eW = eSphere - es(c, mu, std::sqrt(double(wide)));
                        if (std::fabs(eL) > std::fabs(atLobe)) atLobe = eL;
                        if (std::fabs(eW) > std::fabs(atWide)) atWide = eW;
                    }
            std::printf("   R/d %.2f: %+8.3f %% | %+8.3f %%\n", size, 100.0 * atLobe, 100.0 * atWide);
        }
    }

    std::printf("%s\n", gFail ? "engine.lobe_energy: FAILED" : "engine.lobe_energy: PASSED");
    return gFail ? 1 : 0;
}
