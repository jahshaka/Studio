// gi.soft_mover_ghost / gi.mover_bus_sky (PHOTON-7 stage 8A, item A1; ONE_PICTURE B2) - A MOVER IS GI GEOMETRY.
//
// The renderer follows the ray through geometry: a thing that moves occludes and bounces like any other, so the voxel
// store holds it at its live pose - by REGION (PhotonComponent::serviceRegions: each frame it moves, its swept box is
// re-voxelised in every cascade it meets). Two fixtures (the engine alone, one offscreen view, the rays off: the store's
// own rays carry the bounce, so what is read is the store). Each law is a closed comparison against THE ORACLE - the
// chain torn down and built fresh at the final scene (GI off, then on) - read from the store itself
// (Scene::giVoxelVolume, level 0 of every cascade):
//   `ghost` (gi.soft_mover_ghost): A DOOR OPENED IN PLAY LEAVES NO CLOSED-DOOR BOUNCE. A still door (1 x 2 x 0.1 m) in a
//       sky-lit court (a solid 2 m floor slab, its top at 0) is promoted Movable mid-play (the play-time promotion; it was MobilityChange::Soft, deleted with its ghost) and slid 1.6 m open
//       over 32 frames, then left to rest. At rest every cascade's GEOMETRY (albedo and the six half-axis coverages) is
//       the oracle's BIT FOR BIT (a region's chunk build is a whole build's arithmetic), and its LIGHT within TWO codes
//       (a code = 1 % of the voxel's radiance per channel, over one half step of the store) at every voxel the oracle
//       lights: the feedback stops each brick within one code of the fixed point (PhotonBrickFeedback_cs.glsl's stop
//       rule), so two rests reached by two paths - the moved store's and the fresh build's - lie within two. RED
//       ON THE BASE: the soft promotion left the door's voxels at the closed pose (the ghost) and the oracle has none.
//   `bus_geometry` (gi.mover_bus_geometry) and `bus` (gi.mover_bus_sky): A BUS 10 m AWAY OCCLUDES THE FLOOR'S SKY SHARE. A movable 2.5 x 2.5 x 10 m box drives
//       3 m along x, 10 m from the camera (past the mover hit gate's 4 m reach), and stops; the court is lit by the sky
//       alone. At rest every cascade's geometry is the oracle's bit for bit (no cell it drove through keeps a face - a
//       face on a cell boundary is written into the cell past it too: the region takes a cell past the box), and the
//       floor's voxels beside its final pose (cascade 1's light: `bus`) equal the oracle's within two codes (the store took the
//       bus where it stopped) AND read darker than the same floor with no bus by more than four codes on average (the
//       bus occludes their sky share - the law A1 makes true: RED ON THE BASE, a mover was in no store). PRINTED: the
//       region rebuilds' price while it drives - each cascade's vct.regionN GPU ms per m^3 of the cells re-voxelised.
//   `lamp` (gi.mover_lamp_step): A LAMP STEPPING WHILE A MOVER MOVES EVERY FRAME IN ITS CASCADES lands within the relight's
//       own law (every cascade its range meets completes the round within 2 x lightWorkFrames + 1 frames): the region
//       rebuild's injection carries the owed relight slab (the one-writer latch). RED before: the relights skipped every
//       cascade the mover's region injected, for as long as it moved.
// Counted in frames; each row under the engine fixture's 10 s.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
bool setSkyLevel(Scene *s, unsigned char v) {
    SkyDesc sky;
    sky.mode = SkyMode::Cubemap;
    std::vector<unsigned char> face(8 * 8 * 4);
    for (size_t k = 0; k < face.size(); k += 4) { face[k] = v; face[k + 1] = v; face[k + 2] = v; face[k + 3] = 255; }
    for (int f = 0; f < 6; ++f) sky.faces[f] = s->createTexture(8, 8, face.data(), true, false);
    return s->setSky(sky);
}
NodeId addBox(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo, bool movable) {
    const NodeId n = s->createNode();
    if (movable) s->setNodeMovable(n, true);   // (before its geometry: no reclassification)
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 0.9f;
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(n, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][n] = enginetest::NodePose{};
    s->setNodeTransform(n, at, Quat(), size);
    return n;
}
GiParams chainGi() {
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.bounces = true;
    gi.updateBudget = 0;
    return gi;
}
struct Store { std::vector<GiVoxelVolume> c; };
bool readStore(Scene *s, size_t cascades, Store &out) {
    out.c.assign(cascades, GiVoxelVolume());
    for (size_t k = 0; k < cascades; ++k)
        if (!s->giVoxelVolume(int(k), out.c[k]) || !out.c[k].available) return false;
    return true;
}
/// One code of a stored radiance (1 % of it per channel, past one half-float step of it).
double codesOff(float got, float want) {
    const double o = std::max(double(std::fabs(want)), 5.9604645e-6);
    return std::max(0.0, std::fabs(double(got) - double(want)) - std::ldexp(o, -10)) / (0.01 * o);
}
}   // namespace

int main(int argc, char **argv)
{
    const std::string arm = argc > 1 ? argv[1] : "";
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-mover-geometry-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);   // the store's own rays carry the bounce: the store is what is read
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    int frames = 1;
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("movers", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("movers");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    setSkyLevel(scene, 200);
    // THE COURT: a 60 m floor, sky only (no sun, no lamp)
    // (a SOLID floor, thicker than every cascade's cell: its top voxels are one-sided, the surface's own)
    addBox(scene, Vec3(0.0f, -1.0f, 0.0f), Vec3(60.0f, 2.0f, 60.0f), Colour(0.7f, 0.7f, 0.7f), false);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 0.0f), Vec3(0.0f, 1.0f, -6.0f));
    // AT REST: the chain owes nothing (its feedback included) for 8 frames in a row - twice the feedback readings' fixed
    // lag (the dynamic buffer multiplier + 1 = 4 frames: a pass's reading lands within it, so 8 quiet frames are a rest
    // the GPU has confirmed, never a reading in flight)
    const auto settle = [&](int cap) {
        int quiet = 0, n = 0;
        for (; n < cap && quiet < 8; ++n) {
            run(1);
            const GiStatus g = scene->published().gi;
            quiet = (g.giAtRest && !g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
        }
        return quiet >= 8 ? n : -1;
    };
    // THE LIGHT OF ONE CASCADE ALONE (Scene::giVoxelLight: a row that judges one cascade's light reads no more)
    const auto readLight = [&](size_t cascades, size_t k, Store &out) {
        out.c.assign(cascades, GiVoxelVolume());
        return k < cascades && scene->giVoxelLight(int(k), out.c[k]) && out.c[k].available;
    };
    const auto oracle = [&](Store &out, size_t cascades, int lightOf) {
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        scene->setGlobalIllumination(chainGi());
        e->waitForAsyncShaders();
        const int r = settle(1200);
        return r >= 0 && (lightOf >= 0 ? readLight(cascades, size_t(lightOf), out) : readStore(scene, cascades, out));
    };

    if (arm.empty() || arm == "ghost") {
        std::printf("\n== ghost: a door opened in play ==\n");
        const Vec3 doorSize(1.0f, 2.0f, 0.1f);
        Vec3 doorAt(0.0f, 1.0f, -3.0f);
        const NodeId door = addBox(scene, doorAt, doorSize, Colour(0.9f, 0.2f, 0.1f), false);
        CHECK(door != 0 && scene->setGlobalIllumination(chainGi()), "the court and its still door are up");
        e->waitForAsyncShaders();
        const int rest0 = settle(1200);
        const size_t n = scene->published().gi.cascades.size();
        std::printf("   at rest in %d frames, %zu cascades\n", rest0, n);
        CHECK(rest0 >= 0 && n > 0, "the court comes to rest");
        // THE PLAY: promoted Movable mid-play, slid open 1.6 m along +x over 32 frames
        scene->setNodeMovable(door, true);   // (the play-time promotion's push: a flip, no GI work)
        for (int f = 1; f <= 32; ++f) {
            doorAt.x = 0.05f * float(f);
            scene->setNodeTransform(door, doorAt, Quat(), doorSize);
            run(1);
        }
        const int rest1 = settle(1200);
        Store moved, truth;
        const bool readMoved = readStore(scene, n, moved);
        const bool readTruth = oracle(truth, n, -1);
        CHECK(rest1 >= 0 && readMoved && readTruth, "the opened door comes to rest, and the oracle is built and read");
        size_t geomOff = 0, lightOver = 0, lit = 0, doorCells = 0;
        double worst = 0.0;
        for (size_t k = 0; k < n && readMoved && readTruth; ++k) {
            const GiVoxelVolume &a = moved.c[k], &b = truth.c[k];
            if (a.albedo.size() != b.albedo.size() || a.light.size() != b.light.size()) { ++geomOff; continue; }
            for (size_t i = 0; i < a.albedo.size(); ++i) {
                const bool g = a.albedo[i] != b.albedo[i] || a.coverageP[i] != b.coverageP[i] ||
                               a.coverageN[i] != b.coverageN[i];
                geomOff += g ? 1u : 0u;
            }
            for (size_t v = 0; v * 4u + 3u < a.light.size(); ++v) {
                if (!(b.albedo[v * 4u + 3u] > 0.0f)) continue;   // (a voxel the oracle holds no surface in)
                const double lum = double(b.light[v * 4u]) + double(b.light[v * 4u + 1u]) + double(b.light[v * 4u + 2u]);
                if (!(lum > 1e-5)) continue;
                ++lit;
                double c = 0.0;
                for (int ch = 0; ch < 3; ++ch) c = std::max(c, codesOff(a.light[v * 4u + ch], b.light[v * 4u + ch]));
                worst = std::max(worst, c);
                lightOver += c > 2.0 ? 1u : 0u;
                // (the door's red: the voxels whose albedo is the door's in either store)
                if (b.albedo[v * 4u] > 0.6f && b.albedo[v * 4u + 1u] < 0.4f) ++doorCells;
            }
        }
        std::printf("   at rest in %d frames: geometry texels off the oracle %zu; the oracle's lit voxels %zu (the door's %zu): "
                    "%zu over two codes, worst %.3f codes\n", rest1, geomOff, lit, doorCells, lightOver, worst);
        CHECK(geomOff == 0u, "A DOOR OPENED IN PLAY LEAVES NO CLOSED-DOOR VOXEL: every cascade's geometry is the oracle's, "
                             "bit for bit");
        CHECK(lit > 100u && lightOver == 0u, "...AND NO CLOSED-DOOR BOUNCE: every lit voxel within two codes of the oracle's");
        scene->removeNode(door);
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
    }

    if (arm.empty() || arm == "bus" || arm == "bus_geometry") {
        // TWO ROWS, one proof each (each under the engine fixture's 10 s): `bus_geometry` reads every cascade's geometry
        // (the moved store's and the oracle's), `bus` the light of cascade 1 alone (the floor beside the bus: the empty
        // court's, the moved store's, the oracle's).
        const bool geometryLaw = arm != "bus", skyLaw = arm != "bus_geometry";
        const size_t floorCascade = 1u;   // (the bus 10 m out: cascade 1 holds the floor beside it, cascade 0 ends at 5 m)
        std::printf("\n== bus: a bus 10 m away (%s) ==\n", arm.empty() ? "both laws" : arm.c_str());
        const Vec3 busSize(10.0f, 2.5f, 2.5f);
        Vec3 busAt(-3.0f, 1.25f, -10.0f);
        CHECK(scene->setGlobalIllumination(chainGi()), "the empty court is up");
        e->waitForAsyncShaders();
        const int rest0 = settle(1200);
        const size_t n = scene->published().gi.cascades.size();
        Store empty;
        const bool readEmpty = !skyLaw || readLight(n, floorCascade, empty);
        CHECK(rest0 >= 0 && readEmpty, "the empty court comes to rest and is read");
        const NodeId bus = addBox(scene, busAt, busSize, Colour(0.3f, 0.35f, 0.4f), true);
        CHECK(bus != 0, "the bus (movable) arrives");
        run(2);   // (it drives at once: the oracle is the final scene's, whatever the arrival owed)
        // THE DRIVE: 3 m along +x over 30 frames, its region rebuilds' GPU priced
        const GiStatus gDrive0 = scene->published().gi;
        if (geometryLaw) e->setFrameMonitor(MonitorLevel::Review);   // (the price: the geometry row's)
        for (int f = 1; f <= 30; ++f) {
            busAt.x = -3.0f + 0.1f * float(f);
            scene->setNodeTransform(bus, busAt, Quat(), busSize);
            run(1);
        }
        run(6);   // the GPU samples land a few frames late
        if (geometryLaw) {
            e->setFrameMonitor(MonitorLevel::Off);
            std::vector<MonitorRecord> records;
            while (e->popMonitorRecords(records) > 0) {}
            double ms[8] = {}, kcells[8] = {}, bms[8] = {}, bpasses[8] = {};
            unsigned count[8] = {}, bcount[8] = {};
            for (const MonitorRecord &r : records)
                for (const CacheWork &w : r.cacheWork) {
                    if (w.detail.rfind("vct.regionBounce", 0) == 0 && w.gpuMs >= 0.0f) {
                        const int k = std::atoi(w.detail.c_str() + 16);
                        if (k < 0 || k > 7) continue;
                        bms[k] += double(w.gpuMs);
                        bpasses[k] += double(w.units);
                        ++bcount[k];
                        continue;
                    }
                    if (w.detail.rfind("vct.region", 0) == 0 && w.gpuMs >= 0.0f) {
                        const int k = std::atoi(w.detail.c_str() + 10);
                        if (k < 0 || k > 7) continue;
                        ms[k] += double(w.gpuMs);
                        kcells[k] += double(w.units);
                        ++count[k];
                    }
                }
            const GiStatus g = scene->published().gi;
            std::printf("   THE REGION'S PRICE while it drives (printed):");
            for (size_t k = 0; k < g.cascades.size() && k < 8u; ++k) {
                const double cell = double(g.cascades[k].cell);
                const double m3 = kcells[k] * 1000.0 * cell * cell * cell;
                std::printf("  c%zu %u rebuilds %.3f ms each, %.6f ms/m^3", k, count[k], count[k] ? ms[k] / count[k] : 0.0,
                            m3 > 0.0 ? ms[k] / m3 : 0.0);
            }
            std::printf("\n   THE REGION'S OWN BOUNCE while it drives (A2, printed):");
            for (size_t k = 0; k < g.cascades.size() && k < 8u; ++k)
                std::printf("  c%zu %u rebuilds, %.1f passes each to its convergence (cap %.0f), %.3f ms each", k, bcount[k],
                            bcount[k] ? double(g.cascades[k].regionBouncePasses - gDrive0.cascades[k].regionBouncePasses) /
                                            bcount[k] : 0.0,
                            bcount[k] ? bpasses[k] / bcount[k] : 0.0, bcount[k] ? bms[k] / bcount[k] : 0.0);
            std::printf("\n");
        }
        const int rest1 = settle(1200);
        Store moved, truth;
        const int lightOf = geometryLaw ? -1 : int(floorCascade);
        const bool readMoved = lightOf >= 0 ? readLight(n, floorCascade, moved) : readStore(scene, n, moved);
        const bool readTruth = oracle(truth, n, lightOf);
        CHECK(rest1 >= 0 && readMoved && readTruth, "the bus comes to rest, and the oracle is built and read");
        if (geometryLaw) {
            size_t geomOff = 0;
            for (size_t k = 0; k < n && readMoved && readTruth; ++k) {
                const GiVoxelVolume &a = moved.c[k], &b = truth.c[k];
                if (a.albedo.size() != b.albedo.size()) { ++geomOff; continue; }
                for (size_t i = 0; i < a.albedo.size(); ++i)
                    geomOff += (a.albedo[i] != b.albedo[i] || a.coverageP[i] != b.coverageP[i] ||
                                a.coverageN[i] != b.coverageN[i]) ? 1u : 0u;
            }
            std::printf("   geometry texels off the oracle: %zu\n", geomOff);
            CHECK(geomOff == 0u, "THE STORE HOLDS THE BUS WHERE IT STOPPED, AND NOWHERE IT DROVE: every cascade's geometry is "
                                 "the oracle's, bit for bit");
        }
        // THE FLOOR BESIDE IT: the floor's top voxels within 0.5-3 m of the bus's long sides, under it never
        size_t pts = 0, over = 0;
        double worst = 0.0, darker = 0.0;
        for (size_t k = floorCascade; skyLaw && k == floorCascade && k < n && readMoved && readTruth && readEmpty; ++k) {
            const GiVoxelVolume &a = moved.c[k], &b = truth.c[k], &z = empty.c[k];
            if (a.light.empty() || a.light.size() != b.light.size() || a.light.size() != z.light.size()) continue;
            const float cell = a.cell[1];
            // THE FLOOR'S SURFACE LAYER: of the two layers either side of its top (y = 0, a cell boundary), the one the
            // store lights (its coverage and its light both there) - the upper where the voxeliser put the face in both
            const int y0 = int(std::floor(-a.origin[1] / cell));
            int y = -1;
            for (int yy : { y0, y0 - 1 }) {
                if (yy < 0 || yy >= a.height || y >= 0) continue;
                const size_t v = (size_t(a.depth / 2) * size_t(a.height) + size_t(yy)) * size_t(a.width) + size_t(a.width / 2);
                if (z.light[v * 4u] > 1e-5f) y = yy;
            }
            if (y < 0 || y >= a.height) continue;
            for (int zi = 0; zi < a.depth; ++zi)
                for (int xi = 0; xi < a.width; ++xi) {
                    const Vec3 p(a.origin[0] + (float(xi) + 0.5f) * a.cell[0], 0.0f, a.origin[2] + (float(zi) + 0.5f) * a.cell[2]);
                    const float dz = std::fabs(p.z - busAt.z) - 0.5f * busSize.z;
                    const bool beside = dz > 0.5f && dz < 3.0f && std::fabs(p.x - busAt.x) < 0.5f * busSize.x - 0.5f;
                    if (!beside) continue;
                    const size_t v = (size_t(zi) * size_t(a.height) + size_t(y)) * size_t(a.width) + size_t(xi);
                    if (!(b.light[v * 4u] > 1e-5f) || !(z.light[v * 4u] > 1e-5f)) continue;
                    ++pts;
                    double c = 0.0, d = 0.0;
                    for (int ch = 0; ch < 3; ++ch) {
                        c = std::max(c, codesOff(a.light[v * 4u + ch], b.light[v * 4u + ch]));
                        d += (double(z.light[v * 4u + ch]) - double(a.light[v * 4u + ch])) / (0.01 * double(z.light[v * 4u + ch]));
                    }
                    worst = std::max(worst, c);
                    over += c > 2.0 ? 1u : 0u;
                    darker += d / 3.0;
                }
        }
        const double meanDarker = pts ? darker / double(pts) : 0.0;
        if (skyLaw) std::printf("   at rest in %d frames: %zu floor voxels beside the bus: against the oracle %zu over two codes, worst %.3f; "
                    "darker than the empty court by %.2f codes on average\n", rest1, pts, over, worst, meanDarker);
        if (skyLaw) {
        CHECK(pts > 20u && over == 0u, "THE STORE TOOK THE BUS WHERE IT STOPPED: the floor beside it within two codes of the oracle");
        CHECK(meanDarker > 4.0, "A BUS 10 m AWAY OCCLUDES THE FLOOR'S SKY SHARE: the floor beside it darker than the empty "
                                "court's by more than four codes");
        }
        scene->removeNode(bus);
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
    }

    if (arm.empty() || arm == "lamp") {
        // A LAMP STEPPING WHILE A MOVER MOVES EVERY FRAME IN ITS CASCADES (the one-writer latch: a cascade takes one
        // injection a frame, so the region rebuild's injection carries the lamp's owed relight slab - else the relight,
        // which skips a cascade injected this frame, would wait for as long as the mover moved). THE LAW, the relight's
        // own (GiQualityFacts::lightWorkFrames): an owed cascade's floor alone pays its whole owed volume in 2 x
        // lightWorkFrames frames, so every cascade the lamp's range meets completes the step's relight round within that,
        // plus the frame the step is owed in (the list's Lamp region is owed after the frame's region service).
        std::printf("\n== lamp: a lamp stepping while a mover moves ==\n");
        CHECK(scene->setGlobalIllumination(chainGi()), "the court is up");
        e->waitForAsyncShaders();
        const NodeId lamp = scene->createNode();
        LightDesc ld;
        ld.type = LightType::Point;
        ld.colour = Colour(1.0f, 0.9f, 0.8f);
        ld.intensity = 4.0f;
        ld.range = 6.0f;
        scene->setNodeTransform(lamp, Vec3(0.0f, 2.0f, -3.0f), Quat(), Vec3(1, 1, 1));
        scene->setLight(lamp, ld);
        Vec3 at(-1.0f, 0.5f, -3.0f);
        const NodeId mover = addBox(scene, at, Vec3(0.6f, 1.0f, 0.6f), Colour(0.8f, 0.8f, 0.8f), true);
        CHECK(lamp != 0 && mover != 0 && settle(1200) >= 0, "the lamp and the mover (still) come to rest");
        int f = 0;
        const auto step = [&]() {   // the mover sways 0.6 m along x, a frame at a time, every frame
            at.x = -1.0f + 0.6f * std::sin(0.2f * float(++f));
            scene->setNodeTransform(mover, at, Quat(), Vec3(0.6f, 1.0f, 0.6f));
            run(1);
        };
        for (int k = 0; k < 8; ++k) step();   // (moving)
        const GiStatus g0 = scene->published().gi;
        const unsigned law = 2u * giQualityFacts(GiQuality::High, GiViewProfile::Desktop).lightWorkFrames + 1u;
        ld.intensity = 8.0f;
        scene->setLight(lamp, ld);
        // the cascades the lamp's range meets: each must complete a relight round
        std::vector<int> doneAt(g0.cascades.size(), -1);
        std::vector<bool> meets(g0.cascades.size(), false);
        for (size_t k = 0; k < g0.cascades.size(); ++k) {
            const GiStatus::CascadeStatus &cs = g0.cascades[k];
            bool in = true;
            const float lp[3] = { 0.0f, 2.0f, -3.0f };
            const float cc[3] = { cs.centre.x, cs.centre.y, cs.centre.z };
            for (int ax = 0; ax < 3; ++ax) in = in && std::fabs(lp[ax] - cc[ax]) < cs.halfSize + ld.range;
            meets[k] = in;
        }
        unsigned long long movesDuring = 0ull;
        for (unsigned q = 1; q <= law + 8u; ++q) {
            step();
            const GiStatus g = scene->published().gi;
            for (size_t k = 0; k < g.cascades.size() && k < doneAt.size(); ++k) {
                if (doneAt[k] < 0 && g.cascades[k].relightRounds > g0.cascades[k].relightRounds) doneAt[k] = int(q);
                if (q == law) movesDuring += g.cascades[k].moverRegionRebuilds - g0.cascades[k].moverRegionRebuilds;
            }
        }
        bool all = true;
        std::printf("   the relight's law: %u frames; the mover's region rebuilds meanwhile %llu;", law, movesDuring);
        for (size_t k = 0; k < doneAt.size(); ++k) {
            std::printf("  c%zu %s %d", k, meets[k] ? "round in" : "(out of range)", doneAt[k]);
            if (meets[k]) all = all && doneAt[k] >= 1 && unsigned(doneAt[k]) <= law;
        }
        std::printf("\n");
        CHECK(movesDuring > 0ull, "the mover moved by region every frame of it");
        CHECK(all, "A LAMP'S STEP LANDS WHILE A MOVER MOVES: every cascade its range meets completes the relight round "
                   "within its law's frames");
        scene->removeNode(mover);
        scene->removeNode(lamp);
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
    }

    e->destroyView(view);
    e->destroyScene(scene);
    run(1);
    std::printf("\nframes %d\n", frames);
    std::printf(failures ? "FAILED (%d failures)\n" : "ALL PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
