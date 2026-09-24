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
//
// ===========================================================================
// TWO ctest ROWS, ONE BINARY (PHOTON phase A, A1 §0/§1.2 — lane FENCE-1)
// ===========================================================================
//
// WHAT THIS SUITE USED TO SAY, AND WHY IT IS WRONG TO SAY IT. The bars below
// were a BRACKET AROUND A KNOWN ARTEFACT: "the chain's face steps no more than
// E3 measured it stepping". That is a fence around today's picture, and a fence
// around a defect has to be re-anchored by every part that reduces the defect —
// which is exactly how 1.05 became 1.08 and 0.02 became 0.012 elsewhere. Under
// forward-building (owner 2026-09-22) a suite states the CORRECT number.
//
// THE CORRECT NUMBER IS 1.0. The face between two cascades, and between the
// outermost cascade and the world, is a boundary in a DATA STRUCTURE. The
// ambient a surface receives is a property of the surface and the sky, not of
// which cell size happens to store it, so a flat slab under an unchanging sky
// must render one luminance across the whole picture and every face ratio must
// be 1.000. Anything else is the renderer telling you where its cascades are.
//
// SO THERE ARE TWO ROWS:
//
//   gi.chain_face         ORDINARY, and it gates. It carries (a) the bracket,
//                         renamed to what it actually is — "no regression while
//                         red" — and (b) the "cascades follow the camera" arm,
//                         which is a CORRECT claim and not a target at all.
//                         Both print. The bracket is DELETED by the lane that
//                         removes the target row's label.
//   gi.chain_face_target  LABEL `photon-target`. Every face ratio = 1.0 +- 0.05,
//                         under a real sky AND under a flat ambient. It RUNS in
//                         every scoped selection and prints `target:` lines; it
//                         does not decide a gate (scripts/gate-scope.py splits
//                         the label out, the MERGE/PUSH tiers -LE it).
//
// TODAY'S MEASURED VALUES (this tree, 2026-09-22, RTX 4080 SUPER, all four
// ambients, every face of every arm):
//
//     the chain's faces           0.859 .. 1.234   (worst |ratio-1| = 0.234)
//     the single fitted volume    1.028 .. 1.057
//
//   and the shape of the error is not noise — it is three named mechanisms:
//     * cascade 0's face is the irradiance field's convention meeting the
//       cone's (1.128-1.204 under the 5 degree sun, 0.900-0.971 elsewhere);
//     * the 128^3 -> 64^3 cell-size jump in the tier's cascade table reads
//       0.859-0.948 at cascades 1 and 2;
//     * the outermost face, cascade 3 to the WORLD, is the 2-band hemisphere
//       pair meeting HlmsPbs's full 9 bands: 1.191-1.234, the largest of them.
//
// WHAT TURNS THE TARGET ROW GREEN: PHOTON P4 — ONE-READER (one `.any` piece for
// the voxel radiance read, so the pixel path, the bounce job, the field's Gen
// job and the ray hit cannot disagree), ONE-ENV (one sky cube and one cosine
// convolution for EVERY escape, which is the outermost face's whole mechanism)
// and F10-TABLES (the cell-size jump derived instead of tabulated). The lane
// that lands them deletes the `photon-target` label from this file's CMake row
// AND deletes the bracket from the ordinary row — removing the label is that
// part's acceptance.
//
// THE BAR, DERIVED. +-0.05 is not a taste: `gi.volume_edge` already asserts
// under 5 % for the single fitted volume under a flat ambient, i.e. 5 % is the
// number this renderer has already been shown to reach on the easy case, so it
// is what the hard case is held to. Below that lies the measurement's own
// floor: the profile bands are 16 columns of a 512-wide picture and the slab is
// dithered, so a face reads +-0.005 run to run.
// PHOTON-GATHER-1d: THE GATHER PINNED OFF. Since 1d the screen-probe gather is
// the diffuse at every ray tier (GiToggle::Auto resolves on at Medium and above);
// this suite measures the voxel chain / the field / the cones / the probes, which
// it pins, so its numbers stay about them. The gather has its own suites
// (gi.gather_*).
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
/// `--target` picks the TARGET row (gi.chain_face_target, label photon-target).
/// One binary, two rows: the measurements are identical and only the assertions
/// differ, so the two rows can never drift apart.
static bool gTarget = false;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

/// The ordinary row's assertions — skipped (but still printed) in the target row.
#define CHECK_ORDINARY(cond, msg)                                               \
    do {                                                                        \
        if (gTarget) break;                                                     \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

/// A TARGET line: the value and the bar, on every run of either row, so the
/// distance to the bar is visible from any lane's scoped gate. It only counts a
/// FAILURE in the target row.
#define TARGET(value, bar, what)                                                \
    do {                                                                        \
        const double v_ = double(value), b_ = double(bar);                      \
        const bool met_ = v_ <= b_;                                             \
        std::printf("target: %.4f (bar %.4f) %s%s\n", v_, b_, what,             \
                    met_ ? " -- MET" : "");                                    \
        if (gTarget) {                                                          \
            if (met_) std::printf("ok: %s\n", what);                            \
            else { std::printf("FAIL: %s\n", what); ++failures; }               \
        }                                                                       \
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

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--target") == 0) gTarget = true;
    std::printf("== gi.chain_face%s: %s\n", gTarget ? "_target" : "",
                gTarget ? "THE TARGET ROW (label photon-target) -- every face ratio 1.0 +- 0.05, "
                          "green after PHOTON P4 (ONE-READER + ONE-ENV + F10-TABLES)"
                        : "the ORDINARY row -- no regression while red, and the cascades follow "
                          "the camera");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = gTarget ? "test-gi-chain-face-target-ogre.log" : "test-gi-chain-face-ogre.log";
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
        // THE FLAT ARM'S SIGNAL (PHOTON-WRITER-1 fix round): the readback is 8-bit,
        // and at an ambient of 0.376 the band outside the single volume sat on a
        // flat 17/255 plateau where one code is 5.9 % - the 1.08 fence could not be
        // resolved there. At 2.2 it reads 103 codes (one code under 1 %) and the
        // same fence decides; nothing else about the arm changed (a ratio of one
        // uniform ambient does not depend on its level).
        { "flat",        false, 0.0f, Colour(2.2f, 2.2f, 2.2f),       Colour(2.2f, 2.2f, 2.2f) },
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
    // RE-ANCHORED BY SEAM-1 (2026-09-16, ogre-patch 0066: the cascade march
    // carries the cone's escape opacity and its age across a hop). Before it the
    // chain's faces ran 0.591-1.328 and there was one at EVERY cascade — a
    // staircase, a factor 1.9 from the far world to the eye. After it they run
    // 0.821-1.252 and only two are left: the cascade-0 face, which is now the
    // field's own convention meeting the cone's (1.000-1.003x where the two
    // ambient conventions coincide, 1.086-1.252x under a real sky, which is
    // audit B8's 2-band/9-band difference finally visible on its own), and the
    // 128^3 -> 64^3 cell-size jump in the tier's cascade table (0.821-0.839x),
    // which is a table question and not a march one. The bracket is tight around
    // those: a regression toward the old staircase reds.
    // RE-ANCHORED 1.05 -> 1.08 BY PHOTON-M2 (patch 0077), measured 1.057 here.
    // The specular cone's ESCAPE ambient used to be multiplied by 0.31831 =
    // 1/pi on its way into envColourS -- upstream's eye-tuned cancellation of
    // the light injection's missing 1/pi, which 0077 fixes at the cause -- so
    // the one ambient this engine has (setAmbient's single convention, ledger
    // 177 defect A) reached the specular slot pi times darker than it reached
    // the diffuse one. It now reaches both the same, and the face reads
    // 1.057x instead of 1.029x: +1.4/255 on a 24/255 band, inside the volume
    // where the escape is unoccluded. The bracket's purpose is a REGRESSION
    // toward the 1.9x staircase, and 1.08 keeps it.
    const float kSingleFaceMax = 1.08f;      // measured 1.007-1.057 over four ambients
    const float kChainFaceMax  = 1.35f;      // measured 0.821-1.252 over four ambients
    const float kChainFaceMin  = 0.75f;
    // THE TARGET. A face is a boundary in a data structure, not in the world:
    // the ambient on a flat slab under an unchanging sky is one number, so every
    // ratio is 1.000 and the bar is the deviation from it. 0.05 is the figure
    // `gi.volume_edge` already holds the EASY case to (the single fitted volume
    // under a flat ambient), so it is what the hard case is held to; the
    // measurement's own floor is +-0.005 (16-column bands, a dithered slab).
    const double kFaceTargetBar = 0.05;
    const auto faceTargetName = [](const char *what, size_t cascade) {
        static char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "the face of cascade %zu is a boundary in a data structure and not in the "
                      "world: |ratio - 1| under %.2f  [%s]", cascade, 0.05, what);
        return buf;
    };
    const auto measure = [&](const char *what, bool chain, const Amb &a, float camX, float orthoHalf) {
        r.camX = camX; r.orthoHalf = orthoHalf; placeCamera(r);
        GiParams gi;
        gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
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
                // THE TARGET: 1.000, both ways, under every ambient.
                TARGET(std::fabs(double(ratio) - 1.0), kFaceTargetBar,
                       faceTargetName(what, i));
                // ...and the bracket, which is what it always was: a fence
                // around a KNOWN artefact, kept only so a regression toward the
                // old 1.9x staircase reds while the target is red. DELETED by
                // the lane that removes the target row's label.
                //
                // THE LOW SUN'S CASCADE-0 FACE IS BACK IN THE BRACKET
                // (PHOTON-WRITER-1; it was the target row gi.chain_face_sky_target
                // since PHOTON-ENV-1 put the sky into the field at 1.67-1.78x). The
                // field's probe rays over-occluded GRAZING directions (a cone's
                // composite over a floor) exactly where a low sun's sky is
                // brightest; they now cross the voxels as rays, and a probe behind
                // the shaded point has no say: 1.08-1.19x.
                CHECK_ORDINARY(ratio > kChainFaceMin && ratio < kChainFaceMax,
                               "no regression while red: the chain's face steps no more than E3 "
                               "measured it stepping");
            }
        } else {
            const float face = st.boundsMax.x;
            const unsigned col = columnForX(r, face);
            float in = 0, out = 0;
            const float ratio = stepAt(p, col, in, out);
            std::printf("   single volume x %.2f .. %.2f  face col %u  inner %.4f outer %.4f  "
                        "step %.3fx (%+.1f/255)\n", st.boundsMin.x, st.boundsMax.x, col, in, out,
                        ratio, (in - out) * 255.0f);
            // The single fitted volume has the same target — it is the same
            // claim about the same physics, on the arm that is already closest.
            TARGET(std::fabs(double(ratio) - 1.0), kFaceTargetBar,
                   "the SINGLE fitted volume's edge is not a boundary in the world either: "
                   "|ratio - 1| under 0.05");
            CHECK_ORDINARY(ratio > 1.0f / kSingleFaceMax && ratio < kSingleFaceMax,
                           "no regression while red: the SINGLE fitted volume's edge does not "
                           "step, under a real sky either");
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
            // A CORRECT CLAIM, not a target: the chain is camera-centred by
            // design and nothing in P4 changes that. It gates on the ordinary
            // row and survives the lane that deletes the bracket.
            CHECK_ORDINARY(followed,
                           "every cascade sits within one step of the camera, so its faces "
                           "travel with the eye rather than staying in the world");
        }
        // ...and the inner faces, at 5x the magnification.
        measure((std::string(a.name) + ", chain, cam x=0, +-20 m").c_str(), true, a, 0.0f, 20.0f);
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
