// gi.leak_room — THE FIELD'S LEAK FIX, UNDER A CASCADE CHAIN TOO
// (SPECS/PHOTON_SPEC.md E1's bar; promoted from the PHOTON-S2 spike harness,
// spikes/photon-s2/FINDINGS.md §3, which measured the numbers this pins).
//
// A sealed 10 m room built four times at four wall thicknesses, with one lamp
// INSIDE it (pure green, so the red channel is 100 % the other lamp) and one
// OUTSIDE it (red, a metre off the -Z wall). Shadows are on, and with them the
// direct term through the wall is exactly zero at every thickness — so every
// red pixel inside the room arrived through global illumination, and that
// number IS the leak.
//
// WHY IT IS A GATE SUITE NOW. The irradiance field's reason for existing in
// this engine is that its Chebyshev visibility test stops that leak: the spike
// measured 0.973 without the field and 0.041 with it at a 0.5 m wall — the
// field removes 96 % of it. E1 moves the field onto the innermost cascade of a
// camera-centred chain: a different volume, re-placed as the camera walks. The
// one thing that must survive that move is exactly this, so the suite runs both
// arms over the same rooms — the scene-fitted single volume (the shipped
// reference) and the chain (E1) — with the camera standing close enough to the
// measured wall for it to be inside cascade 0's box.
//
// Its own binary like every GI suite: the voxel lighting and the field bind
// process-wide to HlmsPbs, so this scene must not share a process with another
// arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

/// Mean red and green of a centred block, so one texel of noise cannot move a
/// number. The block is the same one the spike measured with.
static void meanRG(const Image &img, float &r, float &g)
{
    double sr = 0.0, sg = 0.0; int n = 0;
    for (unsigned y = 40; y < 88; ++y)
        for (unsigned x = 40; x < 88; ++x) { const Colour c = img.at(x, y); sr += c.r; sg += c.g; ++n; }
    r = float(sr / n); g = float(sg / n);
}

int main()
{
    const float thicknesses[4] = { 0.5f, 0.2f, 0.1f, 0.05f };
    // THE BARS, and where they come from. The spike measured 0.041 / 0.046 /
    // 0.054 / 0.083 (FINDINGS §3a, "the wall itself") from SIX metres away. This
    // suite stands at THREE, because under a chain the field rides cascade 0 — a
    // 10 m box centred on the camera — and at six metres the wall being measured
    // is outside it, so the suite would be grading the ring instead of the
    // field. Closer means a brighter wall and a slightly larger absolute leak in
    // BOTH arms: the shipped single-volume arm, whose code E1 does not touch,
    // reads 0.0452 at 0.5 m here against the spike's 0.0410 there. The bars are
    // therefore this pose's, measured, with ~12 % of headroom, and the
    // comparative bar below is the one that carries the actual claim: moving the
    // field onto a cascade must not leak MORE than the scene-fitted field does.
    const float bars[4] = { 0.050f, 0.050f, 0.058f, 0.080f };

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-leak-room-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("leak", 128, 128, Colour(0, 0, 0));
    // Offscreen views ship with shadows OFF; without them the outside lamp
    // lights the inside straight through the wall and there is no GI
    // measurement left to make (the spike established this the hard way).
    view->setShadows(true);

    struct Row { float leakSingle = 0.0f, leakChain = 0.0f, greenChain = 0.0f;
                 bool fieldSingle = false, fieldChain = false;
                 float fieldSpan = 0.0f; };
    Row rows[4];

    for (int a = 0; a < 4; ++a) {
        const float T = thicknesses[a];
        Scene *s = e->createScene("leak" + std::to_string(a));
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

        // Interior: x,z in [-5,5], y in [0,4]. The walls sit OUTSIDE that box,
        // so the interior is identical in all four arms and the only thing that
        // changes is how thick the barrier is in voxels.
        const Colour white(0.8f, 0.8f, 0.8f);
        const float ho = 5.0f + T * 0.5f;
        const float span = 10.0f + 2.0f * T;
        addSlab(s, white, Vec3(0, -T * 0.5f, 0), Vec3(span, T, span));            // floor
        addSlab(s, white, Vec3(0, 4.0f + T * 0.5f, 0), Vec3(span, T, span));      // ceiling
        addSlab(s, white, Vec3(0, 2, -ho), Vec3(span, 4.0f, T));                  // -Z: THE wall
        addSlab(s, white, Vec3(0, 2,  ho), Vec3(span, 4.0f, T));                  // +Z
        addSlab(s, white, Vec3(-ho, 2, 0), Vec3(T, 4.0f, span));                  // -X
        addSlab(s, white, Vec3( ho, 2, 0), Vec3(T, 4.0f, span));                  // +X

        // The inside lamp: PURE GREEN, so the red channel is entirely the
        // outside lamp's and no threshold has to separate them.
        const NodeId inside = s->createNode();
        s->setNodeTransform(inside, Vec3(0.0f, 3.0f, 2.5f), Quat(), Vec3(1, 1, 1));
        LightDesc li;
        li.type = LightType::Point;
        li.colour = Colour(0.0f, 1.0f, 0.0f);
        li.intensity = 3.0f;
        li.range = 14.0f;
        li.castShadows = true;
        s->setLight(inside, li);

        // The outside lamp: RED, a metre beyond the outer face of the -Z wall.
        const NodeId outside = s->createNode();
        s->setNodeTransform(outside, Vec3(0.0f, 2.0f, -(ho + T * 0.5f + 1.0f)), Quat(), Vec3(1, 1, 1));
        LightDesc lo;
        lo.type = LightType::Point;
        lo.colour = Colour(1.0f, 0.0f, 0.0f);
        lo.intensity = 25.0f;
        lo.range = 12.0f;
        lo.castShadows = true;
        s->setLight(outside, lo);

        // The camera looks at the inner face of the -Z wall from THREE METRES
        // away, off to +X so nothing else is in the shot. Three metres, and not
        // the spike's six, for one reason: under the chain the field rides
        // cascade 0, a 10 m box centred on the camera, and the wall this
        // measures has to be inside it or the suite is measuring the ring
        // instead of the field.
        enginetest::testCameraLookAt(view, Vec3(3.6f, 2.0f, -2.0f), Vec3(3.6f, 2.0f, -5.0f));

        // ONE measurement path, run once per arm: set the arm, light the
        // outside lamp, read; darken it, read again. The difference in the red
        // channel is the leak, and the green channel beside it says the room is
        // lit at all (a black room leaks nothing and proves nothing).
        const auto leakOf = [&](bool cascades, const char *what) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.ddgi = GiToggle::On;
            gi.updateBudget = 1;
            gi.numBounces = 1;
            gi.cascades = cascades;
            if (!cascades) {
                // The single-volume arm keeps the spike's explicit bounds so the
                // reference number is the spike's number; the chain fits itself.
                gi.testBoundsMin = Vec3(-6.5f, -1.0f, -6.5f);
                gi.testBoundsMax = Vec3( 6.5f,  5.5f,  6.5f);
            }
            if (!s->setGlobalIllumination(gi))
                std::printf("   engine error: %s\n", e->lastError().c_str());
            lo.intensity = 25.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
            render(e, 10);
            Image img; view->readPixels(img);
            float onR = 0.0f, onG = 0.0f; meanRG(img, onR, onG);
            lo.intensity = 0.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
            render(e, 10);
            view->readPixels(img);
            float offR = 0.0f, offG = 0.0f; meanRG(img, offR, offG);
            const GiStatus st = s->giStatus();
            std::printf("   %-6s wall %.2f m: LEAK %.4f (lamp on r %.4f, off r %.4f), green %.4f, "
                        "field %s over %.1f m, cascades %zu\n",
                        what, double(T), double(onR - offR), double(onR), double(offR),
                        double(onG), st.ifdBound ? "bound" : "ABSENT",
                        double(st.ifdMax.x - st.ifdMin.x), st.cascades.size());
            return std::make_tuple(onR - offR, onG, st.ifdBound);
        };

        const auto single = leakOf(false, "single");
        const auto chain = leakOf(true, "chain");
        const GiStatus stChain = s->giStatus();
        // BOTH arms must carry a bound field, or the comparative bar below
        // compares a field against the cone leak (~0.97) and passes trivially.
        rows[a].fieldSingle = std::get<2>(single);
        rows[a].leakSingle = std::get<0>(single);
        rows[a].leakChain = std::get<0>(chain);
        rows[a].greenChain = std::get<1>(chain);
        rows[a].fieldChain = std::get<2>(chain) && stChain.ifdBound;
        rows[a].fieldSpan = stChain.ifdMax.x - stChain.ifdMin.x;
        CHECK(rows[a].fieldSingle,
              "leak_room: the scene-fitted REFERENCE arm carries a bound field (or the "
              "comparative bar would grade a field against the cone leak)");
        CHECK(stChain.ifdBound && !stChain.cascades.empty(),
              "the chain arm really has a chain AND a field bound");
        e->destroyScene(s);
    }

    std::printf("\n wall(m)   leak SINGLE   leak CHAIN   bar      green(chain)\n");
    for (int a = 0; a < 4; ++a)
        std::printf("  %5.2f    %10.4f   %10.4f   %6.4f   %10.4f\n", double(thicknesses[a]),
                    double(rows[a].leakSingle), double(rows[a].leakChain), double(bars[a]),
                    double(rows[a].greenChain));

    // THE BARS. Asserted on the CHAIN arm, because that is E1's new claim; the
    // single-volume arm is measured beside it so a regression in the reference
    // cannot hide behind a chain that matches it.
    for (int a = 0; a < 4; ++a) {
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "the field on cascade 0 holds the %.2f m wall's leak at or under %.4f",
                      double(thicknesses[a]), double(bars[a]));
        CHECK(rows[a].leakChain <= bars[a], msg);
    }
    CHECK(rows[0].greenChain > 0.02f,
          "the room is lit by its own lamp (the leak is measured on a lit wall, not a black one)");
    // AND THE CLAIM ITSELF: the field on a cascade holds the wall at least as
    // well as the field on the scene's fitted box does. It should do slightly
    // BETTER, and does — cascade 0 is a 10 m box, so its 8,192 probes sit 0.5 m
    // apart where the fitted volume's sit 1.6 m apart, and a denser cage is a
    // sharper visibility test.
    for (int a = 0; a < 4; ++a) {
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "...and it leaks no more than the scene-fitted field at %.2f m "
                      "(%.4f vs %.4f)", double(thicknesses[a]), double(rows[a].leakChain),
                      double(rows[a].leakSingle));
        CHECK(rows[a].leakChain <= rows[a].leakSingle * 1.15f + 0.002f, msg);
    }

    view->setScene(nullptr);
    e->destroyView(view);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
