// gi.pane_stack_cut — A CUT RAY ANSWERS AS OCCLUDED (GATHER-HANG-1 stage 2, the lead's ruling (B); jah_rq_alpha.glsl THE
// BOUND): a MIRROR floor (metal, albedo 1, roughness 0) under N = 4 of the same glass slabs, an EMISSIVE ceiling (E) above them, nothing
// else lit (no lamp, no sky, no ambient, the GI off), the reflection's rays alone (reflect.screenOff) and one frame of
// history (reflect.window 1): every floor pixel is the reflection ray's answer through the stack. Over the middle half:
//   (1) THE HOLE: the slabs hidden, the floor's mean R over a code (the ceiling reflected: R = F E);
//   (2) THE UNCUT STACK: the four slabs (eight candidate faces a ray), the mean is T^4 R within ONE CODE at its level plus
//       the readback's half code of R - the panes multiply, never block (no pane has its own light: nothing lights it);
//   (3) THE CUT: the test door rayquery.stepBound 2 (every reflection ray meets eight candidates, so every one is CUT):
//       (a) every texel of the picture FINITE by its bits, (b) the floor's every texel EXACTLY 0 (a cut reading ray
//       answers a BLACK hit at its end - no radiance, no sky, no pane light: a cut never adds light), (c) the counter
//       rayQuery.proceedStepsMax reads exactly 2 (the cut published).
// The cut arm of gi.pane_stack (test_gi_pane_stack.cpp: the same slabs, T = 0.9 as the pane word stores it), its own
// file because its view declares its route (CODING_RULES section 6) and the stack's two arms are still on the ratchet.
// Red on the base: the status field rayQuery.proceedStepsMax and the door rayquery.stepBound do not exist.
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, MaterialId mat)
{
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
// ONE CODE AT A LEVEL: the width of the display's 8-bit sRGB code linear radiance L falls in
double srgbEncode(double L) { return L <= 0.0031308 ? 12.92 * L : 1.055 * std::pow(L, 1.0 / 2.4) - 0.055; }
double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double oneCodeAt(double L)
{
    const double code = std::min(std::floor(srgbEncode(std::max(L, 0.0)) * 255.0), 254.0);
    return srgbDecode((code + 1.0) / 255.0) - srgbDecode(code / 255.0);
}
}   // namespace

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-pane-cut-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("pane_stack_cut", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("pane_stack_cut");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        fx.ssr = 2;   // the reflection's rays at full resolution: a texel a pixel
        view->setPostFx(fx);
    }
    PbrParams mirror;
    mirror.albedo = Colour(1.0f, 1.0f, 1.0f);
    mirror.metalness = 1.0f;
    mirror.roughness = 0.0f;
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.0f, 0.4f, 8.0f), scene->createPbrMaterial(mirror));
    PbrParams ceiling;
    ceiling.albedo = Colour(0.0f, 0.0f, 0.0f);
    ceiling.emissive = Colour(0.5f, 0.5f, 0.5f);
    ceiling.roughness = 1.0f;
    box(scene, Vec3(0.0f, 3.2f, 0.0f), Vec3(8.0f, 0.2f, 8.0f), scene->createPbrMaterial(ceiling));
    PbrParams glass;
    glass.albedo = Colour(0.3f, 0.3f, 0.3f);
    glass.roughness = 0.9f;
    glass.alphaMode = PbrAlphaMode::Glass;
    glass.alpha = 0.1f;
    const MaterialId glassMat = scene->createPbrMaterial(glass);
    constexpr int kCutSlabs = 4;
    std::vector<NodeId> slabs;
    for (int k = 0; k < kCutSlabs; ++k) {
        const NodeId n = box(scene, Vec3(0.0f, 0.6f + 0.15f * float(k), 0.0f), Vec3(4.0f, 0.01f, 4.0f), glassMat);
        if (!n) { std::printf("FAIL: slab %d\n", k); return 1; }
        slabs.push_back(n);
    }
    float sh[27] = { 0 };   // no sky, no ambient
    scene->setAmbientSh(sh);
    if (!enginetest::rayReflectionOnly(e)) { std::printf("FAIL: the arm reflect.screenOff\n"); return 1; }
    if (!e->setArm("reflect.window", 1.0)) { std::printf("FAIL: the arm reflect.window\n"); return 1; }
    enginetest::declareRoutes(view, scene, enginetest::Routes().reflection(), "gi.pane_stack_cut's view");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.45f, 0.25f), Vec3(0.0f, 0.0f, -0.05f));
    e->waitForAsyncShaders();
    const auto settle = [&]() { e->waitForAsyncShaders(); run(8); };
    // the floor's mean over the middle half (its brightest channel), and whether EVERY texel is finite and every middle one 0
    const auto read = [&](double &mean, bool &allFinite, bool &middleZero) {
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return false;
        double sum = 0.0;
        size_t n = 0;
        allFinite = middleZero = true;
        const auto finiteBits = [](float v) { uint32_t b; std::memcpy(&b, &v, 4); return (b & 0x7FFFFFFFu) < 0x7F800000u; };
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x) {
                const Colour c = img.at(x, y);
                allFinite = allFinite && finiteBits(c.r) && finiteBits(c.g) && finiteBits(c.b);
                if (y < img.height / 4u || y >= 3u * img.height / 4u || x < img.width / 4u || x >= 3u * img.width / 4u)
                    continue;
                const double m = std::max(c.r, std::max(c.g, c.b));
                middleZero = middleZero && m == 0.0;
                sum += m;
                ++n;
            }
        mean = n ? sum / double(n) : 0.0;
        return n > 0;
    };
    // (1) THE HOLE
    for (NodeId n : slabs) scene->setNodeVisible(n, false);
    settle();
    double hole = 0.0;
    bool finite = false, zero = false;
    bool ok = read(hole, finite, zero) && finite;
    for (NodeId n : slabs) scene->setNodeVisible(n, true);
    settle();
    // (2) THE UNCUT STACK
    double stack = 0.0;
    ok = read(stack, finite, zero) && finite && ok;
    const double T = std::round(0.9 * 2047.0) / 2047.0, want = std::pow(T, double(kCutSlabs)) * hole;
    const double bar = oneCodeAt(want) + 0.5 * oneCodeAt(hole);
    std::printf("== the mirror's hole: mean %.6f; four panes: mean %.6f, T^4 x the hole %.6f (bar %.6f)\n", hole, stack,
                want, bar);
    check(ok && hole > 0.01, "(1) the hole: the mirror floor reflects the emissive ceiling");
    char what[256];
    std::snprintf(what, sizeof(what), "(2) THE UNCUT STACK: four panes pass T^4 of the reflection (%.6f against %.6f, "
                  "within %.6f)", stack, want, bar);
    check(ok && std::fabs(stack - want) <= bar, what);
    // (3) THE CUT
    const unsigned long long before = scene->published().rayQuery.proceedStepsMax;
    if (!e->setArm("rayquery.stepBound", 2.0)) { std::printf("FAIL: the door rayquery.stepBound\n"); return 1; }
    settle();
    double cutMean = -1.0;
    bool cutFinite = false, cutZero = false;
    unsigned long long counter = 0ull;
    for (int f = 0; f < 30 && !(cutZero && counter == 2ull); ++f) {
        run(1);
        counter = scene->published().rayQuery.proceedStepsMax;
        if (!read(cutMean, cutFinite, cutZero)) break;
    }
    e->setArm("rayquery.stepBound", 0.0);
    std::printf("== the cut (bound 2): the floor's mean %.9f, every texel finite %d, every middle texel 0 %d; the counter "
                "%llu -> %llu (%d frames)\n", cutMean, int(cutFinite), int(cutZero), before, counter, frames);
    check(before == 0ull, "(3) before the door no loop reached the law: the counter reads 0");
    check(cutFinite, "(3a) THE CUT: every texel of the picture is finite (by its bits)");
    check(cutZero, "(3b) THE CUT: a cut reading ray answers a BLACK hit - the floor's every middle texel is exactly 0");
    check(counter == 2ull, "(3c) THE CUT: the counter reads exactly 2 (the cut published)");
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
