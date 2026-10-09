// gi.far_sun_* — WHAT A FAR SUNLIT HIT IS WORTH (FAR-SUN-1; SPECS/briefs/FAR-SUN-1.md).
//
// THE MEASUREMENT THIS LANE EXISTS FOR (BISTRO-HITS-1, spikes/bistro-hits-1/NOTES.txt): a
// self-shaded wall's diffuse GI, the part of it that is the SUNLIT FLOOR in front of it
// bounced back, read 0.43-0.49x of the physics in the static Bistro when the street lay in the
// coarsest cascade alone, and 0.70-0.84x in a Basic canyon control. This suite is that canyon
// with a CLOSED FORM instead of a census: every surface but the floor is BLACK, there is no sky
// and no ambient, so the only radiance in the wall's hemisphere is the sunlit floor's, and the
// floor's is known exactly — the store's own law (LightInjection_piece_cs.any: rho x I x cos x
// the diffuse lobe's directional albedo; the cards relight by the same law). The wall's gathered
// irradiance E/pi (the gather's own answer, GatherTuning::readback) must then be
//
//     E/pi (p) = L_floor x F(p),     F(p) = the cosine-weighted fraction of p's hemisphere that
//                                           lands on SUNLIT floor (a stratified quadrature of
//                                           rays against the fixture's boxes, exact geometry).
//
// MODES (argv[1]):
//   share      gi.far_sun_share   — the wall in cascade 3 (camera 40 m) reads >= 0.90 of the closed
//                                   form, and the near camera (12 m) within 10 % of the far one.
//   clutter    gi.far_sun_clutter — forty small black boxes on the floor: the ratio to the closed
//                                   form (which counts them) within 10 % of the clean arm's.
//   contrast   gi.store_contrast  — a black floor patch beside the sunlit grey floor, both in
//                                   cascade 3: what a gather ray reads at the black patch is
//                                   < 0.15 of what it reads on the sunlit grey (the store's own
//                                   texels printed beside it).
//   hits       gi.gather_hits_view — the instrument (the arm "gather.hitsReadback"): every ray of
//                                   every uniform probe names a source and the counts sum to the
//                                   ray count; the GatherHits photon view paints.
//   castshadow gi.cast_shadow_gi  — a black slab with castShadow off over the sunlit floor: the
//                                   wall reads what it reads with the slab hidden, and the slab
//                                   casting reads less.
//                                   RED WITHOUT THE CASTER SHARE (FAR-SUN-1 D reverted, voxel
//                                   route): no-cast 0.07368 = casting 0.07368 against hidden 0.13969;
//                                   with it: no-cast 0.13969 = hidden (t-castshadow-vox-*.log).
// Paired arms in one process; settled in FRAMES (the gather's rest), never in seconds.
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
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

namespace {

void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

constexpr unsigned kSize = 768u;
constexpr float kFov = 45.0f;
constexpr double kPi = 3.14159265358979323846;

/// THE SUN: elevation 61 degrees, travelling towards +x (the wall's +x face is in its own shadow).
constexpr double kSunElev = 61.0 * kPi / 180.0;
const Vec3 kSunTravel(float(std::cos(kSunElev)), float(-std::sin(kSunElev)), 0.0f);
const Vec3 kToSun(-kSunTravel.x, -kSunTravel.y, 0.0f);
constexpr float kSunIntensity = 3.0f;
constexpr float kFloorAlbedo = 0.5f;

struct Box {
    enginetest::AnalyticBox b;
    bool floor = false;   ///< the one surface that is not black
};

/// Nearest hit of a ray against the fixture's boxes: the box index and the distance (-1 = none).
int nearest(const Vec3 &o, const Vec3 &d, const std::vector<Box> &boxes, float &tHit)
{
    int best = -1;
    tHit = 1e30f;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const enginetest::AnalyticBox &b = boxes[i].b;
        float t0 = 0.0f, t1 = 1e30f;
        const float O[3] = { o.x, o.y, o.z }, D[3] = { d.x, d.y, d.z };
        const float A[3] = { b.mn.x, b.mn.y, b.mn.z }, B[3] = { b.mx.x, b.mx.y, b.mx.z };
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::fabs(D[a]) < 1e-12f) {
                if (O[a] < A[a] || O[a] > B[a]) miss = true;
                continue;
            }
            float ta = (A[a] - O[a]) / D[a], tb = (B[a] - O[a]) / D[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) miss = true;
        }
        if (miss || t0 <= 1e-5f) continue;
        if (t0 < tHit) { tHit = t0; best = int(i); }
    }
    return best;
}

bool sunlit(const Vec3 &p, const std::vector<Box> &boxes)
{
    for (const Box &b : boxes)
        if (enginetest::rayHitsBox(p, kToSun, b.b)) return false;
    return true;
}

/// F(p): the cosine-weighted fraction of the hemisphere about +x at p that lands on the SUNLIT
/// TOP of the floor box (a stratified n x n quadrature).
double sunlitFloorFraction(const Vec3 &p, const std::vector<Box> &boxes, int n = 160)
{
    unsigned lit = 0u, total = 0u;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const double u1 = (i + 0.5) / n, u2 = (j + 0.5) / n;
            const double r = std::sqrt(u1), ph = 2.0 * kPi * u2;
            // local frame: normal +x, tangents +y, +z
            const Vec3 d(float(std::sqrt(1.0 - u1)), float(r * std::cos(ph)), float(r * std::sin(ph)));
            ++total;
            float t;
            const int k = nearest(p, d, boxes, t);
            if (k < 0 || !boxes[size_t(k)].floor) continue;
            const Vec3 h(p.x + d.x * t, p.y + d.y * t, p.z + d.z * t);
            if (h.y < -1e-3f) continue;   // the floor's side, not its top
            if (sunlit(Vec3(h.x, h.y + 1e-3f, h.z), boxes)) ++lit;
        }
    return total ? double(lit) / double(total) : 0.0;
}

/// The floor's stored radiance under the sun: rho x I x cos x the diffuse lobe's albedo
/// (LightInjection_piece_cs.any's law; the card relight takes the same).
double floorRadiance()
{
    const double c = std::sin(kSunElev);
    return double(kFloorAlbedo) * double(kSunIntensity) * c * enginetest::disneyDiffuseAlbedo(c, 1.0);
}

/// World -> pixel through the camera testCameraDescLookAt(pos, target) builds, square view.
bool project(const Vec3 &pos, const Vec3 &target, const Vec3 &w, double &px, double &py)
{
    Vec3 f(target.x - pos.x, target.y - pos.y, target.z - pos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const Vec3 d(w.x - pos.x, w.y - pos.y, w.z - pos.z);
    const double z = d.x * f.x + d.y * f.y + d.z * f.z;
    if (z <= 0.0) return false;
    const double t = std::tan(kFov * 0.5 * kPi / 180.0);
    const double x = (d.x * r.x + d.y * r.y + d.z * r.z) / z / t;
    const double y = (d.x * u.x + d.y * u.y + d.z * u.z) / z / t;
    px = (x + 1.0) * 0.5 * kSize - 0.5;
    py = (1.0 - y) * 0.5 * kSize - 0.5;
    return px >= 4 && py >= 4 && px < kSize - 4 && py < kSize - 4;
}

MaterialId matte(Scene *s, float albedo)
{
    PbrParams p;
    p.albedo = Colour(albedo, albedo, albedo);
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return s->createPbrMaterial(p);
}

struct Fixture {
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *s = nullptr;
    MeshId cube = 0;
    MaterialId grey = 0, black = 0;
    NodeId floor = 0;
    std::vector<Box> boxes;   ///< the analytic twin of everything attached
};

/// A box node with its analytic twin: centre and FULL size.
NodeId addBox(Fixture &f, const Vec3 &c, const Vec3 &size, MaterialId m, bool isFloor)
{
    const NodeId n = f.s->createNode();
    f.s->attachMesh(n, f.cube, m);
    f.s->setNodeTransform(n, c, Quat(), size);
    Box b;
    b.b.mn = Vec3(c.x - size.x * 0.5f, c.y - size.y * 0.5f, c.z - size.z * 0.5f);
    b.b.mx = Vec3(c.x + size.x * 0.5f, c.y + size.y * 0.5f, c.z + size.z * 0.5f);
    b.floor = isFloor;
    f.boxes.push_back(b);
    return n;
}

/// THE CANYON: a 50 m floor (grey), the receiving wall facing +x at x = 0 (10 m tall, 20 m
/// long), the opposite wall at x = 12 — both walls BLACK. The Basic control's shape
/// (spikes/bistro-hits-1/scratch/ctlz.js) with the floor the one light source.
bool buildCanyon(Fixture &f)
{
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);   // a document primitive carries cards
    f.cube = f.s->createMesh(md);
    f.grey = matte(f.s, kFloorAlbedo);
    f.black = matte(f.s, 0.0f);
    if (!f.cube || !f.grey || !f.black) return false;
    f.floor = addBox(f, Vec3(0.0f, -0.1f, 0.0f), Vec3(50.0f, 0.2f, 50.0f), f.grey, true);
    addBox(f, Vec3(-0.25f, 5.0f, 0.0f), Vec3(0.5f, 10.0f, 20.0f), f.black, false);
    addBox(f, Vec3(12.25f, 5.0f, 0.0f), Vec3(0.5f, 10.0f, 20.0f), f.black, false);
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.f, 1.f, 1.f);
    l.intensity = kSunIntensity;
    const NodeId sun = enginetest::addDirectionalLight(f.s, kSunTravel, 1.0f);
    return sun && f.s->setLight(sun, l);
}

/// The engine, owned at file scope and RELEASED BY main before it returns (a static's
/// destructor runs after the plugins have gone: a teardown SIGSEGV).
std::unique_ptr<Engine> gEngine;

bool boot(Fixture &f, const char *log)
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = log;
    std::unique_ptr<Engine> &engine = gEngine;
    engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return false; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    f.e = engine.get();
    f.view = f.e->createOffscreenView("farsun", kSize, kSize, Colour(0, 0, 0));
    if (!f.view) { std::printf("FAIL: view: %s\n", f.e->lastError().c_str()); return false; }
    f.view->setOffscreenContract(OffscreenContract::StillPicture);
    if (!f.e->rayQueryAvailable() || !f.e->rayTracing()) return true;
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdrReadback = true;
    f.view->setPostFx(fx);
    f.view->setShadows(true);
    f.s = f.e->createScene("farsun");
    f.view->setScene(f.s);
    f.s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    return true;
}

/// THE ROUTE (argv[2] "voxels"): the surface cache off, so every hit the cache would answer is
/// answered by the cascades - the route the static Bistro's street takes (BISTRO-HITS-1: no card
/// answers a street hit at its facade, and cards off reads the same).
bool gVoxelRoute = false;

GiParams highGi()
{
    GiParams gi;
    gi.cards = !gVoxelRoute;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    gi.numBounces = 1;
    return gi;
}

/// Renders until the gather has HELD its rest mean (the picture is a function of the scene and
/// the camera), then returns the last readback.
GatherStatus settled(Fixture &f)
{
    GatherStatus g;
    for (int i = 0; i < 600; ++i) {
        f.e->renderOneFrame();
        if (i < 60) continue;
        g = f.s->giStatus().gather;
        if (g.restFrames > g.settleFrames + 4u && !g.irradiance.empty() && f.s->giStatus().giAtRest) break;
    }
    render(f.e, 4);
    return f.s->giStatus().gather;
}

/// The gathered E/pi (luminance) at a world point, a 5 x 5 block of the readback; -1 when the
/// point is off screen or no probe answered there.
double gatheredAt(const GatherStatus &g, const Vec3 &cam, const Vec3 &target, const Vec3 &w)
{
    double px, py;
    if (g.irradiance.empty() || g.irradianceW != kSize || !project(cam, target, w, px, py)) return -1.0;
    double sum = 0.0;
    int n = 0;
    for (int y = int(py) - 2; y <= int(py) + 2; ++y)
        for (int x = int(px) - 2; x <= int(px) + 2; ++x) {
            const float *p = &g.irradiance[(size_t(y) * g.irradianceW + size_t(x)) * 4u];
            if (p[3] < 0.5f) continue;
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    return n >= 13 ? sum / n : -1.0;
}

/// The wall's sample points (on its +x face).
std::vector<Vec3> wallPoints()
{
    std::vector<Vec3> p;
    for (float h : { 2.0f, 4.0f, 6.0f, 8.0f })
        for (float z : { -4.0f, 0.0f, 4.0f }) p.push_back(Vec3(0.01f, h, z));
    return p;
}

const Vec3 kFarCam(8.0f, 5.0f, 40.0f), kNearCam(8.0f, 5.0f, 12.0f), kTarget(0.0f, 4.0f, 0.0f);

/// One camera's ratio: (gathered E/pi with the grey floor - with a black floor) / the closed
/// form, over the wall points both arms see. `perPoint` printed.
double shareRatio(Fixture &f, const Vec3 &cam, const char *label, bool verbose = true)
{
    f.view->setCamera(enginetest::testCameraDescLookAt(cam, kTarget));
    const double L = floorRadiance();
    f.s->attachMesh(f.floor, f.cube, f.grey);
    const GatherStatus lit = settled(f);
    f.s->attachMesh(f.floor, f.cube, f.black);
    const GatherStatus dark = settled(f);
    f.s->attachMesh(f.floor, f.cube, f.grey);
    double sm = 0.0, se = 0.0;
    int n = 0;
    for (const Vec3 &p : wallPoints()) {
        const double a = gatheredAt(lit, cam, kTarget, p), b = gatheredAt(dark, cam, kTarget, p);
        if (a < 0.0 || b < 0.0) continue;
        const double expect = L * sunlitFloorFraction(p, f.boxes);
        if (verbose)
            std::printf("     %s  wall (%.1f, %.1f): gathered %.5f (dark %.5f), closed form %.5f -> %.3f\n",
                        label, double(p.y), double(p.z), a, b, expect, expect > 0 ? (a - b) / expect : 0.0);
        sm += a - b;
        se += expect;
        ++n;
    }
    const double ratio = se > 0.0 ? sm / se : 0.0;
    std::printf("   %s camera (%.0f, %.0f, %.0f): %d wall points, share / closed form = %.3f\n", label,
                double(cam.x), double(cam.y), double(cam.z), n, ratio);
    return n >= 6 ? ratio : -1.0;
}

void printCascades(Scene *s)
{
    const GiStatus g = s->giStatus();
    for (size_t i = 0; i < g.cascades.size(); ++i)
        std::printf("   cascade %zu: cell %.3f m, centre (%.1f, %.1f, %.1f)\n", i, double(g.cascades[i].cell),
                    double(g.cascades[i].centre.x), double(g.cascades[i].centre.y), double(g.cascades[i].centre.z));
}

int modeShare(Fixture &f, bool clutter)
{
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    f.s->setGlobalIllumination(highGi());
    GatherTuning t;
    t.readback = true;
    f.s->setGatherTuning(t);
    std::printf("   the floor's stored radiance under the sun: %.5f (rho %.2f x I %.1f x cos %.3f x albedo(lobe))\n",
                floorRadiance(), double(kFloorAlbedo), double(kSunIntensity), std::sin(kSunElev));
    if (!clutter) {
        const double far = shareRatio(f, kFarCam, "FAR ");
        printCascades(f.s);
        const double near = shareRatio(f, kNearCam, "NEAR");
        printCascades(f.s);
        CHECK_MSG(far >= 0.90, "THE FAR SUNLIT HIT: the wall in cascade 3 reads %.3f of the closed form (bar 0.90)",
                  far);
        CHECK_MSG(far > 0.0 && near > 0.0 && std::fabs(near / far - 1.0) <= 0.10,
                  "NEAR AND FAR AGREE: the near camera reads %.3f against the far %.3f (bar +-10 %%)", near, far);
        return 0;
    }
    const double clean = shareRatio(f, kFarCam, "CLEAN  ", false);
    // FORTY SMALL BLACK BOXES on the floor in front of the wall (0.3-0.6 m, a fixed scatter):
    // street clutter. The closed form counts them (they occlude the floor and shade it).
    unsigned seed = 12345u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (int i = 0; i < 40; ++i) {
        const float sz = 0.3f + 0.3f * rnd();
        const Vec3 c(0.8f + 10.4f * rnd(), sz * 0.5f, -9.0f + 18.0f * rnd());
        addBox(f, c, Vec3(sz, sz, sz), f.black, false);
    }
    const double cluttered = shareRatio(f, kFarCam, "CLUTTER", false);
    CHECK_MSG(clean > 0.0 && cluttered > 0.0 && std::fabs(cluttered / clean - 1.0) <= 0.10,
              "STREET CLUTTER IS COUNTED AS WHAT IT IS: the cluttered canyon reads %.3f of its closed form against "
              "the clean %.3f (bar +-10 %%)", cluttered, clean);
    return 0;
}

int modeCastShadow(Fixture &f)
{
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    f.s->setGlobalIllumination(highGi());
    GatherTuning t;
    t.readback = true;
    f.s->setGatherTuning(t);
    // A BLACK SLAB 14 m up over the sunlit strip, wide enough that its shadow covers most of the
    // strip the wall sees: with castShadow on it shades the strip, with it off the strip stays lit.
    const Vec3 slabC(8.0f + 14.0f * kToSun.x / kToSun.y, 14.0f, 0.0f);
    const NodeId slab = addBox(f, slabC, Vec3(5.0f, 0.2f, 16.0f), f.black, false);
    Fixture noSlab = f;
    noSlab.boxes.pop_back();
    const Vec3 cam = kFarCam;
    f.view->setCamera(enginetest::testCameraDescLookAt(cam, kTarget));
    const auto wallMean = [&](const GatherStatus &g) {
        double s = 0.0;
        int n = 0;
        for (const Vec3 &p : wallPoints()) {
            const double a = gatheredAt(g, cam, kTarget, p);
            if (a >= 0.0) { s += a; ++n; }
        }
        return n >= 6 ? s / n : -1.0;
    };
    f.s->setNodeVisible(slab, false);
    const double hidden = wallMean(settled(f));
    f.s->setNodeVisible(slab, true);
    f.s->setNodeCastShadow(slab, false);
    const double noCast = wallMean(settled(f));
    f.s->setNodeCastShadow(slab, true);
    const double casting = wallMean(settled(f));
    const double expHidden = floorRadiance(), expCast = floorRadiance();
    (void)expHidden;
    (void)expCast;
    double fHidden = 0.0, fCast = 0.0;
    for (const Vec3 &p : wallPoints()) {
        fHidden += sunlitFloorFraction(p, noSlab.boxes, 96);
        fCast += sunlitFloorFraction(p, f.boxes, 96);
    }
    std::printf("   the wall's gathered E/pi: slab hidden %.5f, slab castShadow off %.5f, slab casting %.5f; the "
                "closed form's casting / hidden = %.3f\n", hidden, noCast, casting, fHidden > 0 ? fCast / fHidden : 0.0);
    CHECK_MSG(hidden > 0.0 && noCast > 0.0 && std::fabs(noCast / hidden - 1.0) <= 0.03,
              "GI HONOURS castShadow: the slab that casts no shadow leaves the wall's bounce where the hidden slab "
              "does (%.5f against %.5f, bar 3 %%)", noCast, hidden);
    CHECK_MSG(casting > 0.0 && casting < 0.9 * hidden,
              "...and the casting slab shades the strip (%.5f against %.5f)", casting, hidden);
    return 0;
}

const char *sourceName(unsigned s)
{
    switch (s) {
    case 1: return "card";
    case 2: return "voxel";
    case 3: return "sky";
    case 4: return "decode";
    case 5: return "decodeFar";
    case 6: return "dropped";
    default: return "none";
    }
}

int modeHits(Fixture &f)
{
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    f.s->setGlobalIllumination(highGi());
    f.view->setCamera(enginetest::testCameraDescLookAt(kFarCam, kTarget));
    const bool armed = f.e->setArm("gather.hitsReadback", 1.0);
    CHECK(armed, "the arm \"gather.hitsReadback\" exists");
    // (photonViewRefusal speaks for PRESENTING views; an offscreen suite view is not one, so it
    // reads "nowhere to paint" here - the picture below is the proof the view paints.)
    f.s->setPhotonView(PhotonView::GatherHits);
    render(f.e, 90);
    GatherStatus g;
    for (int i = 0; i < 20 && g.rays.empty(); ++i) {
        render(f.e, 1);
        g = f.s->giStatus().gather;
    }
    const unsigned raysPerProbe = g.raysPerProbe;
    CHECK_MSG(!g.rays.empty() && g.rays.size() == size_t(g.probes) * raysPerProbe,
              "the readback holds one record per ray of every uniform probe (%zu = %u probes x %u rays)", g.rays.size(),
              g.probes, raysPerProbe);
    unsigned counts[8] = {};
    unsigned traced = 0u, unnamed = 0u;
    double lum[8] = {};
    unsigned probesTraced = 0u;
    for (size_t i = 0; i < g.rays.size(); ++i) {
        const GatherRay &r = g.rays[i];
        if (r.source > 7) { ++unnamed; continue; }
        ++counts[r.source];
        lum[r.source] += 0.2126 * r.radiance[0] + 0.7152 * r.radiance[1] + 0.0722 * r.radiance[2];
        if (r.source != 0) ++traced;
        if (r.source != 0 && r.ray == 0) ++probesTraced;
    }
    std::printf("   %u probes traced, %u rays:", probesTraced, traced);
    for (unsigned s = 0; s < 7; ++s)
        std::printf(" %s %u (mean %.4f)", sourceName(s), counts[s], counts[s] ? lum[s] / counts[s] : 0.0);
    std::printf("\n");
    unsigned named = 0u;
    for (unsigned s = 1; s < 7; ++s) named += counts[s];
    CHECK_MSG(unnamed == 0u && probesTraced > 100u && named == probesTraced * raysPerProbe,
              "EVERY RAY OF EVERY TRACED PROBE NAMES ITS SOURCE: %u named = %u probes x %u rays (%u unnamed)", named,
              probesTraced, raysPerProbe, unnamed);
    CHECK_MSG(counts[3] > 0u && (counts[1] + counts[2] + counts[4] + counts[5]) > 0u,
              "the canyon's rays see the sky and the floor (%u sky, %u hits)", counts[3],
              counts[1] + counts[2] + counts[4] + counts[5]);
    // THE PICTURE: the view paints the overlay (a non-black picture where the probes are).
    ImageF img;
    double painted = 0.0;
    if (f.view->readPixelsHdr(img))
        for (unsigned y = 0; y < img.height; y += 4)
            for (unsigned x = 0; x < img.width; x += 4) painted += img.at(x, y).b;
    CHECK_MSG(painted > 1.0, "the GatherHits view paints its sources (blue sum %.1f)", painted);
    f.s->setPhotonView(PhotonView::Off);
    f.e->setArm("gather.hitsReadback", 0.0);
    return 0;
}

int modeContrast(Fixture &f)
{
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    // A BLACK PATCH of floor, 4 x 4 m, flush with the grey floor's top, in the sunlit strip
    // beside sunlit grey floor; seen from the far camera (cascade 3).
    MeshData md = enginetest::unitCubeMesh();
    const Vec3 patchC(9.0f, 0.005f, -6.0f);
    addBox(f, patchC, Vec3(4.0f, 0.01f, 4.0f), f.black, false);
    f.s->setGlobalIllumination(highGi());
    f.view->setCamera(enginetest::testCameraDescLookAt(kFarCam, kTarget));
    f.e->setArm("gather.hitsReadback", 1.0);
    render(f.e, 120);
    double litSum = 0.0, blackSum = 0.0;
    unsigned litN = 0u, blackN = 0u;
    for (int k = 0; k < 16; ++k) {
        render(f.e, 1);
        const GatherStatus g = f.s->giStatus().gather;
        for (const GatherRay &r : g.rays) {
            if (r.distance <= 0.0f || r.hitNormalY < 0.7f || std::fabs(r.hitPos[1]) > 0.1f) continue;
            const double l = 0.2126 * r.radiance[0] + 0.7152 * r.radiance[1] + 0.0722 * r.radiance[2];
            const float x = r.hitPos[0], z = r.hitPos[2];
            if (std::fabs(x - patchC.x) < 1.5f && std::fabs(z - patchC.z) < 1.5f) { blackSum += l; ++blackN; }
            else if (x > 6.5f && x < 11.5f && z > -3.0f && z < 9.0f && r.sunVisibility > 0.5f) { litSum += l; ++litN; }
        }
    }
    f.e->setArm("gather.hitsReadback", 0.0);
    const double lit = litN ? litSum / litN : 0.0, black = blackN ? blackSum / blackN : 0.0;
    std::printf("   gather hits on the floor: sunlit grey %.5f (%u rays), the black patch %.5f (%u rays); the closed "
                "form says %.5f and 0\n", lit, litN, black, blackN, floorRadiance());
    // ...and the STORE's own texels in cascade 3 (the read is separated from the write).
    GiVoxelVolume vol;
    if (f.s->giVoxelVolume(3, vol) && vol.available) {
        const auto texel = [&](const Vec3 &w, float out[4]) {
            const int ix = int(std::floor((w.x - vol.origin[0]) / vol.cell[0]));
            const int iy = int(std::floor((w.y - vol.origin[1]) / vol.cell[1]));
            const int iz = int(std::floor((w.z - vol.origin[2]) / vol.cell[2]));
            std::memset(out, 0, sizeof(float) * 4);
            if (ix < 0 || iy < 0 || iz < 0 || ix >= vol.width || iy >= vol.height || iz >= vol.depth) return;
            const size_t i = (size_t(iz) * vol.height * vol.width + size_t(iy) * vol.width + size_t(ix)) * 4u;
            for (int k = 0; k < 4; ++k) out[k] = vol.light[i + size_t(k)];
        };
        float a[4], b[4];
        texel(Vec3(patchC.x, 0.0f, patchC.z), b);
        texel(Vec3(9.0f, 0.0f, 3.0f), a);
        std::printf("   the cascade-3 STORE (multiplier %.4f): the sunlit grey cell %.4f / %.3f, the black patch's "
                    "cell %.4f / %.3f (light / opacity)\n", double(vol.multiplier), double(a[1]), double(a[3]),
                    double(b[1]), double(b[3]));
    }
    CHECK_MSG(litN > 50u && blackN > 20u && black < 0.15 * lit,
              "THE STORE HAS CONTRAST WHERE THE SCENE DOES: a ray on the black patch reads %.5f against the sunlit "
              "grey's %.5f (bar < 0.15 of it)", black, lit);
    return 0;
}

}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "share";
    gVoxelRoute = argc > 2 && std::string(argv[2]) == "voxels";
    if (gVoxelRoute) std::printf("   THE VOXEL ROUTE: cards off, every hit read from the cascades\n");
    Fixture f;
    if (!boot(f, ("test-gi-far-sun-" + mode + "-ogre.log").c_str())) return 1;
    if (!f.s) {
        std::printf("ok: no ray queries on this machine — gi.far_sun is about the gather and skips cleanly\n");
        gEngine.reset();
        return 0;
    }
    int rc = 0;
    if (mode == "share") rc = modeShare(f, false);
    else if (mode == "clutter") rc = modeShare(f, true);
    else if (mode == "castshadow") rc = modeCastShadow(f);
    else if (mode == "hits") rc = modeHits(f);
    else if (mode == "contrast") rc = modeContrast(f);
    else { std::printf("FAIL: unknown mode %s\n", mode.c_str()); return 1; }
    gEngine.reset();
    if (rc) return rc;
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
