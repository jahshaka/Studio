// frame.clock_integration — A FRAME CONSUMES THE STEPS OF THE SCENES IT DRAWS, AND ONLY THEIRS (ONE-FRAME-1 stage 1, the
// audit's bug 1), through the real engine: the drain, the frame's drawn set (OgreEngine::scenesFeedingEnabledViews) and
// each scene's clock (SceneClock.h, whose arithmetic the CPU rows prove).
// THE LAW: two scenes stamped in one hand-over; the frame draws only scene A (B's view disabled: a hidden editor while
// a thumbnail renders) -> A advances its steps, B advances 0 and keeps them; the first frame that draws B consumes them;
// 36,000 owed steps (ten minutes hidden) arrive as the bound's 8.
// RED ON irisgl bba6409b, by its code (this fixture is the stage's; the card is the feature end's): every frame consumed
// every scene's owed steps, drawn or not, unbounded — B's 3 steps vanished with the undrawn frame, the 36,000 arrived as
// 600 s. A card row: a frame draws a scene only through an enabled view (the NULL render system holds none).
#include <cstdio>

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/FrameInput.h"
#include "jahshaka/engine/EngineInspect.h"
#include "enginetesthelpers.h"

using namespace jahshaka::engine;

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

static void stamp(Engine &e, Scene *s, unsigned long long serial)
{
    FrameInput in;
    FrameInputScene part;
    part.scene = s;
    part.stepSerial = serial;
    part.stepPeriod = 1.0f / 60.0f;
    in.scenes.push_back(std::move(part));
    e.submitFrame(in);
}

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-frame-integration-ogre.log"), err);
    check(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *va = engine->createOffscreenView("a", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    View *vb = engine->createOffscreenView("b", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *a = engine->createScene("a");
    Scene *b = engine->createScene("b");
    check(va && vb && a && b, "two offscreen views, two scenes");
    if (!va || !vb || !a || !b) return 1;
    va->setScene(a);
    vb->setScene(b);
    const double period = double(1.0f / 60.0f);
    const auto dt = [](Scene *s) { return inspect::scene(s)->frameSceneSeconds(); };

    stamp(*engine, a, 100);
    stamp(*engine, b, 100);
    enginetest::frozenFrames(engine, 2);
    vb->setEnabled(false);   // the frame draws only A
    stamp(*engine, a, 103);
    stamp(*engine, b, 103);
    enginetest::frozenFrames(engine, 1);
    std::printf("      one hand-over, only A drawn: A %.6f s, B %.6f s\n", dt(a), dt(b));
    check(dt(a) == 3.0 * period, "the drawn scene advances its 3 steps");
    check(dt(b) == 0.0, "the undrawn scene advances 0 (its steps are not a thumbnail's frame's to eat)");
    vb->setEnabled(true);
    enginetest::frozenFrames(engine, 1);
    std::printf("      B drawn again: B %.6f s\n", dt(b));
    check(dt(b) == 3.0 * period, "the first frame that draws it consumes the 3 steps it kept");
    stamp(*engine, b, 103 + 36000);
    enginetest::frozenFrames(engine, 1);
    std::printf("      36,000 owed: B %.6f s\n", dt(b));
    check(dt(b) == double(FrameInputScene::kMaxStepsPerFrame) * period, "36,000 owed steps arrive as the bound's 8");

    va->setScene(nullptr);
    vb->setScene(nullptr);
    engine->destroyView(va);
    engine->destroyView(vb);
    engine->destroyScene(a);
    engine->destroyScene(b);
    std::printf("frame.clock_integration: %s\n", gFailures == 0 ? "all checks passed" : "FAILURES");
    return gFailures == 0 ? 0 : 1;
}
