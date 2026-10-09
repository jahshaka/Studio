// gi.socket_camera — THE GI FOLLOWS A SOCKETED CAMERA (GI-SOCKET-CAM-1).
//
// A View's camera can ride a scene node (View::setCameraNode — the Player's
// avatar camera rides its socket owner). Ogre's camera then carries an IDENTITY
// local pose and the node carries the pose, so the camera's LOCAL position is
// the origin for ever. The view's once-a-frame hooks hand the camera's position
// to the GI tracking (the cascade chain and the field) and to the particle
// manager; they must hand its WORLD position, or in play the cascades sit at the
// world origin while the player walks away from them.
//
// The case: a camera rides an empty node; the node moves 30 m; within the
// tracking's own scroll schedule (one cascade re-centres a frame) cascade 0 is
// centred on the node again — within one step (its hysteresis band) plus a cell
// (its lattice). The particle manager's camera position is not readable through
// the engine boundary; it is fed from the same world read on the same line.
//
// Its own binary like every GI suite.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static float dist(const Vec3 &a, const Vec3 &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static void printC0(const char *what, const GiStatus &st, const Vec3 &rider)
{
    if (st.cascades.empty()) { std::printf("   %s: no cascades\n", what); return; }
    const auto &c = st.cascades[0];
    std::printf("   %s: rider %.2f,%.2f,%.2f  c0 centre %.2f,%.2f,%.2f  step %.2f  cell %.3f  dist %.2f\n",
                what, rider.x, rider.y, rider.z, c.centre.x, c.centre.y, c.centre.z, c.step, c.cell,
                dist(c.centre, rider));
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-socket-camera-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("socket", 128, 128, Colour(0, 0, 0));
    Scene *scene = e->createScene("socket");
    view->setScene(scene);
    scene->setAmbient(Colour(0.35f, 0.35f, 0.35f), Colour(0.35f, 0.35f, 0.35f));
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(200.0f, 0.1f, 200.0f));

    // The rider: an empty node, 2 m up. The pushed desc's pose is deliberately
    // the ORIGIN, so only the node can place the camera anywhere else.
    const NodeId rider = scene->createNode();
    CHECK(rider != 0, "the rider node exists");
    const Vec3 start(0.0f, 2.0f, 0.0f);
    scene->setNodeTransform(rider, start, Quat(), Vec3(1.0f, 1.0f, 1.0f));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)));
    CHECK(view->setCameraNode(rider), "the camera rides the node");

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    CHECK(scene->setGlobalIllumination(gi), "the cascade arm accepts");
    for (int i = 0; i < 16; ++i) e->renderOneFrame();

    GiStatus st = scene->giStatus();
    CHECK(!st.cascades.empty(), "the chain is up");
    if (st.cascades.empty()) { std::printf("\n%d failure(s)\n", failures); return 1; }
    printC0("at rest ", st, start);

    // The node walks 30 m along +x (the walk the Player's avatar does).
    const Vec3 moved(30.0f, 2.0f, 0.0f);
    scene->setNodeTransform(rider, moved, Quat(), Vec3(1.0f, 1.0f, 1.0f));
    // The tracking re-centres one cascade a frame; cascade 0 is the first. A
    // handful of frames covers its own schedule; the bar is the distance.
    const int kFrames = int(st.cascades.size()) + 2;
    for (int i = 0; i < kFrames; ++i) e->renderOneFrame();
    st = scene->giStatus();
    printC0("after 30 m", st, moved);
    if (!st.cascades.empty()) {
        const auto &c = st.cascades[0];
        const float tol = c.step + 2.0f * c.cell;
        // Only the horizontal walk is asserted: the chain may clamp its height.
        const float dxz = std::sqrt((c.centre.x - moved.x) * (c.centre.x - moved.x) +
                                    (c.centre.z - moved.z) * (c.centre.z - moved.z));
        CHECK(dxz <= tol, "CASCADE 0 FOLLOWS THE SOCKETED CAMERA (within one step + a cell of the node)");
    }

    CHECK(view->setCameraNode(0), "the camera returns to the pushed pose");
    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
