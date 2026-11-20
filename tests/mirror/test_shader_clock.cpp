// mirror.shader_clock_steps — THE MIRROR's CLOCKS COUNT GRID STEPS, NEVER SYNC CALLS (ONE-FRAME-1 stage 0, V2 plan 2.1).
//
// THE LAW: every consumer of the document's time advances by the STEPS the document's SimulationClock took, ONCE —
// shader seconds = (steps counted while the shader clock is live) x 1/60, independent of how many times the host
// syncs; a sync with no new step advances 0. Focus smoothing eases by the same step time.
// THE DEFECT IT GUARDS (ground read spikes/one-frame-v2/GROUND.md § 2.1, measured red on the base 273b8e638 / irisgl
// 4576b132): the mirror added SimulationClock::frameSeconds() — what the LAST advance() bought — on EVERY sync(), so
// a sync with no advance re-added the last tick's steps: an editor screenshot syncs twice (enginesceneviewport.cpp
// takePicture and its helper restore), and every preview, thumbnail and player sync advanced the scene's animated
// materials on its own count. On the base: 30 steps synced once each plus 3 bare syncs read 33 steps (+1 a bare sync).
//
//  1. THE CURSOR (iris::StepCursor, a pure function of the clock's lifetime count): N takes over K steps = K; a take
//     with no step = 0; a play edge (SimulationClock::reset) neither adds nor loses a step.
//  2. THE SHADER CLOCK through a real SceneMirror on the NULL engine (a scrolling map makes it live: TORNADO-1): K steps
//     with one sync each, then bare syncs, advance exactly K/60 s; a frame of two steps synced twice advances 2/60 s.
//  3. FOCUS SMOOTHING: a tracking camera with smoothFocus eases once per step-bearing sync; a bare sync leaves its
//     focus distance where it was (the base eased it again).
//  4. A NEW SOURCE: the cursor re-primes on the incoming document's clock (its past steps are not the mirror's) and
//     the shader clock restarts at 0 ("went live" is per source, the lead's ruling): the same document opened twice
//     reads the same shader seconds after the same steps. RED on the base: the clock carried the session's history.
// NULL render system, offscreen platform: no display, no card.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/simulationclock.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/documentgraph.h"
#include "../support/testmesh.h"

static int gFailures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++gFailures;
}

static constexpr double kStep = iris::SimulationClock::kStepSeconds;

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    enginetest::DocumentGraph graph("test_shader_clock-ogre.log");   // FIRST: destroyed last
    QGuiApplication app(argc, argv);
    if (!graph.require()) return 1;

    // ---- 1. THE CURSOR (pure) ----
    {
        iris::SimulationClock clock;
        clock.advance(5.0 * kStep);           // the past: not the cursor's
        iris::StepCursor cursor;
        cursor.prime(clock);
        check(cursor.take(clock) == 0, "a take right after the prime hands out 0 steps");
        uint64_t total = 0;
        const int k = 17;
        for (int i = 0; i < k; ++i) {
            clock.advance(kStep);
            total += cursor.take(clock);
            total += cursor.take(clock);      // a second read of the same frame
        }
        std::printf("      %d steps, two takes each: %llu steps handed out\n", k, (unsigned long long)total);
        check(total == uint64_t(k), "N takes over K steps hand out K steps, each once");
        clock.advance(3.0 * kStep);
        clock.reset();                        // a play edge between two takes
        clock.advance(2.0 * kStep);
        const uint64_t edge = cursor.take(clock);
        std::printf("      3 steps, a reset, 2 steps: the next take hands out %llu\n", (unsigned long long)edge);
        check(edge == 5, "a play edge (reset) neither adds nor loses a step");
        clock.advance(0.25 * kStep);          // a 240 Hz frame: no whole step
        check(cursor.take(clock) == 0, "a frame that buys no step hands out 0");
    }

    // ---- 2. THE SHADER CLOCK through the mirror, on the NULL engine ----
    jahshaka::engine::Scene *target = graph.engine()->createScene("clock");
    check(target != nullptr, "a NULL-engine scene");
    if (!target) return 1;
    auto doc = iris::Scene::create();
    auto cube = iris::MeshNode::create();
    cube->setName("cube");
    cube->setMesh(testmesh::load(":assets/models/cube.obj"));
    auto scrolling = iris::PbrMaterial::create();
    scrolling->textureVelocityU = 0.5f;   // a scrolling map reads the shader clock with no piece (TORNADO-1)
    cube->setMaterial(scrolling);
    doc->getRootNode()->addChild(cube);
    // the focus half's camera: looking down -Z at a target 10 m away, eased (a focus puller's hand)
    auto focusTarget = iris::SceneNode::create();
    focusTarget->setName("subject");
    focusTarget->setLocalPos(iris::Vec3(0.0f, 0.0f, -10.0f));
    doc->getRootNode()->addChild(focusTarget);
    auto camera = iris::CameraNode::create();
    camera->setName("puller");
    camera->focusMode = iris::CameraFocusMode::Track;
    camera->focusTarget = focusTarget->getGUID();
    camera->smoothFocus = true;
    camera->focusSmoothingSpeed = 2.0f;
    camera->focusOffset = 0.0f;
    camera->minFocusDistance = 0.1f;
    doc->getRootNode()->addChild(camera);

    {
        SceneMirror mirror(target);
        mirror.setSource(doc);
        mirror.sync();
        // THE UNVERIFIED PREMISE (spec "The premise"): a NULL-engine mirror raises the shader clock's live flag.
        check(mirror.shaderClockLive(), "the NULL-engine mirror's shader clock is live (a scrolling map mirrored)");

        const double t0 = mirror.shaderSeconds();
        const int k = 30, bare = 3;
        for (int i = 0; i < k; ++i) { doc->advance(float(kStep)); mirror.sync(); }
        for (int i = 0; i < bare; ++i) mirror.sync();   // a screenshot's second sync, a thumbnail's, a preview's
        const double steps = (mirror.shaderSeconds() - t0) / kStep;
        std::printf("      %d steps synced once each + %d bare syncs: the shader clock advanced %.6f steps\n", k, bare, steps);
        check(std::fabs(steps - double(k)) < 1e-6, "K steps advance the shader clock K/60 s, whatever the sync count");

        const double t1 = mirror.shaderSeconds();
        doc->advance(float(2.0 * kStep));   // a 30 fps host's frame: two steps
        mirror.sync();
        mirror.sync();
        const double two = (mirror.shaderSeconds() - t1) / kStep;
        std::printf("      one frame of two steps, synced twice: %.6f steps\n", two);
        check(std::fabs(two - 2.0) < 1e-6, "a two-step frame synced twice advances 2/60 s (stepped, not sync-counted)");

        // ---- 3. FOCUS SMOOTHING's dt ----
        camera->focusDistance = 1.0f;   // the puller's hand at 1 m, the subject at 10 m
        const float before = camera->focusDistance;
        doc->advance(float(kStep));
        mirror.sync();
        const float eased = camera->focusDistance;
        const float expected = iris::lens::smoothTowards(before, 10.0f, 2.0f, float(kStep));
        std::printf("      focus: %.6f -> %.6f after one step (expected %.6f)\n", double(before), double(eased), double(expected));
        check(std::fabs(eased - expected) < 1e-5f, "a step-bearing sync eases the focus by one step's dt");
        mirror.sync();
        mirror.sync();
        std::printf("      focus after two bare syncs: %.6f\n", double(camera->focusDistance));
        check(camera->focusDistance == eased, "a bare sync leaves the eased focus distance where it was");
    }

    // ---- 4. A NEW SOURCE: the cursor re-primes on ITS clock, and the shader clock restarts at 0 (per document) ----
    auto scrollingDoc = [&](const char *name) {
        auto d = iris::Scene::create();
        auto n = iris::MeshNode::create();
        n->setName(name);
        n->setMesh(testmesh::load(":assets/models/cube.obj"));
        auto m = iris::PbrMaterial::create();
        m->textureVelocityU = 0.5f;
        n->setMaterial(m);
        d->getRootNode()->addChild(n);
        return d;
    };
    {
        SceneMirror mirror(target);
        auto docA = scrollingDoc("a");
        mirror.setSource(docA);
        mirror.sync();
        for (int i = 0; i < 40; ++i) docA->advance(float(kStep));
        mirror.sync();
        auto docB = scrollingDoc("b");
        for (int i = 0; i < 5; ++i) docB->advance(float(kStep));   // B's past: 5 steps against A's 40
        mirror.setSource(docB);
        mirror.sync();
        const double atBind = mirror.shaderSeconds();
        std::printf("      after A's 40 steps, binding B (5 steps of its own): the shader clock reads %.6f steps\n", atBind / kStep);
        check(atBind == 0.0, "a new source restarts the shader clock at 0, and B's past steps are not the mirror's");
        docB->advance(float(kStep));
        mirror.sync();
        const double one = mirror.shaderSeconds() / kStep;
        std::printf("      one step of B: %.6f steps\n", one);
        check(std::fabs(one - 1.0) < 1e-9, "B's next step advances the shader clock 1/60 s");
    }
    {
        // THE SAME DOCUMENT OPENED TWICE reads the same shader seconds after the same steps (no session history)
        double t[2] = { 0.0, 0.0 };
        auto doc2 = scrollingDoc("twice");
        for (int run = 0; run < 2; ++run) {
            SceneMirror mirror(target);
            if (run == 1) {   // the second open follows unrelated history on the same mirror
                auto other = scrollingDoc("other");
                mirror.setSource(other);
                mirror.sync();
                for (int i = 0; i < 25; ++i) { other->advance(float(kStep)); mirror.sync(); }
            }
            mirror.setSource(doc2);
            mirror.sync();
            for (int i = 0; i < 12; ++i) { doc2->advance(float(kStep)); mirror.sync(); }
            t[run] = mirror.shaderSeconds();
        }
        std::printf("      the same document opened twice, 12 steps each: %.6f / %.6f s\n", t[0], t[1]);
        check(t[0] == t[1] && std::fabs(t[0] - 12.0 * kStep) < 1e-9,
              "the same document opened twice reads equal shader seconds after equal steps");
    }

    std::printf("%s\n", gFailures == 0 ? "mirror.shader_clock_steps: all checks passed" : "mirror.shader_clock_steps: FAILURES");
    return gFailures == 0 ? 0 : 1;
}
