// gi.cascade_determinism — THE SAME SCENE VOXELISES TO THE SAME PICTURE, whatever
// the renderer did on the way there (ogre-patches 0061 and 0062; PHOTON_SPEC §7
// E2 round 2).
//
// TWO DEFECTS MADE THIS FALSE, and both are in the pin:
//
//   0061 — THE OCTANTS DID NOT FOLLOW THE REGION. `dividideOctants()` copies the
//     region into every octant, `build()` culls items against that copy AND
//     passes its minimum as the voxelisation shader's world origin, and
//     `setRegionToVoxelize()` replaced the region without touching them. So a
//     cascade that SCROLLED voxelised the geometry of the box it used to cover,
//     at the origin it used to have, into a texture the shader maps onto the box
//     it covers now. Measured as 42,42,42 against 47,47,47 on the same document
//     — the session that built the scene against the session that reopened it —
//     with nothing in the document, the probes, the field, the boxes, the item
//     counts or the attach sets differing at all.
//
//   0062 — THE DISPATCH ORDER WAS THE ALLOCATOR'S. The voxelisation's per-voxel
//     merge writes the double-sided flag as the LAST dispatch's answer (an
//     upstream `max(x, x)` typo), and the dispatch order was a std::map keyed on
//     buffer POINTERS. The same scene in two processes = two orders = two sets of
//     voxels, and the flag is read per light (`abs(NdotL)` against
//     `saturate(NdotL)`), so it is a systematically brighter or darker junction.
//
// WHAT THIS SUITE ASSERTS, and why each case is the shape it is:
//
//   1. A CASCADE SCROLLED TO A PLACE IS THE CASCADE BUILT AT IT. The camera is
//      moved far enough to re-centre the chain, then the whole arm is rebuilt
//      from scratch at that same pose: the two pictures must agree. This is 0061
//      and it is the case that reds on the pre-patch pin.
//   2. THE SAME SCENE VOXELISED TWICE IS THE SAME PICTURE — the idempotence the
//      first case depends on, asserted on its own so a failure says which.
//   3. THE ATTACH ORDER DOES NOT DECIDE THE PICTURE. The items are attached in
//      the reverse order and the chain rebuilt: same picture. This is 0062's
//      shape — order-independence — expressed the one way a suite in this process
//      can express it (two allocation orders in two processes cannot be staged
//      from inside one, so the order that CAN be varied is the one we control).
//   4. ...AND NOT WHEN IT DECIDES THE DISPATCH PARTITION EITHER (ogre-patch
//      0065). Case 3's four objects fit one material pool, so the order only
//      varies what happens INSIDE one dispatch. Two hundred objects each owning
//      a material — what the editor actually produces — spread over several
//      pools, and which pool a material lands in is its creation order, so the
//      attach order decides WHICH DISPATCH each object is voxelised by. That is
//      what patch 0062 bought determinism from by giving every material its own
//      dispatch (86 -> 321 ms on a dense scene) and what 0065 fixes at the cause
//      instead: the per-voxel merge accumulates exact integer sums and resolves
//      them once, so no partition and no order changes a voxel.
//
// Its own binary like every GI suite: the voxel lighting binds process-wide to
// HlmsPbs, so this scene must not share a process with another arm's.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static GiParams chainGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;     // the CONE-traced diffuse: the voxels, read directly
    gi.updateBudget = 0;         // no probe work
    gi.cascades = true;
    return gi;
}

/// HOW FAR APART TWO PICTURES ARE, per PIXEL and not as a mean. A mean is the
/// wrong instrument here: the defects above are systematic but LOCAL — a stale
/// octant is wrong where the box moved, a lost double-sided flag is wrong at the
/// junctions — and averaging over 16,384 pixels divides a real 5/255 down into
/// the noise. The worst pixel is what "the same picture" means.
static float apart(const Image &a, const Image &b)
{
    float worst = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            worst = std::max(worst, std::fabs(ca.r - cb.r));
            worst = std::max(worst, std::fabs(ca.g - cb.g));
            worst = std::max(worst, std::fabs(ca.b - cb.b));
        }
    return worst;
}
/// The mean too, printed beside it — it is what a reader compares across runs.
static float meanLum(const Image &img)
{
    float s = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c = img.at(x, y);
            s += 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        }
    return s / float(kSize * kSize);
}
/// 1/255 in linear terms at the mid-grey these pictures sit at. Two renders of the
/// same voxels are bit-identical; this is the margin for the dither/rounding a
/// re-render can carry, and every defect above is far outside it.
static const float kOneOf255 = 1.0f / 255.0f;
/// THE FLOOR OF AN ORDER TEST, and it is a property of the data rather than a
/// concession: the voxel textures are 8-BIT, and the per-voxel merge averages
/// into them (`mixAverage3/4`) — an average of the same numbers in a different
/// order is the same value only up to that quantisation. So two attach orders can
/// differ by ONE step of the voxel's own precision and be the same answer; they
/// cannot differ by more. Measured on this scene: 6/255 before ogre-patch 0062,
/// 1/255 after.
static const float kOneVoxelStep = 1.6f / 255.0f;

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cascade-determinism-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("determinism", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("determinism");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));

    // A room with JUNCTIONS — a floor, two walls that meet it and a prop — because
    // the merge defect lives exactly where two dispatches write the same voxel.
    // Several materials, so the voxeliser has several buckets to order.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(60.0f, 0.1f, 60.0f));
    const NodeId wallA = enginetest::addTestCube(scene, Colour(0.9f, 0.1f, 0.1f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, wallA, Vec3(0.0f, 2.5f, -5.0f));
    enginetest::setNodeScale(scene, wallA, Vec3(12.0f, 5.0f, 0.3f));
    const NodeId wallB = enginetest::addTestCube(scene, Colour(0.1f, 0.9f, 0.2f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, wallB, Vec3(-5.0f, 2.5f, 0.0f));
    enginetest::setNodeScale(scene, wallB, Vec3(0.3f, 5.0f, 12.0f));
    const NodeId prop = enginetest::addTestCube(scene, Colour(0.2f, 0.3f, 0.95f), 0.0f, 0.4f);
    enginetest::setNodePosition(scene, prop, Vec3(1.2f, 0.7f, 1.0f));
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.5f), 5.0f);

    const Vec3 home(0.0f, 3.0f, 8.0f), look(0.0f, 1.0f, -2.0f);
    enginetest::testCameraLookAt(view, home, look);

    CHECK(scene->setGlobalIllumination(chainGi()), "the cascade chain builds");
    render(e, 8);
    GiStatus st = scene->giStatus();
    CHECK(!st.cascades.empty(), "the chain is up");
    if (st.cascades.empty()) return 1;
    // THE WALK IS MEASURED IN THE OUTERMOST CASCADE'S STEP, not the innermost's.
    // Every cascade must actually RE-CENTRE for this suite to be about scrolling,
    // and the outer one steps furthest (its step is the coarsest by construction:
    // 15 m against cascade 0's 5 on the shipped table). A walk that only moves the
    // inner cascades measures a third of the arm.
    const float stepOut = st.cascades.back().step;
    std::printf("   %zu cascades, the outermost steps every %.2f m\n", st.cascades.size(), stepOut);

    const auto picture = [&]() {
        render(e, 6);
        Image img;
        view->readPixels(img);
        return img;
    };
    const auto rebuildWhole = [&]() {
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        render(e, 2);
        scene->setGlobalIllumination(chainGi());
        render(e, 8);
    };

    // =====================================================================
    // CASE 1 — A CASCADE SCROLLED TO A PLACE IS THE CASCADE BUILT AT IT
    // =====================================================================
    std::printf("\n== case 1: scrolled to a place == built at it (ogre-patch 0061) ==\n");
    {
        // WALKED AWAY FROM THE GEOMETRY, AND LOOKING BACK AT IT. Distance alone
        // proves nothing here: this scene is symmetric about the origin, so a
        // cascade that voxelised the box it used to cover would cover nearly the
        // same THINGS and the two pictures would agree by luck. The walk is
        // therefore out of the inner cascades' reach of the walls and the prop
        // (they are all within 6 m of the origin, and cascade 0 covers 5), with
        // the camera turned back on them: what the near cascades hold after the
        // scroll is then a statement about the box they are actually on.
        const float walk = stepOut * 2.0f;                 // 30 m on the shipped table
        enginetest::testCameraLookAt(view, Vec3(home.x + walk, home.y, home.z), look);
        render(e, 40);                       // one cascade per frame; the queue drains
        const GiStatus scrolled = scene->giStatus();
        const Image afterScroll = picture();

        rebuildWhole();                      // the same pose, built from scratch
        enginetest::testCameraLookAt(view, Vec3(home.x + walk, home.y, home.z), look);
        const GiStatus built = scene->giStatus();
        const Image afterBuild = picture();

        std::printf("   scrolled mean %.5f (c0 rebuilds %llu)\n", meanLum(afterScroll),
                    scrolled.cascades[0].rebuilds);
        std::printf("   built    mean %.5f (c0 rebuilds %llu)\n", meanLum(afterBuild),
                    built.cascades[0].rebuilds);
        std::printf("   apart %.5f (%.2f of 255)\n", apart(afterScroll, afterBuild),
                    apart(afterScroll, afterBuild) * 255.0f);
        CHECK(scrolled.cascades[0].rebuilds > built.cascades[0].rebuilds,
              "the scrolled arm really did re-voxelise more than the fresh one");
        CHECK(apart(afterScroll, afterBuild) <= kOneOf255,
              "A CASCADE SCROLLED TO A PLACE RENDERS WHAT A CASCADE BUILT AT IT RENDERS");
    }

    // =====================================================================
    // CASE 2 — THE SAME SCENE VOXELISED TWICE IS THE SAME PICTURE
    // =====================================================================
    std::printf("\n== case 2: two builds of one scene agree ==\n");
    {
        const Image first = picture();
        rebuildWhole();
        const Image second = picture();
        std::printf("   apart %.5f (%.2f of 255)\n", apart(first, second),
                    apart(first, second) * 255.0f);
        CHECK(apart(first, second) <= kOneOf255, "two builds of one scene are one picture");
    }

    // =====================================================================
    // CASE 3 — THE ATTACH ORDER DOES NOT DECIDE THE PICTURE
    // =====================================================================
    // The order the voxeliser receives its items in is the order the engine walks
    // its item index, and the engine's own order is stable — so the way to vary it
    // from a suite is to make the SCENE's order different and rebuild. Detaching
    // and re-attaching the meshes in reverse re-registers them in the opposite
    // order; a chain built on that must render the same scene.
    std::printf("\n== case 3: the attach order does not decide the picture (0062) ==\n");
    {
        const Image before = picture();
        // Re-create the four meshes back to front. removeNode + addTestCube is the
        // engine's own route, so the item index really is rebuilt in reverse.
        struct Rebuilt { Vec3 pos, scale; Colour albedo; float rough; };
        const Rebuilt again[4] = {
            { Vec3(1.2f, 0.7f, 1.0f),   Vec3(1, 1, 1),        Colour(0.2f, 0.3f, 0.95f), 0.4f },
            { Vec3(-5.0f, 2.5f, 0.0f),  Vec3(0.3f, 5, 12),    Colour(0.1f, 0.9f, 0.2f),  0.8f },
            { Vec3(0.0f, 2.5f, -5.0f),  Vec3(12, 5, 0.3f),    Colour(0.9f, 0.1f, 0.1f),  0.8f },
            { Vec3(0.0f, -0.05f, 0.0f), Vec3(60, 0.1f, 60),   Colour(0.85f, 0.85f, 0.85f), 0.9f },
        };
        scene->removeNode(prop); scene->removeNode(wallB);
        scene->removeNode(wallA); scene->removeNode(ground);
        render(e, 2);
        for (const Rebuilt &r : again) {
            const NodeId n = enginetest::addTestCube(scene, r.albedo, 0.0f, r.rough);
            enginetest::setNodePosition(scene, n, r.pos);
            enginetest::setNodeScale(scene, n, r.scale);
        }
        render(e, 2);
        rebuildWhole();
        const Image reversed = picture();
        std::printf("   apart %.5f (%.2f of 255)\n", apart(before, reversed),
                    apart(before, reversed) * 255.0f);
        CHECK(apart(before, reversed) <= kOneVoxelStep,
              "THE SAME GEOMETRY ATTACHED IN THE OPPOSITE ORDER IS THE SAME PICTURE "
              "(to the voxel's own 8-bit step)");
    }

    // =====================================================================
    // CASE 4 — TWO HUNDRED OBJECTS, EACH WITH ITS OWN MATERIAL, IN TWO ORDERS
    // =====================================================================
    // Case 3's four objects fit in one material pool, so the voxeliser groups
    // them into ONE dispatch either way and only the order INSIDE it varies.
    // This is the other half, and it is the shape the editor actually produces:
    // every primitive gets its own material, so two hundred objects are two
    // hundred materials spread over several pools — and WHICH POOL a material
    // lands in is decided by the order it was created in. So the attach order
    // decides the PARTITION of the instances between dispatches, not just the
    // order within one.
    //
    // They are thin plates stacked in pairs, deliberately: a plate thinner than
    // the outer cascades' cell puts both of its faces in one voxel, which is the
    // case the voxelisation has to decide "these surfaces face opposite ways"
    // about — the decision that used to be made by whichever triangle a thread
    // reached first (ogre-patch 0065).
    std::printf("\n== case 4: 200 objects, 200 materials, two orders (0065) ==\n");
    {
        struct Plate { Vec3 pos; Colour albedo; };
        std::vector<Plate> plates;
        for (int x = 0; x < 10; ++x)
            for (int z = 0; z < 10; ++z)
                for (int y = 0; y < 2; ++y) {
                    // A different albedo per PLATE — bound to the plate and not to
                    // the loop, so the reversed arm is the SAME SCENE built in the
                    // other order and not a different one.
                    const float t = float(plates.size()) / 200.0f;
                    plates.push_back({ Vec3(float(x) * 0.6f - 2.7f, 0.6f + float(y) * 0.5f,
                                            float(z) * 0.6f - 2.7f),
                                       Colour(0.2f + 0.7f * t, 0.8f - 0.6f * t, 0.5f) });
                }
        const Vec3 plateScale(0.5f, 0.06f, 0.5f);
        const auto build = [&](bool reverse) {
            std::vector<NodeId> made;
            made.reserve(plates.size());
            for (size_t i = 0; i < plates.size(); ++i) {
                const Plate &pl = plates[reverse ? plates.size() - 1u - i : i];
                // Every object gets its OWN material (addTestCube creates one per
                // call), which is what the editor gives every primitive.
                const NodeId n = enginetest::addTestCube(scene, pl.albedo, 0.0f, 0.6f);
                enginetest::setNodePosition(scene, n, pl.pos);
                enginetest::setNodeScale(scene, n, plateScale);
                made.push_back(n);
            }
            return made;
        };

        std::vector<NodeId> forward = build(false);
        render(e, 2);
        rebuildWhole();
        const Image forwardPic = picture();
        const GiStatus fwdSt = scene->giStatus();
        long long dispatches = 0;
        for (const auto &c : fwdSt.cascades) dispatches += c.voxelDispatches;
        std::printf("   forward: %zu objects, %lld dispatches over the chain\n", forward.size(),
                    dispatches);

        for (NodeId n : forward) scene->removeNode(n);
        render(e, 2);
        std::vector<NodeId> reversed = build(true);
        render(e, 2);
        rebuildWhole();
        const Image reversedPic = picture();

        std::printf("   apart %.5f (%.2f of 255)\n", apart(forwardPic, reversedPic),
                    apart(forwardPic, reversedPic) * 255.0f);
        CHECK(dispatches > 0, "the chain reported how many dispatches it spent");
        CHECK(apart(forwardPic, reversedPic) <= kOneVoxelStep,
              "TWO HUNDRED OBJECTS WITH TWO HUNDRED MATERIALS RENDER THE SAME PICTURE "
              "IN EITHER ATTACH ORDER");
        for (NodeId n : reversed) scene->removeNode(n);
        render(e, 2);
    }

    GiParams off; off.mode = GiMode::Off;
    CHECK(scene->setGlobalIllumination(off), "the chain comes down");
    render(e, 2);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
