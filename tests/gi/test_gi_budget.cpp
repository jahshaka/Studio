// THE GI UPDATE BUDGET — the realtime model's gate (FIX WAVE B1-B5, 2026-09-07).
//
// What shipped before this wave: GI updated when a LIGHT moved (debounced), and
// otherwise never. Moving an object changed nothing until somebody pressed
// Refresh; reflection probes were captured once at build time and frozen there
// unless an author opted into `dynamicProbes`, which pinned the nearest N probes
// live for ever and cost their full price every frame. Two knobs (autoRefresh,
// dynamicProbes) for one question, and neither of them was "how much may this
// cost per frame".
//
// What this suite pins, and it is the whole model in four properties:
//
//   A. THE SWEEP IS A GUARANTEE. At `updateBudget = N` over P probes, every
//      probe re-captures within ceil(P / N) frames — not "the N nearest ones
//      do". Priority (staleness x proximity x covers-a-moved-AABB) reorders a
//      sweep; it cannot starve one. Asserted through the PICTURE: after a full
//      sweep the budget-1 grid shows what the budget-P grid shows.
//
//   B. MOVING GEOMETRY COSTS THE CHEAP PATHS, NOT A RE-SOLVE PER FRAME. A
//      40-frame drag costs ZERO full GI re-solves, the light re-injection runs
//      on its cadence during it, and letting go costs EXACTLY ONE. That is the
//      contract gi.coalesce pins for a dragged LIGHT; B3 extends it to geometry
//      by folding a quantized transform hash of the GI items into the same
//      signature. Counters, from the mirror, because it is invisible otherwise.
//
//   C. BUDGET 0 IS THE OLD WORLD, EXACTLY. Nothing re-captures, nothing
//      re-solves, however much the scene moves — the rollback, kept as one
//      switch rather than as a mode.
//
//   D. THE SETTLE RE-SOLVE RE-USES THE ARM (B4). When nothing was DESTROYED
//      since the build, the refresh re-runs the existing voxelizer and
//      re-dirties the probes instead of tearing down and re-placing them, and
//      `giStatus().reusedLastRefresh` says so. Both costs are measured and
//      printed: the reuse is what makes B's "one re-solve per gesture"
//      affordable enough to be the default.
//
// Mirror-linked, like gi.coalesce: half the contract (the debounce, the
// counters) is mirror-side and half (the budget, the reuse arm) is engine-side.
#include <QGuiApplication>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static iris::MeshNodePtr slab(const iris::ScenePtr &doc, const char *name, const QColor &c,
                              const iris::Vec3 &pos, const iris::Vec3 &scale,
                              float rough = 0.9f, float metal = 0.0f)
{
    auto n = iris::MeshNode::create();
    n->setName(QString::fromLatin1(name));
    n->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    n->setLocalPos(pos);
    n->setLocalScale(scale);
    auto m = iris::PbrMaterial::create();
    m->setBaseColor(c);
    m->setRoughnessFactor(rough);
    m->setMetallicFactor(metal);
    n->setMaterial(m);
    doc->getRootNode()->addChild(n);
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-budget-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("budget", 128, 128, Colour(0, 0, 0));
    Scene *escene = engine->createScene("budget");
    view->setScene(escene);

    // ---- the room ---------------------------------------------------------
    // gi.dynamic_probes' room, in DOCUMENT form: a sealed box with one red wall
    // behind the camera, a mirror cube in the middle, and a green slab that
    // slides across the red wall's inner face. The mirror's centre pixel is
    // therefore a direct read of "what do the probes currently hold".
    auto doc = iris::Scene::create();
    doc->giMode = iris::GiMode::VCT_PCC_HYBRID;
    doc->giQuality = iris::GiQuality::MEDIUM;
    doc->giNumBounces = 2;
    doc->giPccGrid = iris::Vec3(2, 1, 2);          // 4 probes: a short sweep
    doc->giUpdateBudget = 0;                       // phase C first; raised below
    doc->ambientColor = QColor(0, 0, 0);
    doc->ambientFromSky = false;
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(0, 0, 0);
    // Pinned bounds, like every suite in this directory that is not about the
    // auto fit: probe placement must not move when P1a's heuristic does.
    doc->giBoundsMin = iris::Vec3(-4.6f, -0.6f, -4.6f);
    doc->giBoundsMax = iris::Vec3(4.6f, 5.6f, 4.6f);
    const int kProbes = 4;

    const QColor white(217, 217, 217), red(255, 5, 5), green(5, 255, 5);
    slab(doc, "floor",   white, iris::Vec3(0.0f, -0.2f, 0.0f),  iris::Vec3(8.8f, 0.4f, 8.8f));
    slab(doc, "ceiling", white, iris::Vec3(0.0f, 5.2f, 0.0f),   iris::Vec3(8.8f, 0.4f, 8.8f));
    slab(doc, "-Z wall", white, iris::Vec3(0.0f, 2.5f, -4.2f),  iris::Vec3(8.8f, 5.0f, 0.4f));
    slab(doc, "-X wall", white, iris::Vec3(-4.2f, 2.5f, 0.0f),  iris::Vec3(0.4f, 5.0f, 8.8f));
    slab(doc, "+X wall", white, iris::Vec3(4.2f, 2.5f, 0.0f),   iris::Vec3(0.4f, 5.0f, 8.8f));
    slab(doc, "red wall", red,  iris::Vec3(0.0f, 2.5f, 4.2f),   iris::Vec3(8.8f, 5.0f, 0.4f));
    slab(doc, "mirror", QColor(255, 255, 255), iris::Vec3(0.0f, 2.0f, 0.0f),
         iris::Vec3(1.6f, 1.6f, 1.6f), 0.0f, 1.0f);

    static const float kMoverParked = -3.2f, kMoverOnRay = 0.0f, kMoverZ = 3.7f;
    auto mover = slab(doc, "mover", green, iris::Vec3(kMoverParked, 2.0f, kMoverZ),
                      iris::Vec3(3.2f, 3.2f, 0.4f));

    auto sun = iris::LightNode::create();
    sun->setName("sun");
    sun->lightType = iris::LightType::Directional;
    sun->intensity = 6.0f;
    sun->shadowMap->shadowType = iris::ShadowMapType::None;
    sun->setLocalPos(iris::Vec3(0.0f, 4.0f, -4.0f));
    // Down -Y by convention; pitched so it travels towards +Z and lights the red
    // wall's inner face (and the mover's -Z face) head-on, as in gi.pcc_mirror.
    sun->setLocalRot(iris::Quat::fromEulerAngles(83.0f, 180.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    SceneMirror mirror(escene);
    mirror.setSource(doc);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 2.0f, 2.4f));
    cam->lookAt(iris::Vec3(0.0f, 2.0f, 0.0f));
    cam->update(0.0f);
    mirror.applyCamera(cam, view);

    const auto frame = [&]() {
        doc->update(0.016f);
        mirror.sync();
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
    };
    const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };
    const auto mirrorPixel = [&]() {
        Image img;
        view->readPixels(img);
        return img.at(64, 64);
    };
    const auto show = [](const char *what, const Colour &c) {
        std::printf("   %-44s r=%.3f g=%.3f b=%.3f  (g-r)=%+.3f\n",
                    what, c.r, c.g, c.b, c.g - c.r);
    };
    const auto moveMover = [&](float x) {
        mover->setLocalPos(iris::Vec3(x, 2.0f, kMoverZ));
    };

    // =======================================================================
    // C. BUDGET 0 IS THE OLD WORLD
    // =======================================================================
    std::printf("-- C: updateBudget 0 (paused)\n");
    frames(8);
    {
        const GiStatus st = escene->giStatus();
        std::printf("   giStatus: probes=%d pccBound=%s updates/frame=%d\n",
                    st.probeCount, st.pccBound ? "true" : "false", st.probeUpdatesPerFrame);
        CHECK(st.probeCount == kProbes && st.pccBound, "the 2x1x2 probe grid built and bound");
        CHECK(st.probeUpdatesPerFrame == 0, "budget 0 spends nothing");
    }
    const Colour pausedParked = mirrorPixel();
    show("budget 0, mover parked", pausedParked);
    const quint64 refresh0 = mirror.giRefreshCount(), light0 = mirror.giLightRefreshCount();
    moveMover(kMoverOnRay);
    frames(40);
    const Colour pausedMoved = mirrorPixel();
    show("budget 0, mover ON the reflection ray", pausedMoved);
    CHECK(mirror.giRefreshCount() == refresh0 && mirror.giLightRefreshCount() == light0,
          "budget 0: moving geometry costs no re-solve and no re-inject");
    CHECK(std::fabs(pausedMoved.g - pausedParked.g) < 0.05f,
          "budget 0: the reflection is frozen at what it last captured");

    // =======================================================================
    // A. THE SWEEP, and B's counters with it
    // =======================================================================
    std::printf("-- A: updateBudget 1 (a sweep of %d probes)\n", kProbes);
    moveMover(kMoverParked);
    doc->giUpdateBudget = 1;
    frames(30);          // the budget change is a param push: one rebuild, then settle
    {
        const GiStatus st = escene->giStatus();
        CHECK(st.probeUpdatesPerFrame == 1, "budget 1 spends exactly one probe update a frame");
    }
    const Colour sweepParked = mirrorPixel();
    show("budget 1, mover parked (settled)", sweepParked);

    const quint64 refreshA = mirror.giRefreshCount(), lightA = mirror.giLightRefreshCount();
    moveMover(kMoverOnRay);
    frame();
    const Colour afterOne = mirrorPixel();
    frames(kProbes - 1);
    const Colour afterSweep = mirrorPixel();
    show("budget 1, ONE frame after the move", afterOne);
    show("budget 1, a FULL SWEEP after the move", afterSweep);
    std::printf("   during the catch-up: full re-solves = %llu, light re-injects = %llu\n",
                (unsigned long long)(mirror.giRefreshCount() - refreshA),
                (unsigned long long)(mirror.giLightRefreshCount() - lightA));

    // THE SWEEP GUARANTEE: within ceil(probes / budget) frames the whole grid
    // has re-captured, so the reflection is fully caught up.
    CHECK(afterSweep.g > afterSweep.r + 0.5f,
          "after ceil(probes / budget) frames the mirror shows the GREEN slab");
    // ...and it was NOT there after one frame, which is what makes the budget a
    // RATE rather than a subset. (Which probe the sweep happens to pick first is
    // deliberately not asserted — priority orders a sweep, it does not define
    // it, and pinning the order would pin an implementation detail.)
    CHECK(!(afterOne.g > afterOne.r + 0.5f),
          "one frame of budget 1 has NOT caught up yet (the budget is a rate)");
    // B, first half: no full re-solve happened during the catch-up. The probes
    // did it on the budget, which is the entire point.
    CHECK(mirror.giRefreshCount() == refreshA,
          "the catch-up cost ZERO full GI re-solves (the probes did it on the budget)");

    // The same move at a budget of `kProbes` lands in ONE frame — the two
    // readings together are the rate.
    frames(30);                       // settle, then park and settle again
    moveMover(kMoverParked);
    frames(30);
    doc->giUpdateBudget = kProbes;
    frames(30);
    CHECK(escene->giStatus().probeUpdatesPerFrame == kProbes,
          "budget 4 spends the whole grid every frame");
    const Colour wholeParked = mirrorPixel();
    moveMover(kMoverOnRay);
    frame();
    const Colour wholeOne = mirrorPixel();
    show("budget 4, mover parked", wholeParked);
    show("budget 4, ONE frame after the move", wholeOne);
    CHECK(wholeOne.g > wholeOne.r + 0.5f,
          "at budget = probeCount ONE frame is a full sweep");
    doc->giUpdateBudget = 1;
    frames(30);

    // =======================================================================
    // B. A DRAG COSTS THE CHEAP PATHS AND EXACTLY ONE RE-SOLVE ON SETTLE
    // =======================================================================
    std::printf("-- B: a 40-frame geometry drag\n");
    frames(30);                       // let the previous move settle completely
    const quint64 refreshB = mirror.giRefreshCount(), lightB = mirror.giLightRefreshCount();
    for (int f = 0; f < 40; ++f) {
        moveMover(kMoverOnRay - 0.06f * float(f));
        frame();
    }
    const quint64 duringSolves = mirror.giRefreshCount() - refreshB;
    const quint64 duringLight  = mirror.giLightRefreshCount() - lightB;
    std::printf("   during the drag: full re-solves = %llu, light re-injects = %llu\n",
                (unsigned long long)duringSolves, (unsigned long long)duringLight);
    CHECK(duringSolves == 0,
          "a 40-frame geometry drag triggers ZERO full GI re-solves (B3's contract)");
    CHECK(duringLight > 0,
          "...and the cheap light re-inject runs during it (bounced light follows)");
    frames(25);                       // let go
    const quint64 afterSolves = mirror.giRefreshCount() - refreshB;
    std::printf("   after it stops: full re-solves = %llu\n", (unsigned long long)afterSolves);
    CHECK(afterSolves == 1, "letting go re-solves EXACTLY once for the whole drag");
    frames(25);
    CHECK(mirror.giRefreshCount() - refreshB == 1,
          "...and never again while nothing moves (the re-solve is not self-sustaining)");

    // =======================================================================
    // D. THE REUSE ARM, AND WHAT IT SAVES (B4)
    // =======================================================================
    std::printf("-- D: the reuse arm\n");
    CHECK(escene->giStatus().reusedLastRefresh,
          "the settle re-solve RE-USED the voxel arm (nothing was destroyed)");

    // The from-scratch reference. setGlobalIllumination ALWAYS tears the arm
    // down and rebuilds it (OgreGi.cpp), which is exactly the cost B4 avoids;
    // the params mirror what the document pushes, so the two timings measure the
    // same build.
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 1;
    gi.boundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.boundsMax = Vec3(4.6f, 5.6f, 4.6f);

    const auto timeMs = [&](int reps, bool scratch) {
        // Warm once so neither number pays for a first-touch allocation.
        double total = 0.0;
        for (int i = 0; i < reps; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            if (scratch) escene->setGlobalIllumination(gi);   // always from scratch
            else         escene->refreshGlobalIllumination(); // takes the reuse arm
            engine->renderOneFrame();
            total += std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - t0).count();
        }
        return total / double(reps);
    };
    timeMs(2, false);
    const double reuse = timeMs(6, false);
    CHECK(escene->giStatus().reusedLastRefresh, "...and keeps re-using it while nothing dies");
    timeMs(2, true);
    const double scratch = timeMs(6, true);
    CHECK(!escene->giStatus().reusedLastRefresh,
          "a from-scratch build reports reusedLastRefresh false");
    std::printf("   MID-SCENE REFRESH COST: reused %.2f ms   from scratch %.2f ms   (%.1fx)\n",
                reuse, scratch, scratch / (reuse > 0.001 ? reuse : 0.001));
    CHECK(scratch > reuse, "the reuse arm is cheaper than a from-scratch rebuild");

    // ...and the guarantee that makes it safe: DESTROY something and the arm
    // must refuse. This is the entire protection for the "always from scratch"
    // rule (VctVoxelizer's raw Item* cache, VctMaterial's datablock-pointer
    // cache), so it is asserted rather than assumed.
    escene->refreshGlobalIllumination();
    CHECK(escene->giStatus().reusedLastRefresh, "the arm is being re-used again");
    {
        auto doomed = slab(doc, "doomed", white, iris::Vec3(3.4f, 0.6f, -3.4f),
                           iris::Vec3(0.5f, 0.5f, 0.5f));
        frames(30);
        CHECK(escene->giStatus().reusedLastRefresh,
              "adding a node still lets the arm be re-used (growth is safe)");
        doc->getRootNode()->removeChild(doomed);
        // The mirror destroys the engine node, which bumps the destruction
        // generation and flags the caches; the frame-time flush then rebuilds
        // FROM SCRATCH, and that is the whole protection for the "always from
        // scratch" rule (VctVoxelizer's raw Item* cache, VctMaterial's
        // datablock-pointer cache). Asserted, not assumed.
        frames(4);
        CHECK(!escene->giStatus().reusedLastRefresh,
              "destroying a node forces a from-scratch rebuild (the B4 guarantee)");
    }
    frames(30);

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
