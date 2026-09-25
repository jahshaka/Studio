// THE VOXEL LAB'S LIGHT (PHOTON-VOXEL-5): the colour the plane march carries, over an analytic
// store whose faces carry their own radiance - the isotropic light chain (the voxel's mean
// radiance premultiplied by c, box-filtered), the directional colour (step 0 / step 1's
// composite, each half-axis taking the radiance of the faces on that side) and the march's
// colour (jahVoxelReadPlane: the plane axis from the directional texel, the minor axes'
// radiance from their directional texels at the kernel's level). THREE LIGHT MODELS:
//   kOnePerVoxel   the store before PHOTON-VOXEL-5 (ii): every half reads the voxel's mean
//                  radiance, level 0 through the isotropic volume;
//   kPerSide       LIGHT PER FACE SIDE as the store builds it: a voxel whose faces form two
//                  opposed groups (VoxelMerge's two-sided test on the raw and folded normal
//                  sums) keeps the light of each group - the FRONT (the group its stored normal
//                  n is the mean of) and the BACK - and a half-axis (a, h) takes the side whose
//                  normal agrees with it (h n_a > 0 the front, < 0 the back, 0 their mean); a
//                  one-sided voxel keeps one light; level 0 reads the half looking back at the
//                  travel;
//   kPerFace       the ideal: each half-axis the radiance of its own faces.
#pragma once
#include "voxel_lab.h"

#include <array>
#include <functional>

namespace voxlab {

struct Rgb { double r = 0, g = 0, b = 0; };

enum LightModel { kOnePerVoxel = 0, kPerSide = 1, kPerFace = 2 };

struct LitCascade {
    Cascade c;
    std::vector<std::vector<float>> Li;                 // per level: rgba per texel, the iso light (premultiplied by c)
    std::vector<std::array<std::vector<float>, 6>> Dc;  // per directional level: rgb per texel, premultiplied by its alpha
    std::vector<float> rad0[2];                         // level 0: per texel 9 (per axis rgb), the half's radiance
    std::vector<float> rad0L[2];                        // level 0 AS THE RAYS READ IT (per half, per axis rgb)
    std::vector<float> c0;                              // level 0: the voxel's c (max coverage) - the light's alpha
    int model = kOnePerVoxel;
};

/// The chains over a level 0 already filled (rad0 per half, c0): the isotropic light chain from
/// `li0` (rgba premultiplied by c) and the directional colour - step 0 / step 1's composite.
inline void finishLit(LitCascade &lc, const std::vector<float> &li0)
{
    // the iso light chain: a box filter over the same levels as the coverage chain
    lc.Li.push_back(li0);
    for (size_t m = 1; m < lc.c.L.size(); ++m) {
        const Level &p = lc.c.L[m - 1], &q = lc.c.L[m];
        const std::vector<float> &src = lc.Li[m - 1];
        std::vector<float> dst(size_t(q.r[0]) * q.r[1] * q.r[2] * 4, 0.f);
        for (int z = 0; z < q.r[2]; ++z) for (int y = 0; y < q.r[1]; ++y) for (int x = 0; x < q.r[0]; ++x)
            for (int k8 = 0; k8 < 8; ++k8) {
                const int X = std::min(2 * x + (k8 & 1), p.r[0] - 1), Y = std::min(2 * y + ((k8 >> 1) & 1), p.r[1] - 1),
                          Z = std::min(2 * z + ((k8 >> 2) & 1), p.r[2] - 1);
                const size_t s = (size_t(Z) * p.r[1] + Y) * p.r[0] + X, d = (size_t(z) * q.r[1] + y) * q.r[0] + x;
                for (int k = 0; k < 4; ++k) dst[d * 4 + k] += src[s * 4 + k] / 8.f;
            }
        lc.Li.push_back(dst);
    }
    // the directional levels (opacity: buildDir) and their colour, the same composite
    buildDir(lc.c);
    for (size_t m = 0; m < lc.c.D.size(); ++m) {
        const DirLevel &dl = lc.c.D[m];
        std::array<std::vector<float>, 6> col;
        const size_t nd = size_t(dl.r[0]) * dl.r[1] * dl.r[2];
        int sr[3];
        if (m == 0) for (int a = 0; a < 3; ++a) sr[a] = lc.c.L[0].r[a];
        else for (int a = 0; a < 3; ++a) sr[a] = lc.c.D[m - 1].r[a];
        for (int d = 0; d < 6; ++d) {
            const int ax = d / 2;
            const bool pos = d % 2 == 0;
            const int half = pos ? kN : kP;   // +a travel reads the faces looking -a
            col[size_t(d)].assign(nd * 3, 0.f);
            for (int z = 0; z < dl.r[2]; ++z) for (int y = 0; y < dl.r[1]; ++y) for (int x = 0; x < dl.r[0]; ++x) {
                double acc[3] = { 0, 0, 0 };
                const int u = (ax + 1) % 3, v = (ax + 2) % 3;
                for (int lu = 0; lu < 2; ++lu) for (int lv = 0; lv < 2; ++lv) {
                    double k[2], c3[2][3];
                    for (int f = 0; f < 2; ++f) {
                        int q[3] = { 2 * x, 2 * y, 2 * z };
                        q[u] += lu; q[v] += lv;
                        q[ax] += pos ? f : 1 - f;
                        for (int t = 0; t < 3; ++t) q[t] = std::min(q[t], sr[t] - 1);
                        const size_t s = (size_t(q[2]) * sr[1] + q[1]) * sr[0] + q[0];
                        if (m == 0) {
                            k[f] = std::min(1.0, double(lc.c.L[0].O[half][s * 3 + ax]));
                            for (int ch = 0; ch < 3; ++ch) c3[f][ch] = lc.rad0[half][s * 9 + ax * 3 + ch] * k[f];
                        } else {
                            k[f] = lc.c.D[m - 1].a[d][s];
                            for (int ch = 0; ch < 3; ++ch) c3[f][ch] = lc.Dc[m - 1][size_t(d)][s * 3 + ch];
                        }
                    }
                    const double take = std::min(k[1], 1.0 - k[0]);
                    for (int ch = 0; ch < 3; ++ch)
                        acc[ch] += c3[0][ch] + (k[1] > 0 ? c3[1][ch] * (take / k[1]) : 0.0);
                }
                const size_t o = (size_t(z) * dl.r[1] + y) * dl.r[0] + x;
                for (int ch = 0; ch < 3; ++ch) col[size_t(d)][o * 3 + ch] = float(acc[ch] / 4.0);
            }
        }
        lc.Dc.push_back(col);
    }
}

/// The analytic store with light: `rad(box, axis, side)` the radiance of that face (side 0 the lo
/// face looking -a, 1 the hi face looking +a); `model` a LightModel.
inline LitCascade synthLit(const std::vector<Box> &boxes, const std::function<Rgb(int, int, int)> &rad, int model,
                           const double org[3], const double sizeV[3], const int Rv[3])
{
    LitCascade lc;
    lc.model = model;
    lc.c = synthStore(boxes, false, 0.0, org, sizeV, Rv);
    const size_t n = size_t(Rv[0]) * Rv[1] * Rv[2];
    std::vector<double> RS[2], OS[2];
    for (int h = 0; h < 2; ++h) { RS[h].assign(n * 9, 0.0); OS[h].assign(n * 3, 0.0); }
    const double cellV[3] = { sizeV[0] / Rv[0], sizeV[1] / Rv[1], sizeV[2] / Rv[2] };
    for (size_t bi = 0; bi < boxes.size(); ++bi)
        for (int b = 0; b < 3; ++b)
            for (int side = 0; side < 2; ++side) {
                const Box &bx = boxes[bi];
                const double f = side ? bx.hi[b] : bx.lo[b];
                const int half = side ? kP : kN;
                const Rgb L = rad(int(bi), b, side);
                const int k = int(std::floor((f - org[b]) / cellV[b]));
                if (k < 0 || k >= Rv[b]) continue;
                const int u = (b + 1) % 3, v = (b + 2) % 3;
                const int i0 = std::max(0, int(std::floor((bx.lo[u] - org[u]) / cellV[u]))),
                          i1 = std::min(Rv[u] - 1, int(std::floor((bx.hi[u] - org[u]) / cellV[u])));
                const int j0 = std::max(0, int(std::floor((bx.lo[v] - org[v]) / cellV[v]))),
                          j1 = std::min(Rv[v] - 1, int(std::floor((bx.hi[v] - org[v]) / cellV[v])));
                for (int i = i0; i <= i1; ++i)
                    for (int j = j0; j <= j1; ++j) {
                        const double ou = std::min(bx.hi[u], org[u] + (i + 1) * cellV[u]) - std::max(bx.lo[u], org[u] + i * cellV[u]);
                        const double ov = std::min(bx.hi[v], org[v] + (j + 1) * cellV[v]) - std::max(bx.lo[v], org[v] + j * cellV[v]);
                        if (ou <= 0 || ov <= 0) continue;
                        int idx[3];
                        idx[b] = k; idx[u] = i; idx[v] = j;
                        const size_t id = (size_t(idx[2]) * Rv[1] + idx[1]) * Rv[0] + idx[0];
                        const double cov = ou * ov / (cellV[u] * cellV[v]);
                        OS[half][id * 3 + b] += cov;
                        RS[half][id * 9 + b * 3 + 0] += cov * L.r;
                        RS[half][id * 9 + b * 3 + 1] += cov * L.g;
                        RS[half][id * 9 + b * 3 + 2] += cov * L.b;
                    }
            }
    for (int h = 0; h < 2; ++h) lc.rad0[h].assign(n * 9, 0.f);
    lc.c0.assign(n, 0.f);
    std::vector<float> li0(n * 4, 0.f);
    for (size_t i = 0; i < n; ++i) {
        double sum[3] = { 0, 0, 0 }, w = 0, cmax = 0;
        for (int h = 0; h < 2; ++h)
            for (int a = 0; a < 3; ++a) {
                w += OS[h][i * 3 + a];
                for (int k = 0; k < 3; ++k) sum[k] += RS[h][i * 9 + a * 3 + k];
                cmax = std::max(cmax, double(lc.c.L[0].O[h][i * 3 + a]));
            }
        const double mean[3] = { w > 0 ? sum[0] / w : 0, w > 0 ? sum[1] / w : 0, w > 0 ? sum[2] / w : 0 };
        // THE TWO GROUPS (kPerSide): U the faces looking +a (the fold leaves them), F those looking
        // -a; their normal sums nU = (OS+), nF = -(OS-); two-sided when both are non-empty and
        // their means are more than 120 degrees apart (VoxelMerge's test); the stored normal the
        // larger group's mean (two-sided) or the raw sum.
        double nU[3], nF[3], gU[3] = { 0, 0, 0 }, gF[3] = { 0, 0, 0 }, wU = 0, wF = 0;
        for (int a = 0; a < 3; ++a) {
            nU[a] = OS[kP][i * 3 + a]; nF[a] = -OS[kN][i * 3 + a];
            wU += OS[kP][i * 3 + a]; wF += OS[kN][i * 3 + a];
            for (int k = 0; k < 3; ++k) { gU[k] += RS[kP][i * 9 + a * 3 + k]; gF[k] += RS[kN][i * 9 + a * 3 + k]; }
        }
        const double dUF = nU[0] * nF[0] + nU[1] * nF[1] + nU[2] * nF[2];
        const double lU = nU[0] * nU[0] + nU[1] * nU[1] + nU[2] * nU[2], lF = nF[0] * nF[0] + nF[1] * nF[1] + nF[2] * nF[2];
        const bool twoSided = wU > 0 && wF > 0 && dUF < 0 && dUF * dUF > 0.25 * lU * lF;
        const bool frontU = lU >= lF;
        double nv[3], front[3], back[3];
        for (int a = 0; a < 3; ++a) nv[a] = twoSided ? (frontU ? nU[a] : nF[a]) : nU[a] + nF[a];
        for (int k = 0; k < 3; ++k) {
            const double mU = wU > 0 ? gU[k] / wU : 0, mF = wF > 0 ? gF[k] / wF : 0;
            front[k] = twoSided ? (frontU ? mU : mF) : mean[k];
            back[k] = twoSided ? (frontU ? mF : mU) : mean[k];
        }
        for (int h = 0; h < 2; ++h)
            for (int a = 0; a < 3; ++a)
                for (int k = 0; k < 3; ++k) {
                    const double o = OS[h][i * 3 + a];
                    double v = mean[k];
                    if (model == kPerFace) v = o > 0 ? RS[h][i * 9 + a * 3 + k] / o : 0.0;
                    else if (model == kPerSide) {
                        const double s = (h == kP ? 1.0 : -1.0) * nv[a];
                        v = s > 0 ? front[k] : (s < 0 ? back[k] : 0.5 * (front[k] + back[k]));
                    }
                    lc.rad0[h][i * 9 + a * 3 + k] = float(v);
                }
        for (int k = 0; k < 3; ++k) li0[i * 4 + k] = float(mean[k] * cmax);
        li0[i * 4 + 3] = float(cmax);
        lc.c0[i] = float(cmax);
    }
    lc.rad0L[0] = lc.rad0[0]; lc.rad0L[1] = lc.rad0[1];
    finishLit(lc, li0);
    return lc;
}

/// A trilinear rgba fetch of per-level data (`rgbaPerLevel` 4 floats a texel on the levels of
/// `c.L`, or the directional levels with alpha from D) at the fractional level `Lf`.
inline void fetchIsoLight(const LitCascade &lc, double Lf, const double u[3], double out[4])
{
    for (int k = 0; k < 4; ++k) out[k] = 0;
    const int maxL = int(lc.Li.size()) - 1;
    Lf = std::min(std::max(Lf, 0.0), double(maxL));
    const int l0 = int(std::floor(Lf));
    const double fl = Lf - l0;
    for (int s = 0; s < 2; ++s) {
        const double ws = s == 0 ? 1 - fl : fl;
        if (ws <= 0) continue;
        const int L = std::min(l0 + s, maxL);
        const Level &l = lc.c.L[size_t(L)];
        double f[3]; int i0[3];
        for (int a = 0; a < 3; ++a) { const double g = u[a] * l.r[a] - 0.5; i0[a] = int(std::floor(g)); f[a] = g - i0[a]; }
        for (int q = 0; q < 8; ++q) {
            const int ix = std::min(std::max(i0[0] + (q & 1), 0), l.r[0] - 1);
            const int iy = std::min(std::max(i0[1] + ((q >> 1) & 1), 0), l.r[1] - 1);
            const int iz = std::min(std::max(i0[2] + ((q >> 2) & 1), 0), l.r[2] - 1);
            const double w = ws * ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) * (((q >> 2) & 1) ? f[2] : 1 - f[2]);
            for (int k = 0; k < 4; ++k) out[k] += w * lc.Li[size_t(L)][((size_t(iz) * l.r[1] + iy) * l.r[0] + ix) * 4 + k];
        }
    }
}
inline void fetchDirLight(const LitCascade &lc, double mf, int d, const double u[3], double out[4])
{
    for (int k = 0; k < 4; ++k) out[k] = 0;
    const int maxM = int(lc.c.D.size()) - 1;
    if (maxM < 0) return;
    mf = std::min(std::max(mf, 0.0), double(maxM));
    const int m0 = int(std::floor(mf));
    const double fl = mf - m0;
    for (int s = 0; s < 2; ++s) {
        const double ws = s == 0 ? 1 - fl : fl;
        if (ws <= 0) continue;
        const int m = std::min(m0 + s, maxM);
        const DirLevel &l = lc.c.D[size_t(m)];
        double f[3]; int i0[3];
        for (int a = 0; a < 3; ++a) { const double g = u[a] * l.r[a] - 0.5; i0[a] = int(std::floor(g)); f[a] = g - i0[a]; }
        for (int q = 0; q < 8; ++q) {
            const int ix = std::min(std::max(i0[0] + (q & 1), 0), l.r[0] - 1);
            const int iy = std::min(std::max(i0[1] + ((q >> 1) & 1), 0), l.r[1] - 1);
            const int iz = std::min(std::max(i0[2] + ((q >> 2) & 1), 0), l.r[2] - 1);
            const double w = ws * ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) * (((q >> 2) & 1) ? f[2] : 1 - f[2]);
            const size_t o = (size_t(iz) * l.r[1] + iy) * l.r[0] + ix;
            for (int k = 0; k < 3; ++k) out[k] += w * lc.Dc[size_t(m)][size_t(d)][o * 3 + k];
            out[3] += w * l.a[d][o];
        }
    }
}

/// LEVEL 0 BY THE HALF (kPerSide / kPerFace): half h's light along axis a at `u`, premultiplied
/// by the voxel's c and trilinear - the store's two level-0 volumes picked per texel by the
/// half - over the trilinear c: the mean radiance the plane read multiplies by its opacity.
inline void fetchHalfLight(const LitCascade &lc, int h, int a, const double u[3], double rgb[3])
{
    const Level &l = lc.c.L[0];
    double f[3], acc[4] = { 0, 0, 0, 0 };
    int i0[3];
    for (int k = 0; k < 3; ++k) { const double g = u[k] * l.r[k] - 0.5; i0[k] = int(std::floor(g)); f[k] = g - i0[k]; }
    for (int q = 0; q < 8; ++q) {
        const int ix = std::min(std::max(i0[0] + (q & 1), 0), l.r[0] - 1);
        const int iy = std::min(std::max(i0[1] + ((q >> 1) & 1), 0), l.r[1] - 1);
        const int iz = std::min(std::max(i0[2] + ((q >> 2) & 1), 0), l.r[2] - 1);
        const double w = ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) * (((q >> 2) & 1) ? f[2] : 1 - f[2]);
        const size_t o = (size_t(iz) * l.r[1] + iy) * l.r[0] + ix;
        const double c = lc.c0[o];
        for (int k = 0; k < 3; ++k) acc[k] += w * c * lc.rad0L[h][o * 9 + a * 3 + k];
        acc[3] += w * c;
    }
    for (int k = 0; k < 3; ++k) rgb[k] = acc[3] > 0 ? acc[k] / acc[3] : 0.0;
}

struct ResultC { Result r; double col[3] = { 0, 0, 0 }; };

/// ONE CASCADE of the shipped march with its COLOUR (jahVoxelReadPlane / jahConeMarchCascade),
/// on the anisotropic tier - the shipped rules only (no lab candidates).
inline ResultC marchCascadeC(const LitCascade &lc, const double pos[3], const double d[3], double tanA, double startLod,
                             double startAlpha, double startTrav, const Origin &org)
{
    const Cascade &c = lc.c;
    const bool ray = !(tanA > 0.0);
    const double invRes[3] = { 1.0 / c.R[0], 1.0 / c.R[1], 1.0 / c.R[2] };
    const double vInv = std::fabs(d[0]) * invRes[0] + std::fabs(d[1]) * invRes[1] + std::fabs(d[2]) * invRes[2];
    const double res = 1.0 / vInv;
    const double pl[3] = { std::fabs(d[0]) / invRes[0], std::fabs(d[1]) / invRes[1], std::fabs(d[2]) / invRes[2] };
    const int axis = pl[0] >= pl[1] ? (pl[0] >= pl[2] ? 0 : 2) : (pl[1] >= pl[2] ? 1 : 2);
    const double da = d[axis], sgn = da > 0 ? 1.0 : -1.0, invAbsDa = 1.0 / std::fabs(da), startA = pos[axis];
    const double oLS = org.n >= 0 ? (org.world - c.origin[org.n]) / c.size[org.n] : 0.0;
    const auto inside = [](const double p[3]) { for (int a = 0; a < 3; ++a) if (p[a] < 0.0 || p[a] > 1.0) return false; return true; };
    const auto plane = [&](double b, double T, double &nr, double &fr) {
        const double kk = sgn > 0 ? std::floor(b / T + 1e-4) : std::ceil(b / T - 1e-4) - 1.0;
        const double lo = kk * T; nr = sgn > 0 ? lo : lo + T; fr = sgn > 0 ? lo + T : lo;
    };
    const auto at = [&](double coordA, double p[3]) {
        const double t = (coordA - startA) * sgn * invAbsDa;
        for (int a = 0; a < 3; ++a) p[a] = pos[a] + t * d[a];
    };
    const auto originGate = [&](int b, double o, double pO) {
        if (b != org.n || !(o > 0.0)) return 1.0;
        return tentCdf(((pO / o - oLS) * org.sgn - invRes[b]) / (0.5 * invRes[b]));
    };
    ResultC R;
    Result &r = R.r;
    r.alpha = startAlpha; r.lod = startLod;
    double readTo = startA, prevMip = -1; bool prevDir = false;
    bool done = !inside(pos) || da == 0.0;
    for (int steps = 0; !done && r.alpha < 0.95 && steps < 256; ++steps) {
        const double tRead = (readTo - startA) * sgn * invAbsDa;
        const double trav = std::max(startTrav + tRead, vInv);
        r.lod = std::max(startLod, std::log2(std::max(vInv, 2.0 * tanA * trav) * res));
        if (r.lod >= c.maxLod) break;
        bool dir = r.lod > 0.5;
        double tc = dir ? 2.0 : 1.0;
        double mip = !dir ? 0.0 : std::floor(std::max(r.lod - 1.0 - std::log2(tc), 0.0));
        double T = tc * std::exp2(mip) * invRes[axis], nr, fr;
        plane(readTo, T, nr, fr);
        if (prevMip >= 0 && std::fabs(nr - readTo) > 1e-4 * T && (dir != prevDir || mip != prevMip)) {
            dir = prevDir; tc = dir ? 2.0 : 1.0; mip = prevMip; T = tc * std::exp2(mip) * invRes[axis]; plane(readTo, T, nr, fr);
        }
        prevDir = dir; prevMip = mip;
        double cp[3];
        at(0.5 * (nr + fr), cp);
        done = !inside(cp);
        for (int a = 0; a < 3; ++a) cp[a] = std::min(std::max(cp[a], 0.0), 1.0);
        const double w = std::fabs(fr - readTo) / T;
        const double lenLS = w * T * invAbsDa;
        double x = 0.0, col[3] = { 0, 0, 0 };
        {
            const int h = sgn > 0 ? kN : kP;
            const double Lc = dir ? mip + 1.0 : mip;
            double O[3], P[3];
            fetch(c, selO, h, Lc, cp, O);
            fetch(c, selP, h, Lc, cp, P);
            double aA, cA[3];
            if (dir) {
                double v[4];
                fetchDirLight(lc, mip, 2 * axis + (sgn > 0 ? 0 : 1), cp, v);
                aA = v[3];
                for (int k = 0; k < 3; ++k) cA[k] = v[k];
            } else {
                // a ray takes both halves (any face it crosses stops it), a cone the one looking back
                double o = O[axis];
                if (ray) { double O2[3]; fetch(c, selO, 1 - h, Lc, cp, O2); o = std::min(1.0, o + O2[axis]); }
                aA = o * std::exp2(mip);
                if (lc.model == kOnePerVoxel) {
                    double s[4];
                    fetchIsoLight(lc, mip, cp, s);
                    for (int k = 0; k < 3; ++k) cA[k] = s[3] > 0 ? s[k] * (aA / s[3]) : 0.0;
                } else {
                    double rgb[3];
                    fetchHalfLight(lc, h, axis, cp, rgb);
                    for (int k = 0; k < 3; ++k) cA[k] = rgb[k] * aA;
                }
            }
            double g = 1.0;
            if (w < 1.0 - 1e-9 && firstPos && O[axis] > 0.0) {
                const double pa = P[axis] / O[axis], hh = 0.5 * invRes[axis];
                g = tentCdf((pa - readTo) * sgn / hh);
            }
            const double gA = originGate(axis, O[axis], P[axis]) * g;
            const double kA = w < 1.0 ? gA : gA * w;
            x += kA * aA;
            for (int k = 0; k < 3; ++k) col[k] += kA * cA[k];
        }
        const double lk = std::max(std::max(r.lod - 1.0, 0.0), gSpan ? std::log2(tc) + mip : 0.0);
        double kp[3];
        at(0.5 * (readTo + fr), kp);
        for (int a2 = 0; a2 < 3; ++a2) kp[a2] = std::min(std::max(kp[a2], 0.0), 1.0);
        double OB[3] = { 0, 0, 0 };
        if (ray) {
            double O0[3], O1[3];
            fetch(c, selO, 0, lk, kp, O0); fetch(c, selO, 1, lk, kp, O1);
            for (int b = 0; b < 3; ++b) OB[b] = std::min(1.0, O0[b] + O1[b]);
        }
        for (int h = 0; h < (ray ? 1 : 2); ++h) {
            double O[3], P[3];
            fetch(c, selO, h, lk, kp, O);
            fetch(c, selP, h, lk, kp, P);
            if (ray) for (int b = 0; b < 3; ++b) O[b] = OB[b];
            for (int b = 0; b < 3; ++b) {
                if (b == axis || !(O[b] > 0.0)) continue;
                // rays heading +b meet the faces looking -b (half N), rays heading -b the +b ones;
                // a ray takes both halves at |d_b| (and the texel read by -b travel: the GPU's h 0)
                const double cdir = ray ? std::fabs(d[b]) : (h == kN ? d[b] : -d[b]);
                const double rate = coneRate(cdir, tanA * std::sqrt(std::max(0.0, 1.0 - d[b] * d[b])));
                const double xb = originGate(b, O[b], P[b]) * O[b] * rate * lenLS / invRes[b];
                if (!(xb > 0)) continue;
                double v[4];
                fetchDirLight(lc, std::max(lk - 1.0, 0.0), 2 * b + (h == kN ? 0 : 1), kp, v);
                x += xb;
                for (int k = 0; k < 3; ++k) col[k] += xb * (v[3] > 0 ? v[k] / v[3] : 0.0);
            }
        }
        const double take = std::min(x, 1.0 - r.alpha);
        for (int k = 0; k < 3; ++k) R.col[k] += x > 0 ? col[k] * (take / x) : 0.0;
        r.alpha += take;
        readTo = fr;
        if (sgn > 0 ? readTo >= 1.0 : readTo <= 0.0) done = true;
    }
    const double tEnd = (readTo - startA) * sgn * invAbsDa;
    for (int a = 0; a < 3; ++a) r.posLS[a] = pos[a] + tEnd * d[a];
    r.trav = std::max(startTrav + tEnd, vInv);
    return R;
}

/// THE WALK with colour over a lit chain, from a surface with an axis-aligned normal (axis `n`,
/// sign `sg`): the pixel's start (one cell of cascade 0 along the normal), the origin plane at
/// the surface, the contiguous hop. Returns rgb + alpha; `lastCascade` the cascade it stopped in.
inline ResultC walkNC(const std::vector<LitCascade> &ch, const double pw[3], int n, double sg, const double dirW[3],
                      double tanA, int &lastCascade)
{
    const Cascade &c0 = ch[0].c;
    double d[3], l = 0;
    for (int a = 0; a < 3; ++a) { d[a] = dirW[a] / c0.size[a]; l += d[a] * d[a]; }
    l = std::sqrt(l);
    for (int a = 0; a < 3; ++a) d[a] /= l;
    double p[3];
    for (int a = 0; a < 3; ++a) p[a] = (pw[a] - c0.origin[a]) / c0.size[a];
    p[n] += sg / c0.R[n];
    Origin org; org.n = n; org.world = pw[n]; org.sgn = sg;
    double N[3] = { 0, 0, 0 }; N[n] = sg;
    double dw[3], lw = 0; for (int a = 0; a < 3; ++a) lw += dirW[a] * dirW[a]; lw = std::sqrt(lw);
    for (int a = 0; a < 3; ++a) dw[a] = dirW[a] / lw;
    const double a0 = gBelow ? coneBelow(dw, N, tanA) : 0.0;
    ResultC R = marchCascadeC(ch[0], p, d, tanA, 0, a0, 0, org);
    lastCascade = 0;
    for (size_t j = 1; j < ch.size() && R.r.alpha < 0.95; ++j) {
        const Cascade &pc = ch[j - 1].c, &c = ch[j].c;
        double np[3], hop = 0;
        for (int a = 0; a < 3; ++a) {
            const double scale = pc.size[a] / c.size[a];
            np[a] = R.r.posLS[a] * scale + (pc.origin[a] - c.origin[a]) / c.size[a];
            hop += std::fabs(d[a]) * scale;
        }
        ResultC N2 = marchCascadeC(ch[j], np, d, tanA, std::max(R.r.lod - pc.maxLod, 0.0), R.r.alpha, R.r.trav * hop, org);
        for (int k = 0; k < 3; ++k) R.col[k] += N2.col[k];
        R.r = N2.r;
        lastCascade = int(j);
    }
    return R;
}

/// ONE PLANE READ WHOLE AT A POINT AS A RAY (aperture 0), no origin plane - jahVoxelSample: the
/// plane axis at the point (directional above lod 0.5, level 0 otherwise), the other two axes
/// through the kernel at the same point. Returns the premultiplied colour and the opacity, capped.
inline void samplePointC(const LitCascade &lc, const double u[3], const double d[3], double lod, double rgb[3], double &alpha)
{
    const Cascade &c = lc.c;
    const double invRes[3] = { 1.0 / c.R[0], 1.0 / c.R[1], 1.0 / c.R[2] };
    const double pl[3] = { std::fabs(d[0]) / invRes[0], std::fabs(d[1]) / invRes[1], std::fabs(d[2]) / invRes[2] };
    const int axis = pl[0] >= pl[1] ? (pl[0] >= pl[2] ? 0 : 2) : (pl[1] >= pl[2] ? 1 : 2);
    const double sgn = d[axis] > 0 ? 1.0 : -1.0;
    const bool dir = lod > 0.5 && !c.D.empty();
    const double tc = dir ? 2.0 : 1.0;
    const double mip = !dir ? 0.0 : std::floor(std::max(lod - 1.0 - std::log2(tc), 0.0));
    const double lenLS = tc * std::exp2(mip) * invRes[axis] / std::fabs(d[axis]);
    const int h = sgn > 0 ? kN : kP;
    double x = 0, col[3] = { 0, 0, 0 };
    if (dir) {
        double v[4];
        fetchDirLight(lc, mip, 2 * axis + (sgn > 0 ? 0 : 1), u, v);
        x = v[3];
        for (int k = 0; k < 3; ++k) col[k] = v[k];
    } else {
        double O[3], O2[3];
        fetch(c, selO, h, 0.0, u, O);
        fetch(c, selO, 1 - h, 0.0, u, O2);
        x = std::min(1.0, O[axis] + O2[axis]);
        if (lc.model == kOnePerVoxel) {
            double s[4];
            fetchIsoLight(lc, 0.0, u, s);
            for (int k = 0; k < 3; ++k) col[k] = s[3] > 0 ? s[k] * (x / s[3]) : 0.0;
        } else {
            double r3[3];
            fetchHalfLight(lc, h, axis, u, r3);
            for (int k = 0; k < 3; ++k) col[k] = r3[k] * x;
        }
    }
    const double lk = std::max(std::max(lod - 1.0, 0.0), std::log2(tc) + mip);
    double O0[3], O1[3];
    fetch(c, selO, 0, lk, u, O0); fetch(c, selO, 1, lk, u, O1);
    for (int b = 0; b < 3; ++b) {
        if (b == axis) continue;
        const double ob = std::min(1.0, O0[b] + O1[b]);
        if (!(ob > 0)) continue;
        const double xb = ob * std::fabs(d[b]) * lenLS / invRes[b];
        double v[4];
        fetchDirLight(lc, std::max(lk - 1.0, 0.0), 2 * b + 1, u, v);
        x += xb;
        for (int k = 0; k < 3; ++k) col[k] += xb * (v[3] > 0 ? v[k] / v[3] : 0.0);
    }
    alpha = x;
    for (int k = 0; k < 3; ++k) rgb[k] = x > 1.0 ? col[k] / x : col[k];
    if (x > 1.0) alpha = 1.0;
}

/// THE RAY'S HIT READ (jahVoxelRadiance, jah_rq_hit.glsl): per cascade from the finest, the two
/// texels either side of the world hit along the ray's dominant axis read whole (the larger
/// opacity answers), the six-cell hand-over band; the radiance (rgb over its opacity). `ok`
/// false when no cascade holds it.
inline void hitReadC(const std::vector<LitCascade> &ch, const double hitW[3], const double dirW[3], double lod,
                     double out[3], bool &ok)
{
    double acc[3] = { 0, 0, 0 }, accW = 0;
    ok = false;
    for (const LitCascade &lc : ch) {
        if (accW >= 0.999) break;
        const Cascade &c = lc.c;
        double d[3], l = 0, ls[3];
        for (int a = 0; a < 3; ++a) { d[a] = dirW[a] / c.size[a]; l += d[a] * d[a]; }
        l = std::sqrt(l);
        for (int a = 0; a < 3; ++a) d[a] /= l;
        bool in = true;
        for (int a = 0; a < 3; ++a) {
            ls[a] = (hitW[a] + dirW[a] * 0.5 * c.cell[a] - c.origin[a]) / c.size[a];
            if (ls[a] < 0 || ls[a] > 1) in = false;
        }
        if (!in) continue;
        const double ir[3] = { 1.0 / c.R[0], 1.0 / c.R[1], 1.0 / c.R[2] };
        const double pl[3] = { std::fabs(d[0]) / ir[0], std::fabs(d[1]) / ir[1], std::fabs(d[2]) / ir[2] };
        const int ax = pl[0] >= pl[1] ? (pl[0] >= pl[2] ? 0 : 2) : (pl[1] >= pl[2] ? 1 : 2);
        const double at = (hitW[ax] - c.origin[ax]) / c.size[ax];
        const double k = std::floor(at / ir[ax]);
        const double kO = (at / ir[ax] - k) < 0.5 ? k - 1.0 : k + 1.0;
        double lsA[3] = { ls[0], ls[1], ls[2] }, lsB[3] = { ls[0], ls[1], ls[2] };
        lsA[ax] = (k + 0.5) * ir[ax]; lsB[ax] = (kO + 0.5) * ir[ax];
        double cA[3], cB[3], aA, aB;
        samplePointC(lc, lsA, d, lod, cA, aA);
        samplePointC(lc, lsB, d, lod, cB, aB);
        const double *cc = aB > aA ? cB : cA;
        const double a = aB > aA ? aB : aA;
        if (!(a > 0.02)) continue;
        double e = 1e9;
        for (int q = 0; q < 3; ++q) e = std::min(e, std::min(ls[q], 1.0 - ls[q]));
        const double cellLS = std::max(std::max(ir[0], ir[1]), ir[2]);
        double w = std::min(1.0, std::max(0.0, e / (6.0 * cellLS)));
        w = std::min(w, 1.0 - accW);
        if (w <= 0) continue;
        for (int q = 0; q < 3; ++q) acc[q] += cc[q] / a * w;
        accW += w;
        ok = true;
    }
    for (int q = 0; q < 3; ++q) out[q] = ok ? acc[q] / std::max(accW, 1e-6) : 0.0;
}

/// A RAY (aperture 0) from a world point in free space - a field probe's: no origin plane, no
/// start bias, the contiguous hop. Returns rgb + alpha.
inline ResultC walkRayC(const std::vector<LitCascade> &ch, const double pw[3], const double dirW[3])
{
    const Cascade &c0 = ch[0].c;
    double d[3], l = 0;
    for (int a = 0; a < 3; ++a) { d[a] = dirW[a] / c0.size[a]; l += d[a] * d[a]; }
    l = std::sqrt(l);
    for (int a = 0; a < 3; ++a) d[a] /= l;
    double p[3];
    for (int a = 0; a < 3; ++a) p[a] = (pw[a] - c0.origin[a]) / c0.size[a];
    Origin org;
    ResultC R = marchCascadeC(ch[0], p, d, 0.0, 0, 0, 0, org);
    for (size_t j = 1; j < ch.size() && R.r.alpha < 0.95; ++j) {
        const Cascade &pc = ch[j - 1].c, &c = ch[j].c;
        double np[3], hop = 0;
        for (int a = 0; a < 3; ++a) {
            const double scale = pc.size[a] / c.size[a];
            np[a] = R.r.posLS[a] * scale + (pc.origin[a] - c.origin[a]) / c.size[a];
            hop += std::fabs(d[a]) * scale;
        }
        ResultC N2 = marchCascadeC(ch[j], np, d, 0.0, std::max(R.r.lod - pc.maxLod, 0.0), R.r.alpha, R.r.trav * hop, org);
        for (int k = 0; k < 3; ++k) R.col[k] += N2.col[k];
        R.r = N2.r;
    }
    return R;
}

// ============================================================================================
// THE STORE LIT BY LAMPS, AS THE INJECTION BUILDS IT (PHOTON-VOXEL-5 item (ii), the fused form).
// The injection's own lighting per voxel - one normal, the shadow march over the coverage with
// the origin plane (LightInjection_piece_cs.any) - against the ideal (each face piece lit at its
// own centroid, exact visibility against the boxes):
//   kInjOneLight  the store before (ii): one light, |N.L| for a two-sided voxel;
//   kInjTwoSides  front/back by the canonicalised normal, every level (the design refuted at
//                 the corners: lab2);
//   kInjFused     THE FUSED FORM: the directional level 0 composited from each child's light PER
//                 HALF-AXIS (its own cosine, its own shadow march from its own face's plane), level
//                 0 as the rays read it two-sided (front/back by the canonicalised normal);
//   kInjIdeal     per face piece, exact.
// `cosGroup`: the half's cosine from its normal GROUP's mean normal (U the faces looking +a, F
// -a; VoxelMerge's two sums) - else the axis normal.
enum InjModel { kInjOneLight = 0, kInjTwoSides = 1, kInjFused = 2, kInjIdeal = 3, kInjFusedGpu = 4 };

struct Lamp { bool directional = false; double pos[3] = { 0, 0, 0 }; double toLight[3] = { 0, 1, 0 }; Rgb col; };

/// A fixed generic direction: a two-sided voxel's FRONT is the side whose normal has a positive
/// dot with it - VoxelMerge's direction; its comment states the seam check.
inline double canonDot(const double n[3]) { return 0.8925 * n[0] + 0.4166 * n[1] + 0.1726 * n[2]; }

inline bool segBlocked(const double p[3], const double q[3], const std::vector<Box> &boxes)
{
    double d[3];
    for (int a = 0; a < 3; ++a) d[a] = q[a] - p[a];
    for (const Box &b : boxes) {
        double t0 = 1e-9, t1 = 1.0 - 1e-9;
        bool hit = true;
        for (int a = 0; a < 3 && hit; ++a) {
            if (std::fabs(d[a]) < 1e-15) { if (p[a] <= b.lo[a] || p[a] >= b.hi[a]) hit = false; continue; }
            double ta = (b.lo[a] - p[a]) / d[a], tb = (b.hi[a] - p[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 >= t1) hit = false;
        }
        if (hit) return true;
    }
    return false;
}

/// THE RETIRED STEPPED SHADOW MARCH (LightInjection_piece_cs.any before the DDA): from the voxel's centre one cell a
/// step toward the lamp, both halves' coverage capped, crossed per axis by the cells the step
/// crosses, the origin plane (axis `oAxis`, coordinate `oCoord` normalised, sign `oSign`) never
/// counted; to the box's edge or past a point lamp. Returns the transmittance.
/// Lab switch, THE SHIPPED START by default: the fused form's per-half march starts on the half's
/// face (its stored position); false = the RETIRED start at the voxel's centre (the `...stepped` arms).
static bool gStartOnFace = true;


/// Lab switch: THE EXACT MARCH - a 3D-DDA over the level-0 voxels from the start toward the lamp; in
/// each voxel every half's surface plane (its stored position along its axis) that the segment
/// inside the voxel crosses stops the ray by that half's coverage (either side, like the shipped
/// march's both halves); the origin plane never. Nearest texels, no filtering. THE SHIPPED MARCH by
/// default; false = the RETIRED stepped march above (the `...stepped` arms).
static bool gMarchDDA = true;

inline double marchVisDDA(const Cascade &c, const double su[3], const Lamp &L, const double sw[3], int oAxis, double oCoord,
                          double oSign)
{
    double d[3];   // in normalised space
    for (int a = 0; a < 3; ++a) d[a] = (L.directional ? L.toLight[a] : L.pos[a] - sw[a]) / c.size[a];
    double tEnd = L.directional ? 1e30 : 1.0;   // the lamp at t = 1 for a point lamp
    for (int a = 0; a < 3; ++a) {   // the box
        if (d[a] > 0) tEnd = std::min(tEnd, (1.0 - su[a]) / d[a]);
        else if (d[a] < 0) tEnd = std::min(tEnd, (0.0 - su[a]) / d[a]);
    }
    int iv[3], stp[3];
    double tMax[3], tDel[3];
    for (int a = 0; a < 3; ++a) {
        iv[a] = std::min(c.R[a] - 1, std::max(0, int(std::floor(su[a] * c.R[a]))));
        stp[a] = d[a] > 0 ? 1 : -1;
        if (d[a] == 0) { tMax[a] = 1e30; tDel[a] = 1e30; continue; }
        const double bnd = (iv[a] + (d[a] > 0 ? 1 : 0)) / double(c.R[a]);
        tMax[a] = (bnd - su[a]) / d[a];
        tDel[a] = 1.0 / (c.R[a] * std::fabs(d[a]));
    }
    const double invRes[3] = { 1.0 / c.R[0], 1.0 / c.R[1], 1.0 / c.R[2] };
    const Level &l0 = c.L[0];
    double occ = 0, tIn = 0;
    for (int it = 0; it < 4096 && occ < 1.0; ++it) {
        const double tOut = std::min(std::min(tMax[0], tMax[1]), std::min(tMax[2], tEnd));
        const size_t id = (size_t(iv[2]) * c.R[1] + iv[1]) * c.R[0] + iv[0];
        for (int b = 0; b < 3; ++b) {
            if (d[b] == 0) continue;
            double take = 0;
            for (int h = 0; h < 2; ++h) {
                const double o = l0.O[h][id * 3 + b];
                if (!(o > 0)) continue;
                const double pp = l0.P[h][id * 3 + b] / o;
                const double tb = (pp - su[b]) / d[b];
                if (!(tb > tIn - 1e-12 && tb <= tOut)) continue;
                if (b == oAxis && tentCdf(((pp - oCoord) * oSign - invRes[b]) / (0.5 * invRes[b])) < 1.0) {
                    take += o * tentCdf(((pp - oCoord) * oSign - invRes[b]) / (0.5 * invRes[b]));
                    continue;
                }
                if (!(tb > 1e-9)) continue;   // a plane through the start is the start's own surface
                take += o;
            }
            occ += std::min(1.0, take);
        }
        if (tOut >= tEnd) break;
        const int a = tMax[0] <= tMax[1] ? (tMax[0] <= tMax[2] ? 0 : 2) : (tMax[1] <= tMax[2] ? 1 : 2);
        iv[a] += stp[a];
        if (iv[a] < 0 || iv[a] >= c.R[a]) break;
        tIn = tMax[a];
        tMax[a] += tDel[a];
    }
    return std::max(0.0, 1.0 - occ);
}

inline double marchVis(const Cascade &c, const double cenUvw[3], const double cenW[3], const Lamp &L, int oAxis,
                       double oCoord, double oSign)
{
    if (gMarchDDA) return marchVisDDA(c, cenUvw, L, cenW, oAxis, oCoord, oSign);
    double ld[3];
    for (int a = 0; a < 3; ++a) ld[a] = L.directional ? L.toLight[a] : L.pos[a] - cenW[a];
    double m = 0;
    for (int a = 0; a < 3; ++a) { ld[a] /= c.cell[a]; m = std::max(m, std::fabs(ld[a])); }
    if (!(m > 0)) return 1.0;
    for (int a = 0; a < 3; ++a) ld[a] /= m;
    const double invRes[3] = { 1.0 / c.R[0], 1.0 / c.R[1], 1.0 / c.R[2] };
    double lamp[3], cur[3];
    for (int a = 0; a < 3; ++a) { lamp[a] = (L.pos[a] - c.origin[a]) / c.size[a]; cur[a] = cenUvw[a] + ld[a] * invRes[a]; }
    double occ = 0, alpha = 1;
    bool reached = false;
    for (int it = 0; it < 4096 && alpha > 0 && !reached; ++it) {
        double O0[3], O1[3], cov[3];
        fetch(c, selO, 0, 0.0, cur, O0);
        fetch(c, selO, 1, 0.0, cur, O1);
        for (int a = 0; a < 3; ++a) cov[a] = std::min(1.0, O0[a] + O1[a]);
        if (oAxis >= 0 && cov[oAxis] > 0) {
            double P0[3], P1[3];
            fetch(c, selP, 0, 0.0, cur, P0);
            fetch(c, selP, 1, 0.0, cur, P1);
            const double oSum = O0[oAxis] + O1[oAxis];
            if (oSum > 0)
                cov[oAxis] *= tentCdf((((P0[oAxis] + P1[oAxis]) / oSum - oCoord) * oSign - invRes[oAxis]) / (0.5 * invRes[oAxis]));
        }
        occ += cov[0] * std::fabs(ld[0]) + cov[1] * std::fabs(ld[1]) + cov[2] * std::fabs(ld[2]);
        alpha = std::max(0.0, 1.0 - occ);
        for (int a = 0; a < 3; ++a) cur[a] += ld[a] * invRes[a];
        for (int a = 0; a < 3; ++a) if (cur[a] <= 0.0 || cur[a] >= 1.0) reached = true;
        if (!L.directional) {
            double dd = 0;
            for (int a = 0; a < 3; ++a) dd += ld[a] * (lamp[a] - cur[a]);
            if (dd <= 0) reached = true;
        }
    }
    return alpha;
}

/// The store of `boxes` (albedo `albedo`) lit by `lamps`, built as `model` says.
inline LitCascade synthLamps(const std::vector<Box> &boxes, const std::vector<Lamp> &lamps, double albedo, int model,
                             bool cosGroup, const double org[3], const double sizeV[3], const int Rv[3])
{
    LitCascade lc;
    lc.model = kPerSide;   // the level-0 read goes through rad0L
    lc.c = synthStore(boxes, false, 0.0, org, sizeV, Rv);
    const Cascade &c = lc.c;
    const size_t n = size_t(Rv[0]) * Rv[1] * Rv[2];
    const double cellV[3] = { c.cell[0], c.cell[1], c.cell[2] };
    // the face pieces: area per half-axis, and (the ideal) their exact light at their centroid
    std::vector<double> OS[2], RS[2];
    for (int h = 0; h < 2; ++h) { OS[h].assign(n * 3, 0.0); RS[h].assign(n * 9, 0.0); }
    for (const Box &bx : boxes)
        for (int b = 0; b < 3; ++b)
            for (int side = 0; side < 2; ++side) {
                const double f = side ? bx.hi[b] : bx.lo[b];
                const int half = side ? kP : kN;
                const int k = int(std::floor((f - org[b]) / cellV[b]));
                if (k < 0 || k >= Rv[b]) continue;
                const int u = (b + 1) % 3, v = (b + 2) % 3;
                const int i0 = std::max(0, int(std::floor((bx.lo[u] - org[u]) / cellV[u]))),
                          i1 = std::min(Rv[u] - 1, int(std::floor((bx.hi[u] - org[u]) / cellV[u])));
                const int j0 = std::max(0, int(std::floor((bx.lo[v] - org[v]) / cellV[v]))),
                          j1 = std::min(Rv[v] - 1, int(std::floor((bx.hi[v] - org[v]) / cellV[v])));
                for (int i = i0; i <= i1; ++i)
                    for (int j = j0; j <= j1; ++j) {
                        const double ulo = std::max(bx.lo[u], org[u] + i * cellV[u]), uhi = std::min(bx.hi[u], org[u] + (i + 1) * cellV[u]);
                        const double vlo = std::max(bx.lo[v], org[v] + j * cellV[v]), vhi = std::min(bx.hi[v], org[v] + (j + 1) * cellV[v]);
                        if (uhi <= ulo || vhi <= vlo) continue;
                        int idx[3];
                        idx[b] = k; idx[u] = i; idx[v] = j;
                        const size_t id = (size_t(idx[2]) * Rv[1] + idx[1]) * Rv[0] + idx[0];
                        const double cov = (uhi - ulo) * (vhi - vlo) / (cellV[u] * cellV[v]);
                        OS[half][id * 3 + b] += cov;
                        if (model != kInjIdeal) continue;
                        double pc[3];
                        pc[b] = f + (side ? 1e-6 : -1e-6); pc[u] = 0.5 * (ulo + uhi); pc[v] = 0.5 * (vlo + vhi);
                        for (const Lamp &L : lamps) {
                            double l[3], ll = 0;
                            for (int a = 0; a < 3; ++a) { l[a] = L.directional ? L.toLight[a] : L.pos[a] - pc[a]; ll += l[a] * l[a]; }
                            ll = std::sqrt(ll);
                            const double cs = (side ? 1.0 : -1.0) * l[b] / ll;
                            if (cs <= 0) continue;
                            double q[3];
                            for (int a = 0; a < 3; ++a) q[a] = L.directional ? pc[a] + l[a] / ll * 1e3 : L.pos[a];
                            if (segBlocked(pc, q, boxes)) continue;
                            const double e = albedo * cs * cov;
                            RS[half][id * 9 + b * 3 + 0] += e * L.col.r;
                            RS[half][id * 9 + b * 3 + 1] += e * L.col.g;
                            RS[half][id * 9 + b * 3 + 2] += e * L.col.b;
                        }
                    }
            }
    for (int h = 0; h < 2; ++h) { lc.rad0[h].assign(n * 9, 0.f); lc.rad0L[h].assign(n * 9, 0.f); }
    lc.c0.assign(n, 0.f);
    std::vector<float> li0(n * 4, 0.f);
    for (int z = 0; z < Rv[2]; ++z) for (int y = 0; y < Rv[1]; ++y) for (int x = 0; x < Rv[0]; ++x) {
        const size_t i = (size_t(z) * Rv[1] + y) * Rv[0] + x;
        double cmax = 0, w = 0;
        for (int h = 0; h < 2; ++h) for (int a = 0; a < 3; ++a) { cmax = std::max(cmax, double(c.L[0].O[h][i * 3 + a])); w += OS[h][i * 3 + a]; }
        if (!(w > 0)) continue;
        lc.c0[i] = float(cmax);
        if (model == kInjIdeal) {
            double mean[3] = { 0, 0, 0 };
            for (int h = 0; h < 2; ++h) for (int a = 0; a < 3; ++a) {
                const double o = OS[h][i * 3 + a];
                for (int k = 0; k < 3; ++k) {
                    const double v = o > 0 ? RS[h][i * 9 + a * 3 + k] / o : 0.0;
                    lc.rad0[h][i * 9 + a * 3 + k] = lc.rad0L[h][i * 9 + a * 3 + k] = float(v);
                    mean[k] += RS[h][i * 9 + a * 3 + k] / w;
                }
            }
            for (int k = 0; k < 3; ++k) li0[i * 4 + k] = float(mean[k] * cmax);
            li0[i * 4 + 3] = float(cmax);
            continue;
        }
        // THE VOXEL'S NORMAL (VoxelMerge): the two groups, two-sided by the 120-degree test
        double nU[3], nF[3];
        for (int a = 0; a < 3; ++a) { nU[a] = OS[kP][i * 3 + a]; nF[a] = -OS[kN][i * 3 + a]; }
        const double lU = nU[0] * nU[0] + nU[1] * nU[1] + nU[2] * nU[2], lF = nF[0] * nF[0] + nF[1] * nF[1] + nF[2] * nF[2];
        const double dUF = nU[0] * nF[0] + nU[1] * nF[1] + nU[2] * nF[2];
        const bool two = lU > 0 && lF > 0 && dUF < 0 && dUF * dUF > 0.25 * lU * lF;
        double nv[3], nl = 0;
        for (int a = 0; a < 3; ++a) { nv[a] = two ? (lU >= lF ? nU[a] : nF[a]) : nU[a] + nF[a]; nl += nv[a] * nv[a]; }
        nl = std::sqrt(nl);
        if (nl > 0) for (double &q : nv) q /= nl;
        double np[3];   // the canonicalised side normal (front)
        const double sg = canonDot(nv) >= 0 ? 1.0 : -1.0;
        for (int a = 0; a < 3; ++a) np[a] = sg * nv[a];
        const double cenUvw[3] = { (x + 0.5) / Rv[0], (y + 0.5) / Rv[1], (z + 0.5) / Rv[2] };
        const double cenW[3] = { org[0] + (x + 0.5) * cellV[0], org[1] + (y + 0.5) * cellV[1], org[2] + (z + 0.5) * cellV[2] };
        double today[3] = { 0, 0, 0 }, front[3] = { 0, 0, 0 }, back[3] = { 0, 0, 0 }, half[2][3][3] = {};
        const double an[3] = { std::fabs(nv[0]), std::fabs(nv[1]), std::fabs(nv[2]) };
        const int dom = an[0] >= an[1] ? (an[0] >= an[2] ? 0 : 2) : (an[1] >= an[2] ? 1 : 2);
        for (const Lamp &L : lamps) {
            double l[3], ll = 0;
            for (int a = 0; a < 3; ++a) { l[a] = L.directional ? L.toLight[a] : L.pos[a] - cenW[a]; ll += l[a] * l[a]; }
            ll = std::sqrt(ll);
            for (double &q : l) q /= ll;
            const double col[3] = { L.col.r, L.col.g, L.col.b };
            // the voxel's own march: the origin on its normal's dominant axis, the half facing the lamp
            {
                int oA = -1; double oC = 0, oS = 1;
                if (l[dom] != 0.0) {
                    const int hh = l[dom] > 0 ? kP : kN;
                    const double ownO = c.L[0].O[hh][i * 3 + dom];
                    if (ownO > 0) { oA = dom; oC = c.L[0].P[hh][i * 3 + dom] / ownO; oS = hh == kP ? 1.0 : -1.0; }
                }
                const double ndl = nv[0] * l[0] + nv[1] * l[1] + nv[2] * l[2];
                const double cT = two ? std::fabs(ndl) : std::max(0.0, ndl);
                const double cF = std::max(0.0, np[0] * l[0] + np[1] * l[1] + np[2] * l[2]);
                const double cB = std::max(0.0, -(np[0] * l[0] + np[1] * l[1] + np[2] * l[2]));
                if (cT > 0 || cF > 0 || cB > 0) {
                    const double vis = marchVis(c, cenUvw, cenW, L, oA, oC, oS);
                    for (int k = 0; k < 3; ++k) {
                        today[k] += albedo * col[k] * cT * vis;
                        front[k] += albedo * col[k] * cF * vis;
                        back[k] += albedo * col[k] * cB * vis;
                    }
                }
            }
            // PER HALF-AXIS (the fused form): the half's own cosine and its own march
            if (model == kInjFused)
                for (int h = 0; h < 2; ++h)
                    for (int a = 0; a < 3; ++a) {
                        const double o = c.L[0].O[h][i * 3 + a];
                        if (!(o > 0)) continue;
                        double cs;
                        if (cosGroup) {
                            const double *g = h == kP ? nU : nF;
                            const double gl = std::sqrt(h == kP ? lU : lF);
                            cs = gl > 0 ? (g[0] * l[0] + g[1] * l[1] + g[2] * l[2]) / gl : 0.0;
                        } else cs = (h == kP ? 1.0 : -1.0) * l[a];
                        if (!(cs > 0)) continue;
                        // the half's march starts ON ITS FACE: the voxel's centre with the half's axis
                        // coordinate moved to the face's stored position
                        double su[3] = { cenUvw[0], cenUvw[1], cenUvw[2] }, sw[3] = { cenW[0], cenW[1], cenW[2] };
                        if (gStartOnFace) {
                            su[a] = c.L[0].P[h][i * 3 + a] / o;
                            sw[a] = org[a] + su[a] * sizeV[a];
                        }
                        const double vis = marchVis(c, su, sw, L, a, c.L[0].P[h][i * 3 + a] / o, h == kP ? 1.0 : -1.0);
                        for (int k = 0; k < 3; ++k) half[h][a][k] += albedo * col[k] * cs * vis;
                    }
        }
        if (model == kInjFusedGpu) {
            // THE GPU PLAN, exactly: per light, every covered half (a, h) takes the cosine of its
            // SIDE normal (the canonical normal np if h np_a > 0, -np if < 0; a one-sided voxel's
            // own normal if h n_a > 0; the axis normal otherwise) and its own exact march from its
            // face; the level-0 sides reuse the dominant axis's halves: front = np.l x vis(dom,
            // sign np_dom), back = -np.l x vis(dom, -sign np_dom).
            for (int k = 0; k < 3; ++k) front[k] = back[k] = 0.0;
            for (int h = 0; h < 2; ++h) for (int a = 0; a < 3; ++a) for (int k = 0; k < 3; ++k) half[h][a][k] = 0.0;
            for (const Lamp &L : lamps) {
                double l[3], ll = 0;
                for (int a = 0; a < 3; ++a) { l[a] = L.directional ? L.toLight[a] : L.pos[a] - cenW[a]; ll += l[a] * l[a]; }
                ll = std::sqrt(ll);
                for (double &q : l) q /= ll;
                const double col[3] = { L.col.r, L.col.g, L.col.b };
                double visH[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } };
                const auto visOf = [&](int h, int a) {
                    if (visH[h][a] >= 0) return visH[h][a];
                    const double o = c.L[0].O[h][i * 3 + a];
                    double su[3] = { cenUvw[0], cenUvw[1], cenUvw[2] }, sw[3] = { cenW[0], cenW[1], cenW[2] };
                    const double pa = o > 0 ? c.L[0].P[h][i * 3 + a] / o : cenUvw[a];
                    su[a] = pa; sw[a] = org[a] + pa * sizeV[a];
                    visH[h][a] = marchVisDDA(c, su, L, sw, o > 0 ? a : -1, pa, h == kP ? 1.0 : -1.0);
                    return visH[h][a];
                };
                for (int h = 0; h < 2; ++h)
                    for (int a = 0; a < 3; ++a) {
                        if (!(c.L[0].O[h][i * 3 + a] > 0)) continue;
                        const double hs = h == kP ? 1.0 : -1.0;
                        double nn[3] = { 0, 0, 0 };
                        if (two) { const double sd = hs * np[a] > 0 ? 1.0 : (hs * np[a] < 0 ? -1.0 : 0.0); for (int q = 0; q < 3; ++q) nn[q] = sd * np[q]; if (sd == 0) nn[a] = hs; }
                        else if (hs * nv[a] > 0) { for (int q = 0; q < 3; ++q) nn[q] = nv[q]; }
                        else nn[a] = hs;
                        const double cs = nn[0] * l[0] + nn[1] * l[1] + nn[2] * l[2];
                        if (!(cs > 0)) continue;
                        const double vis = visOf(h, a);
                        for (int k = 0; k < 3; ++k) half[h][a][k] += albedo * col[k] * cs * vis;
                    }
                const double nfl = np[0] * l[0] + np[1] * l[1] + np[2] * l[2];
                const int hf = np[dom] >= 0 ? kP : kN;
                if (nfl > 0) { const double v = visOf(hf, dom); for (int k = 0; k < 3; ++k) front[k] += albedo * col[k] * nfl * v; }
                if (nfl < 0 && two) { const double v = visOf(1 - hf, dom); for (int k = 0; k < 3; ++k) back[k] += albedo * col[k] * -nfl * v; }
            }
            if (!two) {
                // one side: lit only from in front of its own normal n (np = +-n): front if np = n,
                // else the light np.l < 0 put nowhere - take n.l > 0 with the facing half's march
                if (sg < 0) {
                    for (int k = 0; k < 3; ++k) front[k] = 0.0;
                    for (const Lamp &L : lamps) {
                        double l[3], ll = 0;
                        for (int a = 0; a < 3; ++a) { l[a] = L.directional ? L.toLight[a] : L.pos[a] - cenW[a]; ll += l[a] * l[a]; }
                        ll = std::sqrt(ll);
                        const double ndl = (nv[0] * l[0] + nv[1] * l[1] + nv[2] * l[2]) / ll;
                        if (!(ndl > 0)) continue;
                        const int hh = nv[dom] >= 0 ? kP : kN;
                        const double o = c.L[0].O[hh][i * 3 + dom];
                        double su[3] = { cenUvw[0], cenUvw[1], cenUvw[2] }, sw[3] = { cenW[0], cenW[1], cenW[2] };
                        const double pa = o > 0 ? c.L[0].P[hh][i * 3 + dom] / o : cenUvw[dom];
                        su[dom] = pa; sw[dom] = org[dom] + pa * sizeV[dom];
                        const double v = marchVisDDA(c, su, L, sw, o > 0 ? dom : -1, pa, hh == kP ? 1.0 : -1.0);
                        const double col[3] = { L.col.r, L.col.g, L.col.b };
                        for (int k = 0; k < 3; ++k) front[k] += albedo * col[k] * ndl * v;
                    }
                }
                for (int k = 0; k < 3; ++k) back[k] = front[k];
            }
        } else if (!two) for (int k = 0; k < 3; ++k) front[k] = back[k] = today[k];
        for (int h = 0; h < 2; ++h)
            for (int a = 0; a < 3; ++a)
                for (int k = 0; k < 3; ++k) {
                    const double s = (h == kP ? 1.0 : -1.0) * np[a];
                    const double side = s > 0 ? front[k] : (s < 0 ? back[k] : 0.5 * (front[k] + back[k]));
                    const double lvl0 = model == kInjOneLight ? today[k] : side;
                    lc.rad0L[h][i * 9 + a * 3 + k] = float(lvl0);
                    lc.rad0[h][i * 9 + a * 3 + k] = float(model == kInjFused || model == kInjFusedGpu ? half[h][a][k] : lvl0);
                }
        for (int k = 0; k < 3; ++k)
            li0[i * 4 + k] = float((model == kInjOneLight ? today[k] : 0.5 * (front[k] + back[k])) * cmax);
        li0[i * 4 + 3] = float(cmax);
    }
    finishLit(lc, li0);
    return lc;
}

}   // namespace voxlab
