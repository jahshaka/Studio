// gi.pane_stack_cut — A CUT RAY ANSWERS AS OCCLUDED (GATHER-HANG-1 stage 2, the lead's ruling (B); jah_rq_alpha.glsl THE
// BOUND): a MIRROR floor (metal, albedo 1, roughness 0) under N = 4 of the same glass slabs, an EMISSIVE ceiling (E) above them, nothing
// else lit (no lamp, no sky, no ambient, the GI off), the reflection's rays alone (reflect.screenOff) and one frame of
// history (reflect.window 1): every floor pixel is the reflection ray's answer through the stack. Over the middle half:
//   (1) THE HOLE: the slabs hidden, the floor's mean R over a code (the ceiling reflected: R = F E);
//   (2) THE UNCUT STACK: the four slabs (eight candidate faces a ray), the mean is T^4 R within ONE CODE at its level plus
//       the readback's half code of R - the panes multiply, never block (no pane has its own light: nothing lights it);
//   (3) THE CUT, under a FLAT ENVIRONMENT an escaping ray reads (the ambient's constant band, kEnv: with no cube it is
//       what a miss answers, OgreScene::rayEnvironment - so a cut ray wrongly answered as an ESCAPE reads kEnv, never 0;
//       the final read's F3): the test door rayquery.stepBound 2 (every reflection ray meets eight candidates, so every
//       one is CUT): (a) every texel of the picture FINITE by its bits, (b) the floor's every texel EXACTLY 0 (a cut
//       reading ray answers a BLACK hit at its end - no radiance, no sky, no pane light: a cut never adds light), (c) the
//       counter rayQuery.proceedStepsMax reads exactly 2 (the cut published); (d) THE CONTROL that (b) can fail: the door
//       off and the ceiling hidden, the rays escape and the floor reads the environment (its mean over half of kEnv's
//       reflected share) - an escape is not black here.
// The cut arm of gi.pane_stack (test_gi_pane_stack.cpp: the same slabs, T = 0.9 as the pane word stores it), its own
// file because its view declares its route (CODING_RULES section 6) and the stack's two arms are still on the ratchet.
// Red on the base: the status field rayQuery.proceedStepsMax and the door rayquery.stepBound do not exist.
// THE NaN ARM (argv[1] "nanray": gi.ray_nan_refused; GATHER-HANG-1 stage 2, JAH_RQ_INIT): the same scene, uncut. (4) a
// healthy scene refuses no ray: rayQuery.raysRefused reads 0 after its frames; (5) the test door rayquery.nanRay
// (invocation 0 of each job with the hit params traces from a NaN origin, by its bits): the frames END, the counter
// moves (every such ray refused - an empty query, a miss - never traced), every texel stays finite, and a frame is marked
// INCOMPLETE by the ray input term (PublishedFrame::Completeness bit 5: no rest decision on it). Red on the base:
// the status field rayQuery.raysRefused and the door do not exist - NEVER an unguarded NaN ray on the card (undefined in
// the API: a device loss under the shared lock).
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Mesh.h"
#include "jahshaka/engine/Pictures.h"
#include "jahshaka/engine/Published.h"
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

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "cut";
    if (mode != "cut" && mode != "nanray") { std::printf("FAIL: the mode is cut or nanray, not '%s'\n", mode.c_str()); return 2; }
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
    const NodeId ceilingNode = box(scene, Vec3(0.0f, 3.2f, 0.0f), Vec3(8.0f, 0.2f, 8.0f), scene->createPbrMaterial(ceiling));
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
    enginetest::declareRoutes(view, scene, enginetest::Routes().reflection(),
                              mode == "cut" ? "gi.pane_stack_cut's view" : "gi.ray_nan_refused's view");
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
    if (mode == "nanray") {
        // (4) a healthy scene refuses no ray; (5) the NaN door
        settle();
        double mean = 0.0;
        bool finite = false, zero = false;
        const bool ok0 = read(mean, finite, zero) && finite;
        const unsigned long long healthy = scene->published().rayQuery.raysRefused;
        std::printf("== the healthy scene: the floor's mean %.6f, the rays refused %llu (%d frames)\n", mean, healthy, frames);
        check(ok0 && mean > 0.01, "(4) the scene renders: the mirror reflects the ceiling through the panes");
        check(healthy == 0ull, "(4) A HEALTHY SCENE REFUSES NO RAY: rayQuery.raysRefused reads 0");
        if (!e->setArm("rayquery.nanRay", 1.0)) { std::printf("FAIL: the door rayquery.nanRay\n"); return 1; }
        const int from = frames;
        unsigned long long refused = healthy;
        bool incompleteSeen = false;
        for (int f = 0; f < 30 && !(refused > healthy && incompleteSeen); ++f) {
            run(1);
            refused = scene->published().rayQuery.raysRefused;
            const PublishedFrame::Completeness c = scene->published().completeness;
            incompleteSeen = incompleteSeen || (c.incomplete && (c.terms & 32u) != 0u);
        }
        const bool ok1 = read(mean, finite, zero);
        e->setArm("rayquery.nanRay", 0.0);
        std::printf("== the NaN door: %d frames ended, the rays refused %llu -> %llu, every texel finite %d\n", frames - from,
                    healthy, refused, int(finite));
        check(refused > healthy, "(5) THE NaN DOOR: the frames end and every NaN ray is REFUSED (counted, never traced)");
        check(ok1 && finite, "(5) THE NaN DOOR: every texel of the picture stays finite (by its bits)");
        check(incompleteSeen, "(5) THE NaN DOOR: a frame with a refused ray is INCOMPLETE (PublishedFrame::Completeness "
                              "term bit 5: no rest decision on it)");
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }
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
    // (3) THE CUT, under the flat environment an escape reads (kEnv)
    constexpr float kEnv = 0.25f;
    {
        float env[27] = { 0 };
        env[0] = env[1] = env[2] = kEnv;   // the constant band: the mean radiance a direction-free environment answers
        scene->setAmbientSh(env);
    }
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
    std::printf("== the cut (bound 2, under the environment %.2f): the floor's mean %.9f, every texel finite %d, every "
                "middle texel 0 %d; the counter %llu -> %llu (%d frames)\n", double(kEnv), cutMean, int(cutFinite),
                int(cutZero), before, counter, frames);
    // (3d) the control: the door off, the ceiling hidden - the rays escape and read the environment
    for (NodeId n : slabs) scene->setNodeVisible(n, false);
    scene->setNodeVisible(ceilingNode, false);
    settle();
    double escapeMean = 0.0;
    bool escFinite = false, escZero = false;
    const bool escOk = read(escapeMean, escFinite, escZero);
    std::printf("== the escape (the ceiling hidden, the door off): the floor's mean %.6f\n", escapeMean);
    check(before == 0ull, "(3) before the door no loop reached the law: the counter reads 0");
    check(cutFinite, "(3a) THE CUT: every texel of the picture is finite (by its bits)");
    check(cutZero, "(3b) THE CUT: a cut reading ray answers a BLACK hit - the floor's every middle texel is exactly 0");
    check(counter == 2ull, "(3c) THE CUT: the counter reads exactly 2 (the cut published)");
    check(escOk && escapeMean > 0.5 * 0.04 * double(kEnv),
          "(3d) THE CONTROL: an escaping ray reads the environment (the floor's mean over half of a dielectric-floor "
          "F0 0.04 x kEnv - the mirror's F is higher) - so (3b)'s exact 0 is a cut answered as a BLACK hit, never a miss");
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
