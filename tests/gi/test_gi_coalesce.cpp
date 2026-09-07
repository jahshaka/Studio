// GI REBUILD COALESCING — the drag-a-light gate (REFLECTIONS_ADOPTION_SPEC.md
// §5 / P2).
//
// The thing being prevented: with Auto Refresh on, a light whose transform
// changes made SceneMirror::applyEnvironment call
// Scene::refreshGlobalIllumination() on THAT FRAME. For VCT that is a full
// teardown plus a re-voxelize of every item; for the hybrid it is additionally
// every probe re-rendered twice — 216 face renders at the shipped 18-probe grid
// — synchronously inside mirror sync. Dragging a light therefore paid the whole
// bill once per frame. It is invisible in the picture (a re-voxelized scene
// looks identical) and invisible in the document; only a counter can see it,
// which is why this suite is counters plus one pixel.
//
// The fix has two halves and this asserts both:
//   * a changed light signature ARMS a pending refresh instead of performing
//     one; the full re-solve fires when the signature has held still for
//     kGiStableFrames frames or kGiStableMs milliseconds, whichever comes
//     first. So a 60-frame drag costs ONE re-solve, not sixty.
//   * while the drag is in flight the CHEAP path runs every
//     kGiLightOnlyEveryN frames — Scene::refreshGiLighting(), which re-injects
//     the lights into the voxels that are already there and skips the
//     voxelizer and the probes entirely — so bounced light follows the light
//     being dragged rather than freezing until the mouse comes up.
//
// It links the mirror as well as the engine, like tests/mirror does, because
// the coalescing gate lives in the mirror and the cheap path lives in the
// engine, and the contract is the pair.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
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

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-coalesce-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("coalesce", 128, 128, Colour(0, 0, 0));
    Scene *escene = engine->createScene("coalesce");
    view->setScene(escene);

    // ---- the document ----------------------------------------------------
    // A white floor and a RED WALL, the gi.modes construction: a light aimed
    // near-horizontally at the wall lights it brightly and the floor only at a
    // graze, so red on the floor is bounced light and nothing else. Low quality
    // 64^3 voxels — the same resolution gi.modes proves a red bounce at.
    auto doc = iris::Scene::create();
    doc->giMode = iris::GiMode::VCT;
    doc->giQuality = iris::GiQuality::MEDIUM;
    doc->giAutoRefresh = true;
    doc->giNumBounces = 2;
    // Zero ambient, like gi.modes: the red on the floor has to be BOUNCE and
    // nothing else, or the assertion is a study of the ambient term.
    doc->ambientColor = QColor(0, 0, 0);
    doc->ambientFromSky = false;
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(0, 0, 0);
    // Pinned bounds: the auto fit is P1a's subject and would make this suite's
    // numbers move when that heuristic changes. Nothing here is about bounds.
    doc->giBoundsMin = iris::Vec3(-7.2f, -0.3f, -7.2f);
    doc->giBoundsMax = iris::Vec3(7.2f, 6.5f, 7.2f);

    // Geometry and light copied from gi.modes (tests/gi/test_gi.cpp), which is
    // the construction already proven to produce a measurable red bounce: a big
    // white floor, a thick red wall at -Z, and a directional light pitched +80
    // degrees about X so it hits the wall nearly head-on and the floor only at
    // a ~10 degree graze. Document lights shine down their -Y exactly as engine
    // lights do, so the same +80 reads the same way here.
    auto floor = iris::MeshNode::create();
    floor->setName("floor");
    floor->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    floor->setLocalPos(iris::Vec3(0.0f, -0.05f, 0.0f));
    floor->setLocalScale(iris::Vec3(14.0f, 0.1f, 14.0f));
    auto floorMat = iris::PbrMaterial::create();
    floorMat->setBaseColor(QColor(255, 255, 255));
    floorMat->setRoughnessFactor(0.9f);
    floorMat->setMetallicFactor(0.0f);
    floor->setMaterial(floorMat);
    doc->getRootNode()->addChild(floor);

    auto wall = iris::MeshNode::create();
    wall->setName("red wall");
    wall->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    wall->setLocalPos(iris::Vec3(0.0f, 3.0f, -3.0f));
    wall->setLocalScale(iris::Vec3(12.0f, 6.0f, 0.9f));
    auto wallMat = iris::PbrMaterial::create();
    wallMat->setBaseColor(QColor(255, 13, 13));
    wallMat->setRoughnessFactor(0.9f);
    wallMat->setMetallicFactor(0.0f);
    wall->setMaterial(wallMat);
    doc->getRootNode()->addChild(wall);

    auto sun = iris::LightNode::create();
    sun->setName("sun");
    sun->lightType = iris::LightType::Directional;
    sun->intensity = 2.0f;
    sun->shadowMap->shadowType = iris::ShadowMapType::None;
    sun->setLocalPos(iris::Vec3(0.0f, 6.0f, 6.0f));
    sun->setLocalRot(iris::Quat::fromEulerAngles(80.0f, 0.0f, 0.0f));   // at the wall
    doc->getRootNode()->addChild(sun);

    SceneMirror mirror(escene);
    mirror.setSource(doc);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 4.0f, 6.0f));
    cam->lookAt(iris::Vec3(0.0f, 0.0f, -0.5f));
    cam->update(0.0f);
    mirror.applyCamera(cam, view);

    const auto frame = [&]() {
        doc->update(0.016f);
        mirror.sync();
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
    };
    const auto floorPixel = [&]() {
        Image img;
        view->readPixels(img);
        return img.at(64, 96);     // the floor patch the bounce tints (gi.modes' pixel)
    };

    // ---- arm ---------------------------------------------------------------
    frame();
    CHECK(mirror.giPushCount() == 1, "the first sync pushes VCT exactly once");
    const quint64 refresh0 = mirror.giRefreshCount();
    const quint64 light0 = mirror.giLightRefreshCount();
    CHECK(refresh0 == 0 && light0 == 0, "the push is neither a re-solve nor a light re-inject");
    for (int f = 0; f < 20; ++f) frame();
    CHECK(mirror.giRefreshCount() == refresh0 && mirror.giLightRefreshCount() == light0,
          "20 idle frames cost nothing at all (the pre-existing debounce still holds)");
    const Colour before = floorPixel();
    std::printf("   floor, light at start   r=%.3f g=%.3f b=%.3f\n", before.r, before.g, before.b);

    // ---- THE DRAG ----------------------------------------------------------
    // 60 frames of a light moving every single frame — the shape of a user
    // dragging a light across the scene, which is exactly what used to cost one
    // full re-voxelize per frame. It ends aimed AWAY from the red wall.
    const quint64 refreshBeforeDrag = mirror.giRefreshCount();
    const quint64 lightBeforeDrag = mirror.giLightRefreshCount();
    for (int f = 0; f < 60; ++f) {
        // +80 degrees (at the wall) swinging to -80 (away from it), so the red
        // bounce the first frame had must be gone by the last one.
        const float t = float(f + 1) / 60.0f;
        sun->setLocalRot(iris::Quat::fromEulerAngles(80.0f - 160.0f * t, 0.0f, 0.0f));
        frame();
    }
    const quint64 refreshDuringDrag = mirror.giRefreshCount() - refreshBeforeDrag;
    const quint64 lightDuringDrag = mirror.giLightRefreshCount() - lightBeforeDrag;
    std::printf("   60-frame drag: full re-solves = %llu, cheap light re-injects = %llu\n",
                (unsigned long long)refreshDuringDrag, (unsigned long long)lightDuringDrag);
    CHECK(refreshDuringDrag == 0,
          "a 60-frame drag triggers ZERO full GI re-solves (before this phase: 60)");
    CHECK(lightDuringDrag >= 4 && lightDuringDrag <= 8,
          "...and the cheap light-only re-inject runs every ~10 frames instead");

    // The cheap path is not merely cheap, it WORKS: the picture right now — the
    // drag's last frame, before any full re-solve has run — must already show
    // the bounce following the light. (r - g) is what isolates it: the direct
    // term on this floor is neutral and, because the swing is symmetric about
    // straight-down, is very nearly the same at both ends of the drag, so the
    // red is bounce and only bounce.
    const Colour midDrag = floorPixel();
    std::printf("   floor, drag's last frame (no full re-solve yet)  r=%.3f g=%.3f b=%.3f\n",
                midDrag.r, midDrag.g, midDrag.b);
    CHECK((midDrag.r - midDrag.g) < (before.r - before.g) - 0.015f,
          "the light-only re-inject moves the bounce DURING the drag (not just after it)");

    // ---- letting go --------------------------------------------------------
    // The signature is now still. Within the coalescing window the full
    // re-solve fires, and exactly once however long we keep rendering.
    for (int f = 0; f < 25; ++f) frame();
    const quint64 refreshAfter = mirror.giRefreshCount() - refreshBeforeDrag;
    std::printf("   after the drag stops:   full re-solves = %llu\n",
                (unsigned long long)refreshAfter);
    CHECK(refreshAfter == 1, "letting go re-solves EXACTLY once for the whole drag");
    for (int f = 0; f < 25; ++f) frame();
    CHECK(mirror.giRefreshCount() - refreshBeforeDrag == 1,
          "and never again while nothing moves");

    // ---- the picture is right, not merely cheap ----------------------------
    // The light now points away from the wall, so the red bounce on the floor
    // must be gone. A coalescing gate that never fired would leave the OLD
    // bounce on screen, so this is what stops "cheap" from meaning "stale".
    const Colour after = floorPixel();
    std::printf("   floor, light at end     r=%.3f g=%.3f b=%.3f\n", after.r, after.g, after.b);
    CHECK((before.r - before.g) > 0.02f,
          "the starting frame really had a red bounce to lose (the test is not vacuous)");
    CHECK((after.r - after.g) < (before.r - before.g) - 0.01f,
          "the bounce ends up at the light's FINAL position, not its first one");
    // And the strongest statement this suite can make about the fast path: for
    // a change that is ONLY a light moving, the cheap re-inject already produced
    // the answer the full re-solve then confirms. The expensive rebuild is
    // buying correctness for everything ELSE that may have changed, not for the
    // light — which is exactly why deferring it to the end of the drag is safe.
    CHECK(std::fabs((after.r - after.g) - (midDrag.r - midDrag.g)) < 0.01f,
          "the full re-solve agrees with what the cheap path had already drawn");

    // ---- the explicit refresh does NOT wait (P1d) --------------------------
    // world.refreshGi() bumps the document serial. It is a demand, not a hint:
    // it must re-solve on the very next sync, coalescing window or not.
    const quint64 beforeExplicit = mirror.giRefreshCount();
    ++doc->giRefreshSerial;
    frame();
    CHECK(mirror.giRefreshCount() == beforeExplicit + 1,
          "world.refreshGi()'s serial re-solves on the NEXT frame, without waiting");
    for (int f = 0; f < 20; ++f) frame();
    CHECK(mirror.giRefreshCount() == beforeExplicit + 1,
          "...and only once per bump");

    // ---- auto refresh off means off ----------------------------------------
    doc->giAutoRefresh = false;
    const quint64 beforeOff = mirror.giRefreshCount();
    const quint64 lightBeforeOff = mirror.giLightRefreshCount();
    for (int f = 0; f < 30; ++f) {
        sun->setLocalPos(iris::Vec3(0.0f, 6.0f, 6.0f - 0.1f * f));
        frame();
    }
    for (int f = 0; f < 20; ++f) frame();
    CHECK(mirror.giRefreshCount() == beforeOff &&
          mirror.giLightRefreshCount() == lightBeforeOff,
          "with Auto Refresh off, neither path runs however much the light moves");

    // ---- RE-FIT ON EXIT (LIGHTING_FIX fix 2) -------------------------------
    //
    // The other thing that stales a GI solve, and the one nothing watched: an
    // OBJECT leaving the lit volume. Raise a cube above the auto-fitted volume
    // and it kept the lighting it had at the old height until the user happened
    // to nudge a LIGHT, at which point everything "mysteriously" fixed itself.
    // That workaround is the bug report.
    //
    // The contract asserted here is exactly the one a dragged light already
    // has, because it rides the same debounce: continuous movement costs ZERO
    // rebuilds, and letting go costs exactly ONE.
    //
    // Auto bounds for this section — the suite above pins them on purpose, and
    // an escape from a hand-typed box is not a thing (a typed box is the user's
    // statement about where GI happens, so giEscapeSignature returns 0 for it).
    doc->giAutoRefresh = true;
    doc->giBoundsMin = iris::Vec3(0, 0, 0);
    doc->giBoundsMax = iris::Vec3(0, 0, 0);
    frame();                       // the bounds change is a param change: one push
    for (int f = 0; f < 25; ++f) frame();

    auto flyer = iris::MeshNode::create();
    flyer->setName("flyer");
    flyer->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    flyer->setLocalPos(iris::Vec3(0.0f, 0.6f, 1.0f));
    flyer->setLocalScale(iris::Vec3(1.0f, 1.0f, 1.0f));
    auto flyerMat = iris::PbrMaterial::create();
    flyerMat->setBaseColor(QColor(255, 255, 255));
    flyer->setMaterial(flyerMat);
    doc->getRootNode()->addChild(flyer);
    for (int f = 0; f < 25; ++f) frame();          // let it settle into the volume

    const quint64 refreshBeforeLift = mirror.giRefreshCount();
    // THE DRAG: 40 frames of the cube climbing out of the lit volume.
    for (int f = 0; f < 40; ++f) {
        flyer->setLocalPos(iris::Vec3(0.0f, 0.6f + 0.6f * float(f + 1), 1.0f));
        frame();
    }
    const quint64 duringLift = mirror.giRefreshCount() - refreshBeforeLift;
    std::printf("   40-frame lift out of the volume: full re-solves = %llu\n",
                (unsigned long long)duringLift);
    CHECK(duringLift == 0,
          "dragging a cube out of the lit volume triggers ZERO per-frame rebuilds");

    // ...and letting go re-fits, ONCE, without anyone touching a light.
    for (int f = 0; f < 25; ++f) frame();
    const quint64 afterLift = mirror.giRefreshCount() - refreshBeforeLift;
    std::printf("   after the cube stops:   full re-solves = %llu\n",
                (unsigned long long)afterLift);
    CHECK(afterLift == 1, "letting go re-fits the volume EXACTLY once");
    for (int f = 0; f < 40; ++f) frame();
    CHECK(mirror.giRefreshCount() - refreshBeforeLift == 1,
          "...and never again while nothing moves (the re-fit is not self-sustaining)");

    // The point of all of it: the cube is now INSIDE the lit volume the
    // renderer is using. Nothing but a re-fit could have put it there.
    {
        const jahshaka::engine::GiStatus st = escene->giStatus();
        std::printf("   lit volume after the re-fit: y %.2f .. %.2f (cube at y %.2f)\n",
                    st.boundsMin.y, st.boundsMax.y, flyer->getLocalPos().y());
        CHECK(st.boundsMax.y > flyer->getLocalPos().y(),
              "the re-fitted volume reaches the cube's new height");
    }

    doc->giMode = iris::GiMode::OFF;
    frame();
    mirror.setSource(iris::ScenePtr());
    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
