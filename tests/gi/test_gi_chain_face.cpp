// gi.chain_face — PHOTON-E3 item 0 (ii): THE CHAIN-TO-WORLD FACE, and every
// face inside the chain, MEASURED (PHOTON_SPEC §7 E3, audit B8).
//
// WHAT THE QUESTION IS. Inside a voxel cascade the ambient a surface receives is
// VctLighting's HEMISPHERE PAIR — `c0 +- c1` of the Sky Light's spherical
// harmonics, the 2-band fit, weighted by the cone march's escape term. Outside
// the outermost cascade HlmsPbs' own ambient path applies the FULL 9 BANDS.
// `blend` is a literal 1.0 in `Vct_piece_ps.any`, so there is no fade between
// them: the two conventions meet at a hard face which, under a camera-centred
// chain, MOVES WITH THE CAMERA. A flat ambient makes them coincide by
// construction (c0 alone), which is why `gi.volume_edge` reads a step of under
// 5% and could not see this; a REAL SKY's bands 2..8 do not coincide.
//
// HOW IT IS MEASURED. A 400 m slab, no lights at all, an ORTHOGRAPHIC camera
// looking straight down: every pixel of the picture is the ambient term on the
// same surface with the same normal, so the profile across the image must be
// FLAT and any step in it is a boundary, not a shape. The camera is at y = 4 so
// the slab lies inside every cascade of the chain, and the faces land at
// |x - camera.x| = the cascades' half-sizes (5, 10, 15, 60 m at High).
//
// Four ambients: a flat colour (the control that makes the conventions agree),
// a hemisphere (the 2-band fit exactly — still no bands 2..8), and the analytic
// sky integrated by the engine itself at NOON and at a 5 degree sun, which is
// what the shipped Sky Light pushes. Each measured with the chain ON and with
// the single fitted volume (the arm before E2), and once more with the camera
// moved, to answer "does the face travel with the camera".
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 512;

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

struct Rig {
    View  *view = nullptr;
    Scene *scene = nullptr;
    float  orthoHalf = 100.0f;
    float  camX = 0.0f;
};

static void placeCamera(Rig &r)
{
    CameraDesc cam;
    cam.position = Vec3(r.camX, 4.0f, 0.0f);
    cam.orientation = Quat{ -0.70710678f, 0.0f, 0.0f, 0.70710678f };   // straight down
    cam.orthographic = true;
    cam.orthoSize = r.orthoHalf;
    cam.farClip = 500.0f;
    r.view->setCamera(cam);
}

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// The column profile: the mean luminance of each column over the middle rows.
static std::vector<float> profile(const Image &img)
{
    std::vector<float> p(kSize, 0.0f);
    for (unsigned x = 0; x < kSize; ++x) {
        float s = 0.0f;
        for (unsigned y = kSize / 2 - 8; y <= kSize / 2 + 8; ++y) s += lum(img.at(x, y));
        p[x] = s / 17.0f;
    }
    return p;
}

static unsigned columnForX(const Rig &r, float x)
{
    const float t = ((x - r.camX) / r.orthoHalf) * 0.5f + 0.5f;
    return unsigned(std::min(std::max(int(t * float(kSize) + 0.5f), 0), int(kSize) - 1));
}

/// The step across a face: the mean of a band each side, 3..18 columns clear of
/// it (the boundary column itself is a blend of both).
static float stepAt(const std::vector<float> &p, unsigned col, float &inside, float &outside)
{
    auto band = [&](int a, int b) {
        float s = 0.0f; int n = 0;
        for (int x = a; x <= b; ++x) { if (x < 0 || x >= int(kSize)) continue; s += p[x]; ++n; }
        return n ? s / float(n) : 0.0f;
    };
    inside  = band(int(col) + 3, int(col) + 18);
    outside = band(int(col) - 18, int(col) - 3);
    return outside > 1e-6f ? inside / outside : 0.0f;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-chain-face-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    Rig r;
    r.view = e->createOffscreenView("chain_face", kSize, kSize, Colour(0, 0, 0));
    r.scene = e->createScene("chain_face");
    r.view->setScene(r.scene);
    const NodeId slab = enginetest::addTestCube(r.scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(r.scene, slab, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(r.scene, slab, Vec3(400.0f, 0.1f, 400.0f));
    placeCamera(r);

    // ---- the four ambients -------------------------------------------------
    struct Amb { const char *name; bool sky; float elevDeg; Colour upper, lower; };
    const Amb ambients[] = {
        { "flat",        false, 0.0f, Colour(0.376f, 0.376f, 0.376f), Colour(0.376f, 0.376f, 0.376f) },
        { "hemisphere",  false, 0.0f, Colour(0.40f, 0.40f, 0.44f),    Colour(0.10f, 0.10f, 0.12f) },
        { "sky-noon",    true, 90.0f, Colour(), Colour() },
        { "sky-low",     true,  5.0f, Colour(), Colour() },
    };

    const auto applyAmbient = [&](const Amb &a) {
        if (!a.sky) {
            SkyDesc none; r.scene->setSky(none);
            r.scene->setAmbient(a.upper, a.lower);
            return true;
        }
        SkyDesc sky;
        sky.mode = SkyMode::Atmosphere;
        sky.atmosphere.hasSun = true;
        const float rad = a.elevDeg * 3.14159265f / 180.0f;
        sky.atmosphere.sunDir[0] = std::cos(rad);
        sky.atmosphere.sunDir[1] = std::sin(rad);
        sky.atmosphere.sunDir[2] = 0.0f;
        if (!r.scene->setSky(sky)) { std::printf("FAIL: setSky refused\n"); return false; }
        render(e, 8);                                    // the capture runs inside a frame
        float sh[27] = { 0.0f };
        if (!r.scene->skyAmbientSh(sh)) { std::printf("FAIL: skyAmbientSh not ready\n"); return false; }
        std::printf("   sky SH band0 %.4f %.4f %.4f  band1y %.4f  band6 %.4f\n",
                    sh[0], sh[1], sh[2], sh[3], sh[18]);
        r.scene->setAmbientSh(sh);
        return true;
    };

    // What each arm is allowed to do. The SINGLE VOLUME's face is the claim
    // `gi.volume_edge` makes for a flat ambient, asserted here for a REAL SKY as
    // well; the CHAIN's faces are E3's measurement, pinned loosely (they are a
    // known artefact with a number, not a target) so that a regression which
    // doubles them reds and an improvement never does.
    const float kSingleFaceMax = 1.05f;      // measured 1.007-1.029 over four ambients
    const float kChainFaceMax  = 1.40f;      // measured 1.031-1.328 over four ambients
    const float kChainFaceMin  = 0.55f;      // the c0 face with the field bound: 0.591-0.821
    const auto measure = [&](const char *what, bool chain, const Amb &a, float camX, float orthoHalf) {
        r.camX = camX; r.orthoHalf = orthoHalf; placeCamera(r);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.numBounces = 1;
        gi.ddgi = (std::getenv("JAH_E3_NO_FIELD") ? GiToggle::Off : GiToggle::On);
        gi.cascades = chain;
        CHECK(r.scene->setGlobalIllumination(gi), "GI arms");
        render(e, 16);
        Image img;
        r.view->readPixels(img);
        const std::vector<float> p = profile(img);
        const GiStatus st = r.scene->giStatus();
        std::printf("== %-34s chain=%d cascades=%zu\n", what, chain ? 1 : 0, st.cascades.size());
        if (chain) {
            for (size_t i = 0; i < st.cascades.size(); ++i) {
                const float face = st.cascades[i].centre.x + st.cascades[i].halfSize;
                const unsigned col = columnForX(r, face);
                if (col < 20 || col > kSize - 20) continue;
                float in = 0, out = 0;
                const float ratio = stepAt(p, col, in, out);
                std::printf("   cascade %zu half %5.1f centre %6.2f  face x %6.2f (col %3u)  "
                            "inner %.4f outer %.4f  step %.3fx (%+.1f/255)\n",
                            i, st.cascades[i].halfSize, st.cascades[i].centre.x, face, col, in, out,
                            ratio, (in - out) * 255.0f);
                CHECK(ratio > kChainFaceMin && ratio < kChainFaceMax,
                      "the chain's face steps no more than E3 measured it stepping");
            }
        } else {
            const float face = st.boundsMax.x;
            const unsigned col = columnForX(r, face);
            float in = 0, out = 0;
            const float ratio = stepAt(p, col, in, out);
            std::printf("   single volume x %.2f .. %.2f  face col %u  inner %.4f outer %.4f  "
                        "step %.3fx (%+.1f/255)\n", st.boundsMin.x, st.boundsMax.x, col, in, out,
                        ratio, (in - out) * 255.0f);
            CHECK(ratio > 1.0f / kSingleFaceMax && ratio < kSingleFaceMax,
                  "the SINGLE fitted volume's edge does not step, under a real sky either");
        }
        // The whole profile, thinned, so a face nobody predicted is still visible.
        std::printf("   profile:");
        for (unsigned x = 8; x < kSize; x += 16) std::printf(" %.3f", p[x]);
        std::printf("\n");
        return p;
    };

    for (const Amb &a : ambients) {
        std::printf("\n===== ambient: %s =====\n", a.name);
        if (!applyAmbient(a)) { ++failures; continue; }
        measure((std::string(a.name) + ", single volume").c_str(), false, a, 0.0f, 100.0f);
        const std::vector<float> p0 = measure((std::string(a.name) + ", chain, cam x=0").c_str(),
                                              true, a, 0.0f, 100.0f);
        const std::vector<float> p1 = measure((std::string(a.name) + ", chain, cam x=8").c_str(),
                                              true, a, 8.0f, 100.0f);
        {
            // THE FACES TRAVEL WITH THE CAMERA (audit B8), said as a measurement.
            // Read from the chain itself rather than from the picture, because the
            // OUTERMOST cascade steps every 15 m and an 8 m move does not move it:
            // the profile is a shift for the inner faces and not for the outer one,
            // which is itself the reading (the world profile is not a function of
            // the world — it is a function of where the eye is).
            const GiStatus st = r.scene->giStatus();
            bool followed = !st.cascades.empty();
            for (size_t i = 0; i < st.cascades.size(); ++i) {
                const float slack = st.cascades[i].step + 0.001f;
                if (std::fabs(st.cascades[i].centre.x - 8.0f) > slack) followed = false;
                std::printf("   cascade %zu centre x %6.3f after the camera moved to 8 (step %.2f)\n",
                            i, st.cascades[i].centre.x, st.cascades[i].step);
            }
            float worst = 0.0f;
            for (unsigned x = 40; x < kSize - 40; ++x) worst = std::max(worst, std::fabs(p0[x] - p1[x]));
            std::printf("   the same world columns changed by up to %.4f (%.1f/255) because the "
                        "chain moved under them\n", worst, worst * 255.0f);
            CHECK(followed, "every cascade sits within one step of the camera, so its faces "
                            "travel with the eye rather than staying in the world");
        }
        // ...and the inner faces, at 5x the magnification.
        measure((std::string(a.name) + ", chain, cam x=0, +-20 m").c_str(), true, a, 0.0f, 20.0f);
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
