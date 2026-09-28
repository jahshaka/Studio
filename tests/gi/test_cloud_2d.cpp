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
//   shape  — THE FIELD'S CHARACTER: many cloud sizes, an edge that is a ramp in
//            kilometres, a period the eye cannot find (caseShape says the metrics).
//
// NO BRIGHTNESS SLIDER exists or is used: the layer's radiance is the physics of
// the slab (JahCloudLayer_ps.glsl), and nothing here tunes it to a picture.
#include <QGuiApplication>
#include <QImage>

#include "bridge/previewmesh.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "irisgl/core/color.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/texture2d.h"
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
    double beam = 0.0;   // the sun's irradiance on the ground with no sheet in its way
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
    e.beam = double(sd.clouds.sunIrradiance.r + sd.clouds.sunIrradiance.g + sd.clouds.sunIrradiance.b) * mu;
    e.direct = e.beam * t;
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
    float bestSun = -1.0f, bestShade = -1.0f, shadeTau = 0.0f, sunTau = 1e9f;
    for (int a = 0; a < 360; a += 3) {
        r.sunAt(el, float(a));
        r.frame();
        const SkyDesc sd = r.escene->sky();
        const float t = tauAlongSun(tau, n, tile, sd.clouds.sunDir, sd.clouds.altitude);
        if (t < sunTau) { sunTau = t; bestSun = float(a); }
        if (t > shadeTau) { shadeTau = t; bestShade = float(a); }
    }
    // A SUN PATCH passes 95 % of the beam at this sun; a SHADOW under 1 %.
    CHECK(bestSun >= 0.0f && bestShade >= 0.0f && sunTau < 0.03f && shadeTau > 2.7f,
          "a half deck has a sun patch and a deep shadow on the sun's circle");
    const double muS = std::sin(double(el) * M_PI / 180.0);
    double meanT = 0.0, cover = 0.0;
    for (float t : tau) {
        meanT += std::exp(-double(t) / muS);
        cover += t > 0.05f ? 1.0 : 0.0;
    }
    meanT /= double(tau.size());
    cover /= double(tau.size());
    std::printf("   half deck: cloud over %.1f %% of the tile, the beam's mean transmittance %.3f; "
                "sun patch (tau %.3f) at azimuth %.0f, shadow (tau %.1f) at %.0f\n",
                cover * 100.0, meanT, sunTau, bestSun, shadeTau, bestShade);

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
    const double tileMean = sunPatch.ambient + sunPatch.beam * meanT;
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
/// A DISTANCE MAP on the wrapped tile: metres from every texel to the nearest
/// texel of `seed` (a two-pass 8-neighbour chamfer, 1 and sqrt 2 texels — within
/// 8 % of the Euclidean distance, stated), iterated until it stops moving because
/// the tile wraps.
std::vector<float> chamfer(const std::vector<char> &seed, unsigned n, float texel) {
    const float kInf = 1e30f, d1 = texel, d2 = texel * 1.41421356f;
    std::vector<float> d(size_t(n) * n);
    for (size_t i = 0; i < d.size(); ++i) d[i] = seed[i] ? 0.0f : kInf;
    const auto at = [&](int x, int y) -> float & {
        x = (x % int(n) + int(n)) % int(n);
        y = (y % int(n) + int(n)) % int(n);
        return d[size_t(y) * n + size_t(x)];
    };
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (int y = 0; y < int(n); ++y)
            for (int x = 0; x < int(n); ++x) {
                float &v = at(x, y);
                const float m = std::min({ v, at(x - 1, y) + d1, at(x, y - 1) + d1,
                                           at(x - 1, y - 1) + d2, at(x + 1, y - 1) + d2 });
                if (m < v) { v = m; moved = true; }
            }
        for (int y = int(n) - 1; y >= 0; --y)
            for (int x = int(n) - 1; x >= 0; --x) {
                float &v = at(x, y);
                const float m = std::min({ v, at(x + 1, y) + d1, at(x, y + 1) + d1,
                                           at(x + 1, y + 1) + d2, at(x - 1, y + 1) + d2 });
                if (m < v) { v = m; moved = true; }
            }
        if (!moved) break;
    }
    return d;
}

// shape — THE FIELD'S CHARACTER (CLOUDS-2D-3 (b); the owner: "they look all the
// same, hard edges"), read off the baked tile itself at the shipped defaults
// (coverage 0.5, density 1):
//   S1. NO SINGLE CLOUD: the clouds (connected regions of a visibly opaque
//       column, tau >= 1, wrapped, at least 250 m across) binned by their
//       equivalent diameter in octaves — the fullest octave holds at most half
//       of them, the 90th percentile diameter is at least four times the 10th —
//       and by their peak depth: a spread (CV >= 0.25), at most half of them at
//       the thickest one's core;
//   S2. THE EDGE IS A RAMP IN KILOMETRES: no texel within 1 km of clear sky
//       (tau <= 0.05) holds half a full column's optical depth (the 1st
//       percentile of the distance at which a column first reaches half is
//       >= 1 km; its minimum is printed);
//   S3. THE PERIOD IS HIDDEN: the tile is at least the distance the far sheet
//       fades over (60 km), and two 16 km windows of it (the old tile's size)
//       differ — their normalised correlation below 0.5.
int caseShape() {
    Rig r;
    if (!r.make("test-cloud-2d-shape-ogre.log")) return 1;
    r.clouds(true, 0.5f, 1.0f);
    r.sunAt(50.0f, 0.0f);
    r.settle();
    std::vector<float> tau;
    unsigned n = 0;
    float tile = 0.0f;
    CHECK(r.escene->cloudField(tau, n, tile) && n > 0, "the field reads back");
    if (tau.empty()) return 1;
    const float texel = tile / float(n);
    float tauMax = 0.0f;
    double cover = 0.0;
    for (float t : tau) { tauMax = std::max(tauMax, t); cover += t > 0.05f ? 1.0 : 0.0; }
    cover /= double(tau.size());
    // A full column's depth at density 1: OgreSky.cpp kCloudTauFull.
    const float kFull = 32.0f;
    std::printf("   field %u^2 over %.1f km (%.1f m a texel); cloud over %.1f %% of it; peak tau %.2f\n",
                n, tile / 1000.0f, texel, cover * 100.0, tauMax);

    // ---- S0: the coverage dial still means coverage ----
    {
        const float dials[] = { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f };
        std::string line = "   S0: the cloud-covered fraction (tau > 0.05) at coverage";
        double prev = 0.0;
        bool rising = true;
        for (float cv : dials) {
            double c2 = cover;
            if (cv != 0.5f) {
                r.clouds(true, cv, 1.0f);
                r.settle();
                std::vector<float> t2;
                unsigned n2 = 0;
                float tl2 = 0.0f;
                r.escene->cloudField(t2, n2, tl2);
                c2 = 0.0;
                for (float t : t2) c2 += t > 0.05f ? 1.0 : 0.0;
                c2 /= double(std::max<size_t>(1, t2.size()));
            }
            char buf[48];
            std::snprintf(buf, sizeof buf, " %.2f -> %.1f %%;", cv, c2 * 100.0);
            line += buf;
            rising = rising && c2 >= prev;
            prev = c2;
        }
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        CHECK(rising, "S0. more coverage covers more of the tile");
        CHECK(cover >= 0.3 && cover <= 0.65, "S0. coverage 0.5 covers between 30 and 65 % of the tile");
        r.clouds(true, 0.5f, 1.0f);
        r.settle();
    }

    // ---- S1 ----
    std::vector<int> label(size_t(n) * n, -1);
    std::vector<double> areas;
    std::vector<size_t> stack;
    for (size_t i0 = 0; i0 < tau.size(); ++i0) {
        if (tau[i0] < 1.0f || label[i0] >= 0) continue;
        const int id = int(areas.size());
        size_t area = 0;
        stack.assign(1, i0);
        label[i0] = id;
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            ++area;
            const int x = int(i % n), y = int(i / n);
            const int nb[4][2] = { { x + 1, y }, { x - 1, y }, { x, y + 1 }, { x, y - 1 } };
            for (const auto &q : nb) {
                const size_t j = size_t((q[1] + int(n)) % int(n)) * n + size_t((q[0] + int(n)) % int(n));
                if (tau[j] >= 1.0f && label[j] < 0) { label[j] = id; stack.push_back(j); }
            }
        }
        areas.push_back(double(area));
    }
    // Each cloud's own peak depth, for the thickness half of the claim.
    std::vector<float> peak(areas.size(), 0.0f);
    for (size_t i = 0; i < tau.size(); ++i)
        if (label[i] >= 0) peak[size_t(label[i])] = std::max(peak[size_t(label[i])], tau[i]);
    std::vector<double> diam;
    std::vector<double> peaks;
    for (size_t k = 0; k < areas.size(); ++k) {
        const double dk = 2.0 * std::sqrt(areas[k] / M_PI) * double(texel) / 1000.0;
        // A CLOUD is at least 250 m across (7 degrees overhead at the default
        // 2 km): the eroded margin's specks are not clouds of their own.
        if (dk < 0.25) continue;
        diam.push_back(dk);
        peaks.push_back(double(peak[k]));
    }
    std::sort(diam.begin(), diam.end());
    int bins[16] = { 0 };
    for (double dk : diam) bins[std::min(15, std::max(0, int(std::floor(std::log2(dk / 0.125)))))]++;
    int fullest = 0;
    for (int b : bins) fullest = std::max(fullest, b);
    const double p10 = diam.empty() ? 0.0 : diam[diam.size() / 10];
    const double p90 = diam.empty() ? 0.0 : diam[diam.size() * 9 / 10];
    std::printf("   S1: %zu clouds; diameters p10 %.2f km, median %.2f km, p90 %.2f km; octaves from 0.125 km:",
                diam.size(), p10, diam.empty() ? 0.0 : diam[diam.size() / 2], p90);
    for (int b = 0; b < 10; ++b) std::printf(" %d", bins[b]);
    std::printf("\n");
    CHECK(diam.size() >= 10, "S1. the tile holds clouds to measure");
    CHECK(!diam.empty() && double(fullest) <= 0.5 * double(diam.size()),
          "S1. no single octave of cloud size holds more than half the clouds");
    CHECK(p10 > 0.0 && p90 >= 4.0 * p10, "S1. the large clouds are at least four times the small ones (p90 / p10)");
    // ...AND OF THEIR OWN THICKNESS: the clouds' peak depths spread (their
    // coefficient of variation >= 0.25) and at most half of them sit within 5 %
    // of the thickest — a field whose every cloud saturates to one core is the
    // "all the same" the owner saw.
    double pm = 0.0, pv = 0.0, pmax = 0.0;
    for (double pk : peaks) { pm += pk; pmax = std::max(pmax, pk); }
    pm /= std::max<size_t>(1, peaks.size());
    for (double pk : peaks) pv += (pk - pm) * (pk - pm);
    const double cv = peaks.size() > 1 ? std::sqrt(pv / double(peaks.size() - 1)) / std::max(pm, 1e-9) : 0.0;
    size_t atMax = 0;
    for (double pk : peaks) atMax += pk >= 0.95 * pmax ? 1u : 0u;
    std::printf("   S1: the clouds' peak depths: mean %.2f, CV %.3f, %zu of %zu within 5 %% of the thickest (%.2f)\n",
                pm, cv, atMax, peaks.size(), pmax);
    CHECK(cv >= 0.25, "S1. the clouds differ in thickness (peak-depth CV >= 0.25)");
    CHECK(2 * atMax <= peaks.size(), "S1. at most half the clouds reach the thickest one's core");

    // ---- S2 ----
    std::vector<char> clearSeed(tau.size());
    for (size_t i = 0; i < tau.size(); ++i) clearSeed[i] = tau[i] <= 0.05f;
    const std::vector<float> dist = chamfer(clearSeed, n, texel);
    std::vector<float> rise;
    for (size_t i = 0; i < tau.size(); ++i)
        if (tau[i] >= 0.5f * kFull) rise.push_back(dist[i]);
    std::sort(rise.begin(), rise.end());
    const float riseMin = rise.empty() ? 0.0f : rise.front();
    const float riseP1 = rise.empty() ? 0.0f : rise[rise.size() / 100];
    std::printf("   S2: %zu texels hold half a full column; their distance to clear sky: min %.0f m, "
                "1st percentile %.0f m, median %.0f m\n",
                rise.size(), riseMin, riseP1, rise.empty() ? 0.0f : rise[rise.size() / 2]);
    CHECK(!rise.empty(), "S2. some columns reach half a full column");
    CHECK(riseP1 >= 1000.0f, "S2. a column reaches half a full column no nearer than 1 km to clear sky (1st percentile)");

    // ---- S3 ----
    const unsigned w = std::max(1u, unsigned(std::lround(16000.0f / texel)));
    double ncc = 1.0;
    if (w * 2 <= n) {
        double ma = 0, mb = 0;
        for (unsigned y = 0; y < w; ++y)
            for (unsigned x = 0; x < w; ++x) { ma += tau[size_t(y) * n + x]; mb += tau[size_t(y) * n + x + w]; }
        ma /= double(w) * w;
        mb /= double(w) * w;
        double sab = 0, saa = 0, sbb = 0;
        for (unsigned y = 0; y < w; ++y)
            for (unsigned x = 0; x < w; ++x) {
                const double a = tau[size_t(y) * n + x] - ma, b = tau[size_t(y) * n + x + w] - mb;
                sab += a * b; saa += a * a; sbb += b * b;
            }
        ncc = saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 1.0;
    }
    std::printf("   S3: the tile %.1f km; two adjacent 16 km windows correlate %.3f\n", tile / 1000.0f, ncc);
    CHECK(tile >= 60000.0f, "S3. the field's period is at least the distance the far sheet fades over (60 km)");
    CHECK(std::fabs(ncc) < 0.5, "S3. two 16 km windows of the field differ");

    // ---- S4: THE WEATHER MAP HOLDS AT EVERY COVERAGE (the fix round's D1) ----
    // A map black over its left half keeps that half clear even at coverage 1,
    // where the overcast deck closes everything else: the deck reads the map too.
    {
        QImage map(256, 256, QImage::Format_RGBA8888);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                map.setPixelColor(x, y, x < 128 ? QColor(0, 0, 0) : QColor(255, 255, 255));
        const QString path = QStringLiteral("cloud_2d_halfmap.png");
        CHECK(map.save(path), "S4. the half-black weather map is written");
        r.clouds(true, 1.0f, 1.0f);
        r.doc->clouds.weatherMapGuid = QStringLiteral("cloud-2d-halfmap");
        r.doc->cloudWeatherMap = iris::Texture2D::load(path);
        r.settle();
        std::vector<float> t4;
        unsigned n4 = 0;
        float tile4 = 0.0f;
        CHECK(r.escene->cloudField(t4, n4, tile4) && n4 > 0, "S4. the mapped deck's field reads back");
        // Margins: the footprint's blur (0.6 km sigma, 3 sigma = 0.028 of the tile)
        // and the map's own bilinear edge — the middle of each half is measured.
        float blackMax = 0.0f, whiteMin = 1e30f;
        for (unsigned y = 0; y < n4; ++y)
            for (unsigned x = 0; x < n4; ++x) {
                const float u = (float(x) + 0.5f) / float(n4);
                const float t = t4[size_t(y) * n4 + x];
                if (u > 0.06f && u < 0.44f) blackMax = std::max(blackMax, t);
                if (u > 0.56f && u < 0.94f) whiteMin = std::min(whiteMin, t);
            }
        std::printf("   S4: coverage 1 under a half-black map: tau over the black half at most %.4f, over the "
                    "white half at least %.3f\n", blackMax, whiteMin);
        CHECK(blackMax == 0.0f, "S4. the map's black half is clear sky at coverage 1 (tau 0, the deck included)");
        CHECK(whiteMin > 1.0f, "S4. ...and its white half is the overcast deck");
        r.doc->clouds.weatherMapGuid.clear();
        r.doc->cloudWeatherMap.reset();
    }
    return 0;
}
/// NOT A ROW: `test_cloud_2d look <dir>` writes what the sheet looks like from the
/// ground (960x540, the plain grade, the view up at 20 degrees, sun ahead at 25 and
/// behind at 60 degrees, coverage 0.3 / 0.5 / 0.8) for a human's read.
int caseLook(const char *dir) {
    Rig r;
    if (!r.make("test-cloud-2d-look-ogre.log")) return 1;
    r.view->resize(960, 540);
    r.cam->setLocalPos(iris::Vec3(0.0f, 2.0f, 0.0f));
    r.cam->lookAt(iris::Vec3(0.0f, 2.0f + std::tan(20.0f * float(M_PI) / 180.0f), -1.0f));
    r.cam->update(0.0f);
    r.mirror->applyCamera(r.cam, r.view);
    const float covers[] = { 0.3f, 0.5f, 0.8f };
    const float suns[2][2] = { { 25.0f, 180.0f }, { 60.0f, 0.0f } };
    for (const auto &sun : suns)
        for (float cv : covers) {
            r.sunAt(sun[0], sun[1]);
            r.clouds(true, cv, 1.0f);
            r.settle();
            Image img;
            if (!r.view->readPixels(img)) continue;
            QImage q(img.rgba.data(), int(img.width), int(img.height), QImage::Format_RGBA8888);
            char name[256];
            std::snprintf(name, sizeof name, "%s/clouds_sun%02.0f_cov%02.0f.png", dir, sun[0], cv * 100.0f);
            q.copy().save(QString::fromUtf8(name));
            std::printf("   wrote %s\n", name);
        }
    return 0;
}
}  // namespace

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const std::string which = argc > 1 ? argv[1] : "energy";
    if (argc > 2 && which != "look") kSunIntensity = float(std::atof(argv[2]));
    int rc = 1;
    if (which == "energy") rc = caseEnergy();
    else if (which == "shape") rc = caseShape();
    else if (which == "look" && argc > 2) return caseLook(argv[2]);
    else { std::printf("FAIL: unknown case '%s'\n", which.c_str()); return 2; }
    if (rc) return rc;
    std::printf(failures ? "cloud_2d.%s: FAIL (%d)\n" : "cloud_2d.%s: PASS\n", which.c_str(), failures);
    return failures ? 1 : 0;
}
