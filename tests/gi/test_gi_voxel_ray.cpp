// gi.voxel_ray_wall (PHOTON-7 stage 10, 10-1; L9: NO LIGHT PASSES A WALL) — THE CACHES' RAY READS OCCUPANCY: a wall in
// the OUTER cascade's 1.875 m cells stops a ray at every angle, the wall-ceiling edge cell stops it, the hit reads the
// side facing the ray, and the surface the ray left is never its occluder.
//
// THE DEFECT IT GUARDS (10-0's P5, spikes/photon-7/stage10/README.txt): the store marks a 0.4 m wall in the outer
// cascade (every wall cell's occupancy 1.000), but the aperture-0 march (jah_voxel_march.glsl at tan 0: the field's
// probe ray, and what the preflight first proposed for the caches) reads COVERAGE - the plane's dominant axis's, and
// the lateral axes' at the ray's crossing rate - so a wall that covers part of a cell's face passes the rest: MEASURED
// on the base, the 1 m wall here passed 0.44 of a ray at normal incidence and 0.19-0.59 over the sweep (the cone march's
// escape, printed beside every answer). (P5's one-cell numbers - opaque within 45 degrees, transparent past it, the
// edge cell 40 % - read one half of one plane; the march's ray reads both halves and accumulates the lateral axes, so
// a LONG wall and the edge cell held on the base.) The lead's ruling: THE RAY READS OCCUPANCY (jah_voxel_ray.glsl).
//
// THE FIXTURE: High, rays on, the camera at the origin; everything below stands where only the OUTERMOST cascade holds
// it (past the next-to-outer box), placed on that cascade's lattice once the chain is built:
//   W, a SHORT wall 0.4 m thick, 8 m tall, 1 m long, its lit face a quarter cell into a cell column (both faces in
//     one cell: a two-sided voxel), lit on that face by a sun, the sky black;
//   B, 10-0's sealed box (4 x 3 x 4 m inside, 0.4 m walls);
//   P, a plate 30 x 30 m, 0.4 m thick.
// Through Engine::voxelReaderParity's ray mode (VoxelReaderCone::kRay; the fragment and the compute halves both):
//   (1) THE SWEEP - rays aimed at the centre of W's lit face from 5 m away, 0, 5, ..., 85 degrees off its normal:
//       every one HITS, in the outer cascade, inside W's cells (never before the cell-rounded box of W, never past the
//       face) - RED ON THE BASE: the aperture-0 march takes a wall crossed at a grazing angle by the lateral crossing
//       rate (0.087 a plane at 85 degrees) over the one or two planes a 1 m wall gives it, and passes the rest;
//   (2) THE EDGE CELL - a ray from outside B at normal incidence through its wall-ceiling edge cell HITS there;
//   (3) THE SIDE - every sweep hit reads the side of W's voxel the ray is on (its entry point against the voxel's faces
//       along the canonical normal: the lit face; the store's own value, un-premultiplied), and the other side - W's
//       unlit face, whose rays leave W and must never read its lit side through it - holds under 1 % of it;
//   (4) THE ORIGIN PLANE - a ray 10 degrees off P's top, started one cascade-0 cell above it with its origin plane,
//       ESCAPES (P's own coarse cells are the hemisphere's boundary), and the same ray with no origin plane HITS in
//       P's own cell (so it is the plane that lets it through, not an empty cell).
// Every answer is also asserted equal between the two halves (the reader's parity). The aperture-0 cone march's
// escape for the same rays is PRINTED beside each (today's ray: the leak this row was red on).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p)
{
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
}   // namespace

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-voxel-ray-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("voxel_ray", 256, 256, Colour(0, 0, 0));
    Scene *scene = e->createScene("voxel_ray");
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.5f, 0.0f), Vec3(1.0f, 1.5f, 0.0f));
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    box(scene, Vec3(0.0f, -50.0f, 0.0f), Vec3(0.5f, 0.5f, 0.5f), matte);   // a speck: the chain builds before the fixture
    float sh[27] = { 0 };
    scene->setAmbientSh(sh);   // the sky black: W's unlit face holds nothing
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    enginetest::frames(e, 30);
    const GiStatus g0 = scene->published().gi;
    const size_t n = g0.cascades.size();
    if (n < 2) { std::printf("FAIL: a chain of %zu cascades\n", n); return 1; }
    const GiStatus::CascadeStatus &cL = g0.cascades[n - 1], &cP = g0.cascades[n - 2], &c0 = g0.cascades[0];
    const double cell = cL.cell;
    const double lo[3] = { cL.centre.x - cL.halfSize, cL.centre.y - cL.halfSize, cL.centre.z - cL.halfSize };
    std::printf("   the outer cascade %zu: cell %.4f, half %.2f, corner (%.3f %.3f %.3f), guaranteed radius %.2f; the "
                "next-to-outer's half %.2f\n", n - 1, cell, double(cL.halfSize), lo[0], lo[1], lo[2],
                double(cL.guaranteedRadius), double(cP.halfSize));
    // W's lit face a quarter cell into the first column past the next-to-outer box (+8 m)
    const double wantX = cP.centre.x + cP.halfSize + 8.0;
    const int col = int(std::floor((wantX - lo[0]) / cell));
    const double colLo = lo[0] + col * cell;
    const double xf = colLo + 0.25 * cell;
    box(scene, Vec3(float(xf + 0.2), 0.0f, 0.0f), Vec3(0.4f, 8.0f, 1.0f), matte);                        // W
    const double bx = cP.centre.x - cP.halfSize - 8.0;
    box(scene, Vec3(float(bx), -0.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), matte);                             // B
    box(scene, Vec3(float(bx), 3.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), matte);
    box(scene, Vec3(float(bx - 2.2), 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), matte);
    box(scene, Vec3(float(bx + 2.2), 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), matte);
    box(scene, Vec3(float(bx), 1.5f, -2.2f), Vec3(4.8f, 3.0f, 0.4f), matte);
    box(scene, Vec3(float(bx), 1.5f, 2.2f), Vec3(4.8f, 3.0f, 0.4f), matte);
    const double pz = cP.centre.z + cP.halfSize + 17.0;   // P past the next-to-outer box along z, under nothing
    box(scene, Vec3(0.0f, -10.0f, float(pz)), Vec3(30.0f, 0.4f, 30.0f), matte);                          // P
    enginetest::addDirectionalLight(scene, Vec3(1.0f, -0.25f, 0.1f), 3.0f);   // the sun on W's -x face
    int rest = 0;
    for (; rest < 600; ++rest) {
        enginetest::frames(e, 1);
        if (rest > 60 && scene->published().gi.giAtRest) break;
    }
    const GiStatus g = scene->published().gi;
    std::printf("   at rest after %d frames (%s); the row renders %d frames\n", rest, g.giAtRest ? "rest" : "NOT at rest",
                1 + 30 + rest + 1);
    for (size_t i = 0; i < n; ++i) {
        if (g.cascades[i].centre.x != g0.cascades[i].centre.x || g.cascades[i].centre.z != g0.cascades[i].centre.z) {
            std::printf("FAIL: cascade %zu moved while the fixture settled\n", i);
            return 1;
        }
    }
    GiVoxelVolume v;
    if (!scene->giVoxelVolume(int(n - 1), v) || !v.available) { std::printf("FAIL: the outer volume\n"); return 1; }
    const auto idx = [&](int x, int y, int z) { return (size_t(z) * v.height + y) * v.width + x; };
    const auto cellOf = [&](double w, int a) { return int(std::floor((w - v.origin[a]) / v.cell[a])); };
    if (std::fabs(v.origin[0] - lo[0]) > 1e-3 || std::fabs(v.cell[0] - cell) > 1e-5) {
        std::printf("FAIL: the volume's corner %.4f / cell %.5f is not the status's\n", double(v.origin[0]), double(v.cell[0]));
        return 1;
    }

    // world -> cascade 0's normalised space (its box: the centre minus the half; a cube)
    const double size0 = 2.0 * c0.halfSize;
    const double lo0[3] = { c0.centre.x - c0.halfSize, c0.centre.y - c0.halfSize, c0.centre.z - c0.halfSize };
    const auto toLS = [&](const double w[3]) {
        return Vec3(float((w[0] - lo0[0]) / size0), float((w[1] - lo0[1]) / size0), float((w[2] - lo0[2]) / size0));
    };
    const float lastShell = float(cL.guaranteedRadius / size0);
    struct Ray { double start[3]; double dir[3]; double bias[3]; };
    std::vector<Ray> rays;
    // (1) the sweep: aimed at the centre of W's lit face (at a height off every cell plane, 0.37 m) from kAim away
    const double yS = 0.37, kAim = 5.0;
    for (int d = 0; d <= 85; d += 5) {
        const double t = d * M_PI / 180.0;
        const double dir[3] = { std::cos(t), 0.0, std::sin(t) };
        Ray r = { { xf - kAim * dir[0], yS, 0.13 - kAim * dir[2] }, { dir[0], dir[1], dir[2] }, { 0.0, 0.0, 0.0 } };
        rays.push_back(r);
    }
    const size_t kSweep = rays.size();
    // (2) the edge cell of B's +x wall and its roof (the cell holding the wall's outer face at the roof's height, (bx +
    // 2.4, 3.05)), from outside at normal incidence
    const int ex = cellOf(bx + 2.4 - 1e-3, 0), ey = cellOf(3.05, 1), ez = cellOf(0.37, 2);
    const double eyLo = v.origin[1] + ey * cell;
    const double rayY = std::min(std::max(3.05, eyLo + 0.01), std::min(eyLo + cell, 3.4) - 0.01);
    const double exStart = v.origin[0] + (ex + 2.5) * cell;   // two empty columns out
    rays.push_back({ { exStart, rayY, 0.37 }, { -1.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } });
    // (4) off P's top at 10 degrees, one cascade-0 cell above it: with the origin plane, and without
    const double c0cell = c0.cell;
    const double el = 10.0 * M_PI / 180.0;
    rays.push_back({ { 0.0, -9.8 + c0cell, pz }, { 0.0, std::sin(el), std::cos(el) }, { 0.0, 1.0, 0.0 } });
    rays.push_back({ { 0.0, -9.8 + c0cell, pz }, { 0.0, std::sin(el), std::cos(el) }, { 0.0, 0.0, 0.0 } });

    std::vector<VoxelReaderCone> cones, march;
    for (const Ray &r : rays) {
        VoxelReaderCone k;
        k.posLS = toLS(r.start);
        k.dirLS = Vec3(float(r.dir[0]), float(r.dir[1]), float(r.dir[2]));
        k.biasDirLS = Vec3(float(r.bias[0]), float(r.bias[1]), float(r.bias[2]));
        k.tanHalfAngle = 0.0f;
        k.flags = VoxelReaderCone::kRay;
        k.rayLastShell = lastShell;
        cones.push_back(k);
        k.flags = 0u;   // today's ray: the aperture-0 cone march
        march.push_back(k);
    }
    std::vector<VoxelReaderAnswer> fr, co, mfr, mco;
    if (!e->voxelReaderParity(scene, cones, fr, co) || co.size() != cones.size() || fr.size() != cones.size()) {
        std::printf("FAIL: the harness: %s\n", e->lastError().c_str());
        return 1;
    }
    if (!e->voxelReaderParity(scene, march, mfr, mco) || mco.size() != march.size()) {
        std::printf("FAIL: the harness (the march): %s\n", e->lastError().c_str());
        return 1;
    }
    bool parity = true;
    for (size_t i = 0; i < co.size(); ++i)
        parity = parity && std::memcmp(&fr[i], &co[i], sizeof(VoxelReaderAnswer)) == 0;
    check(parity, "the fragment and the compute halves give the same answers, to the bit");

    const auto exitName = [](float x) { return x == 1.0f ? "HIT" : x == 2.0f ? "ESCAPED" : x == 0.0f ? "EXHAUSTED" : "?"; };
    // (1) + (3)
    const int wy = cellOf(yS, 1);
    bool sweepHits = true, sweepWhere = true, sideOk = true, sideReal = true;
    double worstSide = 0.0;
    for (size_t i = 0; i < kSweep; ++i) {
        const Ray &r = rays[i];
        const VoxelReaderAnswer &a = co[i];
        const double tHit = double(a.escape[0]) * size0;
        // the ray's entry into W's cell-rounded box (the slabs), and the face
        const double wLo[3] = { xf, -4.0, -0.5 }, wHi[3] = { xf + 0.4, 4.0, 0.5 };   // (aimed at z 0.13: off the cell plane z 0)
        double tCol = -1e30;
        for (int ax = 0; ax < 3; ++ax) {
            const double cLo = v.origin[ax] + std::floor((wLo[ax] - v.origin[ax]) / cell) * cell;
            const double cHi = v.origin[ax] + std::ceil((wHi[ax] - v.origin[ax]) / cell) * cell;
            if (r.dir[ax] > 0.0) tCol = std::max(tCol, (cLo - r.start[ax]) / r.dir[ax]);
            else if (r.dir[ax] < 0.0) tCol = std::max(tCol, (cHi - r.start[ax]) / r.dir[ax]);
        }
        const double tFace = kAim;
        // the hit cell (its entry nudged along the ray)
        const size_t vi = idx(cellOf(r.start[0] + (tHit + 1e-3) * r.dir[0], 0), wy,
                              cellOf(r.start[2] + (tHit + 1e-3) * r.dir[2], 2)) * 4u;
        const double nx = v.normal[vi] * 2.0 - 1.0, ny = v.normal[vi + 1] * 2.0 - 1.0, nz = v.normal[vi + 2] * 2.0 - 1.0;
        // the side the ray's entry point lies on, along the canonical normal's dominant axis (jah_voxel_ray.glsl): the
        // voxel's faces' coverage-weighted plane on that axis
        double along = -(nx * r.dir[0] + ny * r.dir[1] + nz * r.dir[2]);
        {
            const double an[3] = { std::fabs(nx), std::fabs(ny), std::fabs(nz) };
            const int na = an[0] >= an[1] ? (an[0] >= an[2] ? 0 : 2) : (an[1] >= an[2] ? 1 : 2);
            const double oP = v.coverageP[vi + na], oN = v.coverageN[vi + na];
            if (oP + oN > 0.0) {
                const double pP = oP > 0.0 ? std::min(std::max(v.positionP[vi + na] / oP, 0.0), 1.0) : 0.0;
                const double pN = oN > 0.0 ? std::min(std::max(v.positionN[vi + na] / oN, 0.0), 1.0) : 0.0;
                const int ci = na == 0 ? cellOf(r.start[0] + (tHit + 1e-3) * r.dir[0], 0) : na == 1 ? wy
                                       : cellOf(r.start[2] + (tHit + 1e-3) * r.dir[2], 2);
                const double m = v.origin[na] + (ci + (oP * pP + oN * pN) / (oP + oN)) * cell;
                const double e = r.start[na] + tHit * r.dir[na];
                const double sd = (e - m) * ((na == 0 ? nx : na == 1 ? ny : nz) >= 0.0 ? 1.0 : -1.0);
                if (sd != 0.0) along = sd;
            }
        }
        const float *front = &v.lightFront[vi], *back = &v.lightBack[vi];
        const float *facing = along < 0.0 ? back : front, *other = along < 0.0 ? front : back;
        const double w = front[3];
        double rel = 0.0, oth = 0.0, lum = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double want = w > 0.0 ? facing[c] / w : 0.0;
            rel = std::max(rel, std::fabs(double(a.march[c]) - want) / std::max(want, 1e-6));
            lum = std::max(lum, want);
            oth = std::max(oth, w > 0.0 ? other[c] / w : 0.0);
        }
        const double escMarch = 1.0 - std::min(1.0, double(mco[i].escape[0]) / 0.95);
        std::printf("   (1) %2d deg: %-9s cascade %.0f, t %.3f m (W's cells at %.3f, face at %.3f), %.0f cells; reads %.5f "
                    "(the facing side %.5f, the other %.5f, rel %.1e) | the cone march at tan 0 escapes %.3f\n",
                    int(i) * 5, exitName(a.march[3]), double(a.escape[1]), tHit, tCol, tFace, double(a.escape[2]),
                    double(a.march[1]), lum, oth, rel, escMarch);
        sweepHits = sweepHits && a.march[3] == 1.0f && int(a.escape[1]) == int(n - 1);
        sweepWhere = sweepWhere && tHit >= tCol - 1e-3 && tHit <= tFace + 1e-3;
        sideOk = sideOk && w > 0.0 && rel < 1e-4;
        sideReal = sideReal && oth < 0.01 * lum && lum > 1e-3;
        worstSide = std::max(worstSide, rel);
    }
    check(sweepHits, "(1) THE SWEEP: every ray 0-85 degrees off the wall's normal HITS, in the outer cascade");
    check(sweepWhere, "(1) ...inside the wall's cells (never before its cell-rounded box, never past the face)");
    check(sideOk, "(3) THE SIDE: every hit reads the side of the wall's voxel facing the ray, the store's own value");
    check(sideReal, "(3) ...and the other side holds under 1 % of it (the side is a real choice)");
    {
        const VoxelReaderAnswer &a = co[kSweep];
        const double tHit = double(a.escape[0]) * size0;
        const double tCol = exStart - (v.origin[0] + (ex + 1) * cell);
        const double tFace = exStart - (bx + 2.4);
        const size_t ei = idx(ex, ey, ez) * 4u;
        std::printf("   (2) the edge cell (%d %d %d): occupancy %.3f, coverage P_x %.3f; the ray at y %.3f: %s in cascade "
                    "%.0f at t %.3f m (column at %.3f, the outer face at %.3f) | the cone march at tan 0 escapes %.3f\n",
                    ex, ey, ez, double(v.albedo[ei + 3]), double(v.coverageP[ei]), rayY, exitName(a.march[3]),
                    double(a.escape[1]), tHit, tCol, tFace, 1.0 - std::min(1.0, double(mco[kSweep].escape[0]) / 0.95));
        check(a.march[3] == 1.0f && int(a.escape[1]) == int(n - 1) && tHit >= tCol - 1e-3 && tHit <= tFace + 1e-3,
              "(2) THE EDGE CELL: a ray through the wall-ceiling edge cell HITS there");
    }
    {
        const VoxelReaderAnswer &a = co[kSweep + 1], &b = co[kSweep + 2];
        std::printf("   (4) off the plate's top at 10 degrees: with the origin plane %s at t %.2f m (cascade %.0f, %.0f "
                    "cells; the last shell %.2f m); without it %s at t %.3f m (cascade %.0f)\n",
                    exitName(a.march[3]), double(a.escape[0]) * size0, double(a.escape[1]), double(a.escape[2]),
                    double(cL.guaranteedRadius), exitName(b.march[3]), double(b.escape[0]) * size0, double(b.escape[1]));
        check(a.march[3] == 2.0f, "(4) THE ORIGIN PLANE: a grazing ray off the plate ESCAPES (its own cells are no occluder)");
        check(b.march[3] == 1.0f && double(b.escape[0]) * size0 < cell,
              "(4) ...and with no origin plane it HITS in the plate's own cell (the plane is what lets it through)");
    }
    std::printf("%s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
