// SHADOW TOOLING — PHASE 0, THE ISOLATED SPIKE (SPECS/SHADOW_TOOLING_SPEC.md §9).
//
// The question the whole program hangs on (spec decision D6): can static and
// dynamic shadow maps share ONE depth atlas if the whole-atlas clear is replaced
// by a per-map clear QUAD, with `setStaticBranchingLights(true)` still on?
// Upstream says do not try (OgreCompositorShadowNode.h:305) — but upstream's
// reason is the whole-target clear, which is exactly what B2 removes.
//
// This binary answers it four ways, each selectable from argv so a crash in one
// cannot hide the others:
//
//   layout  — the packer's arithmetic, no device: N == 2 must reproduce the
//             historical R x 3.5R strip texel for texel (the negative gate's
//             foundation), and a 4096 base with 3+ maps must stop busting the
//             16384 limit the old strip busted.
//   ab      — the same scene rendered twice in one process: once with the shadow
//             node UPSTREAM's helper builds (injected before the engine can
//             build its own), once with ours. The images must be identical.
//             This is both the clear-quad correctness proof and the spec's T2.
//   four    — four shadow-casting lamps over a floor. With two focused maps two
//             of them are shadowless (the shipped defect); with four, all four
//             cast.
//   cachekinds — the lamp-map cache (ENGINE_CACHE_POLICY_SPEC P4/P5) on the
//             probe-capture and planar-reflect node instances, under the
//             validation layer (gated as shadow.cache_kinds). It replaced
//             `static`, the phase-0 proof that one light fixed by hand renders
//             once: the engine now fixes every point/spot light itself, every
//             frame, so a hand-fixed light is overwritten by design.
//   r3      — the spec's risk R3: a shadow-node definition deleted while the
//             hybrid's PCC probe workspaces still instantiate it.
//
// It reaches past the public Engine API on purpose (EnginePrivate.h, the same
// sources JahshakaEngine is built from): phase 0 exists to prove the MECHANISM
// before any verb, panel or document field is designed around it.
#include "EnginePrivate.h"
#include "jahshaka/engine/Engine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::OgreEngine;
using jahshaka::engine::detail::OgreView;
using jahshaka::engine::detail::planShadowAtlas;

static int failures = 0;
#define CHECK(cond, ...)                                                         \
    do {                                                                         \
        if (cond) { std::printf("ok: "); std::printf(__VA_ARGS__); }             \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); ++failures; }    \
        std::printf("\n");                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------
static void layoutCase()
{
    std::printf("-- the packer\n");
    {
        // The historical layout, which every shipped pixel suite was captured
        // against: PSSM split 0 at (0,0) R x R, splits 1-2 at (0,R) and (R/2,R)
        // at half size, and the two focused maps stacked beneath at y=1.5R and
        // y=2.5R, in a single R x 3.5R column.
        const auto plan = planShadowAtlas(2048u, 2u, 16384u);
        CHECK(plan.width == 2048u && plan.height == 7168u,
              "N=2 @2048 is the historical 2048x7168 strip (got %ux%u)", plan.width, plan.height);
        CHECK(plan.pssm[0].x == 0 && plan.pssm[0].y == 0 && plan.pssm[0].w == 2048,
              "split 0 at (0,0) 2048^2");
        CHECK(plan.pssm[1].x == 0 && plan.pssm[1].y == 2048 && plan.pssm[1].w == 1024,
              "split 1 at (0,2048) 1024^2");
        CHECK(plan.pssm[2].x == 1024 && plan.pssm[2].y == 2048,
              "split 2 at (1024,2048)");
        CHECK(plan.focused.size() == 2 && plan.focused[0].x == 0 && plan.focused[0].y == 3072 &&
              plan.focused[1].x == 0 && plan.focused[1].y == 5120,
              "focused maps at y=3072 and y=5120, column 0");
    }
    {
        const auto plan = planShadowAtlas(4096u, 3u, 16384u);
        CHECK(plan.width <= 16384u && plan.height <= 16384u && plan.focusedMaps == 3u,
              "N=3 @4096 fits inside 16384 (%ux%u) — the old strip needed 18432 rows",
              plan.width, plan.height);
    }
    for (unsigned n : { 2u, 4u, 8u, 16u }) {
        for (unsigned r : { 512u, 1024u, 2048u, 4096u }) {
            const auto plan = planShadowAtlas(r, n, 16384u);
            const bool fits = plan.width <= 16384u && plan.height <= 16384u;
            const bool all = plan.focusedMaps == n;
            std::printf("    R=%-5u N=%-3u -> %5ux%-5u  %8.1f MB  maps=%u\n", r, n,
                        plan.width, plan.height, double(plan.bytes()) / (1024.0 * 1024.0),
                        plan.focusedMaps);
            CHECK(fits, "R=%u N=%u stays inside the device limit", r, n);
            // 16 maps at a 4096 base want 16 x 16.8 M texels plus the PSSM
            // header: more than a 16384^2 texture HAS. The packer degrades to
            // the largest count that fits and says so, which is what the
            // resolution-tiered budget (spec §4.2) exists to avoid asking for.
            if (r == 4096u && n == 16u)
                CHECK(plan.focusedMaps == 14u && plan.focusedMaps < n,
                      "R=4096 N=16 degrades to what fits (%u maps)", plan.focusedMaps);
            else
                CHECK(all, "R=%u N=%u places every map", r, n);
        }
    }
    {   // 8192 is legal on a 16384 device only because the layout can use two columns.
        const auto plan = planShadowAtlas(8192u, 2u, 16384u);
        std::printf("    R=8192 N=2   -> %ux%u (%.0f MB)\n", plan.width, plan.height,
                    double(plan.bytes()) / (1024.0 * 1024.0));
        CHECK(plan.width <= 16384u && plan.height <= 16384u && plan.focusedMaps == 2u,
              "R=8192 N=2 fits in 16384^2");
    }
}

// ---------------------------------------------------------------------------
// scene helpers
// ---------------------------------------------------------------------------
static MeshData cubeMesh()
{
    MeshData d;
    const float h = 0.5f;
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const float fv[6][4][3] = {
        {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
        {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
        {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}},
        {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
        {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}},
        {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}} };
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            d.positions.insert(d.positions.end(), { fv[f][v][0], fv[f][v][1], fv[f][v][2] });
            d.normals.insert(d.normals.end(), { fn[f][0], fn[f][1], fn[f][2] });
        }
        const unsigned b = unsigned(f * 4);
        d.indices.insert(d.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return d;
}

/// A floor with four pillars, each lit from above by its own shadow-casting
/// point light. Every lamp's shadow lands in its own quadrant of the floor, so
/// one readback says which lamps got a map and which did not.
struct Room {
    Scene *scene = nullptr;
    NodeId lamps[4] = { 0, 0, 0, 0 };
    NodeId pillars[4] = { 0, 0, 0, 0 };
    NodeId floor = 0;
};

static const float kQuadX[4] = { -6.0f, 6.0f, -6.0f, 6.0f };
static const float kQuadZ[4] = { -6.0f, -6.0f, 6.0f, 6.0f };

static Room buildRoom(Engine *e, View *v, const char *name, int lampCount, bool spot = false)
{
    Room room;
    room.scene = e->createScene(name);
    if (!room.scene) return room;
    Scene *s = room.scene;
    v->setScene(s);
    s->setAmbient(Colour(0.02f, 0.02f, 0.025f), Colour(0.01f, 0.01f, 0.015f));
    const MeshId mesh = s->createMesh(cubeMesh());
    PbrParams white; white.albedo = Colour(0.85f, 0.85f, 0.85f); white.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(white);
    const NodeId floor = s->createNode();
    s->attachMesh(floor, mesh, mat);
    s->setNodeTransform(floor, Vec3(0, -0.1f, 0), Quat(), Vec3(40.0f, 0.2f, 40.0f));
    room.floor = floor;
    for (int i = 0; i < lampCount; ++i) {
        const NodeId pillar = s->createNode();
        s->attachMesh(pillar, mesh, mat);
        s->setNodeTransform(pillar, Vec3(kQuadX[i], 1.0f, kQuadZ[i]), Quat(), Vec3(1.2f, 2.0f, 1.2f));
        room.pillars[i] = pillar;
        const NodeId lamp = s->createNode();
        LightDesc d;
        d.type = spot ? LightType::Spot : LightType::Point;
        d.intensity = float(std::atof(std::getenv("JAH_SPIKE_INTENSITY") ? std::getenv("JAH_SPIKE_INTENSITY") : "0.25"));
        d.range = 12.0f;
        d.castShadows = true;
        d.spotAngleDegrees = 50.0f;
        s->setLight(lamp, d);
        // Above and slightly outward, so the pillar's shadow falls towards the
        // centre of the room where the probe row reads it.
        s->setNodeTransform(lamp, Vec3(kQuadX[i] * 1.35f, 4.5f, kQuadZ[i] * 1.35f), Quat(),
                            Vec3(1, 1, 1));
        room.lamps[i] = lamp;
    }
    CameraDesc c;
    c.position = Vec3(0.0f, 22.0f, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);   // straight down
    c.fovDegrees = 60.0f;
    v->setCamera(c);
    v->setShadows(true);
    return room;
}

static void render(Engine *e, int frames = 4) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static int lum(const Image &img, unsigned x, unsigned y)
{
    const Colour c = img.at(x, y);
    return int((c.r + c.g + c.b) / 3.0f * 255.0f + 0.5f);
}

/// World (x,z) on the floor plane -> pixel, for the fixed overhead camera
/// buildRoom sets: y=22, 60 degrees vertical, square view, looking straight
/// down with screen-up = world -Z.
static const float kCamHeight = 22.0f;
static const float kHalfExtent = 12.702f;   // 22 * tan(30 degrees)

static int probe(const Image &img, float x, float z)
{
    const float u = (x / kHalfExtent) * 0.5f + 0.5f;
    const float v = (z / kHalfExtent) * 0.5f + 0.5f;
    const int px = int(u * float(img.width));
    const int py = int(v * float(img.height));
    if (px < 1 || py < 1 || px >= int(img.width) - 1 || py >= int(img.height) - 1) return -1;
    // 3x3 average: the probe is a claim about a REGION, and one texel of PCF
    // penumbra should not decide a gate.
    int sum = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            sum += lum(img, unsigned(px + dx), unsigned(py + dy));
    return sum / 9;
}

/// Lamp `i` is at 1.35x its pillar's position and 4.5 up; the pillar is 1.2
/// wide and 2 tall at +-6. Geometry says its shadow covers the floor between
/// r = 4.6 and r = 7.6 along that quadrant's diagonal (r measured from the
/// room's centre), so r = 6.1 is inside the shadow and the same r offset three
/// units sideways is outside it, at nearly the same distance from the lamp.
///
/// Returns shadowed/lit as a RATIO: the absolute level is not comparable across
/// map counts, because winning an atlas slot also moves a light from the
/// Forward+ path to the pass buffer and the two do not agree on brightness
/// (attenCase measures it).
static double lampShadowRatio(const Image &img, int i, int *shadowOut, int *litOut)
{
    const float sx = kQuadX[i] < 0 ? -1.0f : 1.0f;
    const float sz = kQuadZ[i] < 0 ? -1.0f : 1.0f;
    const float ux = sx * 0.70711f, uz = sz * 0.70711f;      // along the diagonal
    const float wx = sx * 0.70711f, wz = -sz * 0.70711f;     // across it
    const int shadowed = probe(img, ux * 6.1f, uz * 6.1f);
    const int lit      = probe(img, ux * 6.1f + wx * 3.0f, uz * 6.1f + wz * 3.0f);
    if (shadowOut) *shadowOut = shadowed;
    if (litOut) *litOut = lit;
    return lit > 0 ? double(shadowed) / double(lit) : 1.0;
}

/// A coarse luminance map of the frame, for eyeballing what a case rendered.
static void dumpGrid(const Image &img)
{
    for (unsigned gy = 0; gy < 16u; ++gy) {
        std::printf("    ");
        for (unsigned gx = 0; gx < 16u; ++gx) {
            const unsigned x = gx * img.width / 16u + img.width / 32u;
            const unsigned y = gy * img.height / 16u + img.height / 32u;
            std::printf("%4d", lum(img, x, y));
        }
        std::printf("\n");
    }
}

static double meanLum(const Image &img)
{
    double sum = 0.0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
        sum += (img.rgba[i] + img.rgba[i+1] + img.rgba[i+2]) / 3.0;
    return sum / double(img.rgba.size() / 4);
}

static unsigned long long checksum(const Image &img)
{
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char b : img.rgba) { h ^= b; h *= 1099511628211ull; }
    return h;
}

static EngineConfig config(const char *log)
{
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = log;
    return cfg;
}

// ---------------------------------------------------------------------------
// ab: upstream's shadow node vs ours, same scene, same process
// ---------------------------------------------------------------------------
// The swap: drop the view's workspace exactly as an atlas rebuild does, delete
// the definition the engine built, declare upstream's in its place, and put the
// workspace back. (It cannot be injected before Engine::create returns —
// neither the CompositorManager2 nor the render system's capabilities exist
// until the first render target does.) The parameters below are the ones this
// engine passed to the helper before the definition became ours
// (OgreEngine.cpp @3feedf8f).
static void swapToUpstreamShadowNode(Engine *engine, View *v, unsigned R)
{
    auto *view = static_cast<OgreView *>(v);
    Ogre::Root *root = Ogre::Root::getSingletonPtr();
    Ogre::CompositorManager2 *cm = root->getCompositorManager2();
    const bool dropped = view->dropWorkspaceForShadowRebuild();
    if (cm->hasShadowNodeDefinition(OgreView::kShadowNodeName))
        cm->removeShadowNodeDefinition(OgreView::kShadowNodeName);
    const Ogre::uint32 H = std::max(128u, R / 2u);
    Ogre::ShadowNodeHelper::ShadowParamVec params;
    Ogre::ShadowNodeHelper::ShadowParam p;
    memset(&p, 0, sizeof(p));
    p.technique = Ogre::SHADOWMAP_PSSM;
    p.numPssmSplits = 3u;
    p.resolution[0].x = R; p.resolution[0].y = R;
    for (size_t i = 1u; i < 4u; ++i) { p.resolution[i].x = H; p.resolution[i].y = H; }
    p.atlasStart[0].x = 0u; p.atlasStart[0].y = 0u;
    p.atlasStart[1].x = 0u; p.atlasStart[1].y = R;
    p.atlasStart[2].x = H;  p.atlasStart[2].y = R;
    p.supportedLightTypes = 0u;
    p.addLightType(Ogre::Light::LT_DIRECTIONAL);
    params.push_back(p);
    p.technique = Ogre::SHADOWMAP_FOCUSED;
    p.resolution[0].x = R; p.resolution[0].y = R;
    p.atlasStart[0].x = 0u; p.atlasStart[0].y = R + H;
    p.supportedLightTypes = 0u;
    p.addLightType(Ogre::Light::LT_POINT);
    p.addLightType(Ogre::Light::LT_SPOTLIGHT);
    params.push_back(p);
    p.atlasStart[0].y = R + H + R;
    params.push_back(p);
    Ogre::ShadowNodeHelper::createShadowNodeWithSettings(
        cm, root->getRenderSystem()->getCapabilities(), OgreView::kShadowNodeName, params,
        false, 1024u, 0.95f, 1.0f, 0.125f, 0.313f, 2u, jahshaka::engine::detail::kVisibleBit);
    if (dropped) view->recreateWorkspaceAfterShadowRebuild();
}

static bool renderRoom(bool upstreamNode, Image &out, const char *log, int lamps, bool spot)
{
    std::string err;
    EngineConfig cfg = config(log);
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); ++failures; return false; }
    View *v = engine->createOffscreenView("spike", 160, 160, Colour(0, 0, 0));
    if (!v) { std::printf("FAIL: view\n"); ++failures; return false; }
    Room room = buildRoom(engine.get(), v, "room", lamps, spot);
    if (!room.scene) { std::printf("FAIL: scene\n"); ++failures; return false; }
    render(engine.get(), 2);                     // the workspace exists after this
    if (upstreamNode) swapToUpstreamShadowNode(engine.get(), v, 2048u);
    render(engine.get(), 8);
    const bool ok = v->readPixels(out);
    engine.reset();
    return ok;
}

static void abCase()
{
    std::printf("-- upstream's shadow node vs the clear-quad node, two casters\n");
    Image upstream, ours;
    if (!renderRoom(true, upstream, "spike-ab-upstream.log", 2, false)) return;
    if (!renderRoom(false, ours, "spike-ab-ours.log", 2, false)) return;
    CHECK(upstream.width == ours.width && upstream.height == ours.height,
          "same size (%ux%u vs %ux%u)", upstream.width, upstream.height, ours.width, ours.height);
    size_t differing = 0;
    int worst = 0;
    for (size_t i = 0; i < std::min(upstream.rgba.size(), ours.rgba.size()); ++i) {
        const int d = std::abs(int(upstream.rgba[i]) - int(ours.rgba[i]));
        if (d) { ++differing; worst = std::max(worst, d); }
    }
    std::printf("    upstream checksum %016llx, ours %016llx; %zu byte(s) differ, worst %d\n",
                checksum(upstream), checksum(ours), differing, worst);
    CHECK(differing == 0, "the clear-quad atlas renders the SAME image as upstream's whole-atlas clear");
    int lit = 0;
    for (unsigned i = 0; i < ours.rgba.size(); i += 4) if (ours.rgba[i] > 30) ++lit;
    CHECK(lit > 200, "the picture is actually lit (%d bright pixels) — a black frame would match trivially", lit);
}

// ---------------------------------------------------------------------------
// four: the defect, and the fix
// ---------------------------------------------------------------------------
static void fourCase()
{
    std::string err;
    EngineConfig cfg = config("spike-four.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); ++failures; return; }
    View *v = engine->createOffscreenView("spike", 200, 200, Colour(0, 0, 0));
    Room room = buildRoom(engine.get(), v, "room4", 4, false);
    auto *impl = static_cast<OgreEngine *>(engine.get());

    std::printf("-- four casters, two focused maps (today's shipped atlas)\n");
    render(engine.get(), 8);
    Image two;
    v->readPixels(two);
    dumpGrid(two);
    int shadowed2 = 0;
    for (int i = 0; i < 4; ++i) {
        int dark = 0, lit = 0;
        const double r = lampShadowRatio(two, i, &dark, &lit);
        std::printf("    lamp %d: shadow probe %3d, lit probe %3d, ratio %.2f\n", i, dark, lit, r);
        if (r < 0.55) ++shadowed2;
    }
    CHECK(shadowed2 <= 2, "with two maps at most two lamps cast (%d did) — the shipped defect", shadowed2);

    // CONTROL: the same picture 8 frames later, with nothing changed. Anything
    // that moves here is temporal (warm-up, exposure, a settling cache) and must
    // not be read as a consequence of the map count.
    render(engine.get(), 8);
    Image again;
    v->readPixels(again);
    long drift = 0;
    for (size_t i = 0; i < again.rgba.size(); ++i)
        drift += std::abs(int(again.rgba[i]) - int(two.rgba[i]));
    std::printf("    control: mean per-byte drift over 8 idle frames = %.2f\n",
                double(drift) / double(again.rgba.size()));

    std::printf("-- the same scene with four focused maps\n");
    CHECK(impl->rebuildShadowAtlas(impl->shadowResolution(), 4u, true), "the atlas rebuilt at N=4");
    render(engine.get(), 8);
    Image four;
    v->readPixels(four);
    dumpGrid(four);
    int shadowed4 = 0;
    for (int i = 0; i < 4; ++i) {
        int dark = 0, lit = 0;
        const double r = lampShadowRatio(four, i, &dark, &lit);
        std::printf("    lamp %d: shadow probe %3d, lit probe %3d, ratio %.2f\n", i, dark, lit, r);
        if (r < 0.55) ++shadowed4;
    }
    CHECK(shadowed4 == 4, "all four lamps cast a shadow with four maps (%d did)", shadowed4);
    std::printf("    mean luminance: N=2 %.2f, N=4 %.2f\n", meanLum(two), meanLum(four));
    // ...and back down, to see whether the brightness follows the map count.
    impl->rebuildShadowAtlas(impl->shadowResolution(), 2u, true);
    render(engine.get(), 8);
    Image back;
    v->readPixels(back);
    std::printf("    mean luminance back at N=2: %.2f\n", meanLum(back));
    engine.reset();
}

// ---------------------------------------------------------------------------
// r3: the definition deleted under the hybrid's probe workspaces
// ---------------------------------------------------------------------------
static void r3Case()
{
    std::printf("-- R3: rebuild the shadow atlas while hybrid-GI probe workspaces hold the node\n");
    std::string err;
    EngineConfig cfg = config("spike-r3.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); ++failures; return; }
    View *v = engine->createOffscreenView("spike", 160, 160, Colour(0, 0, 0));
    auto *impl = static_cast<OgreEngine *>(engine.get());
    // Hold the derivation still: this mode is about the EXPLICIT rebuild that a
    // Shadow Quality change performs, between frames.
    engine->setShadowMapBudget(2u);
    Room room = buildRoom(engine.get(), v, "roomGi", 3, false);
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::High;          // probeShadows resolves true at high
    gi.probeShadows = GiToggle::On;
    room.scene->setGlobalIllumination(gi);
    render(engine.get(), 8);
    const GiStatus st = room.scene->giStatus();
    std::printf("    gi: probes %d, pccBound %d, probeShadows %d\n",
                st.probeCount, int(st.pccBound), int(st.probeShadows));
    CHECK(st.probeShadows, "the probe captures really are shadowed (the precondition for R3)");
    std::printf("    rebuilding the atlas (this is where the definition dies)\n");
    std::fflush(stdout);
    impl->rebuildShadowAtlas(impl->shadowResolution(), 4u, true);
    render(engine.get(), 4);
    Image img;
    CHECK(v->readPixels(img), "the engine survived the rebuild and still renders");
    engine.reset();
    std::printf("    survived teardown too\n");
}

// ---------------------------------------------------------------------------
// atten: is a mapped caster BRIGHTER than the same light without a map?
// ---------------------------------------------------------------------------
// Chasing the spike's surprise: raising the focused-map count from 2 to 4 made
// the whole picture 38% brighter, reversibly. Shadows can only darken, so the
// brightness must follow WHICH LIGHT PATH each lamp takes — Forward+ clustered
// for unmapped lights, the pass buffer for mapped ones.
static void attenCase()
{
    std::string err;
    EngineConfig cfg = config("spike-atten.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); ++failures; return; }
    View *v = engine->createOffscreenView("spike", 200, 200, Colour(0, 0, 0));
    auto *impl = static_cast<OgreEngine *>(engine.get());
    Room room = buildRoom(engine.get(), v, "roomAtten", 4, false);
    impl->rebuildShadowAtlas(impl->shadowResolution(), 4u, true);
    render(engine.get(), 8);
    Image mapped;
    v->readPixels(mapped);
    std::printf("    4 casters, 4 maps (pass buffer):        mean %.2f\n", meanLum(mapped));

    // The same four lights, now not casting at all: every one of them goes
    // through Forward+ instead, and nothing else in the scene changes.
    for (int i = 0; i < 4; ++i) {
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = float(std::atof(std::getenv("JAH_SPIKE_INTENSITY") ? std::getenv("JAH_SPIKE_INTENSITY") : "0.25"));
        d.range = 12.0f;
        d.castShadows = false;
        room.scene->setLight(room.lamps[i], d);
    }
    render(engine.get(), 8);
    Image forward;
    v->readPixels(forward);
    std::printf("    4 non-casters (Forward+ clustered):     mean %.2f\n", meanLum(forward));

    // And with shadows switched off on the VIEW: the lights still cast per the
    // document, but no shadow node runs, so they are all Forward+ again.
    for (int i = 0; i < 4; ++i) {
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = float(std::atof(std::getenv("JAH_SPIKE_INTENSITY") ? std::getenv("JAH_SPIKE_INTENSITY") : "0.25"));
        d.range = 12.0f;
        d.castShadows = true;
        room.scene->setLight(room.lamps[i], d);
    }
    v->setShadows(false);
    render(engine.get(), 8);
    Image noShadowNode;
    v->readPixels(noShadowNode);
    std::printf("    4 casters, view shadows off:            mean %.2f\n", meanLum(noShadowNode));
    engine.reset();
}

// ---------------------------------------------------------------------------
// cachekinds: THE LAMP-MAP CACHE ON THE PROBE AND REFLECT NODE INSTANCES, UNDER
// THE VALIDATION LAYER (ENGINE_CACHE_POLICY_SPEC §6 — E2's opening probe, kept
// as the gate row `shadow.cache_kinds`).
//
// The view node's static path was proven by SHADOW_TOOLING P0 (`static`
// above); a PROBE-capture node and a planar REFLECT node had never held a fixed
// light. Two lamps, no sun, a shadowed planar floor and four shadowed probes:
//   reflect: once cached, the slot's node renders NO lamp pass at rest;
//   probe:   a capture after refreshShadows renders each lamp map on its FIRST
//            face only (16 passes, not 6 x 16 = 96); the next capture none;
//   and not one "Validation Error" (the ctest row fails on the string).
//
// WHAT THE PROBE FOUND (2026-09-12): with every pass of a node skipped (all its
// lamps cached, no sun) the first touch of its DISCARDABLE atlas in a frame was
// the scene pass sampling it, and Ogre's barrier solver threw ("Transitioning
// texture from Undefined to a read-only layout ... keep_content",
// OgreResourceTransition.cpp:185) — mid-analysis, leaving the pass's colour
// target recorded as a render target, so the next frame's barrier was
// COLOR_ATTACHMENT -> COLOR_ATTACHMENT over a SHADER_READ image: a validation
// error on the mirror's and the probes' targets. The caching atlas is now
// keep_content (buildShadowNode); the ctest row keeps it that way.
namespace {
struct KindCounter final : public Ogre::CompositorWorkspaceListener {
    Ogre::IdString nodeName;
    unsigned total = 0;
    std::vector<unsigned> perMap;
    explicit KindCounter(const char *n) : nodeName(n) { perMap.assign(3u + 16u, 0u); }
    void passPreExecute(Ogre::CompositorPass *pass) override {
        const Ogre::CompositorNode *node = pass->getParentNode();
        if (!node || node->getName() != nodeName) return;
        ++total;
        const Ogre::uint32 idx = pass->getDefinition()->mShadowMapIdx;
        if (idx < perMap.size()) ++perMap[idx];
    }
    void reset() { total = 0; std::fill(perMap.begin(), perMap.end(), 0u); }
    unsigned lamps() const { unsigned n = 0; for (size_t i = 3; i < perMap.size(); ++i) n += perMap[i]; return n; }
};
void attach(KindCounter &c, const std::vector<Ogre::CompositorWorkspace *> &ws) {
    for (Ogre::CompositorWorkspace *w : ws) {
        const auto &ls = w->getListeners();
        if (std::find(ls.begin(), ls.end(), &c) == ls.end()) w->addListener(&c);
    }
}
void detach(KindCounter &c, const std::vector<Ogre::CompositorWorkspace *> &ws) {
    for (Ogre::CompositorWorkspace *w : ws) w->removeListener(&c);
}
}   // namespace

static void cacheKindsCase()
{
    std::printf("-- the lamp-map cache on probe-node and reflect-node instances\n");
    std::string err;
    EngineConfig cfg = config("spike-cachekinds.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); ++failures; return; }
    View *v = engine->createOffscreenView("spike", 256, 256, Colour(0, 0, 0));
    engine->setShadowMapBudget(2u);
    Room room = buildRoom(engine.get(), v, "roomKinds", 2, false);
    auto *scene = static_cast<jahshaka::engine::detail::OgreScene *>(room.scene);
    PlanarReflectionParams pr; pr.budget = 1; pr.resolution = 256; pr.shadows = true;
    CHECK(room.scene->setPlanarReflections(pr), "planar arm up");
    CHECK(room.scene->setNodePlanarReflector(room.floor, true), "the floor is a reflector");
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Low;
    gi.probeShadows = GiToggle::On;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 0;               // no captures but the ones this case asks for
    gi.dynamicProbes = 0;
    room.scene->setGlobalIllumination(gi);
    render(engine.get(), 12);
    const GiStatus gst = room.scene->giStatus();
    std::printf("    gi: probes %d, probeShadows %d, planar active %d\n", gst.probeCount,
                int(gst.probeShadows), room.scene->activePlanarReflectors());
    CHECK(gst.probeShadows && gst.probeCount == 4, "four shadowed probes");

    std::vector<Ogre::CompositorWorkspace *> probeWs, reflWs;
    scene->shadowWorkspaces(jahshaka::engine::detail::ShadowNodeKind::Probe, probeWs);
    scene->shadowWorkspaces(jahshaka::engine::detail::ShadowNodeKind::Reflect, reflWs);
    CHECK(probeWs.size() == 4u && reflWs.size() == 1u, "every probe and the slot are reachable");
    std::vector<Ogre::CompositorShadowNode *> probeNodes, reflNodes;
    for (auto *w : probeWs) if (auto *n = w->findShadowNode(OgreView::kProbeShadowNodeName)) probeNodes.push_back(n);
    for (auto *w : reflWs) if (auto *n = w->findShadowNode(OgreView::kReflectShadowNodeName)) reflNodes.push_back(n);
    CHECK(probeNodes.size() == 4u && reflNodes.size() == 1u, "each workspace instantiated its node");
    if (probeNodes.size() != 4u || reflNodes.size() != 1u) { engine.reset(); return; }
    // THE ENGINE did the assignment: both lamps fixed on every instance.
    Ogre::Light *l0 = scene->ogreLight(room.lamps[0]), *l1 = scene->ogreLight(room.lamps[1]);
    unsigned fixedOk = 0;
    for (auto *n : probeNodes) {
        const auto &held = n->getShadowCastingLights();
        bool has0 = false, has1 = false;
        for (size_t i = 1; i < held.size(); ++i) {
            if (held[i].isStatic && held[i].light == l0) has0 = true;
            if (held[i].isStatic && held[i].light == l1) has1 = true;
        }
        if (has0 && has1) ++fixedOk;
    }
    CHECK(fixedOk == 4u, "every probe node holds both lamps cached (%u of 4)", fixedOk);

    KindCounter probeC(OgreView::kProbeShadowNodeName), reflC(OgreView::kReflectShadowNodeName);
    attach(probeC, probeWs);
    attach(reflC, reflWs);
    for (auto *p : scene->parallaxCorrectedCubemap()->getProbes()) p->mDirty = true;
    render(engine.get(), 2);                    // every probe holds current maps now
    reflC.reset();
    render(engine.get(), 20);
    std::printf("    20 frames at rest: reflect lamp passes %u (all %u)\n", reflC.lamps(), reflC.total);
    CHECK(reflC.lamps() == 0u, "the mirror renders ZERO lamp passes at rest (%u)", reflC.lamps());

    CHECK(engine->refreshShadows(), "refreshShadows dirties every cached map");
    probeC.reset();
    scene->parallaxCorrectedCubemap()->getProbes()[0]->mDirty = true;
    render(engine.get(), 1);
    std::printf("    capture after refreshShadows: probe lamp passes %u\n", probeC.lamps());
    CHECK(probeC.lamps() == 16u, "a capture renders each dirty lamp map on its FIRST face only "
                                 "(%u, want 16 not 96)", probeC.lamps());
    probeC.reset();
    scene->parallaxCorrectedCubemap()->getProbes()[0]->mDirty = true;
    render(engine.get(), 1);
    CHECK(probeC.lamps() == 0u, "and the next capture of that probe reuses them (%u)", probeC.lamps());
    detach(probeC, probeWs);
    detach(reflC, reflWs);
    engine.reset();
}

// ---------------------------------------------------------------------------
// scancost: what the per-frame item walks cost (ENGINE_CACHE_POLICY_SPEC E2,
// lead review item 4). N cube nodes in a grid, 4 shadow-casting point lamps and
// a shadowed hybrid-GI scene (so the GI movement scan runs every frame too);
// the engine's own steady-clock readings of collectShadowCacheFrame and
// scanGiMovement, at rest and with 10 movers. Numbers, not a gate.
static void scanCostCase(int n)
{
    std::string err;
    EngineConfig cfg = config("spike-scancost.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); ++failures; return; }
    View *v = engine->createOffscreenView("spike", 64, 64, Colour(0, 0, 0));
    Scene *s = engine->createScene("scancost");
    v->setScene(s);
    auto *scene = static_cast<jahshaka::engine::detail::OgreScene *>(s);
    const MeshId mesh = s->createMesh(cubeMesh());
    PbrParams white; white.albedo = Colour(0.8f, 0.8f, 0.8f); white.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(white);
    const int side = int(std::ceil(std::sqrt(double(n))));
    const float span = 30.0f, step = span / float(side);
    std::vector<NodeId> cubes;
    cubes.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const NodeId c = s->createNode();
        s->attachMesh(c, mesh, mat);
        s->setNodeTransform(c, Vec3(-span * 0.5f + step * float(i % side), 0.25f,
                                    -span * 0.5f + step * float(i / side)),
                            Quat(), Vec3(step * 0.4f, 0.5f, step * 0.4f));
        cubes.push_back(c);
    }
    for (int i = 0; i < 4; ++i) {
        const NodeId lamp = s->createNode();
        LightDesc d; d.type = LightType::Point; d.intensity = 0.3f; d.range = 8.0f; d.castShadows = true;
        s->setLight(lamp, d);
        s->setNodeTransform(lamp, Vec3(i % 2 ? 7.0f : -7.0f, 3.0f, i / 2 ? 7.0f : -7.0f), Quat(), Vec3(1, 1, 1));
    }
    CameraDesc c;
    c.position = Vec3(0, 40, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    v->setCamera(c);
    v->setShadows(true);
    engine->setShadowMapBudget(8u);
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Low;
    gi.probeShadows = GiToggle::On;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    // JAH_SCANCOST_NOGI=1: budget 0, so no GI consumer scans and the shadow
    // cache walks on its own after updateSceneGraph (cached world AABBs).
    gi.updateBudget = std::getenv("JAH_SCANCOST_NOGI") ? 0 : 1;
    gi.dynamicProbes = 0;
    s->setGlobalIllumination(gi);
    for (int i = 0; i < 30; ++i) engine->renderOneFrame();
    const auto sample = [&](int frames, bool move, double &shadowUs, double &giUs, double &frameMs) {
        shadowUs = giUs = frameMs = 0.0;
        for (int f = 0; f < frames; ++f) {
            if (move)
                for (int m = 0; m < 10; ++m) {
                    const NodeId id = cubes[size_t(m * (n / 10))];
                    s->setNodeTransform(id, Vec3(-span * 0.5f + step * float((m * (n / 10)) % side),
                                                 0.25f + 0.2f * std::sin(0.1f * float(f + m)),
                                                 -span * 0.5f + step * float((m * (n / 10)) / side)),
                                        Quat(), Vec3(step * 0.4f, 0.5f, step * 0.4f));
                }
            const auto t0 = std::chrono::steady_clock::now();
            engine->renderOneFrame();
            frameMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            shadowUs += scene->shadowScanMicros();
            giUs += scene->giScanMicros();
        }
        shadowUs /= frames; giUs /= frames; frameMs /= frames;
    };
    double a, b, fr;
    sample(60, false, a, b, fr);
    std::printf("    N=%5d at rest:     shadow scan %8.1f us   gi scan %8.1f us   (frame %.2f ms)\n", n, a, b, fr);
    sample(60, true, a, b, fr);
    std::printf("    N=%5d 10 movers:   shadow scan %8.1f us   gi scan %8.1f us   (frame %.2f ms)\n", n, a, b, fr);
    engine.reset();
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "layout";
    if (mode == "layout")      layoutCase();
    else if (mode == "ab")     abCase();
    else if (mode == "four")   fourCase();
    else if (mode == "r3")     r3Case();
    else if (mode == "atten")  attenCase();
    else if (mode == "cachekinds") cacheKindsCase();
    else if (mode == "scancost") {
        for (int i = 2; i < argc; ++i) scanCostCase(std::atoi(argv[i]));
    }
    else { std::printf("unknown mode %s\n", mode.c_str()); return 2; }
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
