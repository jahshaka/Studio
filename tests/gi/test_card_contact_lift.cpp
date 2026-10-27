// gi.card_contact_lift (PHOTON-7 stage 10-3, R10 / B1; L9 is physics: a sample point off its surface is a geometry
// error) - A CARD'S SUN RAY STARTS ON ITS SURFACE, SO A LOW OBJECT'S CONTACT SHADOW IS WHOLE ON THE CARD. A card
// texel's still-world sun term is one traced ray from the texel's point; the capture writes ONE rasterised depth at the
// texel's centre, so that point lies ON the surface at any slant (up to the Depth layer's quantum), and the ray's start
// is lifted by the captured level's bound against level 0 and that quantum alone (OgreSurfaceCache.cpp sunLiftOf).
// RED before: the lift also carried ONE CARD TEXEL, so every shadow ray started a texel above the floor (3.2 cm on this
// 16 m floor's split cards, 13 cm on a coarse far wall) and a contact shadow lost lift / tan(elevation) of its length:
// 39 of the umbra's 129 points read LIT here.
// THE FIXTURE: a carded 16 m floor (its +Y face split into 128-texel cards), a carded slab 1 m x 0.10 m x 6 m lying on
// it, a sun from +x at 12 degrees (the slab's shadow on the floor ends 0.47 m from its -x side, analytic), two uncarded
// walls whose gather probes read the floor (the cards' readers). The ray tier at High, run to the cards' rest.
// THE LAWS, at the floor's card through Scene::readCardAt along three lines across the shadow (z = -1, 0, 1), every
// point more than 3 cm from the analytic edge: (1) every umbra point reads the sun occluded (shadow < 0.5); (2) every lit
// point reads it lit; (3) the lift is the depth quantum alone on this planar floor (under 1 cm: its level's bound is 0).
// A fixture (the engine alone, one 128 x 128 offscreen view; 10 s budget). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
const float kElev = 12.0f * 3.14159265f / 180.0f;
const float kH = 0.10f;   // the slab's height

NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, bool cards) {
    PbrParams p;
    p.albedo = Colour(0.5f, 0.5f, 0.5f);
    p.roughness = 0.9f;
    const NodeId n = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    if (cards) d.cards = enginetest::boxCards(0.5f);
    s->attachMesh(n, s->createMesh(d), s->createPbrMaterial(p));
    enginetest::poseRegistry()[s][n] = enginetest::NodePose{};
    enginetest::setNodePosition(s, n, at);
    enginetest::setNodeScale(s, n, size);
    return n;
}
}   // namespace

int main(int, char **) {
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-card-contact-lift-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("contact_lift", 128, 128, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *s = e->createScene("contact_lift");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the cards' sun term is traced\n");
        return 77;
    }
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
    }
    s->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.15f, 0.15f, 0.15f));
    {
        const NodeId sun = enginetest::addDirectionalLight(s, Vec3(-std::cos(kElev), -std::sin(kElev), 0.0f), 2.0f);
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.f, 1.f, 1.f);
        l.intensity = 2.0f / 3.14159265f;
        l.castShadows = true;
        s->setLight(sun, l);
    }
    const NodeId floorN = box(s, Vec3(0.0f, -0.2f, 0.0f), Vec3(16.0f, 0.4f, 16.0f), true);
    box(s, Vec3(0.0f, 0.5f * kH, 0.0f), Vec3(1.0f, kH, 6.0f), true);   // the slab: its -x side at x = -0.5
    for (const float z : { -7.0f, 7.0f }) box(s, Vec3(0.0f, 3.0f, z), Vec3(6.0f, 6.0f, 0.2f), false);   // the readers
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    gi.cards = true;
    gi.updateBudget = 0;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: GI\n"); return 1; }
    enginetest::testCameraLookAt(view, Vec3(9.0f, 6.0f, 0.0f), Vec3(0.0f, 0.5f, 0.0f));
    int rest = -1;
    unsigned long long relights = ~0ull;
    for (int f = 0, quiet = 0; f < 400; ++f) {
        enginetest::frames(e, 1);
        ++frames;
        const GiStatus g = s->published().gi;
        quiet = (g.cards.built && g.cards.queueLength == 0u && g.cards.relights == relights && f > 48) ? quiet + 1 : 0;
        relights = g.cards.relights;
        if (quiet >= 30) { rest = f - quiet; break; }
    }
    const float edge = -0.5f - kH / std::tan(kElev);
    unsigned umbra = 0, umbraLit = 0, lit = 0, litDark = 0;
    float liftMax = 0.0f;
    for (const float z : { -1.0f, 0.0f, 1.0f })
        for (float x = -1.6f; x < -0.52f; x += 0.01f) {
            CardSample c;
            if (!s->readCardAt(Vec3(x, 0.0f, z), Vec3(0.0f, 1.0f, 0.0f), c, floorN) || !c.ok) continue;
            liftMax = std::max(liftMax, c.sunLift);
            if (x > edge + 0.03f) { ++umbra; if (c.shadow > 0.5f) ++umbraLit; }
            if (x < edge - 0.03f) { ++lit; if (c.shadow < 0.5f) ++litDark; }
        }
    std::printf("   at rest after %d frames; the floor's sun lift %.4f m; the umbra (the analytic edge x = %.3f): %u points, "
                "%u read lit; the lit side: %u points, %u read occluded\n", rest, liftMax, edge, umbra, umbraLit, lit,
                litDark);
    CHECK(rest >= 0 && umbra >= 100u && lit >= 100u, "premise: the floor's cards at rest, both sides of the edge read");
    CHECK(umbraLit == 0u, "(1) the slab's whole contact shadow is on the floor's card");
    CHECK(litDark == 0u, "(2) ...and nothing past its analytic edge is shadowed");
    CHECK(liftMax > 0.0f && liftMax < 0.01f, "(3) the planar floor's sun ray starts within its depth quantum of the floor");
    std::printf("\ngi.card_contact_lift: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
