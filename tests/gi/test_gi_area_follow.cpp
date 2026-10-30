// gi.area_follow (PHOTON-7 stage 8B, B2) — THE AREA LIGHT'S VISIBILITY RESTS PER TEXEL: a mover's shadow follows it, and
// the texels its light rays never meet keep their rest bit for bit.
//
// A carded 3 m floor and a back wall under an ACCURATE area light 1 x 0.5 m facing down 1.2 m over the floor's centre;
// a still 0.05 m plate 0.6 m up over x < 0 (its penumbra a 1 m band about x = 0) and a MOVABLE 0.2 m block 0.6 m up at
// x = 0.6 (its shadow from the light's centre at twice its place: x = 1.2). The view looks straight down at the floor
// (the pixel's visibility read back: the arm area.visReadback). At rest and held, the block slides 0.6 m along z over
// 20 frames and stops.
//   (1) THE REST IS KEPT WHERE NOTHING CHANGED: every lit or penumbra floor pixel whose light rays pass clear of each of
//       the block's per-frame swept boxes (the law's own conservative bundle: the segment to the light's centre widened
//       to its half diagonal, against the box's sphere) reads the SAME BYTE at every frame of the move and its window as
//       at the hold before it (before B2 the scene's view-wide rest key broke every texel's rest on the block's first
//       frame, and a penumbra moved with the strata's partial cycles).
//   (2) THE SHADOW FOLLOWS: n^2 + 8 frames after the stop, the floor at twice the block's new place (its shadow on
//       the light's centre line, in no other shadow) reads less visibility than at twice its old place, which reads
//       the unshadowed 1 to the store's half step.
//   (3) THE VIEW HOLDS AGAIN within n^2 + 8 frames of the stop.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
NodeId cube(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo, bool movable) {
    const NodeId node = s->createNode();
    if (movable) s->setNodeMovable(node, true);   // before its geometry: no reclassification rebuild
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 0.9f;
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
    const auto cfg = enginetest::testEngineConfig("test-gi-area-follow-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };

    View *v = e->createOffscreenView("area_follow", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *s = e->createScene("area_follow");
    if (!v || !s) { std::printf("FAIL: the view/scene\n"); return 1; }
    v->setScene(s);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        v->setPostFx(fx);
    }
    cube(s, Vec3(0.0f, -0.05f, 0.0f), Vec3(3.0f, 0.1f, 3.0f), Colour(0.6f, 0.6f, 0.6f), false);
    cube(s, Vec3(0.0f, 1.0f, -1.55f), Vec3(3.0f, 2.0f, 0.1f), Colour(0.5f, 0.5f, 0.6f), false);
    cube(s, Vec3(-0.85f, 0.6f, 0.0f), Vec3(1.7f, 0.05f, 2.4f), Colour(0.5f, 0.5f, 0.5f), false);   // the still plate
    const float kBlockX = 0.6f, kZ0 = -0.3f, kZ1 = 0.3f;
    const NodeId block = cube(s, Vec3(kBlockX, 0.6f, kZ0), Vec3(0.2f, 0.05f, 0.2f), Colour(0.5f, 0.5f, 0.5f), true);
    {
        const NodeId lamp = s->createNode();
        LightDesc l;
        l.type = LightType::Area;
        l.rectWidth = 1.0f;
        l.rectHeight = 0.5f;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 3.0f;
        l.range = 20.0f;
        s->setNodeTransform(lamp, Vec3(0.0f, 1.2f, 0.0f), Quat(), Vec3(1, 1, 1));
        s->setLight(lamp, l);
    }
    GiParams gi;   // (gi.card_lamp_shadow_area's: High, the cards and the bounce on, no irradiance field)
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!block || !s->setGlobalIllumination(gi)) { std::printf("FAIL: the scene builds\n"); return 1; }
    e->waitForAsyncShaders();
    e->setArm("area.visReadback", 1.0);
    // straight down from 3.2 m over the floor's centre: the floor fills the view, x right, z down the image
    const Vec3 eyeP(0.0f, 3.2f, 0.001f), atP(0.0f, 0.0f, 0.0f);
    enginetest::testCameraLookAt(v, eyeP, atP);
    const auto projectTo = [&](double wx, double wz, unsigned w, unsigned h, int &ix, int &iy) {
        // testCameraLookAt's basis looking down -y with up +y projected: right = +x, image down = +z (45-degree field)
        const double tanY = std::tan(22.5 * 3.14159265358979 / 180.0), tanX = tanY * double(w) / double(h);
        const double zf = double(eyeP.y);
        ix = int(std::floor((wx / (zf * tanX) + 1.0) * 0.5 * double(w)));
        iy = int(std::floor((wz / (zf * tanY) + 1.0) * 0.5 * double(h)));
        return ix >= 0 && iy >= 0 && ix < int(w) && iy < int(h);
    };
    // THE REST: settle, then the visibility's n^2 rest frames and its hold
    run(240);
    unsigned n = std::max(s->published().areaVis.strata, 1u);
    int heldAt = -1;
    for (int f = 0; f < int(4 * n * n) + 60 && heldAt < 0; ++f) {
        run(1);
        if (s->published().areaVis.held) heldAt = f;
    }
    run(6);   // the read-back a few frames late: the held picture
    const AreaVisStatus a0 = s->published().areaVis;
    n = std::max(a0.strata, 1u);
    const bool read0 = a0.vis.size() == size_t(a0.visW) * a0.visH * 8u && a0.visW > 0u;
    std::printf("   the visibility %s: n = %u, the trace %ux%u, held after %d frames (%d frames run)\n",
                a0.running ? "running" : a0.reason.c_str(), n, a0.traceW, a0.traceH, heldAt, frames);
    if (!read0 || heldAt < 0) {
        CHECK(false, "the area visibility runs, holds and reads back");
        return 1;
    }
    // THE FAR PIXELS: every floor point of a 0.04 m grid whose light bundle (the law's own conservative test: the segment
    // to the light's centre widened to its half diagonal) passes clear of every one of the block's per-frame swept boxes
    // along its path (0.2 x 0.05 x 0.23 m, a 0.05 m margin on its sphere), and that the plate leaves lit or in its penumbra
    struct Px { int x, y; unsigned char b; };
    std::vector<Px> band;
    size_t partial = 0;
    const double R = 0.5 * std::sqrt(0.2 * 0.2 + 0.05 * 0.05 + 0.23 * 0.23) + 0.05;
    const double lightR = 0.5 * std::sqrt(1.0 + 0.25), Cy = 1.2;
    for (int i = -32; i <= 32; ++i)
        for (int k = -32; k <= 32; ++k) {
            const double px = 0.04 * i, pz = 0.04 * k;
            const double dx = -px, dy = Cy, dz = -pz, len2 = dx * dx + dy * dy + dz * dz;
            bool near = false;
            for (int f = 0; f <= 20 && !near; ++f) {
                const double qx = kBlockX, qy = 0.6, qz = kZ0 + (kZ1 - kZ0) * f / 20.0;
                const double u = std::min(1.0, std::max(0.0, ((qx - px) * dx + qy * dy + (qz - pz) * dz) / len2));
                const double ex = px + dx * u - qx, ey = dy * u - qy, ez = pz + dz * u - qz;
                near = std::sqrt(ex * ex + ey * ey + ez * ez) <= R + lightR * u;
            }
            if (near) continue;
            int ix, iy;
            if (!projectTo(px, pz, a0.visW, a0.visH, ix, iy)) continue;
            const unsigned char b = a0.vis[(size_t(iy) * a0.visW + size_t(ix)) * 8u];
            if (b == 0u) continue;
            band.push_back({ ix, iy, b });
            partial += b < 255u ? 1u : 0u;
        }
    // THE MOVE: 0.6 m along z over 20 frames, then the window
    size_t changed = 0, readFrames = 0;
    int worst = 0;
    int heldAgain = -1;
    for (int f = 0; f < 20 + int(n * n) + 8; ++f) {
        if (f < 20) enginetest::setNodePosition(s, block, Vec3(kBlockX, 0.6f, kZ0 + (kZ1 - kZ0) * float(f + 1) / 20.0f));
        run(1);
        const AreaVisStatus a = s->published().areaVis;
        if (f >= 20 && heldAgain < 0 && a.held) heldAgain = f - 20;
        if (f == 10) {
            // (printed: the list the move's frame closed - its kinds and its largest box)
            GiChangedRegions list;
            s->giChangedRegions(list);
            unsigned kinds[8] = {};
            double largest = 0.0;
            for (const GiChangedRegion &r : list.regions) {
                if (unsigned(r.kind) < 8u) ++kinds[unsigned(r.kind)];
                largest = std::max(largest, double(std::max(r.hi.x - r.lo.x, std::max(r.hi.y - r.lo.y, r.hi.z - r.lo.z))));
            }
            std::printf("   the move's list (frame 10): overflow %u restart %d; slab %u card %u edit %u mover %u moverShadow %u "
                        "lampShadow %u lamp %u; the largest box's side %.2f m\n", list.overflow, list.restart ? 1 : 0,
                        kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], kinds[7], largest);
        }
        if (a.vis.size() != a0.vis.size()) continue;
        ++readFrames;
        for (const Px &p : band) {
            const int d = std::abs(int(a.vis[(size_t(p.y) * a.visW + size_t(p.x)) * 8u]) - int(p.b));
            changed += d ? 1u : 0u;
            worst = std::max(worst, d);
        }
    }
    std::printf("   (1) %zu far pixels (%zu in the plate's penumbra) over %zu read frames of the move and its window: %zu "
                "pixel-frames changed, the worst %d of 255\n", band.size(), partial, readFrames, changed, worst);
    CHECK(band.size() > 200u && partial > 40u && readFrames > 20u && changed == 0u,
          "(1) THE REST IS KEPT WHERE NOTHING CHANGED: the far penumbra's visibility bit for bit through the move");
    run(6);
    const AreaVisStatus a1 = s->published().areaVis;
    double newV = -1.0, oldV = -1.0;
    {
        int ix, iy;
        if (a1.vis.size() == a0.vis.size() && projectTo(2.0 * kBlockX, 2.0 * kZ1, a1.visW, a1.visH, ix, iy))
            newV = a1.vis[(size_t(iy) * a1.visW + size_t(ix)) * 8u] / 255.0;
        if (a1.vis.size() == a0.vis.size() && projectTo(2.0 * kBlockX, 2.0 * kZ0, a1.visW, a1.visH, ix, iy))
            oldV = a1.vis[(size_t(iy) * a1.visW + size_t(ix)) * 8u] / 255.0;
    }
    std::printf("   (2) %u + 8 frames after the stop: the visibility at its new shadow %.4f, at its old %.4f\n",
                n * n, newV, oldV);
    CHECK(newV >= 0.0 && oldV >= 0.0 && newV < oldV - 0.05 && oldV > 1.0 - 0.5 / 255.0 - 1e-9,
          "(2) THE SHADOW FOLLOWS: darker under the block's new place, the old place lit whole");
    std::printf("   (3) the view held again %d frames after the stop (n^2 = %u)\n", heldAgain, n * n);
    CHECK(heldAgain >= 0 && heldAgain <= int(n * n) + 8, "(3) THE VIEW HOLDS AGAIN within n^2 + 8 frames of the stop");
    std::printf("gi.area_follow: %s (%d frames)\n", failures ? "FAIL" : "PASS", frames);
    return failures ? 1 : 0;
}
