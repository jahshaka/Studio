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
//   share      gi.far_sun_share   — ONE claim: the sunlit floor's bounce into a self-shaded wall, the
//                                   wall in cascade 3 (camera 40 m), equals the HEMISPHERICAL LAW
//                                   within 10 % (0.90-1.10), and the near camera (12 m) agrees
//                                   within 10 %. The law the store holds: rho I cos DA(cos, r) -
//                                   here 0.5 x 3.0 x 0.875 x DA(0.875, 1) = 0.898; on the static
//                                   Bistro 0.216 x 1.0 x 0.875 x 0.816 (the sun's colour) x 0.743
//                                   = 0.114 (a camera pixel is NOT the law: it carries the lobe's
//                                   view term, 1.27x at Bistro's ORCA view). Wall points h >= 4 m.
//   clutter    gi.far_sun_clutter — the Bistro's dilution: a cafe TERRACE in front of the wall
//                                   (grey tables and chairs - tops, seats, backs, legs: lit tops,
//                                   sides facing every way, dark undersides, the floor under them
//                                   shaded - several pieces inside every cascade-3 cell of the
//                                   street, the shape of Bistro's chairs and tables); its ratio to
//                                   the closed form (which counts every face of them, exact
//                                   geometry, SINGLE SCATTER - so this mode runs bounces 0, the
//                                   direct store alone) within 0.90-1.10, and within 0.90-1.10 of
//                                   the clean arm's. GREEN since FAR-SUN-2 (a voxel-answered hit's
//                                   direct light computed AT THE HIT, jah_rq_hit_direct.glsl):
//                                   terrace 1.060, clean 1.025-1.070 (the store read whole: 1.235 /
//                                   0.963 - a lit face read its cell's mean, a shaded one the lit
//                                   floor's light). The wall's own probes' rays read 0.999 of
//                                   their truth: the rest is the gather's integration, not the
//                                   store (spikes/far-sun-2/NOTES.md sections 10, 17).
//   lamp       gi.far_sun_lamp    — no sun: a point lamp and a glowing crate; every voxel-answered
//                                   gather ray against its exact radiance (the lamp's falloff x cos
//                                   x DA x its shadow, + the glow), paired with the arm gi.hitDirect
//                                   = 0 on the same rays (nothing darkens).
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
//                                   RED WITHOUT THE CASTER SHARE (D reverted on 4d38b30: every
//                                   surface a caster, voxel route): no-cast 0.07716 = casting
//                                   0.07716 against hidden 0.14564; with it: no-cast 0.14564 =
//                                   hidden, casting 0.07716. THE MIXED CELL (read F1): the slab
//                                   with three non-casting panels in its cells reads 0.10641 under
//                                   a share of the RAW coverage sums against 0.07716 alone (red);
//                                   0.07729 with the share of the capped sums
//                                   (spikes/far-sun-1/logs/d-red.log, f1-red.log, f1-green.log).
//   diag       (no row)           — FAR-SUN-2's per-ray instrument (a measure, run by hand): every
//                                   voxel-answered gather ray against its own hit's exact direct
//                                   light, the store at the hit and the cell means, by cascade.
// Paired arms in one process; settled in FRAMES (the gather's rest), never in seconds.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

unsigned kSize = 768u;   ///< the view (square); --size for the far-cascade rows
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

/// The ray's slab interval against a box, clipped at 0: false where it misses, else its entry t0.
bool slabEntry(const Vec3 &o, const Vec3 &d, const enginetest::AnalyticBox &b, float &t0)
{
    t0 = 0.0f;
    float t1 = 1e30f;
    const float O[3] = { o.x, o.y, o.z }, D[3] = { d.x, d.y, d.z };
    const float A[3] = { b.mn.x, b.mn.y, b.mn.z }, B[3] = { b.mx.x, b.mx.y, b.mx.z };
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(D[a]) < 1e-12f) {
            if (O[a] < A[a] || O[a] > B[a]) return false;
            continue;
        }
        float ta = (A[a] - O[a]) / D[a], tb = (B[a] - O[a]) / D[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

bool sunlit(const Vec3 &p, const std::vector<Box> &boxes)
{
    for (const Box &b : boxes)
        if (enginetest::rayHitsBox(p, kToSun, b.b)) return false;
    return true;
}

/// THE CLOSED FORM'S ACCELERATION (FAR-CASCADE-FIXTURE: the 80 m canyon's terrace is 2,576 boxes, a
/// closed form of 25,600 rays a wall point against every one of them at -O0 is minutes): the boxes in
/// runs of 16 consecutive indices (the terrace adds them table by table, so a run is a few metres of
/// street) and the runs in groups of 16, each under the union of its members. A union the ray misses,
/// or enters at or past the nearest hit so far, holds no box the ray meets first: a union's slab planes
/// are the extremes of its members', and float subtraction and division are monotonic, so its entry is
/// <= every member's and its exit >= every member's. The answers are THE PLAIN LOOPS' TO THE BIT
/// (nearest() in index order, a tie to the lower index; sunlit() any hit) - the closed form is
/// unchanged in form.
struct BoxSet {
    static constexpr size_t kRun = 16;
    const std::vector<Box> &boxes;
    std::vector<enginetest::AnalyticBox> runs, groups;   ///< groups: kRun runs each
    static void grow(enginetest::AnalyticBox &u, const enginetest::AnalyticBox &m)
    {
        u.mn = Vec3(std::min(u.mn.x, m.mn.x), std::min(u.mn.y, m.mn.y), std::min(u.mn.z, m.mn.z));
        u.mx = Vec3(std::max(u.mx.x, m.mx.x), std::max(u.mx.y, m.mx.y), std::max(u.mx.z, m.mx.z));
    }
    explicit BoxSet(const std::vector<Box> &b) : boxes(b)
    {
        for (size_t i = 0; i < b.size(); i += kRun) {
            enginetest::AnalyticBox u = b[i].b;
            for (size_t j = i + 1; j < std::min(b.size(), i + kRun); ++j) grow(u, b[j].b);
            runs.push_back(u);
        }
        for (size_t r = 0; r < runs.size(); r += kRun) {
            enginetest::AnalyticBox u = runs[r];
            for (size_t j = r + 1; j < std::min(runs.size(), r + kRun); ++j) grow(u, runs[j]);
            groups.push_back(u);
        }
    }
    /// [first, end) of the children of node k at a level holding `count` children in all
    static size_t childEnd(size_t k, size_t count) { return std::min(count, (k + 1) * kRun); }
};

int nearest(const Vec3 &o, const Vec3 &d, const BoxSet &set, float &tHit)
{
    int best = -1;
    tHit = 1e30f;
    for (size_t g = 0; g < set.groups.size(); ++g) {
        float tg;
        if (!slabEntry(o, d, set.groups[g], tg) || tg >= tHit) continue;
        for (size_t r = g * BoxSet::kRun; r < BoxSet::childEnd(g, set.runs.size()); ++r) {
            float tr;
            if (!slabEntry(o, d, set.runs[r], tr) || tr >= tHit) continue;
            for (size_t i = r * BoxSet::kRun; i < BoxSet::childEnd(r, set.boxes.size()); ++i) {
                float t0;
                if (!slabEntry(o, d, set.boxes[i].b, t0) || t0 <= 1e-5f) continue;
                if (t0 < tHit) { tHit = t0; best = int(i); }
            }
        }
    }
    return best;
}

bool sunlit(const Vec3 &p, const BoxSet &set)
{
    for (size_t g = 0; g < set.groups.size(); ++g) {
        if (!enginetest::rayHitsBox(p, kToSun, set.groups[g])) continue;
        for (size_t r = g * BoxSet::kRun; r < BoxSet::childEnd(g, set.runs.size()); ++r) {
            if (!enginetest::rayHitsBox(p, kToSun, set.runs[r])) continue;
            for (size_t i = r * BoxSet::kRun; i < BoxSet::childEnd(r, set.boxes.size()); ++i)
                if (enginetest::rayHitsBox(p, kToSun, set.boxes[i].b)) return false;
        }
    }
    return true;
}

/// F(p): the cosine-weighted fraction of the hemisphere about +x at p that lands on the SUNLIT
/// TOP of the floor box (a stratified n x n quadrature).
double sunlitFloorFraction(const Vec3 &p, const std::vector<Box> &boxes, int n = 160)
{
    const BoxSet set(boxes);
    unsigned lit = 0u, total = 0u;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const double u1 = (i + 0.5) / n, u2 = (j + 0.5) / n;
            const double r = std::sqrt(u1), ph = 2.0 * kPi * u2;
            // local frame: normal +x, tangents +y, +z
            const Vec3 d(float(std::sqrt(1.0 - u1)), float(r * std::cos(ph)), float(r * std::sin(ph)));
            ++total;
            float t;
            const int k = nearest(p, d, set, t);
            if (k < 0 || !boxes[size_t(k)].floor) continue;
            const Vec3 h(p.x + d.x * t, p.y + d.y * t, p.z + d.z * t);
            if (h.y < -1e-3f) continue;   // the floor's side, not its top
            if (sunlit(Vec3(h.x, h.y + 1e-3f, h.z), set)) ++lit;
        }
    return total ? double(lit) / double(total) : 0.0;
}

/// THE CLOSED FORM, GENERAL (C.3: lit clutter): the cosine-weighted mean over p's hemisphere about +x
/// of the radiance the first face hit stores - rho I cos DA(cos) where the face is grey ("floor") and
/// sunlit, 0 elsewhere (the walls are black, there is no sky). E/pi(p) is exactly this mean.
double expectedEoverPi(const Vec3 &p, const std::vector<Box> &boxes, int n = 160)
{
    const BoxSet set(boxes);
    // the lobe's albedo at each face's sun cosine, computed once a cosine (an exact memo: the six face
    // normals give at most six cosines, and disneyDiffuseAlbedo is a 64-step integral)
    std::vector<std::pair<double, double>> albedoMemo;
    const auto albedoAt = [&](double c) {
        for (const auto &m : albedoMemo)
            if (m.first == c) return m.second;
        albedoMemo.push_back({ c, enginetest::disneyDiffuseAlbedo(c, 1.0) });
        return albedoMemo.back().second;
    };
    double sum = 0.0;
    unsigned total = 0u;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const double u1 = (i + 0.5) / n, u2 = (j + 0.5) / n;
            const double r = std::sqrt(u1), ph = 2.0 * kPi * u2;
            const Vec3 d(float(std::sqrt(1.0 - u1)), float(r * std::cos(ph)), float(r * std::sin(ph)));
            ++total;
            float t;
            const int k = nearest(p, d, set, t);
            if (k < 0 || !boxes[size_t(k)].floor) continue;
            const Vec3 h(p.x + d.x * t, p.y + d.y * t, p.z + d.z * t);
            // the face hit: the slab boundary the point lies on
            const enginetest::AnalyticBox &b = boxes[size_t(k)].b;
            Vec3 nrmF(0.0f, 0.0f, 0.0f);
            const float e = 1e-3f;
            if (std::fabs(h.y - b.mx.y) < e) nrmF.y = 1.0f;
            else if (std::fabs(h.y - b.mn.y) < e) nrmF.y = -1.0f;
            else if (std::fabs(h.x - b.mx.x) < e) nrmF.x = 1.0f;
            else if (std::fabs(h.x - b.mn.x) < e) nrmF.x = -1.0f;
            else if (std::fabs(h.z - b.mx.z) < e) nrmF.z = 1.0f;
            else nrmF.z = -1.0f;
            const double c = nrmF.x * kToSun.x + nrmF.y * kToSun.y + nrmF.z * kToSun.z;
            if (!(c > 0.0)) continue;
            if (!sunlit(Vec3(h.x + nrmF.x * e, h.y + nrmF.y * e, h.z + nrmF.z * e), set)) continue;
            sum += double(kFloorAlbedo) * double(kSunIntensity) * c * albedoAt(c);
        }
    return total ? sum / double(total) : 0.0;
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
    std::vector<NodeId> greys;   ///< every grey node but the floor (the clutter): black in the dark arm
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

/// THE CANYON'S LENGTH (--length <m>, FAR-CASCADE-FIXTURE): the walls' extent along the street (z),
/// 20 m by default (every row written before the far-cascade rows: the walls 20 m long, z -10 .. 10);
/// the far-cascade rows run 80 m. The terrace is tiled along the whole street.
float gLength = 20.0f;

/// The floor's extent along the street: 15 m past either end of the walls, never under 50 m.
float floorLength() { return std::max(50.0f, gLength + 30.0f); }

/// THE CANYON: a 50 m x floorLength() floor (grey), the receiving wall facing +x at x = 0 (10 m
/// tall, gLength long), the opposite wall at x = 12 — both walls BLACK. The Basic control's shape
/// (spikes/bistro-hits-1/scratch/ctlz.js) with the floor the one light source.
bool buildCanyon(Fixture &f)
{
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);   // a document primitive carries cards
    f.cube = f.s->createMesh(md);
    f.grey = matte(f.s, kFloorAlbedo);
    f.black = matte(f.s, 0.0f);
    if (!f.cube || !f.grey || !f.black) return false;
    f.floor = addBox(f, Vec3(0.0f, -0.1f, 0.0f), Vec3(50.0f, 0.2f, floorLength()), f.grey, true);
    addBox(f, Vec3(-0.25f, 5.0f, 0.0f), Vec3(0.5f, 10.0f, gLength), f.black, false);
    addBox(f, Vec3(12.25f, 5.0f, 0.0f), Vec3(0.5f, 10.0f, gLength), f.black, false);
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
    const auto cfg = enginetest::testEngineConfig(log);
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

GiParams highGi(unsigned bounces = 1u)
{
    GiParams gi;
    gi.cards = !gVoxelRoute;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    gi.numBounces = bounces;
    return gi;
}

unsigned gSettleMin = 60u;   ///< the frames a settle renders before it reads the rest
unsigned gFrames = 0u;       ///< every frame the settles rendered (printed by the far-cascade rows)
/// THE SETTLE TRACE (a measure, FAR-CASCADE-FIXTURE): FARSUN_SETTLE_TRACE=1 prints each settle's frames
/// and the clock, =2 every frame's rest count and the wall's mean (these points seen from this camera).
std::vector<Vec3> gTraceWall;
Vec3 gTraceCam;

/// Renders until the gather has HELD its rest mean (the picture is a function of the scene and
/// the camera), then returns the last readback.
GatherStatus settled(Fixture &f)
{
    GatherStatus g;
    unsigned i = 0u;
    const bool perFrame = std::getenv("FARSUN_SETTLE_TRACE") && std::string(std::getenv("FARSUN_SETTLE_TRACE")) == "2";
    for (; i < 600u; ++i) {
        f.e->renderOneFrame();
        if (perFrame) {
            const GatherStatus t = f.s->giStatus().gather;
            double sum = 0.0;
            int n = 0;
            for (const Vec3 &p : gTraceWall) {
                double px, py;
                if (t.irradiance.empty() || t.irradianceW != kSize || !project(gTraceCam, Vec3(0.0f, 4.0f, 0.0f) /* kTarget */, p, px, py))
                    continue;
                const float *q = &t.irradiance[(size_t(py) * t.irradianceW + size_t(px)) * 4u];
                if (q[3] < 0.5f) continue;
                sum += 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2];
                ++n;
            }
            std::printf("     frame %u at %.3f s: rest %u settle %u atRest %d wall mean %.5f (%d)\n", i, std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count(), t.restFrames, t.settleFrames,
                int(f.s->giStatus().giAtRest), n ? sum / n : -1.0, n);
        }
        if (i < gSettleMin) continue;
        g = f.s->giStatus().gather;
        if (g.restFrames > g.settleFrames + 4u && !g.irradiance.empty() && f.s->giStatus().giAtRest) break;
    }
    render(f.e, 4);
    gFrames += i + 1u + 4u;
    if (std::getenv("FARSUN_SETTLE_TRACE"))
        std::printf("   SETTLE %u frames (rest %u, settle %u) at %.2f s\n", i + 1u + 4u, f.s->giStatus().gather.restFrames,
                    f.s->giStatus().gather.settleFrames,
                    std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
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
    // h >= 4 m: the strip at the wall's foot (h = 2 m reads 1.30-1.33 on both routes) is
    // sky-energy-1's leaky shadow strip, its own lane - not what this suite measures.
    for (float h : { 4.0f, 6.0f, 8.0f })
        for (float z : { -4.0f, 0.0f, 4.0f }) p.push_back(Vec3(0.01f, h, z));
    return p;
}

const Vec3 kFarCam(8.0f, 5.0f, 40.0f), kNearCam(8.0f, 5.0f, 12.0f), kTarget(0.0f, 4.0f, 0.0f);

/// One camera's ratio: (gathered E/pi with the grey floor - with a black floor) / the closed
/// form, over the wall points both arms see. `perPoint` printed. Without `darkArm` the dark arm is
/// not rendered and reads 0 - what it measures where the grey surfaces are the scene's only light
/// (no sky, no ambient, black walls: 0.00000 at every wall point, the 80 m canyon at cascade 3).
double shareRatio(Fixture &f, const Vec3 &cam, const char *label, bool verbose = true, bool darkArm = true)
{
    f.view->setCamera(enginetest::testCameraDescLookAt(cam, kTarget));
    // THE DARK ARM blackens EVERY grey surface (the floor and the clutter): the closed form counts
    // the light of all of them, so the measured share must be all of them too.
    const auto paint = [&](MaterialId m) {
        f.s->attachMesh(f.floor, f.cube, m);
        for (NodeId n : f.greys) f.s->attachMesh(n, f.cube, m);
    };
    paint(f.grey);
    const GatherStatus lit = settled(f);
    GatherStatus dark;
    if (darkArm) {
        paint(f.black);
        dark = settled(f);
        paint(f.grey);
    }
    double sm = 0.0, se = 0.0;
    int n = 0;
    for (const Vec3 &p : wallPoints()) {
        const double a = gatheredAt(lit, cam, kTarget, p), b = darkArm ? gatheredAt(dark, cam, kTarget, p) : 0.0;
        if (a < 0.0 || b < 0.0) continue;
        const double expect = expectedEoverPi(p, f.boxes);
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

/// THE TERRACE (C.3; modeShare's clutter arm and FAR-SUN-2's diagnostic): 4 rows of tables on a 1.7 m
/// pitch along the street, two chairs each, every face grey (see modeShare for the shape's
/// reasoning). The rows are TILED along the canyon: as many tables as fit 1.5 m inside either end,
/// centred - 11 a row at the default 20 m (z -8.5 .. 8.5, the terrace every earlier row measured),
/// 46 at 80 m.
void addTerrace(Fixture &f)
{
    const auto clutterBox = [&](const Vec3 &c, const Vec3 &size) {
        f.greys.push_back(addBox(f, c, size, f.grey, true));
    };
    const int perRow = int(std::floor((gLength - 3.0f) / 1.7f + 1e-3f)) + 1;
    const float z0 = -(float(perRow - 1) * 1.7f) * 0.5f;
    for (int ix = 0; ix < 4; ++ix)
        for (int iz = 0; iz < perRow; ++iz) {
            const float x = 5.6f + 1.6f * float(ix), z = z0 + 1.7f * float(iz);
            clutterBox(Vec3(x, 0.74f, z), Vec3(0.7f, 0.04f, 0.7f));
            clutterBox(Vec3(x, 0.36f, z), Vec3(0.08f, 0.72f, 0.08f));
            for (int side = -1; side <= 1; side += 2) {
                const float cz = z + float(side) * 0.55f;
                clutterBox(Vec3(x, 0.45f, cz), Vec3(0.42f, 0.04f, 0.42f));
                clutterBox(Vec3(x, 0.695f, cz + float(side) * 0.195f), Vec3(0.42f, 0.45f, 0.03f));
                for (int lx = -1; lx <= 1; lx += 2)
                    for (int lz = -1; lz <= 1; lz += 2)
                        clutterBox(Vec3(x + float(lx) * 0.18f, 0.215f, cz + float(lz) * 0.18f),
                                   Vec3(0.03f, 0.43f, 0.03f));
            }
        }
}

int modeShare(Fixture &f, bool clutter)
{
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    // THE TERRACE'S CLOSED FORM IS SINGLE-SCATTER (expectedEoverPi: the first face a ray meets, its
    // DIRECT light), and with a bounce stored the grey tables, chairs and floor light each other - a
    // light the closed form leaves out (the clean canyon has no grey-to-grey exchange: the floor sees
    // black walls and no sky). So both arms of this mode run the DIRECT STORE ALONE (bounces 0,
    // BOUNCES-ZERO-1); the clean modes keep the shipped bounce.
    f.s->setGlobalIllumination(highGi(clutter ? 0u : 1u));
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
        CHECK_MSG(far >= 0.90 && far <= 1.10,
                  "THE FAR SUNLIT HIT: the sunlit floor's bounce into the self-shaded wall, the wall in cascade 3, "
                  "reads %.3f of the hemispherical law (bar 0.90-1.10)", far);
        CHECK_MSG(far > 0.0 && near > 0.0 && std::fabs(near / far - 1.0) <= 0.10,
                  "NEAR AND FAR AGREE: the near camera reads %.3f against the far %.3f (bar +-10 %%)", near, far);
        return 0;
    }
    const double clean = shareRatio(f, kFarCam, "CLEAN  ", false);
    // STREET CLUTTER, THE BISTRO'S SHAPE (C.3, RESUME item 3: raised slabs did NOT reproduce it - grey
    // slabs read 0.96 of the clean arm): a cafe TERRACE across the canyon's floor, 4 rows x 11 tables
    // on a 1.6 m x 1.7 m grid, every table with two chairs facing it from +-z. A table: a 0.7 m square
    // top (4 cm, 0.72-0.76 m up) on a 8 cm pedestal; a chair: a 0.42 m seat (0.43-0.47 m up) on four
    // 3 cm legs, a back 0.45 m tall on the side away from the table. Every face of them is grey and
    // in the closed form; the floor under them is in their shadow; a cascade-3 cell of the street
    // (1.875 m) holds a table, its chairs' backs and legs, and the floor - tops facing the sun, sides
    // facing every way, undersides dark, the merge's one normal turned off the street's.
    addTerrace(f);
    std::printf("   the terrace: %zu grey clutter boxes\n", f.greys.size());
    const double cluttered = shareRatio(f, kFarCam, "CLUTTER", false);
    CHECK_MSG(cluttered >= 0.90 && cluttered <= 1.10,
              "THE TERRACE READS ITS CLOSED FORM: %.3f of the single-scatter closed form (bar 0.90-1.10)", cluttered);
    CHECK_MSG(clean > 0.0 && cluttered > 0.0 && cluttered / clean >= 0.90 && cluttered / clean <= 1.10,
              "STREET CLUTTER IS COUNTED AS WHAT IT IS: the cluttered canyon reads %.3f of its closed form against "
              "the clean %.3f (bar +-10 %%)", cluttered, clean);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// gi.far_sun_share_c3 / gi.far_sun_clutter_c3 — THE FAR CASCADES, IN SECONDS (FAR-CASCADE-FIXTURE).
// The canyon at --length 80 (the terrace tiled along it), the camera on the street's axis at the
// pose --pose c2|c3 picks (poseCam; the rows run c3: the wall points and the floor their rays land
// on all in cascade 3), every wall point's distance from the camera and its cascade printed. One
// claim a row: the wall's share reads its closed form within 0.90-1.10 - the clean canyon (share_c3)
// and the terrace (clutter_c3, the store's dilution where it is largest: 1.875 m cells). Both at
// bounces 0, the direct store alone: the terrace's closed form is single-scatter, and the clean
// canyon has nothing to exchange (the floor sees black walls and no sky), so 0 and 1 are one light.

/// LAMBERT'S POLYGON FORM FACTOR (the hand integral): from a differential area at p with unit normal
/// n to a planar polygon wholly in front of it, F = (1 / 2 pi) |sum_i gamma_i n . u_i|, with v_i the
/// vertices from p, gamma_i the angle between v_i and v_i+1 and u_i the unit normal of their plane.
double lambertFormFactor(const double p[3], const double n[3], const std::vector<std::array<double, 2>> &floorPoly)
{
    double sum = 0.0;
    const size_t m = floorPoly.size();
    for (size_t i = 0; i < m; ++i) {
        const auto &a = floorPoly[i], &b = floorPoly[(i + 1) % m];
        const double va[3] = { a[0] - p[0], 0.0 - p[1], a[1] - p[2] }, vb[3] = { b[0] - p[0], 0.0 - p[1], b[1] - p[2] };
        const double c[3] = { va[1] * vb[2] - va[2] * vb[1], va[2] * vb[0] - va[0] * vb[2], va[0] * vb[1] - va[1] * vb[0] };
        const double cl = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (cl < 1e-15) continue;
        const double la = std::sqrt(va[0] * va[0] + va[1] * va[1] + va[2] * va[2]);
        const double lb = std::sqrt(vb[0] * vb[0] + vb[1] * vb[1] + vb[2] * vb[2]);
        const double gamma = std::acos(std::max(-1.0, std::min(1.0, (va[0] * vb[0] + va[1] * vb[1] + va[2] * vb[2]) / (la * lb))));
        sum += gamma * (n[0] * c[0] + n[1] * c[1] + n[2] * c[2]) / cl;
    }
    return std::fabs(sum) / (2.0 * kPi);
}

/// A floor polygon (x, z) clipped to the half-plane a x + b z + c >= 0 (Sutherland-Hodgman, one edge).
std::vector<std::array<double, 2>> clipFloor(const std::vector<std::array<double, 2>> &poly, double a, double b, double c)
{
    std::vector<std::array<double, 2>> out;
    for (size_t i = 0; i < poly.size(); ++i) {
        const auto &p = poly[i], &q = poly[(i + 1) % poly.size()];
        const double dp = a * p[0] + b * p[1] + c, dq = a * q[0] + b * q[1] + c;
        if (dp >= 0.0) out.push_back(p);
        if ((dp >= 0.0) != (dq >= 0.0)) {
            const double t = dp / (dp - dq);
            out.push_back({ p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1]) });
        }
    }
    return out;
}

/// THE CLEAN CANYON BY HAND, at a wall point p (x = 0.01, under the walls' top, |z| < gLength / 2):
/// E/pi(p) = L_floor x the form factor of the SUNLIT FLOOR p sees, every piece a polygon -
///   A   the sunlit strip inside the canyon: x from the receiving wall's shadow edge (its 10 m top
///       over tan 61 deg = 5.543 m) to the opposite wall (12), z over the walls' length;
///   B+- past either end of the walls (no wall shades it, the sun travels along x): x from p's
///       plane to 12, z from the end to the floor's end - between the walls' planes, so nothing
///       stands between p and it;
///   C+- past either end, beyond the opposite wall's plane (x 12 .. 25): seen where p's ray clears
///       the wall's end - the line through p and the wall's end corner (12, +-L/2), clipped.
/// The floor under the walls, the floor behind the opposite wall inside its length and behind p's
/// plane are not seen; the floor's side faces turn away from p. The closed form (expectedEoverPi,
/// a 160 x 160 stratified quadrature of rays against the analytic boxes) must agree within its own
/// quadrature error.
double cleanCanyonByHand(const Vec3 &pw)
{
    const double p[3] = { pw.x, pw.y, pw.z }, n[3] = { 1.0, 0.0, 0.0 };
    const double half = 0.5 * double(gLength), zEnd = 0.5 * double(floorLength());
    const double xs = 10.0 / std::tan(kSunElev);
    using Poly = std::vector<std::array<double, 2>>;
    double F = lambertFormFactor(p, n, Poly{ { xs, -half }, { 12.0, -half }, { 12.0, half }, { xs, half } });
    for (int side = -1; side <= 1; side += 2) {
        const double s = double(side);
        // B: x p.x .. 12, z from the end outwards (as s z from half to zEnd)
        F += lambertFormFactor(p, n, Poly{ { p[0], s * half }, { 12.0, s * half }, { 12.0, s * zEnd }, { p[0], s * zEnd } });
        // C: x 12 .. 25 past the end, where s z >= s p.z + (half - s p.z) (x - p.x) / (12 - p.x)
        const double k = (half - s * p[2]) / (12.0 - p[0]);
        Poly c = { { 12.0, s * half }, { 25.0, s * half }, { 25.0, s * zEnd }, { 12.0, s * zEnd } };
        c = clipFloor(c, -k, s, -(s * p[2]) + k * p[0]);   // s z - s p.z - k (x - p.x) >= 0
        if (c.size() >= 3) F += lambertFormFactor(p, n, c);
    }
    return floorRadiance() * F;
}

/// THE POSES (on the street's axis, x 8, y 5, looking at the wall's middle; the High tier's cascades
/// are camera-centred boxes of half-extent 2.5 / 5 / 15 / 60 m, cells 0.078 / 0.156 / 0.469 / 1.875 m):
///   c2  8 m down the street: every wall point in cascade 2 (4 .. 12 m off along z, 8 m across);
///   c3  40 m down the street: the wall points AND the floor their rays land on (x 0 .. 12,
///       z -12 .. 12) past cascade 2's 15 m and inside cascade 3's 60 m.
/// No pose holds the measured wall in cascade 1: its box is 10 m across, the wall points span 8 m
/// and a camera that sees them all through 45 degrees stands 10 m off them.
Vec3 poseCam(const std::string &pose)
{
    if (pose == "c2") return Vec3(8.0f, 5.0f, 8.0f);
    if (pose == "c3") return Vec3(8.0f, 5.0f, 40.0f);
    return Vec3(8.0f, 5.0f, float(std::atof(pose.c_str())));   // a number: the camera's z (a measure)
}
std::string gPose = "c3";

/// The finest cascade whose box holds w (-1 = none).
int cascadeOf(const GiStatus &g, const Vec3 &w)
{
    for (size_t i = 0; i < g.cascades.size(); ++i) {
        const auto &c = g.cascades[i];
        if (std::fabs(w.x - c.centre.x) <= c.halfSize && std::fabs(w.y - c.centre.y) <= c.halfSize &&
            std::fabs(w.z - c.centre.z) <= c.halfSize)
            return int(i);
    }
    return -1;
}

int modeCascade(Fixture &f, bool clutter)
{
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    if (clutter) addTerrace(f);
    f.s->setGlobalIllumination(highGi(0u));
    GatherTuning t;
    t.readback = true;
    f.s->setGatherTuning(t);
    const Vec3 cam = poseCam(gPose);
    std::printf("   the canyon %.0f m long (floor %.0f m), %s, %zu boxes; pose %s camera (%.0f, %.0f, %.0f)\n", double(gLength),
                double(floorLength()), clutter ? "the terrace tiled" : "clean", f.boxes.size(), gPose.c_str(), double(cam.x),
                double(cam.y), double(cam.z));
    gTraceWall = wallPoints();
    gTraceCam = cam;
    // ONE ARM, SETTLED BY THE GATHER'S OWN REST: the grey surfaces are the only light (the dark arm
    // reads 0.00000 at every wall point - the rows before this one measure it), and the settle reads
    // the held rest mean the first frame the rest law allows (restFrames > settleFrames + 4), with no
    // floor of frames under it: measured, the wall's mean holds its rest value from the 20th frame.
    gSettleMin = 0u;
    const double ratio = shareRatio(f, cam, clutter ? "TERRACE" : "CLEAN  ", true, false);
    if (std::getenv("FARSUN_SETTLE_TRACE"))
        std::printf("   SHARE DONE at %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // THE POSE: where the measured wall and the floor its rays land on lie
    const GiStatus g = f.s->giStatus();
    printCascades(f.s);
    const int want = gPose == "c2" ? 2 : 3;   // a numeric pose is held to cascade 3
    bool inPose = true;
    for (const Vec3 &p : wallPoints()) {
        const double d = std::sqrt(double(p.x - cam.x) * (p.x - cam.x) + double(p.y - cam.y) * (p.y - cam.y) +
                                   double(p.z - cam.z) * (p.z - cam.z));
        const int c = cascadeOf(g, p);
        std::printf("     wall (%.1f, %.1f): %.1f m from the camera, cascade %d\n", double(p.y), double(p.z), d, c);
        inPose = inPose && c == want;
    }
    for (const Vec3 &q : { Vec3(0.5f, 0.0f, -12.0f), Vec3(12.0f, 0.0f, -12.0f), Vec3(0.5f, 0.0f, 12.0f), Vec3(12.0f, 0.0f, 12.0f) }) {
        const double d = std::sqrt(double(q.x - cam.x) * (q.x - cam.x) + double(q.y - cam.y) * (q.y - cam.y) +
                                   double(q.z - cam.z) * (q.z - cam.z));
        const int c = cascadeOf(g, q);
        std::printf("     floor footprint (%.1f, %.1f): %.1f m from the camera, cascade %d\n", double(q.x), double(q.z), d, c);
        if (want == 3) inPose = inPose && c == want;   // cascade 2's 30 m box cannot hold the 24 m footprint and the wall
    }
    CHECK_MSG(inPose, "THE POSE: the wall points%s are in cascade %d", want == 3 ? " and the floor their rays land on" : "", want);
    if (!clutter) {
        // THE HAND INTEGRAL at one wall point: the closed form is exact at this length
        const Vec3 p(0.01f, 6.0f, 0.0f);
        const double quad = expectedEoverPi(p, f.boxes), hand = cleanCanyonByHand(p);
        std::printf("   THE HAND INTEGRAL at the wall (6, 0): Lambert's polygons %.6f, the closed form %.6f (%.4f)\n", hand, quad,
                    hand > 0 ? quad / hand : 0.0);
        CHECK_MSG(hand > 0.0 && std::fabs(quad / hand - 1.0) <= 0.01,
                  "THE CLOSED FORM IS EXACT AT %.0f m: it reads %.4f of the hand integral (bar +-1 %%, the quadrature's own error)",
                  double(gLength), hand > 0 ? quad / hand : 0.0);
    }
    CHECK_MSG(ratio >= 0.90 && ratio <= 1.10,
              "THE FAR CASCADE READS THE LAW: the %s canyon's wall, its rays answered in cascade %d, reads %.3f of its closed "
              "form (bar 0.90-1.10)", clutter ? "terraced" : "clean", want, ratio);
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
    // THE MIXED CELL (FAR-SUN-1 read F1): the casting slab and THREE NON-CASTING black panels facing
    // the same way IN ITS CELLS (2 cm thick, within 0.25 m of it; the far camera holds the slab in
    // cells of 0.47 m and up). The panels stop no light, so the slab must shade the strip exactly as
    // it does alone. A share taken of the raw coverage sums (1 - S_nc / S: 1 / 4 per half here)
    // scaled the slab's CAPPED coverage (1 per half) and marched the cell at 2 x 1/4: half the sun
    // through a casting slab. The share of the capped sums keeps it min( Sc, 1 ) = 1 per half.
    for (float dy : { -0.15f, 0.15f, 0.22f }) {
        const NodeId panel = addBox(f, Vec3(slabC.x, slabC.y + dy, slabC.z), Vec3(5.0f, 0.02f, 16.0f), f.black, false);
        f.s->setNodeCastShadow(panel, false);
    }
    const double mixed = wallMean(settled(f));
    std::printf("   the mixed cell: slab casting + three non-casting panels in its cells %.5f (the slab alone %.5f)\n",
                mixed, casting);
    CHECK_MSG(mixed > 0.0 && std::fabs(mixed / casting - 1.0) <= 0.03,
              "NON-CASTERS BESIDE A CASTER IN ONE CELL STOP NOTHING AND LEAK NOTHING: the slab with the "
              "panels reads %.5f against the slab alone %.5f (bar 3 %%)", mixed, casting);
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
    // THE FIRST READBACK, in frames (FAR-SUN-1 2026-10-13: a fixed 90-frame settle cut): the
    // instrument names every ray of the frame it read, whatever the GI's rest - the row waits for
    // the first frame the readback holds, nothing longer.
    GatherStatus g;
    for (int i = 0; i < 120 && g.rays.empty(); ++i) {
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
    // A BLACK PATCH of floor, 6 x 6 m, its top flush with the grey floor's, in the sunlit strip beside
    // sunlit grey floor; seen from the far camera (cascade 3, 1.875 m cells). THE SIZE IS THE
    // QUESTION'S (FAR-SUN-1 C.2): the hits counted lie within 1 m of its centre, so every texel that
    // holds them lies wholly inside the patch (1 + 1.875 < 3) and the STORE there is black - what the
    // row then measures is the READ: a kernel the size of the hit's footprint reads black, one that
    // takes metres of neighbours reads the grey round the patch. (At 4 x 4 m, hits within 1.5 m, the
    // texels at the hits straddled the edge: a PERFECT point read of the store returned 0.201 of the
    // grey, so no read could meet the bar - measured, the row's "store at the same hits" line.)
    // The patch REPLACES the floor where it lies (the grey floor is four boxes round a hole): a black
    // sheet laid on the grey floor kept the grey top hidden under it in the same texels, and the store
    // averaged it in (0.147 of the grey at hits wholly inside the patch).
    const Vec3 patchC(8.6f, -0.1f, -6.5f);
    const float px0 = patchC.x - 3.0f, px1 = patchC.x + 3.0f, pz0 = patchC.z - 3.0f, pz1 = patchC.z + 3.0f;
    f.s->setNodeVisible(f.floor, false);
    f.boxes.erase(f.boxes.begin());
    const auto greyFloor = [&](float x0, float x1, float z0, float z1) {
        addBox(f, Vec3(0.5f * (x0 + x1), -0.1f, 0.5f * (z0 + z1)), Vec3(x1 - x0, 0.2f, z1 - z0), f.grey, true);
    };
    greyFloor(-25.0f, 25.0f, pz1, 25.0f);
    greyFloor(-25.0f, 25.0f, -25.0f, pz0);
    greyFloor(-25.0f, px0, pz0, pz1);
    greyFloor(px1, 25.0f, pz0, pz1);
    addBox(f, patchC, Vec3(6.0f, 0.2f, 6.0f), f.black, false);
    f.s->setGlobalIllumination(highGi());
    f.view->setCamera(enginetest::testCameraDescLookAt(kFarCam, kTarget));
    // THE CASCADES FIRST: the voxels are built (their pipelines compile off the frame - ASYNC-SHADERS-1) and
    // the GI at rest before a ray is counted; until then a hit is answered by the decode, which shades the
    // patch exactly and proves nothing about the store (the row read 0.00000 on the decode once).
    GiVoxelVolume probe;
    for (int i = 0; i < 1200 && !(f.s->giVoxelVolume(3, probe) && probe.available); ++i) render(f.e, 1);
    CHECK_MSG(probe.available, "cascade 3 is built (its volume reads back; %zu cascades)",
              f.s->giStatus().cascades.size());
    settled(f);
    // THE STORE AT REST, read once: what a PERFECT point read returns at each counted hit is its own
    // texel's light (the store's resolution, not the read's kernel) - printed beside the read.
    GiVoxelVolume rest;
    const bool haveRest = f.s->giVoxelVolume(3, rest) && rest.available;
    const auto storeAt = [&](const float *hp) {
        if (!haveRest) return 0.0;
        const int ix = int(std::floor((hp[0] - rest.origin[0]) / rest.cell[0]));
        const int iy = int(std::floor((hp[1] + 1e-3f - rest.origin[1]) / rest.cell[1]));
        const int iz = int(std::floor((hp[2] - rest.origin[2]) / rest.cell[2]));
        if (ix < 0 || iy < 0 || iz < 0 || ix >= rest.width || iy >= rest.height || iz >= rest.depth) return 0.0;
        const size_t i = (size_t(iz) * rest.height * rest.width + size_t(iy) * rest.width + size_t(ix)) * 4u;
        const double a = rest.light[i + 3];
        return a > 0.0 ? double(rest.light[i + 1]) / a / std::max(double(rest.multiplier), 1e-9) : 0.0;
    };
    f.e->setArm("gather.hitsReadback", 1.0);
    render(f.e, 4);
    double litSum = 0.0, blackSum = 0.0, litStore = 0.0, blackStore = 0.0;
    unsigned litN = 0u, blackN = 0u, otherSource = 0u;
    for (int k = 0; k < 16; ++k) {
        render(f.e, 1);
        const GatherStatus g = f.s->giStatus().gather;
        for (const GatherRay &r : g.rays) {
            if (r.distance <= 0.0f || r.hitNormal[1] < 0.7f || std::fabs(r.hitPos[1]) > 0.1f) continue;
            // THE VOXEL ROUTE'S CASCADE 3 ONLY: what this row measures is the coarse store and its read
            if (r.source != GatherRay::Voxel || r.cascade != 3) { ++otherSource; continue; }
            const double l = 0.2126 * r.radiance[0] + 0.7152 * r.radiance[1] + 0.0722 * r.radiance[2];
            const float x = r.hitPos[0], z = r.hitPos[2];
            if (std::fabs(x - patchC.x) < 1.0f && std::fabs(z - patchC.z) < 1.0f) {
                blackSum += l; blackStore += storeAt(r.hitPos); ++blackN;
            } else if (x > 6.5f && x < 10.0f && z > -1.0f && z < 9.0f && r.sunVisibility > 0.5f) {
                litSum += l; litStore += storeAt(r.hitPos); ++litN;
            }
        }
    }
    f.e->setArm("gather.hitsReadback", 0.0);
    const double lit = litN ? litSum / litN : 0.0, black = blackN ? blackSum / blackN : 0.0;
    std::printf("   gather hits on the floor, answered by cascade 3: sunlit grey %.5f (%u rays), the black patch %.5f "
                "(%u rays); the closed form says %.5f and 0 (%u floor hits answered otherwise)\n", lit, litN, black,
                blackN, floorRadiance(), otherSource);
    std::printf("   the STORE at the same hits (each hit's own cascade-3 texel, green over opacity): sunlit grey %.5f, the "
                "black patch %.5f = %.3f of it (a perfect point read's answer)\n", litN ? litStore / litN : 0.0,
                blackN ? blackStore / blackN : 0.0, litStore > 0.0 ? (blackStore / std::max(blackN, 1u)) / (litStore / litN) : 0.0);
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

// ---------------------------------------------------------------------------------------------
// FAR-SUN-2 — THE PER-RAY MEASURE (a `measure` INSTRUMENT, run by hand and at the ship, never a merge
// row; spikes/far-sun-2/NOTES.md). mode "diag": the canyon (FARSUN_SCENE=terrace|clean), per camera
// (FARSUN_CAMS far,near,mid): the wall's share / closed form, the count of rays per source, then
// EVERY voxel-answered gather ray against ITS OWN hit's exact direct radiance (rho I cos DA(cos) x
// the hit's GPU sun ray; the grey/black face found from the analytic boxes), by cascade, face
// class and lit/shaded; the STORE at each hit (giVoxelVolume: the level-0 texel, the
// side facing the hit) against the hit's truth AND against the CELL MEANS of the exact direct
// light over the texel's faces: of the hit's half-axis (what a per-half store could hold) and of
// all its faces (what an isotropic mean holds).
namespace diag {

double faceRad(const Box &b, double cosL) { return (b.floor && cosL > 0.0) ? double(kFloorAlbedo) * double(kSunIntensity) * cosL * enginetest::disneyDiffuseAlbedo(cosL, 1.0) : 0.0; }

/// the box whose surface lies nearest p (0 = inside or on it)
int boxAt(const Vec3 &p, const std::vector<Box> &boxes)
{
    int best = -1;
    double bd = 1e30;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto &b = boxes[i].b;
        const double P[3] = { p.x, p.y, p.z }, A[3] = { b.mn.x, b.mn.y, b.mn.z }, B[3] = { b.mx.x, b.mx.y, b.mx.z };
        double d2 = 0.0;
        for (int a = 0; a < 3; ++a) {
            const double e = std::max({ A[a] - P[a], 0.0, P[a] - B[a] });
            d2 += e * e;
        }
        if (d2 < bd) { bd = d2; best = int(i); }
    }
    return best;
}

struct CellMean { double half[6] = {}, halfA[6] = {}, all = 0.0, allA = 0.0; };

/// The exact direct radiance's area means over the faces inside cell [c0, c1) (half-open), per
/// half-axis (q = h * 3 + a; h 0 the faces looking +a) and over all of them; 4 x 4 samples a face.
CellMean cellMean(const double c0[3], const double c1[3], const std::vector<Box> &boxes)
{
    CellMean m;
    for (const Box &bx : boxes) {
        const double A[3] = { bx.b.mn.x, bx.b.mn.y, bx.b.mn.z }, B[3] = { bx.b.mx.x, bx.b.mx.y, bx.b.mx.z };
        for (int a = 0; a < 3; ++a)
            for (int h = 0; h < 2; ++h) {
                const double pl = h == 0 ? B[a] : A[a];
                if (!(pl >= c0[a] && pl < c1[a])) continue;
                const int u = (a + 1) % 3, v = (a + 2) % 3;
                const double u0 = std::max(A[u], c0[u]), u1 = std::min(B[u], c1[u]);
                const double v0 = std::max(A[v], c0[v]), v1 = std::min(B[v], c1[v]);
                if (!(u1 > u0 && v1 > v0)) continue;
                const double area = (u1 - u0) * (v1 - v0);
                double n[3] = { 0, 0, 0 };
                n[a] = h == 0 ? 1.0 : -1.0;
                const double cosL = n[0] * kToSun.x + n[1] * kToSun.y + n[2] * kToSun.z;
                double sum = 0.0;
                if (bx.floor && cosL > 0.0) {
                    const double L = faceRad(bx, cosL);
                    int lit = 0;
                    for (int i = 0; i < 4; ++i)
                        for (int j = 0; j < 4; ++j) {
                            double P[3];
                            P[a] = pl + n[a] * 1e-3;
                            P[u] = u0 + (u1 - u0) * (i + 0.5) / 4.0;
                            P[v] = v0 + (v1 - v0) * (j + 0.5) / 4.0;
                            if (sunlit(Vec3(float(P[0]), float(P[1]), float(P[2])), boxes)) ++lit;
                        }
                    sum = L * lit / 16.0;
                }
                const int q = h * 3 + a;
                m.half[q] += sum * area;
                m.halfA[q] += area;
                m.all += sum * area;
                m.allA += area;
            }
    }
    return m;
}

struct Acc { double n = 0, read = 0, truth = 0, store = 0, cellHalf = 0, cellAll = 0; };

double lumOf(const float *c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

} // namespace diag

int modeDiag(Fixture &f)
{
    using namespace diag;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    const char *sceneEnv = std::getenv("FARSUN_SCENE");
    const bool terrace = !sceneEnv || std::string(sceneEnv) != "clean";
    const char *bEnv = std::getenv("FARSUN_BOUNCES");
    f.s->setGlobalIllumination(highGi(bEnv ? unsigned(std::atoi(bEnv)) : 0u));
    GatherTuning t;
    t.readback = true;
    f.s->setGatherTuning(t);
    // FARSUN_PRECLEAN=1: the clean canyon settled first and the terrace added after (gi.far_sun_clutter's order)
    if (std::getenv("FARSUN_PRECLEAN")) std::printf("   PRECLEAN clean %.3f\n", shareRatio(f, kFarCam, "PRECLEAN", false));
    if (terrace) addTerrace(f);
    const std::vector<int> arms = { 0 };   // the shipped read (the stage-1 arms are reverted)
    std::vector<std::pair<std::string, Vec3>> cams;
    {
        const char *e = std::getenv("FARSUN_CAMS");
        const std::string c = e ? e : "far";
        if (c.find("far") != std::string::npos) cams.push_back({ "far", kFarCam });
        if (c.find("near") != std::string::npos) cams.push_back({ "near", kNearCam });
        if (c.find("mid") != std::string::npos) cams.push_back({ "mid", Vec3(8.0f, 5.0f, 24.0f) });
        // the far-cascade poses (FAR-CASCADE-FIXTURE; meant for --length 80)
        for (const char *pose : { "c2", "c3" })
            if (c.find(pose) != std::string::npos) cams.push_back({ pose, poseCam(pose) });
    }
    if (const char *dEnv = std::getenv("FARSUN_DECODE")) f.e->setArm("gather.decodeHits", std::atof(dEnv));
    if (const char *hEnv = std::getenv("FARSUN_HITDIRECT")) f.e->setArm("gi.hitDirect", std::atof(hEnv));
    std::printf("   DIAG scene %s, bounces %s, %zu boxes, the canyon %.0f m\n", terrace ? "terrace" : "clean", bEnv ? bEnv : "0",
                f.boxes.size(), double(gLength));
    for (const auto &cam : cams) {
        for (int arm : arms) {
            char lbl[64];
            std::snprintf(lbl, sizeof lbl, "%s", cam.first.c_str());
            const double ratio = shareRatio(f, cam.second, lbl, false);
            printCascades(f.s);
            // THE PER-RAY READING: shareRatio ends on the dark arm and repaints grey - settle the grey
            // store again before a ray is read (a record of a frame the store was still rebuilding reads it black)
            settled(f);
            f.e->setArm("gather.hitsReadback", 1.0);
            std::map<std::string, Acc> acc;
            std::vector<GatherRay> rays;
            unsigned lastFrame = 0u, raysPer = 64u;
            unsigned long long sourceCount[7] = {};
            unsigned stride = 8u;
            std::vector<GatherRay> wallRays;
            for (int i = 0; i < 40 && rays.size() < 120000u; ++i) {
                render(f.e, 1);
                const GatherStatus g = f.s->giStatus().gather;
                if (g.rays.empty() || g.raysFrame == lastFrame) continue;
                lastFrame = g.raysFrame;
                raysPer = g.raysPerProbe ? g.raysPerProbe : raysPer;
                for (const GatherRay &r : g.rays)
                    if (r.source < 7u && r.distance > 0.0f) ++sourceCount[r.source];
                // THE WALL'S OWN PROBES (the cells over the share's wall points, +-1): every hit ray
                // of theirs, whatever answered it - what the share integrates
                stride = g.stride ? g.stride : stride;
                for (const GatherRay &r : g.rays) {
                    if (!(r.distance > 0.0f) || r.source == GatherRay::Sky || r.source == GatherRay::None) continue;
                    bool onWall = false;
                    for (const Vec3 &wp : wallPoints()) {
                        double px, py;
                        if (!project(cam.second, kTarget, wp, px, py)) continue;
                        const int cx = int(px) / int(stride), cy = int(py) / int(stride);
                        if (std::abs(int(r.cellX) - cx) <= 1 && std::abs(int(r.cellY) - cy) <= 1) { onWall = true; break; }
                    }
                    if (onWall) wallRays.push_back(r);
                }
                for (const GatherRay &r : g.rays)
                    if ((r.source == GatherRay::Voxel || (std::getenv("FARSUN_DECODE") && r.source == GatherRay::Decode)) && r.distance > 0.0f) rays.push_back(r);
            }
            f.e->setArm("gather.hitsReadback", 0.0);
            // THE STORE at the hits (arm 0 only)
            std::vector<GiVoxelVolume> vols(4);
            if (arm == 0)
                for (int c = 0; c < 4; ++c) {
                    f.s->giVoxelVolume(c, vols[size_t(c)]);
                    const GiVoxelVolume &v = vols[size_t(c)];
                    std::printf("   store c%d: available %d %dx%dx%d light %zu back %zu normal %zu covP %zu direct %zu\n", c, int(v.available),
                                v.width, v.height, v.depth, v.light.size(), v.lightBack.size(), v.normal.size(), v.coverageP.size(),
                                v.direct.size());
                    // THE STORE'S OWN CONSISTENCY: where nothing bounced (bounces 0, no sky) the total IS the direct
                    // volume at level 0 - texels where they differ, and by how much (luminance, k units)
                    if (v.direct.size() == v.light.size()) {
                        size_t differ = 0, filled = 0;
                        double sumT = 0.0, sumD = 0.0, worst = 0.0;
                        for (size_t i = 0; i + 3 < v.light.size(); i += 4) {
                            if (!(v.light[i + 3] > 0.0f)) continue;
                            ++filled;
                            const double lt = lumOf(&v.light[i]), ld = lumOf(&v.direct[i]);
                            sumT += lt; sumD += ld;
                            if (std::fabs(lt - ld) > 1e-3 * std::max(lt, 1e-3)) { ++differ; worst = std::max(worst, std::fabs(lt - ld)); }
                        }
                        std::printf("   STORE T vs D c%d: %zu filled texels, %zu differ, sum T %.4f sum D %.4f, worst |T-D| %.5f\n", c,
                                    filled, differ, sumT, sumD, worst);
                    }
                }
            std::map<long long, CellMean> cellCache;
            std::vector<double> cells;
            for (const auto &cs : f.s->giStatus().cascades) cells.push_back(double(cs.cell));
            unsigned agreeVis = 0u, nVis = 0u;
            for (const GatherRay &r : rays) {
                const Vec3 p(r.hitPos[0], r.hitPos[1], r.hitPos[2]);
                double n[3] = { r.hitNormal[0], r.hitNormal[1], r.hitNormal[2] };
                const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (nl < 0.5) continue;
                for (double &x : n) x /= nl;
                const int bi = boxAt(p, f.boxes);
                if (bi < 0) continue;
                const Box &bx = f.boxes[size_t(bi)];
                const double cosL = n[0] * kToSun.x + n[1] * kToSun.y + n[2] * kToSun.z;
                const double truth = faceRad(bx, cosL) * double(r.sunVisibility);
                const bool aLit = sunlit(Vec3(float(p.x + n[0] * 0.02), float(p.y + n[1] * 0.02), float(p.z + n[2] * 0.02)), f.boxes);
                if (cosL > 0.0) { ++nVis; agreeVis += (aLit == (r.sunVisibility > 0.5f)) ? 1u : 0u; }
                const double read = lumOf(r.radiance);
                const char *face = n[1] > 0.7 ? "up" : (n[1] < -0.7 ? "down" : "side");
                const char *lit = r.sunVisibility > 0.5f && cosL > 0.0 ? "lit" : "shade";
                const char *mat = bx.floor ? (bi == 0 ? "floor" : "clutter") : "black";
                const int an = std::fabs(n[0]) >= std::fabs(n[1]) ? (std::fabs(n[0]) >= std::fabs(n[2]) ? 0 : 2)
                                                                  : (std::fabs(n[1]) >= std::fabs(n[2]) ? 1 : 2);
                const int hq = (n[an] > 0.0 ? 0 : 1) * 3 + an;
                // the store and the cell means at this hit (arm 0)
                double store = -1.0, cHalf = -1.0, cAll = -1.0;
                const GiVoxelVolume &vol = vols[size_t(std::min<int>(r.cascade, 3))];
                const size_t nvox = size_t(vol.width) * size_t(vol.height) * size_t(vol.depth) * 4u;
                if (arm == 0 && vol.available && vol.lightBack.size() == nvox && vol.light.size() == nvox &&
                    vol.normal.size() == nvox && vol.coverageP.size() == nvox && vol.coverageN.size() == nvox) {
                    const double P[3] = { p.x, p.y, p.z };
                    int ix[3];
                    for (int a = 0; a < 3; ++a) ix[a] = int(std::floor((P[a] - vol.origin[a]) / vol.cell[a]));
                    // the texel along the normal's axis that holds the surface: the larger coverage of its half
                    double best = -1.0;
                    int pick[3] = { ix[0], ix[1], ix[2] };
                    for (int d = -1; d <= 1; ++d) {
                        int jx[3] = { ix[0], ix[1], ix[2] };
                        jx[an] += d;
                        if (jx[0] < 0 || jx[1] < 0 || jx[2] < 0 || jx[0] >= vol.width || jx[1] >= vol.height || jx[2] >= vol.depth) continue;
                        const size_t i = (size_t(jx[2]) * size_t(vol.height) + size_t(jx[1])) * size_t(vol.width) + size_t(jx[0]);
                        const double cov = (hq < 3 ? vol.coverageP : vol.coverageN)[i * 4 + size_t(an)];
                        if (cov > best + 1e-6) { best = cov; pick[0] = jx[0]; pick[1] = jx[1]; pick[2] = jx[2]; }
                    }
                    if (best < 0.0) goto noStore;
                    {
                    const size_t i = (size_t(pick[2]) * size_t(vol.height) + size_t(pick[1])) * size_t(vol.width) + size_t(pick[0]);
                    const double c = vol.light[i * 4 + 3], k = vol.multiplier > 0 ? vol.multiplier : 1.0;
                    if (c > 0.0) {
                        const float *L = &vol.light[i * 4], *Bk = &vol.lightBack[i * 4];
                        const double mean = lumOf(L) / c / k, back = lumOf(Bk) / c / k;
                        const double vn[3] = { vol.normal[i * 4] * 2.0 - 1.0, vol.normal[i * 4 + 1] * 2.0 - 1.0, vol.normal[i * 4 + 2] * 2.0 - 1.0 };
                        const bool agree = vn[0] * n[0] + vn[1] * n[1] + vn[2] * n[2] >= 0.0;
                        store = std::max(agree ? 2.0 * mean - back : back, 0.0);
                    }
                    const long long key = (((long long)std::min<int>(r.cascade, 3) * 4096 + pick[0]) * 4096 + pick[1]) * 4096 + pick[2];
                    auto it = cellCache.find(key);
                    if (it == cellCache.end()) {
                        double c0[3], c1[3];
                        for (int a = 0; a < 3; ++a) {
                            c0[a] = vol.origin[a] + pick[a] * double(vol.cell[a]);
                            c1[a] = c0[a] + double(vol.cell[a]);
                        }
                        it = cellCache.emplace(key, cellMean(c0, c1, f.boxes)).first;
                    }
                    const CellMean &cm = it->second;
                    cHalf = cm.halfA[hq] > 0.0 ? cm.half[hq] / cm.halfA[hq] : 0.0;
                    cAll = cm.allA > 0.0 ? cm.all / cm.allA : 0.0;
                    }
                }
                noStore:
                const std::string cas = "c" + std::to_string(int(r.cascade));
                // the read's path (jahVoxelSample): the directional chain when the footprint's lod > 0.5
                const double cell = r.cascade < cells.size() ? cells[r.cascade] : 1.0;
                const double lod = std::log2(std::max(double(r.distance) * std::sqrt(2.0 / raysPer), cell) / cell);
                const char *path = lod > 0.5 ? "dir" : "iso";
                for (const std::string &k : { cas + "|all", cas + "|" + face, cas + "|" + face + "|" + lit,
                                              cas + "|" + mat + "|" + lit, cas + "|" + path + "|" + lit }) {
                    Acc &a = acc[k];
                    a.n += 1;
                    a.read += read;
                    a.truth += truth;
                    if (store >= 0.0) { a.store += store; a.cellHalf += cHalf; a.cellAll += cAll; }
                }
            }
            std::printf("   PERRAY %s: share/closed %.3f, %zu voxel rays, sun ray agrees with the analytic box test on %.3f\n",
                        lbl, ratio, rays.size(), nVis ? double(agreeVis) / nVis : 0.0);
            {   // the wall's probes: sum read / sum truth over their hit rays (the share's own rays), and BY CASCADE
                double sr = 0.0, st = 0.0, n = 0.0, srV = 0.0, stV = 0.0;
                double cN[8] = {}, cR[8] = {}, cT[8] = {}, cDist[8] = {};
                for (const GatherRay &r : wallRays) {
                    const Vec3 p(r.hitPos[0], r.hitPos[1], r.hitPos[2]);
                    double nn[3] = { r.hitNormal[0], r.hitNormal[1], r.hitNormal[2] };
                    const double nl = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                    if (nl < 0.5) continue;
                    const int bi = boxAt(p, f.boxes);
                    if (bi < 0) continue;
                    const double cosL = (nn[0] * kToSun.x + nn[1] * kToSun.y + nn[2] * kToSun.z) / nl;
                    const double truth = faceRad(f.boxes[size_t(bi)], cosL) * double(r.sunVisibility);
                    // the probe's projection: the ray's re-weight times its cosine to the wall's normal (+x;
                    // the probe stands on the wall, so the hit's x over its distance)
                    const double wgt = double(r.weight) * std::max(0.0, double(r.hitPos[0]) / std::max(double(r.distance), 1e-3));
                    sr += wgt * lumOf(r.radiance);
                    st += wgt * truth;
                    n += 1;
                    if (r.source == GatherRay::Voxel) { srV += wgt * lumOf(r.radiance); stV += wgt * truth; }
                    const int ci = std::min<int>(r.cascade, 7);
                    cN[ci] += 1;
                    cR[ci] += wgt * lumOf(r.radiance);
                    cT[ci] += wgt * truth;
                    cDist[ci] += std::sqrt(double(p.x - cam.second.x) * (p.x - cam.second.x) + double(p.y - cam.second.y) * (p.y - cam.second.y) +
                                           double(p.z - cam.second.z) * (p.z - cam.second.z));
                }
                std::printf("   WALL PROBES %s: %.0f hit rays, read/truth %.3f (voxel-answered %.3f)\n", lbl, n,
                            st > 0 ? sr / st : -1.0, stV > 0 ? srV / stV : -1.0);
                for (int ci = 0; ci < 8; ++ci)
                    if (cN[ci] > 0)
                        std::printf("     WALL PROBES %s c%d: %.0f hit rays, the hit %.1f m from the camera on average, read/truth %.3f\n",
                                    lbl, ci, cN[ci], cDist[ci] / cN[ci], cT[ci] > 0 ? cR[ci] / cT[ci] : -1.0);
            }
            std::printf("   SOURCES %s: hits answered by card %llu, voxel %llu, decode %llu, far decode %llu, dropped %llu\n", lbl,
                        sourceCount[1], sourceCount[2], sourceCount[4], sourceCount[5], sourceCount[6]);
            for (const auto &kv : acc) {
                const Acc &a = kv.second;
                if (a.n < 30) continue;
                std::printf("     %-22s n %6.0f  read %.4f  truth %.4f  read/truth %6.3f", kv.first.c_str(), a.n, a.read / a.n,
                            a.truth / a.n, a.truth > 0 ? a.read / a.truth : -1.0);
                if (arm == 0)
                    std::printf("  | store %.4f cellHalf %.4f cellAll %.4f  store/truth %6.3f store/cellHalf %6.3f read/store %6.3f",
                                a.store / a.n, a.cellHalf / a.n, a.cellAll / a.n, a.truth > 0 ? a.store / a.truth : -1.0,
                                a.cellHalf > 0 ? a.store / a.cellHalf : -1.0, a.store > 0 ? a.read / a.store : -1.0);
                std::printf("\n");
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// gi.far_sun_lamp — A LAMP AND AN EMISSIVE SURFACE, AT THE HIT (FAR-SUN-2 stage 2). The store's direct
// volume holds EVERY light and the emissive surfaces, and a voxel-answered hit now takes all of its
// direct light at the hit (jah_rq_hit_direct.glsl) - so a lamp-lit scene must read its lamp and its
// glow there, exactly. The canyon with NO SUN: a point lamp (intensity 8, range 15, source radius
// 0.1) 2.5 m over the floor, and a grey crate on the floor that GLOWS (emissive 0.5). Bounces 0, the
// direct store alone, the voxel route, rays through their texels' centres (identical rays in both
// arms). Every voxel-answered gather ray against its own hit's exact radiance: rho I falloff(d) cos
// DA(cos) V (the analytic shadow test towards the lamp) plus the emissive on the crate. Paired with
// the measuring arm "gi.hitDirect" = 0 (the store whole) in one process.
//   1. the lamp-lit floor hits read their exact direct term within 0.90-1.10
//   2. the glowing crate's hits read their exact radiance (emissive + the lamp on it) within 0.90-1.10
//   3. NOTHING DARKENS: the mean over every voxel-answered hit reads at least 0.90 of the store's
//      (the same rays; the store is a cell mean of the same light)
namespace lamp {
const Vec3 kLampPos(6.0f, 2.5f, 0.0f);
constexpr float kLampI = 8.0f, kLampRange = 15.0f, kLampSrc = 0.1f, kGlow = 0.5f;
const Vec3 kCrateC(8.0f, 0.6f, -2.5f), kCrateSize(1.2f, 1.2f, 1.2f);

double falloff(double d) {
    const double x = d / kLampRange, x2 = x * x;
    double w = std::max(0.0, std::min(1.0, 1.0 - x2 * x2));
    w *= w;
    return w / std::max(d * d, double(kLampSrc) * kLampSrc);
}
/// The lamp visible from p (the segment p -> lamp misses every box)?
bool lampVisible(const Vec3 &p, const std::vector<Box> &boxes) {
    const Vec3 d(kLampPos.x - p.x, kLampPos.y - p.y, kLampPos.z - p.z);
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const Vec3 u(d.x / len, d.y / len, d.z / len);
    float t;
    const int k = nearest(p, u, BoxSet(boxes), t);
    return k < 0 || t >= len;
}
struct Acc { double n = 0, read = 0, base = 0, truth = 0; };
} // namespace lamp

int modeLamp(Fixture &f)
{
    using namespace lamp;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);
    f.cube = f.s->createMesh(md);
    f.grey = matte(f.s, kFloorAlbedo);
    f.black = matte(f.s, 0.0f);
    PbrParams glow;
    glow.albedo = Colour(kFloorAlbedo, kFloorAlbedo, kFloorAlbedo);
    glow.roughness = 1.0f;
    glow.workflow = PbrParams::Workflow::Specular;
    glow.ior = 1.0f;
    glow.specularColour = Colour(0.0f, 0.0f, 0.0f);
    glow.emissive = Colour(kGlow, kGlow, kGlow);
    const MaterialId glowMat = f.s->createPbrMaterial(glow);
    if (!f.cube || !f.grey || !f.black || !glowMat) { std::printf("FAIL: the materials\n"); return 1; }
    f.floor = addBox(f, Vec3(0.0f, -0.1f, 0.0f), Vec3(50.0f, 0.2f, 50.0f), f.grey, true);
    addBox(f, Vec3(-0.25f, 5.0f, 0.0f), Vec3(0.5f, 10.0f, 20.0f), f.black, false);
    addBox(f, Vec3(12.25f, 5.0f, 0.0f), Vec3(0.5f, 10.0f, 20.0f), f.black, false);
    addBox(f, kCrateC, kCrateSize, glowMat, true);
    const size_t crate = f.boxes.size() - 1u;
    const NodeId lampNode = f.s->createNode();
    f.s->setNodeTransform(lampNode, kLampPos, Quat(), Vec3(1.0f, 1.0f, 1.0f));
    LightDesc l;
    l.type = LightType::Point;
    l.intensity = kLampI;
    l.range = kLampRange;
    l.sourceRadius = kLampSrc;
    if (!f.s->setLight(lampNode, l)) { std::printf("FAIL: the lamp\n"); return 1; }
    f.s->setGlobalIllumination(highGi(0u));
    GatherTuning t;
    t.readback = true;
    t.rayJitterOff = true;
    f.s->setGatherTuning(t);
    const Vec3 cam = kNearCam;
    f.view->setCamera(enginetest::testCameraDescLookAt(cam, kTarget));
    // THE SETTLE BY THE GATHER'S OWN REST LAW alone (modeCascade's rule): no floor of frames under it
    gSettleMin = 0u;
    // the rays of one arm: [cell, ray] -> the record
    const auto rays = [&](double arm) {
        f.e->setArm("gi.hitDirect", arm);
        settled(f);
        f.e->setArm("gather.hitsReadback", 1.0);
        std::map<long long, GatherRay> out;
        unsigned last = 0u;
        for (int i = 0; i < 10 && out.empty(); ++i) {   // rays through texel centres: one frame's records are the set
            render(f.e, 1);
            const GatherStatus g = f.s->giStatus().gather;
            if (g.rays.empty() || g.raysFrame == last) continue;
            last = g.raysFrame;
            for (const GatherRay &r : g.rays)
                if (r.source == GatherRay::Voxel && r.distance > 0.0f)
                    out[((long long)r.cellY * 65536 + r.cellX) * 256 + r.ray] = r;
        }
        f.e->setArm("gather.hitsReadback", 0.0);
        return out;
    };
    const std::map<long long, GatherRay> base = rays(0.0), now = rays(1.0);
    f.e->setArm("gi.hitDirect", 1.0);
    std::map<std::string, Acc> acc;
    for (const auto &kv : now) {
        const auto b = base.find(kv.first);
        if (b == base.end()) continue;
        const GatherRay &r = kv.second;
        const Vec3 p(r.hitPos[0], r.hitPos[1], r.hitPos[2]);
        double n[3] = { r.hitNormal[0], r.hitNormal[1], r.hitNormal[2] };
        const double nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl < 0.5) continue;
        for (double &x : n) x /= nl;
        const int bi = diag::boxAt(p, f.boxes);
        if (bi < 0) continue;
        const Box &bx = f.boxes[size_t(bi)];
        const Vec3 q(float(p.x + n[0] * 0.02), float(p.y + n[1] * 0.02), float(p.z + n[2] * 0.02));
        const double dx = kLampPos.x - q.x, dy = kLampPos.y - q.y, dz = kLampPos.z - q.z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double c = (n[0] * dx + n[1] * dy + n[2] * dz) / d;
        const bool vis = c > 0.0 && lampVisible(q, f.boxes);
        double truth = (bx.floor && vis) ? double(kFloorAlbedo) * kLampI * falloff(d) * c * enginetest::disneyDiffuseAlbedo(c, 1.0) : 0.0;
        if (size_t(bi) == crate) truth += kGlow;
        const char *cls = size_t(bi) == crate ? "crate" : (bx.floor ? (vis ? "floor lit" : "floor shaded") : "black wall");
        for (const std::string &k : { std::string(cls), std::string("all") }) {
            Acc &a = acc[k];
            a.n += 1;
            a.read += diag::lumOf(r.radiance);
            a.base += diag::lumOf(b->second.radiance);
            a.truth += truth;
        }
    }
    for (const auto &kv : acc) {
        const Acc &a = kv.second;
        std::printf("   %-13s n %6.0f  read %.4f (store whole %.4f)  truth %.4f  read/truth %.3f  store/truth %.3f\n",
                    kv.first.c_str(), a.n, a.read / a.n, a.base / a.n, a.truth / a.n, a.truth > 0 ? a.read / a.truth : -1.0,
                    a.truth > 0 ? a.base / a.truth : -1.0);
    }
    const auto ratio = [&](const char *k) { const Acc &a = acc[k]; return a.truth > 0 && a.n >= 50 ? a.read / a.truth : -1.0; };
    const double lit = ratio("floor lit"), glowR = ratio("crate");
    const Acc &all = acc["all"];
    const double kept = all.base > 0 ? all.read / all.base : -1.0;
    CHECK_MSG(lit >= 0.90 && lit <= 1.10, "THE LAMP AT THE HIT: the lamp-lit floor's voxel-answered hits read %.3f of their exact "
              "direct term (bar 0.90-1.10; %.0f hits)", lit, acc["floor lit"].n);
    CHECK_MSG(glowR >= 0.90 && glowR <= 1.10, "THE GLOW AT THE HIT: the emissive crate's hits read %.3f of their exact radiance "
              "(emissive + the lamp; bar 0.90-1.10; %.0f hits)", glowR, acc["crate"].n);
    std::printf("   frames rendered (the view's counter): %u\n", f.s->giStatus().gather.frame);
    CHECK_MSG(kept >= 0.90, "NOTHING DARKENS: over every voxel-answered hit (%.0f, the same rays) the hit reads %.3f of the "
              "store whole (bar >= 0.90)", all.n, kept);
    return 0;
}

/// THE COST (FAR-SUN-2; a `measure` instrument, no ctest row): the gather's trace GPU ms on the terrace (far camera, the voxel
/// route, bounces 0), the arm "gi.hitDirect" 1 against 0, ABBA twice, 60 frames a block after 10, the camera nudged 1 cm every
/// frame so the gather traces every frame.
int modeCost(Fixture &f)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (!buildCanyon(f)) { std::printf("FAIL: the canyon\n"); return 1; }
    addTerrace(f);
    f.s->setGlobalIllumination(highGi(0u));
    settled(f);
    int k = 0;
    const auto nudge = [&]() {
        k ^= 1;
        f.view->setCamera(enginetest::testCameraDescLookAt(Vec3(kFarCam.x + 0.01f * float(k), kFarCam.y, kFarCam.z), kTarget));
    };
    const auto block = [&](double arm) {
        f.e->setArm("gi.hitDirect", arm);
        for (int i = 0; i < 10; ++i) { nudge(); render(f.e, 1); }
        double s = 0.0;
        int n = 0;
        for (int i = 0; i < 60; ++i) {
            nudge();
            render(f.e, 1);
            const float ms = f.s->giStatus().gather.traceMs;
            if (ms > 0.0f) { s += ms; ++n; }
        }
        return n ? s / n : -1.0;
    };
    f.e->setFrameMonitor(MonitorLevel::Review);   // the GPU timestamps (the pass's own timer)
    render(f.e, 4);
    std::printf("   COST trace ms (terrace, far):");
    for (double arm : { 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0 }) std::printf(" %s %.4f", arm > 0.5 ? "on" : "off", block(arm));
    std::printf("\n");
    f.e->setFrameMonitor(MonitorLevel::Off);
    f.e->setArm("gi.hitDirect", 1.0);
    return 0;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "share";
    gVoxelRoute = argc > 2 && std::string(argv[2]) == "voxels";
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--length") gLength = float(std::atof(argv[i + 1]));
        if (std::string(argv[i]) == "--pose") gPose = argv[i + 1];
        if (std::string(argv[i]) == "--size") kSize = unsigned(std::atoi(argv[i + 1]));
    }
    if (const char *m = std::getenv("FARSUN_SETTLE_MIN")) gSettleMin = unsigned(std::atoi(m));
    if (!(gLength >= 12.0f)) { std::printf("FAIL: --length %.1f (at least 12 m)\n", double(gLength)); return 1; }
    if (kSize < 128u) { std::printf("FAIL: --size %u (at least 128)\n", kSize); return 1; }
    if (std::getenv("FARSUN_SETTLE_TRACE")) std::printf("   START at %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
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
    else if (mode == "diag") rc = modeDiag(f);
    else if (mode == "lamp") rc = modeLamp(f);
    else if (mode == "cost") rc = modeCost(f);
    else if (mode == "cshare") rc = modeCascade(f, false);
    else if (mode == "cclutter") rc = modeCascade(f, true);
    else { std::printf("FAIL: unknown mode %s\n", mode.c_str()); return 1; }
    if (mode == "cshare" || mode == "cclutter") std::printf("   frames rendered by the settles: %u\n", gFrames);
    const auto stamp = [](const char *what) {
        if (std::getenv("FARSUN_SETTLE_TRACE"))
            std::printf("   %s at %.2f s\n", what, std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    stamp("MODE DONE");
    gEngine.reset();
    stamp("ENGINE GONE");
    if (rc) return rc;
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
