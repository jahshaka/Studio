// gi.voxel_coverage — THE VOXEL STORE HOLDS A SURFACE'S PROJECTED AREA PER AXIS
// (PHOTON-VOXEL-3; spikes/photon-voxel-3/).
//
// THE PHYSICS. The voxel store is a density field. A voxel keeps the MEAN radiance
// of the surfaces inside it and, per axis a, the fraction of its face those surfaces
// cover when seen along a — O_a = sum over the pieces of (area inside the voxel / face
// area) x opacity x |n . a|. A ray along a sees O_a of the voxel. So a wall covers its
// voxels along its normal and is invisible along its own plane, and a thin CLOSED box
// is right from every side: its faces opaque face-on, its sides opaque side-on, and the
// sides' voxels transparent to a ray crossing the wall. The light volume carries the
// mean radiance times the conservative opacity c = max(O_a), and the reader's mip-0
// read replaces c by the opacity along the ray (jah_voxel_sample.glsl).
//
// THE FIXTURE. A black world with one emitter of radiance L = 1: a closed box 0.8 m
// square and ONE or FOUR cascade-0 cells thick, and an OPEN QUAD (two triangles, no
// sides), turned 0, 15 and 45 degrees about y, near the camera (cascade 0); at 0
// degrees with its faces ON the cell boundaries (phase 0) and 0.37 of a cell off them.
// And a quad at material alpha 0.5. ddgi is OFF: the subject is the store.
//
// THE REFERENCE IS THE AUTHORED GEOMETRY, voxelised on the CPU by the same model
// (each triangle clipped exactly to each half-open voxel, O_a capped at 1): the store
// must BE the model, and the model's departure from the authored surface (the cap,
// where two faces project onto one voxel face) is stated, never a bar.
//
// WHAT IS READ (giVoxelVolume: cascade 0's mip 0 — the total light, the albedo and the
// per-axis coverage):
//   FLUX_a  sum over the voxels of the stored radiance (colour / c / k) x O_a x cell^2:
//           what the store shows a viewer along axis a, against the authored surfaces'
//           projection on a (L x sum A |n_a|) and against the CPU model's.
//   FACE-ON the reader's own march at mip 0 (half-cell samples, trilinear, the
//           directional read) along the wall's normal over a grid of rays, the flux
//           it returns against the front face's authored area x L; the opacity it
//           reaches at the wall's centre (a thin wall is OPAQUE along its normal).
//   ALONG   the same march IN the wall's plane through its centre: the opacity it
//           reaches, against the same march through the CPU model's store.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/voxelcoveragemodel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using namespace voxelmodel;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static void render(Engine *e, int frames = 1) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static GiParams storeGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.cascades = true;
    return gi;
}

enum class Shape { Box, Quad, HalfQuad };
struct Case {
    double angleDeg;
    Shape shape;
    int thick;          ///< cells (the box)
    double phase;       ///< of a cell, the front face off the boundary (0 degrees)
};

struct Result {
    bool ok = false;
    long long lit = 0;
    double F[3] = { 0, 0, 0 }, Fm[3] = { 0, 0, 0 }, P[3] = { 0, 0, 0 };
    double faceOn = 0, faceOnModel = 0;
    double opaque = 0, opaqueModel = 0;         ///< mean over the rays 1.5 cells inside the edges
    double opaqueMin = 1, opaqueMinModel = 1;   ///< ...and the least of them
    double along = 0, alongModel = 0;
    D3 centre{ 0, 0, 0 }, n{ 0, 0, 1 }, u{ 1, 0, 0 };   ///< where the surface stands (MEASURE-2 reads it)
    Store gpu;                                         ///< the store read back
};

static std::vector<Tri> worldTris(const MeshData &m, D3 pos, double th, D3 scale)
{
    std::vector<Tri> out;
    const double c = std::cos(th), s = std::sin(th);
    auto xf = [&](unsigned idx) {
        const D3 l{ m.positions[idx * 3] * scale.x, m.positions[idx * 3 + 1] * scale.y, m.positions[idx * 3 + 2] * scale.z };
        return D3{ pos.x + l.x * c + l.z * s, pos.y + l.y, pos.z - l.x * s + l.z * c };
    };
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
        out.push_back(Tri{ { xf(m.indices[i]), xf(m.indices[i + 1]), xf(m.indices[i + 2]) } });
    return out;
}

static const char *shapeName(Shape s) { return s == Shape::Box ? "box " : (s == Shape::Quad ? "quad" : "q50%"); }

static Result measure(Scene *scene, Engine *e, const std::map<Shape, NodeId> &nodes,
                      const std::map<Shape, MeshData> &meshes, const D3 &cam, const Case &c)
{
    Result r;
    const NodeId wall = nodes.at(c.shape);
    GiParams off; off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    render(e, 2);
    double park = 200;
    for (const auto &kv : nodes) { enginetest::setNodePosition(scene, kv.second, Vec3(0, float(park), 0)); park += 20; }
    scene->setGlobalIllumination(storeGi());
    render(e, 12);
    GiVoxelVolume v0;
    if (!scene->giVoxelVolume(0, v0) || !v0.available) return r;
    const double cell = v0.cell[0];
    const double T = c.shape == Shape::Box ? c.thick * cell : 0.0;
    const double th = c.angleDeg * M_PI / 180.0;
    const D3 n{ std::sin(th), 0.0, std::cos(th) };
    const D3 u{ std::cos(th), 0.0, -std::sin(th) };
    const double zf = v0.origin[2] + (std::floor((cam.z + 1.6 - v0.origin[2]) / cell) + c.phase) * cell;
    const double xc = v0.origin[0] + (std::floor((cam.x - v0.origin[0]) / cell) + 0.37) * cell;
    const double yc = v0.origin[1] + (std::floor((cam.y - v0.origin[1]) / cell) + 0.29) * cell;
    const D3 centre{ xc + n.x * 0.5 * T, yc, zf + n.z * 0.5 * T };
    const D3 scale{ 0.8, 0.8, c.shape == Shape::Box ? T : 1.0 };
    scene->setGlobalIllumination(off);
    render(e, 2);
    enginetest::NodePose &p = enginetest::poseRegistry()[scene][wall];
    p.pos = Vec3(float(centre.x), float(centre.y), float(centre.z));
    p.rot = Quat{ 0.f, float(std::sin(th / 2)), 0.f, float(std::cos(th / 2)) };
    p.scale = Vec3(float(scale.x), float(scale.y), float(scale.z));
    scene->setNodeTransform(wall, p.pos, p.rot, p.scale);
    render(e, 2);
    scene->setGlobalIllumination(storeGi());
    render(e, 12);
    for (int i = 0; i < 400 && !scene->giStatus().giAtRest; ++i) render(e, 1);
    GiVoxelVolume v;
    if (!scene->giVoxelVolume(0, v) || !v.available || v.coverageP.size() != v.light.size()) return r;
    const Store gpu = fromGpu(v);
    r.centre = centre; r.n = n; r.u = u; r.gpu = gpu;
    const double alpha = c.shape == Shape::HalfQuad ? 0.5 : 1.0;
    const Store model = modelStore(gpu, worldTris(meshes.at(c.shape), centre, th, scale), alpha);
    for (size_t i = 0; i < gpu.light.size(); i += 4)
        if (gpu.cov[0][i + 3] > 0.f) ++r.lit;
    flux(gpu, r.F);
    flux(model, r.Fm);
    // the authored projections (L = 1): faces +-n (area 0.64), sides +-u and +-y (0.8 T)
    for (int a = 0; a < 3; ++a) {
        const double pn = std::fabs(comp(n, a)), pu = std::fabs(comp(u, a)), py = a == 1 ? 1.0 : 0.0;
        r.P[a] = c.shape == Shape::Box ? 2 * 0.64 * pn + 2 * 0.8 * T * pu + 2 * 0.8 * T * py
                                       : alpha * 0.64 * pn;
    }
    // face-on: rays along +n from in front of the wall, over the wall +- 3 cells
    const D3 up{ 0, 1, 0 };
    const double half = 0.4 + 3 * cell, dA = cell / 4.0;
    const D3 front = add(centre, mul(n, -0.5 * T - 4 * cell));
    double fl = 0.0, flm = 0.0, op, opm;
    int interior = 0;
    const double inner = 0.4 - 1.5 * cell;
    for (double a = -half; a < half; a += dA)
        for (double b = -half; b < half; b += dA) {
            const D3 o = add(front, add(mul(u, a + 0.5 * dA), mul(up, b + 0.5 * dA)));
            fl += march(gpu, o, n, T + 8 * cell, op) * dA * dA;
            flm += march(model, o, n, T + 8 * cell, opm) * dA * dA;
            if (std::fabs(a + 0.5 * dA) < inner && std::fabs(b + 0.5 * dA) < inner) {
                ++interior;
                r.opaque += op; r.opaqueModel += opm;
                r.opaqueMin = std::min(r.opaqueMin, op); r.opaqueMinModel = std::min(r.opaqueMinModel, opm);
            }
        }
    r.faceOn = fl / 0.64;
    r.faceOnModel = flm / 0.64;
    r.opaque /= std::max(interior, 1);
    r.opaqueModel /= std::max(interior, 1);
    // along: in the plane (mid-thickness), from outside the wall's side, along +u
    const D3 side = add(centre, mul(u, -0.4 - 4 * cell));
    march(gpu, side, u, 0.8 + 8 * cell, r.along);
    march(model, side, u, 0.8 + 8 * cell, r.alongModel);
    std::printf("   %s %2.0f deg %d c ph %.2f | lit %5lld | FLUX/authored x %.3f y %.3f z %.3f"
                " (model %.3f %.3f %.3f) | face-on %.3f (model %.3f) | opq along n mean %.4f min %.4f"
                " (model %.4f / %.4f) | along-plane opq %.3f (model %.3f)\n",
                shapeName(c.shape), c.angleDeg, c.thick, c.phase, r.lit,
                r.P[0] > 1e-9 ? r.F[0] / r.P[0] : r.F[0], r.P[1] > 1e-9 ? r.F[1] / r.P[1] : r.F[1],
                r.P[2] > 1e-9 ? r.F[2] / r.P[2] : r.F[2],
                r.P[0] > 1e-9 ? r.Fm[0] / r.P[0] : r.Fm[0], r.P[1] > 1e-9 ? r.Fm[1] / r.P[1] : r.Fm[1],
                r.P[2] > 1e-9 ? r.Fm[2] / r.P[2] : r.Fm[2],
                r.faceOn, r.faceOnModel, r.opaque, r.opaqueMin, r.opaqueModel, r.opaqueMinModel, r.along,
                r.alongModel);
    r.ok = true;
    return r;
}

/// THE INJECTION'S SHADOW FOR AN AREA LAMP (PHOTON-VOXEL-4, INJECT-SHADOW-1). An LTC area lamp
/// lights a floor across a one-cell wall; the voxels' DIRECT light (no bounce) on the floor
/// behind the wall, against the floor before it. The injection's shadow march widens for an
/// area lamp (a coarser coverage mip per step); the mips hold a MEAN PER CELL, so the cells a
/// step crosses count as at mip 0 - a factor 2^-lod there let the widening march through the
/// wall. Bar: 5 % - the store's quantum (0.3 % a crossing) and the widening's 0.2-mip steps.
static void areaLampShadow(Engine *e, View *view)
{
    std::printf("\n== an LTC area lamp's voxel shadow through a one-cell wall ==\n");
    Scene *s = e->createScene("area-lamp-shadow");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams mp; mp.albedo = Colour(0.8f, 0.8f, 0.8f); mp.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(mp);
    const double cell = 8.0 / 64.0;   // the fitted volume below at Medium: 64 cells over 8 m
    const NodeId floorN = s->createNode(), wallN = s->createNode();
    s->attachMesh(floorN, cube, mat);
    s->attachMesh(wallN, cube, mat);
    s->setNodeTransform(floorN, Vec3(0, -0.05f, 0), Quat(), Vec3(7.5f, 0.1f, 7.5f));
    s->setNodeTransform(wallN, Vec3(float(0.5 * cell), 1.5f, 0), Quat(), Vec3(float(cell), 3.0f, 6.0f));
    const NodeId lamp = s->createNode();
    const float h = 0.70710678f;
    s->setNodeTransform(lamp, Vec3(-1.5f, 1.5f, 0.0f), Quat(0.0f, 0.0f, h, h), Vec3(1, 1, 1));   // -Y turned to +X
    LightDesc ld;
    ld.type = LightType::Area;
    ld.accurate = true;           // LTC: the injection's widening shadow march
    ld.rectWidth = 1.0f; ld.rectHeight = 1.0f;
    ld.intensity = 20.0f;
    ld.range = 20.0f;
    ld.castShadows = true;
    s->setLight(lamp, ld);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 0;            // the DIRECT term alone
    gi.cascades = false;
    gi.ddgi = GiToggle::Off;
    gi.testBoundsMin = Vec3(-4.0f, -4.0f, -4.0f);
    gi.testBoundsMax = Vec3(4.0f, 4.0f, 4.0f);
    CHECK_MSG(s->setGlobalIllumination(gi), "%s", "the area-lamp fixture's voxel arm builds");
    render(e, 8);
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available) { CHECK_MSG(false, "%s", "the area-lamp store reads back"); e->destroyScene(s); return; }
    const auto band = [&](double x0, double x1) {
        double sum = 0.0; long n = 0;
        for (int z = 0; z < v.depth; ++z) for (int y = 0; y < v.height; ++y) for (int x = 0; x < v.width; ++x) {
            const double wx = v.origin[0] + (x + 0.5) * v.cell[0], wy = v.origin[1] + (y + 0.5) * v.cell[1],
                         wz = v.origin[2] + (z + 0.5) * v.cell[2];
            if (wx < x0 || wx > x1 || wy < 0.0 || wy > double(v.cell[1]) || std::fabs(wz) > 1.0) continue;   // the floor's top face: the voxel ABOVE y = 0 (half-open)
            const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4;
            sum += v.light[i]; ++n;
        }
        return n ? sum / n : 0.0;
    };
    const double nearL = band(-1.0, -0.4), farL = band(1.5, 3.0);   // far: 12-24 steps behind the wall, where the widening march reads coarse mips
    std::printf("   the floor's direct light in the voxels: before the wall %.5f, behind it %.5f (%.2f %%)\n",
                nearL, farL, nearL > 0 ? 100.0 * farL / nearL : 0.0);
    CHECK_MSG(nearL > 0.0 && farL <= 0.05 * nearL,
              "AN AREA LAMP'S VOXEL SHADOW HOLDS through a one-cell wall: behind it %.2f %% of before it (<= 5 %%)",
              nearL > 0 ? 100.0 * farL / nearL : 0.0);
    GiParams off; off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    e->destroyScene(s);
}

/// MEASURE-2 (a) (PHOTON-VOXEL-4; the audit's A.2): AN OPEN SHEET AT 45 DEGREES TO THE LATTICE,
/// crossed by a ray at 0 / 30 / 60 / 90 degrees to its plane through its centre. The physics: an
/// opaque sheet stops every ray that crosses it (1 at 30-90 degrees) and none that runs in its
/// plane (0 at 0 degrees). The additive per-axis read sums the sheet's projections on the axes
/// the ray crosses and caps at 1, so a grazing ray may over-occlude up to the cap. PRINTED, no
/// bar: the engine's reader (a ray, the parity harness's compute arm) and the CPU mip-0 march
/// over the same store.
static void measureSheet(Engine *e, Scene *scene, const Result &r)
{
    std::printf("\n== MEASURE-2 (a): the 45-degree open sheet, a ray at 0/30/60/90 degrees to its plane ==\n");
    GiVoxelVolume v;
    if (!scene->giVoxelVolume(0, v) || !v.available) { std::printf("   (no store)\n"); return; }
    const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height, double(v.cell[2]) * v.depth };
    for (double deg : { 0.0, 30.0, 60.0, 90.0 }) {
        const double t = deg * M_PI / 180.0;
        const D3 d = add(mul(r.u, std::cos(t)), mul(r.n, std::sin(t)));
        const D3 o = add(r.centre, mul(d, -1.0));   // one metre before the sheet's centre
        VoxelReaderCone c;
        double dl[3] = { d.x / size[0], d.y / size[1], d.z / size[2] }, l = 0.0;
        for (double q : dl) l += q * q;
        l = std::sqrt(l);
        c.posLS = Vec3(float((o.x - v.origin[0]) / size[0]), float((o.y - v.origin[1]) / size[1]),
                       float((o.z - v.origin[2]) / size[2]));
        c.dirLS = Vec3(float(dl[0] / l), float(dl[1] / l), float(dl[2] / l));
        c.biasDirLS = Vec3(0, 0, 0);
        c.tanHalfAngle = 0.0f;
        c.flags = 0u;
        std::vector<VoxelReaderAnswer> fr, co;
        const bool ok = e->voxelReaderParity(scene, { c }, fr, co) && co.size() == 1;
        double opCpu = 0.0;
        march(r.gpu, o, d, 2.0, opCpu);
        std::printf("   %2.0f deg to the plane: the reader's ray alpha %.3f | the CPU mip-0 march %.3f | the "
                    "physics %.0f\n", deg, ok ? double(co[0].march[3]) : -1.0, opCpu, deg > 0.0 ? 1.0 : 0.0);
    }
}

/// MEASURE-2 (b) (the audit's A.1): A LIT ROOF THINNER THAN THE CELL over a closed dark room, at
/// cascade 2 (0.47 m cells) and cascade 3 (1.875 m cells). One voxel holds both faces of the
/// 0.1 m roof: over the roof's voxels above the room's interior, the light the faces looking +y
/// (the top) and those looking -y (the underside, lit by nothing but the dark room) carry at level
/// 0. PHOTON-VOXEL-5 (ii), LIGHT PER FACE SIDE: the injection lights each side of a two-sided voxel
/// from its own side (the front along the canonical normal, the back against it) and the bounce
/// gathers each side's own hemisphere, so the underside holds the room's light - none. BAR: the
/// underside at most the top's x 2^-10 (the half-float store's quantum at the top's level); before
/// (ii) it read 1.00 of the top's at cascade 2 and 0.95 at cascade 3.
/// THREE LAYERS IN ONE CELL (PHOTON-VOXEL-5; the VOXEL-4 audit's F2): one mesh of three full
/// quads facing -z at 0.1 / 0.5 / 0.9 of one cascade cell along z - three times the z field's
/// saturation (2.0 of full coverage). The coverage caps; the store's surface position must stay
/// the mean of the surfaces it counted, 0.5 (it read 0.75 when the position sum kept adding past
/// the cap). Bar: 0.01 of a cell - the 1/512 grid per contribution over three layers and the
/// position's 16-bit store, with room.
static void measureLayers(Engine *e, View *view)
{
    std::printf("\n== three full layers in one cell: the mean position past the coverage cap ==\n");
    Scene *s = e->createScene("layers");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const double cell = 8.0 / 64.0;      // the fitted volume below: Medium, 64 cells over 8 m
    const double z0 = 4.0;               // a cell boundary (the box is anchored at 0)
    MeshData md;
    for (double f : { 0.1, 0.5, 0.9 }) {
        const float z = float(z0 + f * cell);
        const unsigned base = unsigned(md.positions.size() / 3);
        const float q[4][2] = { { 2.0f, 2.0f }, { 2.5f, 2.0f }, { 2.5f, 2.5f }, { 2.0f, 2.5f } };
        for (auto &v : q) {
            md.positions.insert(md.positions.end(), { v[0], v[1], z });
            md.normals.insert(md.normals.end(), { 0.f, 0.f, -1.f });
        }
        md.indices.insert(md.indices.end(), { base, base + 2u, base + 1u, base, base + 3u, base + 2u });
    }
    PbrParams mp;
    mp.albedo = Colour(0, 0, 0);
    mp.emissive = Colour(1, 1, 1);
    mp.roughness = 1.0f;
    const NodeId n = s->createNode();
    CHECK_MSG(n && s->attachMesh(n, s->createMesh(md), s->createPbrMaterial(mp)), "%s", "the three layers attach");
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 0;
    gi.cascades = false;
    gi.ddgi = GiToggle::Off;
    gi.testBoundsMin = Vec3(0.0f, 0.0f, 0.0f);
    gi.testBoundsMax = Vec3(8.0f, 8.0f, 8.0f);
    CHECK_MSG(s->setGlobalIllumination(gi), "%s", "the layers' volume builds");
    render(e, 8);
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available) { CHECK_MSG(false, "%s", "the layers' store reads back"); e->destroyScene(s); return; }
    const int zi = int(std::floor((z0 + 0.5 * cell - v.origin[2]) / v.cell[2]));
    const int xi = int(std::floor((2.25 - v.origin[0]) / v.cell[0])), yi = int(std::floor((2.25 - v.origin[1]) / v.cell[1]));
    const size_t i = ((size_t(zi) * v.height + yi) * v.width + xi) * 4;
    const double o = v.coverageN[i + 2], po = v.positionN[i + 2];
    const double p = o > 0 ? po / o * v.depth - zi : -1.0;   // the mean in the cell, [0, 1]
    std::printf("   cell (%d, %d, %d) of %.4f m: coverage along z %.4f (the cap 1), the mean position %.4f of the cell\n",
                xi, yi, zi, double(v.cell[2]), o, p);
    CHECK_MSG(o > 0.99 && std::fabs(p - 0.5) <= 0.01,
              "THE MEAN POSITION PAST THE CAP: three layers at 0.1 / 0.5 / 0.9 of a cell read %.4f (0.5 +- 0.01)", p);
    GiParams off; off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    e->destroyScene(s);
}

static void measureRoof(Engine *e, View *view)
{
    std::printf("\n== MEASURE-2 (b): a 0.1 m lit roof over a closed dark room, cascades 2 and 3 ==\n");
    Scene *s = e->createScene("roof");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0, 1.5f, 0), Vec3(0, 1.5f, 5)));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams mp; mp.albedo = Colour(0.8f, 0.8f, 0.8f); mp.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(mp);
    const auto slab = [&](double x, double y, double z, double sx, double sy, double sz) {
        const NodeId nd = s->createNode();
        s->attachMesh(nd, cube, mat);
        s->setNodeTransform(nd, Vec3(float(x), float(y), float(z)), Quat(), Vec3(float(sx), float(sy), float(sz)));
    };
    const double rooms[2] = { 11.0, 40.0 };   // inside cascade 2's +-15 m / only in cascade 3's +-60 m
    for (double cx : rooms) {
        slab(cx, -0.25, 0.0, 7.0, 0.5, 7.0);                // the floor
        slab(cx, 3.05, 0.0, 7.0, 0.1, 7.0);                 // THE ROOF, 0.1 m
        slab(cx - 3.35, 1.5, 0.0, 0.3, 3.0, 7.0);           // four walls, 0.3 m
        slab(cx + 3.35, 1.5, 0.0, 0.3, 3.0, 7.0);
        slab(cx, 1.5, -3.35, 7.0, 3.0, 0.3);
        slab(cx, 1.5, 3.35, 7.0, 3.0, 0.3);
    }
    enginetest::addDirectionalLight(s, Vec3(0.1f, -1.0f, 0.2f), 3.0f);
    // at one bounce (the shipped default: the direct term and the cones reading it) and at two
    // (a bounce pass: each side of a two-sided voxel gathers its own hemisphere, and step 0 adds
    // the bounce's part to the directional level 0 per half-axis)
    for (const int bounces : { 1, 2 }) {
    GiParams roofGi = storeGi();
    roofGi.numBounces = bounces;
    CHECK_MSG(s->setGlobalIllumination(roofGi), "the roof fixture's chain builds (%d bounce(s))", bounces);
    render(e, 12);
    for (int i = 0; i < 400 && !s->giStatus().giAtRest; ++i) render(e, 1);
    std::printf("   %d bounce(s):\n", bounces);
    const int cascades[2] = { 2, 3 };
    for (int k = 0; k < 2; ++k) {
        GiVoxelVolume v;
        if (!s->giVoxelVolume(cascades[k], v) || !v.available) { std::printf("   cascade %d: no store\n", cascades[k]); continue; }
        const Store st = fromGpu(v);
        double top = 0.0, under = 0.0;
        long n = 0;
        for (int z = 0; z < st.D; ++z)
            for (int y = 0; y < st.H; ++y)
                for (int x = 0; x < st.W; ++x) {
                    const double lo[3] = { st.origin[0] + x * st.cell, st.origin[1] + y * st.cell, st.origin[2] + z * st.cell };
                    if (lo[0] + st.cell <= rooms[k] - 2.5 || lo[0] >= rooms[k] + 2.5 || lo[2] + st.cell <= -2.5 || lo[2] >= 2.5)
                        continue;   // the voxels over the room's interior
                    if (lo[1] > 3.1 || lo[1] + st.cell < 3.0) continue;   // ...holding the roof
                    const size_t i = st.at(x, y, z);
                    const double c = st.cov[0][i + 3];
                    if (c <= 0.0) continue;
                    double radTop = 0.0, radUnder = 0.0;
                    for (int ch = 0; ch < 3; ++ch) {
                        radTop += st.sideLight(i, 1, 0, ch) / 3.0 / st.k / c;
                        radUnder += st.sideLight(i, 1, 1, ch) / 3.0 / st.k / c;
                    }
                    top += radTop * st.cov[0][i + 1];
                    under += std::max(radUnder, 0.0) * st.cov[1][i + 1];
                    ++n;
                }
        std::printf("   cascade %d (cell %.3f m, room at x = %.0f): %ld roof voxels; the top's radiance x coverage "
                    "%.4f, the UNDERSIDE's %.6f (%.5f of the top's; the physics 0)\n", cascades[k], st.cell, rooms[k],
                    n, n ? top / n : 0.0, n ? under / n : 0.0, top > 0.0 ? under / top : 0.0);
        // THE SHIPPED DEFAULT (one bounce: the direct term the injection writes per side and per
        // half) is asserted; a BOUNCE PASS prints its residual - measured 0.022 of the top's at
        // cascade 2 and 0.005 at cascade 3 (PHOTON-VOXEL-5, reported to the lead; the mechanism UNVERIFIED - the likely one: the back side's
        // own gather meets two-sided CORNER voxels at level 0 - where a wall's top meets the roof -
        // whose two sides mix orientations; level 0 keeps two sides, the directional levels six).
        if (bounces == 1)
            CHECK_MSG(n > 0 && top > 0.0 && under <= top * (1.0 / 1024.0),
                      "A LIT ROOF'S UNDERSIDE HOLDS NONE OF THE TOP'S LIGHT at cascade %d: %.5f of the top's "
                      "(<= 2^-10)", cascades[k], top > 0.0 ? under / top : -1.0);
    }
    }
    GiParams off; off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    e->destroyScene(s);
}

int main(int argc, char **argv)
{
    const bool measureOnly = argc > 1 && std::strcmp(argv[1], "measure") == 0;
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-voxel-coverage-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("coverage", 128, 128, Colour(0, 0, 0));
    Scene *scene = e->createScene("coverage");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    std::map<Shape, MeshData> meshes;
    meshes[Shape::Box] = enginetest::unitCubeMesh();
    MeshData qd;
    qd.positions = { -0.5f, -0.5f, 0.f, 0.5f, -0.5f, 0.f, 0.5f, 0.5f, 0.f, -0.5f, 0.5f, 0.f };
    qd.normals = { 0.f, 0.f, -1.f, 0.f, 0.f, -1.f, 0.f, 0.f, -1.f, 0.f, 0.f, -1.f };
    qd.indices = { 0u, 2u, 1u, 0u, 3u, 2u };
    meshes[Shape::Quad] = qd;
    meshes[Shape::HalfQuad] = qd;

    PbrParams wp;
    wp.albedo = Colour(0, 0, 0);
    wp.metalness = 0.0f;
    wp.roughness = 1.0f;
    wp.emissive = Colour(1, 1, 1);
    PbrParams hp = wp;
    hp.alphaMode = PbrAlphaMode::Blend;
    hp.alpha = 0.5f;
    std::map<Shape, NodeId> nodes;
    for (Shape s : { Shape::Box, Shape::Quad, Shape::HalfQuad }) {
        const NodeId node = scene->createNode();
        const MeshId mesh = scene->createMesh(meshes[s]);
        const MaterialId mat = scene->createPbrMaterial(s == Shape::HalfQuad ? hp : wp);
        if (!node || !mesh || !mat || !scene->attachMesh(node, mesh, mat)) {
            std::printf("FAIL: fixture\n");
            return 1;
        }
        enginetest::poseRegistry()[scene][node] = enginetest::NodePose{};
        nodes[s] = node;
    }
    const D3 cam{ 0.3, 1.5, 0.2 };
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(float(cam.x), float(cam.y), float(cam.z)),
                                                     Vec3(float(cam.x), float(cam.y), float(cam.z + 5))));
    render(e, 4);

    const Case cases[] = {
        { 0, Shape::Quad, 0, 0.0 }, { 0, Shape::Quad, 0, 0.37 }, { 15, Shape::Quad, 0, 0.0 }, { 45, Shape::Quad, 0, 0.0 },
        { 0, Shape::Box, 1, 0.0 }, { 0, Shape::Box, 1, 0.37 }, { 15, Shape::Box, 1, 0.0 }, { 45, Shape::Box, 1, 0.0 },
        { 0, Shape::Box, 4, 0.0 }, { 0, Shape::Box, 4, 0.37 }, { 15, Shape::Box, 4, 0.0 }, { 45, Shape::Box, 4, 0.0 },
        { 0, Shape::HalfQuad, 0, 0.37 }, { 45, Shape::HalfQuad, 0, 0.0 },
    };
    std::printf("== the store against the authored surfaces and the CPU model (L = 1) ==\n");
    for (const Case &c : cases) {
        const Result r = measure(scene, e, nodes, meshes, cam, c);
        CHECK_MSG(r.ok, "%s %.0f deg: the store read back", shapeName(c.shape), c.angleDeg);
        if (measureOnly || !r.ok) continue;
        const char *nm = shapeName(c.shape);
        const double deg = c.angleDeg;
        // (1) THE STORE IS THE MODEL, per axis. The per-axis sums are on a 1/512 grid
        // per contribution (+-1/1024) and resolve to 10 bits (+-1/2046): a voxel is
        // within 0.0015 of the model, the integral within 0.5 %.
        for (int a = 0; a < 3; ++a) {
            if (r.Fm[a] > 0.01)
                CHECK_MSG(std::fabs(r.F[a] / r.Fm[a] - 1.0) <= 0.005,
                          "%s %.0f deg %d c: THE STORE IS THE MODEL along %c: flux %.4f m2 against the "
                          "model's %.4f (+-0.5 %%)", nm, deg, c.thick, "xyz"[a], r.F[a], r.Fm[a]);
            else
                CHECK_MSG(r.F[a] < 0.001, "%s %.0f deg %d c: nothing along %c (flux %.5f m2; the "
                          "surface is edge-on)", nm, deg, c.thick, "xyz"[a], r.F[a]);
        }
        // (2) AN OPEN SURFACE IS EXACT: the store's flux along each axis is the authored
        // projection (the model has nothing to cap).
        if (c.shape != Shape::Box)
            for (int a = 0; a < 3; ++a)
                if (r.P[a] > 0.01)
                    CHECK_MSG(std::fabs(r.F[a] / r.P[a] - 1.0) <= 0.005,
                              "%s %.0f deg: THE SURFACE INTEGRAL IS THE AUTHORED ONE along %c: %.4f "
                              "(+-0.5 %%)", nm, deg, "xyz"[a], r.F[a] / r.P[a]);
        // (3) WHAT THE READER SEES: the march over the store against the march over the
        // model (the directional model's own value, derived - never the envelope's).
        CHECK_MSG(std::fabs(r.faceOn - r.faceOnModel) <= 0.01,
                  "%s %.0f deg %d c: face-on flux %.3f against the model's %.3f (+-0.01)", nm, deg,
                  c.thick, r.faceOn, r.faceOnModel);
        CHECK_MSG(r.opaque >= r.opaqueModel - 0.005 && r.opaqueMin >= r.opaqueMinModel - 0.01,
                  "%s %.0f deg %d c: opacity along the normal, mean %.4f min %.4f, against the "
                  "model's %.4f / %.4f", nm, deg, c.thick, r.opaque, r.opaqueMin, r.opaqueModel,
                  r.opaqueMinModel);
        CHECK_MSG(r.along <= r.alongModel + 0.005,
                  "%s %.0f deg %d c: opacity along the plane %.3f, at most the model's %.3f", nm, deg,
                  c.thick, r.along, r.alongModel);
        // (4) A THIN WALL IS OPAQUE ALONG ITS NORMAL: every closed box at every angle,
        // and a flat open quad. (A TILTED open quad is held to the model above: an
        // infinitely thin sheet split between voxels reads 0.97 along its normal
        // through the trilinear half-footprint march - the finding in the evidence.)
        if (c.shape == Shape::Box || (c.shape == Shape::Quad && deg == 0.0))
            CHECK_MSG(r.opaque >= 0.98 && r.opaqueMin >= 0.98,
                      "%s %.0f deg %d c: OPAQUE ALONG ITS NORMAL (mean %.4f, least %.4f; >= 0.98)", nm,
                      deg, c.thick, r.opaque, r.opaqueMin);
        if (c.shape == Shape::Quad && deg == 0.0) {
            CHECK_MSG(std::fabs(r.faceOn - 1.0) <= 0.005,
                      "quad 0 deg: a ray along the normal sees the authored emission, flux %.4f (+-0.5 %%)",
                      r.faceOn);
            CHECK_MSG(r.along <= 0.005, "quad 0 deg: INVISIBLE ALONG ITS PLANE (opacity %.4f)", r.along);
        }
        // (5) A HALF-TRANSPARENT SURFACE IS HALF ITS OPAQUE SELF: the store premultiplies
        // the coverage by the material's opacity, so the reader sees half the
        // emission and half the occlusion.
        if (c.shape == Shape::HalfQuad) {
            CHECK_MSG(std::fabs(r.faceOn - 0.5) <= 0.005,
                      "q50%% %.0f deg: HALF ITS OPAQUE SELF face-on: flux %.4f (0.5 +-0.005)", deg, r.faceOn);
            CHECK_MSG(std::fabs(r.opaque - 0.5) <= 0.01,
                      "q50%% %.0f deg: ...and half as opaque: %.4f (0.5 +-0.01)", deg, r.opaque);
        }
    }
    {   // MEASURE-2 (a): the 45-degree open quad, at rest, read by rays
        const Result r = measure(scene, e, nodes, meshes, cam, Case{ 45, Shape::Quad, 0, 0.0 });
        if (r.ok) measureSheet(e, scene, r);
    }
    if (!measureOnly) areaLampShadow(e, view);
    if (!measureOnly) measureLayers(e, view);
    measureRoof(e, view);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
