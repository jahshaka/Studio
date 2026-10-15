// mirror.frame_input — THE MIRROR EMITS ONE RECORD A FRAME, AND A STILL DOCUMENT'S RECORD DRAINS TO NOTHING
// (ENGINE-UPGRADE-1 U0 3a UPGRADE-RECORD-1). A law in counts (FrameInputStatus), one process:
//   1. a still document, synced with its world view and camera and rendered for 20 frames: ONE submit and ONE drain
//      per frame, and every drain applies NOTHING (fieldsApplied == 0) — the engine's compare at the drain replaced
//      the mirror's pushed-on-change latches;
//   2. the World fog switched on: the next drain applies ONE field (the fog), the one after it none;
//   3. the camera moved: the next drain applies the per-view part only (one view field, no scene field).
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>

#include "../support/testmesh.h"
#include "../support/mirrorframe.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
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
    std::printf("== mirror.frame_input: one record a frame, and a still one drains to nothing\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test_mirror_frame_input-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    View *view = engine->createOffscreenView("frame_input", 64, 64, Colour(0, 0, 0));
    Scene *target = view ? engine->createScene("frame_input") : nullptr;
    CHECK(view && target, "an offscreen view and its scene");
    if (!view || !target) return 1;
    view->setScene(target);

    auto doc = iris::Scene::create();
    auto cube = iris::MeshNode::create();
    cube->setMesh(testmesh::load(":assets/models/cube.obj"));
    cube->setMaterial(iris::PbrMaterial::create());
    doc->getRootNode()->addChild(cube, false);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(2.0f, 1.5f, 4.0f));
    cam->lookAt(iris::Vec3(0.0f, 0.0f, 0.0f));
    doc->getRootNode()->addChild(cam, false);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    auto frame = [&] {
        mirror.sync(mirrorframe::world(view, cam));
        engine->renderOneFrame();
    };
    for (int i = 0; i < 10; ++i) frame();   // the open: everything applies once, and settles

    // 1. a still document: one submit and one drain a frame, every drain empty of changes
    const FrameInputStatus s0 = engine->frameInputStatus();
    const FrameSubmitStatus u0 = engine->frameSubmitStatus();
    unsigned appliedWhileStill = 0;
    std::vector<double> drainUs;
    for (int i = 0; i < 20; ++i) {
        frame();
        appliedWhileStill += engine->frameInputStatus().fieldsApplied;
        drainUs.push_back(engine->frameInputStatus().drainMicros);
    }
    std::sort(drainUs.begin(), drainUs.end());
    std::printf("    the drain's own cost, a still document's record: median %.2f us, max %.2f us over 20 frames\n",
                drainUs[drainUs.size() / 2], drainUs.back());
    const FrameInputStatus s1 = engine->frameInputStatus();
    const FrameSubmitStatus u1 = engine->frameSubmitStatus();
    CHECK(u1.submitted - u0.submitted == 20 && s1.drained - s0.drained == 20 && s1.drainedEmpty == s0.drainedEmpty,
          "20 still frames: 20 submits, 20 drains, none empty (%llu / %llu / %llu)", u1.submitted - u0.submitted,
          s1.drained - s0.drained, s1.drainedEmpty - s0.drainedEmpty);
    CHECK(appliedWhileStill == 0, "a still document's records apply nothing (%u fields over 20 drains)",
          appliedWhileStill);

    // 2. the fog on: one field at the next drain, none at the one after
    doc->fogEnabled = true;
    frame();
    const FrameInputStatus f1 = engine->frameInputStatus();
    frame();
    const FrameInputStatus f2 = engine->frameInputStatus();
    CHECK(f1.fieldsApplied == 1 && f1.viewFieldsApplied == 0,
          "the World fog on: the next drain applies ONE scene field (%u, %u of them per view)", f1.fieldsApplied,
          f1.viewFieldsApplied);
    CHECK(f2.fieldsApplied == 0, "...and the one after it nothing (%u)", f2.fieldsApplied);

    // 3. the camera moved: the per-view part only
    cam->setLocalPos(iris::Vec3(2.5f, 1.5f, 4.0f));
    frame();
    const FrameInputStatus c1 = engine->frameInputStatus();
    CHECK(c1.fieldsApplied == 1 && c1.viewFieldsApplied == 1,
          "a camera move: the per-view part only (%u applied, %u of them per view)", c1.fieldsApplied,
          c1.viewFieldsApplied);

    mirror.setSource(iris::ScenePtr());
    view->setScene(nullptr);
    engine->destroyScene(target);
    std::printf("mirror.frame_input: %s\n", gFailures ? "FAILED" : "PASSED");
    return gFailures ? 1 : 0;
}
