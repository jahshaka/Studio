// engine.message_order — ONE ORDER ACROSS STREAMS (ENGINE-UPGRADE-U3 decision 46 (1); stage 3's FIFO law). With the
// host's calls as records (EngineConfig::hostRecords, the app's shape), a scene command and an engine verb posted
// in one record land in the order they were called, whatever stream each rides: a scene's shadow request
// (Scene::setShadowSettings — a scene message) then the engine's shadow resolution (Engine::setShadowResolution —
// an engine message) ends at the ENGINE's value; the other order ends at the scene's. Before this the engine's
// stream always drained first and the scene's won both ways. A law in values; 2 x 2 frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

int main()
{
    std::printf("== engine.message_order: an engine message posted after a scene message lands after it\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-message-order-ogre.log");
    cfg.hostRecords = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("order", 64, 64, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("order");
    CHECK(view && scene && view->setScene(scene), "a view on a scene");
    if (!view || !scene) return 1;
    enginetest::addDirectionalLight(scene, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    enginetest::frames(e, 2);
    ShadowDesc d;
    d.resolution = 1024;
    // SCENE, THEN ENGINE: the engine's value is the last word.
    scene->setShadowSettings(d);
    e->setShadowResolution(2048);
    enginetest::frames(e, 2);
    const unsigned a = e->published().shadow.resolution;
    CHECK(a == 2048, "a scene request then the engine's resolution: the engine's lands last (%u)", a);
    // ENGINE, THEN SCENE: the scene's value is the last word.
    e->setShadowResolution(4096);
    d.resolution = 512;
    scene->setShadowSettings(d);
    enginetest::frames(e, 2);
    const unsigned b = e->published().shadow.resolution;
    CHECK(b == 512, "the engine's resolution then a scene request: the scene's lands last (%u)", b);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "engine.message_order: FAILED\n" : "engine.message_order: PASS\n");
    return failures ? 1 : 0;
}
