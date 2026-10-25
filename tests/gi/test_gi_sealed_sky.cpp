// gi.sealed_sky (PHOTON-7 stage 10; L9: NO LIGHT OF ANY TYPE PASSES A WALL) — THE SKY CANNOT LIGHT A SEALED ROOM, on
// every store and reader that carries it. A closed box (4 x 3 x 4 m inside, 0.4 m walls, albedo 0.6) under a uniform
// white sky of radiance 1 (the SH's constant band, no cube) and nothing else; the rays on, a still picture, at High and
// at Epic.
//
// THE DEFECT IT GUARDS (10-0's P1, spikes/photon-7/stage10/README.txt): the caches' cones let the sky through the walls
// - the card relight's sixteen-cone sky share and four-cone escape (jahSkyShareFine, jahDiffuseCones) and the bounce's
// - so a sealed room read 16.3 % of an open roof at the gather's peak. Stage 10 gathers the caches by occupancy RAYS
// (jah_voxel_ray.glsl): the sky enters only at an earned miss, and a ray in a closed room never misses.
//
// THE LAWS, per tier, the bounce ON (the store's feedback converges the sky with every other light):
//   (1) THE CONTROL - the sky is on: the roof's top voxels (cascade 0's store, the side facing the sky) read over half
//       the albedo times the sky (the sky's irradiance times the albedo over pi, radiance: 0.6 for a white sky of 1) -
//       the reference R; and the card path carries the sky: the box's OUTER wall cards (read from the ground the box
//       stands on) read over a quarter of R;
//   (2) THE CARDS - the card the readers hit at every point of an 8 x 8 grid on each of the room's six faces (0.15 m in
//       from its edges: Scene::readCardAt, the hit's own pick) reads under 1 % of R;
//   (3) THE VOXELS - every occupied voxel of cascades 0 and 1 whose centre lies inside the room (its inner faces' cells:
//       the walls are two or more of those cells thick) reads under 1 % of R. (The coarser cascades' voxels hold both
//       faces of a wall in one cell, their sides by orientation, not by inside and outside - the outermost's mix the
//       sky-lit roof with the ceiling: 10-1's evidence, spikes/photon-7/stage10/10-1.)
// RED ON THE BASE (the caches' cones: the card relight's sixteen-cone sky share and four-cone escape, the bounce's -
// and the bounce's mean of a voxel's two sides, which let half of each sky-lit outer face light the inside).
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
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
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-sealed-sky-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("sealed_sky", 256, 256, Colour(0, 0, 0));
    Scene *scene = e->createScene("sealed_sky");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    view->setOffscreenContract(OffscreenContract::StillPicture);   // a still picture gathers
    const float kAlbedo = 0.6f;
    PbrParams matte;
    matte.albedo = Colour(kAlbedo, kAlbedo, kAlbedo);
    matte.roughness = 0.9f;
    // the room
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), matte);
    box(scene, Vec3(0.0f, 3.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), matte);
    box(scene, Vec3(-2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), matte);
    const NodeId wxp = box(scene, Vec3(2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), matte);
    box(scene, Vec3(0.0f, 1.5f, -2.2f), Vec3(4.8f, 3.0f, 0.4f), matte);
    const NodeId wzp = box(scene, Vec3(0.0f, 1.5f, 2.2f), Vec3(4.8f, 3.0f, 0.4f), matte);
    box(scene, Vec3(0.0f, -0.6f, 0.0f), Vec3(20.0f, 0.4f, 20.0f), matte);   // the ground the box stands on
    float sh[27] = { 0 };
    sh[0] = sh[1] = sh[2] = 1.0f;   // a uniform white sky of radiance 1
    scene->setAmbientSh(sh);

    // THE REST (gi.card_oracle's, its window cut to the law's): the chain owes nothing, and the cache admitted, captured
    // and relit nothing, for 30 frames in a row - past the readers' window (S = 10) and the demand read-back's worst
    // landing (period + lag + 1 = 9: tests/support/owed_rows.py), so the resident set the reads ask for has arrived
    const auto settle = [&]() {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1200 && quiet < 30; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool still = !g.chainOwed && !g.feedbackOwed && g.cards.built && g.cards.relights == relights &&
                               g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    const auto middle = [&](double &mean) {
        ImageF pic;
        mean = -1.0;
        if (!view->readPixelsHdr(pic) || pic.width < 64u || pic.height < 64u) return false;
        double sum = 0.0;
        size_t n = 0;
        for (unsigned y = pic.height / 4u; y < pic.height * 3u / 4u; ++y)
            for (unsigned x = pic.width / 4u; x < pic.width * 3u / 4u; ++x) { sum += pic.at(x, y).g; ++n; }
        mean = n ? sum / double(n) : -1.0;
        return n > 0;
    };
    // a face's resident cards: their texels' brightest channel's peak and mean (texels holding nothing skipped)
    struct Read { unsigned cards = 0; size_t texels = 0; double peak = 0.0, mean = 0.0; };
    const auto readFace = [&](NodeId node, unsigned bucket) {
        Read r;
        std::vector<CardRadiance> all;
        unsigned long long cacheFrame = 0ull;
        if (!scene->readCardRadiance(all, cacheFrame)) return r;
        double sum = 0.0;
        for (const CardRadiance &c : all) {
            if (c.node != node || c.bucket != bucket || c.lastRelit == 0ull) continue;
            ++r.cards;
            for (size_t i = 0; i + 2 < c.rgb.size(); i += 3) {
                const double v = std::max(c.rgb[i], std::max(c.rgb[i + 1], c.rgb[i + 2]));
                if (!(v > 0.0)) continue;
                r.peak = std::max(r.peak, v);
                sum += v;
                ++r.texels;
            }
        }
        r.mean = r.texels ? sum / double(r.texels) : 0.0;
        return r;
    };

    // THE ROOM's voxels of one cascade: the occupied cells whose centre lies inside the room (x, z in [-2, 2], y in
    // [0, 3]); the brightest channel of each one's light (un-premultiplied); and the roof's top cells (the reference)
    struct VoxRead { size_t cells = 0; double peak = 0.0; size_t roofCells = 0; double roofMean = 0.0; bool ok = false; };
    const auto readVoxels = [&](int c) {
        VoxRead r;
        GiVoxelVolume v;
        if (!scene->giVoxelLight(c, v) || !v.available) return r;
        r.ok = true;
        double roofSum = 0.0;
        for (int z = 0; z < v.depth; ++z)
            for (int y = 0; y < v.height; ++y)
                for (int x = 0; x < v.width; ++x) {
                    const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                    const float cOcc = v.light[i + 3];
                    if (!(cOcc > 0.0f)) continue;
                    const double cx = v.origin[0] + (x + 0.5) * v.cell[0], cy = v.origin[1] + (y + 0.5) * v.cell[1],
                                 cz = v.origin[2] + (z + 0.5) * v.cell[2];
                    const double L = std::max(v.light[i], std::max(v.light[i + 1], v.light[i + 2])) / cOcc;
                    if (std::fabs(cx) < 2.0 && std::fabs(cz) < 2.0 && cy > 0.0 && cy < 3.0) {
                        ++r.cells;
                        r.peak = std::max(r.peak, L);
                    }
                    // the roof's top: the cell holding y = 3.4 over the roof's middle
                    if (std::fabs(cx) < 1.5 && std::fabs(cz) < 1.5 && cy - 0.5 * v.cell[1] <= 3.4 &&
                        cy + 0.5 * v.cell[1] > 3.4) {
                        ++r.roofCells;
                        roofSum += std::max(v.lightFront[i], std::max(v.lightFront[i + 1], v.lightFront[i + 2])) / cOcc;
                    }
                }
        r.roofMean = r.roofCells ? roofSum / double(r.roofCells) : 0.0;
        return r;
    };

    const GiQuality tiers[2] = { GiQuality::High, GiQuality::Epic };
    const char *tierName[2] = { "High", "Epic" };
    for (int t = 0; t < 2; ++t) {
        std::printf("== %s\n", tierName[t]);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = tiers[t];
        gi.ddgi = GiToggle::Off;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        e->waitForAsyncShaders();
        // (1) the control: above the box, the ground's rays read its outer walls
        enginetest::testCameraLookAt(view, Vec3(6.0f, 7.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f));
        const int restOut = settle();
        const VoxRead out0 = readVoxels(0);
        const double R = out0.roofMean;
        const Read outer = readFace(wxp, 0u);
        const Read outerZ = readFace(wzp, 4u);
        std::printf("   (1) above: at rest after %d frames; the roof's top voxels (%zu) read %.4f - the sky's irradiance "
                    "times the albedo over pi is %.2f; the outer +x wall's cards %u (%zu texels) mean %.4f, the +z's %u "
                    "mean %.4f\n", restOut, out0.roofCells, R, double(kAlbedo), outer.cards, outer.texels, outer.mean,
                    outerZ.cards, outerZ.mean);
        const double outerMean = std::max(outer.mean, outerZ.mean);
        check(restOut >= 0 && out0.ok && R > 0.5 * kAlbedo && outerMean > 0.25 * R,
              "(1) THE CONTROL - THE SKY IS ON: the roof's voxels read over half the albedo times the sky, the outer "
              "walls' cards over a quarter of that");
        // inside: its gather reads (and so keeps resident) the room's faces
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.5f, 1.4f), Vec3(0.0f, 1.0f, -2.0f));
        const int restIn = settle();
        // THE ROOM'S VISIBLE FACES, sampled where a reader can hit them: an 8 x 8 grid on each inner face, 0.15 m in from
        // its edges (a card also captures the parts of a face a neighbouring box covers - the floor under the walls -
        // whose texels gather from inside a solid and no ray can read: they are not the room's)
        struct Inner { Vec3 o, u, v, n; const char *name; };
        const Inner faces[6] = {
            { Vec3(-1.85f, 0.0f, -1.85f), Vec3(3.7f, 0, 0), Vec3(0, 0, 3.7f), Vec3(0, 1, 0), "the floor" },
            { Vec3(-1.85f, 3.0f, -1.85f), Vec3(3.7f, 0, 0), Vec3(0, 0, 3.7f), Vec3(0, -1, 0), "the ceiling" },
            { Vec3(-2.0f, 0.15f, -1.85f), Vec3(0, 2.7f, 0), Vec3(0, 0, 3.7f), Vec3(1, 0, 0), "the -x wall" },
            { Vec3(2.0f, 0.15f, -1.85f), Vec3(0, 2.7f, 0), Vec3(0, 0, 3.7f), Vec3(-1, 0, 0), "the +x wall" },
            { Vec3(-1.85f, 0.15f, -2.0f), Vec3(3.7f, 0, 0), Vec3(0, 2.7f, 0), Vec3(0, 0, 1), "the -z wall" },
            { Vec3(-1.85f, 0.15f, 2.0f), Vec3(3.7f, 0, 0), Vec3(0, 2.7f, 0), Vec3(0, 0, -1), "the +z wall" } };
        double worst = 0.0;
        unsigned facesRead = 0u;
        for (const Inner &f : faces) {
            unsigned read = 0u;
            double peak = 0.0, sum = 0.0;
            for (int a = 0; a < 8; ++a)
                for (int b = 0; b < 8; ++b) {
                    const float ua = (a + 0.5f) / 8.0f, vb = (b + 0.5f) / 8.0f;
                    const Vec3 at(f.o.x + f.u.x * ua + f.v.x * vb, f.o.y + f.u.y * ua + f.v.y * vb,
                                  f.o.z + f.u.z * ua + f.v.z * vb);
                    CardSample cs;
                    if (!scene->readCardAt(at, f.n, cs) || !cs.ok) continue;
                    ++read;
                    const double v = std::max(cs.radiance[0], std::max(cs.radiance[1], cs.radiance[2]));
                    peak = std::max(peak, v);
                    sum += v;
                }
            std::printf("   (2) %-11s %u of 64 points carded: peak %.6f (%.4f %% of R), mean %.6f\n", f.name, read, peak,
                        R > 0.0 ? 100.0 * peak / R : -1.0, read ? sum / read : 0.0);
            if (read >= 48u) ++facesRead;
            worst = std::max(worst, peak);
        }
        std::printf("   (2) inside: at rest after %d frames, %u of 6 faces carded; the brightest point %.4f %% of R\n",
                    restIn, facesRead, R > 0.0 ? 100.0 * worst / R : -1.0);
        check(restIn >= 0 && facesRead == 6u && R > 0.0 && worst < 0.01 * R,
              "(2) THE CARDS: no card point of the sealed room's faces reads 1 % of the roof's");
        double vWorst = 0.0;
        bool vOk = true;
        for (int c = 0; c < 2; ++c) {
            const VoxRead vr = readVoxels(c);
            std::printf("   (3) cascade %d: %zu voxels inside the room, the brightest %.6f (%.4f %% of R)\n", c, vr.cells,
                        vr.peak, R > 0.0 ? 100.0 * vr.peak / R : -1.0);
            vOk = vOk && vr.ok && vr.cells > 0;
            vWorst = std::max(vWorst, vr.peak);
        }
        check(vOk && R > 0.0 && vWorst < 0.01 * R,
              "(3) THE VOXELS: no voxel of cascades 0 and 1 inside the sealed room reads 1 % of the roof's");
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
