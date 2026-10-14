// mirror.change_set — THE FRAME'S CHANGE SET IS THE ONE SOURCE OF "WHAT MOVED" (ENGINE-UPGRADE-1 U0 3b
// UPGRADE-CHANGESET-1). A law in counts, one process, a VCT world of a 4x4 grid of cubes, a group of 8 more and a
// shadow-casting spot lamp:
//   1. A STILL WORLD: 20 frames drain 20 EMPTY change sets and cost no GI work — no movement scan (giScans flat,
//      giScanMicros 0), no caster-walk visit, no GPU-table slot composed (lastDirty 0 every frame).
//   2. ONE CUBE MOVED: one change-set entry; ONE movement scan that re-reads the mover and nothing else (one
//      caster-walk visit, not the scene's 25).
//   3. THE GROUP MOVED: one entry (the parent), its 8 children re-read by the caster walk — a subtree, not a scene.
//   4. THE LAMP MOVED: one entry, one light in it, the light serial up by exactly one (the re-inject's key) — what
//      the mirror's three light hashes and noteLightsMoved used to say, named instead.
//   5. THE SOFT PROMOTION IS DECIDED BY THE SET: a cube written while playing is promoted (one miss), a cube written
//      just BEFORE play started is not (the play edge's sync holds no surprise), and a cube nobody writes stays still.
#include <QGuiApplication>
#include <cstdio>
#include <string>
#include <vector>

#include "../support/testmesh.h"
#include "../support/mirrorframe.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("== mirror.change_set: the frame's change set is the one source of what moved\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test_mirror_change_set-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    View *view = engine->createOffscreenView("change_set", 64, 64, Colour(0, 0, 0));
    Scene *target = view ? engine->createScene("change_set") : nullptr;
    CHECK(view && target, "an offscreen view and its scene");
    if (!view || !target) return 1;
    view->setScene(target);

    auto doc = iris::Scene::create();
    doc->giMode = iris::GiMode::VCT;
    doc->giDdgi = 0;
    doc->giGather = 0;
    doc->giQuality = iris::GiQuality::LOW;
    const auto cubeMesh = testmesh::load(":assets/models/cube.obj");
    const auto mat = iris::PbrMaterial::create();
    std::vector<iris::MeshNodePtr> cubes;
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x) {
            auto c = iris::MeshNode::create();
            c->setName(QStringLiteral("cube %1").arg(z * 4 + x));
            c->setMesh(cubeMesh);
            c->setMaterial(mat);
            c->setLocalPos(iris::Vec3(float(x) * 2.0f - 3.0f, 0.5f, float(z) * 2.0f - 3.0f));
            doc->getRootNode()->addChild(c, false);
            cubes.push_back(c);
        }
    auto group = iris::SceneNode::create();
    group->setLocalPos(iris::Vec3(0.0f, 0.0f, -8.0f));
    doc->getRootNode()->addChild(group, false);
    for (int i = 0; i < 8; ++i) {
        auto c = iris::MeshNode::create();
        c->setMesh(cubeMesh);
        c->setMaterial(mat);
        c->setLocalPos(iris::Vec3(float(i) - 3.5f, 0.5f, 0.0f));
        group->addChild(c, false);
    }
    auto lamp = iris::LightNode::create();
    lamp->lightType = iris::LightType::Spot;
    lamp->intensity = 4.0f;
    lamp->distance = 30.0f;
    lamp->shadowMap->shadowType = iris::ShadowMapType::Soft;
    lamp->setLocalPos(iris::Vec3(0.0f, 8.0f, 2.0f));
    lamp->setLocalRot(iris::Quat::fromEulerAngles(-60.0f, 0.0f, 0.0f));
    doc->getRootNode()->addChild(lamp, false);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 10.0f, 12.0f));
    cam->lookAt(iris::Vec3(0.0f, 0.0f, -2.0f));
    doc->getRootNode()->addChild(cam, false);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    auto frame = [&] {
        mirror.sync(mirrorframe::world(view, cam));
        engine->renderOneFrame();
    };
    for (int i = 0; i < 40; ++i) frame();   // the open: the arm builds, the static settle runs, everything quiets

    const auto gi = [&] { return target->giStatus(); };
    const auto walks = [&] { return engine->shadowStatus().casterWalkItems; };
    const auto changes = [&] { return engine->frameInputStatus(); };

    // 1. a still world
    {
        const GiStatus g0 = gi();
        const unsigned long long w0 = walks();
        unsigned moved = 0, composed = 0;
        double scanUs = 0.0;
        for (int i = 0; i < 20; ++i) {
            frame();
            moved += changes().changesMoved;
            composed += target->gpuSceneStatus().lastDirty;
            scanUs += gi().giScanMicros;
        }
        CHECK(moved == 0, "20 still frames drain 20 EMPTY change sets (%u entries)", moved);
        CHECK(gi().giScans == g0.giScans && scanUs == 0.0,
              "...and run no GI movement scan (%llu scans, %.1f us)", gi().giScans - g0.giScans, scanUs);
        CHECK(walks() == w0, "...and no caster-walk visit (%llu)", walks() - w0);
        CHECK(composed == 0, "...and compose no GPU-table slot: a frame with no writes composes zero flags (%u)",
              composed);
    }

    // 2. one cube moved
    {
        const GiStatus g0 = gi();
        const unsigned long long w0 = walks();
        cubes[5]->setLocalPos(cubes[5]->getLocalPos() + iris::Vec3(0.0f, 0.0f, 0.5f));
        frame();
        const FrameInputStatus c = changes();
        CHECK(c.changesMoved == 1 && c.changesLights == 0, "one cube moved: ONE change-set entry, no light (%u, %u)",
              c.changesMoved, c.changesLights);
        CHECK(gi().giScans - g0.giScans == 1, "...ONE movement scan (%llu)", gi().giScans - g0.giScans);
        CHECK(walks() - w0 == 1, "...that re-reads the mover and nothing else: 1 caster-walk visit of 25 items (%llu)",
              walks() - w0);
        for (int i = 0; i < 20; ++i) frame();
    }

    // 3. the group moved
    {
        const unsigned long long w0 = walks();
        group->setLocalPos(group->getLocalPos() + iris::Vec3(0.5f, 0.0f, 0.0f));
        frame();
        CHECK(changes().changesMoved == 1, "the group moved: ONE entry, the parent (%u)", changes().changesMoved);
        CHECK(walks() - w0 == 8, "...and its 8 children re-read, a subtree and not the scene (%llu)", walks() - w0);
        for (int i = 0; i < 20; ++i) frame();
    }

    // 4. the lamp moved
    {
        const unsigned long long s0 = target->lightWriteSerial();
        lamp->setLocalPos(lamp->getLocalPos() + iris::Vec3(0.5f, 0.0f, 0.0f));
        frame();
        const FrameInputStatus c = changes();
        CHECK(c.changesMoved == 1 && c.changesLights == 1, "the lamp moved: ONE entry, ONE light in it (%u, %u)",
              c.changesMoved, c.changesLights);
        CHECK(target->lightWriteSerial() - s0 == 1, "...the light serial up by exactly one, the re-inject's key (%llu)",
              target->lightWriteSerial() - s0);
        for (int i = 0; i < 20; ++i) frame();
    }

    // 5. the soft promotion, decided by the set
    {
        const quint64 m0 = mirror.mobilityMissCount();
        cubes[1]->setLocalPos(cubes[1]->getLocalPos() + iris::Vec3(0.0f, 0.25f, 0.0f));   // BEFORE play
        doc->setPlaying(true);
        frame();                                        // the play edge's sync
        CHECK(mirror.mobilityMissCount() == m0, "a cube written just before play is no surprise mover (%llu)",
              (unsigned long long)(mirror.mobilityMissCount() - m0));
        for (int i = 0; i < 3; ++i) frame();
        CHECK(mirror.mobilityMissCount() == m0, "...nor is anything nobody writes while playing (%llu)",
              (unsigned long long)(mirror.mobilityMissCount() - m0));
        cubes[2]->setLocalPos(cubes[2]->getLocalPos() + iris::Vec3(0.0f, 0.25f, 0.0f));   // WHILE playing
        frame();
        CHECK(mirror.mobilityMissCount() == m0 + 1 && mirror.lastMobilityMiss() == cubes[2]->getName(),
              "a cube WRITTEN while playing is promoted: one miss, that cube (%llu)",
              (unsigned long long)(mirror.mobilityMissCount() - m0));
        doc->setPlaying(false);
        frame();
    }

    mirror.setSource(iris::ScenePtr());
    view->setScene(nullptr);
    engine->destroyScene(target);
    std::printf("mirror.change_set: %s\n", gFailures ? "FAILED" : "PASSED");
    return gFailures ? 1 : 0;
}
