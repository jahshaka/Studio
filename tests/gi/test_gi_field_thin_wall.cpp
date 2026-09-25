// gi.field_thin_wall + gi.field_alias — THE IRRADIANCE FIELD'S ESTIMATOR ON A ONE-VOXEL
// EMISSIVE WALL (PHOTON-FIELD-ROTATE-1; spikes/photon-field-rotate-1/EVIDENCE.txt).
//
// THE FIXTURE. A black world with one emitter: a wall of radiance L = 1, 0.8 m square
// and ONE cascade-0 cell thick, its near face four probe spacings in +z from a probe
// of the field (2.81 m at High), at three lateral offsets (0, 1/3 and 2/3 of an x
// spacing) - the fixture WRITER-1's fix round measured the field on, reproduced to
// the last digit on the base binary. The probe's reading is taken where the pixel's
// reader takes it: the irradiance atlas texels around the wall's direction (the four
// texels whose bilinear footprint holds +z), each against the analytic irradiance of
// the wall AS THE VOXELS HOLD IT at that texel's own direction.
//
// PART 1 (`wall`): WHAT THE ~2.2x OVERSHOOT WAS, one assertion per cause.
//   (a) THE STORE (PHOTON-VOXEL-3). The voxeliser stores the wall's PROJECTED AREA per
//       axis (each triangle clipped to each half-open voxel): the wall's two faces in
//       one layer each (the lit count), and the store's flux along the wall's normal -
//       the sum of radiance x O_z x cell^2 - is the authored front and back faces'
//       (read back whole, giVoxelVolume). Until VOXEL-3 the conservative raster
//       rounded the wall out to whole cells at full emission, three layers deep: the
//       probe read 1.36-1.57x the authored wall.
//   (b) THE MARCH. A 4 m wall reads what the model predicts (no double count).
//   (d) THE ESTIMATOR (the dominant cause). Upstream shot one ray per depth texel at a
//       FIXED octahedral direction and integrated the texels as a cosine-weighted
//       Riemann sum - but octahedral texels are not equal solid angle (1/|q|^3: 1 at
//       the axis vertices, 5.2 at the face centres), so a source near an axis
//       over-read 1.68x even at 256 rays a texel. The field now shoots a spherical-
//       Fibonacci set uniform in solid angle, rotated per integration, and every texel
//       integrates every ray (DDGI's estimator).
//   THE REFERENCE IS THE AUTHORED WALL, and the bar is DERIVED from the directional
//   model: the CPU model (tests/support/voxelcoveragemodel.h) voxelises the authored
//   box on cascade 0's lattice and the reader's own mip-0 march (the field's probe
//   rays have aperture zero, so they read mip 0 only) is run over it from the probe:
//   M, the model's reading over the authored wall's. The probe must read M x the
//   authored irradiance within the estimator's standard error at the target sample
//   count (twice sigma / sqrt(K), sigma measured per integration), the march's
//   start-phase spread (the model at one phase against four) and 1 % for the atlas
//   texels' lobe (the model weights the rays by the cosine about +z, the field by
//   its four pole texels').
//   WHAT (d) THEREFORE PROVES, SAID PLAINLY (PHOTON-VOXEL-4): M is the READER'S RULE
//   applied to the model, not the authored wall - so (d) proves "the field reads what
//   the rule reads", and the rule's own over-read of the authored wall (M, printed per
//   lateral size) sits inside the prediction rather than the bar. The rule's distance
//   from the authored wall is the voxel lab's record (gi.voxel_lab) and (a)'s flux.
//
// PART 2 (`alias`): ONE RAY THAT DOES NOT ALIAS.
//   1. THE NOISE OF ONE SAMPLE at 1 and 2 rays a texel (a probe's sample m is rotated
//      by a hash of its world lattice point and m, so the M samples are read out of
//      the probe's own mean as it refines at budget 1): their mean against the
//      truth (unbiased) and their relative standard deviation sigma - the number the
//      target sample count K is derived from (K = ceil((2 sigma / e)^2), e the
//      wall's trilinear edge uncertainty 1 - ((n-1)/n)^2: the converged mean's
//      two-sigma error inside what the store itself can resolve).
//   2. THE SAME SET NOT ROTATED (JAHSHAKA_GI_FIELD_STATIC): its reading does not move
//      from integration to integration - that is the aliasing, stated as a number.
//   3. CONVERGENCE at the shipped K and budget 1: the frames until the field owes
//      nothing (K frames, printed), the converged reading inside part 1's bar, and at
//      rest afterwards the atlas BYTE-IDENTICAL frame to frame (the field stops: the
//      frame-to-frame variance at rest is zero, the floor).
//   4. 1 ray against 2 rays, both converged at K: within the two estimators' joint
//      standard error; and the cost of one whole-field integration at 1 and 2 rays in
//      ONE process (the monitor's ifd.converge.inline rows, medians) - a ratio, clocks
//      unlocked.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/voxelcoveragemodel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static void render(Engine *e, int frames = 1) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static float halfToFloat(unsigned short h)
{
    const unsigned s = (h >> 15) & 1u, ex = (h >> 10) & 31u, m = h & 1023u;
    float v;
    if (ex == 0) v = std::ldexp(float(m), -24);
    else if (ex == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(float(m + 1024u), int(ex) - 25);
    return s ? -v : v;
}

static GiParams fieldGi(int budget)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::On;
    gi.updateBudget = budget;
    gi.cascades = true;
    return gi;
}

struct V3 { double x, y, z; };

/// The direction the generation job integrates texel (x, y) of an r-texel tile about:
/// Ogre's octahedral decode at the texel's CENTRE (jahIfdTexelDir).
static V3 texelDir(int x, int y, int r)
{
    const double fx = (x + 0.5) * 2.0 / r - 1.0, fy = (y + 0.5) * 2.0 / r - 1.0;
    V3 n{ fx, fy, 1.0 - std::fabs(fx) - std::fabs(fy) };
    const double t = std::max(0.0, std::min(1.0, -n.z));
    n.x += n.x >= 0 ? -t : t;
    n.y += n.y >= 0 ? -t : t;
    const double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    return V3{ n.x / l, n.y / l, n.z / l };
}

/// E/pi of a Lambertian rectangle of radiance 1 in the plane z = zw (normal -z),
/// x in [x0, x1], y in [y0, y1], seen from the origin by a surface of normal n.
static double analytic(double x0, double x1, double y0, double y1, double zw, const V3 &n, int k = 400)
{
    double s = 0.0;
    const double dA = (x1 - x0) * (y1 - y0) / (double(k) * k);
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j) {
            const double x = x0 + (i + .5) * (x1 - x0) / k, y = y0 + (j + .5) * (y1 - y0) / k;
            const double r2 = x * x + y * y + zw * zw, r = std::sqrt(r2);
            const double cp = (x * n.x + y * n.y + zw * n.z) / r;
            if (cp > 0) s += cp * (zw / r) / r2 * dA;
        }
    return s / M_PI;
}

struct Fixture {
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *scene = nullptr;
    NodeId wall = 0;
    unsigned N[3] = { 0, 0, 0 };
    float sp[3] = { 0, 0, 0 };
    int loc[3] = { 0, 0, 0 };
    double P[3] = { 0, 0, 0 };     ///< the probe, world
    double O[3] = { 0, 0, 0 };     ///< cascade 0's voxel origin
    double cell = 0.0;
    double D = 0.0;                ///< the wall's near face, from the probe
    double multiplier = 0.0;       ///< cascade 0's decode multiplier, cached per build
    voxelmodel::Store lattice;     ///< cascade 0's lattice (the camera never moves: fixed)
    double mKey[3] = { -1, -1, -1 };
    double M = 0.0, M1 = 0.0;      ///< the model's reading over the authored wall's (4 phases / 1)
};

/// One reading of the probe: the four texels around +z against their truths.
struct Reading {
    bool ok = false;
    double value[4] = { 0, 0, 0, 0 };   ///< radiance units (atlas / cascade 0's multiplier)
    double truth[4] = { 0, 0, 0, 0 };   ///< the model's prediction: M x the authored wall's
    double authored[4] = { 0, 0, 0, 0 };///< the authored wall's analytic at the texel's direction
    double ratio = 0.0;                 ///< sum value / sum truth
    double ratioAuthored = 0.0;         ///< sum value / sum authored
    double M = 0.0, M1 = 0.0;
    double count = 0.0;                 ///< the probe's sample count (atlas alpha)
    long long lit = 0, litPredicted = 0;
    int cellsX = 0, cellsY = 0;
    double peakRadiance = 0.0;
    std::vector<unsigned char> tile;    ///< the probe's irradiance tile bytes
};

static const int kPoleTexel[4][2] = { { 2, 2 }, { 3, 2 }, { 2, 3 }, { 3, 3 } };

/// The authored wall (the unit cube scaled and placed exactly as buildWith places it).
static std::vector<voxelmodel::Tri> wallTris(const Fixture &f, double lat, double hx, double hy)
{
    const MeshData m = enginetest::unitCubeMesh();
    const voxelmodel::D3 c{ f.P[0] + lat, f.P[1], f.P[2] + f.D + 0.5 * f.cell };
    const double sc[3] = { 2 * hx, 2 * hy, f.cell };
    std::vector<voxelmodel::Tri> out;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        voxelmodel::Tri t;
        for (int k = 0; k < 3; ++k) {
            const unsigned v = m.indices[i + size_t(k)];
            t.v[k] = voxelmodel::D3{ c.x + m.positions[v * 3] * sc[0], c.y + m.positions[v * 3 + 1] * sc[1],
                                     c.z + m.positions[v * 3 + 2] * sc[2] };
        }
        out.push_back(t);
    }
    return out;
}

/// M: WHAT THE DIRECTIONAL MODEL PREDICTS THE PROBE READS, over the authored wall's
/// reading. Rays from the probe over the wall and three cells round it, each marched
/// through the model's store by the reader's mip-0 rule (the field's rays have
/// aperture zero) and weighted by the cosine about +z and their solid angle, against
/// the same rays' authored radiance (1 where they meet the front face, else 0).
static void modelFactor(Fixture &f, double lat, double hx, double hy)
{
    if (f.mKey[0] == lat && f.mKey[1] == hx && f.mKey[2] == hy) return;
    const voxelmodel::Store model = voxelmodel::modelStore(f.lattice, wallTris(f, lat, hx, hy), 1.0);
    const double half = 3 * f.cell, dA = f.cell / 4.0, zf = f.D;
    const voxelmodel::D3 o{ f.P[0], f.P[1], f.P[2] };
    double num = 0, num1 = 0, den = 0;
    for (double x = lat - hx - half; x < lat + hx + half; x += dA)
        for (double y = -hy - half; y < hy + half; y += dA) {
            const double px = x + 0.5 * dA, py = y + 0.5 * dA;
            const double r = std::sqrt(px * px + py * py + zf * zf);
            const voxelmodel::D3 d{ px / r, py / r, zf / r };
            const double w = (zf / r) * (zf / r) / (r * r) * dA * dA;   // cos about +z x d omega
            double op;
            num += voxelmodel::march(model, o, d, r + 8 * f.cell, op) * w;
            num1 += voxelmodel::march(model, o, d, r + 8 * f.cell, op, 1) * w;
            if (px >= lat - hx && px < lat + hx && py >= -hy && py < hy) den += w;
        }
    f.M = den > 0 ? num / den : 0.0;
    f.M1 = den > 0 ? num1 / den : 0.0;
    f.mKey[0] = lat; f.mKey[1] = hx; f.mKey[2] = hy;
}

static Reading readProbe(Fixture &f, double lat, double hx, double hy, bool voxels = true)
{
    Reading rd;
    GiFieldAtlas at;
    if (!f.scene->giFieldAtlas(at) || !at.available) return rd;
    GiVoxelStats vs;
    if (voxels) {
        vs = f.scene->giVoxelStats(0);
        if (!vs.available || vs.multiplier <= 0.0f) return rd;
        f.multiplier = vs.multiplier;
    } else {
        if (f.multiplier <= 0.0) return rd;
        vs.multiplier = float(f.multiplier);
    }
    unsigned s3[3];
    for (int k = 0; k < 3; ++k) s3[k] = (unsigned(f.loc[k]) + at.windowOffset[k]) % f.N[k];
    const unsigned slot = s3[0] + s3[1] * f.N[0] + s3[2] * f.N[0] * f.N[1];
    const unsigned B = at.irradBordered, W = at.irradWidth, R = B - 2u;
    const unsigned x0 = (slot * B) % W, y0 = ((slot * B) / W) * B;
    const auto texelAt = [&](unsigned x, unsigned y, int c) {
        const unsigned short *p = reinterpret_cast<const unsigned short *>(
            &at.irradiance[(size_t(y0 + 1 + y) * W + x0 + 1 + x) * at.irradBytesPerTexel]);
        return halfToFloat(p[c]);
    };
    for (unsigned y = 0; y < B; ++y) {
        const size_t row = (size_t(y0 + y) * W + x0) * at.irradBytesPerTexel;
        rd.tile.insert(rd.tile.end(), at.irradiance.begin() + long(row),
                       at.irradiance.begin() + long(row + size_t(B) * at.irradBytesPerTexel));
    }
    rd.count = texelAt(0, 0, 3);
    // THE CELLS the wall's faces cover (half-open voxels: a face ON a boundary belongs
    // to the voxel above it), for the lit count - each face one layer, the sides in the
    // front face's - and for the edge count part 2 derives its target from.
    const double wx0 = f.P[0] + lat - hx, wx1 = f.P[0] + lat + hx;
    const double wy0 = f.P[1] - hy, wy1 = f.P[1] + hy;
    const long i0 = long(std::floor((wx0 - f.lattice.origin[0]) / f.cell));
    const long i1 = long(std::ceil((wx1 - f.lattice.origin[0]) / f.cell)) - 1;
    const long j0 = long(std::floor((wy0 - f.lattice.origin[1]) / f.cell));
    const long j1 = long(std::ceil((wy1 - f.lattice.origin[1]) / f.cell)) - 1;
    rd.cellsX = int(i1 - i0 + 1);
    rd.cellsY = int(j1 - j0 + 1);
    rd.litPredicted = (long long)rd.cellsX * rd.cellsY * 2;
    modelFactor(f, lat, hx, hy);
    rd.M = f.M;
    rd.M1 = f.M1;
    double sv = 0, st = 0, sa = 0;
    for (int t = 0; t < 4; ++t) {
        const int x = kPoleTexel[t][0], y = kPoleTexel[t][1];
        const double v = (texelAt(unsigned(x), unsigned(y), 0) + texelAt(unsigned(x), unsigned(y), 1) +
                          texelAt(unsigned(x), unsigned(y), 2)) / 3.0;
        const V3 n = texelDir(x, y, int(R));
        rd.value[t] = v / double(vs.multiplier);
        rd.authored[t] = analytic(lat - hx, lat + hx, -hy, hy, f.D, n);
        rd.truth[t] = f.M * rd.authored[t];
        sv += rd.value[t];
        st += rd.truth[t];
        sa += rd.authored[t];
    }
    rd.ratio = st > 0 ? sv / st : 0.0;
    rd.ratioAuthored = sa > 0 ? sv / sa : 0.0;
    rd.lit = vs.voxelsLit;
    rd.peakRadiance = double(vs.peak) / double(vs.multiplier);
    rd.ok = true;
    return rd;
}

/// A fresh chain with the wall at (lat, hx, hy), built at `budget` and rendered `frames`.
static void buildWith(Fixture &f, double lat, double hx, double hy, int budget, int frames = 16)
{
    GiParams off; off.mode = GiMode::Off;
    f.scene->setGlobalIllumination(off);
    render(f.e, 2);
    enginetest::setNodePosition(f.scene, f.wall,
                                Vec3(float(f.P[0] + lat), float(f.P[1]), float(f.P[2] + f.D + 0.5 * f.cell)));
    enginetest::setNodeScale(f.scene, f.wall, Vec3(float(2 * hx), float(2 * hy), float(f.cell)));
    render(f.e, 2);
    f.scene->setGlobalIllumination(fieldGi(budget));
    render(f.e, frames);
}

/// ...and CONVERGED: built at budget 1, rendered until the field owes nothing (every
/// probe at the target sample count). Returns the frames it took (-1: it never did).
static int buildConverged(Fixture &f, double lat, double hx, double hy)
{
    buildWith(f, lat, hx, hy, 1, 1);
    int frames = 1;
    for (; frames < 4000 && f.scene->giStatus().ifdRefinesOwed > 0u; ++frames) render(f.e, 1);
    return f.scene->giStatus().ifdRefinesOwed == 0u ? frames : -1;
}

static bool setup(Fixture &f, Engine *e)
{
    f.e = e;
    f.view = e->createOffscreenView("thinwall", 128, 128, Colour(0, 0, 0));
    f.scene = e->createScene("thinwall");
    f.view->setScene(f.scene);
    f.scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    f.wall = f.scene->createNode();
    const MeshId cube = f.scene->createMesh(enginetest::unitCubeMesh());
    PbrParams wp;
    wp.albedo = Colour(0, 0, 0);
    wp.metalness = 0.0f;
    wp.roughness = 1.0f;
    wp.emissive = Colour(1, 1, 1);
    const MaterialId wm = f.scene->createPbrMaterial(wp);
    if (!f.wall || !cube || !wm || !f.scene->attachMesh(f.wall, cube, wm)) return false;
    enginetest::poseRegistry()[f.scene][f.wall] = enginetest::NodePose{};
    enginetest::setNodePosition(f.scene, f.wall, Vec3(0, 200, 0));
    enginetest::setNodeScale(f.scene, f.wall, Vec3(0.8f, 0.8f, 0.1f));
    const Vec3 cam(0.3f, 1.5f, 0.2f);
    f.view->setCamera(enginetest::testCameraDescLookAt(cam, Vec3(cam.x, cam.y, cam.z + 5)));
    render(e, 4);
    if (!f.scene->setGlobalIllumination(fieldGi(0))) return false;
    render(e, 12);
    const GiStatus st = f.scene->giStatus();
    GiFieldAtlas a0;
    if (!st.ifdBound || st.cascades.empty() || !f.scene->giFieldAtlas(a0)) return false;
    f.cell = st.cascades[0].cell;
    for (int k = 0; k < 3; ++k) f.N[k] = a0.probes[k];
    const double O[3] = { st.ifdMin.x, st.ifdMin.y, st.ifdMin.z };
    const double S[3] = { st.ifdMax.x - st.ifdMin.x, st.ifdMax.y - st.ifdMin.y, st.ifdMax.z - st.ifdMin.z };
    const double cc[3] = { cam.x, cam.y, cam.z };
    for (int k = 0; k < 3; ++k) {
        f.O[k] = O[k];
        f.sp[k] = float(S[k] * (f.N[k] + 2) / (double(f.N[k]) * f.N[k]));
        const double o2 = O[k] - S[k] / f.N[k];      // the field's enlarged origin: probe 0
        f.loc[k] = int(std::lround((cc[k] - o2) / f.sp[k]));
        if (k == 2) f.loc[k] -= 2;
        f.P[k] = o2 + f.loc[k] * double(f.sp[k]);
    }
    f.D = 4.0 * f.sp[2];
    // Cascade 0's lattice, for the model (the camera never moves, so it is fixed).
    GiVoxelVolume vol;
    if (!f.scene->giVoxelVolume(0, vol) || !vol.available) return false;
    f.lattice = voxelmodel::fromGpu(vol);
    std::printf("   field %ux%ux%u, spacing %.4f %.4f %.4f m, c0 cell %.4f m; probe (%d,%d,%d) at "
                "%.3f %.3f %.3f; wall near face %.3f m in +z; %u samples a probe (target)\n",
                f.N[0], f.N[1], f.N[2], f.sp[0], f.sp[1], f.sp[2], f.cell, f.loc[0], f.loc[1], f.loc[2],
                f.P[0], f.P[1], f.P[2], f.D, st.ifdTargetSamples);
    return true;
}

static double edgeLo(int n) { return double(n - 1) * (n - 1) / (double(n) * n); }

/// THE NOISE OF ONE SAMPLE. A probe's sample m is rotated by a function of its lattice
/// point and m, so the samples are read out of the probe's own mean as it refines: the
/// field built at budget 1 with a target of `k`, the probe read every frame, and each
/// time its count steps from c to c + 1 its new sample is (c + 1) x_{c+1} - c x_c.
struct Noise { double mean = 0, sd = 0; int n = 0; };
static Noise sampleNoise(Fixture &f, double lat, int k)
{
    Noise out;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", k);
    ::setenv("JAHSHAKA_GI_FIELD_SAMPLES", buf, 1);
    buildWith(f, lat, 0.4, 0.4, 1, 0);
    ::unsetenv("JAHSHAKA_GI_FIELD_SAMPLES");
    std::vector<double> v;
    Reading prev = readProbe(f, lat, 0.4, 0.4, true);
    if (prev.ok && prev.count >= 1.0) v.push_back(prev.ratio * prev.count);
    for (int fr = 0; fr < (k + 2) * 10 && (int)v.size() < k; ++fr) {
        render(f.e, 1);
        const Reading r = readProbe(f, lat, 0.4, 0.4, false);
        if (!r.ok || r.count <= prev.count) continue;
        if (r.count == prev.count + 1.0) v.push_back(r.count * r.ratio - prev.count * prev.ratio);
        prev = r;
    }
    out.n = int(v.size());
    if (v.size() < 2) return out;
    for (double x : v) out.mean += x;
    out.mean /= double(v.size());
    for (double x : v) out.sd += (x - out.mean) * (x - out.mean);
    out.sd = std::sqrt(out.sd / double(v.size() - 1));
    return out;
}

/// (a): the store's flux along the wall's normal (z) against the model's and the
/// authored faces' (front and back, 2 x 2hx x 2hy; the sides are edge-on along z).
struct StoreFlux { bool ok = false; double gpu = 0, model = 0, authored = 0; };
static StoreFlux storeFlux(Fixture &f, double lat, double hx, double hy)
{
    StoreFlux out;
    GiVoxelVolume vol;
    if (!f.scene->giVoxelVolume(0, vol) || !vol.available) return out;
    const voxelmodel::Store gpu = voxelmodel::fromGpu(vol);
    const voxelmodel::Store model = voxelmodel::modelStore(gpu, wallTris(f, lat, hx, hy), 1.0);
    double F[3], Fm[3];
    voxelmodel::flux(gpu, F);
    voxelmodel::flux(model, Fm);
    out.gpu = F[2];
    out.model = Fm[2];
    out.authored = 2.0 * (2 * hx) * (2 * hy);
    out.ok = true;
    return out;
}

static int runWall(Fixture &f, double sigma)
{
    std::printf("\n== PART 1: the thin wall against the authored wall and the directional model ==\n");
    const double lats[3] = { 0.0, f.sp[0] / 3.0, 2.0 * f.sp[0] / 3.0 };
    double K = 1.0, se = 2.0 * sigma;
    for (double lat : lats) {
        const int frames = buildConverged(f, lat, 0.4, 0.4);
        CHECK_MSG(frames > 0, "the field converged (%d frames at budget 1)", frames);
        K = double(std::max(1u, f.scene->giStatus().ifdTargetSamples));
        se = 2.0 * sigma / std::sqrt(K);
        const Reading r = readProbe(f, lat, 0.4, 0.4);
        if (!r.ok) { CHECK(false, "the probe reads back"); continue; }
        const StoreFlux sf = storeFlux(f, lat, 0.4, 0.4);
        const double tol = se + std::fabs(r.M1 - r.M) + 0.01;
        std::printf("   lateral %.3f m: %d x %d cells x 2 faces; lit voxels %lld (predicted %lld), peak "
                    "radiance %.3f; flux along the normal %.4f m2 (model %.4f, authored %.4f); the "
                    "model reads %.3f of the authored wall (one phase %.3f); the probe %.3f of the "
                    "authored wall, %.3f of the model's (count %.0f)\n",
                    lat, r.cellsX, r.cellsY, r.lit, r.litPredicted, r.peakRadiance, sf.gpu, sf.model,
                    sf.authored, r.M, r.M1, r.ratioAuthored, r.ratio, r.count);
        CHECK_MSG(sf.ok && r.lit == r.litPredicted && std::fabs(sf.gpu / sf.model - 1.0) <= 0.005 &&
                      std::fabs(sf.model / sf.authored - 1.0) <= 0.005,
                  "(a) THE STORE HOLDS THE WALL: %lld lit voxels = the %d x %d x 2 its faces cover; its flux "
                  "along the normal %.4f m2 = the model's %.4f = the authored faces' %.4f (+-0.5 %%)",
                  r.lit, r.cellsX, r.cellsY, sf.gpu, sf.model, sf.authored);
        CHECK_MSG(std::fabs(r.ratio - 1.0) <= tol,
                  "(d) THE PROBE READS THE WALL: %.3f of the authored irradiance, %.3f of the directional "
                  "model's prediction (M = %.3f), within %.3f (2 sigma/sqrt(K) %.3f at K %.0f + the "
                  "phase spread %.3f + 1 %% lobe)", r.ratioAuthored, r.ratio, r.M, tol, se, K,
                  std::fabs(r.M1 - r.M));
    }
    buildConverged(f, 0.0, 2.0, 2.0);
    const Reading big = readProbe(f, 0.0, 2.0, 2.0);
    const double tolBig = se + std::fabs(big.M1 - big.M) + 0.01;
    std::printf("   a 4 m wall: %lld lit voxels (predicted %lld); the model reads %.3f of the authored "
                "wall; the probe %.3f of it, %.3f of the model's\n", big.lit, big.litPredicted, big.M,
                big.ratioAuthored, big.ratio);
    CHECK_MSG(big.ok && std::fabs(big.ratio - 1.0) <= tolBig,
              "(b) THE MARCH COUNTS A WALL ONCE: a 4 m wall reads %.3f of the model's prediction "
              "(within %.3f)", big.ratio, tolBig);
    return 0;
}

static int runAlias(Fixture &f)
{
    std::printf("\n== PART 2: one ray that does not alias ==\n");
    const double lat = 0.0;
    // ---- 1 + 2. THE NOISE OF ONE INTEGRATION, rotated and static --------------
    struct NoiseArm { const char *rays; bool fixed; double lat; Noise n; };
    NoiseArm arms[5] = { { "1", false, 0.0, {} }, { "1", false, f.sp[0] / 3.0, {} },
                         { "1", false, 2.0 * f.sp[0] / 3.0, {} }, { "2", false, 0.0, {} },
                         { "1", true, 0.0, {} } };
    const int M = 48;
    for (NoiseArm &a : arms) {
        ::setenv("JAHSHAKA_GI_FIELD_RAYS", a.rays, 1);
        if (a.fixed) ::setenv("JAHSHAKA_GI_FIELD_STATIC", "1", 1);
        a.n = sampleNoise(f, a.lat, M);
        ::unsetenv("JAHSHAKA_GI_FIELD_STATIC");
        std::printf("   %s ray%s a texel, %s set, lateral %.3f: %d integrations, mean %.3f of the truth, "
                    "sigma %.3f\n", a.rays, a.rays[0] == '1' ? "" : "s", a.fixed ? "STATIC" : "rotated",
                    a.lat, a.n.n, a.n.mean, a.n.sd);
    }
    ::unsetenv("JAHSHAKA_GI_FIELD_RAYS");
    const double sigma1 = std::max(arms[0].n.sd, std::max(arms[1].n.sd, arms[2].n.sd));
    const double sigma2 = arms[3].n.sd;
    for (int i = 0; i < 3; ++i)
        CHECK_MSG(std::fabs(arms[i].n.mean - 1.0) <= 3.0 * arms[i].n.sd / std::sqrt(double(M)) + 0.17,
                  "ONE ROTATED RAY IS UNBIASED within its standard error and the wall's edge: mean "
                  "%.3f of the truth over %d integrations (lateral %.3f)", arms[i].n.mean, M, arms[i].lat);
    // THE TARGET, DERIVED FROM THE STORE: the smallest K whose two-sigma error sits
    // inside the wall's own trilinear edge uncertainty, 1 - ((n-1)/n)^2 (n the cells
    // its faces cover).
    const Reading wall = readProbe(f, lat, 0.4, 0.4);
    const int nCells = std::min(wall.cellsX, wall.cellsY);
    const double edgeTol = 1.0 - edgeLo(nCells);
    const int kDerived = int(std::ceil((2.0 * sigma1 / edgeTol) * (2.0 * sigma1 / edgeTol)));
    CHECK_MSG(arms[4].n.sd < 1e-6 && arms[0].n.sd > 0.0,
              "THE STATIC SET ALIASES: its reading never moves (sigma %.2g) - %.3f of the truth, "
              "whatever the integration count - where the rotated set's mean converges", arms[4].n.sd,
              arms[4].n.mean);
    std::printf("   K DERIVED: ceil((2 x worst sigma %.3f / %.3f)^2) = %d samples (the converged mean's "
                "two-sigma error inside the store's own edge uncertainty on the field's hardest case, a "
                "source of one ray's solid angle; %d cells)\n", sigma1, edgeTol, kDerived, nCells);

    // ---- 3. CONVERGENCE AT THE SHIPPED K, budget 1 ---------------------------
    // A build's own pass is the first sample everywhere; its K - 1 refinements run
    // at the budget until the field owes nothing.
    const int frames = buildConverged(f, lat, 0.4, 0.4);
    const GiStatus st = f.scene->giStatus();
    const Reading conv = readProbe(f, lat, 0.4, 0.4);
    std::printf("   budget 1: the field owes nothing %d frames after its build (K = %u samples a probe, %u "
                "probes a frame); converged reading %.3f of the truth (count %.0f)\n", frames, st.ifdTargetSamples,
                unsigned(st.ifdProbesPerFrame), conv.ratio, conv.count);
    // A MARGIN OF ONE SAMPLE (PHOTON-VOXEL-3): the derived K is a ceiling of a square in
    // sigma, and sigma is itself estimated from M integrations - its standard error is
    // sigma / sqrt(2 (M - 1)), 10 % at M = 48. K = 25 covers sigma up to 0.160 x 5 / 2 =
    // 0.400; the directional store measures 0.401 (the leaky store 0.389), 0.25 % over, and
    // that crossing moves the derived K from 25 to 26. One sample of margin is sigma +2 %,
    // a fifth of its own estimation error.
    CHECK_MSG(int(st.ifdTargetSamples) >= kDerived - 1,
              "THE SHIPPED TARGET COVERS THE DERIVED ONE, within one sample of sigma's own "
              "estimation error (%u >= %d - 1)", st.ifdTargetSamples, kDerived);
    CHECK_MSG(st.ifdRefinesOwed == 0u && conv.count >= double(st.ifdTargetSamples),
              "THE FIELD CONVERGES AND STOPS: %d frames at budget 1, the probe's count %.0f",
              frames, conv.count);
    const double se1 = 2.0 * sigma1 / std::sqrt(double(st.ifdTargetSamples));
    const double tol1 = se1 + std::fabs(conv.M1 - conv.M) + 0.01;
    CHECK_MSG(std::fabs(conv.ratio - 1.0) <= tol1,
              "CONVERGED AT 1 RAY, INSIDE PART 1'S BAR: %.3f of the model's prediction (within %.3f)",
              conv.ratio, tol1);
    bool still = true;
    for (int i = 0; i < 30; ++i) {
        render(f.e, 1);
        const Reading r = readProbe(f, lat, 0.4, 0.4);
        if (!r.ok || r.tile != conv.tile) still = false;
    }
    CHECK(still, "AT REST THE FIELD STOPS: 30 frames after convergence the probe's tile is byte-identical "
                 "(the frame-to-frame variance at rest is 0)");

    // ---- 4. 1 RAY AGAINST 2 RAYS, and the cost -------------------------------
    ::setenv("JAHSHAKA_GI_FIELD_RAYS", "2", 1);
    buildConverged(f, lat, 0.4, 0.4);
    const Reading two = readProbe(f, lat, 0.4, 0.4);
    ::unsetenv("JAHSHAKA_GI_FIELD_RAYS");
    buildConverged(f, lat, 0.4, 0.4);
    const Reading one = readProbe(f, lat, 0.4, 0.4);
    const double K = double(st.ifdTargetSamples);
    const double joint = 2.0 * std::sqrt(sigma1 * sigma1 / K + sigma2 * sigma2 / K);
    std::printf("   converged at K %.0f: 1 ray %.3f, 2 rays %.3f of the truth (joint 2-sigma %.3f)\n", K,
                one.ratio, two.ratio, joint);
    CHECK_MSG(std::fabs(one.ratio - two.ratio) <= joint,
              "1 RAY AND 2 RAYS CONVERGE TO THE SAME READING: %.3f against %.3f (joint two-sigma "
              "%.3f)", one.ratio, two.ratio, joint);
    // ---- 5. THE NOISE OF ONE INTEGRATION ON AN ORDINARY LIT ROOM --------------
    // What the field shows between a light change and its refinements (the first
    // integration after a change replaces the probe, kIfdKeepOnChange): the relative
    // standard deviation of a probe's mean irradiance over M single integrations, in
    // the leak room (a 10 m white room, a lamp inside, a lamp outside).
    {
        {
            GiParams off; off.mode = GiMode::Off;
            f.scene->setGlobalIllumination(off);
            render(f.e, 2);
        }
        Scene *room = f.e->createScene("noise-room");
        f.view->setScene(nullptr);
        f.view->setScene(room);
        enginetest::leakroom::build(room, f.view, 0.2f);
        render(f.e, 4);
        ::setenv("JAHSHAKA_GI_FIELD_SAMPLES", "17", 1);
        const bool built = room->setGlobalIllumination(fieldGi(1));
        ::unsetenv("JAHSHAKA_GI_FIELD_SAMPLES");
        std::printf("   the room: GI %s (%s), field %s, %d probes\n", built ? "built" : "REFUSED", f.e->lastError().c_str(),
                    room->giStatus().ifdBound ? "bound" : "UNBOUND", room->giStatus().ifdProbes);
        // Every probe's samples out of its own refining mean (read after each whole
        // pass: a probe whose count stepped by one gave up exactly one sample).
        std::vector<double> prevMean, prevCount, sum, sq, cnt;
        int reads = 0;
        for (int pass = 0; pass <= 17 && room->giStatus().ifdRefinesOwed > 0u; ++pass) {
            GiFieldAtlas at;
            if (room->giFieldAtlas(at) && at.available) {
                const unsigned total = at.probes[0] * at.probes[1] * at.probes[2];
                const unsigned B = at.irradBordered, W = at.irradWidth;
                if (sum.empty()) {
                    prevMean.assign(total, 0.0); prevCount.assign(total, 0.0);
                    sum.assign(total, 0.0); sq.assign(total, 0.0); cnt.assign(total, 0.0);
                }
                for (unsigned slot = 0; slot < total; ++slot) {
                    const unsigned x0 = (slot * B) % W, y0 = ((slot * B) / W) * B;
                    double m = 0.0, c = 0.0;
                    for (unsigned y = 1; y + 1 < B; ++y)
                        for (unsigned x = 1; x + 1 < B; ++x) {
                            const unsigned short *p = reinterpret_cast<const unsigned short *>(
                                &at.irradiance[(size_t(y0 + y) * W + x0 + x) * at.irradBytesPerTexel]);
                            m += (halfToFloat(p[0]) + halfToFloat(p[1]) + halfToFloat(p[2])) / 3.0;
                            c = halfToFloat(p[3]);
                        }
                    m /= double((B - 2) * (B - 2));
                    if (c == prevCount[slot] + 1.0) {
                        const double sample = c * m - prevCount[slot] * prevMean[slot];
                        sum[slot] += sample;
                        sq[slot] += sample * sample;
                        cnt[slot] += 1.0;
                    }
                    prevMean[slot] = m;
                    prevCount[slot] = c;
                }
                ++reads;
            }
            render(f.e, 8);
        }
        double peak = 0.0;
        for (size_t i = 0; i < sum.size(); ++i) if (cnt[i] > 0) peak = std::max(peak, sum[i] / cnt[i]);
        std::vector<double> rel;
        for (size_t i = 0; i < sum.size(); ++i) {
            if (cnt[i] < 4) continue;
            const double mean = sum[i] / cnt[i];
            if (mean < 0.01 * peak) continue;               // the unlit and the buried
            const double var = std::max(0.0, (sq[i] - cnt[i] * mean * mean) / (cnt[i] - 1));
            rel.push_back(std::sqrt(var) / mean);
        }
        std::sort(rel.begin(), rel.end());
        const double med = rel.empty() ? -1 : rel[rel.size() / 2];
        const double p95 = rel.empty() ? -1 : rel[size_t(double(rel.size()) * 0.95)];
        std::printf("   THE LEAK ROOM, one sample: %zu lit probes, %d reads - relative sigma median %.4f, "
                    "95th percentile %.4f\n", rel.size(), reads, med, p95);
        CHECK_MSG(!rel.empty() && med >= 0.0, "the room's probes were measured (%zu)", rel.size());
        GiParams off; off.mode = GiMode::Off;
        room->setGlobalIllumination(off);
        render(f.e, 2);
        f.view->setScene(nullptr);
        f.e->destroyScene(room);
        f.view->setScene(f.scene);
        render(f.e, 2);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "wall";
    std::printf("== gi.field_%s: the irradiance field's estimator on a one-voxel emissive wall\n",
                mode == "alias" ? "alias" : "thin_wall");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = mode == "alias" ? "test-gi-field-alias-ogre.log" : "test-gi-field-thin-wall-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Fixture f;
    CHECK(setup(f, engine.get()), "the chain, the field and the wall are up");
    if (!failures) {
        if (mode == "alias") runAlias(f);
        else {
            // The estimator's per-sample sigma at the shipped ray count, for the bar.
            const Noise nz = sampleNoise(f, 0.0, 24);
            std::printf("   one integration: sigma %.3f of the truth (%d integrations)\n", nz.sd, nz.n);
            runWall(f, nz.sd);
        }
    }
    GiParams off; off.mode = GiMode::Off;
    f.scene->setGlobalIllumination(off);
    render(f.e, 2);
    f.view->setScene(nullptr);
    f.e->destroyScene(f.scene);
    f.e->destroyView(f.view);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
