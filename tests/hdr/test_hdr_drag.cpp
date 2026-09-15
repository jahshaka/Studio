// THE HDR AUTO-EXPOSURE MUST BE STABLE WHILE A LIGHT MOVES (lane HDR-1,
// ogre-patch 0042) — the owner's "with HDR on, dragging the light makes the
// materials flicker/shimmer, and it settles when I stop".
//
// WHAT WAS WRONG, and therefore what this suite is shaped to catch. The HDR
// chain's exposure is a MEAN of log( luminance ) over a sparse grid of the scene
// target, and a mean has no resistance: one unusable sample makes the whole
// FRAME's measurement unusable. Two things produce one, and both of them come
// and go from frame to frame while a light turns:
//
//   * the scene target is RGBA16F and a punctual light's specular lobe goes as
//     1 / (pi * alpha^2), so a near-mirror surface stores +Inf — and the
//     measurement's bilinear fetch weights that texel by zero on some frames and
//     not others, where 0 * Inf is a NaN;
//   * the target is a SUM of shading terms, and a filtered fetch across a blown
//     neighbourhood comes back BELOW ZERO, whose log() is a NaN too.
//
// An unusable measurement was then read as "the darkest scene this chain admits"
// (ogre-patch 0034), which is the LARGEST exposure it can produce — so every
// measurement failure yanked the grade towards its brightest limit, visibly, on
// a fraction of the frames of every drag.
//
// THE FIXTURE IS THE DEFECT'S OWN RECIPE, not a generic lit scene: a mirror-
// smooth metal floor, glossy spheres, and a BRIGHT POINT LIGHT low over the
// floor whose specular lobe is exactly the 1/(pi*alpha^2) term. Dragging it is
// what makes the blown texels move.
//
// THE ASSERTION IS ON THE ADAPTED LUMINANCE, not on pixels: View::
// measuredExposureScale() is the number the tonemapper samples, read off the
// chain's own 1x1 adaptation history, so this measures the grade itself rather
// than a picture that the grade happens to move. Two statements:
//
//   1. WHILE THE LIGHT DRAGS the adapted luminance changes SMOOTHLY. The
//      adaptation is a first-order filter that can move at most ~2.3 % of the
//      distance to the measurement per frame at 60 Hz, so a bound well inside
//      that is a statement about the MEASUREMENT being stable, not about the
//      filter. Measured with the fix: 0.06 % per frame; without it, repeated
//      1.7-2.3 % lurches, every one of them a frame whose measurement was a NaN.
//   2. AT REST it is quiet, which is what says the drag is the subject.
//
// It also asserts the same with SSR ON, because the SSR path roughly doubles the
// rate of unrepresentable pixels and is what made the owner's report look like
// an SSR defect (lane SSR-1 falsified that: a resolve forced to zero confidence
// flickers identically).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                               std::printf("\n"); ++failures; } \
                               else { std::printf("  ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static MeshData sphereMesh(unsigned rings = 32, unsigned segments = 48)
{
    MeshData d;
    const float kPi = 3.14159265358979323846f;
    for (unsigned r = 0; r <= rings; ++r)
        for (unsigned sg = 0; sg <= segments; ++sg) {
            const float v = float(r) / float(rings), u = float(sg) / float(segments);
            const float phi = v * kPi, theta = u * 2.0f * kPi;
            const float nx = std::sin(phi) * std::cos(theta);
            const float ny = std::cos(phi);
            const float nz = std::sin(phi) * std::sin(theta);
            d.positions.insert(d.positions.end(), { 0.5f * nx, 0.5f * ny, 0.5f * nz });
            d.normals.insert(d.normals.end(), { nx, ny, nz });
            d.uvs.insert(d.uvs.end(), { u, v });
        }
    const unsigned stride = segments + 1u;
    for (unsigned r = 0; r < rings; ++r)
        for (unsigned sg = 0; sg < segments; ++sg) {
            const unsigned a = r * stride + sg, b = a + stride;
            d.indices.insert(d.indices.end(), { a, b, a + 1u, a + 1u, b, b + 1u });
        }
    return d;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-hdr-drag-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    // 1280x720: the luminance reduction samples a 64x64 grid, so at 256x256 it
    // would sample EVERY pixel and the sparse-sampling half of the defect could
    // not arise at all. A real viewport's shape is the subject.
    const unsigned kW = 1280, kH = 720;
    View *view = engine->createOffscreenView("hdr", kW, kH, Colour(1.6f, 1.75f, 2.2f));
    Scene *s = engine->createScene("hdr");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    s->setAmbient(Colour(1.2f, 1.25f, 1.45f), Colour(0.7f, 0.72f, 0.85f));

    // THE MIRROR FLOOR. roughness 0 is clamped to the shader's alpha floor, which
    // is where 1/(pi*alpha^2) reaches ~1e6 and RGBA16F stops being able to say it.
    {
        const NodeId floor = s->createNode();
        PbrParams p; p.albedo = Colour(1, 1, 1); p.metalness = 1.0f; p.roughness = 0.0f;
        s->attachMesh(floor, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
        enginetest::setNodeScale(s, floor, Vec3(40.0f, 0.2f, 40.0f));
        enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    }
    // Curved glossy geometry, so the blown highlight is a small moving spot
    // rather than one flat mirror direction.
    for (int i = 0; i < 5; ++i) {
        const NodeId n = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.95f, 0.93f, 0.88f);
        p.metalness = 1.0f;
        p.roughness = 0.01f + 0.02f * float(i);
        s->attachMesh(n, s->createMesh(sphereMesh()), s->createPbrMaterial(p));
        enginetest::setNodeScale(s, n, Vec3(1.6f, 1.6f, 1.6f));
        enginetest::setNodePosition(s, n, Vec3(-3.6f + 1.8f * float(i), 0.9f, 0.4f));
    }
    // Something matte for the shadows to land on.
    for (int i = 0; i < 4; ++i) {
        const NodeId c = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.75f, 0.72f, 0.68f);
        p.metalness = 0.0f;
        p.roughness = 0.5f;
        s->attachMesh(c, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
        enginetest::setNodeScale(s, c, Vec3(1.1f, 1.6f + 0.5f * float(i), 1.1f));
        enginetest::setNodePosition(s, c, Vec3(-5.0f + 3.2f * float(i), 0.8f, -3.2f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 6.0f);

    // THE UNREPRESENTABLE HIGHLIGHT, made deterministic. In a real world this is
    // a punctual light's specular lobe on a near-mirror surface: 1/(pi*alpha^2)
    // at the shader's roughness floor reaches ~1e6 and the RGBA16F scene target
    // stores +Inf. Reproducing THAT depends on a driver's exact GGX arithmetic, so
    // the fixture states the condition outright instead: a SMALL emitter whose
    // radiance is past what a half float can say. It is small on purpose — a few
    // pixels — because the luminance reduction samples a sparse grid, and the
    // defect is that whether the frame's measurement is usable depends on whether
    // that grid happens to land on the blown pixels THIS frame.
    const NodeId spark = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.0f, 0.0f, 0.0f);
        p.emissive = Colour(-40.0f, -40.0f, -40.0f);
        p.roughness = 1.0f;
        s->attachMesh(spark, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
        enginetest::setNodeScale(s, spark, Vec3(0.05f, 0.05f, 0.05f));
    }

    // THE DRAGGED LIGHT. Bright, close to the mirror, and a POINT light — the
    // 1/(pi*alpha^2) specular peak is a punctual-light term.
    const NodeId lamp = s->createNode();
    {
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 0.97f, 0.92f);
        l.intensity = 900.0f;
        l.range = 40.0f;
        if (!lamp || !s->setLight(lamp, l)) { std::printf("FAIL: lamp\n"); return 1; }
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.8f, 7.0f), Vec3(0.0f, 0.9f, 0.0f));

    const int kFrames = 60;
    auto lampAt = [&](int f) {
        const float a = -1.1f + 2.2f * (float(f) / float(kFrames));
        enginetest::setNodePosition(s, lamp, Vec3(std::sin(a) * 6.0f, 1.3f, 2.0f + std::cos(a) * 2.0f));
        // The spark rides with it, so which sampling taps land on its blown
        // pixels changes from frame to frame - the whole point.
        enginetest::setNodePosition(s, spark, Vec3(std::sin(a) * 3.0f, 1.55f, 1.0f));
    };

    // THE CHAIN'S FALLBACK VALUE, derived the way the shader derives it
    // (HdrUtils::setExposure: exposure.x = 1024 * e^(exposure-2),
    // exposure.y = 7.5 - exposureMax). An adapted luminance sitting on THIS
    // number is not a measurement of anything - it is the chain saying "I could
    // not measure this frame", and before ogre-patch 0042 that is what a single
    // unusable sample made it say about every frame.
    // THE ADAPTATION WINDOW IS WIDE ON PURPOSE. At the shipped default
    // (+/-2.5) this fixture's geometric-mean luminance sits ON the floor, so the
    // adapted luminance is the clamp's value whatever the meter says and the
    // suite could assert nothing. Wide open, the 1x1 history reports the
    // MEASUREMENT - which is the thing under test.
    const float kExposure = 0.0f, kExpMin = -6.0f, kExpMax = 6.0f;
    const float kNoMeasurement = 1024.0f * std::exp(kExposure - 2.0f) / std::exp(7.5f - kExpMax);
    std::printf("== the chain's 'no measurement' constant is %.3f ==\n", kNoMeasurement);

    struct Run { const char *name; int ssr; bool spark; float dragBound; };
    const Run runs[] = { { "SSR off",              0, false, 0.8f },
                         { "SSR full-res",         2, false, 0.8f },
                         { "SSR off,      spark",  0, true,  0.8f },
                         { "SSR full-res, spark",  2, true,  0.8f } };
    float clean[2] = { 0.0f, 0.0f };

    for (const Run &r : runs) {
        s->setNodeVisible(spark, r.spark);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.bloom = true;
        fx.ssr = r.ssr;
        fx.ssrMaxDistance = 40.0f;
        fx.exposure = kExposure; fx.exposureMin = kExpMin; fx.exposureMax = kExpMax;
        view->setPostFx(fx);

        // Settle at the start pose: the adaptation and the shadow caches.
        lampAt(0);
        for (int i = 0; i < 150; ++i) engine->renderOneFrame();

        // A run-up at the same step, so the measured window sits in the MIDDLE of
        // a drag rather than at its onset (where a real lighting change legitimately
        // moves the grade fast).
        for (int f = 0; f < kFrames; ++f) { lampAt(f); engine->renderOneFrame(); }

        std::vector<float> drag;
        for (int f = 0; f < kFrames; ++f) {
            lampAt(f);
            engine->renderOneFrame();
            drag.push_back(view->measuredExposureScale());
        }
        // ...and at rest, with nothing moving at all.
        std::vector<float> rest;
        for (int f = 0; f < kFrames; ++f) {
            engine->renderOneFrame();
            rest.push_back(view->measuredExposureScale());
        }

        auto worst = [](const std::vector<float> &v, int *at) {
            float m = 0.0f;
            for (size_t i = 1; i < v.size(); ++i) {
                if (!(v[i] > 0.0f) || !(v[i - 1] > 0.0f)) continue;
                const float d = std::fabs(v[i] - v[i - 1]) / v[i - 1] * 100.0f;
                if (d > m) { m = d; if (at) *at = int(i); }
            }
            return m;
        };
        int atDrag = -1, atRest = -1;
        const float wd = worst(drag, &atDrag), wr = worst(rest, &atRest);
        float lo = drag[0], hi = drag[0];
        for (float x : drag) { if (x < lo) lo = x; if (x > hi) hi = x; }

        float mid = drag[drag.size() / 2];
        CHECK_MSG(drag[0] > 0.0f, "%s: the chain reports an adapted luminance (%.4f)",
                  r.name, drag[0]);
        if (!r.spark) {
            clean[r.ssr ? 1 : 0] = mid;
        } else {
            // THE ASSERTION THIS SUITE EXISTS FOR. A few pixels of unusable
            // radiance - a specular lobe past what RGBA16F can say, or a sum of
            // shading terms that came out below zero - must not decide the
            // exposure of the whole picture. Before ogre-patch 0042 the meter's
            // mean carried the NaN to every one of its 4096 texels and the chain
            // fell back to "the darkest scene I admit", which is its BRIGHTEST
            // exposure: measured on this fixture, 30.25 against a real
            // measurement of 6.80 - the entire picture blown, permanently.
            CHECK_MSG(mid < kNoMeasurement * 0.9f,
                      "%s: the meter still MEASURES with unusable pixels in the frame "
                      "(%.4f, and the 'no measurement' constant is %.4f)",
                      r.name, mid, kNoMeasurement);
            const float ref = clean[r.ssr ? 1 : 0];
            CHECK_MSG(ref > 0.0f && std::fabs(mid - ref) <= ref * 0.30f,
                      "%s: and it measures nearly what the same frame measures without them "
                      "(%.4f vs %.4f)", r.name, mid, ref);
        }
        // THE BOUND IS A STATEMENT ABOUT THE MEASUREMENT. The filter moves at most
        // 2.284 % of the gap per frame (pow(0.25, 1/60)), and a NaN frame's
        // fallback is ~2x the converged value, i.e. a ~2 % lurch. Anything at or
        // under 0.8 % cannot be one.
        CHECK_MSG(wd <= r.dragBound,
                  "%s: the exposure moves SMOOTHLY while the light drags "
                  "(worst %.3f %% per frame at f%d, bound %.2f %%; range %.4f..%.4f)",
                  r.name, wd, atDrag, r.dragBound, lo, hi);
        CHECK_MSG(wr <= 0.4f,
                  "%s: the exposure is quiet at rest (worst %.3f %% per frame at f%d)",
                  r.name, wr, atRest);

        if (std::getenv("JAH_HDR_DUMP")) {
            std::printf("   [%s] drag:", r.name);
            for (float x : drag) std::printf(" %.4f", x);
            std::printf("\n   [%s] rest:", r.name);
            for (float x : rest) std::printf(" %.4f", x);
            std::printf("\n");
        }
    }

    view->setPostFx(PostFxDesc());
    engine->destroyView(view);
    engine->destroyScene(s);
    std::printf("\n%s\n", failures ? "FAILURES" : "ALL OK");
    return failures ? 1 : 0;
}
