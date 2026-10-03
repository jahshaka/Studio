// A STILL CHARACTER IN FRONT OF A MOVING ATOM MOVER KEEPS ITS OWN (ZERO) MOTION
// (ID-DEPTH-1) — `gi.id_depth`.
//
// THE DEFECT: the screen march's resolve (JahSsrResolve_ps.glsl) fetches a reflected
// colour from the PREVIOUS frame's picture, moved by the object motion at the hit
// (jahSsrVelocity). rq_motion.comp writes that motion for every pixel whose ID — the
// id pass's image — names a slot that moved. But the id pass draws Atom-routed items
// only, before any stock-drawn surface: behind a character (a skinned item stays on
// stock HlmsPbs) the id image still names the Atom surface the character covers. A
// still character standing in front of a moving Atom mover therefore took the
// MOVER's motion, and the floor's reflection of the character was fetched from where
// the mover had been.
//
// THE FIX: the id pass's own depth (the chain copies it right after the id pass, on
// every chain whose velocity job runs) against the scene's final depth: nearer means
// the pixel is not the id's surface and keeps the camera path; rq_motion_skin.comp
// identifies such a pixel by the same test instead of trusting the id.
//
// THE FIXTURE: a mirror floor; a still, SKINNED comb of eight slabs (stock-drawn) in
// front; an Atom sphere (a mover) sliding behind the comb, 0.06 m a frame. At three
// checkpoints the MOVING frame is read, the sphere held there for kSettle frames and
// the SETTLED frame read; the error is the mean absolute difference over the floor's
// reflection of the comb, outside every pixel the sphere itself (or its reflection)
// touches. The STILL case — the sphere parked at the same pose for the same frames
// from a fresh start — is the control. Gate: moving within 1.5x the still case + 0.5
// codes (measured in the lane: base vs fixed in spikes/atom-engine-1).
//
// `--rays`: THE RAY REFLECTION'S OWN MOTION (PHOTON-II-1 item 11, `gi.id_depth_rays`). The
// ray tier's reflection reads the same id image for "this surface, last frame"
// (rq_reflect.comp, REFLECT-MOVERS-1): a pixel the id names as a MOVED slot finds its
// history through that slot's two poses. Behind a stock-drawn character the id names the
// mover, so the character's OWN reflection was carried back through the mover's motion
// and restarted where the mover passed behind it. Same fixture, the march OFF (the rays
// ARE the reflection) and the comb a glossy metal, so its own reflection is traced; the
// region is the COMB'S OWN PIXELS (outside every pixel the sphere touches), the same
// moving-vs-settled error against the same still control and the same bar.
//
// `--cost`: THE COPY'S PRICE. The id pass's depth copy ("Jahshaka atom id depth") runs
// every frame on a chain whose velocity job runs; this mode renders the same fixture
// at 1920x1080 with the sphere sliding, the render-loop monitor at Review, and reads
// the copy's own GPU timestamps. Run it under scripts/gpu-exclusive.sh with the
// clocks locked. Bar 0.05 ms (0.45 % of VR's 11.1 ms frame); past it the mode exits
// non-zero.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static unsigned kWidth = 640, kHeight = 360;
static bool gRays = false;   // --rays: the ray reflection's own motion (see the header)
static const int kWarm = 90, kSettle = 60;
static const float kStep = 0.06f, kSphereR = 0.45f;
static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static MeshData sphereMesh(int rings = 24, int segments = 48)
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    for (int r = 0; r <= rings; ++r) {
        const float th = float(r) / float(rings) * kPi;
        for (int sg = 0; sg < segments; ++sg) {
            const float ph = float(sg) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int sg = 0; sg < segments; ++sg) {
            const unsigned a = unsigned(r * segments + sg), b = unsigned(r * segments + (sg + 1) % segments);
            const unsigned c = a + unsigned(segments), e = b + unsigned(segments);
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
        }
    return d;
}

/// Eight vertical slabs, 0.12 m wide with 0.12 m gaps, 1.6 m tall, skinned to bone 0.
static MeshData combMesh()
{
    MeshData d;
    const MeshData cube = enginetest::unitCubeMesh();
    for (int k = 0; k < 8; ++k) {
        const unsigned base = unsigned(d.positions.size() / 3u);
        const float cx = -0.84f + 0.24f * float(k);
        for (size_t i = 0; i < cube.positions.size(); i += 3) {
            d.positions.insert(d.positions.end(), { cube.positions[i] * 0.12f + cx, cube.positions[i + 1] * 1.6f + 0.8f,
                                                    cube.positions[i + 2] * 0.2f });
            d.normals.insert(d.normals.end(), { cube.normals[i], cube.normals[i + 1], cube.normals[i + 2] });
            d.blendIndices.insert(d.blendIndices.end(), { 0, 0, 0, 0 });
            d.blendWeights.insert(d.blendWeights.end(), { 1.0f, 0.0f, 0.0f, 0.0f });
        }
        for (unsigned idx : cube.indices) d.indices.push_back(base + idx);
    }
    return d;
}

static Vec3 sphereAt(int f) { return Vec3(-2.0f + kStep * float(f), 0.6f, -1.0f); }

static double meanDiff(const Image &a, const Image &b, const std::vector<unsigned char> &mask, int &n)
{
    double sum = 0.0;
    n = 0;
    for (size_t p = 0; p < mask.size(); ++p) {
        if (!mask[p]) continue;
        for (int c = 0; c < 3; ++c) sum += std::abs(int(a.rgba[p * 4u + c]) - int(b.rgba[p * 4u + c]));
        ++n;
    }
    return n ? sum / (3.0 * n) : 0.0;
}

/// --cost: the depth copy's GPU ms per frame, from the monitor's per-pass timestamps.
static int costMain(Engine *e, Scene *s, NodeId sphere)
{
    static const double kBarMs = 0.05;
    e->setFrameMonitor(MonitorLevel::Review);
    std::vector<FrameRecord> recs;
    double copy = 0.0, frame = 0.0;
    int nCopy = 0, nFrame = 0, f = 0;
    for (int round = 0; round < 12; ++round) {
        for (int i = 0; i < 60; ++i) {
            enginetest::setNodePosition(s, sphere, sphereAt(f++ % 66));
            e->renderOneFrame();
        }
        recs.clear();
        e->takeFrameRecords(recs);
        if (round == 0) continue;   // warm-up: the first second is pipeline creation
        for (const FrameRecord &r : recs) {
            double c = -1.0;
            for (const FramePass &p : r.passes)
                if (p.pass == "Jahshaka atom id depth" && p.gpuMs >= 0.0f) c = (c < 0.0 ? 0.0 : c) + p.gpuMs;
            if (c >= 0.0) { copy += c; ++nCopy; }
            if (r.gpuMs > 0.0f) { frame += r.gpuMs; ++nFrame; }
        }
    }
    e->setFrameMonitor(MonitorLevel::Off);
    recs.clear();
    e->takeFrameRecords(recs);
    const double c = nCopy ? copy / nCopy : -1.0, fr = nFrame ? frame / nFrame : -1.0;
    std::printf("target: 1920x1080, a moving Atom sphere behind a still skinned comb on a mirror floor: the id "
                "depth copy %.4f ms of a %.4f ms frame (bar %.2f) [%d / %d frames]\n", c, fr, kBarMs, nCopy, nFrame);
    if (nCopy < 100) { std::printf("FAIL: the copy's pass was not timed (%d frames)\n", nCopy); return 1; }
    return c <= kBarMs ? 0 : 1;
}

int main(int argc, char **argv)
{
    const bool cost = argc > 1 && std::string(argv[1]) == "--cost";
    gRays = argc > 1 && std::string(argv[1]) == "--rays";
    if (cost) { kWidth = 1920; kHeight = 1080; }
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-id-depth-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("iddepth", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("iddepth");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine — gi.id_depth is about the tier; skipping\n");
        return 0;
    }
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());

    // THE MIRROR FLOOR.
    const NodeId floor = s->createNode();
    PbrParams fp; fp.albedo = Colour(0.9f, 0.9f, 0.9f); fp.metalness = 1.0f; fp.roughness = 0.02f;
    if (!(floor && s->attachMesh(floor, cube, s->createPbrMaterial(fp)))) { std::printf("FAIL: floor\n"); return 1; }
    enginetest::setNodeScale(s, floor, Vec3(30.0f, 0.2f, 30.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));

    // THE MOVER (an Atom item), told before its geometry.
    const NodeId sphere = s->createNode();
    s->setNodeMovable(sphere, true);
    PbrParams sp; sp.albedo = Colour(1.0f, 0.45f, 0.1f); sp.roughness = 0.8f;
    if (!(sphere && s->attachMesh(sphere, s->createMesh(sphereMesh()), s->createPbrMaterial(sp)))) {
        std::printf("FAIL: sphere\n"); return 1;
    }
    enginetest::setNodeScale(s, sphere, Vec3(2.0f * kSphereR, 2.0f * kSphereR, 2.0f * kSphereR));

    // THE STILL CHARACTER: a skinned comb, never posed again (stock-drawn).
    const NodeId comb = s->createNode();
    {
        SkeletonDesc rig;
        rig.id = "gi.id_depth comb rig v1";
        BoneDesc root; root.name = "root";
        rig.bones.push_back(root);
        PbrParams cp; cp.albedo = Colour(0.1f, 0.35f, 0.95f); cp.roughness = 0.7f;
        if (gRays) { cp.metalness = 1.0f; cp.roughness = 0.25f; cp.albedo = Colour(0.6f, 0.75f, 0.95f); }
        if (!(comb && s->attachSkinnedMesh(comb, s->createMesh(combMesh()), s->createPbrMaterial(cp), rig))) {
            std::printf("FAIL: the comb: %s\n", e->lastError().c_str()); return 1;
        }
        BonePose p[1];
        s->setBonePoses(comb, p, 1);
        enginetest::setNodePosition(s, comb, Vec3(0.0f, 0.0f, 1.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 2.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 20.0f, 128, 0.0f };
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: gi\n"); return 1; }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;   // the screen march on (the desktop default) over the ray tier
    if (gRays) fx.ssrScreenMarch = false;   // --rays: the rays ARE the reflection
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.0f, 6.0f), Vec3(0.0f, 0.4f, 0.0f));
    if (cost) return costMain(e, s, sphere);

    // THE REGION: the floor's reflection of the comb — the pixels the comb changes
    // (comb shown vs hidden, the sphere away) below the comb's own foot on screen.
    std::vector<unsigned char> region(size_t(kWidth) * kHeight, 0);
    {
        enginetest::setNodePosition(s, sphere, Vec3(0.0f, -50.0f, 0.0f));
        render(e, kWarm);
        Image with, without;
        view->readPixels(with);
        s->setNodeVisible(comb, false);
        render(e, kWarm);
        view->readPixels(without);
        s->setNodeVisible(comb, true);
        // The comb's foot (y = 0 at z = 1) through the camera: rows below it are floor.
        unsigned footRow = kHeight;
        for (unsigned y = 0; y < kHeight && footRow == kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) {
                const size_t p = size_t(y) * kWidth + x;
                bool d = false;
                for (int c = 0; c < 3; ++c) d |= std::abs(int(with.rgba[p * 4u + c]) - int(without.rgba[p * 4u + c])) > 12;
                if (d) { footRow = y; break; }
            }
        // the reflection is under the comb's own pixels: keep the changed pixels of the
        // lower half of the changed band (the comb's own run ends at the floor line)
        unsigned lastRow = footRow;
        for (unsigned y = footRow; y < kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) {
                const size_t p = size_t(y) * kWidth + x;
                for (int c = 0; c < 3; ++c)
                    if (std::abs(int(with.rgba[p * 4u + c]) - int(without.rgba[p * 4u + c])) > 12) lastRow = y;
            }
        const unsigned floorLine = footRow + (lastRow - footRow) / 2u;
        for (unsigned y = 0; y < kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) {
                const size_t p = size_t(y) * kWidth + x;
                bool d = false;
                for (int c = 0; c < 3; ++c) d |= std::abs(int(with.rgba[p * 4u + c]) - int(without.rgba[p * 4u + c])) > 12;
                // --rays: the comb's OWN pixels (above its foot's reflection); else the
                // floor's reflection of it.
                if (d && (gRays ? y + 2u < floorLine : y > floorLine + 2u)) region[p] = 1;
            }
    }
    double moving[3] = {}, still[3] = {};
    int pixels[3] = {};
    const int checkpoints[3] = { 20, 33, 46 };
    for (int k = 0; k < 3; ++k) {
        const int f = checkpoints[k];
        // MOVING: from the start of the slide to the checkpoint, one step a frame.
        for (int i = 0; i <= f; ++i) {
            enginetest::setNodePosition(s, sphere, sphereAt(i));
            render(e, 1);
        }
        Image mov, settled, stillImg, gone;
        view->readPixels(mov);
        render(e, kSettle);
        view->readPixels(settled);
        // THE SPHERE'S OWN FOOTPRINT (and its reflection), excluded: the settled frame
        // against the same pose with the sphere hidden — and the previous pose's too.
        s->setNodeVisible(sphere, false);
        render(e, kSettle);
        view->readPixels(gone);
        s->setNodeVisible(sphere, true);
        std::vector<unsigned char> mask = region;
        for (size_t p = 0; p < mask.size(); ++p) {
            if (!mask[p]) continue;
            for (int c = 0; c < 3; ++c)
                if (std::abs(int(settled.rgba[p * 4u + c]) - int(gone.rgba[p * 4u + c])) > 3) { mask[p] = 0; break; }
        }
        // STILL: parked at the checkpoint for the same frames from far away.
        enginetest::setNodePosition(s, sphere, Vec3(0.0f, -50.0f, 0.0f));
        render(e, kSettle);
        enginetest::setNodePosition(s, sphere, sphereAt(f));
        render(e, f + 1);
        view->readPixels(stillImg);
        int n = 0;
        moving[k] = meanDiff(mov, settled, mask, n);
        still[k] = meanDiff(stillImg, settled, mask, n);
        pixels[k] = n;
        std::printf("RESULT checkpoint %d (sphere x %.2f): the comb's reflection, moving %.3f codes, still %.3f "
                    "(%d px)\n", f, double(sphereAt(f).x), moving[k], still[k], n);
        enginetest::setNodePosition(s, sphere, Vec3(0.0f, -50.0f, 0.0f));
        render(e, kSettle);
    }
    for (int k = 0; k < 3; ++k)
        CHECK_MSG(pixels[k] > 200 && moving[k] <= 1.5 * still[k] + 0.5,
                  "checkpoint %d: a still character in front of a moving Atom mover keeps its own %s (moving %.3f "
                  "vs still %.3f codes; bar 1.5x + 0.5; %d px)", checkpoints[k],
                  gRays ? "reflection (the ray tier's)" : "motion", moving[k], still[k], pixels[k]);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
