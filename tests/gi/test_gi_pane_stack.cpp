// gi.pane_stack (PHOTON-7 10-4 fix round, the one read's F4; L9: THE RENDERER FOLLOWS THE RAY) — A RAY THROUGH MORE PANES
// THAN A LIST HOLDS IS NEVER BLOCKED BY THEIR COUNT. Twelve glass slabs (4 x 4 m, 1 cm, Glass at alpha 0.1: T = 0.9 each)
// stacked flat 0.6-2.25 m over a matte floor (albedo 0.6) in a black world (no sky, no ambient); ONE point lamp above them
// with castShadows off - a Forward+ lamp, its visibility ONE ray from the pixel (jah_lamp_ray.glsl); the eye under the
// stack, 0.45 m over the floor, looking down: every pixel it reads is floor under all twelve panes.
//
// RED ON THE BASE: the ray queries listed eight panes and CONFIRMED the ninth as an opaque hit (jah_rq_alpha.glsl's old
// JAH_RQ_PANE_CANDIDATE) - the floor under twelve panes read 0 (a lamp behind a shelf of wine glasses: black). Now a LIGHT
// ray multiplies every pane's T as it meets it (JAH_RQ_PROCEED_TRANSMIT: no list, no cap), and a READING ray keeps the
// eight nearest and applies the rest (their T; their own light left out), each such ray counted (giStatus
// rayQuery.panesOverflow).
// THE LAW (the ray tier at High), from the HDR readback (linear radiance), over the picture's middle half:
//   (0) THE HOLE - the slabs hidden: the floor's mean H, over a code;
//   (1) THE STACK - the GI off, the pixel's direct light alone: the floor's mean is T^12 H (T as the pane word stores it,
//       on 2047 steps), within ONE CODE at T^12 H's level (the display's sRGB code width there) plus the readback's half
//       code of H;
//   (2) THE READING RAYS - the GI on at High (the screen probe gather traces from the floor up through the stack): the
//       overflow counter moves (a reading ray met more than eight panes and was counted, not blocked).
// THE CARDS ARM (argv[1] "cards"; the one read's F3: the cards saw a lamp through glass as 0 or 1 by a > 0.5 test - its
// table bit - so a lamp behind T = 0.7 read 1 and one behind T^12 = 0.28 read 0): the GI on with the cards, their direct
// half alone (no bounce), the cards at rest; over the floor's card texels under the stack, the direct half's mean D:
//   (3) the hole: D0 over a code; (4) twelve panes: D = T^12 D0; (5) ONE pane at alpha 0.3: D = 0.7 D0 - each within
//       ONE CODE at its level plus the texel's byte (the transmitted lamps' visibility is stored on 255 steps: half a
//       step of D0).
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "cards") { std::printf("FAIL: the mode is ray or cards, not '%s'\n", mode.c_str()); return 2; }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-pane-stack-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("pane_stack", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("pane_stack");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    view->setShadows(true);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.0f, 0.4f, 8.0f), scene->createPbrMaterial(matte));
    PbrParams glass = matte;
    glass.albedo = Colour(0.3f, 0.3f, 0.3f);
    glass.alphaMode = PbrAlphaMode::Glass;
    glass.alpha = 0.1f;
    const MaterialId glassMat = scene->createPbrMaterial(glass);
    constexpr int kSlabs = 12;
    std::vector<NodeId> slabs;
    for (int k = 0; k < kSlabs; ++k) {
        const NodeId n = box(scene, Vec3(0.0f, 0.6f + 0.15f * float(k), 0.0f), Vec3(4.0f, 0.01f, 4.0f), glassMat);
        if (!n) { std::printf("FAIL: slab %d\n", k); return 1; }
        slabs.push_back(n);
    }
    {
        const NodeId lamp = scene->createNode();
        scene->setNodeTransform(lamp, Vec3(0.0f, 2.8f, 0.0f), Quat(), Vec3(1, 1, 1));
        LightDesc ld;
        ld.type = LightType::Point;
        ld.colour = Colour(1, 1, 1);
        ld.intensity = 8.0f;
        ld.range = 30.0f;
        ld.castShadows = false;   // a Forward+ lamp: its visibility is the lamp ray's
        if (!scene->setLight(lamp, ld)) { std::printf("FAIL: the lamp\n"); return 1; }
    }
    float sh[27] = { 0 };   // no sky, no ambient
    scene->setAmbientSh(sh);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.45f, 0.25f), Vec3(0.0f, 0.0f, -0.05f));
    e->waitForAsyncShaders();

    const auto read = [&](double &mean) {
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return false;
        double sum = 0.0;
        size_t n = 0;
        for (unsigned y = img.height / 4u; y < 3u * img.height / 4u; ++y)
            for (unsigned x = img.width / 4u; x < 3u * img.width / 4u; ++x) {
                const Colour c = img.at(x, y);
                sum += std::max(c.r, std::max(c.g, c.b));
                ++n;
            }
        mean = n ? sum / double(n) : 0.0;
        return n > 0 && std::isfinite(mean);
    };
    const auto settle = [&]() { e->waitForAsyncShaders(); run(8); };
    const auto overflow = [&]() { return scene->published().rayQuery.panesOverflow; };

    if (mode == "cards") {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.cards = true;
        gi.bounces = false;   // the direct half alone: what the lamp's traced visibility gates
        gi.updateBudget = 0;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        // the cards at rest: THE ENGINE's PREDICATE (giAtRest: the readers held AND the cards' one rest decision taken -
        // never a frame count; PHOTON-7 stage 8B), and built, no relight or admission moving, the queue empty, for 30
        // frames. (Under the per-cell rest the decision waits for the LAST cell's rest - ~100 frames later here than a
        // view-wide rest's, 292 frames against 195 - a named cost; the window's drain before it evicts the floor's 13
        // cards no reader stamps, and the decision re-admits them from its face marks: a read before the decision read
        // them absent.)
        const auto atRest = [&]() {
            unsigned long long relights = ~0ull, admitted = ~0ull;
            int quiet = 0;
            for (int f = 0; f < 600 && quiet < 30; ++f) {
                run(1);
                const GiStatus g = scene->published().gi;
                const bool still = g.giAtRest && !g.chainOwed && g.cards.built && g.cards.relights == relights &&
                                   g.cards.admitted == admitted && g.cards.queueLength == 0u;
                relights = g.cards.relights;
                admitted = g.cards.admitted;
                quiet = still ? quiet + 1 : 0;
            }
            return quiet >= 30;
        };
        // the direct half's mean over the floor's card texels under the stack (its middle 1.6 x 1.6 m)
        const auto direct = [&](double &mean) {
            double sum = 0.0;
            size_t n = 0;
            for (int i = -4; i <= 4; ++i)
                for (int j = -4; j <= 4; ++j) {
                    CardSample c;
                    if (!scene->readCardAt(Vec3(0.2f * float(i), 0.0f, 0.2f * float(j)), Vec3(0, 1, 0), c) || !c.ok || !c.lit)
                        continue;
                    sum += c.radiance[1] - c.indirect[1] - c.flat[1];
                    ++n;
                }
            mean = n ? sum / double(n) : 0.0;
            return n > 40;
        };
        for (NodeId n : slabs) scene->setNodeVisible(n, false);
        double d0 = 0.0, d12 = 0.0, d1 = 0.0;
        bool ok = atRest() && direct(d0);
        for (NodeId n : slabs) scene->setNodeVisible(n, true);
        ok = atRest() && direct(d12) && ok;
        for (size_t k = 1; k < slabs.size(); ++k) scene->setNodeVisible(slabs[k], false);
        PbrParams one = glass;
        one.alpha = 0.3f;
        ok = scene->setPbrMaterial(glassMat, one) && atRest() && direct(d1) && ok;
        const double T12 = std::pow(std::round(0.9 * 2047.0) / 2047.0, double(kSlabs)) * d0;
        const double T1 = std::round(0.7 * 2047.0) / 2047.0 * d0;
        const double byte = 0.5 / 255.0 * d0;
        std::printf("== cards: the direct half, the hole %.6f; twelve panes %.6f (T^12 x %.6f); one pane at 0.3 %.6f "
                    "(0.7 x %.6f); %d frames\n", d0, d12, T12, d1, T1, frames);
        check(ok && d0 > 0.005, "(3) the hole: the lamp lights the floor's cards (at rest, the texels read)");
        char w[256];
        std::snprintf(w, sizeof(w), "(4) the cards through twelve panes: T^12 of the lamp (%.6f against %.6f)", d12, T12);
        check(ok && std::fabs(d12 - T12) <= oneCodeAt(T12) + byte, w);
        std::snprintf(w, sizeof(w), "(5) the cards through one pane at 0.3: 0.7 of the lamp (%.6f against %.6f)", d1, T1);
        check(ok && std::fabs(d1 - T1) <= oneCodeAt(T1) + byte, w);
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }

    // (0) THE HOLE
    for (NodeId n : slabs) scene->setNodeVisible(n, false);
    settle();
    double hole = 0.0;
    bool ok = read(hole);
    for (NodeId n : slabs) scene->setNodeVisible(n, true);
    settle();
    const unsigned long long before = overflow();
    // (1) THE STACK, the direct light alone
    double stack = 0.0;
    ok = read(stack) && ok;
    const double T = std::round(0.9 * 2047.0) / 2047.0, want = std::pow(T, double(kSlabs)) * hole;
    const double bar = oneCodeAt(want) + 0.5 * oneCodeAt(hole);
    std::printf("== the hole: mean %.6f; twelve panes: mean %.6f, T^12 x the hole %.6f (bar %.6f); the overflow counter "
                "%llu\n", hole, stack, want, bar, before);
    check(ok && hole > 0.01, "(0) the hole: the lamp lights the floor");
    char what[256];
    std::snprintf(what, sizeof(what), "(1) THE LIGHT RAY: twelve panes pass T^12 of the lamp (%.6f against %.6f, within %.6f)",
                  stack, want, bar);
    check(ok && std::fabs(stack - want) <= bar, what);
    // (2) THE READING RAYS: the GI on at High
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    unsigned long long after = before;
    for (int f = 0; f < 240 && after == before; ++f) {
        run(1);
        after = overflow();
    }
    std::printf("== the GI on: the overflow counter %llu -> %llu (%d frames)\n", before, after, frames);
    check(after > before, "(2) THE READING RAYS: a ray through twelve panes is counted past the list, never blocked");
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
