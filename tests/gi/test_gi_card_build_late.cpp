// gi.card_build_late (COLD-BOOT-1 item 3, the lead's ruling 2026-11-22) - A CARD READER WHOSE BUILD IS IN FLIGHT IS A READER.
//
// THE DEFECT IT GUARDS (spikes/cold-boot-1/VERIFY-card-demand.md): in a scene that compiles in the background (its view
// asynchronous, the engine on the Streaming pace) a card reader whose ray pipeline is still building takes its "not this
// frame" path (RayQueryTier::aheadReady) and records nothing; a reader that has not recorded was read as NO reader, so the
// cache's demand window - opened by its own build - closed empty, the rest's ONE decision was taken on nothing, and the
// build's landing was no change of the demand: the readers' first stamps arrived after the window and nothing was ever
// admitted (the startup gate's cold Basic world: admitted 0 through every phase). The resident set at a pose then depended
// on how long a driver compile took.
// THE FIXTURE: a closed 6 x 3 x 6 m room of carded slabs, two carded pillars and a lamp inside, the eye inside; the chain
// at High, the cards on, the rays on, GI paused; a 128 x 128 StillPicture view with the post chain and the ray reflection
// (it gathers: both card readers), asynchronous, the engine on the Streaming pace. A late build is the test door
// rays.buildHold (with rays.buildHoldIds naming the builds): a held build counts as in flight, finished or not, until the
// arm is set back to 0 (then the frame's harvest lands it).
// THE ROWS (argv[1]):
//   late    - THE LAW. Run H: the two card readers' builds (the reflection's, the gather's) HELD from the first frame until
//             kHeldPastBuild frames after the cache's build (three times its demand window: S + a read period + the read's
//             lag, ~19 frames), then released. (1) while they are held, NO rest decision is taken and giAtRest is false;
//             (2) after they land the cache admits and captures, and at rest holds THE SAME RESIDENT SET as run C - the same
//             room at the same pose, a new scene drawn after every build landed (its readers recorded from its first frame):
//             CardCacheStatus::residentHash equal (the two scenes' nodes carry the same explicit ids), cards > 0.
//             RED before the fix: the decision taken in the hold on an empty demand, admitted 0 for ever.
//   unasked - THE DEFINITION'S OTHER HALF (the lead's condition): a build NO READER ASKED FOR holds no rest. The glass escape's
//             build launched with the rest (Engine::prepareRayPipelines) and held; the room has no blended material, so its
//             consumer never runs. (1) the scene comes to rest - its decision taken, giAtRest true - while that build is still
//             in flight; (2) its landing is no change of the demand: the change frame, the decisions and the resident set the
//             same kAfterLanding frames after it.
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
/// The frames after the cache's build the late run's readers stay held: 3 x the demand window (S 10 + a read period 4 +
/// the read's lag ~5 frames, SurfaceCache::demandWindowOpen), so a window opened by the build is long closed.
const int kHeldPastBuild = 60;
/// rays.buildHoldIds: bit k for AheadId k (OgreFrameMonitor.cpp's row) - the reflection (1) and the gather (32); the glass
/// escape (16)
const double kReaderBuilds = 1.0 + 32.0, kGlassEscapeBuild = 16.0;
/// A hold the run releases itself (arm back to 0): longer than any run
const double kHoldForever = 100000.0;
/// The frames the unasked run watches after its held build lands
const int kAfterLanding = 24;
/// The frames a run may take to come to rest (a bound on the fixture, not a law)
const int kRestCap = 1500;
/// The explicit node ids (both scenes the same: residentHash is over (node id, card index))
const NodeId kFirstId = 1ull << 40;

NodeId slab(Scene *s, NodeId id, const Vec3 &at, const Vec3 &size, MaterialId mat)
{
    const NodeId node = s->createNode(0, id);
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}

struct Room { View *view = nullptr; Scene *scene = nullptr; };
Room buildRoom(Engine *e, const char *name)
{
    Room r;
    r.view = e->createOffscreenView(name, 128, 128, Colour(0, 0, 0), OffscreenContract::StillPicture);
    r.scene = e->createScene(name);
    if (!r.view || !r.scene) return Room();
    r.view->setScene(r.scene);
    {
        PostFxDesc fx;   // the chain: a StillPicture view with it gathers, ssr 1 runs the ray reflection
        fx.allowOffscreen = true;
        fx.ssr = 1;
        r.view->setPostFx(fx);
    }
    r.view->setAsyncShaders(true);   // THE SCENE COMPILES IN THE BACKGROUND (with the Streaming pace)
    Scene *s = r.scene;
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    PbrParams glossy = matte;
    glossy.roughness = 0.25f;   // the floor: the reflection reads cards too
    const MaterialId wall = s->createPbrMaterial(matte), floorMat = s->createPbrMaterial(glossy);
    NodeId id = kFirstId;
    bool ok = slab(s, id++, Vec3(0.0f, -0.1f, 0.0f), Vec3(6.4f, 0.2f, 6.4f), floorMat) &&   // floor
              slab(s, id++, Vec3(0.0f, 3.1f, 0.0f), Vec3(6.4f, 0.2f, 6.4f), wall) &&       // roof
              slab(s, id++, Vec3(-3.1f, 1.5f, 0.0f), Vec3(0.2f, 3.0f, 6.0f), wall) &&      // -x
              slab(s, id++, Vec3(3.1f, 1.5f, 0.0f), Vec3(0.2f, 3.0f, 6.0f), wall) &&       // +x
              slab(s, id++, Vec3(0.0f, 1.5f, -3.1f), Vec3(6.4f, 3.0f, 0.2f), wall) &&      // -z
              slab(s, id++, Vec3(0.0f, 1.5f, 3.1f), Vec3(6.4f, 3.0f, 0.2f), wall) &&       // +z
              slab(s, id++, Vec3(-1.2f, 1.0f, -1.0f), Vec3(0.5f, 2.0f, 0.5f), wall) &&     // pillars
              slab(s, id++, Vec3(1.3f, 0.75f, -0.4f), Vec3(0.6f, 1.5f, 0.6f), wall);
    const NodeId lamp = s->createNode(0, id++);
    s->setNodeTransform(lamp, Vec3(0.0f, 2.4f, 0.5f), Quat(), Vec3(1, 1, 1));
    LightDesc ld;
    ld.type = LightType::Point;
    ld.colour = Colour(1, 1, 1);
    ld.intensity = 4.0f;
    ld.range = 20.0f;
    ld.castShadows = true;
    ok = ok && s->setLight(lamp, ld);
    float sh[27] = { 0 };   // no sky, no ambient: the room is closed
    s->setAmbientSh(sh);
    enginetest::testCameraLookAt(r.view, Vec3(0.0f, 1.6f, 2.3f), Vec3(0.2f, 0.9f, -2.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    ok = ok && s->setGlobalIllumination(gi);
    if (!ok) return Room();
    // THE DECLARED ROUTE (CODING_RULES § 6): the gather, the ray reflection, the cards and the store, GI paused
    enginetest::declareRoutes(r.view, s, enginetest::Routes().gather().reflection().cards().store().paused(),
                              (std::string("gi.card_build_late's ") + name + " view").c_str());
    return r;
}

/// AT REST: giAtRest (the decision taken, the cache owing its readers nothing) with a decision and an empty queue,
/// held 2 frames (the published status is a frame late). Returns the frames it took, -1 at the cap.
int toRest(Engine *e, Scene *s, int &frames)
{
    int quiet = 0, f = 0;
    for (; f < kRestCap && quiet < 2; ++f) {
        enginetest::frames(e, 1);
        ++frames;
        const GiStatus g = s->published().gi;
        quiet = (g.giAtRest && g.cards.built && g.cards.restDecisions > 0ull && g.cards.queueLength == 0u) ? quiet + 1 : 0;
    }
    return quiet >= 2 ? f : -1;
}

void printCards(const char *what, const GiStatus &g, unsigned inFlight)
{
    std::printf("== %s: cards resident %u (hash %016llx), admitted %llu, captured %llu, queued %u; demand changed %llu, "
                "first %llu/%llu, rest decisions %llu (readers %u, admitted %u), giAtRest %d, ray builds in flight %u\n",
                what, g.cards.cardsResident, g.cards.residentHash, g.cards.admitted, g.cards.captures,
                g.cards.queueLength, g.cards.demandChangeFrame, g.cards.demandFirst[0], g.cards.demandFirst[1],
                g.cards.restDecisions, g.cards.decisionReaders, g.cards.decisionAdmitted, int(g.giAtRest), inFlight);
}
}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "late";
    if (mode != "late" && mode != "unasked") {
        std::printf("FAIL: the mode is late or unasked, not '%s'\n", mode.c_str());
        return 2;
    }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-card-build-late-" + mode + "-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->setFramePace(FramePace::Streaming);   // in force until set again: every frame of the row
    int frames = 0;
    const auto inFlight = [&]() { return e->published().shaders.rayBuildsInFlight; };

    e->setArm("rays.buildHold", kHoldForever);
    e->setArm("rays.buildHoldIds", mode == "late" ? kReaderBuilds : kGlassEscapeBuild);
    Room h = buildRoom(e, mode == "late" ? "build_late_held" : "build_late_unasked");
    if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
    if (mode == "unasked") {
        // EVERY BUILD LAUNCHED, the glass escape's among them - at the first frame, as the startup gate does (before it
        // the ray tier does not exist and the call launches nothing)
        enginetest::frames(e, 1);
        ++frames;
        e->prepareRayPipelines();
    }

    if (mode == "late") {
        // RUN H: the readers held until kHeldPastBuild frames after the cache's build; every frame of the hold read
        int builtAt = -1, heldFrames = 0, decidedInHold = 0, atRestInHold = 0, firstBad = -1;
        for (int f = 0; f < kRestCap; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const GiStatus g = h.scene->published().gi;
            if (builtAt < 0 && g.cards.built) builtAt = frames;
            if (builtAt < 0) continue;
            ++heldFrames;
            if (g.cards.restDecisions > 0ull) { ++decidedInHold; if (firstBad < 0) firstBad = frames; }
            if (g.giAtRest) { ++atRestInHold; if (firstBad < 0) firstBad = frames; }
            if (frames - builtAt >= kHeldPastBuild) break;
        }
        const GiStatus held = h.scene->published().gi;
        printCards("H, the readers' builds held", held, inFlight());
        std::printf("   the cache built at frame %d; %d frames held after it: %d with a rest decision, %d at rest "
                    "(the first at frame %d)\n", builtAt, heldFrames, decidedInHold, atRestInHold, firstBad);
        check(builtAt > 0, "the cache is built while the readers' builds are held");
        check(inFlight() >= 1u, "the held builds are in flight to the hold's end");
        char w[256];
        std::snprintf(w, sizeof(w), "(1) while a card reader's build is in flight no rest decision is taken and giAtRest is "
                      "false (%d of %d held frames decided, %d at rest)", decidedInHold, heldFrames, atRestInHold);
        check(heldFrames > 0 && decidedInHold == 0 && atRestInHold == 0, w);
        e->setArm("rays.buildHold", 0.0);   // THE LANDING
        const int restH = toRest(e, h.scene, frames);
        const GiStatus gh = h.scene->published().gi;
        printCards("H at rest", gh, inFlight());
        std::printf("   at rest %d frames after the landing\n", restH);
        e->destroyView(h.view);
        e->destroyScene(h.scene);
        enginetest::poseRegistry().erase(h.scene);

        // RUN C: the same room, every build landed before its first frame - its readers record from frame 1
        Room c = buildRoom(e, "build_late_control");
        if (!c.scene) { std::printf("FAIL: the control room: %s\n", e->lastError().c_str()); return 1; }
        const int restC = toRest(e, c.scene, frames);
        const GiStatus gc = c.scene->published().gi;
        printCards("C at rest", gc, inFlight());
        std::printf("   at rest after %d frames\n", restC);
        check(restH >= 0 && restC >= 0, "both runs come to rest");
        check(gc.cards.cardsResident > 0u, "the control's readers admit cards (the room's faces read)");
        std::snprintf(w, sizeof(w), "(2) after the readers' builds land the cache admits the SAME resident set as the control "
                      "(H %u cards, hash %016llx; C %u cards, hash %016llx)", gh.cards.cardsResident,
                      gh.cards.residentHash, gc.cards.cardsResident, gc.cards.residentHash);
        check(gh.cards.cardsResident > 0u && gh.cards.residentHash == gc.cards.residentHash &&
                  gh.cards.cardsResident == gc.cards.cardsResident, w);
        check(gh.cards.captures > 0ull, "the late readers' cards are captured");
    } else {
        // THE UNASKED BUILD: the room comes to rest with the glass escape's build still held
        const int rest = toRest(e, h.scene, frames);
        const GiStatus before = h.scene->published().gi;
        const unsigned flying = inFlight();
        printCards("U at rest, the glass escape's build held", before, flying);
        char w[256];
        std::snprintf(w, sizeof(w), "(1) a build no reader asked for holds no rest: at rest after %d frames with %u ray build(s) "
                      "in flight", rest, flying);
        check(rest >= 0 && flying >= 1u, w);
        e->setArm("rays.buildHold", 0.0);   // THE LANDING
        int landed = -1;
        for (int f = 0; f < 30 && landed < 0; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            if (inFlight() == 0u) landed = frames;
        }
        enginetest::frames(e, kAfterLanding);
        frames += kAfterLanding;
        const GiStatus after = h.scene->published().gi;
        printCards("U after the landing", after, inFlight());
        check(landed > 0, "the held build lands once released");
        std::snprintf(w, sizeof(w), "(2) its landing is no change of the demand: changed %llu -> %llu, decisions %llu -> %llu, "
                      "hash %016llx -> %016llx, at rest %d", before.cards.demandChangeFrame, after.cards.demandChangeFrame,
                      before.cards.restDecisions, after.cards.restDecisions, before.cards.residentHash,
                      after.cards.residentHash, int(after.giAtRest));
        check(after.cards.demandChangeFrame == before.cards.demandChangeFrame &&
                  after.cards.restDecisions == before.cards.restDecisions &&
                  after.cards.residentHash == before.cards.residentHash && after.giAtRest, w);
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
