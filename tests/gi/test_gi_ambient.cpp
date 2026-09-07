// THE VCT AMBIENT (LIGHTING_FIX fix 3 / F-V1, F-V3).
//
// WHAT WAS WRONG. `Ogre::VctLighting` is born with both ambient hemispheres at
// BLACK (OgreVctLighting.cpp:106-107) and nothing in this engine ever set them.
// That matters because binding VctLighting REPLACES the ambient term inside the
// volume rather than adding to it: every PBS ambient piece is wrapped in
//
//     @property( vct_num_probes )
//         //Only use ambient lighting if object is outside any VCT probe
//         if( vctSpecular.w == 0 )
//
// (Samples/Media/Hlms/Pbs/Any/AmbientLighting_piece_ps.any), and the
// replacement is `light.xyz += ambient * light.w` in Vct_piece_ps.any — with an
// ambient of zero. So turning VCT on took the scene's ambient away from
// everything the volume covered, and the same disagreement made probe captures
// (which run the same shaders under different bindings) brighter than the world
// they were captured from.
//
// WHAT THIS SUITE ASSERTS, and why each assertion is the honest one:
//   1. A surface with NO direct light at all is not black under VCT. That is
//      the failure a user sees, stated as directly as it can be.
//   2. The SAME scene, same ambient, with and without VCT, reads within
//      tolerance. This is the assertion that catches a WRONG ambient as well as
//      a missing one — in particular the 1/pi trap: `Scene::setAmbient` scales
//      the flat case by 1/pi to reproduce HlmsPbs' own discrepancy between its
//      AmbientFixed and AmbientHemisphere paths, and pushing THAT value into
//      VctLighting (which has no such split) would make a VCT scene pi times
//      brighter than the same scene without it.
//   3. The shader variant does not flip under a colour drag. `needsAmbientHemi
//      sphere()` is a memcmp of the two hemispheres and its result is a shader
//      PROPERTY, so an ambient that happens to be flat for one frame recompiles
//      the scene twice. Asserted by compile counters, not by pixels.
//
// Its own binary, like every GI suite: the arm binds process-wide HlmsPbs state.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

// A white floor and a white wall, NO LIGHT OF ANY KIND. Whatever the camera
// sees is the ambient term and nothing else, which is what makes the numbers
// below a measurement rather than a comparison of two lighting solutions.
static Scene *ambientOnlyScene(Engine *engine, View *view, const char *name)
{
    Scene *s = engine->createScene(name);
    view->setScene(s);
    // A mid grey ambient with a genuine hemisphere split, so the SH path and the
    // VCT path are both exercised with a non-degenerate pair.
    s->setAmbient(Colour(0.40f, 0.40f, 0.44f), Colour(0.10f, 0.10f, 0.12f));

    const NodeId floor = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(8.0f, 0.2f, 8.0f));
    const NodeId wall = enginetest::addTestCube(s, Colour(0.9f, 0.9f, 0.9f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 2.0f, -3.0f));
    enginetest::setNodeScale(s, wall, Vec3(8.0f, 4.0f, 0.4f));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 1.0f, -1.0f));
    return s;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ambient-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("ambient", 128, 128, Colour(0, 0, 0));

    // ---- the reference: the same scene with GI OFF -------------------------
    Colour plain;
    {
        Scene *s = ambientOnlyScene(engine.get(), view, "ambient_plain");
        render(engine.get());
        Image img; view->readPixels(img);
        plain = img.at(64, 96);       // a floor patch
        std::printf("-- GI off:  floor r=%.3f g=%.3f b=%.3f\n", plain.r, plain.g, plain.b);
        CHECK(plain.r > 0.02f, "the reference scene really is lit by its ambient (not vacuous)");
        view->setScene(nullptr);
        engine->destroyScene(s);
    }

    // ---- the same scene with VCT on ----------------------------------------
    Colour vct;
    {
        Scene *s = ambientOnlyScene(engine.get(), view, "ambient_vct");
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::Low;
        gi.numBounces = 1;
        CHECK(s->setGlobalIllumination(gi), "VCT builds over the ambient-only scene");
        render(engine.get(), 6);
        Image img; view->readPixels(img);
        vct = img.at(64, 96);
        const GiStatus st = s->giStatus();
        std::printf("-- VCT on:  floor r=%.3f g=%.3f b=%.3f   (lit volume y %.2f..%.2f)\n",
                    vct.r, vct.g, vct.b, st.boundsMin.y, st.boundsMax.y);

        // 1. THE FAILURE, STATED DIRECTLY.
        CHECK(vct.r > 0.02f,
              "a surface with no direct light is NOT black under VCT (F-V1: the ambient survives)");

        // 2. ...and it is the RIGHT ambient, not merely a non-zero one. The
        //    tolerance is wide on purpose: VCT genuinely adds inter-reflection
        //    between the floor and the wall, so the two numbers must be close,
        //    never equal. A factor of pi — the 1/pi trap — is 3.14x and fails
        //    this comfortably in either direction.
        const float ratio = plain.r > 1e-4f ? vct.r / plain.r : 0.0f;
        std::printf("   VCT / no-VCT ambient ratio = %.3f\n", ratio);
        CHECK(ratio > 0.5f && ratio < 2.0f,
              "the VCT scene's ambient matches the non-VCT scene's within tolerance");

        // 3. THE SHADER VARIANT MUST NOT FLIP UNDER A COLOUR DRAG. A flat pair
        //    (upper == lower) turns `vct_ambient_sphere` off and recompiles
        //    everything; the engine adds an epsilon to keep the variant pinned,
        //    and this is what proves it. The drag deliberately PASSES THROUGH
        //    exactly-equal hemispheres, which is the case that used to flip.
        const unsigned before = engine->shaderCacheStats().compiledThisRun;
        for (int i = 0; i <= 8; ++i) {
            const float t = float(i) / 8.0f;             // 0.40 -> 0.10, passing 0.25 == lower
            const float upper = 0.40f - 0.30f * t;
            s->setAmbient(Colour(upper, upper, upper), Colour(0.25f, 0.25f, 0.25f));
            engine->renderOneFrame();
        }
        const unsigned after = engine->shaderCacheStats().compiledThisRun;
        std::printf("   ambient drag through the flat point: %u shader compiles\n", after - before);
        CHECK(after == before,
              "dragging the ambient THROUGH upper == lower compiles no shaders (the epsilon holds)");

        GiParams off;
        s->setGlobalIllumination(off);
        view->setScene(nullptr);
        engine->destroyScene(s);
    }

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
