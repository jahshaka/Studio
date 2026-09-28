// THE 2D CLOUD LAYER'S PHYSICS (CLOUDS-2D-3; the owner 2026-09-28: "they look all
// the same, hard edges, and they brighten the scene"). The verb, the document, the
// capture cadence and the ground shadow's strength are pool.world_sky's clouds_2d
// arm and gi.card_clouds; this file holds the two claims those cannot make:
//
//   energy — A SHEET NEVER ADDS LIGHT. The ground under a full deck, at every sun
//            height and every density, receives at most what it receives under a
//            clear sky: the deck takes the direct beam away and gives back only
//            what it transmits diffusely. Under a PARTIAL deck the ground's mean
//            over the whole tile (the sun patches and the shade in the proportion
//            the field holds them) is at most the clear sky's too, and a point in
//            a cloud's shadow is brighter than the same shadow under a full deck
//            (the gaps' sky and the lit cloud sides light it). Measured on a
//            matte ground in the plain grade, the sun dimmed so nothing clips,
//            GI off (the sheet's shadow on the bounce is gi.card_clouds' claim),
//            through the document and the mirror — the product's own path.
//   shape  — see caseShape below (CLOUDS-2D-3 (b)).
//
// NO BRIGHTNESS SLIDER exists or is used: the layer's radiance is the physics of
// the slab (JahCloudLayer_ps.glsl), and nothing here tunes it to a picture.
#include <QGuiApplication>

#include "bridge/previewmesh.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "irisgl/core/color.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {
constexpr unsigned kSize = 128;
// Dim enough that the clear ground under a zenith sun stays below the 8-bit ceiling.
float kSunIntensity = 1.0f;

struct Rig {
    std::unique_ptr<Engine> engine;
    View *view = nullptr;
    Scene *escene = nullptr;
    iris::ScenePtr doc;
    iris::LightNodePtr sun;
    std::unique_ptr<SceneMirror> mirror;
    iris::CameraNodePtr cam;

    bool make(const char *log) {
        std::string err;
        EngineConfig cfg;
        cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
        cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
        cfg.logFile = log;
        engine = Engine::create(cfg, err);
        if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return false; }
        engine->setFixedFrameDelta(1.0f / 60.0f);
        view = engine->createOffscreenView("clouds", kSize, kSize, Colour(0, 0, 0));
        if (!view) { std::printf("FAIL: no offscreen view\n"); return false; }
        view->setOffscreenContract(OffscreenContract::StillPicture);
        escene = engine->createScene("clouds");
        view->setScene(escene);

        doc = iris::Scene::create();
        doc->giMode = iris::GiMode::OFF;
        doc->skyType = iris::SkyType::REALISTIC;
        doc->sunDiscVisible = false;
        doc->fogEnabled = false;
        doc->hdrEnabled = false;

        // THE GROUND: a matte grey plane far wider than the view, nothing on it.
        auto ground = iris::MeshNode::create();
        ground->setName("ground");
        ground->setMesh(previewmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj")));
        ground->setLocalScale(iris::Vec3(400.0f, 1.0f, 400.0f));
        auto mat = iris::PbrMaterial::create();
        mat->setBaseColor(QColor(128, 128, 128));
        mat->setRoughnessFactor(1.0f);
        mat->setMetallicFactor(0.0f);
        ground->setMaterial(mat);
        doc->getRootNode()->addChild(ground);

        sun = iris::LightNode::create();
        sun->setName("Sun");
        sun->lightType = iris::LightType::Directional;
        sun->intensity = kSunIntensity;
        doc->getRootNode()->addChild(sun);

        auto skyLight = iris::LightNode::create();
        skyLight->setName("Sky Light");
        skyLight->lightType = iris::LightType::Sky;
        skyLight->intensity = 1.0f;
        doc->getRootNode()->addChild(skyLight);

        mirror.reset(new SceneMirror(escene));
        mirror->setLightWires(false);
        mirror->setSource(doc);
        // Straight down at the ground's origin from 3 m: the whole frame is ground.
        cam = iris::CameraNode::create();
        cam->setLocalPos(iris::Vec3(0.0f, 3.0f, 0.001f));
        cam->lookAt(iris::Vec3(0.0f, 0.0f, 0.0f));
        cam->update(0.0f);
        mirror->applyCamera(cam, view);
        return true;
    }
    void frame() {
        doc->refresh();
        mirror->sync();
        mirror->applySky(view);
        mirror->applyEnvironment(view, engine.get());
        engine->renderOneFrame();
    }
    // THE REALISTIC SKY'S BAKE IS DEBOUNCED (applySky, 150 ms of wall time — the
    // mirror's, not the engine's): let it expire, then frames until the captured
    // environment has landed.
    void settle() {
        for (int f = 0; f < 4; ++f) frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (int f = 0; f < 8; ++f) frame();
    }
    /// The sun at `elevation` degrees above the horizon, `azimuth` about +Y. A
    /// document light travels down its local -Y: pitch 0 is the zenith sun.
    void sunAt(float elevation, float azimuth) {
        sun->setLocalRot(iris::Quat::fromEulerAngles(-(90.0f - elevation), azimuth, 0.0f));
    }
    void clouds(bool on, float coverage, float density) {
        iris::CloudLayer c;
        c.enabled = on;
        c.coverage = coverage;
        c.density = density;
        c.speed = 0.0f;          // a still sheet: the scroll is exactly zero
        c.altitude = 2000.0f;
        c.shadow = 1.0f;
        doc->clouds = c;
    }
    /// The ground's mean (r + g + b) over the frame's central 64 x 64, in the
    /// PLAIN grade (hdrEnabled off: linear radiance, 8 bits, clipped at 1.0 —
    /// the sun is dimmed below so the clear ground sits well under it; the
    /// mirror pushes the view's chain from the document every frame, so the
    /// float readback's switch cannot be held on a mirror-driven view).
    double ground() {
        Image img;
        if (!view->readPixels(img)) return -1.0;
        double sum = 0.0, peak = 0.0;
        int n = 0;
        for (unsigned y = kSize / 4; y < 3 * kSize / 4; ++y)
            for (unsigned x = kSize / 4; x < 3 * kSize / 4; ++x) {
                const Colour c = img.at(x, y);
                sum += double(c.r) + double(c.g) + double(c.b);
                peak = std::max(peak, double(std::max(c.r, std::max(c.g, c.b))));
                ++n;
            }
        if (peak >= 0.99) std::printf("   WARNING: the ground clips (%.3f)\n", peak);
        return n ? sum / n : -1.0;
    }
};

/// tau at the tile point a WORLD point's sun ray crosses the sheet (the shadow's
/// own arithmetic, jah_cloud_shadow.glsl; the scroll is zero).
float tauAlongSun(const std::vector<float> &tau, unsigned n, float tile, const float toSun[3],
                  float altitude) {
    const float k = altitude / std::max(toSun[1], 0.02f);
    float u = toSun[0] * k / tile, v = toSun[2] * k / tile;
    u -= std::floor(u);
    v -= std::floor(v);
    const unsigned x = std::min(n - 1u, unsigned(u * float(n)));
    const unsigned y = std::min(n - 1u, unsigned(v * float(n)));
    return tau[size_t(y) * n + x];
}

/// THE GROUND'S IRRADIANCE, in the renderer's units, split in its two terms —
/// the physics claim is about IRRADIANCE, and the ground's pixel carries the
/// material's two different responses to it (HlmsPbs's Disney diffuse answers a
/// grazing sun and the ambient's hemisphere differently), so it is computed
/// from what the engine applies: the sky's ambient SH (the environment the sheet
/// is captured into; the Sky Light's gain is 1) evaluated for an up-facing
/// normal, times pi (Engine.h setAmbientSh: the sum is irradiance / pi), and the
/// sun's irradiance (CloudLayerDesc::sunIrradiance, the plate's units E0 checks)
/// times mu times the sheet's transmittance at the ground point's sun ray.
struct Irradiance {
    double ambient = 0.0, direct = 0.0;
    double total() const { return ambient + direct; }
};
Irradiance irradianceAt(Rig &r, const std::vector<float> *tau, unsigned n, float tile) {
    Irradiance e;
    float sh[27] = { 0.0f };
    if (!r.escene->skyAmbientSh(sh)) return e;
    for (int c = 0; c < 3; ++c)
        e.ambient += M_PI * (double(sh[0 * 3 + c]) + double(sh[1 * 3 + c]) - double(sh[6 * 3 + c]) -
                             double(sh[8 * 3 + c]));
    const SkyDesc sd = r.escene->sky();
    const double mu = std::max(0.0, double(sd.clouds.sunDir[1]));
    double t = 1.0;
    if (tau && !tau->empty())
        t = std::exp(-double(tauAlongSun(*tau, n, tile, sd.clouds.sunDir, sd.clouds.altitude)) /
                     std::max(mu, 0.02));
    e.direct = double(sd.clouds.sunIrradiance.r + sd.clouds.sunIrradiance.g + sd.clouds.sunIrradiance.b) *
               mu * t;
    return e;
}

int caseEnergy() {
    Rig r;
    if (!r.make("test-cloud-2d-energy-ogre.log")) return 1;
    // THE BAR: the SH is integrated from a 32^2 mip of the capture, so two skies
    // agree to its sampling, not to the float — one percent.
    const double kTol = 1.01;

    // ---- E0. THE SHEET'S SUN IS THE GROUND'S SUN -------------------------------
    // The layer is lit by CloudLayerDesc::sunIrradiance, "what a white Lambert
    // plate facing the sun reflects, times pi". Measured on the ground itself: a
    // BLACK sky (no sky light, no sheet light: coverage 0) and a zenith sun, so
    // the ground's radiance is its albedo times the plate's times HlmsPbs's
    // Disney diffuse at V = L = N (its energy factor, 1/1.51 at roughness 1) —
    // against the number the engine received. A pi between them lights the sheet
    // pi times brighter than the sun it stands in (x3.1 or x0.32 here).
    {
        r.doc->skyType = iris::SkyType::SINGLE_COLOR;
        r.doc->skyColor = QColor(0, 0, 0);
        r.clouds(true, 0.0f, 1.0f);
        r.sunAt(90.0f, 0.0f);
        r.settle();
        const double g = r.ground() / 3.0;
        const SkyDesc sd = r.escene->sky();
        const double albedo = std::pow((128.0 / 255.0 + 0.055) / 1.055, 2.4);
        const double disney = 1.0 / 1.51;
        const double plate = double(sd.clouds.sunIrradiance.r + sd.clouds.sunIrradiance.g +
                                    sd.clouds.sunIrradiance.b) / (3.0 * M_PI);
        const double ratio = g / (albedo * disney * plate);
        std::printf("   E0: ground %.4f under a zenith sun on a black sky; the sheet's plate %.4f x "
                    "albedo %.4f x Disney %.3f = %.4f (x%.3f)\n", g, plate, albedo, disney,
                    albedo * disney * plate, ratio);
        CHECK(sd.clouds.hasSun && ratio > 0.88 && ratio < 1.12,
              "E0. the sheet's sun irradiance is the ground's (a plate's radiance x pi)");
        r.doc->skyType = iris::SkyType::REALISTIC;
    }

    // ---- E1. A FULL DECK NEVER BRIGHTENS THE GROUND --------------------------
    const float elevations[] = { 10.0f, 35.0f, 65.0f, 88.0f };
    const float densities[] = { 0.03f, 0.1f, 0.3f, 1.0f };
    double worstRatio = 0.0;
    std::vector<float> tau;
    unsigned n = 0;
    float tile = 0.0f;
    for (float el : elevations) {
        r.sunAt(el, 30.0f);
        r.clouds(true, 0.0f, 1.0f);      // coverage 0 IS the clear sky (clouds_2d: byte-identical)
        r.settle();
        const Irradiance clear = irradianceAt(r, nullptr, 0, 0.0f);
        const double clearPx = r.ground();
        CHECK(clear.total() > 0.0, "the clear sky's irradiance reads back");
        for (float d : densities) {
            r.clouds(true, 1.0f, d);
            r.settle();
            CHECK(r.escene->cloudField(tau, n, tile) && n > 0, "the deck's field reads back");
            const Irradiance deck = irradianceAt(r, &tau, n, tile);
            const double ratio = deck.total() / clear.total();
            worstRatio = std::max(worstRatio, ratio);
            std::printf("   sun %4.0f deg, full deck density %.2f: irradiance %.4f (sky %.4f + sun %.4f) vs "
                        "clear %.4f (sky %.4f + sun %.4f) x%.3f; ground pixel %.4f vs %.4f\n",
                        el, d, deck.total(), deck.ambient, deck.direct, clear.total(), clear.ambient,
                        clear.direct, ratio, r.ground(), clearPx);
            char msg[160];
            std::snprintf(msg, sizeof msg,
                          "E1. a full deck (density %.2f) at a %.0f deg sun leaves the ground's irradiance at most the clear sky's",
                          d, el);
            CHECK(deck.total() <= clear.total() * kTol, msg);
        }
    }
    std::printf("   E1 worst deck/clear irradiance ratio %.4f\n", worstRatio);

    // ---- E2/E3. A PARTIAL DECK ------------------------------------------------
    // The ground point stays at the origin; the SUN's azimuth moves the point of
    // the field its ray crosses (a 35 deg sun throws it 2.9 km sideways at a 2 km
    // sheet), so one field gives a sun patch and a deep shadow without moving
    // the camera off its ground.
    const float el = 35.0f;
    r.clouds(true, 0.5f, 1.0f);
    r.sunAt(el, 0.0f);
    r.settle();
    CHECK(r.escene->cloudField(tau, n, tile) && n > 0, "the half deck's field reads back");
    if (tau.empty()) return 1;
    float bestSun = -1.0f, bestShade = -1.0f, shadeTau = 0.0f;
    for (int a = 0; a < 360; a += 3) {
        r.sunAt(el, float(a));
        r.frame();
        const SkyDesc sd = r.escene->sky();
        const float t = tauAlongSun(tau, n, tile, sd.clouds.sunDir, sd.clouds.altitude);
        if (t == 0.0f && bestSun < 0.0f) bestSun = float(a);
        if (t > shadeTau) { shadeTau = t; bestShade = float(a); }
    }
    CHECK(bestSun >= 0.0f && bestShade >= 0.0f && shadeTau > 4.0f,
          "a half deck has a sun patch and a deep shadow on the sun's circle");
    const double muS = std::sin(double(el) * M_PI / 180.0);
    double meanT = 0.0, cover = 0.0;
    for (float t : tau) {
        meanT += std::exp(-double(t) / muS);
        cover += t > 0.0f ? 1.0 : 0.0;
    }
    meanT /= double(tau.size());
    cover /= double(tau.size());
    std::printf("   half deck: cloud over %.1f %% of the tile, the beam's mean transmittance %.3f; "
                "sun patch at azimuth %.0f, shadow (tau %.1f) at %.0f\n",
                cover * 100.0, meanT, bestSun, shadeTau, bestShade);

    r.sunAt(el, bestShade);
    r.settle();
    const Irradiance shade = irradianceAt(r, &tau, n, tile);
    const double shadePx = r.ground();
    r.sunAt(el, bestSun);
    r.settle();
    const Irradiance sunPatch = irradianceAt(r, &tau, n, tile);
    const double sunPx = r.ground();
    // THE TILE'S MEAN: the sky light every point shares plus the sun carried by
    // the field's mean transmittance.
    const double tileMean = sunPatch.ambient + (sunPatch.direct / 1.0) * meanT;
    r.clouds(true, 0.0f, 1.0f);
    r.settle();
    const Irradiance clear = irradianceAt(r, nullptr, 0, 0.0f);
    const double clearPx = r.ground();
    r.sunAt(el, bestShade);
    r.clouds(true, 1.0f, 1.0f);
    r.settle();
    std::vector<float> tauFull;
    unsigned nFull = 0;
    float tileFull = 0.0f;
    CHECK(r.escene->cloudField(tauFull, nFull, tileFull), "the full deck's field reads back");
    const Irradiance full = irradianceAt(r, &tauFull, nFull, tileFull);
    const double fullPx = r.ground();
    std::printf("   half deck at a %.0f deg sun: irradiance in a sun patch %.4f, in a shadow %.4f, the "
                "tile's mean %.4f; clear %.4f; the same shadow under a full deck %.4f\n"
                "   (ground pixels: sun patch %.4f, shadow %.4f, clear %.4f, full-deck shadow %.4f)\n",
                el, sunPatch.total(), shade.total(), tileMean, clear.total(), full.total(),
                sunPx, shadePx, clearPx, fullPx);
    CHECK(sunPatch.total() > shade.total(), "E2. the sun patch is brighter than the shadow");
    CHECK(tileMean <= clear.total() * kTol,
          "E2. a half deck's ground irradiance, averaged over the tile, is at most the clear sky's");
    CHECK(shade.total() > full.total() * 1.05,
          "E3. a cloud's shadow under a half deck is brighter than under a full deck (the gaps light it)");
    std::printf("   the sun patch against the clear sky: x%.3f (the lit cloud sides add sky light "
                "where the beam is not blocked: stated, not bounded — the TILE is)\n",
                sunPatch.total() / clear.total());
    return 0;
}
}  // namespace

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const std::string which = argc > 1 ? argv[1] : "energy";
    if (argc > 2) kSunIntensity = float(std::atof(argv[2]));
    int rc = 1;
    if (which == "energy") rc = caseEnergy();
    else { std::printf("FAIL: unknown case '%s'\n", which.c_str()); return 2; }
    if (rc) return rc;
    std::printf(failures ? "cloud_2d.%s: FAIL (%d)\n" : "cloud_2d.%s: PASS\n", which.c_str(), failures);
    return failures ? 1 : 0;
}
