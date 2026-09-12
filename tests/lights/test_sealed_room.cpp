// lights.sealed_room — THE OWNER'S CASE, in pixels
// (SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md §0, owner decision 1).
//
// A lamp above the roof of a sealed room lit the floor INSIDE it, through the
// roof, because the light that every new scene ships with was explicitly told
// not to cast shadows — and nothing in the editor ever said so. Two lines fixed
// the default (the new scene's point light and the file reader's fallback);
// this is the measurement that says what those lines are worth.
//
// THREE CASES, one rig, one probe (the floor's centre, seen from below the
// roof):
//   1. a lamp born the ordinary way — the document's default, soft shadows —
//      leaves the sealed floor DARK. The roof stops it, which is what a roof is;
//   2. the SAME lamp with its shadows switched off lights that floor brightly,
//      straight through the roof. This is not a bug, it is what "Off (fill
//      light)" means, and it is why the switch is named for its purpose;
//   3. the control: with the roof removed the shadowed lamp lights the floor
//      too, so case 1 is the ROOF blocking it and not the lamp failing.
//
// Headless offscreen view; same runtime requirements as the other engine
// suites (a reachable DISPLAY, a Vulkan driver or lavapipe).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

struct Room {
    Scene *scene = nullptr;
    NodeId floorNode = 0, roof = 0, lamp = 0;
};

/// A sealed room: a floor at y=0, a roof at y=3, and a lamp ABOVE the roof.
/// Nothing else emits, and the ambient is zero, so every photon on the floor
/// came through (or around) the roof.
Room buildRoom(Engine *engine, View *view)
{
    Room r;
    r.scene = engine->createScene("sealed_room");
    view->setScene(r.scene);
    r.scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const MeshId mesh = r.scene->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.9f, 0.9f, 0.9f);
    p.roughness = 0.9f;
    const MaterialId mat = r.scene->createPbrMaterial(p);

    r.floorNode = r.scene->createNode();
    r.scene->attachMesh(r.floorNode, mesh, mat);
    r.scene->setNodeTransform(r.floorNode, Vec3(0, -0.05f, 0), Quat(), Vec3(8, 0.1f, 8));

    // The roof: wide enough that the lamp cannot simply shine round it.
    r.roof = r.scene->createNode();
    r.scene->attachMesh(r.roof, mesh, mat);
    r.scene->setNodeTransform(r.roof, Vec3(0, 3.0f, 0), Quat(), Vec3(8, 0.2f, 8));

    // The lamp, ABOVE the roof, reaching well past the floor.
    r.lamp = r.scene->createNode();
    r.scene->setNodeTransform(r.lamp, Vec3(0, 5.0f, 0), Quat(), Vec3(1, 1, 1));
    return r;
}

/// The lamp, described the way the DOCUMENT would: `castShadows` is what
/// SceneMirror derives from the light's Shadow Type, and the document's default
/// is Soft — i.e. true.
void setLamp(Room &r, bool castShadows)
{
    LightDesc d;
    d.type = LightType::Point;
    d.intensity = 12.0f;
    d.range = 20.0f;
    d.castShadows = castShadows;
    r.scene->setLight(r.lamp, d);
}

/// How bright the middle of the floor is, seen from INSIDE the room (a camera
/// under the roof, looking down).
int floorBrightness(Engine *engine, View *view)
{
    CameraDesc c;
    c.position = Vec3(0, 2.0f, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);   // look down -Y
    c.fovDegrees = 50;
    view->setCamera(c);
    for (int i = 0; i < 6; ++i) engine->renderOneFrame();
    Image img;
    if (!view->readPixels(img)) return -1;
    const Colour q = img.at(img.width / 2, img.height / 2);
    return int(std::lround((q.r + q.g + q.b) / 3.0f * 255.0f));
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-sealed-room-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("sealed", 128, 128, Colour(0, 0, 0));
    Room room = buildRoom(engine.get(), view);
    view->setShadows(true);

    // ---- 1. the default: shadows ON --------------------------------------
    setLamp(room, true);
    const int sealed = floorBrightness(engine.get(), view);
    std::printf("-- lamp above the roof, shadows ON (the default): floor %d\n", sealed);
    CHECK(sealed < 40, "a SEALED ROOM's floor is not lit by a shadowed lamp above its roof");

    // ---- 2. the author switches the lamp's shadows off -------------------
    setLamp(room, false);
    const int leaking = floorBrightness(engine.get(), view);
    std::printf("-- the same lamp, shadows OFF (fill light):     floor %d\n", leaking);
    CHECK(leaking > 150, "...and IS lit, through the roof, when the author turns them off");
    CHECK(leaking - sealed > 100, "...which is the whole difference the default makes");

    // ---- 3. the control: no roof -----------------------------------------
    setLamp(room, true);
    room.scene->setNodeVisible(room.roof, false);
    const int noRoof = floorBrightness(engine.get(), view);
    std::printf("-- shadows ON, roof removed:                    floor %d\n", noRoof);
    CHECK(noRoof > 150,
          "the control: with no roof the shadowed lamp lights the floor (it was the ROOF)");

    view->setShadows(false);
    view->setScene(nullptr);
    engine->destroyScene(room.scene);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
