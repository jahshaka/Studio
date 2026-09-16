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

/// ONE FLAT QUAD WITH A CHOSEN NORMAL — the smallest thing that can be put in a
/// voxel facing a direction of our choosing.
static MeshData tiltedQuadMesh(float nx, float ny, float nz, float size)
{
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float w[3] = { nx / len, ny / len, nz / len };
    const float up[3] = { std::fabs(w[1]) < 0.9f ? 0.0f : 1.0f,
                          std::fabs(w[1]) < 0.9f ? 1.0f : 0.0f, 0.0f };
    float u[3] = { up[1] * w[2] - up[2] * w[1], up[2] * w[0] - up[0] * w[2],
                   up[0] * w[1] - up[1] * w[0] };
    const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    // v = w x u, so u x v = w and the winding below is counter-clockwise from +w.
    const float v[3] = { w[1] * u[2] - w[2] * u[1], w[2] * u[0] - w[0] * u[2],
                         w[0] * u[1] - w[1] * u[0] };
    const float h = size * 0.5f;
    const float sgn[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    MeshData d;
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c)
            d.positions.push_back((u[c] * sgn[i][0] + v[c] * sgn[i][1]) * h);
        d.normals.insert(d.normals.end(), { w[0], w[1], w[2] });
    }
    d.indices.insert(d.indices.end(), { 0u, 1u, 2u, 0u, 2u, 3u });
    return d;
}

static NodeId addMesh(Scene *s, const MeshData &md, const Colour &albedo, const Vec3 &pos,
                      const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(md);
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

    // =====================================================================
    // THE FOLD'S SEAM — a curved SINGLE-SIDED surface is one surface
    // (ogre-patch 0065; the review's F1)
    // =====================================================================
    //
    // The voxelisation decides "this voxel holds surfaces facing opposite ways"
    // from two sums: the raw one and a FOLDED one, folded into a half-space by
    // the sign of the normal's largest component. The fold's seam runs along the
    // six arcs where that largest component changes, and a smooth surface
    // crosses those arcs everywhere — so "the two groups are both non-empty"
    // calls a sphere's voxels two-sided, the injection takes abs(NdotL), and the
    // sphere's FAR side is lit by a lamp that is behind it. That is the leak
    // class, and no other suite sees it: this file's rooms and
    // gi.cascade_determinism's plates are all axis-aligned, where the seam is
    // never crossed.
    //
    // THE INSTRUMENT is the far side of the subject itself. One lamp, nothing
    // else in the scene, ambient black, shadows on: the DIRECT term on the far
    // side is zero whatever the voxels say (the pixel shader uses the mesh
    // normal), so every photon the camera sees there came out of the voxel
    // volume. A sphere's far side must stay dark; a plate thinner than a voxel
    // IS two-sided and its far side must NOT — that is the pin's behaviour and
    // the half of the test that stops the fix from being "never flag anything".
    std::printf("\n== the fold's seam: a curved single-sided surface is ONE surface ==\n");
    {
        // A FIXED GRADE, or this measures the auto-exposure instead of the light:
        // the scene is one lamp over a black void, so the 1x1 luminance history
        // opens all the way up on the dark side and reads a black frame as white
        // (measured: the sphere's unlit side came back at 1.0000 and its LIT side
        // at 0.2609 before this was pinned). tonemapFixed is the same filmic
        // curve with the metering taken out.
        PostFxDesc fx = view->postFx();
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.tonemapFixed = true;
        fx.exposure = 0.0f;
        view->setPostFx(fx);

        // THE TWO SUBJECTS ARE THE SAME TWO QUADS, 2 mm APART — far less than a
        // voxel, so both live in the same voxels — and they differ in ONE thing:
        // the second quad's normal.
        //
        //   SEAM: normals (0.72, 0, -0.69) and (0.69, 0, -0.72). The first has
        //     its largest component in x and is kept; the second has it in z,
        //     negative, and is FOLDED. Two groups, six and a half degrees apart:
        //     one surface, which the pin's 120-degree rule never flagged.
        //   TWO-SIDED: normals (0.707, 0, -0.707) and its exact opposite. Two
        //     groups, 180 degrees apart: two surfaces, which the pin DID flag,
        //     and which must still be flagged or "fix the seam" could be
        //     satisfied by never flagging anything.
        //
        // THE INSTRUMENT is the camera on the side both subjects' first quad
        // faces, with the lamp BEHIND. The direct term there is zero for both
        // (the pixel shader uses the mesh normal), so the difference between the
        // bounce on and the bounce off is light that was injected into a voxel
        // facing away from the lamp — which is the flag, and nothing else.
        const float k = 0.70710678f;
        const Vec3 frontEye(k * 3.4f, 0.0f, -k * 3.4f);
        const Vec3 behind(-k * 3.0f, 0.0f, k * 3.0f), inFront(k * 3.0f, 0.0f, -k * 3.0f);
        const auto measure = [&](bool seam, float &frontGi, float &frontLit) {
            Scene *s = e->createScene(seam ? "seam-pair" : "twosided-pair");
            view->setScene(s);
            s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
            const float d = 0.002f;
            addMesh(s, tiltedQuadMesh(0.72180f, 0.0f, -0.69207f, 3.0f), Colour(0.9f, 0.9f, 0.9f),
                    Vec3(k * d, 0.0f, -k * d), Vec3(1, 1, 1));
            if (seam)
                addMesh(s, tiltedQuadMesh(0.69207f, 0.0f, -0.72180f, 3.0f),
                        Colour(0.9f, 0.9f, 0.9f), Vec3(-k * d, 0.0f, k * d), Vec3(1, 1, 1));
            else
                addMesh(s, tiltedQuadMesh(-k, 0.0f, k, 3.0f), Colour(0.9f, 0.9f, 0.9f),
                        Vec3(-k * d, 0.0f, k * d), Vec3(1, 1, 1));

            const NodeId lamp = s->createNode();
            LightDesc ld;
            ld.type = LightType::Point;
            ld.colour = Colour(1, 1, 1);
            ld.intensity = 40.0f;
            ld.range = 20.0f;
            ld.castShadows = true;
            const auto placeLamp = [&](const Vec3 &at) {
                s->setNodeTransform(lamp, at, Quat(), Vec3(1, 1, 1));
                s->setLight(lamp, ld);
            };
            const auto readFront = [&]() {
                enginetest::testCameraLookAt(view, frontEye, Vec3(0, 0, 0));
                render(e, 10);
                Image img; view->readPixels(img);
                float r = 0.0f, g = 0.0f; meanRG(img, r, g);
                return g;
            };
            const auto setGi = [&](bool on) {
                GiParams gi;
                gi.mode = on ? GiMode::Vct : GiMode::Off;
                gi.quality = GiQuality::High;
                gi.ddgi = GiToggle::Off;     // the cone bounce, read directly
                gi.updateBudget = 0;
                gi.numBounces = 1;
                if (!s->setGlobalIllumination(gi))
                    std::printf("   engine error: %s\n", e->lastError().c_str());
                render(e, 6);
            };

            placeLamp(behind);
            setGi(false);
            const float off = readFront();
            setGi(true);
            const float on = readFront();
            frontGi = on - off;
            // ...and the same camera with the lamp IN FRONT, so a subject that
            // simply is not there cannot pass by reading zero twice.
            placeLamp(inFront);
            setGi(false);
            frontLit = readFront();
            std::printf("   %-9s lamp behind: GI off %.4f, GI on %.4f -> bounce %.4f | "
                        "lamp in front: %.4f\n", seam ? "seam" : "two-sided", double(off),
                        double(on), double(frontGi), double(frontLit));
            e->destroyScene(s);
        };

        float seamFront = 0.0f, seamLit = 0.0f, twoFront = 0.0f, twoLit = 0.0f;
        measure(true, seamFront, seamLit);
        measure(false, twoFront, twoLit);
        CHECK(seamLit > 0.05f && twoLit > 0.05f,
              "both subjects are there and face the camera (the lamp in front lights them)");
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "A SINGLE-SIDED SURFACE THAT CROSSES THE FOLD'S SEAM IS NOT LIT FROM "
                      "BEHIND: the seam pair's unlit side gains %.4f from the bounce against "
                      "the genuinely two-sided pair's %.4f", double(seamFront), double(twoFront));
        CHECK(seamFront < twoFront * 0.25f + 0.004f, msg);
        std::snprintf(msg, sizeof(msg),
                      "...and a genuinely two-sided pair still IS, which is the pin's answer "
                      "and the half that stops the fix being \"never flag anything\" (%.4f)",
                      double(twoFront));
        CHECK(twoFront > 0.02f, msg);
    }

    view->setScene(nullptr);
    e->destroyView(view);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
