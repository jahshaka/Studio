// gi.probe_nan — PROBE-NAN-1: A BRIGHT LIGHT CLOSE TO A SURFACE IS BRIGHT, NOT INFINITE.
//
// THE DEFECT (PHOTON-VIEW-1's finding, reproduced by D4-PHOTON-TIERS on the rig,
// spikes/d4-photon-tiers/probe-nan): a point light of intensity 20 placed 0.3 m above the
// default ground turned EVERY probe of the irradiance field black in the Probes view — the
// probes a hundred metres away included — while intensity 2 left them lit. Every probe at
// once is the signature of a non-finite value, not of an exposure: a single Inf/NaN in a
// quantity the whole field shares.
//
// WHAT IT WAS (measured here): NOT a non-finite value. The field is finite at every
// intensity and correct; the atlas holds it in the voxels' STORED units — radiance over the
// decode multiplier D_max / pi, D_max the brightest light — and the Probes view shaded each
// sphere with the RAW texel, so a brighter lamp shrank every stored value and the view went
// black while the picture did not. The fix is at that source (fork commit, IrradianceField /
// IfdProbeVisualizer::setColourScale): the view shades a probe in the unit the pixel decodes.
//
// THE CLAIMS, as physics: (1) the field is FINITE in every texel (tested on the BITS —
// `x == x` is not a NaN test on this stack); (2) a brighter light never makes the DECODED
// field darker (stored / GiVoxelStats::multiplier, cascade 0's unit). The VIEW's own claim —
// the probes never darken as the lamp brightens — is the app's (the Probes view draws over
// the editor's graded picture): gi_verbs.probe_nan_view.
//
// Its own binary like every GI suite.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// An IEEE half from its bits; `finite` false for an Inf or NaN (exponent all ones).
static float halfBits(uint16_t h, bool &finite)
{
    const uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, man = h & 0x3FFu;
    finite = exp != 0x1Fu;
    if (!finite) return 0.0f;
    float v = exp == 0 ? std::ldexp(float(man), -24) : std::ldexp(float(man | 0x400u), int(exp) - 25);
    return sign ? -v : v;
}

struct FieldRead { bool ok = false; unsigned texels = 0, nonFinite = 0; double mean = 0.0, decoded = 0.0; };

static FieldRead readField(Scene *s)
{
    FieldRead r;
    GiFieldAtlas a;
    if (!s->giFieldAtlas(a) || !a.available || a.irradBytesPerTexel != 8u) return r;
    const size_t n = a.irradiance.size() / 8u;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        uint16_t c[4];
        std::memcpy(c, &a.irradiance[i * 8u], 8u);
        bool f0, f1, f2, f3;
        const float rr = halfBits(c[0], f0), gg = halfBits(c[1], f1), bb = halfBits(c[2], f2);
        halfBits(c[3], f3);
        if (!(f0 && f1 && f2 && f3)) { ++r.nonFinite; continue; }
        sum += (rr + gg + bb) / 3.0;
    }
    r.ok = true;
    r.texels = unsigned(n);
    r.mean = n ? sum / double(n) : 0.0;
    return r;
}

int main()
{
    std::printf("== gi.probe_nan: a bright light near a surface leaves the field finite and lit\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-probe-nan-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("probe-nan", 128, 128, Colour(0, 0, 0));
    Scene *scene = e->createScene("probe-nan");
    view->setScene(scene);
    scene->setAmbient(Colour(0.2f, 0.2f, 0.25f), Colour(0.1f, 0.1f, 0.1f));
    // THE DEFAULT GROUND's shape: a 100 m matte slab.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(100.0f, 0.1f, 100.0f));
    const NodeId cube = enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, cube, Vec3(1.5f, 0.5f, -1.0f));
    const NodeId lamp = scene->createNode();
    scene->setNodeTransform(lamp, Vec3(0.0f, 0.3f, 0.0f), Quat(), Vec3(1, 1, 1));
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(3.0f, 2.5f, 3.0f), Vec3(0.0f, 0.3f, 0.0f)));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    gi.numBounces = 3;
    gi.ddgi = GiToggle::On;
    gi.gather = GiToggle::Off;       // the field's probes are the subject
    gi.updateBudget = 0;             // converge inline: every read is a whole field
    CHECK(scene->setGlobalIllumination(gi), "the chain with the field");

    FieldRead reads[3];
    const float intensities[3] = { 2.0f, 20.0f, 200.0f };
    for (int k = 0; k < 3; ++k) {
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1, 1, 1);
        l.intensity = intensities[k];
        l.range = 10.0f;
        l.castShadows = true;
        scene->setLight(lamp, l);
        render(e, 30);
        scene->refreshGlobalIllumination();
        render(e, 30);
        reads[k] = readField(scene);
        const GiVoxelStats vs = scene->giVoxelStats(0);
        if (vs.available && vs.multiplier > 0.0f)
            reads[k].decoded = reads[k].mean / double(vs.multiplier);
        std::printf("   intensity %6.1f at 0.3 m: %u texels, %u non-finite, stored mean %.5f, decoded "
                    "%.5f\n",
                    double(intensities[k]), reads[k].texels, reads[k].nonFinite, reads[k].mean,
                    reads[k].decoded);
    }
    for (int k = 0; k < 3; ++k) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "intensity %.0f: the field reads back", double(intensities[k]));
        CHECK(reads[k].ok && reads[k].texels > 0, msg);
        std::snprintf(msg, sizeof(msg),
                      "intensity %.0f at 0.3 m: EVERY field texel is finite (tested on the bits)",
                      double(intensities[k]));
        CHECK(reads[k].nonFinite == 0, msg);
    }
    CHECK(reads[0].decoded > 0.0 && reads[1].decoded >= reads[0].decoded &&
              reads[2].decoded >= reads[1].decoded,
          "a brighter light never makes the DECODED field darker (2 -> 20 -> 200)");


    GiParams off;
    scene->setGlobalIllumination(off);
    render(e, 2);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
