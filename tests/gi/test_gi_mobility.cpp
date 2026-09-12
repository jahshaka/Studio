// MOVERS LEAVE THE STILL-WORLD LAYER — SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3
// (lane R2), suite `gi.mobility`.
//
// THE RULE THIS PINS, in one sentence: an object the host has told the renderer
// MOVES is drawn by the per-frame layers (the view, the planar mirrors, SSR)
// and casts shadows there in the frame it moves, and is absent from the
// still-world layer (reflection-probe captures, the voxel bounce, the GI
// signatures and the probe-staleness rules) — so a scene full of moving things
// costs the room's lighting nothing at all.
//
// It is asserted in COUNTERS (probe captures, stale serials, rebuilds) and in
// PIXELS (the mover's reflection in a planar mirror and in SSR, and its ABSENCE
// from what a probe holds), never in milliseconds.
//
// The scene is synthetic and sealed, never the Showroom (which is not
// open-to-open deterministic): a closed white room with a red wall, a chrome
// cube whose centre pixel is a direct read of what the probes hold, a MOVABLE
// emissive cube and a STATIC one — so every assertion about the mover has the
// static twin beside it saying "and nothing regressed for a still scene".
//
// Section B swaps GI off and the moving layer on (planar, then SSR, then the
// view's shadow maps) in the same process, deliberately AFTER the hybrid
// section: the probe/VCT binding is process-wide (sVctBindingOwner), so a
// hybrid scene wants the process to itself while it is being measured.
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

static const char *reasonName(GiStaleReason r)
{
    switch (r) {
    case GiStaleReason::None:     return "none";
    case GiStaleReason::Rebuild:  return "rebuild";
    case GiStaleReason::Refresh:  return "refresh";
    case GiStaleReason::Moved:    return "moved";
    case GiStaleReason::Light:    return "light";
    case GiStaleReason::Material: return "material";
    case GiStaleReason::Sky:      return "sky";
    case GiStaleReason::Ambient:  return "ambient";
    case GiStaleReason::Fog:      return "fog";
    case GiStaleReason::Mobility: return "mobility";
    }
    return "?";
}

/// A unit cube scaled and placed, with its own material so a colour or an
/// emissive can be asked for per object.
static NodeId box(Scene *s, const Vec3 &pos, const Vec3 &scale, const Colour &albedo,
                  float metal, float rough, const Colour &emissive = Colour(0, 0, 0))
{
    const NodeId n = s->createNode();
    PbrParams p;
    p.albedo = albedo;
    p.metalness = metal;
    p.roughness = rough;
    p.emissive = emissive;
    const MaterialId mat = s->createPbrMaterial(p);
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    if (!n || !mat || !mesh || !s->attachMesh(n, mesh, mat)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

/// max over the pixels of a band of (red - max(green, blue)): "is the emissive
/// RED cube present in these pixels", immune to exposure and to the surface's
/// own brightness.
static float maxRedExcess(const Image &img, unsigned y0, unsigned y1)
{
    float best = 0.0f;
    for (unsigned y = y0; y < y1 && y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float e = c.r - (c.g > c.b ? c.g : c.b);
            if (e > best) best = e;
        }
    return best;
}

/// The same reading for one pixel and one channel: how much BLUER (or GREENER)
/// than neutral it is. The room is white with one red wall, so a blue or green
/// excess in the chrome cube's pixel can only have come from one of the two
/// emissive twins.
static float blueExcess(const Colour &c)  { return c.b - (c.r > c.g ? c.r : c.g); }
static float greenExcess(const Colour &c) { return c.g - (c.r > c.b ? c.r : c.b); }

// ===========================================================================
// SECTION A — the still-world layer: a mover costs the probes NOTHING
// ===========================================================================
static void sectionA(Engine *engine)
{
    View *view = engine->createOffscreenView("mobility_room", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("mobility_room");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); ++failures; return; }
    view->setScene(s);
    s->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.01f, 0.01f, 0.01f));

    const Colour white(0.85f, 0.85f, 0.85f);
    box(s, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f), white, 0.0f, 0.9f);   // floor
    box(s, Vec3(0.0f, 5.2f, 0.0f),  Vec3(8.8f, 0.4f, 8.8f), white, 0.0f, 0.9f);   // ceiling
    box(s, Vec3(0.0f, 2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f), white, 0.0f, 0.9f);
    box(s, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f), white, 0.0f, 0.9f);
    box(s, Vec3(4.2f, 2.5f, 0.0f),  Vec3(0.4f, 5.0f, 8.8f), white, 0.0f, 0.9f);
    box(s, Vec3(0.0f, 2.5f, 4.2f),  Vec3(8.8f, 5.0f, 0.4f), Colour(1.0f, 0.02f, 0.02f), 0.0f, 0.9f);
    // The chrome cube in the middle: its centre pixel IS what the probes hold.
    box(s, Vec3(0.0f, 2.0f, 0.0f), Vec3(1.6f, 1.6f, 1.6f), Colour(1, 1, 1), 1.0f, 0.0f);

    // THE TWO TWINS. Identical emissive cubes, one movable and one static, so
    // every "the mover costs nothing" assertion has "and the still one still
    // pays" beside it.
    const NodeId mover  = box(s, Vec3(-2.6f, 1.0f, -1.5f), Vec3(0.8f, 0.8f, 0.8f),
                              Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.5f, Colour(0.0f, 0.0f, 3.0f));
    const NodeId stayer = box(s, Vec3(2.6f, 1.0f, -1.5f), Vec3(0.8f, 0.8f, 0.8f),
                              Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.5f, Colour(0.0f, 3.0f, 0.0f));
    CHECK(mover && stayer, "the movable and static twins exist");

    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.2f), 4.0f);

    CameraDesc c;
    c.position = Vec3(0.0f, 2.0f, 2.4f);
    c.orientation = Quat();
    c.fovDegrees = 60.0f;
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.0f, 2.4f), Vec3(0.0f, 2.0f, 0.0f)));

    // THE CLASSIFICATION, pushed BEFORE the GI arm is built — which is what a
    // host does (the mirror resolves mobility in the same walk that creates the
    // node, before any geometry attaches), and why a scene full of movers never
    // pays a rebuild for its own classification.
    s->setNodeMovable(mover, true);
    CHECK(s->nodeMovable(mover) && !s->nodeMovable(stayer), "the mover reads back movable");

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;     // 4 probes
    gi.updateBudget = 1;
    gi.boundsMin = Vec3(-4.6f, -0.6f, -4.6f);
    gi.boundsMax = Vec3(4.6f, 5.6f, 4.6f);
    CHECK(s->setGlobalIllumination(gi), "the hybrid arm builds");

    const auto frame = [&]() { engine->renderOneFrame(); };
    const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };
    const auto worstOver = [&](int n) {
        int worst = 0;
        for (int f = 0; f < n; ++f) { frame(); worst = std::max(worst, s->giStatus().probeCapturesLastFrame); }
        return worst;
    };
    const auto chromePixel = [&]() {
        Image img; view->readPixels(img); return img.at(img.width / 2, img.height / 2);
    };

    // ---- A1: the still room settles and then costs nothing ----------------
    frames(40);
    {
        const GiStatus st = s->giStatus();
        CHECK(st.probeCount == 4 && st.pccBound, "the 2x1x2 probe grid built and bound (%d probes)",
              st.probeCount);
        CHECK(st.staleProbes == 0, "settled: the post-build catch-up is done");
        CHECK(worstOver(20) == 0, "a still room re-captures nothing");
    }

    // ---- A2: the MOVER moves for 120 frames and costs the room nothing -----
    // (the fail-before reading on the base tree: reason `moved`, the serial
    // advancing every frame and a capture every frame for ever.)
    {
        const GiStatus before = s->giStatus();
        const unsigned long long geomSig = s->giGeometrySignature();
        int captures = 0;
        for (int i = 0; i < 120; ++i) {
            const float t = float(i) * 0.04f;
            s->setNodeTransform(mover, Vec3(-2.6f + t, 1.0f, -1.5f), Quat(), Vec3(0.8f, 0.8f, 0.8f));
            frame();
            captures += s->giStatus().probeCapturesLastFrame;
        }
        const GiStatus after = s->giStatus();
        std::printf("-- mover, 120 frames: captures=%d serial %llu -> %llu (%s), rebuilds %llu -> %llu\n",
                    captures, before.staleSerial, after.staleSerial, reasonName(after.lastStaleReason),
                    before.rebuilds, after.rebuilds);
        CHECK(captures == 0, "120 frames of a MOVING movable cube cost ZERO probe captures (%d)", captures);
        CHECK(after.staleSerial == before.staleSerial,
              "...and stale NOTHING: the probe grid never hears about it");
        CHECK(after.rebuilds == before.rebuilds, "...and cost no from-scratch GI rebuild");
        CHECK(s->giGeometrySignature() == geomSig,
              "...and the GI geometry signature is CONSTANT (the host's settle is never armed)");
        CHECK(s->mobilityStatus().mobilityRebuilds == 0,
              "...and no mobility rebuild (the class never changed)");
    }

    // ---- A3: the STATIC twin still stales the grid, exactly as before ------
    {
        const unsigned long long serial = s->giStatus().staleSerial;
        const unsigned long long geomSig = s->giGeometrySignature();
        s->setNodeTransform(stayer, Vec3(2.0f, 1.0f, -1.5f), Quat(), Vec3(0.8f, 0.8f, 0.8f));
        frame();
        const GiStatus st = s->giStatus();
        CHECK(st.staleSerial > serial && st.lastStaleReason == GiStaleReason::Moved,
              "a STATIC object moving still stales the grid (reason %s)", reasonName(st.lastStaleReason));
        CHECK(s->giGeometrySignature() != geomSig,
              "...and still moves the geometry signature, so the host still re-solves on settle");
        CHECK(worstOver(8) <= 1, "...and its catch-up never exceeds the budget");
        frames(20);
    }

    // ---- A4: what a probe HOLDS never contains the mover -------------------
    // The chrome cube's centre pixel reflects the space BEHIND the camera, so
    // an emissive cube parked there lands in it — but only through the PROBES,
    // which is exactly the reading wanted. The static twin is the positive
    // control: it must appear, or the negative below would be vacuous.
    {
        frames(20);
        const Colour clean = chromePixel();
        // (a) the STATIC twin stands behind the camera: the probes capture it.
        s->setNodeTransform(stayer, Vec3(0.6f, 2.0f, 3.4f), Quat(), Vec3(1.2f, 1.2f, 1.2f));
        frames(30);
        const Colour withStatic = chromePixel();
        std::printf("   chrome pixel  clean %.3f/%.3f/%.3f  with the STATIC twin behind the camera "
                    "%.3f/%.3f/%.3f\n", clean.r, clean.g, clean.b,
                    withStatic.r, withStatic.g, withStatic.b);
        CHECK(greenExcess(withStatic) > greenExcess(clean) + 0.05f,
              "the probes DO capture a still object standing behind the camera (control)");
        s->setNodeTransform(stayer, Vec3(2.6f, 1.0f, -1.5f), Quat(), Vec3(0.8f, 0.8f, 0.8f));
        frames(30);
        // (b) the MOVER stands in the same place, and the whole grid is forced
        //     to re-capture (an ambient edit): it must not appear.
        const Colour before = chromePixel();
        s->setNodeTransform(mover, Vec3(0.6f, 2.0f, 3.4f), Quat(), Vec3(1.2f, 1.2f, 1.2f));
        s->setAmbient(Colour(0.03f, 0.03f, 0.03f), Colour(0.015f, 0.015f, 0.015f));
        frames(30);
        CHECK(s->giStatus().staleProbes == 0, "the forced re-capture caught up");
        const Colour withMover = chromePixel();
        std::printf("   chrome pixel  before %.3f/%.3f/%.3f  with the MOVER in the same place "
                    "%.3f/%.3f/%.3f\n", before.r, before.g, before.b,
                    withMover.r, withMover.g, withMover.b);
        CHECK(blueExcess(withMover) < blueExcess(before) + 0.02f,
              "a probe captured WITH a mover standing there does NOT contain it");
        // ...and it is not invisible: the main view draws it.
        s->setNodeTransform(mover, Vec3(0.0f, 2.0f, 1.2f), Quat(), Vec3(1.2f, 1.2f, 1.2f));
        frames(2);
        Image img; view->readPixels(img);
        float bestBlue = 0.0f;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x)
                bestBlue = std::max(bestBlue, blueExcess(img.at(x, y)));
        CHECK(bestBlue > 0.15f, "...while the main view draws that same mover (%.3f)", bestBlue);
        s->setNodeTransform(mover, Vec3(-2.6f, 1.0f, -1.5f), Quat(), Vec3(0.8f, 0.8f, 0.8f));
        frames(20);
    }

    // ---- A5: hiding, showing and DELETING a mover costs no capture ---------
    {
        const unsigned long long serial = s->giStatus().staleSerial;
        s->setNodeVisible(mover, false);
        const int hidden = worstOver(6);
        s->setNodeVisible(mover, true);
        const int shown = worstOver(6);
        CHECK(hidden == 0 && shown == 0 && s->giStatus().staleSerial == serial,
              "hiding and showing a mover stales nothing and captures nothing (%d/%d)", hidden, shown);
    }

    // ---- A6: an AUTHORING flip is one rebuild, and it is COUNTED -----------
    // Marking a still object Movable takes it out of the voxel bounce, which is
    // a GI edge: exactly one from-scratch rebuild, reported as such. (This is
    // the honest cost the play-time promotion below refuses to pay.)
    {
        const unsigned long long rebuilds = s->giStatus().rebuilds;
        const unsigned long long mob = s->mobilityStatus().mobilityRebuilds;
        s->setNodeMovable(stayer, true);
        frames(30);
        const GiStatus st = s->giStatus();
        std::printf("-- authoring flip: rebuilds %llu -> %llu, mobilityRebuilds %llu -> %llu\n",
                    rebuilds, st.rebuilds, mob, s->mobilityStatus().mobilityRebuilds);
        CHECK(s->mobilityStatus().mobilityRebuilds == mob + 1,
              "marking a still object Movable costs EXACTLY ONE mobility rebuild");
        CHECK(st.rebuilds > rebuilds, "...and the scene really did rebuild its GI");
        CHECK(s->mobilityStatus().movableItems == 2,
              "...and both twins now read movable (%zu)", s->mobilityStatus().movableItems);
        // ...and it is now free to move, like its twin.
        const unsigned long long serial = s->giStatus().staleSerial;
        for (int i = 0; i < 20; ++i) {
            s->setNodeTransform(stayer, Vec3(2.0f + 0.02f * float(i), 1.0f, -1.5f), Quat(),
                                Vec3(0.8f, 0.8f, 0.8f));
            frame();
        }
        CHECK(s->giStatus().staleSerial == serial, "...and moving it now costs the probes nothing");
    }

    // ---- A7: the PLAY-TIME SOFT promotion costs no rebuild at all ----------
    // Owner decision O3: an object nobody marked Movable that starts moving
    // during play is treated as moving from that frame with NO rebuild — it
    // leaves its old bounce light behind as a ghost until play stops.
    {
        const NodeId surprise = box(s, Vec3(-1.0f, 0.6f, 2.0f), Vec3(0.6f, 0.6f, 0.6f),
                                    Colour(0.4f, 0.4f, 0.9f), 0.0f, 0.6f);
        frames(30);
        const unsigned long long rebuilds = s->giStatus().rebuilds;
        const unsigned long long mob = s->mobilityStatus().mobilityRebuilds;
        const unsigned long long serial = s->giStatus().staleSerial;
        s->setNodeMovable(surprise, true, MobilityChange::Soft);
        CHECK(s->giStatus().staleSerial == serial + 1 &&
              s->giStatus().lastStaleReason == GiStaleReason::Mobility,
              "the promotion stales the grid ONCE, with its own reason — the probes that "
              "hold the object's picture owe one re-capture (%s)",
              reasonName(s->giStatus().lastStaleReason));
        // The one-off catch-up, at the budget, and then nothing for ever: this
        // is the whole difference from what the base did, where every frame of
        // the movement staled the grid again.
        int catchUp = 0, worst = 0;
        for (int i = 0; i < 8; ++i) { frame(); catchUp += s->giStatus().probeCapturesLastFrame; }
        const unsigned long long after = s->giStatus().staleSerial;
        int captures = 0;
        for (int i = 0; i < 30; ++i) {
            s->setNodeTransform(surprise, Vec3(-1.0f + 0.03f * float(i), 0.6f, 2.0f), Quat(),
                                Vec3(0.6f, 0.6f, 0.6f));
            frame();
            captures += s->giStatus().probeCapturesLastFrame;
            worst = std::max(worst, s->giStatus().probeCapturesLastFrame);
        }
        CHECK(s->mobilityStatus().mobilityRebuilds == mob && s->giStatus().rebuilds == rebuilds,
              "a SOFT (play-time) promotion costs no rebuild at all");
        CHECK(catchUp <= 4 && catchUp > 0, "its catch-up is ONE pass of the grid (%d captures)", catchUp);
        CHECK(captures == 0 && s->giStatus().staleSerial == after,
              "...and then 30 frames of MOVING cost nothing at all (%d captures)", captures);
        // And deleting it — a mover leaving the scene — costs nothing either.
        const unsigned long long r2 = s->giStatus().rebuilds;
        s->removeNode(surprise);
        const int onDelete = worstOver(6);
        CHECK(onDelete == 0 && s->giStatus().rebuilds == r2,
              "deleting a mover costs no capture and no rebuild (%d)", onDelete);
    }

    // ---- A9: THE GHOST HEALS (code review 2026-09-12, item 5) --------------
    // A soft promotion leaves the object's bounce where it stood and clears for
    // free — but only while the voxels still HOLD that bounce. If they are
    // rebuilt from scratch meanwhile the object is not in them, and a free
    // clearing push would leave a hole for ever. So the clearing push
    // invalidates exactly when a rebuild happened in between, and only then.
    {
        // (a) no rebuild in between: clearing is free, as O3 promises.
        const NodeId prop = box(s, Vec3(1.4f, 0.6f, 2.0f), Vec3(0.6f, 0.6f, 0.6f),
                                Colour(0.9f, 0.9f, 0.4f), 0.0f, 0.6f);
        frames(30);
        unsigned long long mob = s->mobilityStatus().mobilityRebuilds;
        unsigned long long reb = s->giStatus().rebuilds;
        s->setNodeMovable(prop, true, MobilityChange::Soft);
        frames(10);
        s->setNodeMovable(prop, false, MobilityChange::Soft);
        frames(10);
        CHECK(s->mobilityStatus().mobilityRebuilds == mob && s->giStatus().rebuilds == reb,
              "a promotion and its clear cost nothing at all when nothing rebuilt in between");
        // (b) a rebuild DID happen while promoted: the clear pays for the hole.
        mob = s->mobilityStatus().mobilityRebuilds;
        s->setNodeMovable(prop, true, MobilityChange::Soft);
        frames(5);
        GiParams regrid = gi;
        regrid.pccProbesX = 2; regrid.pccProbesY = 1; regrid.pccProbesZ = 3;   // 6 probes: a from-scratch build
        s->setGlobalIllumination(regrid);
        frames(30);
        reb = s->giStatus().rebuilds;
        CHECK(reb > 0, "...a from-scratch rebuild happened while it was promoted");
        s->setNodeMovable(prop, false, MobilityChange::Soft);
        frames(30);
        std::printf("-- ghost heal: mobilityRebuilds %llu -> %llu, rebuilds %llu -> %llu\n",
                    mob, s->mobilityStatus().mobilityRebuilds, reb, s->giStatus().rebuilds);
        CHECK(s->mobilityStatus().mobilityRebuilds == mob + 1,
              "clearing a promotion the voxels were rebuilt under costs ONE counted rebuild");
        CHECK(s->giStatus().rebuilds > reb, "...and the object really is back in the voxels");
        s->removeNode(prop);
        frames(20);
    }

    // ---- A8: the counters ---------------------------------------------------
    {
        const MobilityStatus m = s->mobilityStatus();
        std::printf("-- mobility: items=%zu lights=%zu nodes=%zu rebuilds=%llu\n",
                    m.movableItems, m.movableLights, m.movableNodes, m.mobilityRebuilds);
        CHECK(m.movableItems == 2 && m.movableLights == 0,
              "the counters report what the renderer holds as moving");
    }

    engine->destroyScene(s);
    engine->destroyView(view);
}

// ===========================================================================
// SECTION B — the moving layer: the mover is in the mirror, in SSR and in the
// shadow maps, in the frame it moves
// ===========================================================================
static void sectionB(Engine *engine)
{
    View *view = engine->createOffscreenView("mobility_moving", 256, 256, Colour(0, 0, 0));
    Scene *s = engine->createScene("mobility_moving");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); ++failures; return; }
    view->setScene(s);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    // A mirror floor (metal, roughness 0) and the emissive red MOVER above it.
    const NodeId floor = box(s, Vec3(0.0f, -0.1f, 0.0f), Vec3(12.0f, 0.2f, 12.0f),
                             Colour(1, 1, 1), 1.0f, 0.0f);
    const NodeId mover = box(s, Vec3(0.0f, 2.2f, 0.0f), Vec3(1.5f, 1.5f, 1.5f),
                             Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.5f, Colour(3.0f, 0.0f, 0.0f));
    CHECK(floor && mover, "the mirror floor and the emissive mover exist");
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

    const auto lowerRed = [&](int frames, const char *what) {
        for (int i = 0; i < frames; ++i) engine->renderOneFrame();
        Image img; view->readPixels(img);
        const float r = maxRedExcess(img, img.height / 2, img.height);
        std::printf("   %s: max red excess (lower half) = %.3f\n", what, r);
        return r;
    };

    // ---- B1: the PLANAR mirror shows the mover ----------------------------
    {
        s->setNodeMovable(mover, true);
        PlanarReflectionParams pr;
        pr.budget = 1;
        pr.resolution = 512;
        pr.mipmaps = true;
        pr.accurateLighting = true;
        CHECK(s->setPlanarReflections(pr), "the planar budget applies");
        const float before = lowerRed(3, "mover movable, no reflector yet");
        CHECK(s->setNodePlanarReflector(floor, true), "the floor is a reflector");
        const float on = lowerRed(3, "mover movable, reflector on");
        CHECK(s->activePlanarReflectors() == 1, "exactly one actor rendered");
        CHECK(on > before + 0.10f,
              "a MOVABLE object is drawn into the planar mirror (kMovableBit is in the pass)");
        // ...and it follows the object in the frame it moves.
        s->setNodeTransform(mover, Vec3(0.0f, 2.2f, -60.0f), Quat(), Vec3(1.5f, 1.5f, 1.5f));
        const float gone = lowerRed(2, "mover moved far away");
        CHECK(gone < on - 0.10f, "...and the reflection FOLLOWS it the frame it moves");
        s->setNodeTransform(mover, Vec3(0.0f, 2.2f, 0.0f), Quat(), Vec3(1.5f, 1.5f, 1.5f));
        lowerRed(2, "mover back");
        s->setNodePlanarReflector(floor, false);
        PlanarReflectionParams off;
        off.budget = 0;
        s->setPlanarReflections(off);
    }

    // ---- B2: SSR shows the mover ------------------------------------------
    {
        const float before = lowerRed(3, "ssr off");
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
        const float on = lowerRed(4, "ssr on");
        CHECK(on > before + 0.10f, "a MOVABLE object is reflected by SSR (the main chain masks nothing)");
        PostFxDesc none;
        none.allowOffscreen = true;
        view->setPostFx(none);
        lowerRed(3, "ssr off again");
    }

    // ---- B3: the mover still casts a shadow, in the frame it moves --------
    {
        const NodeId lamp = s->createNode();
        LightDesc d;
        d.type = LightType::Point;
        d.intensity = 3.0f;
        d.range = 20.0f;
        d.castShadows = true;
        s->setLight(lamp, d);
        s->setNodeTransform(lamp, Vec3(0.0f, 5.0f, 0.0f), Quat(), Vec3(1, 1, 1));
        view->setShadows(true);
        engine->shadowStatus();                    // arms the per-frame counters
        for (int i = 0; i < 12; ++i) engine->renderOneFrame();
        // At rest: the lamp's map is a cache and re-renders nothing.
        unsigned rest = 0;
        for (int i = 0; i < 20; ++i) { engine->renderOneFrame(); rest += engine->shadowStatus().cachedMapRendersLastFrame; }
        CHECK(rest == 0u, "a still scene re-renders no cached lamp map (%u)", rest);
        // The mover moves: its lamp's map re-renders in the VIEW, that frame.
        s->setNodeTransform(mover, Vec3(1.5f, 2.2f, 0.0f), Quat(), Vec3(1.5f, 1.5f, 1.5f));
        engine->renderOneFrame();
        const ShadowStatus st = engine->shadowStatus();
        std::printf("-- mover moved: cached-map renders %u, view passes %u, probe passes %u\n",
                    st.cachedMapRendersLastFrame, st.shadowPassesLastFrame, st.probePassesLastFrame);
        CHECK(st.cachedMapRendersLastFrame > 0u,
              "a MOVER still re-renders the lamp map it stands in, in the frame it moves");
        CHECK(st.probePassesLastFrame == 0u,
              "...and no probe-kind shadow pass ran for it (movers are not in probe captures)");
    }

    engine->destroyScene(s);
    engine->destroyView(view);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-mobility-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    std::printf("== SECTION A: the still-world layer\n");
    sectionA(engine.get());
    std::printf("== SECTION B: the moving layer\n");
    sectionB(engine.get());

    std::printf(failures ? "FAILED (%d)\n" : "PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
