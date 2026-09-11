// THE SHADOW-MAP BUDGET, through the public engine API only
// (SPECS/SHADOW_TOOLING_SPEC.md §7 — suite `lights.shadows`).
//
// What it pins, in the spec's own numbering:
//   T1  a four-lamp room: with a budget of four every lamp casts a shadow;
//       with two, exactly two do. That second half is the defect as shipped.
//   T2  the negative: at two casters the atlas is the historical 2048 x 7168
//       strip with the maps in their historical places, so every pixel suite
//       captured before this program keeps its bytes. (The pixel half of T2 —
//       the same frame rendered through upstream's own shadow node and ours,
//       byte for byte — lives in test_shadow_spike's `ab` mode, which can
//       swap the definition because it sees EnginePrivate.)
//   T3  THE LAMP-MAP CACHE (ENGINE_CACHE_POLICY_SPEC P2-P5, P8; §7): every
//       point/spot map renders once and then only when ITS inputs change —
//       per light, in the frame it happens, in the view, in a planar mirror
//       and on a reflection probe's first face only; the camera and a colour
//       edit re-render nothing; and the pass counters reset when a view loses
//       its shadow node and switch themselves off when nobody asks.
//   T4  over budget: shadowStatus() names the lights that got no map.
//   T5  the rebuild survives churn: casters 1 -> 8 -> 1 with a resolution
//       change interleaved, and again with hybrid GI's shadowed probe captures
//       live (risk R3, which was a SEGV before the fix).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok: "); std::printf(__VA_ARGS__); }            \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); ++failures; }   \
        std::printf("\n");                                                      \
    } while (0)

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// The room: a floor, and `lamps` pillars in the far corners each lit from above
// and outward by its own shadow-casting point light. The quadrants are far
// enough apart that one lamp's pool does not reach another's pillar, which is
// what lets a single readback say which lamps got a shadow map.
static const float kQuadX[4] = { -6.0f, 6.0f, -6.0f, 6.0f };
static const float kQuadZ[4] = { -6.0f, -6.0f, 6.0f, 6.0f };
static const float kCamHeight = 22.0f;
static const float kHalfExtent = 12.702f;      // kCamHeight * tan(30 degrees)

struct Room { Scene *scene = nullptr; NodeId lamps[4] = {0,0,0,0}; };

static Room buildRoom(Engine *e, View *v, const char *name, int lamps, bool pillars = true)
{
    Room room;
    room.scene = e->createScene(name);
    if (!room.scene) return room;
    Scene *s = room.scene;
    v->setScene(s);
    s->setAmbient(Colour(0.02f, 0.02f, 0.025f), Colour(0.01f, 0.01f, 0.015f));
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams white; white.albedo = Colour(0.85f, 0.85f, 0.85f); white.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(white);
    const NodeId floor = s->createNode();
    s->attachMesh(floor, mesh, mat);
    s->setNodeTransform(floor, Vec3(0, -0.1f, 0), Quat(), Vec3(40.0f, 0.2f, 40.0f));
    for (int i = 0; i < lamps; ++i) {
        if (pillars) {
            const NodeId pillar = s->createNode();
            s->attachMesh(pillar, mesh, mat);
            s->setNodeTransform(pillar, Vec3(kQuadX[i], 1.0f, kQuadZ[i]), Quat(), Vec3(1.2f, 2.0f, 1.2f));
        }
        const NodeId lamp = s->createNode();
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = 0.25f;
        d.range = 12.0f;
        d.castShadows = true;
        s->setLight(lamp, d);
        s->setNodeTransform(lamp, Vec3(kQuadX[i] * 1.35f, 4.5f, kQuadZ[i] * 1.35f), Quat(), Vec3(1,1,1));
        room.lamps[i] = lamp;
    }
    CameraDesc c;
    c.position = Vec3(0.0f, kCamHeight, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);   // straight down
    c.fovDegrees = 60.0f;
    v->setCamera(c);
    v->setShadows(true);
    return room;
}

static int lum(const Image &img, unsigned x, unsigned y)
{
    const Colour c = img.at(x, y);
    return int((c.r + c.g + c.b) / 3.0f * 255.0f + 0.5f);
}

static int probe(const Image &img, float x, float z)
{
    const int px = int(((x / kHalfExtent) * 0.5f + 0.5f) * float(img.width));
    const int py = int(((z / kHalfExtent) * 0.5f + 0.5f) * float(img.height));
    if (px < 1 || py < 1 || px >= int(img.width) - 1 || py >= int(img.height) - 1) return -1;
    int sum = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) sum += lum(img, unsigned(px + dx), unsigned(py + dy));
    return sum / 9;
}

/// shadowed/lit for lamp `i`: the pillar's cast shadow covers the floor between
/// r = 4.6 and r = 7.6 along that quadrant's diagonal, so r = 6.1 is inside it
/// and the same point three units sideways is not, at nearly the same distance
/// from the lamp. A RATIO, because the absolute level is not comparable across
/// map counts: winning a map also moves a light from the Forward+ path to the
/// pass buffer, and the two do not agree on brightness (a pin-level
/// inconsistency this program measured but did not cause).
static double lampShadowRatio(const Image &img, int i)
{
    const float sx = kQuadX[i] < 0 ? -1.0f : 1.0f;
    const float sz = kQuadZ[i] < 0 ? -1.0f : 1.0f;
    const float ux = sx * 0.70711f, uz = sz * 0.70711f;
    const float wx = sx * 0.70711f, wz = -sz * 0.70711f;
    const int shadowed = probe(img, ux * 6.1f, uz * 6.1f);
    const int lit      = probe(img, ux * 6.1f + wx * 3.0f, uz * 6.1f + wz * 3.0f);
    return lit > 0 ? double(shadowed) / double(lit) : 1.0;
}

static int countShadowed(const Image &img, int lamps)
{
    int n = 0;
    for (int i = 0; i < lamps; ++i) {
        const double r = lampShadowRatio(img, i);
        std::printf("    lamp %d shadow/lit ratio %.2f\n", i, r);
        if (r < 0.35) ++n;
    }
    return n;
}

static double meanLum(const Image &img)
{
    double sum = 0.0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
        sum += (img.rgba[i] + img.rgba[i+1] + img.rgba[i+2]) / 3.0;
    return sum / double(img.rgba.size() / 4);
}

// ---------------------------------------------------------------------------
// T0 — THE TWO LIGHT PATHS MUST AGREE (ogre-patch 0018).
//
// A point light that wins a shadow-map slot is lit by the PASS BUFFER; the same
// light without a slot is lit by FORWARD+ clustered. Before patch 0018 those
// two paths did not agree: Forward+ multiplies by
// max((range - d) * (1/range), 0) under `hlms_forward_fade_attenuation_range`
// (default ON) and the pass-buffer path had no such term, so THE SAME LAMP was
// about twice as bright once it got a shadow map — measured here at 141 vs 63
// mean luminance before the patch. That made the whole shadow-map budget
// feature change scene BRIGHTNESS, which is not what a shadow setting may do.
//
// No pillars: with nothing to cast, a shadow map changes no pixel, so the only
// difference between the two runs is which shader path lit the lamps.
static void t0_light_path_parity(Engine *e, View *v)
{
    std::printf("-- T0: the pass-buffer and Forward+ light paths agree (ogre-patch 0018)\n");
    Room room = buildRoom(e, v, "t0", 2, /*pillars*/ false);
    if (!room.scene) { std::printf("FAIL: scene\n"); ++failures; return; }
    e->setShadowMapBudget(2u);
    render(e, 10);
    const ShadowStatus st = e->shadowStatus();
    Image mapped;
    v->readPixels(mapped);
    std::printf("    casters %u, focusedMaps %u, unmapped %zu\n",
                st.casters, st.focusedMaps, st.unmapped.size());
    CHECK(st.casters == 2u && st.unmapped.empty(),
          "both lamps hold a shadow map (pass buffer): casters %u, unmapped %zu",
          st.casters, st.unmapped.size());

    // The same two lamps, now not casting: both go through Forward+ instead.
    for (int i = 0; i < 2; ++i) {
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = 0.25f;
        d.range = 12.0f;
        d.castShadows = false;
        room.scene->setLight(room.lamps[i], d);
    }
    render(e, 10);
    Image forward;
    v->readPixels(forward);
    const double a = meanLum(mapped), b = meanLum(forward);
    const double drift = (a > 0.0) ? std::fabs(a - b) / a : 1.0;
    std::printf("    mean luminance: pass buffer %.2f, Forward+ %.2f, drift %.2f%%\n",
                a, b, drift * 100.0);
    CHECK(a > 5.0, "the room is actually lit (%.2f)", a);
    CHECK(drift < 0.02, "the two paths light the same lamp within 2%% (%.2f%%)", drift * 100.0);
    e->destroyScene(room.scene);
}

// ---------------------------------------------------------------------------
static void t1_four_lamps(Engine *e, View *v)
{
    std::printf("-- T1: four lamps, budget 2 then 4\n");
    Room room = buildRoom(e, v, "t1", 4);
    if (!room.scene) { std::printf("FAIL: scene\n"); ++failures; return; }

    e->setShadowMapBudget(2u);
    render(e, 10);
    Image two;
    if (!v->readPixels(two)) { std::printf("FAIL: readPixels\n"); ++failures; return; }
    const ShadowStatus st2 = e->shadowStatus();
    std::printf("    status: casters %u, focusedMaps %u, unmapped %zu\n",
                st2.casters, st2.focusedMaps, st2.unmapped.size());
    CHECK(st2.casters == 4u, "the status counts all four casters (%u)", st2.casters);
    CHECK(st2.focusedMaps == 2u, "at budget 2 the atlas keeps two focused maps (%u)", st2.focusedMaps);
    const int shadowedAt2 = countShadowed(two, 4);
    CHECK(shadowedAt2 == 2, "exactly two lamps cast with two maps (%d) — the shipped defect",
          shadowedAt2);

    // The derivation does the rest: raising the ceiling is all the caller does,
    // and the engine grows the atlas from the light list it is about to draw.
    e->setShadowMapBudget(8u);
    render(e, 10);
    Image four;
    v->readPixels(four);
    const ShadowStatus st4 = e->shadowStatus();
    std::printf("    status: casters %u, focusedMaps %u, unmapped %zu, atlas %ux%u (%llu MB)\n",
                st4.casters, st4.focusedMaps, st4.unmapped.size(), st4.atlasWidth, st4.atlasHeight,
                (unsigned long long)(st4.atlasBytes / (1024ull * 1024ull)));
    CHECK(st4.focusedMaps == 4u, "four casters step the allocation to FOUR maps, not eight (%u)",
          st4.focusedMaps);
    CHECK(st4.unmapped.empty(), "no caster is left without a map (%zu unmapped)", st4.unmapped.size());
    const int shadowedAt4 = countShadowed(four, 4);
    CHECK(shadowedAt4 == 4, "all four lamps cast with four maps (%d)", shadowedAt4);
    e->destroyScene(room.scene);
}

static void t2_two_casters_keep_the_old_atlas(Engine *e, View *v)
{
    std::printf("-- T2 (negative): two casters keep the historical layout\n");
    Room room = buildRoom(e, v, "t2", 2);
    e->setShadowMapBudget(8u);
    e->setShadowResolution(2048u);
    render(e, 10);
    const ShadowStatus st = e->shadowStatus();
    std::printf("    atlas %ux%u, focusedMaps %u, casters %u\n",
                st.atlasWidth, st.atlasHeight, st.focusedMaps, st.casters);
    CHECK(st.focusedMaps == 2u, "two casters never grow the atlas (%u maps)", st.focusedMaps);
    CHECK(st.atlasWidth == 2048u && st.atlasHeight == 7168u,
          "the atlas is the historical 2048 x 7168 strip (%ux%u)", st.atlasWidth, st.atlasHeight);
    e->destroyScene(room.scene);
}

static void t4_over_budget(Engine *e, View *v)
{
    std::printf("-- T4: more casters than the budget allows\n");
    Room room = buildRoom(e, v, "t4", 4);
    e->setShadowMapBudget(2u);
    render(e, 10);
    const ShadowStatus st = e->shadowStatus();
    std::printf("    casters %u, budget %u, unmapped %zu\n", st.casters, st.budget, st.unmapped.size());
    CHECK(st.budget == 2u, "the effective budget is what was asked for (%u)", st.budget);
    CHECK(st.unmapped.size() == 2u, "the two lights with no map are NAMED (%zu)", st.unmapped.size());
    bool everyUnmappedIsALamp = true;
    for (NodeId n : st.unmapped) {
        const bool known = n == room.lamps[0] || n == room.lamps[1] ||
                           n == room.lamps[2] || n == room.lamps[3];
        if (!known) everyUnmappedIsALamp = false;
    }
    CHECK(everyUnmappedIsALamp, "the unmapped ids are the scene's own lamps");
    // ...and the mapped list accounts for every map the atlas has.
    CHECK(st.mapped.size() == st.lightSlots && st.lightSlots == 1u + st.focusedMaps,
          "the status describes every light slot (%zu of %u)", st.mapped.size(), st.lightSlots);
    e->destroyScene(room.scene);
}

static void t5_rebuild_churn(Engine *e, View *v)
{
    std::printf("-- T5: caster churn 1 -> 8 -> 1 with a resolution change\n");
    Scene *s = e->createScene("t5");
    v->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.02f, 0.02f, 0.02f));
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams white; white.albedo = Colour(0.8f, 0.8f, 0.8f); white.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(white);
    const NodeId floor = s->createNode();
    s->attachMesh(floor, mesh, mat);
    s->setNodeTransform(floor, Vec3(0, -0.1f, 0), Quat(), Vec3(30.0f, 0.2f, 30.0f));
    const NodeId block = s->createNode();
    s->attachMesh(block, mesh, mat);
    s->setNodeTransform(block, Vec3(0, 1.0f, 0), Quat(), Vec3(1.5f, 2.0f, 1.5f));
    CameraDesc c;
    c.position = Vec3(0, 14, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    v->setCamera(c);
    v->setShadows(true);
    e->setShadowMapBudget(8u);

    std::vector<NodeId> lamps;
    const auto addLamp = [&](int i) {
        const NodeId lamp = s->createNode();
        LightDesc d;
        d.type = (i % 2) ? LightType::Spot : LightType::Point;
        d.intensity = 0.2f;
        d.range = 12.0f;
        d.spotAngleDegrees = 50.0f;
        d.castShadows = true;
        s->setLight(lamp, d);
        const float a = float(i) * 0.7854f;
        s->setNodeTransform(lamp, Vec3(6.0f * std::cos(a), 5.0f, 6.0f * std::sin(a)), Quat(), Vec3(1,1,1));
        lamps.push_back(lamp);
    };
    addLamp(0);
    render(e, 6);
    for (int i = 1; i < 8; ++i) addLamp(i);
    render(e, 10);
    ShadowStatus st = e->shadowStatus();
    std::printf("    8 casters -> %u maps, atlas %ux%u\n", st.focusedMaps, st.atlasWidth, st.atlasHeight);
    CHECK(st.focusedMaps == 8u, "eight casters step to eight maps (%u)", st.focusedMaps);
    e->setShadowResolution(1024u);
    render(e, 6);
    st = e->shadowStatus();
    std::printf("    after 1024: %u maps, atlas %ux%u\n", st.focusedMaps, st.atlasWidth, st.atlasHeight);
    CHECK(st.focusedMaps == 8u, "the count survives a resolution change (%u)", st.focusedMaps);
    for (size_t i = 1; i < lamps.size(); ++i) s->removeLight(lamps[i]);
    render(e, 10);
    st = e->shadowStatus();
    CHECK(st.casters == 1u, "one caster left (%u)", st.casters);
    CHECK(st.focusedMaps == 8u, "the atlas does NOT shrink in-session (%u maps) — owner D4",
          st.focusedMaps);
    Image img;
    CHECK(v->readPixels(img), "the view still renders after the churn");
    int lit = 0;
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) if (img.rgba[i] > 20) ++lit;
    CHECK(lit > 100, "and the picture is not black (%d lit pixels)", lit);
    e->setShadowResolution(2048u);
    e->destroyScene(s);
}

// The R3 regression: the hybrid's SHADOWED probe captures instantiate the same
// shadow node, so a rebuild used to delete the definition under them and the
// next frame died in Hlms::preparePassHashBase. Kept small on purpose — this is
// a lifetime test, not a GI test.
static void t5b_rebuild_under_hybrid_gi(Engine *e, View *v)
{
    std::printf("-- T5b (risk R3): rebuild the atlas under hybrid-GI probe workspaces\n");
    Scene *s = e->createScene("t5b");
    v->setScene(s);
    s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.02f, 0.02f, 0.02f));
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams white; white.albedo = Colour(0.8f, 0.8f, 0.8f); white.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(white);
    const NodeId floor = s->createNode();
    s->attachMesh(floor, mesh, mat);
    s->setNodeTransform(floor, Vec3(0, -0.1f, 0), Quat(), Vec3(12.0f, 0.2f, 12.0f));
    const NodeId block = s->createNode();
    s->attachMesh(block, mesh, mat);
    s->setNodeTransform(block, Vec3(0, 1.0f, 0), Quat(), Vec3(1.5f, 2.0f, 1.5f));
    for (int i = 0; i < 3; ++i) {
        const NodeId lamp = s->createNode();
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = 0.3f;
        d.range = 10.0f;
        d.castShadows = true;
        s->setLight(lamp, d);
        const float a = float(i) * 2.094f;
        s->setNodeTransform(lamp, Vec3(4.0f * std::cos(a), 4.0f, 4.0f * std::sin(a)), Quat(), Vec3(1,1,1));
    }
    CameraDesc c;
    c.position = Vec3(0, 10, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    v->setCamera(c);
    v->setShadows(true);
    e->setShadowMapBudget(2u);          // hold the derivation still
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    // DELIBERATELY THE CHEAPEST HYBRID THAT STILL SHADOWS ITS PROBES: Low
    // quality (a small voxel volume) with probeShadows pinned On rather than
    // left to follow the dial, and a 2x1x2 probe grid. This case is about a
    // DEFINITION LIFETIME, not about GI quality, and a fat probe grid only
    // makes it a VRAM test on a shared machine.
    gi.quality = GiQuality::Low;
    gi.probeShadows = GiToggle::On;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 1;
    s->setGlobalIllumination(gi);
    render(e, 8);
    const GiStatus g = s->giStatus();
    std::printf("    gi: probes %d, probeShadows %d\n", g.probeCount, int(g.probeShadows));
    CHECK(g.probeShadows, "the probe captures are shadowed (the precondition for R3)");
    // THE REBUILD, through the path a Shadow Quality change takes — the one
    // that could always fire this, and that the derived count would have made
    // routine. (The count itself cannot be relied on to rebuild here: the atlas
    // never shrinks, so by this point in the suite it may already be at eight.)
    const unsigned before = e->shadowStatus().focusedMaps;
    e->setShadowResolution(1024u);
    render(e, 8);
    Image img;
    CHECK(v->readPixels(img), "the engine survived the rebuild under shadowed probes");
    const ShadowStatus st = e->shadowStatus();
    CHECK(st.resolution == 1024u && st.focusedMaps == before,
          "the atlas was rebuilt at 1024 with its map count intact (%u -> %u maps)",
          before, st.focusedMaps);
    e->setShadowResolution(2048u);
    render(e, 4);
    e->destroyScene(s);
}

// ---------------------------------------------------------------------------
// T6 — THE ATLAS INSPECTOR DRAWS (SHADOW_TOOLING_SPEC.md §4.4).
//
// It also answers the spec's open question (§8): can HlmsUnlit sample the D32
// shadow atlas on our Vulkan pin? Upstream's ShadowMapDebugging sample does it,
// but nothing in this tree ever had. If it cannot, this case is where it says
// so — the tiles are drawn from that texture and nothing else.
static void t6_atlas_overlay(Engine *e, View *v)
{
    std::printf("-- T6: the shadow-atlas overlay draws its tiles\n");
    Room room = buildRoom(e, v, "t6", 2);
    if (!room.scene) { std::printf("FAIL: scene\n"); ++failures; return; }
    render(e, 8);
    Image off;
    if (!v->readPixels(off)) { std::printf("FAIL: readPixels\n"); ++failures; return; }

    ViewOverlayDesc d;
    d.shadowAtlas = true;
    d.allowOffscreen = true;   // the offscreen opt-in; only a suite ever sets it
    v->setOverlay(d);
    render(e, 8);
    Image on;
    v->readPixels(on);

    // The strip lives along the BOTTOM. Count pixels that changed there against
    // pixels that changed in the top half, which must be none: an overlay that
    // altered the picture would be a defect, not a diagnostic.
    unsigned changedBottom = 0, changedTop = 0;
    for (unsigned y = 0; y < on.height; ++y) {
        for (unsigned x = 0; x < on.width; ++x) {
            const int a2 = lum(on, x, y), b2 = lum(off, x, y);
            if (std::abs(a2 - b2) > 4) { (y > on.height / 2u ? changedBottom : changedTop)++; }
        }
    }
    std::printf("    pixels changed by the overlay: bottom %u, top %u\n", changedBottom, changedTop);
    CHECK(changedBottom > 200u, "the atlas strip draws in the bottom half (%u px)", changedBottom);
    CHECK(changedTop == 0u, "and changes NOTHING above it (%u px)", changedTop);

    // ...and the tiles show the ATLAS, not a flat rectangle. A depth map of a
    // room has structure; a failed texture bind would be uniform. This is the
    // assertion that answers the spec's open question about sampling a D32
    // atlas through HlmsUnlit on this pin.
    int tileMin = 255, tileMax = 0;
    for (unsigned y = on.height / 2u; y < on.height; ++y)
        for (unsigned x = 0; x < on.width; ++x) {
            if (std::abs(lum(on, x, y) - lum(off, x, y)) <= 4) continue;   // not a tile pixel
            tileMin = std::min(tileMin, lum(on, x, y));
            tileMax = std::max(tileMax, lum(on, x, y));
        }
    std::printf("    tile luminance range: %d..%d\n", tileMin, tileMax);
    CHECK(tileMax - tileMin > 8, "the tiles show the atlas's CONTENT, not a flat fill (%d..%d)",
          tileMin, tileMax);

    // ...and it goes away again, which is what keeps every screenshot and pixel
    // suite unaffected.
    v->setOverlay(ViewOverlayDesc());
    render(e, 8);
    Image back;
    v->readPixels(back);
    unsigned differing = 0;
    for (size_t i = 0; i < back.rgba.size() && i < off.rgba.size(); ++i)
        if (back.rgba[i] != off.rgba[i]) ++differing;
    CHECK(differing == 0u, "turning it off restores the frame byte for byte (%u bytes differ)",
          differing);
    e->destroyScene(room.scene);
}

// ---------------------------------------------------------------------------
// T3 — THE LAMP-MAP CACHE (ENGINE_CACHE_POLICY_SPEC §7, `lights.shadows`).
//
// A room of three point lamps far enough apart that their reaches (range 9,
// 16 units apart) never share a caster, one MOVER inside Light0's reach only
// and one FAR mover outside every reach. No sun: with no directional light the
// view's shadow node does nothing at rest but re-render lamp maps, so every
// pass the counters see is a lamp-map pass. Counters and reasons, never ms —
// shadowStatus().mapped[].passes is the per-light reading (8 = one point map:
// its clear quad, six cube faces and the DPSM copy).
static const Vec3 kCacheLamp[3] = { Vec3(-8, 3, -8), Vec3(8, 3, -8), Vec3(-8, 3, 8) };
static const Vec3 kMoverAt(-5.5f, 0.5f, -5.5f);
static const Vec3 kFarAt(8.0f, 0.5f, 8.0f);

struct CacheRoom {
    Scene *scene = nullptr;
    NodeId lamps[3] = { 0, 0, 0 };
    NodeId mover = 0, far = 0, floor = 0;
    MeshId mesh = 0;
    MaterialId mat = 0;
};

static LightDesc cacheLamp(float range = 9.0f, Colour c = Colour(1, 1, 1), float intensity = 1.5f)
{
    LightDesc d;
    d.type = LightType::Point;
    d.colour = c;
    d.intensity = intensity;
    d.range = range;
    d.castShadows = true;
    return d;
}

static CacheRoom buildCacheRoom(Engine *e, View *v, const char *name)
{
    CacheRoom r;
    r.scene = e->createScene(name);
    Scene *s = r.scene;
    v->setScene(s);
    s->setAmbient(Colour(0.02f, 0.02f, 0.025f), Colour(0.01f, 0.01f, 0.015f));
    r.mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams white; white.albedo = Colour(0.85f, 0.85f, 0.85f); white.roughness = 0.9f;
    r.mat = s->createPbrMaterial(white);
    r.floor = s->createNode();
    s->attachMesh(r.floor, r.mesh, r.mat);
    s->setNodeTransform(r.floor, Vec3(0, -0.1f, 0), Quat(), Vec3(40.0f, 0.2f, 40.0f));
    for (int i = 0; i < 3; ++i) {
        r.lamps[i] = s->createNode();
        s->setLight(r.lamps[i], cacheLamp());
        s->setNodeTransform(r.lamps[i], kCacheLamp[i], Quat(), Vec3(1, 1, 1));
    }
    r.mover = s->createNode();
    s->attachMesh(r.mover, r.mesh, r.mat);
    s->setNodeTransform(r.mover, kMoverAt, Quat(), Vec3(1, 1, 1));
    r.far = s->createNode();
    s->attachMesh(r.far, r.mesh, r.mat);
    s->setNodeTransform(r.far, kFarAt, Quat(), Vec3(1, 1, 1));
    CameraDesc c;
    c.position = Vec3(0.0f, kCamHeight, 0.01f);
    c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);   // straight down
    c.fovDegrees = 60.0f;
    v->setCamera(c);
    v->setShadows(true);
    return r;
}

/// The passes the last frame spent on `lamp`'s map in the counted view, or -1
/// when the lamp holds no slot at all.
static int lampPasses(const ShadowStatus &st, NodeId lamp)
{
    for (const ShadowMapInfo &m : st.mapped) if (m.node == lamp) return int(m.passesLastFrame);
    return -1;
}

static bool lampCached(const ShadowStatus &st, NodeId lamp)
{
    for (const ShadowMapInfo &m : st.mapped) if (m.node == lamp) return m.isStatic;
    return false;
}

/// One frame, then the three lamps' readings.
static ShadowStatus frame(Engine *e, int out[3], const CacheRoom &r)
{
    e->renderOneFrame();
    const ShadowStatus st = e->shadowStatus();
    for (int i = 0; i < 3; ++i) out[i] = lampPasses(st, r.lamps[i]);
    return st;
}

/// The cache cases need an atlas with room for three lamps BEFORE their first
/// frame (the count only grows, after a debounce). In the full run T1 already
/// grew it to four; run alone, a throwaway three-lamp scene grows it here.
static void ensureRoomForThreeLamps(Engine *e, View *v)
{
    e->setShadowMapBudget(8u);
    if (e->shadowStatus().focusedMaps >= 3u) return;
    CacheRoom warm = buildCacheRoom(e, v, "t3warm");
    for (int i = 0; i < 12 && e->shadowStatus().focusedMaps < 3u; ++i) e->renderOneFrame();
    e->destroyScene(warm.scene);
}

static void t3_cache_view(Engine *e, View *v)
{
    std::printf("-- T3: lamp maps are cached in the view — once, then only on their own inputs\n");
    ensureRoomForThreeLamps(e, v);
    CacheRoom r = buildCacheRoom(e, v, "t3");
    if (!r.scene) { std::printf("FAIL: scene\n"); ++failures; return; }
    e->shadowStatus();                       // arms the counters for the frames below
    int p[3];

    // (a) THE FIRST FRAME: every lamp map renders exactly once...
    ShadowStatus st = frame(e, p, r);
    std::printf("    first frame: lamp passes %d / %d / %d, cached-map passes %u, viewCached %d\n",
                p[0], p[1], p[2], st.staticMapRendersLastFrame, int(st.viewCached));
    CHECK(st.focusedMaps >= 3u && st.unmapped.empty(), "all three lamps hold a map (%u maps)", st.focusedMaps);
    CHECK(lampCached(st, r.lamps[0]) && lampCached(st, r.lamps[1]) && lampCached(st, r.lamps[2]) &&
          st.viewCached, "and all three are CACHED — no per-light switch exists any more");
    CHECK(p[0] == 8 && p[1] == 8 && p[2] == 8,
          "the first frame renders each lamp map once: 8 passes each (%d/%d/%d)", p[0], p[1], p[2]);
    // ...then never again while nothing changes.
    unsigned rest = 0;
    for (int i = 0; i < 30; ++i) { st = frame(e, p, r); rest += st.staticMapRendersLastFrame + st.shadowPassesLastFrame; }
    CHECK(rest == 0u, "30 frames at rest: ZERO shadow passes (%u)", rest);

    // (b) THE CAMERA NEVER DIRTIES A MAP (P3): a 60-frame fly.
    unsigned fly = 0;
    for (int i = 0; i < 60; ++i) {
        CameraDesc c;
        c.position = Vec3(-6.0f + 0.2f * float(i), kCamHeight - 0.1f * float(i), 0.01f + 0.1f * float(i));
        c.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
        c.fovDegrees = 60.0f;
        v->setCamera(c);
        st = frame(e, p, r);
        fly += st.shadowPassesLastFrame;
    }
    CameraDesc home;
    home.position = Vec3(0.0f, kCamHeight, 0.01f);
    home.orientation = Quat(-0.7071068f, 0, 0, 0.7071068f);
    home.fovDegrees = 60.0f;
    v->setCamera(home);
    frame(e, p, r);
    CHECK(fly == 0u, "a 60-frame camera fly re-renders nothing (%u passes)", fly);

    // (c) A CASTER INSIDE LIGHT0's REACH MOVES: only Light0's map re-renders,
    //     IN THAT FRAME — and its shadow moves in that frame's readback.
    //     Light0 (-8,3,-8) through the mover's top centre (-5.5,1,-5.5) lands
    //     on the floor at (-4.25,-4.25); moving the mover 2 units along +z
    //     uncovers that spot.
    Image before;
    v->readPixels(before);
    const int litRef = probe(before, -4.25f, -11.75f);   // same distance from Light0, no occluder
    const int shadowBefore = probe(before, -4.25f, -4.25f);
    r.scene->setNodeTransform(r.mover, Vec3(kMoverAt.x, kMoverAt.y, kMoverAt.z + 2.0f), Quat(), Vec3(1, 1, 1));
    st = frame(e, p, r);
    Image after;
    v->readPixels(after);
    const int shadowAfter = probe(after, -4.25f, -4.25f);
    std::printf("    mover near Light0 moved: lamp passes %d / %d / %d; floor behind it %d -> %d (lit %d)\n",
                p[0], p[1], p[2], shadowBefore, shadowAfter, litRef);
    CHECK(p[0] == 8 && p[1] == 0 && p[2] == 0,
          "ONLY Light0's map re-renders, in the frame the caster moved (%d/%d/%d)", p[0], p[1], p[2]);
    CHECK(litRef > 20 && shadowBefore < litRef / 2,
          "the mover cast a shadow before the move (%d vs lit %d)", shadowBefore, litRef);
    CHECK(shadowAfter > (litRef * 3) / 4,
          "and in THAT frame's pixels the shadow has left the spot (%d, lit %d)", shadowAfter, litRef);
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0, "the next frame is quiet again (%d/%d/%d)", p[0], p[1], p[2]);

    // (d) A CASTER OUTSIDE EVERY REACH MOVES: nothing re-renders.
    r.scene->setNodeTransform(r.far, Vec3(kFarAt.x - 1.0f, kFarAt.y, kFarAt.z), Quat(), Vec3(1, 1, 1));
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0,
          "a caster moving outside every lamp's reach re-renders nothing (%d/%d/%d)", p[0], p[1], p[2]);

    // (e) COLOUR AND INTENSITY ARE NOT SHADOW INPUTS.
    r.scene->setLight(r.lamps[0], cacheLamp(9.0f, Colour(1.0f, 0.4f, 0.2f), 3.0f));
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0,
          "a colour + intensity edit re-renders nothing (%d/%d/%d)", p[0], p[1], p[2]);

    // (f) A RANGE EDIT IS: that lamp only.
    r.scene->setLight(r.lamps[1], cacheLamp(8.5f));
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 8 && p[2] == 0,
          "a range edit re-renders that lamp's map only (%d/%d/%d)", p[0], p[1], p[2]);

    // (g) THE LAMP ITSELF MOVES: that lamp only.
    r.scene->setNodeTransform(r.lamps[2], Vec3(-8.0f, 3.2f, 8.0f), Quat(), Vec3(1, 1, 1));
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 8,
          "moving a lamp re-renders its own map only (%d/%d/%d)", p[0], p[1], p[2]);

    // (h) A CASTER APPEARING / VANISHING inside Light1's reach: Light1 only.
    const NodeId spawn = r.scene->createNode();
    r.scene->attachMesh(spawn, r.mesh, r.mat);
    r.scene->setNodeTransform(spawn, Vec3(6.0f, 0.5f, -6.0f), Quat(), Vec3(1, 1, 1));
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 8 && p[2] == 0,
          "a caster appearing in Light1's reach re-renders Light1 only (%d/%d/%d)", p[0], p[1], p[2]);
    r.scene->setNodeVisible(spawn, false);
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 8 && p[2] == 0,
          "hiding it re-renders Light1 only (%d/%d/%d)", p[0], p[1], p[2]);
    r.scene->removeNode(spawn);
    st = frame(e, p, r);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0, "removing a hidden caster changes nothing (%d/%d/%d)",
          p[0], p[1], p[2]);

    // (i) world.refreshShadows()'s engine call: every lamp, once.
    CHECK(e->refreshShadows(), "refreshShadows() reports work to do");
    st = frame(e, p, r);
    CHECK(p[0] == 8 && p[1] == 8 && p[2] == 8, "refreshShadows re-renders every lamp once (%d/%d/%d)",
          p[0], p[1], p[2]);

    // AND THE PICTURE IS STILL RIGHT after ~110 frames of cached maps.
    Image img;
    v->readPixels(img);
    const int moved = probe(img, -4.25f, -2.25f);   // the moved mover's shadow, one unit on
    std::printf("    final: moved mover's shadow %d vs lit %d\n", moved, probe(img, -4.25f, -11.75f));
    CHECK(moved < probe(img, -4.25f, -11.75f), "the cached maps still shade the room");
    e->destroyScene(r.scene);
}

// T3r — THE PLANAR MIRROR REUSES THE LAMP MAPS (P5; D3 = A: the reflect node's
// focused count follows the main node's, so the mirror caches every lamp).
static void t3_cache_reflect(Engine *e, View *v)
{
    std::printf("-- T3r: a planar mirror reuses the cached lamp maps\n");
    ensureRoomForThreeLamps(e, v);
    CacheRoom r = buildCacheRoom(e, v, "t3r");
    PlanarReflectionParams pr; pr.budget = 1; pr.resolution = 256; pr.shadows = true;
    CHECK(r.scene->setPlanarReflections(pr), "planar arm up");
    CHECK(r.scene->setNodePlanarReflector(r.floor, true), "the floor is a reflector");
    e->shadowStatus();
    for (int i = 0; i < 6; ++i) e->renderOneFrame();
    ShadowStatus st = e->shadowStatus();
    std::printf("    active reflectors %d; cached instances %u, uncached %u\n",
                r.scene->activePlanarReflectors(), st.cachedInstances, st.uncachedInstances);
    CHECK(r.scene->activePlanarReflectors() == 1, "the mirror renders");
    CHECK(st.cachedInstances >= 2u && st.uncachedInstances == 0u,
          "the view AND the mirror cache all three lamps (%u cached, %u not)", st.cachedInstances,
          st.uncachedInstances);
    unsigned rest = 0, restAll = 0;
    for (int i = 0; i < 30; ++i) {
        e->renderOneFrame();
        st = e->shadowStatus();
        rest += st.reflectLampPassesLastFrame;
        restAll += st.reflectPassesLastFrame;
    }
    std::printf("    30 frames at rest: mirror lamp passes %u (all %u)\n", rest, restAll);
    CHECK(rest == 0u, "the mirror renders ZERO lamp-map passes at rest (%u)", rest);
    r.scene->setNodeTransform(r.mover, Vec3(kMoverAt.x + 1.0f, kMoverAt.y, kMoverAt.z), Quat(), Vec3(1, 1, 1));
    e->renderOneFrame();
    st = e->shadowStatus();
    std::printf("    mover near Light0 moved: mirror lamp passes %u\n", st.reflectLampPassesLastFrame);
    CHECK(st.reflectLampPassesLastFrame == 8u,
          "a mover re-renders the mirror's copy of Light0's map once, that frame (%u)",
          st.reflectLampPassesLastFrame);
    e->destroyScene(r.scene);
}

// T3p — A PROBE CAPTURE RENDERS A DIRTY LAMP MAP ON ITS FIRST FACE ONLY (P4).
static void t3_cache_probe(Engine *e, View *v)
{
    std::printf("-- T3p: reflection probes reuse the cached lamp maps\n");
    ensureRoomForThreeLamps(e, v);
    CacheRoom r = buildCacheRoom(e, v, "t3p");
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Low;
    gi.probeShadows = GiToggle::On;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 1;
    gi.dynamicProbes = 0;
    r.scene->setGlobalIllumination(gi);
    e->shadowStatus();
    // Build, place, and let the grid's first catch-up give every probe its own
    // copy of the three lamp maps.
    for (int i = 0; i < 30; ++i) { e->renderOneFrame(); e->shadowStatus(); }
    GiStatus g = r.scene->giStatus();
    std::printf("    gi: probes %d, probeShadows %d, stale %d\n", g.probeCount, int(g.probeShadows), g.staleProbes);
    CHECK(g.probeShadows && g.probeCount == 4, "four shadowed probes");
    // THE REGRESSION THIS CASE FOUND: the GI rebuild's synchronous probe
    // placement left each probe's dynamic slots naming the lamps, fixing them
    // then listed each lamp twice, and upstream's Forward+ hide/restore switched
    // every lamp OFF for good (OgreForwardClustered.cpp:925-951). A switched-off
    // lamp leaves the cache's list — so all three must still be cached here.
    {
        const ShadowStatus st = e->shadowStatus();
        CHECK(lampCached(st, r.lamps[0]) && lampCached(st, r.lamps[1]) && lampCached(st, r.lamps[2]),
              "after the GI build every lamp is still on and cached (a lamp listed twice goes dark)");
    }

    // Light0 moves (its map is dirty in every instance) and Light2 changes
    // colour (the probe grid goes stale — ENGINE_CACHE_POLICY_SPEC P7). The
    // catch-up captures one probe a frame; each capture must render Light0's
    // map ONCE (8 passes), not once per face (48), and nothing for the others.
    r.scene->setNodeTransform(r.lamps[0], Vec3(-8.0f, 3.1f, -8.0f), Quat(), Vec3(1, 1, 1));
    r.scene->setLight(r.lamps[2], cacheLamp(9.0f, Colour(0.6f, 0.8f, 1.0f)));
    int captures = 0, badFrames = 0;
    unsigned lampTotal = 0;
    for (int i = 0; i < 12; ++i) {
        e->renderOneFrame();
        const ShadowStatus st = e->shadowStatus();
        g = r.scene->giStatus();
        captures += g.probeCapturesLastFrame;
        lampTotal += st.probeLampPassesLastFrame;
        if (st.probeLampPassesLastFrame != 8u * unsigned(g.probeCapturesLastFrame)) ++badFrames;
    }
    std::printf("    catch-up: %d captures, probe lamp passes %u\n", captures, lampTotal);
    CHECK(captures == 4, "every probe re-captured once (%d)", captures);
    CHECK(badFrames == 0 && lampTotal == 32u,
          "each capture rendered Light0's map on its FIRST face only: 8 passes a capture, "
          "not 48 (%u over %d captures)", lampTotal, captures);

    // A second stale-only catch-up: every probe already holds current maps.
    r.scene->setLight(r.lamps[2], cacheLamp(9.0f, Colour(1.0f, 0.9f, 0.6f)));
    captures = 0; lampTotal = 0;
    for (int i = 0; i < 12; ++i) {
        e->renderOneFrame();
        const ShadowStatus st = e->shadowStatus();
        captures += r.scene->giStatus().probeCapturesLastFrame;
        lampTotal += st.probeLampPassesLastFrame;
    }
    std::printf("    colour-only catch-up: %d captures, probe lamp passes %u\n", captures, lampTotal);
    CHECK(captures == 4 && lampTotal == 0u,
          "later captures reuse every lamp map: 0 lamp passes over %d captures (%u)", captures, lampTotal);
    e->destroyScene(r.scene);
}

// T3s — THE COUNTERS TELL THE TRUTH (P8): a view whose shadows are switched
// off reads 0, not the last number it counted; and the per-pass listener comes
// off the render path when nobody asks.
static void t3_counters(Engine *e, View *v)
{
    std::printf("-- T3s: the shadow-pass counters reset and expire\n");
    CacheRoom r = buildCacheRoom(e, v, "t3s");
    const NodeId sun = r.scene->createNode();
    LightDesc d; d.type = LightType::Directional; d.intensity = 0.3f; d.castShadows = true;
    r.scene->setLight(sun, d);
    r.scene->setNodeTransform(sun, Vec3(0, 10, 0), Quat(0.9238795f, 0.3826834f, 0, 0), Vec3(1, 1, 1));
    e->shadowStatus();
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    ShadowStatus st = e->shadowStatus();
    std::printf("    with a sun: %u passes a frame\n", st.shadowPassesLastFrame);
    CHECK(st.shadowPassesLastFrame > 0u, "the sun's splits render every frame (%u)", st.shadowPassesLastFrame);
    v->setShadows(false);
    e->renderOneFrame();
    st = e->shadowStatus();
    e->renderOneFrame();
    const ShadowStatus st2 = e->shadowStatus();
    std::printf("    shadows off: %u, then %u\n", st.shadowPassesLastFrame, st2.shadowPassesLastFrame);
    CHECK(st.shadowPassesLastFrame == 0u && st2.shadowPassesLastFrame == 0u,
          "shadows off reads 0 passes, not the last count (%u, %u)", st.shadowPassesLastFrame,
          st2.shadowPassesLastFrame);
    v->setShadows(true);
    for (int i = 0; i < 3; ++i) { e->renderOneFrame(); e->shadowStatus(); }
    // Nobody asks for 130 frames: the counters come off; the next ask is a
    // fresh start (0), and the frame after it reads the truth again.
    for (int i = 0; i < 130; ++i) e->renderOneFrame();
    st = e->shadowStatus();
    e->renderOneFrame();
    const ShadowStatus st3 = e->shadowStatus();
    std::printf("    after 130 unpolled frames: %u, then %u\n", st.shadowPassesLastFrame, st3.shadowPassesLastFrame);
    CHECK(st.shadowPassesLastFrame == 0u, "the counters switched themselves off (%u)", st.shadowPassesLastFrame);
    CHECK(st3.shadowPassesLastFrame > 0u, "and asking turns them back on (%u)", st3.shadowPassesLastFrame);
    e->destroyScene(r.scene);
}

int main(int argc, char **argv)
{
    const std::string only = argc > 1 ? argv[1] : std::string();
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-shadow-maps-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    View *v = engine->createOffscreenView("shadowmaps", 200, 200, Colour(0, 0, 0));
    if (!v) { std::printf("FAIL: offscreen view\n"); return 1; }

    // ORDER IS PART OF THE TEST. The atlas never shrinks within a session
    // (owner decision D4), so the cases that assert a SMALL atlas have to run
    // before the ones that grow it — which is also the honest reading order of
    // the feature: what it was, what it does when it runs out, what it becomes.
    if (only.empty() || only == "t0")  t0_light_path_parity(engine.get(), v);
    if (only.empty() || only == "t2")  t2_two_casters_keep_the_old_atlas(engine.get(), v);
    if (only.empty() || only == "t4")  t4_over_budget(engine.get(), v);
    if (only.empty() || only == "t1")  t1_four_lamps(engine.get(), v);
    // The cache cases after T1: three lamps want four maps, and the atlas never
    // shrinks — they must not run before the cases that assert a small one.
    if (only.empty() || only == "t3")  t3_cache_view(engine.get(), v);
    if (only.empty() || only == "t3r") t3_cache_reflect(engine.get(), v);
    if (only.empty() || only == "t3p") t3_cache_probe(engine.get(), v);
    if (only.empty() || only == "t3s") t3_counters(engine.get(), v);
    if (only.empty() || only == "t5")  t5_rebuild_churn(engine.get(), v);
    if (only.empty() || only == "t5b") t5b_rebuild_under_hybrid_gi(engine.get(), v);
    if (only.empty() || only == "t6")  t6_atlas_overlay(engine.get(), v);

    engine.reset();
    std::printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
