// THE VOXEL LAB (PHOTON-VOXEL-4): the plane march of jah_voxel_march.glsl / jah_voxel_sample.glsl
// on the CPU, over a store read back from the GPU (giVoxelVolume) or voxelised analytically
// from boxes (SYNTH), against THE CONE-TRACE REFERENCE of the true geometry. Header-only, no
// engine dependency: the gate lab's instrument and the suites' harness (test_gi_voxel_lab.cpp,
// test_gi_ddgi_ambient.cpp).
#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace voxlab {

enum Half { kP = 0, kN = 1 };   // faces looking +a (normal +a) / looking -a

// THE READER'S RULES AND THE LAB'S CANDIDATES. The SHIPPED reader (jah_voxel_sample.glsl /
// jah_voxel_march.glsl, PHOTON-VOXEL-4) is the defaults: the split store read by G7 with the
// cone's crossing rate, the first plane by position, the cone's share below its surface, the
// kernel at the middle of the stretch and never finer than the plane. Every other switch is a
// candidate the gate lab measured and did NOT ship - kept, with its table in the test's
// output, so a later lane starts from the numbers (spikes/photon-voxel-4/EVIDENCE.txt).
static bool firstPos = true;     ///< shipped: the first plane's surface by position (jahVoxelFirstPlane)
static bool gBelow = true;       ///< shipped: the cone's share below its surface (jahConeBelow)
static bool gMid = true;         ///< shipped: the kernel at the middle of the stretch
static bool gSpan = true;        ///< shipped: the kernel never finer than the plane (jahVoxelKernelLevel)
static bool gInteg = false;      ///< candidate: the minor axes' kernel integrated over the stretch
static bool gBox = false;        ///< candidate: ...each texel over its own box (not its hat)
static bool gStartTexel = false; ///< candidate: ...the texel holding the start by position
static bool gLatTile = false;    ///< candidate: the lateral level rises only on its own boundary
static bool gLateral = false;    ///< PHOTON-VOXEL-5 (i): the plane axis read from THE LATERAL-ONLY MIP FAMILY
static bool gLateralMinor = false; ///< ...and the minor axes too (depth = the plane's level, across = the footprint)


struct Level {
    int r[3] = { 1, 1, 1 };
    std::vector<float> O[2], P[2];   // per half: coverage O_a+/- and O-premultiplied position, 3 per texel
    std::vector<float> Acap[2];      // per half: min(1, O 2^level) - the directional texel's crossing opacity
};

/// A directional level (the anisotropic mips): per texel, per travel direction (0 +x, 1 -x,
/// 2 +y, 3 -y, 4 +z, 5 -z), the opacity a reader travelling that way sees crossing the
/// texel's depth - step 0 / step 1's composite (AnisotropicMipVctStep0/1_piece_cs.any).
struct DirLevel { int r[3] = { 1, 1, 1 }; std::vector<float> a[6]; };

/// One texture of THE LATERAL-ONLY MIP FAMILY (PHOTON-VOXEL-5 item (i)): one scalar per texel on
/// its own grid - the depth axis at its level's resolution, the two lateral axes halved per step.
struct Lat { int r[3] = { 1, 1, 1 }; std::vector<float> v; };

struct Cascade {
    int R[3] = { 0, 0, 0 };
    double origin[3] = {}, cell[3] = {}, size[3] = {};
    std::vector<Level> L;
    std::vector<DirLevel> D;   // D[m] on the grid of L[m + 1]
    double maxLod = 256.0;
    /// THE FAMILY (buildLateral): per directional level m and travel direction d, the lateral chain
    /// of the directional opacity - [m][d][k], k = 0 the level itself, each k the lateral 2 x 2 box
    /// mean of k - 1 within the same planes (exact along the axis, averaged across); and per
    /// coverage level L, half h and axis a, the same chains of the coverage and the O-premultiplied
    /// position along a (the gate's), [L][h][a][k].
    std::vector<std::array<std::vector<Lat>, 6>> DLat;
    std::vector<std::array<std::array<std::vector<Lat>, 3>, 2>> OLat, PLat;
};

/// THE DIRECTIONAL COMPOSITE, opacity only: each 2x2x2 block, per travel direction, the four
/// lateral (front, back) pairs along the axis composited additively capped (front first for
/// that travel) and averaged. Level 0 from the coverage of the half that looks back at the
/// travel (travelling +a: the faces looking -a); level m from level m - 1.
/// `rule` 0: that (the shipped); further rules are the lab's candidates.
inline void buildDir(Cascade &c, int rule = 0)
{
    c.D.clear();
    for (size_t m = 0; m + 1 < c.L.size(); ++m) {
        const Level &dst = c.L[m + 1];
        DirLevel dl;
        for (int a = 0; a < 3; ++a) dl.r[a] = dst.r[a];
        const size_t n = size_t(dl.r[0]) * dl.r[1] * dl.r[2];
        int sr[3];
        const std::vector<float> *src[6];
        std::vector<float> k0[6];
        if (m == 0) {
            const Level &l0 = c.L[0];
            for (int a = 0; a < 3; ++a) sr[a] = l0.r[a];
            const size_t sn = size_t(sr[0]) * sr[1] * sr[2];
            for (int d = 0; d < 6; ++d) {
                const int ax = d / 2, half = (d % 2 == 0) ? kN : kP;   // +a travel reads -a faces
                k0[d].resize(sn);
                for (size_t i = 0; i < sn; ++i) k0[d][i] = std::min(1.0f, l0.O[half][i * 3 + ax]);
                src[d] = &k0[d];
            }
        } else {
            for (int a = 0; a < 3; ++a) sr[a] = c.D[m - 1].r[a];
            for (int d = 0; d < 6; ++d) src[d] = &c.D[m - 1].a[d];
        }
        for (int d = 0; d < 6; ++d) {
            const int ax = d / 2;
            const bool pos = d % 2 == 0;
            dl.a[d].assign(n, 0.f);
            for (int z = 0; z < dl.r[2]; ++z) for (int y = 0; y < dl.r[1]; ++y) for (int x = 0; x < dl.r[0]; ++x) {
                double sum = 0;
                const int u = (ax + 1) % 3, v = (ax + 2) % 3;
                for (int lu = 0; lu < 2; ++lu) for (int lv = 0; lv < 2; ++lv) {
                    double k[2];
                    for (int f = 0; f < 2; ++f) {
                        int q[3] = { 2 * x, 2 * y, 2 * z };
                        q[u] += lu; q[v] += lv;
                        q[ax] += pos ? f : 1 - f;   // f = 0 the front child for this travel
                        for (int t = 0; t < 3; ++t) q[t] = std::min(q[t], sr[t] - 1);
                        k[f] = (*src[d])[(size_t(q[2]) * sr[1] + q[1]) * sr[0] + q[0]];
                    }
                    sum += k[0] + std::min(k[1], 1.0 - k[0]);
                }
                dl.a[d][(size_t(z) * dl.r[1] + y) * dl.r[0] + x] = float(sum / 4.0);
            }
        }
        c.D.push_back(dl);
    }
    (void)rule;
}

inline double fetchDir(const Cascade &c, int m, int d, const double u[3]);
inline double fetchDir(const Cascade &c, int m, int d, const double u[3])
{
    m = std::min(m, int(c.D.size()) - 1);
    const DirLevel &l = c.D[size_t(m)];
    double f[3];
    int i0[3];
    for (int a = 0; a < 3; ++a) { const double g = u[a] * l.r[a] - 0.5; i0[a] = int(std::floor(g)); f[a] = g - i0[a]; }
    double out = 0;
    for (int q = 0; q < 8; ++q) {
        int jx = i0[0] + (q & 1), jy = i0[1] + ((q >> 1) & 1), jz = i0[2] + ((q >> 2) & 1);
        const int ix = std::min(std::max(jx, 0), l.r[0] - 1);
        const int iy = std::min(std::max(jy, 0), l.r[1] - 1);
        const int iz = std::min(std::max(jz, 0), l.r[2] - 1);
        const double w = ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) * (((q >> 2) & 1) ? f[2] : 1 - f[2]);
        out += w * l.a[d][(size_t(iz) * l.r[1] + iy) * l.r[0] + ix];
    }
    return out;
}

/// The lateral chain of one scalar field on grid r along axis ax: halve the other two axes per
/// step (a 2 x 2 box within each plane) until both are one texel.
inline std::vector<Lat> lateralChain(const int r0[3], int ax, const std::function<float(size_t)> &at)
{
    std::vector<Lat> ch(1);
    for (int a = 0; a < 3; ++a) ch[0].r[a] = r0[a];
    const size_t n0 = size_t(r0[0]) * r0[1] * r0[2];
    ch[0].v.resize(n0);
    for (size_t i = 0; i < n0; ++i) ch[0].v[i] = at(i);
    const int u = (ax + 1) % 3, v = (ax + 2) % 3;
    while (ch.back().r[u] > 1 || ch.back().r[v] > 1) {
        const Lat &p = ch.back();
        Lat q;
        for (int a = 0; a < 3; ++a) q.r[a] = p.r[a];
        q.r[u] = std::max(1, p.r[u] / 2); q.r[v] = std::max(1, p.r[v] / 2);
        q.v.assign(size_t(q.r[0]) * q.r[1] * q.r[2], 0.f);
        for (int z = 0; z < q.r[2]; ++z) for (int y = 0; y < q.r[1]; ++y) for (int x = 0; x < q.r[0]; ++x) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) {
                int qq[3] = { x, y, z };
                if (q.r[u] < p.r[u]) qq[u] = 2 * qq[u] + (k & 1);
                if (q.r[v] < p.r[v]) qq[v] = 2 * qq[v] + ((k >> 1) & 1);
                for (int a = 0; a < 3; ++a) qq[a] = std::min(qq[a], p.r[a] - 1);
                sum += p.v[(size_t(qq[2]) * p.r[1] + qq[1]) * p.r[0] + qq[0]];
            }
            q.v[(size_t(z) * q.r[1] + y) * q.r[0] + x] = float(sum / 4.0);
        }
        ch.push_back(q);
    }
    return ch;
}

/// Builds THE FAMILY over a cascade's directional levels and coverage chain (buildDir first).
inline void buildLateral(Cascade &c)
{
    c.DLat.clear(); c.OLat.clear(); c.PLat.clear();
    for (size_t m = 0; m < c.D.size(); ++m) {
        std::array<std::vector<Lat>, 6> per;
        for (int d = 0; d < 6; ++d) {
            const std::vector<float> &src = c.D[m].a[d];
            per[size_t(d)] = lateralChain(c.D[m].r, d / 2, [&](size_t i) { return src[i]; });
        }
        c.DLat.push_back(per);
    }
    for (size_t L = 0; L < c.L.size(); ++L) {
        std::array<std::array<std::vector<Lat>, 3>, 2> o, p;
        for (int h = 0; h < 2; ++h)
            for (int a = 0; a < 3; ++a) {
                const Level &l = c.L[L];
                o[size_t(h)][size_t(a)] = lateralChain(l.r, a, [&](size_t i) { return l.O[h][i * 3 + size_t(a)]; });
                p[size_t(h)][size_t(a)] = lateralChain(l.r, a, [&](size_t i) { return l.P[h][i * 3 + size_t(a)]; });
            }
        c.OLat.push_back(o); c.PLat.push_back(p);
    }
}

/// A trilinear fetch of a family chain at the fractional lateral level kf.
inline double fetchLat(const std::vector<Lat> &ch, double kf, const double u[3])
{
    kf = std::min(std::max(kf, 0.0), double(ch.size() - 1));
    const int k0 = int(std::floor(kf));
    const double fl = kf - k0;
    double out = 0;
    for (int s = 0; s < 2; ++s) {
        const double ws = s == 0 ? 1 - fl : fl;
        if (ws <= 0) continue;
        const Lat &l = ch[size_t(std::min(k0 + s, int(ch.size()) - 1))];
        double f[3]; int i0[3];
        for (int a = 0; a < 3; ++a) { const double g = u[a] * l.r[a] - 0.5; i0[a] = int(std::floor(g)); f[a] = g - i0[a]; }
        for (int q = 0; q < 8; ++q) {
            const int ix = std::min(std::max(i0[0] + (q & 1), 0), l.r[0] - 1);
            const int iy = std::min(std::max(i0[1] + ((q >> 1) & 1), 0), l.r[1] - 1);
            const int iz = std::min(std::max(i0[2] + ((q >> 2) & 1), 0), l.r[2] - 1);
            const double w = ws * ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) * (((q >> 2) & 1) ? f[2] : 1 - f[2]);
            out += w * l.v[(size_t(iz) * l.r[1] + iy) * l.r[0] + ix];
        }
    }
    return out;
}

inline void buildChain(Cascade &c)
{
    // the chain stops where the SHORTEST axis reaches one texel (every texel a cube)
    while (c.L.back().r[0] > 1 && c.L.back().r[1] > 1 && c.L.back().r[2] > 1) {
        const Level &p = c.L.back();
        Level q;
        for (int a = 0; a < 3; ++a) q.r[a] = std::max(1, p.r[a] / 2);
        const size_t qn = size_t(q.r[0]) * q.r[1] * q.r[2];
        for (int h = 0; h < 2; ++h) { q.O[h].assign(qn * 3, 0.f); q.P[h].assign(qn * 3, 0.f); }
        for (int z = 0; z < q.r[2]; ++z)
            for (int y = 0; y < q.r[1]; ++y)
                for (int x = 0; x < q.r[0]; ++x) {
                    const size_t d = (size_t(z) * q.r[1] + y) * q.r[0] + x;
                    for (int k = 0; k < 8; ++k) {
                        const int X = std::min(2 * x + (k & 1), p.r[0] - 1), Y = std::min(2 * y + ((k >> 1) & 1), p.r[1] - 1),
                                  Z = std::min(2 * z + ((k >> 2) & 1), p.r[2] - 1);
                        const size_t s = (size_t(Z) * p.r[1] + Y) * p.r[0] + X;
                        for (int h = 0; h < 2; ++h)
                            for (int a = 0; a < 3; ++a) {
                                q.O[h][d * 3 + a] += p.O[h][s * 3 + a] / 8.f;
                                q.P[h][d * 3 + a] += p.P[h][s * 3 + a] / 8.f;
                            }
                    }
                }
        c.L.push_back(q);
    }
    for (size_t li = 0; li < c.L.size(); ++li)
        for (int h = 0; h < 2; ++h) {
            Level &l = c.L[li];
            l.Acap[h].resize(l.O[h].size());
            for (size_t i = 0; i < l.O[h].size(); ++i) l.Acap[h][i] = float(std::min(1.0, double(l.O[h][i]) * std::exp2(double(li))));
        }
}

inline void setHandOver(std::vector<Cascade> &ch)
{
    for (size_t i = 0; i + 1 < ch.size(); ++i) {
        double f = 0;
        for (int a = 0; a < 3; ++a) f = std::max(f, ch[i + 1].cell[a] / ch[i].cell[a]);
        ch[i].maxLod = std::min(std::log2(f), double(ch[i].L.size()));
    }
}

/// A split store from its level 0 (per texel RGBA float, rgb = the three axes): coverage+,
/// coverage-, position+, position- - what GiVoxelVolume reads back and a dump holds. The chain
/// is built as the GPU's autogen mips build it (a 2x2x2 box, the chain stopping at the shortest
/// axis).
inline void fromSplit(Cascade &c, const int dims[3], const float o[3], const float ce[3], const float *cov[2],
                      const float *pos[2])
{
    const size_t n = size_t(dims[0]) * dims[1] * dims[2];
    for (int a = 0; a < 3; ++a) { c.R[a] = dims[a]; c.origin[a] = o[a]; c.cell[a] = ce[a]; c.size[a] = double(ce[a]) * dims[a]; }
    Level l0;
    for (int a = 0; a < 3; ++a) l0.r[a] = dims[a];
    for (int h = 0; h < 2; ++h) {
        l0.O[h].resize(n * 3); l0.P[h].resize(n * 3);
        for (size_t i = 0; i < n; ++i)
            for (int a = 0; a < 3; ++a) { l0.O[h][i * 3 + a] = cov[h][i * 4 + a]; l0.P[h][i * 3 + a] = pos[h][i * 4 + a]; }
    }
    c.L.assign(1, l0);
    c.D.clear();
    buildChain(c);
}

/// A store dump (tests/support/voxeldump.h, JAH_VOXEL_DUMP): dims, origin, cell, multiplier,
/// then per texel RGBA float: coverage+, coverage-, position+, position-.
inline bool loadDump(const std::string &path, Cascade &c)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int dims[3];
    float o[3], ce[3], m;
    bool ok = std::fread(dims, 4, 3, f) == 3 && std::fread(o, 4, 3, f) == 3 && std::fread(ce, 4, 3, f) == 3 &&
              std::fread(&m, 4, 1, f) == 1;
    const size_t n = ok ? size_t(dims[0]) * dims[1] * dims[2] : 0;
    std::vector<float> buf[4];
    for (int i = 0; ok && i < 4; ++i) { buf[i].resize(n * 4); ok = std::fread(buf[i].data(), 4, n * 4, f) == n * 4; }
    std::fclose(f);
    if (!ok) return false;
    const float *cov[2] = { buf[0].data(), buf[1].data() }, *pos[2] = { buf[2].data(), buf[3].data() };
    fromSplit(c, dims, o, ce, cov, pos);
    return true;
}

struct Box { double lo[3], hi[3]; };

/// THE ANALYTIC STORE: every box face's area inside a voxel over the voxel's face, into the
/// half-axis it faces (the box's lo face looks -a, its hi face +a), capped at 1 per half; the
/// position the coverage-weighted face coordinate. `ground`: the plane y = groundY facing +y.
inline Cascade synthStore(const std::vector<Box> &boxes, bool ground, double groundY, const double org[3], const double sizeV[3], const int Rv[3])
{
    Cascade c;
    for (int a = 0; a < 3; ++a) { c.R[a] = Rv[a]; c.origin[a] = org[a]; c.cell[a] = sizeV[a] / Rv[a]; c.size[a] = sizeV[a]; }
    Level l0;
    for (int a = 0; a < 3; ++a) l0.r[a] = Rv[a];
    const size_t n = size_t(Rv[0]) * Rv[1] * Rv[2];
    std::vector<double> O[2], S[2];
    for (int h = 0; h < 2; ++h) { O[h].assign(n * 3, 0.0); S[h].assign(n * 3, 0.0); }
    const double *cellV = c.cell;
    const auto addFace = [&](int b, int half, double f, const double lo[3], const double hi[3]) {
        const int k = int(std::floor((f - org[b]) / cellV[b]));
        if (k < 0 || k >= Rv[b]) return;
        const int u = (b + 1) % 3, v = (b + 2) % 3;
        const int i0 = std::max(0, int(std::floor((lo[u] - org[u]) / cellV[u]))), i1 = std::min(Rv[u] - 1, int(std::floor((hi[u] - org[u]) / cellV[u])));
        const int j0 = std::max(0, int(std::floor((lo[v] - org[v]) / cellV[v]))), j1 = std::min(Rv[v] - 1, int(std::floor((hi[v] - org[v]) / cellV[v])));
        for (int i = i0; i <= i1; ++i)
            for (int j = j0; j <= j1; ++j) {
                const double ou = std::min(hi[u], org[u] + (i + 1) * cellV[u]) - std::max(lo[u], org[u] + i * cellV[u]);
                const double ov = std::min(hi[v], org[v] + (j + 1) * cellV[v]) - std::max(lo[v], org[v] + j * cellV[v]);
                if (ou <= 0 || ov <= 0) continue;
                int idx[3];
                idx[b] = k; idx[u] = i; idx[v] = j;
                const size_t id = (size_t(idx[2]) * Rv[1] + idx[1]) * Rv[0] + idx[0];
                const double cov = ou * ov / (cellV[u] * cellV[v]);
                O[half][id * 3 + b] += cov;
                S[half][id * 3 + b] += cov * (f - org[b]) / sizeV[b];
            }
    };
    for (const Box &bx : boxes)
        for (int b = 0; b < 3; ++b) {
            addFace(b, kN, bx.lo[b], bx.lo, bx.hi);
            addFace(b, kP, bx.hi[b], bx.lo, bx.hi);
        }
    if (ground) {
        const double lo[3] = { org[0], groundY, org[2] }, hi[3] = { org[0] + sizeV[0], groundY, org[2] + sizeV[2] };
        addFace(1, kP, groundY, lo, hi);
    }
    for (int h = 0; h < 2; ++h) {
        l0.O[h].resize(n * 3); l0.P[h].resize(n * 3);
        for (size_t i = 0; i < n * 3; ++i) {
            const double o = O[h][i], pm = o > 0 ? S[h][i] / o : 0.0, oc = std::min(1.0, o);
            l0.O[h][i] = float(oc); l0.P[h][i] = float(oc * pm);
        }
    }
    c.L.assign(1, l0);
    buildChain(c);
    return c;
}

inline void fetch(const Cascade &c, const std::vector<float> &(*sel)(const Level &, int), int h, double Lf, const double u[3], double out[3])
{
    // a trilinear fetch across two levels at the fractional level Lf (the GPU's textureLod)
    out[0] = out[1] = out[2] = 0.0;
    const int maxL = int(c.L.size()) - 1;
    Lf = std::min(std::max(Lf, 0.0), double(maxL));
    const int l0 = int(std::floor(Lf));
    const double fl = Lf - l0;
    for (int s = 0; s < 2; ++s) {
        const double ws = s == 0 ? 1.0 - fl : fl;
        if (ws <= 0.0) continue;
        const Level &l = c.L[size_t(std::min(l0 + s, maxL))];
        const std::vector<float> &v = sel(l, h);
        double f[3];
        int i0[3];
        for (int a = 0; a < 3; ++a) { const double g = u[a] * l.r[a] - 0.5; i0[a] = int(std::floor(g)); f[a] = g - i0[a]; }
        for (int q = 0; q < 8; ++q) {
            int jx = i0[0] + (q & 1), jy = i0[1] + ((q >> 1) & 1), jz = i0[2] + ((q >> 2) & 1);
            const int ix = std::min(std::max(jx, 0), l.r[0] - 1);
            const int iy = std::min(std::max(jy, 0), l.r[1] - 1);
            const int iz = std::min(std::max(jz, 0), l.r[2] - 1);
            const double w = ws * ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) * (((q >> 2) & 1) ? f[2] : 1 - f[2]);
            for (int a = 0; a < 3; ++a) out[a] += w * v[((size_t(iz) * l.r[1] + iy) * l.r[0] + ix) * 3 + a];
        }
    }
}
inline const std::vector<float> &selO(const Level &l, int h) { return l.O[h]; }
inline const std::vector<float> &selP(const Level &l, int h) { return l.P[h]; }
inline const std::vector<float> &selA(const Level &l, int h) { return l.Acap[h]; }

inline double tentCdf(double u)
{
    const double t = std::min(std::max(u, -1.0), 1.0);
    return t < 0.0 ? 0.5 * (t + 1.0) * (t + 1.0) : 1.0 - 0.5 * (1.0 - t) * (1.0 - t);
}

inline double coneRate(double c, double k)
{
    if (k <= 1e-12) return std::max(0.0, c);
    const double t = std::min(1.0, std::max(-1.0, -c / k));
    const auto A = [](double x) { return 0.5 * (x * std::sqrt(std::max(0.0, 1 - x * x)) + std::asin(x)); };
    const auto B = [](double x) { const double u = std::max(0.0, 1 - x * x); return -u * std::sqrt(u) / 3.0; };
    return (2.0 / M_PI) * (c * (A(1.0) - A(t)) + k * (B(1.0) - B(t)));
}

struct Origin { int n = -1; double world = 0.0; double sgn = 1.0; };

struct Result { double alpha = 0, lod = 0; double posLS[3] = {}; double trav = 0; };

/// ONE CASCADE of the plane march with THE READ OF jahVoxelReadPlane (split store, G7 + the
/// cone's crossing rate): the plane's own axis at the plane from the half that looks back at the
/// travel, the other two axes through the footprint kernel (fractional level lod - 1), each half
/// at the rate the cone's rays head toward it; the origin plane (the half the surface faces,
/// within one fine cell of its depth or behind) never counted.
inline Result marchCascade(const Cascade &c, bool aniso, const double pos[3], const double d[3], double tanA,
                           double startLod, double startAlpha, double startTrav, const Origin &org)
{
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
    const auto originGate = [&](int b, int h, double o, double pO) {
        (void)h;
        if (b != org.n || !(o > 0.0)) return 1.0;
        return tentCdf(((pO / o - oLS) * org.sgn - invRes[b]) / (0.5 * invRes[b]));
    };
    const bool ray = !(tanA > 0.0);
    Result r; r.alpha = startAlpha; r.lod = startLod;
    double readTo = startA, prevMip = -1; bool prevDir = false;
    double kb[3] = { -1, -1, -1 };   // the lateral kernel's level per axis (gLatTile)
    bool done = !inside(pos) || da == 0.0;
    for (int steps = 0; !done && r.alpha < 0.95 && steps < 256; ++steps) {
        const double tRead = (readTo - startA) * sgn * invAbsDa;
        const double trav = std::max(startTrav + tRead, vInv);
        r.lod = std::max(startLod, std::log2(std::max(vInv, 2.0 * tanA * trav) * res));
        if (r.lod >= c.maxLod) break;
        bool dir = aniso && r.lod > 0.5;
        double tc = dir ? 2.0 : 1.0;
        double mip = (aniso && !dir) ? 0.0 : std::floor(std::max(r.lod - 1.0 - std::log2(tc), 0.0));
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
        double x = 0.0;
        {   // the plane's own axis: the half looking back at the travel
            const int h = sgn > 0 ? kN : kP;
            const double Lc = dir ? mip + 1.0 : mip;
            double O[3], P[3], A[3];
            fetch(c, selO, h, Lc, cp, O);
            fetch(c, selP, h, Lc, cp, P);
            if (dir) {
                if (gLateral && !c.DLat.empty()) {
                    // THE FAMILY: the plane's own depth (level m), the cone's footprint across it -
                    // the kernel's level lk in cells, i.e. lk - (m + 1) lateral steps above m; the
                    // gate reads the coverage and the position from the same lateral level
                    const double lkp = std::max(std::max(r.lod - 1.0, 0.0), gSpan ? std::log2(tc) + mip : 0.0);
                    const double kd = std::max(lkp - (mip + 1.0), 0.0);
                    const int d = 2 * axis + (sgn > 0 ? 0 : 1);
                    A[axis] = fetchLat(c.DLat[size_t(std::min(int(mip), int(c.DLat.size()) - 1))][size_t(d)], kd, cp);
                    const size_t Li = size_t(std::min(int(Lc), int(c.OLat.size()) - 1));
                    O[axis] = fetchLat(c.OLat[Li][size_t(h)][size_t(axis)], kd, cp);
                    P[axis] = fetchLat(c.PLat[Li][size_t(h)][size_t(axis)], kd, cp);
                } else if (!c.D.empty()) A[axis] = fetchDir(c, int(mip), 2 * axis + (sgn > 0 ? 0 : 1), cp);
                else fetch(c, selA, h, Lc, cp, A);
            } else {
                double o = O[axis];
                if (ray) { double O2[3]; fetch(c, selO, 1 - h, Lc, cp, O2); o = std::min(1.0, o + O2[axis]); }
                A[axis] = o * std::exp2(mip);
            }
            // THE FIRST PLANE: w < 1 - part of its depth lies behind the point read up to. Its
            // surface counts by WHERE it lies (the position), not by the unread fraction: all
            // of it if it lies ahead, none if behind (a floor 0.1 m below a start in its own
            // texel read at w = 0.64 let the rest of the cone through the floor to the sky).
            double g = w;
            if (w < 1.0 - 1e-9 && firstPos && O[axis] > 0.0) {
                // ahead of the point read up to, to the texel's own far face (inclusive: the far
                // face is no tile boundary for this surface - it belongs to this texel)
                const double pa = P[axis] / O[axis], hh = 0.5 * invRes[axis];
                g = tentCdf((pa - readTo) * sgn / hh);
            }
            x += originGate(axis, h, O[axis], P[axis]) * A[axis] * g;
        }
        // the kernel no finer than the plane's texel - the hats must span the plane spacing
        const double lk = std::max(std::max(r.lod - 1.0, 0.0), gSpan ? std::log2(tc) + mip : 0.0);
        // THE KERNEL SAMPLES THE STRETCH IT STANDS FOR: at the midpoint of what this plane
        // reads (the plane's centre but on a cascade's first plane) - the hats then tile.
        double kp[3];
        if (gMid) { at(0.5 * (readTo + fr), kp); for (int a2 = 0; a2 < 3; ++a2) kp[a2] = std::min(std::max(kp[a2], 0.0), 1.0); }
        else for (int a2 = 0; a2 < 3; ++a2) kp[a2] = cp[a2];
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
            if (gLateralMinor && dir && !c.OLat.empty()) {
                // THE FAMILY FOR THE MINOR AXES: exact along b at the plane's own level (the stretch
                // crosses at most one of its texels along b), averaged across over the footprint
                const double Lcm = mip + 1.0;
                const size_t Li = size_t(std::min(int(Lcm), int(c.OLat.size()) - 1));
                const double kd = std::max(lk - Lcm, 0.0);
                for (int b = 0; b < 3; ++b) {
                    if (b == axis) continue;
                    O[b] = fetchLat(c.OLat[Li][size_t(h)][size_t(b)], kd, kp);
                    P[b] = fetchLat(c.PLat[Li][size_t(h)][size_t(b)], kd, kp);
                }
            }
            if (ray) for (int b = 0; b < 3; ++b) O[b] = OB[b];
            for (int b = 0; b < 3; ++b) {
                if (b == axis || (!(O[b] > 0.0) && !(gInteg && !ray))) continue;
                // rays heading +b meet the faces looking -b (half N), rays heading -b the +b ones;
                // a ray takes both halves at |d_b|
                const double cdir = ray ? std::fabs(d[b]) : (h == kN ? d[b] : -d[b]);
                const double rate = coneRate(cdir, tanA * std::sqrt(std::max(0.0, 1.0 - d[b] * d[b])));
                double Ob = O[b];
                if (gInteg && !ray) {
                    // THE KERNEL INTEGRATED OVER THE STRETCH, not sampled at its middle: the
                    // mean coverage along b over [b_in, b_out] - each texel's hat's share of the
                    // stretch (its CDF difference: consecutive stretches telescope, so a
                    // surface's texel is read to exactly its full coverage however the planes
                    // fall), both integer levels around lk, lerped.
                    double bi[3], bo[3]; at(readTo, bi); at(fr, bo);
                    const double lo = std::min(bi[b], bo[b]), hi = std::max(bi[b], bo[b]);
                    if (hi - lo > 1e-9) {
                        // the integral of half h's coverage along b over [u0, u1] at level L
                        const auto integ = [&](double u0, double u1, int L) {
                            L = std::min(L, int(c.L.size()) - 1);
                            const double Tb = std::exp2(double(L)) * invRes[b];
                            double sum = 0;
                            for (double j = std::floor(u0 / Tb) - 1; j <= std::floor(u1 / Tb) + 1; j += 1) {
                                const double cj = (j + 0.5) * Tb;
                                const double ov = gBox ? std::max(0.0, std::min(u1, cj + 0.5 * Tb) - std::max(u0, cj - 0.5 * Tb))
                                                       : Tb * (tentCdf((u1 - cj) / Tb) - tentCdf((u0 - cj) / Tb));
                                if (ov <= 0) continue;
                                double q[3] = { kp[0], kp[1], kp[2] }; q[b] = std::min(std::max(cj, 0.0), 1.0);
                                double Oq[3]; fetch(c, selO, h, double(L), q, Oq);
                                if (gStartTexel && pos[b] >= cj - 0.5 * Tb && pos[b] < cj + 0.5 * Tb && Oq[b] > 0) {
                                    double Pq[3]; fetch(c, selP, h, double(L), q, Pq);
                                    const double pj = Pq[b] / Oq[b];
                                    sum += (pj >= u0 && pj < u1) ? Oq[b] * Tb : 0.0;
                                    continue;
                                }
                                sum += Oq[b] * ov;
                            }
                            return sum;
                        };
                        double mean;
                        if (gLatTile) {
                            // THE LATERAL LEVEL RISES ONLY ON ITS OWN BOUNDARY ALONG b: one level
                            // a plane, at the coarser level's boundary inside the stretch - the part
                            // before it at the old level, the rest at the new (a partition of b)
                            if (kb[b] < 0) kb[b] = std::floor(lk);
                            const double s0 = d[b] > 0 ? lo : hi, s1 = d[b] > 0 ? hi : lo;
                            double sum = 0, u = s0;
                            // rise as many levels as this stretch holds boundaries of (each rise at
                            // the next coarser level's boundary on the way)
                            while (kb[b] < std::floor(lk)) {
                                const double Tn = std::exp2(kb[b] + 1.0) * invRes[b];
                                const double B = d[b] > 0 ? std::ceil(u / Tn - 1e-6) * Tn : std::floor(u / Tn + 1e-6) * Tn;
                                if (!((d[b] > 0 && B <= s1) || (d[b] <= 0 && B >= s1))) break;
                                sum += integ(std::min(u, B), std::max(u, B), int(kb[b]));
                                kb[b] += 1; u = B;
                            }
                            sum += integ(std::min(u, s1), std::max(u, s1), int(kb[b]));
                            mean = sum / (hi - lo);
                        } else {
                            const int L0 = int(std::floor(lk)); const double fl = lk - L0;
                            mean = ((1 - fl) * integ(lo, hi, L0) + (fl > 0 ? fl * integ(lo, hi, L0 + 1) : 0.0)) / (hi - lo);
                        }
                        Ob = mean;
                    }
                }
                x += originGate(b, h, O[b], P[b]) * Ob * rate * lenLS / invRes[b];
            }
        }
        r.alpha += std::min(x, 1.0 - r.alpha);
        readTo = fr;
        if (sgn > 0 ? readTo >= 1.0 : readTo <= 0.0) done = true;
    }
    const double tEnd = (readTo - startA) * sgn * invAbsDa;
    for (int a = 0; a < 3; ++a) r.posLS[a] = pos[a] + tEnd * d[a];
    r.trav = std::max(startTrav + tEnd, vInv);
    return r;
}

/// THE SHARE OF A CONE BELOW ITS SURFACE: the rays through its cross-section (a disc of radius
/// tan at unit axial distance) that point into the surface (d.N + q.N < 0) - stopped by it, the
/// hemisphere's boundary being opaque from above. With sin e = d.N and q.N = tan cos e X (X of
/// density (2/pi) sqrt(1 - X^2)): the disc's segment beyond t = tan e / tan, the fraction
/// (acos t - t sqrt(1 - t^2)) / pi; none when the rim clears the surface (t >= 1).
inline double coneBelow(const double d[3], const double N[3], double tanA)
{
    const double se = d[0] * N[0] + d[1] * N[1] + d[2] * N[2];
    const double ce = std::sqrt(std::max(0.0, 1.0 - se * se));
    if (!(tanA > 0.0) || ce <= 1e-9) return se < 0 ? 1.0 : 0.0;
    const double t = std::min(1.0, std::max(-1.0, se / (ce * tanA)));
    return (std::acos(t) - t * std::sqrt(std::max(0.0, 1.0 - t * t))) / M_PI;
}

/// THE WALK from a world point on a surface with an axis-aligned normal (axis `n`, sign `sg`):
/// the start bias one cell of cascade 0 along it, the origin plane at the surface.
inline double walkN(const std::vector<Cascade> &ch, bool aniso, const double pw[3], int n, double sg,
                    const double dirW[3], double tanA)
{
    const Cascade &c0 = ch[0];
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
    Result r = marchCascade(c0, aniso, p, d, tanA, 0, a0, 0, org);
    return r.alpha;
}

/// THE WALK from a world point on a surface with world normal +y (the pixel's start bias: one
/// cell of cascade 0 along it), over the chain.
inline double walk(const std::vector<Cascade> &ch, bool aniso, const double pw[3], const double dirW[3], double tanA)
{
    const Cascade &c0 = ch[0];
    double d[3], l = 0;
    for (int a = 0; a < 3; ++a) { d[a] = dirW[a] / c0.size[a]; l += d[a] * d[a]; }
    l = std::sqrt(l);
    for (int a = 0; a < 3; ++a) d[a] /= l;
    double p[3];
    for (int a = 0; a < 3; ++a) p[a] = (pw[a] - c0.origin[a]) / c0.size[a];
    p[1] += 1.0 / c0.R[1];
    Origin org; org.n = 1; org.world = pw[1]; org.sgn = 1.0;
    Result r = marchCascade(c0, aniso, p, d, tanA, 0, 0, 0, org);
    for (size_t j = 1; j < ch.size() && r.alpha < 0.95; ++j) {
        const Cascade &pc = ch[j - 1], &c = ch[j];
        double np[3], hop = 0;
        for (int a = 0; a < 3; ++a) {
            const double scale = pc.size[a] / c.size[a];
            np[a] = r.posLS[a] * scale + (pc.origin[a] - c.origin[a]) / c.size[a];
            hop += std::fabs(d[a]) * scale;
        }
        r = marchCascade(c, aniso, np, d, tanA, std::max(r.lod - pc.maxLod, 0.0), r.alpha, r.trav * hop, org);
    }
    return r.alpha;
}

inline bool hitBox(const double p[3], const double d[3], const Box &b)
{
    double t0 = 0, t1 = 1e30;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-12) { if (p[a] < b.lo[a] || p[a] > b.hi[a]) return false; continue; }
        double ta = (b.lo[a] - p[a]) / d[a], tb = (b.hi[a] - p[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

inline void frame(const double ax[3], double e1[3], double e2[3])
{
    double u[3] = { 0, 0, 0 };
    u[std::fabs(ax[0]) < 0.9 ? 0 : 2] = 1.0;
    e1[0] = ax[1] * u[2] - ax[2] * u[1]; e1[1] = ax[2] * u[0] - ax[0] * u[2]; e1[2] = ax[0] * u[1] - ax[1] * u[0];
    const double l = std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    for (int a = 0; a < 3; ++a) e1[a] /= l;
    e2[0] = ax[1] * e1[2] - ax[2] * e1[1]; e2[1] = ax[2] * e1[0] - ax[0] * e1[2]; e2[2] = ax[0] * e1[1] - ax[1] * e1[0];
}

/// THE CONE-TRACE REFERENCE of one cone over the true geometry: its cross-section a uniform disc
/// of radius tan at unit axial distance (the rays through it - the density the reader's planes
/// integrate), every occluder ENTERED along each ray counted (the split store read front faces
/// only: its fine-store limit), alpha = min(1, E). The origin plane is the hemisphere's boundary.
inline double refAlpha(const double p[3], const double ax[3], double tanA, const std::vector<Box> &boxes)
{
    double e1[3], e2[3];
    frame(ax, e1, e2);
    const int NR = 96, NP = 192;
    double E = 0;
    for (int i = 0; i < NR; ++i)
        for (int j = 0; j < NP; ++j) {
            const double r = std::sqrt((i + 0.5) / NR) * tanA, ph = 2 * M_PI * (j + 0.5) / NP;
            double d[3];
            for (int a = 0; a < 3; ++a) d[a] = ax[a] + r * (std::cos(ph) * e1[a] + std::sin(ph) * e2[a]);
            for (const Box &b : boxes) if (hitBox(p, d, b)) E += 1;
        }
    return std::min(1.0, E / (NR * NP));
}

/// The hemisphere's truth: the cosine-weighted sky visibility at p (normal +y).
inline double truthVis(const double p[3], const std::vector<Box> &boxes)
{
    const int N = 320;
    double v = 0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            const double u = (i + 0.5) / N, r = std::sqrt(u), ph = 2 * M_PI * (j + 0.5) / N;
            const double d[3] = { r * std::cos(ph), std::sqrt(1 - u), r * std::sin(ph) };
            bool h = false;
            for (const Box &b : boxes) if (hitBox(p, d, b)) { h = true; break; }
            if (!h) v += 1;
        }
    return v / (N * N);
}

/// THE CONE SETS (in the frame of a normal +y): directions, weights, the aperture.
struct ConeSet { std::string name; std::vector<std::array<double, 3>> dirs; std::vector<double> w; double tan; };
inline std::vector<ConeSet> coneSets()
{
    std::vector<ConeSet> s;
    const double four[4][3] = { { 0.707107, 0.707107, 0 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0.707107, 0 }, { 0, 0.707107, -0.707107 } };
    { ConeSet a{ "four (tan 0.98)", {}, {}, 0.98269 }; for (auto &d : four) { a.dirs.push_back({ d[0], d[1], d[2] }); a.w.push_back(0.25); } s.push_back(a); }
    {
        ConeSet a{ "six (tan 0.577)", {}, {}, 0.577 };
        const double six[6][3] = { { 0, 1, 0 }, { 0.866025, 0.5, 0 }, { 0.267617, 0.5, 0.823639 }, { -0.700629, 0.5, 0.509037 },
                                   { -0.700629, 0.5, -0.509037 }, { 0.267617, 0.5, -0.823639 } };
        const double w6[6] = { 0.25, 0.15, 0.15, 0.15, 0.15, 0.15 };
        for (int j = 0; j < 6; ++j) { a.dirs.push_back({ six[j][0], six[j][1], six[j][2] }); a.w.push_back(w6[j]); }
        s.push_back(a);
    }
    // THE FOUR-CONE SET TURNED about the normal (CARDS-3's rotations)
    for (double spin : { 0.3927, 0.7854 }) {
        ConeSet a{ "four turned " + std::to_string(int(spin * 180 / M_PI + 0.5)), {}, {}, 0.98269 };
        for (auto &d : four) {
            const double x = d[0] * std::cos(spin) - d[2] * std::sin(spin), z = d[0] * std::sin(spin) + d[2] * std::cos(spin);
            a.dirs.push_back({ x, d[1], z }); a.w.push_back(0.25);
        }
        s.push_back(a);
    }
    // FIBONACCI-N: N cosine-distributed directions (a Fibonacci spiral in the disc), equal
    // weights, each cone the solid angle 2 pi / N: cos(half-angle) = 1 - 1 / N.
    for (int N : { 8, 12, 16 }) {
        ConeSet a{ "Fibonacci-" + std::to_string(N) + " (" + std::to_string(int(std::acos(1.0 - 1.0 / N) * 180.0 / M_PI + 0.5)) + " deg)", {}, {},
                   std::tan(std::acos(1.0 - 1.0 / N)) };
        for (int i = 0; i < N; ++i) {
            const double u = (i + 0.5) / N, r = std::sqrt(u), ph = i * M_PI * (3.0 - std::sqrt(5.0));
            a.dirs.push_back({ r * std::cos(ph), std::sqrt(1.0 - u), r * std::sin(ph) });
            a.w.push_back(1.0 / N);
        }
        s.push_back(a);
    }
    return s;
}

inline double keepOf(double alpha) { return 1.0 - std::min(1.0, alpha / 0.95); }

}   // namespace voxlab
