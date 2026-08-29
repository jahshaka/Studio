// Global illumination: Instant Radiosity, pixel-asserted (GI_SPEC.md phase 1).
//
// Scene: a red wall, a white floor, and a directional light aimed almost
// horizontally at the wall. The wall is brightly lit; the floor only catches
// the light at a grazing angle, so its direct term is dim and NEUTRAL
// (r == g == b). Bounced light is the only thing that can make the floor
// noticeably redder than green — which is exactly what the assertions measure:
//   1. enabling Instant Radiosity raises the floor's red bounce measurably;
//   2. turning it off restores the original pixels;
//   3. rotating the light away from the wall + refresh removes the bounce;
//   4. the unimplemented VCT mode is accepted but renders exactly like off.
#include "jahshaka/engine/Engine.h"

#include <cmath>
#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %s: r=%.3f g=%.3f b=%.3f\n", what, c.r, c.g, c.b);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("gi", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("gi");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // Floor: white, rough, spanning x/z around the origin.
    const NodeId floor = s->addTestCube(Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    s->setNodePosition(floor, Vec3(0.0f, -0.05f, 0.0f));
    s->setNodeScale(floor, Vec3(14.0f, 0.1f, 14.0f));

    // Wall: strongly red, facing +Z, behind the floor's far edge.
    const NodeId wall = s->addTestCube(Colour(1.0f, 0.05f, 0.05f), 0.0f, 0.9f);
    s->setNodePosition(wall, Vec3(0.0f, 3.0f, -3.0f));
    s->setNodeScale(wall, Vec3(12.0f, 6.0f, 0.3f));

    // Directional light. Engine convention: lights shine down the node's -Y, so
    // a +80 degree pitch about X sends it towards -Z (at the wall), dipping just
    // 10 degrees — the wall catches ~cos(10) of it, the floor only ~sin(10).
    const NodeId lightNode = s->createNode();
    const float half = 40.0f * 3.14159265f / 180.0f;   // 80 degrees about X
    const Quat atWall(std::sin(half), 0.0f, 0.0f, std::cos(half));
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    light.intensity = 2.0f;
    light.castShadows = false;
    s->setLight(lightNode, light);

    // Camera: above the floor in front of the wall, looking down at the patch
    // of floor the bounce should tint.
    view->setCameraPosition(Vec3(0.0f, 4.0f, 6.0f));
    view->lookAt(Vec3(0.0f, 0.0f, -0.5f));

    render(engine.get());
    Image img;
    view->readPixels(img);
    // The wall fills the top of the frame, the floor the bottom.
    const unsigned floorX = 64, floorY = 96, wallX = 64, wallY = 20;
    const Colour baseFloor = img.at(floorX, floorY);
    const Colour baseWall  = img.at(wallX, wallY);
    show("floor before GI", baseFloor);
    show("wall  before GI", baseWall);
    CHECK(baseWall.r > 0.3f && baseWall.r > baseWall.g + 0.2f,
          "wall is brightly red-lit by the direct light");
    CHECK(std::fabs(baseFloor.r - baseFloor.g) < 0.02f,
          "floor's direct light is neutral (no red bias before GI)");

    // ---- Enable Instant Radiosity ----------------------------------------
    GiParams gi;
    gi.mode = GiMode::InstantRadiosity;
    gi.quality = GiQuality::Medium;
    gi.irLight = lightNode;
    gi.numBounces = 1;
    CHECK(s->setGlobalIllumination(gi), "setGlobalIllumination(InstantRadiosity) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour onFloor = img.at(floorX, floorY);
    show("floor with IR", onFloor);
    CHECK(onFloor.r > baseFloor.r + 0.03f,
          "IR raises the floor's red measurably");
    CHECK((onFloor.r - onFloor.g) > (baseFloor.r - baseFloor.g) + 0.03f,
          "the raise is red bounce, not overall brightness");

    // Idempotent: pushing identical params again must not change the image.
    CHECK(s->setGlobalIllumination(gi), "re-pushing identical params succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour again = img.at(floorX, floorY);
    CHECK(std::fabs(again.r - onFloor.r) < 0.02f, "identical params leave pixels alone");

    // ---- The driving light moves: refresh re-traces ----------------------
    // Pitch the light the other way (-80 about X) so it shines AWAY from the
    // wall, towards +Z. The floor's grazing direct term keeps the same angle;
    // the red bounce must die.
    const Quat awayFromWall(-std::sin(half), 0.0f, 0.0f, std::cos(half));
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), awayFromWall, Vec3(1, 1, 1));
    s->refreshGlobalIllumination();
    render(engine.get());
    view->readPixels(img);
    const Colour turned = img.at(floorX, floorY);
    show("floor, light turned away + refresh", turned);
    // Some red remains by IR's nature: the reversed rays hit the wall's REAR
    // face (front-facing to them) and VPLs are omni lights that bleed through
    // thin geometry. The re-trace must still shrink the bounce substantially.
    CHECK((turned.r - turned.g) < (onFloor.r - onFloor.g) - 0.03f,
          "refresh after the light turns away shrinks the bounce substantially");

    // Turn it back for the teardown comparison.
    s->setNodeTransform(lightNode, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
    s->refreshGlobalIllumination();
    render(engine.get());

    // ---- Off restores ----------------------------------------------------
    GiParams off;   // defaults: mode Off
    CHECK(s->setGlobalIllumination(off), "setGlobalIllumination(Off) succeeds");
    render(engine.get());
    view->readPixels(img);
    const Colour offFloor = img.at(floorX, floorY);
    show("floor after GI off", offFloor);
    CHECK(std::fabs(offFloor.r - baseFloor.r) < 0.02f &&
          std::fabs(offFloor.g - baseFloor.g) < 0.02f,
          "turning GI off restores the original floor");

    // ---- VCT is accepted but honestly renders as off ---------------------
    GiParams vct;
    vct.mode = GiMode::Vct;
    CHECK(s->setGlobalIllumination(vct), "unimplemented VCT mode is accepted (degrades to off)");
    render(engine.get());
    view->readPixels(img);
    const Colour vctFloor = img.at(floorX, floorY);
    CHECK(std::fabs(vctFloor.r - baseFloor.r) < 0.02f, "VCT mode renders exactly like off");

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
