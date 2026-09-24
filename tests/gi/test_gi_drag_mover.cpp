// gi.drag_mover — A DRAGGED STILL ON THE MOVER CHANNEL (MOVER-1; the render
// audit's PHOTON F3 second half, SYNTHESIS §3's "the MOVER CHANNEL is still
// owed and is its own picture decision").
//
// WHAT THE RULE IS. A Still object that is being dragged records a moved box on
// every frame it moves, the cascade scheduler marks every cascade that box
// reaches and spends one re-voxelisation per frame answering it — measured on
// the Mirror Room, 32 re-voxelisations over a 60-frame drag at 1.0-7.6 ms of
// CPU submission each — and the frames in between are read through a volume
// that holds the object in two places at once. With
// `GiParams::dragMoverChannel` on, the SECOND move inside the settle window
// promotes the object onto the mover channel Mobility already defines
// (kMovableBit, no kGiGeometryBit): the cascades pay ONE re-voxelisation at the
// promotion, which clears the copy they hold at its old pose, and nothing at
// all for the rest of the gesture.
//
// IT IS OFF BY DEFAULT because it changes the picture WHILE the object is
// dragged (the object stops bouncing light into the room and is lit by the
// field and the cones at its live pose, exactly as a Movable object is), and
// that is the owner's decision. What this suite pins is everything that is NOT
// a matter of taste:
//
//   1. OFF IS TODAY: with the rule off nothing promotes, whatever is dragged.
//   2. THE PROMOTION IS A GESTURE'S, NOT A MOVE'S. One move — a scripted
//      setPosition, a nudge, a paste — is answered where it happens and
//      promotes nothing; a second move inside the settle window is a drag.
//   3. THE COST: a gesture's re-voxelisations are a small constant instead of
//      one per moving frame. Measured against the SAME gesture in the same
//      process, so it means the same thing on every machine.
//   4. THE OBJECT REALLY IS ON THE MOVER CHANNEL while it is promoted, and off
//      it again at rest — `world.giStatus().dragMovers` and the mobility census
//      say so, and the DOCUMENT's own mobility is never touched.
//   5. THE PICTURE AT REST IS THE SAME PICTURE. The same pose reached with the
//      rule on and with it off is the same frame, to within the chain's own
//      iterative wander (LAMPREST-2's +-1/255). This is the assertion the whole
//      trade rests on, and the ORDER at the gesture's end is what makes it true:
//      geometry (the re-voxelisation), then light (one full at-rest tick), then
//      the photographs (the probe sweep). Staling the probes at the demotion
//      instead — before the chain had been rebuilt and re-injected — left the
//      Mirror Room's dragged torus 161,318 pixels at up to 32/255 away from the
//      same pose reached without the rule, stably, because every probe had
//      photographed the room WITHOUT it.
//
// Its own binary like every GI suite. RUN_SERIAL because it counts per-frame
// work.
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
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf(cond ? "ok: " : "FAIL: ");                                  \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static const unsigned kSize = 256;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// THE CHAIN, because this is a cascade rule: the single-volume arm
/// re-voxelises nothing during a drag by construction, so there is nothing
/// there for the promotion to save. Plain VCT (no probe grid) keeps the suite
/// about the VOXELS; the probe half of a drag is gi.drag_cadence's.
static GiParams chainGi(bool dragMover)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 1;
    gi.cascades = true;
    gi.cascadeCount = 3;
    gi.dragMoverChannel = dragMover;
    return gi;
}

static unsigned long long cascadeRebuilds(const GiStatus &st)
{
    unsigned long long n = 0;
    for (const auto &c : st.cascades) n += c.rebuilds;
    return n;
}

/// One gesture: `frames` moves of `step` metres, one rendered frame each.
static void drag(Engine *e, Scene *s, NodeId node, Vec3 &pos, float step, int frames)
{
    for (int i = 0; i < frames; ++i) {
        pos.x += step;
        enginetest::setNodePosition(s, node, pos);
        render(e, 1);
    }
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-drag-mover-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("dragmover", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("dragmover");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.12f, 0.12f, 0.12f));

    // A floor, a bright wall to bounce off, and the prop that gets dragged.
    const NodeId floor = enginetest::addTestCube(s, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.2f, 0.0f));
    enginetest::setNodeScale(s, floor, Vec3(12.0f, 0.4f, 12.0f));
    const NodeId wall = enginetest::addTestCube(s, Colour(0.9f, 0.1f, 0.1f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 2.0f, -4.0f));
    enginetest::setNodeScale(s, wall, Vec3(10.0f, 4.0f, 0.4f));
    const NodeId prop = enginetest::addTestCube(s, Colour(0.2f, 0.8f, 0.3f), 0.0f, 0.5f);
    Vec3 propPos(-1.5f, 0.6f, 0.5f);
    enginetest::setNodePosition(s, prop, propPos);
    enginetest::setNodeScale(s, prop, Vec3(1.2f, 1.2f, 1.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 5.0f), Vec3(0.0f, 0.8f, 0.0f));

    CHECK(s->setGlobalIllumination(chainGi(false)), "the cascade chain builds");
    render(e, 20);
    GiStatus st = s->giStatus();
    std::printf("   %zu cascades, budget %d\n", st.cascades.size(), st.probeUpdatesPerFrame);
    if (st.cascades.size() < 2u) {
        std::printf("FAIL: this suite needs a cascade chain\n");
        return 1;
    }
    CHECK(st.dragMovers == 0 && st.dragMoverGestures == 0,
          "a scene that has not been touched has no drag movers");

    // =====================================================================
    // CASE 1 — WITH THE RULE OFF, A DRAG PROMOTES NOTHING
    // =====================================================================
    std::printf("\n== case 1: the rule OFF (the shipped behaviour) ==\n");
    const unsigned long long rebuildsBeforeOff = cascadeRebuilds(s->giStatus());
    drag(e, s, prop, propPos, 0.08f, 40);
    const GiStatus offMid = s->giStatus();
    const unsigned long long offDrag = cascadeRebuilds(offMid) - rebuildsBeforeOff;
    std::printf("   40 dragged frames: %llu cascade re-voxelisations, dragMovers %d\n",
                offDrag, offMid.dragMovers);
    CHECK(offMid.dragMovers == 0, "nothing was promoted");
    CHECK(offMid.dragMoverGestures == 0, "...and no gesture was recorded");
    CHECK_MSG(offDrag >= 8,
              "and the cascades paid for the drag frame by frame (%llu re-voxelisations over 40 "
              "frames)", offDrag);
    render(e, 40);                       // settle
    const unsigned long long offTotal = cascadeRebuilds(s->giStatus()) - rebuildsBeforeOff;

    // =====================================================================
    // CASE 2 — ONE MOVE IS NOT A GESTURE
    // =====================================================================
    std::printf("\n== case 2: a single move promotes nothing ==\n");
    CHECK(s->setGlobalIllumination(chainGi(true)), "the rule goes on");
    render(e, 30);
    propPos.x += 0.5f;                   // ONE scripted move, then stillness
    enginetest::setNodePosition(s, prop, propPos);
    render(e, 1);
    CHECK(s->giStatus().dragMovers == 0,
          "A SINGLE MOVE IS NOT A GESTURE: a nudge, a paste or a scripted setPosition is "
          "answered where it happens");
    render(e, 40);
    CHECK(s->giStatus().dragMoverGestures == 0, "...and it never became one");

    // =====================================================================
    // CASE 3 — A GESTURE PROMOTES, AND COSTS A CONSTANT
    // =====================================================================
    std::printf("\n== case 3: the rule ON, the same gesture ==\n");
    const unsigned long long rebuildsBeforeOn = cascadeRebuilds(s->giStatus());
    // WHERE THE GESTURE STARTS, kept: case 5 replays exactly this gesture with
    // the rule off, and "the same pose reached both ways" means the same START
    // and the same steps, not merely the same number of them.
    const Vec3 gestureStart = propPos;
    drag(e, s, prop, propPos, 0.08f, 40);
    const GiStatus onMid = s->giStatus();
    const unsigned long long onDrag = cascadeRebuilds(onMid) - rebuildsBeforeOn;
    std::printf("   40 dragged frames: %llu cascade re-voxelisations, dragMovers %d\n",
                onDrag, onMid.dragMovers);
    CHECK(onMid.dragMovers == 1, "THE DRAGGED OBJECT IS ON THE MOVER CHANNEL while it moves");
    // ...and the renderer has NOT rewritten the document's answer: the host
    // never said this object moves, and `mobilityStatus` is that answer.
    CHECK(s->mobilityStatus().movableItems == 0,
          "...without the DOCUMENT's mobility being touched (movableItems still 0)");
    CHECK_MSG(onDrag * 2u <= offDrag,
              "A GESTURE COSTS A CONSTANT, NOT A FRAME'S WORK PER FRAME: %llu re-voxelisations "
              "against %llu for the same gesture with the rule off", onDrag, offDrag);

    // =====================================================================
    // CASE 4 — AND IT GOES BACK TO BEING STILL WORLD
    // =====================================================================
    std::printf("\n== case 4: the gesture ends ==\n");
    render(e, 60);
    const GiStatus rest = s->giStatus();
    const unsigned long long onTotal = cascadeRebuilds(rest) - rebuildsBeforeOn;
    std::printf("   at rest: dragMovers %d, gestures %llu, re-voxelisations over the whole "
                "gesture %llu (rule off: %llu)\n",
                rest.dragMovers, rest.dragMoverGestures, onTotal, offTotal);
    CHECK(rest.dragMovers == 0, "the object is still world again");
    CHECK(rest.dragMoverGestures == 1, "...and the gesture was counted ONCE, not once a frame");
    CHECK_MSG(onTotal < offTotal,
              "the whole gesture, settle included, still costs less than the drag alone did "
              "(%llu against %llu)", onTotal, offTotal);
    // THE PROMOTION PAYS ITS OWN WAY: it is not free, and pretending it is
    // would hide the one re-voxelisation that clears the object's old copy out
    // of the volume. Two ends, at least one box each.
    CHECK_MSG(onTotal >= 2,
              "and it is NOT free — the promotion and the settle each re-voxelise (%llu)",
              onTotal);

    // =====================================================================
    // CASE 5 — THE PICTURE AT REST IS THE SAME PICTURE
    // =====================================================================
    // The same pose, reached both ways, in one process. The bar is the chain's
    // own iterative wander: a cascade chain's radiance is a fixed point reached
    // by Jacobi iteration from whatever the volumes held, so two histories that
    // end at the same geometry agree to about 1/255 and not to the bit
    // (LAMPREST-2). What must NOT happen is a SYSTEMATIC difference — an object
    // missing from the room's bounce, or a probe holding a photograph of a room
    // without it.
    std::printf("\n== case 5: the same pose, reached both ways ==\n");
    Image withRule, withoutRule;
    CHECK(view->readPixels(withRule), "readPixels at rest with the rule on");
    // Put it back where the gesture started, turn the rule off, and drag it
    // along the same path again.
    CHECK(s->setGlobalIllumination(chainGi(false)), "the rule goes off");
    Vec3 again = gestureStart;
    enginetest::setNodePosition(s, prop, again);
    render(e, 90);
    drag(e, s, prop, again, 0.08f, 40);
    render(e, 90);
    CHECK(view->readPixels(withoutRule), "readPixels at rest with the rule off");
    unsigned moved = 0, worst = 0;
    if (withRule.width == withoutRule.width && withRule.height == withoutRule.height) {
        for (unsigned y = 0; y < withRule.height; ++y)
            for (unsigned x = 0; x < withRule.width; ++x) {
                const Colour a = withRule.at(x, y), b = withoutRule.at(x, y);
                const float d = std::max(std::max(std::fabs(a.r - b.r), std::fabs(a.g - b.g)),
                                         std::fabs(a.b - b.b));
                const unsigned steps = unsigned(d * 255.0f + 0.5f);
                if (steps) { ++moved; worst = std::max(worst, steps); }
            }
    }
    const double movedPct = 100.0 * double(moved) / double(kSize * kSize);
    std::printf("   the two histories differ on %u pixels (%.2f%%), worst %u/255\n",
                moved, movedPct, worst);
    CHECK_MSG(worst <= 2,
              "THE PICTURE AT REST IS THE SAME PICTURE: worst channel step %u/255 over %.2f%% of "
              "the frame (the chain's own iterative wander; a systematic difference — the object "
              "missing from the room's bounce — is tens of steps over a third of it)",
              worst, movedPct);

    e->destroyView(view);
    e->destroyScene(s);
    std::printf(failures ? "FAILURES: %d\n" : "PASS: gi.drag_mover\n", failures);
    return failures ? 1 : 0;
}
