// gi.screen_bind (PHOTON-7 stage 7, the lead's two risk fixes) - THE NO-RAY SCREEN PASSES' SHARED JOBS BIND NOTHING
// STALE, AND A SCREENSHOT LEAVES THE LIVE VIEW'S AREA HISTORY ALONE.
//
// The no-ray route's three compute passes (Jahshaka/ScreenIntegrate, ScreenAreaVis, ScreenAreaVisResolve) run SHARED
// jobs (CompositorPassCompute finds them by name, uncloned): every view's ScreenGiListener sets their bindings in front
// of its own dispatch.
// THE FIXTURE: two identical scenes (a floor, a back wall, a plate under an area light 1 x 0.5 m facing down, a dim sky),
// High, rays off, each with its own 256 x 256 still picture under one camera - S1's view the LIVE one, S2's its TWIN:
// the no-screenshot arm, in the same process on the same frames (the premise, measured on every frame before a
// screenshot: the two pictures bit-identical).
// THE LAWS:
//   (1) THE STAND-DOWN: a third view of S2 at another size and camera renders last (the jobs' last bindings are its);
//       at rest, the frame the passes read their inputs as missing (the door gi.screenBindMissing) is DRAWN - every
//       view presents it, the engine writes no error - and every view's picture is bit-identical to its picture of the
//       frame before: nothing dispatched over another view's bindings, the products the last ones written; and again
//       the frame after that third view is DESTROYED (its buffers freed: no job holds them). Before the fix the first
//       view's passes dispatched over the third's bindings and Ogre's barrier solver threw ("Transitioning texture ...
//       from Undefined to a read-only layout"): the frame lost for every view.
//   (2) THE CAPTURE: mid-rest (the area history still accumulating), a clean frame capture of the live view
//       (View::requestFrameCapture: its chain a second time, the same frame): the capture is the frame presented, byte
//       for byte, and the live view's next frames are bit-identical to its twin's (before the fix: the capture's trace
//       advanced the live view's history a second time on that frame - a different picture until it held).
//   (3) THE SCREENSHOT AT ANOTHER SIZE (EngineSceneViewport::takeScreenshot's route: a still picture of S1 of its own,
//       192 x 128, the live view's camera): while it renders and after it is destroyed, the live view's frames are
//       bit-identical to its twin's.
// A fixture (the engine alone, offscreen views). Counted in frames.

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
NodeId cube(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo) {
    const NodeId node = s->createNode();
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
Scene *buildScene(Engine *e, const char *name) {
    Scene *s = e->createScene(name);
    if (!s) return nullptr;
    s->setRayTracing(RayTracingMode::Off);
    cube(s, Vec3(0.0f, -0.05f, 0.0f), Vec3(3.0f, 0.1f, 3.0f), Colour(0.6f, 0.6f, 0.6f));     // the floor
    cube(s, Vec3(0.0f, 1.0f, -1.05f), Vec3(3.0f, 2.0f, 0.1f), Colour(0.5f, 0.5f, 0.6f));     // the wall
    cube(s, Vec3(-0.4f, 0.6f, 0.0f), Vec3(0.8f, 0.05f, 1.6f), Colour(0.7f, 0.3f, 0.2f));     // the plate
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
    float sh[27] = { 0 };
    sh[0] = 0.08f; sh[1] = 0.09f; sh[2] = 0.12f;
    s->setAmbientSh(sh);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    return s->setGlobalIllumination(gi) ? s : nullptr;
}
View *stillView(Engine *e, const char *name, unsigned w, unsigned h, Scene *s) {
    View *v = e->createOffscreenView(name, w, h, Colour(0, 0, 0));
    if (!v) return nullptr;
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 1;
    fx.hdrReadback = true;
    v->setPostFx(fx);
    v->setOffscreenContract(OffscreenContract::StillPicture);
    v->setScene(s);
    return v;
}
bool same(const ImageF &a, const ImageF &b) {
    return a.width && a.width == b.width && a.height == b.height &&
           std::memcmp(a.rgba.data(), b.rgba.data(), a.rgba.size() * sizeof(float)) == 0;
}
/// the pixels that differ, and the largest difference of a channel
std::string diff(const ImageF &a, const ImageF &b) {
    if (a.width != b.width || a.height != b.height || !a.width) return "(no picture)";
    size_t n = 0;
    float worst = 0.0f;
    for (size_t p = 0; p < a.rgba.size(); p += 4) {
        float d = 0.0f;
        for (int c = 0; c < 3; ++c) d = std::max(d, std::fabs(a.rgba[p + c] - b.rgba[p + c]));
        if (d > 0.0f) ++n;
        worst = std::max(worst, d);
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%zu px differ, worst %.3g", n, double(worst));
    return buf;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-screen-bind-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };

    Scene *s1 = buildScene(e, "screen_bind_live");
    Scene *s2 = buildScene(e, "screen_bind_twin");
    if (!s1 || !s2) { std::printf("FAIL: the scenes\n"); return 1; }
    View *live = stillView(e, "screen_bind_live", 256, 256, s1);
    View *twin = stillView(e, "screen_bind_twin", 256, 256, s2);
    if (!live || !twin) { std::printf("FAIL: the views\n"); return 1; }
    const Vec3 eye(0.0f, 1.9f, 2.6f), at(0.0f, 0.3f, -0.5f);
    float nudge = 0.0f;
    const auto aim = [&]() {
        for (View *v : { live, twin }) enginetest::testCameraLookAt(v, Vec3(eye.x + nudge, eye.y, eye.z), at);
    };
    aim();
    e->waitForAsyncShaders();

    // THE SETTLE: both scenes' GI at rest, their area visibility held (every stratum once)
    const auto settle = [&](int cap) {
        int quiet = 0, f = 0;
        for (; f < cap && quiet < 20; ++f) {
            run(1);
            const bool rest = s1->published().gi.giAtRest && s2->published().gi.giAtRest &&
                              s1->published().areaVis.held && s2->published().areaVis.held;
            quiet = rest ? quiet + 1 : 0;
        }
        return quiet >= 20 ? f - quiet : -1;
    };
    // A STEP OF THE CAMERA (both views, the same): the area history restarts - mid-rest for the next n^2 frames
    const auto restart = [&]() {
        nudge += 0.002f;
        aim();
        run(1);
    };
    struct Pics { ImageF live, twin; };
    const auto read = [&]() {
        Pics p;
        if (!live->readPixelsHdr(p.live) || !twin->readPixelsHdr(p.twin)) p.live.width = 0;
        return p;
    };
    const int rest0 = settle(700);
    std::printf("   settled in %d frames (live: at rest %d, held %d, restFrames %u, %s; twin: at rest %d, held %d, "
                "restFrames %u, %s)\n", rest0, int(s1->published().gi.giAtRest), int(s1->published().areaVis.held),
                s1->published().areaVis.restFrames, s1->published().areaVis.reason.c_str(),
                int(s2->published().gi.giAtRest), int(s2->published().areaVis.held), s2->published().areaVis.restFrames,
                s2->published().areaVis.reason.c_str());
    if (rest0 < 0) { std::printf("FAIL: the scenes reach their rest\n"); return 1; }
    char msg[384];

    // ---- (2) THE CAPTURE, mid-rest ----
    {
        restart();
        run(3);
        const AreaVisStatus st = s1->published().areaVis;
        bool premise = true;
        for (int k = 0; k < 2; ++k) {
            run(1);
            const Pics p = read();
            premise = premise && same(p.live, p.twin);
        }
        const bool armed = live->requestFrameCapture(false, true);
        Image shot, presented;
        bool taken = false;
        bool twins = true;
        std::string first;
        for (int k = 0; k < 8; ++k) {
            run(1);
            if (!taken && live->frameCaptureState() == FrameCaptureState::Ready)
                taken = live->takeFrameCapture(shot, false, &presented);
            const Pics p = read();
            if (!same(p.live, p.twin) && twins) first = "frame +" + std::to_string(k + 1) + ": " + diff(p.live, p.twin);
            twins = twins && same(p.live, p.twin);
        }
        const AreaVisStatus after = s1->published().areaVis;
        std::printf("   (2) the capture at rest frame %u (held %d; %u after), the premise %s; capture %s, %ux%u; %s\n",
                    st.restFrames, int(st.held), after.restFrames, premise ? "held" : "FAILED",
                    taken ? "taken" : "NOT TAKEN", shot.width, shot.height, twins ? "the twins identical" : first.c_str());
        CHECK(premise && !st.held && st.running, "(2) premise: mid-rest, the live view and its twin bit-identical");
        CHECK(armed && taken && shot.width == 256u && shot.rgba == presented.rgba,
              "(2) the clean capture is the frame presented, byte for byte (it takes the view's area visibility)");
        CHECK(twins, "(2) after the capture the live view's frames are bit-identical to its twin's");
    }

    // ---- (3) THE SCREENSHOT AT ANOTHER SIZE, mid-rest ----
    {
        if (settle(300) < 0) std::printf("   (3) NOT at rest before the screenshot\n");
        restart();
        run(3);
        const AreaVisStatus st = s1->published().areaVis;
        const Pics p0 = read();
        const bool premise = same(p0.live, p0.twin);
        View *shot = stillView(e, "screen_bind_shot", 192, 128, s1);
        bool twins = shot != nullptr;
        std::string first;
        enginetest::testCameraLookAt(shot, Vec3(eye.x + nudge, eye.y, eye.z), at);
        for (int k = 0; k < 6; ++k) {
            run(1);
            const Pics p = read();
            if (!same(p.live, p.twin) && twins) first = "with it, frame +" + std::to_string(k + 1) + ": " + diff(p.live, p.twin);
            twins = twins && same(p.live, p.twin);
        }
        ImageF shotPic;
        const bool shotRead = shot && shot->readPixelsHdr(shotPic) && shotPic.width == 192u;
        if (shot) e->destroyView(shot);
        for (int k = 0; k < 6; ++k) {
            run(1);
            const Pics p = read();
            if (!same(p.live, p.twin) && twins) first = "after it, frame +" + std::to_string(k + 1) + ": " + diff(p.live, p.twin);
            twins = twins && same(p.live, p.twin);
        }
        std::printf("   (3) the screenshot at rest frame %u (held %d), the premise %s; %s\n", st.restFrames,
                    int(st.held), premise ? "held" : "FAILED", twins ? "the twins identical" : first.c_str());
        CHECK(premise && !st.held && shotRead, "(3) premise: mid-rest, the twins identical, the screenshot read");
        CHECK(twins, "(3) while the screenshot renders and after, the live view's frames are bit-identical to its twin's");
    }

    // ---- (1) THE STAND-DOWN, at rest ----
    {
        View *third = stillView(e, "screen_bind_third", 160, 96, s2);
        if (!third) { std::printf("FAIL: the third view\n"); return 1; }
        enginetest::testCameraLookAt(third, Vec3(1.4f, 1.2f, 1.8f), Vec3(-0.2f, 0.5f, -0.6f));
        const int restT = settle(400);
        // A FRAME IS DRAWN: every view presented it, and the engine wrote no error for it (a stale binding threw in
        // Ogre's barrier solver - the whole frame lost, every view's picture the one before)
        const auto errors = [&]() {
            const auto &fe = e->published().frameErrors;
            return fe.empty() ? 0ull : fe.back().serial;
        };
        struct Three { Pics p; ImageF t; unsigned long long shown[3] = {}; };
        const auto read3 = [&](View *t) {
            Three r;
            r.p = read();
            if (t && !t->readPixelsHdr(r.t)) r.t.width = 0;
            r.shown[0] = live->published().presented;
            r.shown[1] = twin->published().presented;
            r.shown[2] = t ? t->published().presented : 0ull;
            return r;
        };
        const auto drawn = [](const Three &x, const Three &y, int views) {
            for (int k = 0; k < views; ++k)
                if (y.shown[k] != x.shown[k] + 1ull) return false;
            return true;
        };
        run(1);
        const Three a = read3(third);
        run(1);
        const Three b = read3(third);
        const bool floor = same(a.p.live, b.p.live) && same(a.p.twin, b.p.twin) && same(a.t, b.t) && drawn(a, b, 3);
        const unsigned long long err0 = errors();
        e->setArm("gi.screenBindMissing", 1.0);
        run(1);
        const Three c = read3(third);
        e->setArm("gi.screenBindMissing", 0.0);
        run(1);
        const Three d = read3(third);
        const unsigned long long err1 = errors();
        std::printf("   (1) at rest in %d frames; frame to frame %s; the stand-down frame %s (presented live %llu -> %llu, "
                    "third %llu -> %llu; %s): live %s, twin %s, third %s; the frame after: live %s\n", restT,
                    floor ? "identical" : "MOVING", drawn(b, c, 3) ? "drawn" : "LOST", b.shown[0], c.shown[0], b.shown[2],
                    c.shown[2], err1 == err0 ? "no error" : e->published().frameErrors.back().text.substr(0, 160).c_str(),
                    diff(b.p.live, c.p.live).c_str(), diff(b.p.twin, c.p.twin).c_str(), diff(b.t, c.t).c_str(),
                    diff(b.p.live, d.p.live).c_str());
        CHECK(restT >= 0 && floor, "(1) premise: at rest, every view draws every frame, its picture bit-identical");
        CHECK(drawn(b, c, 3) && drawn(c, d, 3) && err1 == err0 && same(b.p.live, c.p.live) && same(b.p.twin, c.p.twin) &&
                  same(b.t, c.t) && same(b.p.live, d.p.live),
              "(1) the frame the passes stand down is drawn, no error, every view's picture its picture of the frame before");
        // the third view destroyed: its listener's buffers freed - the jobs hold none of them
        e->destroyView(third);
        run(2);
        const Three f0 = read3(nullptr);
        e->setArm("gi.screenBindMissing", 1.0);
        run(1);
        const Three f1 = read3(nullptr);
        e->setArm("gi.screenBindMissing", 0.0);
        run(1);
        const Three f2 = read3(nullptr);
        const unsigned long long err2 = errors();
        std::printf("   (1) the third view destroyed, the stand-down frame %s (%s): live %s, twin %s; the frame after: live "
                    "%s\n", drawn(f0, f1, 2) ? "drawn" : "LOST", err2 == err1 ? "no error" : "AN ERROR",
                    diff(f0.p.live, f1.p.live).c_str(), diff(f0.p.twin, f1.p.twin).c_str(),
                    diff(f0.p.live, f2.p.live).c_str());
        std::snprintf(msg, sizeof msg, "(1) the third view destroyed, the stand-down frame and the next are drawn, no "
                      "error, the pictures of the frame before");
        CHECK(drawn(f0, f1, 2) && drawn(f1, f2, 2) && err2 == err1 && same(f0.p.live, f1.p.live) &&
                  same(f0.p.twin, f1.p.twin) && same(f0.p.live, f2.p.live) && same(f0.p.twin, f2.p.twin), msg);
    }

    live->setScene(nullptr);
    twin->setScene(nullptr);
    e->destroyView(live);
    e->destroyView(twin);
    e->destroyScene(s1);
    e->destroyScene(s2);
    run(2);
    std::printf("\ngi.screen_bind: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
