// THE VOXEL STORE'S DIRECTIONAL COVERAGE MODEL, ON THE CPU (PHOTON-VOXEL-3).
//
// What the voxeliser SHOULD store for a set of authored triangles - each triangle
// clipped exactly to each half-open voxel, the coverage PER HALF-AXIS (PHOTON-VOXEL-4)
// O_a+ = sum of the pieces' (area / the voxel's face area along a) x opacity x
// max(n . a, 0) and O_a- with max(-n . a, 0), each capped at 1, the light the mean
// radiance times c = max of the six - and what a ray's mip-0 march reads out of a
// store (the crossing rule of jah_voxel_sample.glsl's jahVoxelReadPlane at mip 0 for a
// ray: both halves). The suites hold the GPU store to this
// model and derive their bars from what the model predicts, never from the store.
// Shared by gi.voxel_coverage and gi.field_thin_wall; cubic cells (the chain's).
#pragma once

#include "jahshaka/engine/Engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace voxelmodel {

using jahshaka::engine::GiVoxelVolume;

struct D3 { double x, y, z; };
inline D3 add(D3 a, D3 b) { return D3{ a.x + b.x, a.y + b.y, a.z + b.z }; }
inline D3 sub(D3 a, D3 b) { return D3{ a.x - b.x, a.y - b.y, a.z - b.z }; }
inline D3 mul(D3 a, double s) { return D3{ a.x * s, a.y * s, a.z * s }; }
inline D3 cross(D3 a, D3 b) { return D3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double len(D3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline double comp(D3 a, int i) { return i == 0 ? a.x : (i == 1 ? a.y : a.z); }

/// A store: the light (radiance x c, k units), c and the coverage per half-axis, on a lattice.
struct Store {
    int W = 0, H = 0, D = 0;
    double origin[3] = { 0, 0, 0 }, cell = 0, k = 1;
    std::vector<float> light;     ///< rgba per voxel (rgb only read)
    std::vector<float> cov[2];    ///< [0] the faces looking +a, [1] -a: O_x O_y O_z c per voxel
    /// LEVEL 0 PER SIDE (PHOTON-VOXEL-5; empty on a Low volume): the back side's light and the
    /// voxeliser's normal (GiVoxelVolume::lightBack / normal).
    std::vector<float> lightBack, normal;
    size_t at(int x, int y, int z) const { return ((size_t(z) * H + y) * W + x) * 4u; }
    /// The light (premultiplied like `light`, channel ch) of the faces of half h (0 looking +a,
    /// 1 -a) along axis a in the voxel at index i: the side those faces are - where the normal's
    /// component along a looks the half's way the front (2 light - back), against it the back,
    /// across it the two sides' mean; a Low volume's one light either way.
    double sideLight(size_t i, int a, int h, int ch) const
    {
        if (lightBack.empty() || normal.empty()) return light[i + size_t(ch)];
        const double along = (h == 0 ? 1.0 : -1.0) * (normal[i + size_t(a)] * 2.0 - 1.0);
        const double mean = light[i + size_t(ch)], back = lightBack[i + size_t(ch)];
        return along > 0.0 ? 2.0 * mean - back : (along < 0.0 ? back : mean);
    }
    bool in(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < W && y < H && z < D; }
};

inline Store fromGpu(const GiVoxelVolume &v)
{
    Store s;
    s.W = v.width; s.H = v.height; s.D = v.depth;
    for (int a = 0; a < 3; ++a) s.origin[a] = v.origin[a];
    s.cell = v.cell[0];
    s.k = v.multiplier;
    s.light = v.light;
    s.lightBack = v.lightBack;
    s.normal = v.normal;
    const std::vector<float> *src[2] = { &v.coverageP, &v.coverageN };
    for (int h = 0; h < 2; ++h) {
        s.cov[h].assign(src[h]->size(), 0.f);
        for (size_t i = 0; i < src[h]->size(); i += 4) {
            for (int a = 0; a < 3; ++a) s.cov[h][i + a] = (*src[h])[i + a];
            s.cov[h][i + 3] = v.albedo[i + 3];
        }
    }
    return s;
}

// ---- THE CPU MODEL: exact clip of each triangle to each half-open voxel -------------
struct Tri { D3 v[3]; };

inline double clipArea(const Tri &t, const D3 &lo, const D3 &hi)
{
    std::vector<D3> a(t.v, t.v + 3), b;
    for (int face = 0; face < 6 && a.size() >= 3; ++face) {
        const int axis = face >> 1;
        const bool high = face & 1;
        auto side = [&](const D3 &p) { return high ? comp(hi, axis) - comp(p, axis) : comp(p, axis) - comp(lo, axis); };
        b.clear();
        for (size_t i = 0; i < a.size(); ++i) {
            const D3 &pa = a[i], &pb = a[(i + 1) % a.size()];
            const double sa = side(pa), sb = side(pb);
            const bool ina = high ? sa > 0 : sa >= 0, inb = high ? sb > 0 : sb >= 0;
            if (ina) b.push_back(pa);
            if (ina != inb) b.push_back(add(pa, mul(sub(pb, pa), sa / (sa - sb))));
        }
        a.swap(b);
    }
    double area = 0;
    for (size_t i = 1; i + 1 < a.size(); ++i) area += len(cross(sub(a[i], a[0]), sub(a[i + 1], a[0])));
    return 0.5 * area;
}

/// The model store of `tris` (radiance L = 1, opacity `alpha`) on `ref`'s lattice.
inline Store modelStore(const Store &ref, const std::vector<Tri> &tris, double alpha)
{
    Store s = ref;
    s.k = 1.0;
    std::fill(s.light.begin(), s.light.end(), 0.f);
    for (int h = 0; h < 2; ++h) s.cov[h].assign(s.light.size(), 0.f);
    std::vector<double> sum[2] = { std::vector<double>(s.light.size(), 0.0), std::vector<double>(s.light.size(), 0.0) };
    const double cell = s.cell, face = cell * cell;
    for (const Tri &t : tris) {
        D3 n = cross(sub(t.v[1], t.v[0]), sub(t.v[2], t.v[0]));
        const double nl = len(n);
        if (nl <= 0) continue;
        n = mul(n, 1.0 / nl);
        int lo[3], hi[3];
        for (int a = 0; a < 3; ++a) {
            double mn = 1e30, mx = -1e30;
            for (int i = 0; i < 3; ++i) { mn = std::min(mn, comp(t.v[i], a)); mx = std::max(mx, comp(t.v[i], a)); }
            lo[a] = int(std::floor((mn - s.origin[a]) / cell)) - 1;
            hi[a] = int(std::floor((mx - s.origin[a]) / cell)) + 1;
        }
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int y = lo[1]; y <= hi[1]; ++y)
                for (int x = lo[0]; x <= hi[0]; ++x) {
                    if (!s.in(x, y, z)) continue;
                    const D3 vlo{ s.origin[0] + x * cell, s.origin[1] + y * cell, s.origin[2] + z * cell };
                    const D3 vhi = add(vlo, D3{ cell, cell, cell });
                    const double w = clipArea(t, vlo, vhi) / face;
                    if (w <= 0) continue;
                    const size_t i = s.at(x, y, z);
                    for (int a = 0; a < 3; ++a) {
                        sum[0][i + a] += w * alpha * std::max(0.0, comp(n, a));
                        sum[1][i + a] += w * alpha * std::max(0.0, -comp(n, a));
                    }
                }
    }
    for (size_t i = 0; i < s.light.size(); i += 4) {
        double c = 0;
        for (int h = 0; h < 2; ++h)
            for (int a = 0; a < 3; ++a) {
                s.cov[h][i + a] = float(std::min(1.0, sum[h][i + a]));
                c = std::max(c, double(s.cov[h][i + a]));
            }
        s.cov[0][i + 3] = s.cov[1][i + 3] = float(c);
        for (int a = 0; a < 3; ++a) s.light[i + a] = float(c);   // L = 1, premultiplied by c
    }
    return s;
}

/// FLUX along each axis, both halves: sum of radiance x (O_a+ + O_a-) x cell^2 (radiance
/// = colour / c / k).
inline void flux(const Store &s, double out[3])
{
    out[0] = out[1] = out[2] = 0;
    for (size_t i = 0; i < s.light.size(); i += 4) {
        const double c = s.cov[0][i + 3];
        if (c <= 0) continue;
        const double rad = (s.light[i] + s.light[i + 1] + s.light[i + 2]) / 3.0 / s.k / c;
        for (int a = 0; a < 3; ++a) out[a] += rad * (s.cov[0][i + a] + s.cov[1][i + a]) * s.cell * s.cell;
    }
}

/// A RAY'S MIP-0 MARCH (the crossing rule of jah_voxel_sample.glsl's jahVoxelReadPlane at
/// mip 0 for aperture 0: both halves, a ray is stopped by any face it crosses), on the CPU
/// over a store read back whole:
/// samples half the footprint apart along the ray - half the L1 cell, 0.5 x |d|_1 cells,
/// the march's own step at mip 0 - trilinear; each sample's opacity is the per-axis
/// coverage times the cells the step crosses along each axis (O . |d| x step), capped
/// at 1; composited ADDITIVELY (the pieces of one surface add up), the colour the
/// stored radiance times the opacity taken. Averaged over `phases` start phases, a
/// quarter step apart.
inline double march(const Store &s, D3 o, D3 d, double tMax, double &opacity, int phases = 4)
{
    const double cell = s.cell;
    const double ad[3] = { std::fabs(d.x), std::fabs(d.y), std::fabs(d.z) };
    const double stepCells = 0.5 * (ad[0] + ad[1] + ad[2]);
    const double step = stepCells * cell;
    double Csum = 0.0, Asum = 0.0;
    for (int ph = 0; ph < phases; ++ph) {
        double A = 0.0, C = 0.0;
        for (double t = 0.25 * ph * step; t < tMax && A < 0.999; t += step) {
            const D3 p = add(o, mul(d, t));
            const double g[3] = { (p.x - s.origin[0]) / cell - 0.5, (p.y - s.origin[1]) / cell - 0.5,
                                  (p.z - s.origin[2]) / cell - 0.5 };
            int i0[3];
            double f[3];
            for (int a = 0; a < 3; ++a) { i0[a] = int(std::floor(g[a])); f[a] = g[a] - i0[a]; }
            double col = 0, c = 0, O[3] = { 0, 0, 0 };
            for (int q = 0; q < 8; ++q) {
                const int ix = i0[0] + (q & 1), iy = i0[1] + ((q >> 1) & 1), iz = i0[2] + ((q >> 2) & 1);
                if (!s.in(ix, iy, iz)) continue;
                const double wt = ((q & 1) ? f[0] : 1 - f[0]) * (((q >> 1) & 1) ? f[1] : 1 - f[1]) *
                                  (((q >> 2) & 1) ? f[2] : 1 - f[2]);
                const size_t i = s.at(ix, iy, iz);
                col += wt * (s.light[i] + s.light[i + 1] + s.light[i + 2]) / 3.0 / s.k;
                c += wt * s.cov[0][i + 3];
                // a RAY is stopped by any face it crosses: both halves, capped per texel
                for (int a = 0; a < 3; ++a) O[a] += wt * std::min(1.0f, s.cov[0][i + a] + s.cov[1][i + a]);
            }
            if (c <= 0.0) continue;
            const double k = std::min(1.0, (O[0] * ad[0] + O[1] * ad[1] + O[2] * ad[2]) * stepCells);
            const double take = std::min(k, 1.0 - A);
            C += col / c * take;
            A += take;
        }
        Csum += C;
        Asum += A;
    }
    opacity = Asum / phases;
    return Csum / phases;
}


}  // namespace voxelmodel
