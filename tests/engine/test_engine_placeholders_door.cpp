// engine.placeholders_door — THE TEST DOOR Engine::setAsyncShaderPlaceholdersOnly REACHES THE COMPILER FROM ANY CALL ORDER
// (SHADER-SYSTEM-1 7.0, the landing's red gi.card_build_late.pso; the lead's ruling 2026-11-23). The setter was DROPPED in
// silence when called before the first view (OgreEngine::setAsyncShaderPlaceholdersOnly's `!mHlmsRegistered` return, since
// irisgl 9886bd51): no Hlms and no compiler exist until a view (or prepareDevice) makes them, so gi.card_build_late.pso -
// which sets the door before its room's view - never had its door on, and its rows read a transient. Now the engine keeps the
// wish and startAsyncShaders applies it.
// THE LAWS (read from AsyncShaderStats::placeholdersOnly: the flag as the COMPILER holds it, never the engine's wish):
//  (0) the service runs (AsyncShaderStats::running): else the compiler's flag means nothing;
//  (1) the setter called BEFORE ANY VIEW, then a view and one frame: the compiler's flag reads ON (RED before the fix: OFF);
//  (2) the setter(false), one frame: OFF;
//  (3) the CONTROL, the order that always worked: the setter(true) with the view existing, one frame: ON (the published
//      field reads the compiler at all - else (1)'s red would be the field's, not the door's).
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Published.h"
#include "jahshaka/engine/Shaders.h"
#include "../support/enginetesthelpers.h"
#include <cstdio>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

int main()
{
    std::printf("== engine.placeholders_door: the placeholders-only door reaches the compiler from any call order\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-engine-placeholders-door-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->setAsyncShaderPlaceholdersOnly(true);   // BEFORE ANY VIEW: no Hlms, no compiler yet
    View *view = e->createOffscreenView("door", 32, 32, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("door");
    if (!view || !scene) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    enginetest::frames(e, 1);
    const AsyncShaderStats s1 = e->published().shaders;
    CHECK(s1.running, "(0) the background compile service runs (%u thread(s))", s1.threads);
    CHECK(s1.placeholdersOnly, "(1) set before any view, the compiler holds placeholders-only after a view and a frame");
    e->setAsyncShaderPlaceholdersOnly(false);
    enginetest::frames(e, 1);
    CHECK(!e->published().shaders.placeholdersOnly, "(2) the setter(false): the compiler's door is off after a frame");
    e->setAsyncShaderPlaceholdersOnly(true);
    enginetest::frames(e, 1);
    CHECK(e->published().shaders.placeholdersOnly,
          "(3) CONTROL: set with the view existing, the compiler's door reads on (the field reads the compiler)");
    e->setAsyncShaderPlaceholdersOnly(false);
    enginetest::frames(e, 1);
    if (failures) std::printf("engine.placeholders_door: FAIL (%d)\n", failures);
    else std::printf("engine.placeholders_door: PASS\n");
    return failures ? 1 : 0;
}
