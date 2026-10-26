// gi.card_sun_lift — A CARD'S SUN RAY STARTS OUTSIDE THE LEVEL IT TRACES (PHOTON-7 stage 6, READ_A finding 3).
//
// THE LAW: a card texel's still-world sun term is one traced ray from the texel's surface toward the sun; the texel
// shows the CAPTURED level (the bake's level for the card's nominal page) while the ray traces each mesh's LEVEL 0 (ONE
// RAY LEVEL PER MESH). On a CONVEX mesh the captured level's chords lie inside level 0 by up to that level's measured
// bound, so the ray must start at least that far off the texel along its normal (OgreSurfaceCache.cpp sunLiftOf: the
// bound x the instance's scale + one texel + the depth quantum) - or it starts inside level 0 and the texel shadows
// itself. THE REFERENCE is analytic: nothing stands above the mesh and the sun is overhead, so every texel facing it is
// LIT (1).
// WHAT IT GUARDS AGAINST (the base, checkpoint A): a lift of "2 card texels + the quantum", which assumed the split
// card's texel bounded the level's error - but the bake picks the level for the WHOLE card's 128-texel page and a
// split card's texel is that page's over the split (up to kCardMaxSplit 4), so on a convex card wider than 4 m the
// captured level's bound reaches ~3-4 runtime texels: self-shadowed bands along every chord's middle.
// THE FIXTURE: a convex cylinder CAP (radius 8 m, +-40 degrees about the top, 4 m along its axis: a 10.3 m x 4 m +Y card,
// split 4 x 2 into 2.0 cm texels), level 0 96 segments (0.83 degrees), level 1 6 segments (13.3 degrees: its chords'
// middles 5.4 cm inside the arc - the measured bound, under the nominal page's 8.0 cm texel, so the card captures level
// 1), a sun 3 degrees off overhead, the card cache on. Every texel of the eight cards along v = 0.5, 64 per card: the
// sun term, the lift it started at, against the level's bound. A still picture with two uncarded walls beside the cap
// (the readers that make its cards resident, since PHOTON-7 stage 5). Frames: 1 + the cards' residency (<= 400).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const float kRadius = 8.0f;
static const float kHalfAngle = 40.0f * 3.14159265f / 180.0f;
static const float kHalfLength = 2.0f;
static const float kTop = 2.0f;
static const int kSegments0 = 96;   // level 0
static const int kStride1 = 16;     // level 1 takes every 16th: 6 segments

/// THE CAP, level 0 and level 1 over one vertex buffer (level 1 indexes the same vertices), its +Y card at level 1.
static MeshData capMesh(float &bound1)
{
    MeshData d;
    for (int i = 0; i <= kSegments0; ++i) {
        const float th = -kHalfAngle + 2.0f * kHalfAngle * float(i) / float(kSegments0);
        const float x = kRadius * std::sin(th), y = kTop - kRadius * (1.0f - std::cos(th));
        for (int j = 0; j < 2; ++j) {
            const float z = j == 0 ? kHalfLength : -kHalfLength;
            d.positions.insert(d.positions.end(), { x, y, z });
            d.normals.insert(d.normals.end(), { std::sin(th), std::cos(th), 0.0f });
            d.uvs.insert(d.uvs.end(), { float(i) / float(kSegments0), float(j) });
        }
    }
    // counter-clockwise seen from outside (+Y up): (i, front) (i+1, front) (i+1, back) / (i, front) (i+1, back) (i, back)
    const auto quad = [](std::vector<unsigned> &idx, int i, int k) {
        const unsigned a = unsigned(2 * i), b = unsigned(2 * i + 1), c = unsigned(2 * k), e = unsigned(2 * k + 1);
        idx.insert(idx.end(), { a, c, e, a, e, b });
    };
    for (int i = 0; i < kSegments0; ++i) quad(d.indices, i, i + 1);
    std::vector<unsigned> level1;
    for (int i = 0; i < kSegments0; i += kStride1) quad(level1, i, i + kStride1);
    d.lodIndices.push_back(level1);
    // THE MEASURED BOUND: a level-1 chord's middle lies on the level-0 VERTEX at its angle (the stride is even), so the
    // two-sided distance is the chord's sagitta, R (1 - cos(half the chord's angle))
    const float dth1 = 2.0f * kHalfAngle * float(kStride1) / float(kSegments0);
    bound1 = kRadius * (1.0f - std::cos(0.5f * dth1));
    d.lodBounds.push_back(bound1);
    d.lodErrors.push_back(bound1);
    const float sag = kRadius * (1.0f - std::cos(kHalfAngle));
    MeshCardDesc c;
    c.axis = 2;                                   // +Y: u = +x, v = -z
    c.lodLevel = 1;                               // the bake's: the level whose bound is under the page's texel
    c.origin = Vec3(0.0f, kTop - 0.5f * sag, 0.0f);
    c.halfU = kRadius * std::sin(kHalfAngle);
    c.halfV = kHalfLength;
    c.halfDepth = 0.5f * sag + 0.02f;
    d.cards.push_back(c);
    return d;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-card-sun-lift-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("cardsun", 192u, 192u, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *s = e->createScene("cardsun");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the cards' sun term is traced\n");
        return 77;
    }
    s->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.15f, 0.15f, 0.15f));
    {
        const NodeId sun = enginetest::addDirectionalLight(s, Vec3(0.04f, -1.0f, 0.03f), 2.0f);
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.f, 1.f, 1.f);
        l.intensity = 2.0f / 3.14159265f;
        l.castShadows = true;
        CHECK(sun && s->setLight(sun, l), "the sun casts shadows");
    }
    float bound1 = 0.0f;
    const NodeId cap = s->createNode();
    PbrParams p;
    p.albedo = Colour(0.6f, 0.6f, 0.6f);
    p.roughness = 0.8f;
    const MaterialId mat = s->createPbrMaterial(p);
    const MeshId mesh = s->createMesh(capMesh(bound1));
    CHECK(cap && mat && mesh && s->attachMesh(cap, mesh, mat), "the carded cap exists (level 1's bound %.4f m)", bound1);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;   // (the bounces by feedback: PHOTON-7 stage 4 deleted the count)
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 16.0f, 128, 0.0f };
    gi.cards = true;
    // THE CARDS' READERS (PHOTON-7 stage 5, 5-2, at the photon-7r merge: a card is resident while a reader's rays hit it -
    // the gather's and the reflection's - never by its distance from the eye): a still picture gathers, and two
    // UNCARDED walls 6 m tall beside the cap (x +-7 m, past its 5.14 m half-width - their shadows fall outside it: the
    // sun is 3 degrees off overhead) hold the gather's probes whose rays come down on the cap's top, both halves.
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
        PbrParams wp;
        wp.albedo = Colour(0.5f, 0.5f, 0.5f);
        wp.roughness = 0.9f;
        for (float x : { -7.0f, 7.0f }) {
            const NodeId wall = s->createNode();
            const MeshId wm = s->createMesh(enginetest::unitCubeMesh());
            const MaterialId wmat = s->createPbrMaterial(wp);
            if (!wall || !wm || !wmat || !s->attachMesh(wall, wm, wmat)) { std::printf("FAIL: a wall\n"); return 1; }
            enginetest::setNodeScale(s, wall, Vec3(0.2f, 6.0f, 6.0f));
            enginetest::setNodePosition(s, wall, Vec3(x, 3.0f, 0.0f));
        }
    }
    CHECK(s->setGlobalIllumination(gi), "GI builds with the card row on");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 9.0f, 14.0f), Vec3(0.0f, 1.0f, 0.0f));
    // (to residency: the cap's eight cards admitted and captured - the demand's settle and its read-back lag)
    int f = 0;
    for (; f < 400; ++f) {
        enginetest::frames(e, 1);
        const CardCacheStatus c = s->published().gi.cards;
        if (c.built && c.cardsResident == 8u && c.queueLength == 0u && f >= 48) break;
    }
    std::printf("   the cap's cards resident after %d frames\n", f + 1);
    const CardCacheStatus st = s->published().gi.cards;
    CHECK(st.built && st.cardsResident == 8u, "the cap holds its card split 4 x 2 (%u resident)", st.cardsResident);
    CHECK(st.queueLength == 0u, "every card is captured (%u queued)", st.queueLength);

    // THE TEXELS: the eight split cards, 64 along u at v 0.5 each
    int sampled = 0, shadowed = 0, underLift = 0;
    float minLift = 1e9f, maxLift = 0.0f;
    for (unsigned card = 0; card < st.cardsResident; ++card)
        for (int i = 0; i < 64; ++i) {
            CardSample c;
            if (!s->readCardTexel(cap, card, (float(i) + 0.5f) / 64.0f, 0.5f, c) || !c.ok || !(c.depth > 0.0f)) continue;
            ++sampled;
            if (c.shadow < 0.99f) ++shadowed;
            minLift = std::min(minLift, c.sunLift);
            maxLift = std::max(maxLift, c.sunLift);
            if (!(c.sunLift > bound1)) ++underLift;
        }
    std::printf("   %d texels sampled: %d self-shadowed; the sun rays' lift %.4f .. %.4f m against the captured level's"
                " bound %.4f m\n", sampled, shadowed, minLift, maxLift, bound1);
    CHECK(sampled >= 400, "the cards' texels read back (%d of 512)", sampled);
    CHECK(underLift == 0, "every sun ray started beyond the captured level's measured bound (%d under it)", underLift);
    CHECK(shadowed == 0, "no texel of the lit convex cap shadows itself (%d of %d)", shadowed, sampled);

    std::printf("%s\n", failures ? "gi.card_sun_lift: FAIL" : "gi.card_sun_lift: PASS");
    return failures ? 1 : 0;
}
