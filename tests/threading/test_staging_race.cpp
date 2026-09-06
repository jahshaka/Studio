// threading.staging_race — the render loop must not walk a scene manager some
// other thread is writing (THREADING_ADOPTION_SPEC.md P3, gate G3-c).
//
// THE BUG THIS PINS. Until P3, `OgreEngine::renderOneFrame` ended in
// `Root::renderOneFrame()`, and upstream's body (OgreRoot.cpp:1101-1126)
// iterates EVERY SceneManager in the process and calls `updateSceneGraph()` on
// each — including the document's STAGING managers, which nothing ever draws.
// The asset-import worker builds its fragments in exactly those managers while
// the main thread renders. So the render thread sat inside
// `SceneManager::updateAllTransforms`, reading the SoA pools that the worker's
// `ArrayMemoryManager::createNewSlot` frees whenever it grows them
// (OgreArrayMemoryManager.cpp:167-215). A use-after-free, not a torn value.
//
// P3 closes it structurally: the frame updates only the scenes an ENABLED View
// draws, and a staging manager never feeds a View.
//
// TWO PARTS, because the hazard has two faces:
//
//   PART 1 — THE GATE, deterministic. Two engine scenes, one drawn by an
//   enabled offscreen view and one drawn by nobody, plus the staging managers.
//   `app.engineObjects().updatedScenes` (ObjectCounts::updatedScenes) must
//   report exactly the drawn ones, and must follow setEnabled both ways — the
//   over-gating check matters as much as the under-gating one, because a scene
//   whose workspace renders MUST have been updated in the same frame.
//
//   PART 2 — THE SOAK, the regression net. A worker thread builds and destroys
//   document subtrees in the staging manager (the graph's only off-main-thread
//   exposure — the shape the importer has, without dragging in assimp and the
//   import service) while the main thread renders 250 frames. Before P3 this is
//   the pre-fix build's crash window; after it the two threads touch disjoint
//   memory by construction.
//
//   HONESTY NOTE, and it belongs in the file rather than in a report: under
//   ThreadSanitizer this suite proves less than it looks like it does. TSan
//   only sees accesses it instrumented, and Ogre-Next is a linked shared
//   library we do not compile (scripts/tsan.supp says so at the top). The
//   racing accesses in the pre-fix build were BOTH inside Ogre. So Part 2 is a
//   crash/corruption soak that TSan makes slower and more interleaved, not a
//   TSan-detectable race; Part 1 is the assertion that actually fails if the
//   gate regresses.
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <QGuiApplication>

#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
                              else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// One importer-shaped fragment: a root, `kFanout` children, and a grandchild
/// under each — three depth levels, which is what makes the staging manager's
/// per-depth-level node pools grow and defragment rather than sit still.
static const int kFanout = 24;

static void buildAndDropFragment()
{
    auto root = iris::SceneNode::create();
    std::vector<iris::SceneNodePtr> kids;
    kids.reserve(size_t(kFanout));
    for (int i = 0; i < kFanout; ++i) {
        auto child = iris::SceneNode::create();
        auto grand = iris::SceneNode::create();
        child->addChild(grand);
        root->addChild(child);
        // Transform writes go to the node's own SoA slot — the other half of
        // what the render thread used to read out from under this thread.
        child->setLocalPos(iris::Vec3(float(i), 0.0f, 0.0f));
        grand->setLocalPos(iris::Vec3(0.0f, float(i), 0.0f));
        kids.push_back(child);
    }
    // Everything dies here: destroyNode, deepest first, freeing the slots the
    // next fragment's createNewSlot will recycle (and, past the high-water
    // mark, reallocate).
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_staging_race-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // The host's staging manager, exactly as EngineHost wires it. Without this
    // the document falls back to making its own — same hazard, but this is the
    // configuration the app ships.
    void *docScene = engine->documentGraphScene();
    CHECK(docScene != nullptr, "document staging scene manager");
    iris::graph::setStagingScene(reinterpret_cast<iris::graph::SceneHandle>(docScene));

    Scene *drawn = engine->createScene("drawn");
    Scene *hidden = engine->createScene("hidden");
    View *view = engine->createOffscreenView("drawn-view", 96, 96, Colour(0, 0, 0.5f));
    CHECK(drawn && hidden && view, "two scenes + one offscreen view");
    if (!drawn || !hidden || !view) return 1;
    view->setScene(drawn);
    drawn->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    hidden->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::testCameraLookAt(view, Vec3(2.2f, 1.8f, 2.6f), Vec3(0, 0, 0));
    enginetest::addTestCube(drawn, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.5f);
    enginetest::addTestCube(hidden, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.5f);

    // ---- PART 1: the gate ---------------------------------------------------
    ObjectCounts counts;
    engine->renderOneFrame();
    CHECK(engine->objectCounts(counts), "objectCounts read");
    std::printf("    scenes=%u updatedScenes=%u views=%u enabledViews=%u\n",
                counts.scenes, counts.updatedScenes, counts.views, counts.enabledViews);
    CHECK(counts.scenes == 2, "two engine scenes exist");
    CHECK(counts.updatedScenes == 1,
          "the frame updated ONLY the scene an enabled view draws");

    View *second = engine->createOffscreenView("hidden-view", 64, 64, Colour(0, 0, 0));
    CHECK(second != nullptr, "second offscreen view");
    if (second) {
        second->setScene(hidden);
        enginetest::testCameraLookAt(second, Vec3(2.2f, 1.8f, 2.6f), Vec3(0, 0, 0));
        engine->renderOneFrame();
        engine->objectCounts(counts);
        CHECK(counts.updatedScenes == 2,
              "a scene gains its update the moment an enabled view draws it (no over-gating)");
        second->setEnabled(false);
        engine->renderOneFrame();
        engine->objectCounts(counts);
        CHECK(counts.updatedScenes == 1, "disabling the view takes the scene back out");
        second->setEnabled(true);
        engine->renderOneFrame();
        engine->objectCounts(counts);
        CHECK(counts.updatedScenes == 2, "and re-enabling puts it back");
        engine->destroyView(second);
    }
    engine->renderOneFrame();
    engine->objectCounts(counts);
    CHECK(counts.updatedScenes == 1, "destroying the view takes the scene back out");

    // ---- PART 2: the soak ---------------------------------------------------
    // 250 rendered frames against a worker doing the importer's graph work in
    // the staging manager. The assertion is negative — no crash, no engine
    // error — which is what a use-after-free regression test looks like.
    std::atomic<bool> stop{false};
    std::atomic<long long> fragments{0};
    std::thread worker([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            buildAndDropFragment();
            fragments.fetch_add(1, std::memory_order_relaxed);
        }
    });

    const int kFrames = 250;
    for (int i = 0; i < kFrames; ++i) engine->renderOneFrame();
    stop.store(true, std::memory_order_relaxed);
    worker.join();

    std::printf("    soak: %d frames against %lld staging fragments (%d nodes each)\n",
                kFrames, fragments.load(), kFanout * 2 + 1);
    CHECK(fragments.load() > 0, "the worker really ran beside the render loop");
    CHECK(engine->lastError().empty(), "no engine error was raised during the soak");

    engine->objectCounts(counts);
    CHECK(counts.updatedScenes == 1, "the gate still holds after the soak");

    iris::graph::setStagingScene(nullptr);
    engine.reset();
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
