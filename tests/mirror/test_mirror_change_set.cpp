// mirror.change_set — THE FRAME'S CHANGE SET IS THE ONE SOURCE OF "WHAT MOVED" (ENGINE-UPGRADE-1 U0 3b
// UPGRADE-CHANGESET-1). A law in counts, one process, a VCT world of a 4x4 grid of cubes, a group of 8 more and a
// shadow-casting spot lamp:
//   1. A STILL WORLD: 20 frames drain 20 EMPTY change sets and cost no GI work — no movement scan (giScans flat,
//      giScanMicros 0), no caster-walk visit, no GPU-table slot composed (lastDirty 0 every frame).
//   2. ONE CUBE MOVED: one change-set entry; ONE movement scan that re-reads the mover and nothing else (one
//      caster-walk visit, not the scene's 25).
//   3. THE GROUP MOVED: one entry (the parent), its 8 children re-read by the caster walk — a subtree, not a scene.
//   4. THE LAMP: written with its own pose = one entry and NO re-solve (the pose key compares the named lamp); moved =
//      one entry, one light in it, the light serial up by exactly one (the re-inject's key), one settled re-solve —
//      what the mirror's three light hashes and noteLightsMoved used to say, named instead.
//   6. A POSED RIG NAMES ITS RIDERS: a bone rider's item is re-read when the rig poses — one movement scan over
//      the rider, never a full one (the read of 3b, B: the pose used to owe a nodeless full pass every frame).
//   7. THE ONE GI SCHEDULER answers in environment frames: a moved still lamp settles into one re-solve after the
//      stability window and none before it (settlePending meanwhile), world.refreshGi() re-solves on the next.
//   5. THE SOFT PROMOTION IS DECIDED BY THE SET: a cube written while playing is promoted (one miss), a cube written
//      just BEFORE play started is not (the play edge's sync holds no surprise), and a cube nobody writes stays still.
#include <QGuiApplication>
#include <cmath>
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
#include "jahshaka/engine/EngineInspect.h"   // the engine\'s record of what the fixture pushed (U3 stage 2)
#include "../support/enginetesthelpers.h"
#include "../skeletal/armrig.h"

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
        enginetest::frames(engine, 1);
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

    // 4. the lamp moved (and, first, written with its OWN pose: named, but no move the debounce answers)
    {
        const quint64 r0 = mirror.target()->giStatus().schedulerResolves;
        lamp->setLocalPos(lamp->getLocalPos());
        frame();
        CHECK(changes().changesMoved == 1, "the lamp written with its own pose: still ONE entry, named (%u)",
              changes().changesMoved);
        for (int i = 0; i < 30; ++i) frame();
        CHECK(mirror.target()->giStatus().schedulerResolves == r0, "...but no re-solve settles out of a pose that did not change (%llu)",
              (unsigned long long)(mirror.target()->giStatus().schedulerResolves - r0));
        const unsigned long long s0 = target->lightWriteSerial();
        lamp->setLocalPos(lamp->getLocalPos() + iris::Vec3(0.5f, 0.0f, 0.0f));
        frame();
        const FrameInputStatus c = changes();
        CHECK(c.changesMoved == 1 && c.changesLights == 1, "the lamp moved: ONE entry, ONE light in it (%u, %u)",
              c.changesMoved, c.changesLights);
        CHECK(target->lightWriteSerial() - s0 == 1, "...the light serial up by exactly one, the re-inject's key (%llu)",
              target->lightWriteSerial() - s0);
        const quint64 r1 = mirror.target()->giStatus().schedulerResolves;
        for (int i = 0; i < 20; ++i) frame();
        CHECK(mirror.target()->giStatus().schedulerResolves == r1 + 1, "...and a still lamp that MOVED settles into exactly one re-solve (%llu)",
              (unsigned long long)(mirror.target()->giStatus().schedulerResolves - r1));
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

    // 6. a posed rig with a bone rider (engine verbs on the mirror's own scene)
    {
        const iris::MeshPtr armMesh = armrig::buildArmMesh();
        SkeletonDesc rig;
        MeshData armData;
        const bool translated = SceneMirror::toSkeletonDesc(armMesh->getSkeleton(), rig) &&
                                SceneMirror::toMeshData(armMesh.data(), armData);
        std::vector<float> bi, bw;
        SceneMirror::toSkinData(armMesh.data(), bi, bw);
        armData.blendIndices.resize(bi.size());
        for (size_t i = 0; i < bi.size(); ++i) armData.blendIndices[i] = (unsigned char)(int)bi[i];
        armData.blendWeights = bw;
        MeshData cubeData;
        SceneMirror::toMeshData(cubeMesh.data(), cubeData);
        const MaterialId m = target->createPbrMaterial(PbrParams());
        const NodeId owner = target->createNode();
        const NodeId rider = target->createNode();
        const bool built = translated && target->attachSkinnedMesh(owner, target->createMesh(armData), m, rig) &&
                           target->attachMesh(rider, target->createMesh(cubeData), m) &&
                           target->attachToBone(rider, owner, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1));
        CHECK(built, "a rig with a cube riding its jointTip bone");
        for (int i = 0; i < 20; ++i) frame();
        const size_t bones = jahshaka::engine::inspect::scene(target)->boneNames(owner).size();
        std::vector<BonePose> poses(bones);
        const GiStatus g0 = gi();
        for (int i = 0; i < 4; ++i) {
            const float half = 0.5f * 0.087f * float(i + 1);   // 5 degrees a frame about X
            for (size_t b = 0; b < bones; ++b) poses[b].rotation = Quat(std::sin(half), 0.0f, 0.0f, std::cos(half));
            target->setBonePoses(owner, poses.data(), poses.size());
            frame();
        }
        CHECK(bones > 0 && gi().giScans - g0.giScans == 4 && gi().giScansFull == g0.giScansFull,
              "a posed rig names its rider: 4 posed frames, 4 movement scans, NONE full (%llu, %llu)",
              gi().giScans - g0.giScans, gi().giScansFull - g0.giScansFull);
        for (int i = 0; i < 20; ++i) frame();
    }

    // 7. the one GI scheduler, in environment frames
    {
        const GiStatus g0 = gi();
        lamp->setLocalPos(lamp->getLocalPos() + iris::Vec3(0.0f, 0.0f, 0.5f));
        frame();
        CHECK(gi().settlePending, "a moved still lamp ARMS the settle");
        int frames = 1;
        while (gi().schedulerResolves == g0.schedulerResolves && frames < 40) { frame(); ++frames; }
        CHECK(gi().schedulerResolves == g0.schedulerResolves + 1 && !gi().settlePending,
              "...and settles into ONE re-solve after the stability window (%d environment frames)", frames);
        CHECK(frames == 16, "...at exactly the window: the move's frame + 15 still frames (%d)", frames);
        const unsigned long long r1 = gi().schedulerResolves;
        ++doc->giRefreshSerial;
        frame();
        CHECK(gi().schedulerResolves == r1 + 1, "world.refreshGi()'s serial re-solves on the next frame");
        for (int i = 0; i < 20; ++i) frame();
        CHECK(gi().schedulerResolves == r1 + 1, "...and nothing after it while the world is still");
    }

    mirror.setSource(iris::ScenePtr());
    view->setScene(nullptr);
    engine->destroyScene(target);
    std::printf("mirror.change_set: %s\n", gFailures ? "FAILED" : "PASSED");
    return gFailures ? 1 : 0;
}
