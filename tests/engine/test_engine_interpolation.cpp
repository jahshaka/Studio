// frame.interpolation — THE OWNER'S MODE DRAWS BETWEEN THE GRID STATES (ENGINE-UPGRADE-U3 stage 5).
// A document node moves one unit per 60 Hz step; the render thread draws at a 90 Hz display period (FREE-RUNNING
// frames, inspect::EngineEcho::presentationFrame — the frame it draws on its own, on request so the count is exact):
// three frames per two steps, records arriving after the second and third frame of each pair. The presentation
// clock renders one step behind and advances 2/3 of a step per frame, so the drawn x is the motion sampled at 90 Hz —
// a CLOSED FORM: frame j shows x = (2/3) j (within 1e-4), three distinct poses per two steps. A LOCKSTEP frame (every
// test, every hash) draws the newest grid state exactly. Two threads (EngineConfig::renderThread); 30 frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

static FrameInput record(Scene *scene, NodeId node, unsigned long long step, float x, float frameDelta = 1.0f / 60.0f)
{
    FrameInput in;
    in.frameDelta = frameDelta;          // the PARTICLES' delta (simulated x particleTimeScale) — never the grid's
    in.stepSerial = step;
    in.stepPeriod = 1.0f / 60.0f;        // the grid's step (the host's SimulationClock::kStepSeconds; decision 55)
    FrameInputScene part;
    part.scene = scene;
    NodeRecord r;
    r.id = node;
    r.fields = NodeRecord::Trs;
    r.position = Vec3(x, 0.0f, 0.0f);
    r.rotation = Quat(0.0f, 0.0f, 0.0f, 1.0f);
    r.scale = Vec3(1.0f, 1.0f, 1.0f);
    part.nodes.push_back(r);
    part.changes.moved.push_back(node);
    in.scenes.push_back(part);
    return in;
}

// frame.interpolation_apply (`--apply`, a MEASURE row): the apply of 10,000 moved nodes per free frame, on the scene's
// pool against the frame's thread (engine.arm "interp.serial"), median of 30 frames each — printed, never a bar.
static int applyMeasure()
{
    EngineConfig cfg = enginetest::testEngineConfig("test-interpolation-apply-ogre.log");
    cfg.renderThread = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create (%s)\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    const inspect::EngineEcho *echo = inspect::engine(e);
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("interp-apply", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("interp-apply", 8u);
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    const int N = 10000;
    std::vector<NodeId> nodes;
    for (int i = 0; i < N; ++i) nodes.push_back(scene->createDocumentNode(reserveNodeId()));
    e->setDisplayPeriod(1.0f / 90.0f);
    unsigned long long step = 1;
    const auto submitAll = [&](float x) {
        FrameInput in = record(scene, nodes[0], step, x);
        for (int i = 1; i < N; ++i) {
            NodeRecord r = in.scenes[0].nodes[0];
            r.id = nodes[size_t(i)];
            in.scenes[0].nodes.push_back(r);
            in.scenes[0].changes.moved.push_back(r.id);
        }
        e->submitFrame(in);
    };
    submitAll(0.0f);
    enginetest::frames(e, 2);
    const auto arm = [&](double v) { e->request([&](Engine &x) { x.setArm("interp.serial", v); }); };
    double lastNaming = 0.0, lastFrame = 0.0;
    const auto measure = [&](const char *tag) {
        std::vector<double> us, names, frameUs;
        for (int f = 0; f < 30; ++f) {
            ++step;
            submitAll(float(step));
            const auto f0 = std::chrono::steady_clock::now();
            const_cast<inspect::EngineEcho *>(echo)->presentationFrame();
            frameUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - f0).count());
            unsigned n = 0; double m = 0.0, nm = 0.0;
            echo->lastInterpolation(scene, n, m, nm);
            if (n == unsigned(N)) { us.push_back(m); names.push_back(nm); }
        }
        std::sort(us.begin(), us.end());
        std::sort(names.begin(), names.end());
        const double med = us.empty() ? -1.0 : us[us.size() / 2];
        const double nmed = names.empty() ? -1.0 : names[names.size() / 2];
        std::printf("    the apply of %d nodes, %s: median %.1f us (+ %.1f us naming them to the scans) over %zu free "
                    "frames\n", N, tag, med, nmed, us.size());
        std::sort(frameUs.begin(), frameUs.end());
        lastFrame = frameUs.empty() ? -1.0 : frameUs[frameUs.size() / 2];
        std::printf("    the whole free frame (drain + apply + naming + draw), %s: median %.1f us\n", tag, lastFrame);
        lastNaming = nmed;
        return med;
    };
    arm(0.0);
    const double parallel = measure("on the scene's pool (8 workers + the frame's thread)");
    const double parallelFrame = lastFrame;
    arm(1.0);
    const double serial = measure("on the frame's thread (interp.serial)");
    std::printf("INTERP_APPLY nodes=%d parallel_us=%.1f serial_us=%.1f speedup=%.2f naming_us=%.1f frame_us=%.1f\n", N,
                parallel, serial, parallel > 0.0 ? serial / parallel : 0.0, lastNaming, parallelFrame);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    return parallel > 0.0 && serial > 0.0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--apply") return applyMeasure();
    std::printf("== frame.interpolation: a 60 Hz mover drawn at 90 Hz, three poses per two steps, a closed form\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-interpolation-ogre.log");
    cfg.renderThread = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created on its render thread (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    const inspect::EngineEcho *echo = inspect::engine(e);
    CHECK(echo != nullptr, "the engine's probes");
    if (!echo) return 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("interp", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("interp");
    CHECK(view && scene && view->setScene(scene), "a view on a scene");
    if (!view || !scene) return 1;
    const NodeId node = scene->createDocumentNode(reserveNodeId());
    e->setDisplayPeriod(1.0f / 90.0f);
    e->submitFrame(record(scene, node, 1, 0.0f));
    enginetest::frames(e, 3);   // LOCKSTEP: the newest grid state exactly
    const auto drawnX = [&]() {
        Vec3 p; Quat q;
        e->request([&](Engine &) { scene->nodeWorldPose(node, p, q); });
        return p.x;
    };
    CHECK(std::fabs(drawnX()) < 1e-6f, "a lockstep frame draws the newest grid state (x = 0)");

    // THE PATTERN: step s arrives (x = s - 1), then 2 or 1 free frames (3 per 2 steps at 90 / 60).
    std::vector<float> drawn;
    unsigned long long step = 1;
    for (int pair = 0; pair < 5; ++pair) {
        for (int k : { 2, 1 }) {
            ++step;
            e->submitFrame(record(scene, node, step, float(step - 1)));
            for (int f = 0; f < k; ++f) {
                const_cast<inspect::EngineEcho *>(echo)->presentationFrame();
                drawn.push_back(drawnX());
            }
        }
    }
    int closedForm = 0, distinctPerTwoSteps = 0;
    for (size_t j = 0; j < drawn.size(); ++j)
        if (std::fabs(drawn[j] - (2.0f / 3.0f) * float(j)) < 1e-4f) ++closedForm;
    for (size_t j = 0; j + 2 < drawn.size(); j += 3)
        if (drawn[j] != drawn[j + 1] && drawn[j + 1] != drawn[j + 2] && drawn[j] != drawn[j + 2]) ++distinctPerTwoSteps;
    std::printf("    drawn x:");
    for (float x : drawn) std::printf(" %.4f", double(x));
    std::printf("\n");
    CHECK(closedForm == int(drawn.size()),
          "every free frame draws x = (2/3) j — the motion sampled at the display period (%d of %zu)", closedForm,
          drawn.size());
    CHECK(distinctPerTwoSteps == int(drawn.size() / 3), "three distinct poses per two steps (%d of %zu pairs)",
          distinctPerTwoSteps, drawn.size() / 3);
    // ...and the next LOCKSTEP frame is the newest grid state again.
    enginetest::frames(e, 1);
    CHECK(std::fabs(drawnX() - float(step - 1)) < 1e-6f, "a lockstep frame after them draws the newest state exactly");

    // THE GRID'S STEP IS THE CLOCK'S, NEVER THE PARTICLES' DELTA (decision 55): the same pattern with every record's
    // frameDelta at a particle time scale of 2 and of 0.5 draws the same closed form — the weight is never pinned.
    const auto pattern = [&](float frameDelta, unsigned long long &s, float base, std::vector<float> &out) {
        for (int pair = 0; pair < 4; ++pair)
            for (int k : { 2, 1 }) {
                ++s;
                e->submitFrame(record(scene, node, s, float(s - 1), frameDelta));
                for (int f = 0; f < k; ++f) {
                    const_cast<inspect::EngineEcho *>(echo)->presentationFrame();
                    out.push_back(drawnX() - base);
                }
            }
    };
    for (float scale : { 2.0f, 0.5f }) {
        // A lockstep frame first: the clock stands at the newest step, the pattern starts one step behind it again.
        enginetest::frames(e, 1);
        const float base = drawnX();
        std::vector<float> d;
        pattern(scale / 60.0f, step, base, d);
        std::printf("    particle time scale %.1f, drawn x - x0:", double(scale));
        for (float x : d) std::printf(" %.4f", double(x));
        std::printf("\n");
        int monotone = 0;
        for (size_t j = 1; j < d.size(); ++j) if (d[j] > d[j - 1] + 1e-5f) ++monotone;
        float stepSize = d.size() > 3 ? d[3] - d[0] : 0.0f;
        CHECK(monotone == int(d.size()) - 1 && std::fabs(stepSize - 2.0f) < 1e-4f,
              "particle time scale %.1f: every free frame a new pose, two steps per three frames (%d of %zu rising, "
              "%.4f per three)", double(scale), monotone, d.size() - 1, double(stepSize));
    }
    // A TWO-STEP HITCH (the tick that carries two steps at once: stepSerial + 2, frameDelta 2/60): the drawn pose never
    // steps back to an older state and never jumps more than the record did — and the cadence after it is the closed
    // form's again (two steps per three frames).
    {
        enginetest::frames(e, 1);
        const float base = drawnX();
        std::vector<float> d;
        pattern(1.0f / 60.0f, step, base, d);
        step += 2;
        e->submitFrame(record(scene, node, step, float(step - 1), 2.0f / 60.0f));
        for (int f = 0; f < 2; ++f) { const_cast<inspect::EngineEcho *>(echo)->presentationFrame(); d.push_back(drawnX() - base); }
        const size_t after = d.size();
        pattern(1.0f / 60.0f, step, base, d);
        std::printf("    a two-step hitch, drawn x - x0:");
        for (float x : d) std::printf(" %.4f", double(x));
        std::printf("\n");
        int back = 0; float worst = 0.0f;
        for (size_t j = 1; j < d.size(); ++j) {
            if (d[j] < d[j - 1] - 1e-5f) ++back;
            worst = std::max(worst, d[j] - d[j - 1]);
        }
        const float cadence = d.size() > after + 4 ? d[after + 4] - d[after + 1] : 0.0f;
        CHECK(back == 0, "the hitch never draws an older pose after a newer one (%d steps back)", back);
        CHECK(worst <= 2.0f + 1e-4f, "no frame jumps further than the hitch's own two steps (worst %.4f)", double(worst));
        CHECK(std::fabs(cadence - 2.0f) < 1e-4f, "after the hitch, two steps per three frames again (%.4f)", double(cadence));
    }
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    enginetest::frames(e, 1);
    std::printf(failures ? "frame.interpolation: FAILED\n" : "frame.interpolation: PASS\n");
    return failures ? 1 : 0;
}
