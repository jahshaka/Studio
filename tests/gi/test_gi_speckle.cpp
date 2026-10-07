// gi.speckle — THE GATHER'S HITS ARE DIFFUSE-ONLY ON EVERY ROUTE (SPECKLE-FIX-1;
// the owner's smoke, 2026-10-05: "speckly circular reflections flashing when the
// silver sphere moves"; the mechanism is spikes/speckle-1/NOTES.txt).
//
// THE DEFECT. A screen-probe gather ray that hits a glossy metal was answered two
// ways: by the CACHES (a card, a voxel: the diffuse response alone — albedo x
// irradiance, plus the emissive) or by the DECODE (a hit record shaded by
// HlmsAtom's hit mode — the full PBS lobe, the sun's GGX highlight included,
// seen from the reversed ray). A ray on the highlight returned hundreds x the
// sky, one hot probe was splatted as a disc across the floor and held for the
// history's frames, and a moved object's cards re-allocate, so the route
// alternated and the floor flashed. THE CHOICE (the owner's option 1, Lumen's):
// a gather hit is shaded DIFFUSE-ONLY on every route — the record carries the
// bit (jah_rq_hit_record.glsl kJahHitDiffuseOnlyBit) and the decode drops every
// specular term; the REFLECTION's records keep the full lobe.
//
// THE ARMS, one process, one fixture (Epic: VctPccHybrid, the gather, the cards,
// the full-resolution ray reflection; a silver PBR sphere — metallic 1,
// roughness 0.22 — under the sun and an atmosphere sky):
//   (a) THE CONTROLS: the sphere STILL, and a MATTE sphere MOVED, put no burst
//       on the floor band — the band sees neither the sphere, its shadow nor
//       its reflection, so a burst there is the gather's alone.
//   (b) THE SPECKLE: the silver sphere moved up and down for 60 frames (an
//       automatic-mobility object, its cards re-allocated as it moves): 0
//       bursts (the spike measured 21-23 of 59 on the base).
//   (c) THE FORCED DECODE: the same sphere MOVABLE (every hit on it a record):
//       0 bursts (49 of 59 on the base).
//   (d) ROUTE AGREEMENT: a static diffuse box's bounce onto the floor, the
//       gather's own irradiance read back, with every hit answered by the caches
//       (the shipped route) and with every hit DECODED (the arm
//       "gather.decodeHits"): the same quantity within the cards' tolerance.
//   (e) THE REFLECTION KEEPS THE LOBE: the silver sphere seen in a mirror floor
//       is its full specular picture (a diffuse-only metal would be black).
//
// THE BURST METRIC (the spike's, cap2.py / series.sh): per consecutive frame
// pair, the floor band's pixels whose display colour moved by more than 1.2 %
// (the RGB distance over sqrt(3), ImageMagick's fuzz); a pair is a BURST when
// more than 0.06 % of the band moved (the spike's roughness-1 control peaked
// at 0.025 %).
//
// `JAH_SPECKLE_DUMP=1` writes each moving arm's worst pair (speckle-<arm>-a.ppm,
// -b.ppm, and the x10 difference -d.ppm) into the working directory.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static const unsigned kW = 640, kH = 360;
/// The floor band: the bottom of the frame, the floor 2-5 m in front of the
/// sphere (the camera below), which neither the sphere nor its shadow reaches.
static const unsigned kBand0 = 280;
static const float kPi = 3.14159265358979323846f;

static MeshData sphereMesh(int seg, int ring, float r)
{
    MeshData d;
    for (int j = 0; j <= ring; ++j) {
        const float th = float(j) / float(ring) * kPi;
        for (int i = 0; i <= seg; ++i) {
            const float ph = float(i) / float(seg) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = -std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { r * x, r * y, r * z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(i) / float(seg), float(j) / float(ring) });
        }
    }
    for (int j = 0; j < ring; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = unsigned(j * (seg + 1) + i), b = a + 1u;
            const unsigned c = unsigned((j + 1) * (seg + 1) + i), f = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, f });
        }
    // The bake's card set for a convex mesh: six box cards on its bounds.
    d.cards = enginetest::boxCards(r);
    return d;
}

/// A pure diffuse surface (the default ground's recipe: specular workflow, ior 1,
/// black specular — F0 0).
static MaterialId matte(Scene *s, const Colour &albedo)
{
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return s->createPbrMaterial(p);
}

static MaterialId pbr(Scene *s, const Colour &albedo, float metal, float rough)
{
    PbrParams p;
    p.albedo = albedo;
    p.metalness = metal;
    p.roughness = rough;
    return s->createPbrMaterial(p);
}

static GiParams epicGi()
{
    // kPhotonTable's Epic row (src/services/worldmodes.cpp): the hybrid, Epic,
    // three bounces, the gather on, the cards (the default) resident over the set.
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Epic;
    gi.numBounces = 3;
    gi.gather = GiToggle::On;
    gi.cards = true;
    gi.cardResidencyRadius = 40.0f;
    // THE DOCUMENT'S DEFAULT (scene.cpp, MOVER-1: on): a dragged still object is
    // promoted onto the mover channel for the gesture — its hits are then the
    // decode's, and at rest the caches' again: the route the owner saw alternate.
    gi.dragMoverChannel = true;
    return gi;
}

static void setupView(View *view)
{
    view->setOffscreenContract(OffscreenContract::StillPicture);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;                  // Epic's full-resolution ray reflection
    view->setPostFx(fx);
    view->setShadows(true);
}

static void writePpm(const char *path, const Image &img, const Image *other = nullptr)
{
    FILE *f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
        for (int c = 0; c < 3; ++c) {
            int v = img.rgba[i * 4 + c];
            if (other) v = std::min(255, std::abs(v - int(other->rgba[i * 4 + c])) * 10);
            std::fputc(v, f);
        }
    std::fclose(f);
}

/// Pixels of the floor band whose display colour moved by more than 1.2 %.
static unsigned bandChanged(const Image &a, const Image &b)
{
    unsigned n = 0;
    for (unsigned y = kBand0; y < kH; ++y)
        for (unsigned x = 0; x < kW; ++x) {
            const size_t i = (size_t(y) * kW + x) * 4u;
            double d2 = 0.0;
            for (int c = 0; c < 3; ++c) {
                const double d = (double(a.rgba[i + c]) - double(b.rgba[i + c])) / 255.0;
                d2 += d * d;
            }
            if (std::sqrt(d2 / 3.0) > 0.012) ++n;
        }
    return n;
}

struct Fixture {
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *s = nullptr;
    NodeId floor = 0, sphere = 0;
    MeshId sphereMeshId = 0, cube = 0;
    MaterialId silver = 0, matteGrey = 0, floorMatte = 0, floorMirror = 0;
};

static bool build(Fixture &f)
{
    f.view = f.e->createOffscreenView("speckle", kW, kH, Colour(0, 0, 0));
    f.s = f.e->createScene("speckle");
    if (!f.view || !f.s) return false;
    f.view->setScene(f.s);
    setupView(f.view);
    // THE OWNER'S SCENE, rebuilt (spikes/speckle-1: Floor + sun + Sky Light, the
    // silver sphere r = 1 at y = 1.2, three 1 m cubes).
    const Vec3 sunDir(-0.35f, -1.0f, -0.5f);
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.atmosphere.hasSun = true;
    {
        const float l = std::sqrt(sunDir.x * sunDir.x + sunDir.y * sunDir.y + sunDir.z * sunDir.z);
        sky.atmosphere.sunDir[0] = -sunDir.x / l;
        sky.atmosphere.sunDir[1] = -sunDir.y / l;
        sky.atmosphere.sunDir[2] = -sunDir.z / l;
    }
    if (!f.s->setSky(sky)) return false;
    f.s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));
    enginetest::addDirectionalLight(f.s, sunDir, 3.0f);

    MeshData cubeMd = enginetest::unitCubeMesh();
    cubeMd.cards = enginetest::boxCards(0.5f);
    const MeshId cube = f.s->createMesh(cubeMd);
    f.cube = cube;
    f.floorMatte = matte(f.s, Colour(0.5f, 0.5f, 0.5f));
    f.floorMirror = pbr(f.s, Colour(0.95f, 0.95f, 0.95f), 1.0f, 0.02f);
    f.floor = f.s->createNode();
    if (!f.s->attachMesh(f.floor, cube, f.floorMatte)) return false;
    f.s->setNodeTransform(f.floor, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(40.0f, 0.5f, 40.0f));
    const struct { float x, z; Colour c; } cubes[3] = {
        { -2.6f, -1.2f, Colour(0.7f, 0.2f, 0.15f) },
        { 2.6f, -1.6f, Colour(0.2f, 0.45f, 0.7f) },
        { 2.2f, 0.9f, Colour(0.6f, 0.6f, 0.55f) },
    };
    for (const auto &c : cubes) {
        const NodeId n = f.s->createNode();
        if (!f.s->attachMesh(n, cube, matte(f.s, c.c))) return false;
        f.s->setNodeTransform(n, Vec3(c.x, 0.5f, c.z), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    }
    f.sphereMeshId = f.s->createMesh(sphereMesh(64, 32, 1.0f));
    f.silver = pbr(f.s, Colour(0.97f, 0.96f, 0.91f), 1.0f, 0.22f);
    f.matteGrey = matte(f.s, Colour(0.6f, 0.6f, 0.6f));
    f.sphere = f.s->createNode();
    if (!f.s->attachMesh(f.sphere, f.sphereMeshId, f.silver)) return false;
    f.s->setNodeTransform(f.sphere, Vec3(0.0f, 1.2f, 0.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 2.6f, 7.5f), Vec3(0.0f, 0.9f, 0.0f));
    return f.s->setGlobalIllumination(epicGi());
}

struct Run { int bursts = 0; int pairs = 0; unsigned long long sum = 0; unsigned worst = 0;
             unsigned long long records = 0; std::string series; };

/// 60 frames of the sphere at y = 1.2 + 0.6 sin(2 pi i / 40) (or still), each
/// frame read; the bursts counted over the 59 consecutive pairs.
static Run runArm(Fixture &f, const char *name, bool move, bool dump)
{
    f.s->setNodeTransform(f.sphere, Vec3(0.0f, 1.2f, 0.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    render(f.e, 90);   // the arm's own settle (a material or mobility change restarts the GI)
    const unsigned bandPx = (kH - kBand0) * kW;
    const unsigned burstPx = unsigned(std::ceil(0.0006 * bandPx));
    Run r;
    const GiStatus g0 = f.s->giStatus();
    Image prev, cur, worstA, worstB;
    for (int i = 0; i < 60; ++i) {
        const float y = move ? 1.2f + 0.6f * std::sin(float(i) * 2.0f * kPi / 40.0f) : 1.2f;
        f.s->setNodeTransform(f.sphere, Vec3(0.0f, y, 0.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
        render(f.e, 1);
        f.view->readPixels(cur);
        r.records += f.s->rayQueryStatus().hitRecords;
        if (i > 0) {
            const unsigned n = bandChanged(prev, cur);
            ++r.pairs;
            r.sum += n;
            if (n > burstPx) ++r.bursts;
            r.series += " " + std::to_string(n);
            if (n >= r.worst) { r.worst = n; worstA = prev; worstB = cur; }
        }
        std::swap(prev, cur);
    }
    const GiStatus g1 = f.s->giStatus();
    std::printf("   [%s] cards: %llu invalidated by a transform, %llu captures; cascade rebuilds %llu\n", name,
                g1.cards.invalidTransform - g0.cards.invalidTransform, g1.cards.captures - g0.cards.captures,
                (unsigned long long)(g1.rebuilds - g0.rebuilds));
    std::printf("   [%s] floor band %u px (burst > %u px): %d burst(s) of %d pairs, sum %llu, worst %u; "
                "hit records over the run %llu\n   series:%s\n",
                name, bandPx, burstPx, r.bursts, r.pairs, r.sum, r.worst, r.records, r.series.c_str());
    if (dump && worstA.width) {
        const std::string base = std::string("speckle-") + name;
        writePpm((base + "-a.ppm").c_str(), worstA);
        writePpm((base + "-b.ppm").c_str(), worstB);
        writePpm((base + "-d.ppm").c_str(), worstB, &worstA);
    }
    return r;
}

// (d) ROUTE AGREEMENT ----------------------------------------------------------
static void routeAgreement(Engine *e)
{
    std::printf("\n== (d) ROUTE AGREEMENT: a static diffuse box's bounce, the caches' answer against the "
                "decode's ==\n");
    View *view = e->createOffscreenView("speckleroute", kW, kH, Colour(0, 0, 0));
    Scene *s = e->createScene("speckleroute");
    view->setScene(s);
    setupView(view);
    // NO SKY, NO AMBIENT: the floor's gathered light is the box's bounce alone (a
    // floor ray sees the box or the black sky), so the two routes answer the same
    // hits and nothing else.
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    enginetest::addDirectionalLight(s, Vec3(-1.0f, -1.0f, -0.35f), 3.0f);
    MeshData cubeMd = enginetest::unitCubeMesh();
    cubeMd.cards = enginetest::boxCards(0.5f);
    const MeshId cube = s->createMesh(cubeMd);
    const NodeId floorN = s->createNode();
    s->attachMesh(floorN, cube, matte(s, Colour(0.5f, 0.5f, 0.5f)));
    s->setNodeTransform(floorN, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(40.0f, 0.5f, 40.0f));
    // THE BOX: white-ish, sun-lit on its +x face (the sun travels towards -x), a
    // static, carded object — the shipped route answers its hits from its cards.
    const NodeId box = s->createNode();
    s->attachMesh(box, cube, matte(s, Colour(0.8f, 0.8f, 0.8f)));
    s->setNodeTransform(box, Vec3(0.0f, 1.0f, 0.0f), Quat(), Vec3(2.0f, 2.0f, 2.0f));
    // Top-down, orthographic: a pixel is a floor point.
    CameraDesc c;
    c.position = Vec3(0.0f, 8.0f, 0.0f);
    c.orientation = Quat(-0.70710678f, 0.0f, 0.0f, 0.70710678f);
    c.orthographic = true;
    c.orthoSize = 4.0f;
    c.farClip = 100.0f;
    view->setCamera(c);
    s->setGlobalIllumination(epicGi());
    GatherTuning t;
    t.readback = true;
    t.freezeFrameIndex = true;
    t.jitterOff = true;
    t.rayJitterOff = true;
    // THE REST OFF: a still view HOLDS its rest mean and traces nothing more, so
    // the arm's second half would read the first half's held answer.
    t.restOff = true;
    s->setGatherTuning(t);

    // The strip beside the box's lit face: x in [1.1, 2.1] m, z in [-0.7, 0.7] m.
    // THE INSTRUMENT REFUSES A DROPPED HIT (GATHER-NOISE-1): a record the full
    // list dropped leaves its ray black, which reads as a route DISAGREEMENT.
    unsigned long long dropped = 0;
    const auto strip = [&](double &mean, unsigned &n, unsigned long long &records) {
        render(e, 120);
        mean = 0.0;
        n = 0;
        records = 0;
        for (int k = 0; k < 8; ++k) {
            render(e, 1);
            records += s->rayQueryStatus().hitRecords;
            dropped += s->rayQueryStatus().hitDropped;
            const GatherStatus g = s->giStatus().gather;
            if (g.irradiance.empty()) continue;
            const double halfW = 4.0 * double(g.irradianceW) / double(g.irradianceH);
            for (unsigned y = 0; y < g.irradianceH; ++y)
                for (unsigned x = 0; x < g.irradianceW; ++x) {
                    // ortho top-down: screen right = +x, screen up = -z.
                    const double wx = (double(x) + 0.5) / g.irradianceW * 2.0 * halfW - halfW;
                    const double wz = (double(y) + 0.5) / g.irradianceH * 8.0 - 4.0;
                    if (wx < 1.1 || wx > 2.1 || std::fabs(wz) > 0.7) continue;
                    const float *p = &g.irradiance[(size_t(y) * g.irradianceW + x) * 4u];
                    if (p[3] < 0.5f) continue;
                    mean += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                    ++n;
                }
        }
        if (n) mean /= double(n);
    };
    double cacheE = 0.0, decodeE = 0.0;
    unsigned cacheN = 0, decodeN = 0;
    unsigned long long cacheRec = 0, decodeRec = 0;
    const bool armed = e->setArm("gather.decodeHits", 0.0);
    strip(cacheE, cacheN, cacheRec);
    e->setArm("gather.decodeHits", 1.0);
    strip(decodeE, decodeN, decodeRec);
    e->setArm("gather.decodeHits", 0.0);
    std::printf("   the strip's gathered E/pi: CACHES %.5f (%u px, %llu records over 8 frames), DECODE %.5f "
                "(%u px, %llu records); decode / caches %.4f\n",
                cacheE, cacheN, cacheRec, decodeE, decodeN, decodeRec, cacheE > 0.0 ? decodeE / cacheE : 0.0);
    CHECK_MSG(armed, "the arm \"gather.decodeHits\" exists");
    CHECK_MSG(dropped == 0u, "the decode route dropped no hit (%llu dropped): every ray is measured", dropped);
    CHECK_MSG(cacheN > 1000 && decodeN > 1000 && cacheE > 1e-4,
              "the strip beside the box is gathered and lit by its bounce (%u / %u px, E/pi %.5f)", cacheN, decodeN,
              cacheE);
    CHECK_MSG(decodeRec > 50u * cacheRec + 1000u,
              "the arm sends the box's hits to the decode (%llu records against %llu)", decodeRec, cacheRec);
    // THE BAR: the cards' own tolerance against their closed form (gi.hit_shade's
    // --voxel-view: the store's step, the octahedral half-cell of the stored light
    // direction — 3 %).
    const double rel = cacheE > 0.0 ? std::fabs(decodeE / cacheE - 1.0) : 1.0;
    CHECK_MSG(rel < 0.03,
              "ROUTE AGREEMENT: a gather hit on a static diffuse box answered by the DECODE is the card's answer "
              "(%.2f %% apart, bar 3 %%: the cards' tolerance)", 100.0 * rel);
    // THE SAME HITS ON A GLOSSY METAL (the silver PBR): the caches store its
    // diffuse response (none: a metal's albedo is all specular), so the decode must
    // answer the same — on the base its answer was the lobe, the sun's highlight
    // seen from the floor. The bar is absolute: 3 % of the diffuse box's bounce.
    s->attachMesh(box, cube, pbr(s, Colour(0.97f, 0.96f, 0.91f), 1.0f, 0.22f));
    double mCacheE = 0.0, mDecodeE = 0.0;
    unsigned mCacheN = 0, mDecodeN = 0;
    unsigned long long mCacheRec = 0, mDecodeRec = 0;
    strip(mCacheE, mCacheN, mCacheRec);
    e->setArm("gather.decodeHits", 1.0);
    strip(mDecodeE, mDecodeN, mDecodeRec);
    e->setArm("gather.decodeHits", 0.0);
    std::printf("   a SILVER box: CACHES %.5f (%llu records), DECODE %.5f (%llu records)\n", mCacheE, mCacheRec,
                mDecodeE, mDecodeRec);
    CHECK_MSG(mDecodeRec > 50u * mCacheRec + 1000u, "...and the silver box's hits are decoded (%llu records)",
              mDecodeRec);
    CHECK_MSG(std::fabs(mDecodeE - mCacheE) < 0.03 * cacheE,
              "ROUTE AGREEMENT ON A GLOSSY METAL: the decode answers the gather's hit with the diffuse response "
              "the card stores (|%.5f - %.5f| against the bar %.5f) — no specular lobe", mDecodeE, mCacheE,
              0.03 * cacheE);
    e->destroyView(view);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-speckle-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    {
        View *probe = e->createOffscreenView("speckleprobe", 16, 16, Colour(0, 0, 0));
        const bool rays = e->rayQueryAvailable() && e->rayTracing();
        e->destroyView(probe);
        if (!rays) {
            std::printf("ok: no ray queries on this machine — gi.speckle skips\n");
            return 0;
        }
    }
    const bool dump = std::getenv("JAH_SPECKLE_DUMP") != nullptr;
    Fixture f;
    f.e = e;
    if (!build(f)) { std::printf("FAIL: the fixture\n"); return 1; }
    render(e, 120);

    std::printf("\n== (a) THE CONTROLS ==\n");
    const Run still = runArm(f, "still", false, false);
    CHECK_MSG(still.bursts == 0, "the silver sphere STILL: no burst on the floor band (%d)", still.bursts);
    f.s->attachMesh(f.sphere, f.sphereMeshId, f.matteGrey);
    const Run matteMove = runArm(f, "matte", true, dump);
    CHECK_MSG(matteMove.bursts == 0,
              "a MATTE sphere moved: no burst — the band sees neither the sphere, its shadow nor its "
              "reflection (%d)", matteMove.bursts);

    std::printf("\n== (b) THE SPECKLE: the silver sphere moved (automatic mobility, its cards re-allocated) ==\n");
    f.s->attachMesh(f.sphere, f.sphereMeshId, f.silver);
    const Run silver = runArm(f, "silver", true, dump);
    CHECK_MSG(silver.bursts == 0,
              "THE SPECKLE: the moving silver sphere puts no burst on the floor (%d of %d pairs; the base: 21-23 "
              "of 59 in the owner's scene)", silver.bursts, silver.pairs);

    std::printf("\n== (c) THE FORCED DECODE: the silver sphere MOVABLE (every hit on it a record) ==\n");
    f.s->setNodeMovable(f.sphere, true);
    const Run movable = runArm(f, "movable", true, dump);
    CHECK_MSG(movable.records > 0, "the movable sphere's hits are records (%llu)", movable.records);
    CHECK_MSG(movable.bursts == 0,
              "THE FORCED DECODE: no burst (%d of %d pairs; the base: 49 of 59 in the owner's scene)",
              movable.bursts, movable.pairs);

    // (e) on the same fixture: the sphere still and movable (its reflection is a
    // record's), the floor a mirror.
    std::printf("\n== (e) THE REFLECTION KEEPS THE FULL LOBE: the silver sphere in a mirror floor ==\n");
    {
        f.s->setNodeTransform(f.sphere, Vec3(0.0f, 1.2f, 0.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
        f.s->attachMesh(f.floor, f.cube, f.floorMirror);
        render(e, 120);
        Image withSphere, without;
        f.view->readPixels(withSphere);
        f.s->setNodeVisible(f.sphere, false);
        render(e, 120);
        f.view->readPixels(without);
        f.s->setNodeVisible(f.sphere, true);
        render(e, 30);
        // The sphere's REFLECTION: the pixels below the floor line that the sphere
        // changes (its direct image sits above y ~ 0.45 H; the band below is the
        // mirror's).
        unsigned n = 0;
        double sumL = 0.0, maxL = 0.0;
        unsigned hot = 0;   // the sun's lobe: the mirrored sky alone peaks at 0.61
        for (unsigned y = kBand0 - 45; y < kH; ++y)
            for (unsigned x = kW / 2 - 120; x < kW / 2 + 120; ++x) {
                const size_t i = (size_t(y) * kW + x) * 4u;
                double d2 = 0.0, l = 0.0;
                for (int c = 0; c < 3; ++c) {
                    const double d = (double(withSphere.rgba[i + c]) - double(without.rgba[i + c])) / 255.0;
                    d2 += d * d;
                }
                if (std::sqrt(d2 / 3.0) < 0.05) continue;
                l = (0.2126 * withSphere.rgba[i] + 0.7152 * withSphere.rgba[i + 1] + 0.0722 * withSphere.rgba[i + 2]) /
                    255.0;
                sumL += l;
                maxL = std::max(maxL, l);
                if (l > 0.85) ++hot;
                ++n;
            }
        const double meanL = n ? sumL / n : 0.0;
        std::printf("   the sphere's reflection: %u px, mean display luminance %.3f, max %.3f, %u px above 0.85\n", n, meanL,
                    maxL, hot);
        if (dump) {
            writePpm("speckle-mirror.ppm", withSphere);
            writePpm("speckle-mirror-without.ppm", without);
        }
        CHECK_MSG(n > 2000, "the mirror floor shows the sphere (%u px)", n);
        CHECK_MSG(meanL > 0.15 && maxL > 0.6,
                  "...as its SPECULAR picture — the sky and the sun's highlight it mirrors (mean %.3f, max %.3f; a "
                  "diffuse-only metal reflects black)", meanL, maxL);
        // THE SUN'S LOBE ITSELF: the highlight a reflection record shades with the
        // direct light's specular (the lane measured ~319 px above 0.85; the
        // mirrored sky alone reaches 0.61, so a reflection that lost the direct
        // specular has none).
        CHECK_MSG(hot > 150, "...and the SUN'S HIGHLIGHT is in it: %u px above 0.85 (bar 150; the sky alone gives 0)",
                  hot);
        f.s->attachMesh(f.floor, f.cube, f.floorMatte);
    }

    routeAgreement(e);

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
