// MEASURING ARM (SCRIPT-IN-ENGINE-1, the lead's last item (b)): MANY CALLS PER FRAME in C++ - measure_calls.js's scene
// and calls through the public API directly (Scene::setNodeTransform), the calls' own time and the frames'. Never
// registered as a row; reverted with the arm.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace jahshaka::engine;

int main()
{
    const int N = 200, F = 300;
    std::string err;
    auto e = Engine::create(enginetest::testEngineConfig("measure_calls-ogre.log"), err);
    if (!e) { std::printf("no engine: %s\nmeasure_calls: FAILED (1)\n", err.c_str()); return 1; }
    View *v = e->createOffscreenView("calls", 256, 144, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *s = e->createScene("calls");
    v->setScene(s);
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.8f, 0.8f, 0.8f);
    const MaterialId mat = s->createPbrMaterial(p);
    std::vector<NodeId> nodes;
    for (int i = 0; i < N; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, cube, mat);
        nodes.push_back(n);
    }
    enginetest::testCameraLookAt(v, Vec3(0, 5, 20), Vec3(0, 0, 0));
    enginetest::frames(e, 2);
    using clk = std::chrono::steady_clock;
    double callsMs = 0, framesMs = 0;
    for (int f = 0; f < F; ++f) {
        const auto t0 = clk::now();
        for (int i = 0; i < N; ++i)
            s->setNodeTransform(nodes[size_t(i)], Vec3(float(i % 20 - 10), float(i / 20) * 0.5f, std::sin(f * 0.05f + i)),
                                Quat(0, 0, 0, 1), Vec3(1, 1, 1));
        const auto t1 = clk::now();
        enginetest::frames(e, 1);
        framesMs += std::chrono::duration<double, std::milli>(clk::now() - t1).count();
        callsMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    std::printf("MEASURE calls cpp us_per_call=%.3f calls_ms=%.1f frames_ms=%.1f\n", callsMs * 1000.0 / (N * F), callsMs,
                framesMs);
    std::printf("measure_calls: PASSED\n");
    return 0;
}
