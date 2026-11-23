// gi.card_build_late (COLD-BOOT-1 item 3, the lead's rulings 2026-11-22) - THE REST LAW's INPUT IS A COMPLETE FRAME.
//
// THE DEFECT IT GUARDS (spikes/cold-boot-1/VERIFY-card-demand.md; the second path: the hold F-fix): in a scene that compiles
// in the background (its view asynchronous, the engine on the Streaming pace) a frame can be INCOMPLETE - a card reader whose
// ray pipeline is still building takes its "not this frame" path (RayQueryTier::aheadReady) and records nothing; an object
// whose permutation is still compiling draws the PLACEHOLDER (rough 0.8: the reflection traces no ray there) or is
// skipped; a material wears a FALLBACK while its texture loads. The cache's demand window, opened by its own build, closed
// on what those frames read, the rest's ONE decision was taken on it, and the frame that became complete was no change of
// the demand: the scene's real readers stamped after the window and their cards never arrived (the startup gate's cold
// Basic world: admitted 0 through every phase). THE LAW (PublishedFrame::Completeness): an incomplete scene is not at rest
// and takes no rest decision; the frame in which it becomes complete is a demand change (the window reopens, the readers
// rest anew); and a FAILURE is final, never pending - a scene whose build fails comes to rest on the picture it can draw.
// THE FIXTURE: a closed 6 x 3 x 6 m room of carded slabs, two carded pillars and a lamp inside, the eye inside; the chain
// at High, the cards on, the rays on, GI paused; a 128 x 128 StillPicture view with the post chain and the ray reflection
// (it gathers: both card readers), asynchronous, the engine on the Streaming pace. The doors: rays.buildHold (+ Ids) a ray
// build held in flight, Engine::setAsyncShaderPlaceholdersOnly every object's own permutation held unrequested (it draws its
// placeholder), textures.residencyHold every texture bind awaited; rays.buildFail and shaders.asyncFail force failures.
// THE ROWS (argv[1]), each one law; a held row: run H held from the first frame until kHeldPastBuild frames after the
// cache's build (three times its demand window: S + a read period + the read's lag, ~19 frames), then released - (1) while
// held, NO rest decision and giAtRest false; (2) after the release, at rest, THE SAME RESIDENT SET as run C - the same room
// at the same pose, a new scene drawn after everything landed: CardCacheStatus::residentHash equal (the scenes' nodes carry
// the same explicit ids), cards > 0:
//   late      - the two card readers' builds (the reflection's, the gather's) held. RED before the fix: decided on 32 of 61
//               held frames on an empty demand, 0 cards for ever (spikes/cold-boot-1/build-late-red/).
//   pso       - every object's own graphics permutation held (placeholders only): the readers ready, the picture not.
//   texture   - the floor's roughness map awaited (what the reflection traces follows it).
//   unasked   - a build NO READER ASKED FOR holds no rest: the glass escape's build launched with the rest
//               (Engine::prepareRayPipelines) and held in a room with no blended material - (1) the scene comes to rest while
//               it is in flight; (2) its landing is no change: the change frame, the decisions and the resident set the same
//               kAfterLanding frames after it.
//   psofail   - every object's own permutation FAILS (shaders.asyncFail): the scene comes to rest on its placeholders - the
//               resident set of a control room whose materials ARE the placeholder's (albedo 0.18, rough 0.8, dielectric).
//   buildfail - the readers' builds held past the window, then released into a forced FAILURE (rays.buildFail): no reader
//               runs, the scene comes to rest with no card admitted.
//   corrupt   - the floor wears a texture whose file is corrupt (the loader's fallback is its final texture): the scene comes
//               to rest with the control's cards (the same room, no texture: an albedo map moves no reader's ray).
//   window    - THE DECISION's FIRE SITE (COLD-BOOT-1's leftover L2): the rest decision is SCHEDULED on a complete frame
//               (CardCacheStatus::decisionReaders armed, restDecisions 0) and fires SurfaceCache::demandDueFrames (~9) frames
//               later; the frames between turn INCOMPLETE with no demand change (the door frames.incompleteHold, term bit
//               4: an object entering the view under a sub-pixel turn while its permutation compiles) - (1) no rest decision
//               is taken while they are incomplete; (2) released, the scene comes to rest, complete. RED before the fix: the
//               decision fired on the due frame, incomplete (the schedule's term only, OgreSurfaceCache.cpp's decide).
//   silent    - EVERY RAY CONSUMER REFUSED BY ITS BUILD OWES ITS TERM (SHADER-SYSTEM-1 stage 7, the lead's ruling P1): the
//               builds of the card gather, the card movers and the voxel sides held - (0) the CONTROL: the voxel sides ran
//               and refused (GiStatus::voxelSides.waits above 0). The card gather's and the movers' consumers are NOT
//               reached in this room (no mover; the card gather runs only behind the reflection's card demand:
//               spikes/shader-system/ss7/b8-hold/RESULT.md) - their door is the same line of aheadReady, printed;
//               (1) every frame after both refused, while held, is published INCOMPLETE with bit 2; (2) released, the
//               room comes to rest, complete. RED before the fix: only the card readers' builds (the reflection, the
//               gather, the glass escape) named their scene - these three refused silently, their frames complete.
//   complete  - A BUILD IS NEVER JOINED ON A COMPLETE FRAME OF A SCENE ON THE SERVICE (SHADER-SYSTEM-1 stage 7, call 3; the
//               lead's ruling P1): the card readers' builds held, the engine on the COMPLETE pace - (1) the asynchronous
//               room: every frame of the hold is published INCOMPLETE by bit 2 and the builds stay in flight (RED before:
//               a Complete frame joined them - the term never set, the builds landed at once); (2) THE CONTROL, a
//               SYNCHRONOUS room on the same pace and hold: the builds are joined at once (none in flight), bit 2 never
//               set - the join kept on purpose where frames are counted. AND THE NOTICE's FACT (AsyncShaderStats::rayBurst,
//               rayBuildsLanded): held, the ray burst's open part (total - landed) is the builds in flight (at least the
//               2 held; the unheld builds the room launched land inside the burst); joined, total of total and
//               rayBuildsLanded risen by the joined builds (RED with the burst not fed: 0 of 0).
//   failwait  - A FAILURE WAITS FOR ITS ANSWER (COLD-BOOT-1's leftover L1): every object's own permutation FAILS
//               (shaders.asyncFail) on a COLD boot, so its placeholder is still compiling when it fails - (1) those frames
//               are published INCOMPLETE by a pending skip (bit 1: the object waits for the placeholder it will draw);
//               (2) the scene comes to rest, complete (the wait ends: a placeholder's request is urgent, never failed by the
//               door). RED before the fix: the FAILED entry was returned and counted as nothing - the frames read
//               complete with every object missing (Hlms::getMaterialAsync's tail).
//   landing_gather - A ROUTE JOINS A FRAME ONLY WHEN ITS PIPELINE AND ITS LANDED SET ARE BOTH BUILT (SHADER-SYSTEM-1
//               stage 7, P3; row gi.route_landing.gather; COLD: the room's permutations must be unbuilt in the process):
//               the gather's build held while the room draws - (0) THE CONTROLS: while held, ahead passes prepared and
//               permutations requested (both above 0) and the frames INCOMPLETE by bit 2; (1) NOT HERE: the frame
//               with the measuring arm shaders.aheadPassOff on equal to it off is OWED (this room's picture never held
//               still across an A/B/A compare: spikes/shader-system/ss7/p3/hold2-green.log, no verdict); (2) released once the background compiler is quiet: over the landing frame and kLandingWatch
//               frames after it, 0 graphics permutations built in a frame, 0 placeholder draws, 0 pending skips - RED
//               before P3 (30 pending skips: the room's draw skipped every frame, the picture cut to black;
//               spikes/shader-system/ss7/p3/hold1-red.log; b8-hold/RESULT.md); the permutations REQUESTED at the landing
//               printed (OPEN: the hit decode's PBS twins and the GI's bounce injection are still asked for there); (3)
//               THE CONTROL: the gather is DRAWN within kLandingBound frames of its release; (4) at rest, ZERO COST: 0
//               ahead passes and 0 requests over kRestWatch frames.
//   landing_order - THE FRAME's OWN REQUESTS ARE SERVED FIRST (SHADER-SYSTEM-1 stage 7; the owner, 2026-11-23: "faster
//               loading over correctness when loading in shaders"; row gi.route_landing.order; COLD): the room built
//               COLD with the reflection and the gather held (both owed: the ahead pass and the request-only hit decode
//               run); TWO ENGINES
//               in one process, P3 ON first, then OFF (the arm shaders.aheadPassOff; OFF runs second, on a driver warmed
//               by ON: the law is the stricter for ON) - (0) THE CONTROLS: ON's ahead class ran jobs (above 0), OFF's ran
//               none; (1) TIME-TO-PICTURE: the first frame from which the room's picture is its held final picture, exact
//               (a hash of the read-back codes every frame) - ON's no later than OFF's + kOrderBound frames. RED before the
//               ahead class (the ahead placeholders pushed URGENT to the front of the one queue, ahead of the room's own:
//               spikes/shader-system/ss7/p3/order/); (2) the two final pictures are the same codes (the ahead requests
//               change no picture).
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/GiStatus.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Mesh.h"
#include "jahshaka/engine/Monitor.h"
#include "jahshaka/engine/Published.h"
#include "jahshaka/engine/Shaders.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <unistd.h>
#include <vector>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
/// The frames after the cache's build a held run stays held: 3 x the demand window (S 10 + a read period 4 + the read's
/// lag ~5 frames, SurfaceCache::demandWindowOpen), so a window opened by the build is long closed.
const int kHeldPastBuild = 60;
/// rays.buildHoldIds / rays.buildFail: bit k for AheadId k (OgreFrameMonitor.cpp's row) - the reflection (1) and the
/// gather (32); the glass escape (16)
const double kReaderBuilds = 1.0 + 32.0, kGlassEscapeBuild = 16.0;
/// .silent's held builds: the three consumers that refused SILENTLY before - the card gather (2), the card movers (4), the
/// voxel sides (8)
const double kSilentBuilds = 2.0 + 4.0 + 8.0;
/// .silent's bound on the frames until its reached consumer (the voxel sides) has refused (it runs with the room's first arm)
const int kSilentReachCap = 600;
/// .silent's frames held after both refused (the term must stand on every one of them)
const int kSilentHeldFrames = 30;
/// A hold the run releases itself (arm back to 0): longer than any run
const double kHoldForever = 100000.0;
/// The frames the unasked run watches after its held build lands
const int kAfterLanding = 24;
/// The frames a run may take to come to rest (a bound on the fixture, not a law)
const int kRestCap = 1500;
/// THE LANDING ROWS (P3): the frames watched from the release (the landing and after it), the bound on the frames until
/// the released route is drawn (its harvest at the next frame's head, its set measured the frame before: 2, + the
/// published status's frame of lag), the cap on the hold's wait for a quiet background compiler, and the frames watched
/// at rest for the zero cost
const int kLandingWatch = 30, kLandingBound = 4, kQuietCap = 1500, kRestWatch = 30;
/// THE ORDER ROW: the cap on a run's frames until its service is quiet (a bound on the fixture: a cold room's frames are
/// ~2 ms), the frames past the service's quiet a run keeps drawing (the picture's last change after the last job lands),
/// and the bound on ON's time-to-picture over OFF's (frames): twice the same arm's spread between two runs (OFF 505 and
/// 534: 29 frames), rounded up - the red-first run (the ahead class removed) read ON 763 against OFF 505, +258; the green
/// ON 439 against OFF 534 (spikes/shader-system/ss7/p3/order/)
const int kOrderCap = 6000, kOrderTail = 30, kOrderBound = 60;
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

/// The room's look: as authored, or every material the async placeholder's (OgreEngine.cpp: albedo 0.18, rough 0.8,
/// dielectric) - psofail's control; and the floor's map: none, a roughness texture, or a corrupt albedo file.
enum class Floor { Plain, Texture, Corrupt };
struct Look { bool placeholder = false; Floor floor = Floor::Plain; };

struct Room { View *view = nullptr; Scene *scene = nullptr; };
Room buildRoom(Engine *e, const char *name, const Look &look = Look())
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
    if (look.placeholder) {
        matte.albedo = Colour(0.18f, 0.18f, 0.18f);
        matte.roughness = 0.8f;
        matte.metalness = 0.0f;
        glossy = matte;
    }
    const MaterialId wall = s->createPbrMaterial(matte), floorMat = s->createPbrMaterial(glossy);
    if (look.floor == Floor::Texture) {
        // a ROUGHNESS map (white: the floor's own 0.25): what the reflection traces follows it, and it is no voxel input
        // (an albedo map is, and the chain waits for those: PhotonComponent::giVoxelTexturesPending)
        std::vector<unsigned char> px(16u * 16u * 4u, 255u);
        const TextureId t = s->createTexture(16u, 16u, px.data(), false);
        if (!t || !s->setPbrTexture(floorMat, PbrTextureSlot::Roughness, t)) return Room();
    } else if (look.floor == Floor::Corrupt) {
        char cwd[4096] = { 0 };
        const std::string path = std::string(getcwd(cwd, sizeof(cwd)) ? cwd : ".") + "/card_build_late_corrupt.png";
        {
            std::ofstream f(path, std::ios::binary);
            const char bytes[] = "\x89PNG\r\n\x1a\nnot an image: the decoder fails here";
            f.write(bytes, sizeof(bytes));
        }
        const TextureId t = s->loadTexture(path, true);
        std::printf("   the corrupt texture: %s (%s)\n", t ? "loaded (its decode fails: the loader's fallback)"
                                                            : "refused at its load", t ? "" : e->lastError().c_str());
        if (t && !s->setPbrTexture(floorMat, PbrTextureSlot::Albedo, t)) return Room();
    }
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

/// THE FRAME's COMPLETENESS at rest (PublishedFrame::Completeness): complete, no incomplete frame in a row - a scene stuck
/// incomplete is visible there (the lead's condition: no term held for ever)
void checkComplete(const char *who, Scene *s)
{
    const PublishedFrame::Completeness c = s->published().completeness;
    char w[200];
    std::snprintf(w, sizeof(w), "(visible) %s at rest is complete: incomplete %d, terms %u, %llu incomplete frames in a row, "
                  "completion serial %llu", who, int(c.incomplete), c.terms, c.incompleteFrames, c.completionSerial);
    check(!c.incomplete && c.incompleteFrames == 0ull, w);
}

void drop(Engine *e, Room &r)
{
    e->destroyView(r.view);
    e->destroyScene(r.scene);
    enginetest::poseRegistry().erase(r.scene);
    r = Room();
}
}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "late";
    const std::vector<std::string> modes = { "late", "pso", "texture", "unasked", "psofail", "buildfail", "corrupt",
                                             "window", "failwait", "silent", "complete", "landing_gather",
                                             "landing_order" };
    if (std::find(modes.begin(), modes.end(), mode) == modes.end()) {
        std::printf("FAIL: the mode is late, pso, texture, unasked, psofail, buildfail, corrupt, window, failwait, "
                    "silent, complete, landing_gather or landing_order, not '%s'\n", mode.c_str());
        return 2;
    }
    std::string err;
    // ---- .landing_order: the frame's own requests first; time-to-picture with P3 on no later than off -------------
    if (mode == "landing_order") {
        struct OrderRun { bool ok = false; int pictureFrame = -1, frames = 0; unsigned long long aheadRun = 0, promoted = 0;
                          unsigned long long skips = 0, placeholders = 0; uint64_t finalHash = 0; };
        const auto runOrder = [&](bool aheadOff) {
            OrderRun o;
            const std::string olog = std::string("test-gi-card-build-late-landing-order-") + (aheadOff ? "off" : "on") +
                                     "-ogre.log";
            auto engine = Engine::create(
                enginetest::testEngineConfig(olog, enginetest::EngineOverrides().shaderCacheOff().realDestroy()), err);
            if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return o; }
            Engine *e = engine.get();
            e->setRayTracing(true);
            e->setFramePace(FramePace::Streaming);
            e->setArm("rays.buildHold", kHoldForever);
            e->setArm("rays.buildHoldIds", kReaderBuilds);   // both routes owed: the ahead pass AND the request-only decode
            if (aheadOff) e->setArm("shaders.aheadPassOff", 1.0);
            Room r = buildRoom(e, aheadOff ? "route_landing_order_off" : "route_landing_order_on");
            if (!r.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return o; }
            const AsyncShaderStats at = e->published().shaders;
            std::vector<uint64_t> hashes;
            int quietAt = -1;
            for (int f = 0; f < kOrderCap && (quietAt < 0 || f < quietAt + kOrderTail); ++f) {
                enginetest::frames(e, 1);
                Image img;
                if (!r.view->readPixels(img) || img.rgba.empty()) { std::printf("FAIL: the read-back\n"); return o; }
                uint64_t h = 1469598103934665603ull;   // FNV-1a over the codes
                for (unsigned char c : img.rgba) { h ^= c; h *= 1099511628211ull; }
                hashes.push_back(h);
                const AsyncShaderStats a = e->published().shaders;
                // the SERVICE quiet (pending counts the ray builds in flight: OgreEngine.cpp's asyncShaderStats)
                if (quietAt < 0 && f > 10 && a.pending == a.rayBuildsInFlight) quietAt = f;
            }
            const AsyncShaderStats end = e->published().shaders;
            o.frames = int(hashes.size());
            o.finalHash = hashes.empty() ? 0ull : hashes.back();
            for (int f = int(hashes.size()) - 1; f >= 0 && hashes[size_t(f)] == o.finalHash; --f) o.pictureFrame = f + 1;
            o.aheadRun = end.aheadRun - at.aheadRun;
            o.promoted = end.aheadPromoted - at.aheadPromoted;
            o.skips = end.pendingSkips - at.pendingSkips;
            o.placeholders = end.placeholderDraws - at.placeholderDraws;
            o.ok = quietAt >= 0;
            std::printf("   P3 %s: the service quiet at frame %d; the held picture from frame %d (of %d); ahead-class jobs %llu, "
                        "promoted %llu; pending skips %llu, placeholder draws %llu\n", aheadOff ? "OFF" : "ON ", quietAt + 1,
                        o.pictureFrame, o.frames, o.aheadRun, o.promoted, o.skips, o.placeholders);
            drop(e, r);
            engine.reset();
            return o;
        };
        const OrderRun on = runOrder(false), off = runOrder(true);
        char w[320];
        std::snprintf(w, sizeof(w), "(0) the controls: both runs reached a quiet service; ON's ahead class ran %llu jobs (> 0), "
                      "OFF's %llu (0)", on.aheadRun, off.aheadRun);
        check(on.ok && off.ok && on.aheadRun > 0ull && off.aheadRun == 0ull, w);
        std::snprintf(w, sizeof(w), "(1) TIME-TO-PICTURE: the room's held picture from frame %d with P3 ON, %d with it OFF "
                      "(bound: ON <= OFF + %d)", on.pictureFrame, off.pictureFrame, kOrderBound);
        check(on.pictureFrame > 0 && off.pictureFrame > 0 && on.pictureFrame <= off.pictureFrame + kOrderBound, w);
        std::snprintf(w, sizeof(w), "(2) the two held pictures are the same codes (%016llx / %016llx)",
                      (unsigned long long)on.finalHash, (unsigned long long)off.finalHash);
        check(on.finalHash == off.finalHash, w);
        std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
        return g_fail ? 1 : 0;
    }
    // .pso IS a permutation still compiling: its row is COLD (its CMake line) and boots with no shader cache - a warm one
    // loads the room's permutations ready (every green row's run promotes its cache: tests/support/fresh_home.sh); so is
    // .failwait's placeholder
    const std::string log = "test-gi-card-build-late-" + mode + "-ogre.log";
    const bool cold = mode == "pso" || mode == "failwait" || mode == "landing_gather";
    auto engine = Engine::create(cold ? enginetest::testEngineConfig(log, enginetest::EngineOverrides().shaderCacheOff())
                                      : enginetest::testEngineConfig(log),
                                 err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->setFramePace(FramePace::Streaming);   // in force until set again: every frame of the row
    int frames = 0;
    char w[320];
    const auto inFlight = [&]() { return e->published().shaders.rayBuildsInFlight; };

    // ---- .landing_gather: the gather joins only when its pipeline AND its landed set are built (P3) -------------------
    if (mode == "landing_gather") {
        e->setArm("rays.buildHold", kHoldForever);
        e->setArm("rays.buildHoldIds", 32.0);
        Room r = buildRoom(e, "route_landing_gather");
        if (!r.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        const auto shaders = [&]() { return e->published().shaders; };
        const unsigned long long submittedAt = shaders().submitted, aheadAt = shaders().aheadPasses;
        // THE HOLD: until the background compiler is quiet (only the held build in flight) and the frame incomplete
        int f = 0, withTerm = 0;
        for (; f < kQuietCap; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const AsyncShaderStats a = shaders();
            if (r.scene->published().completeness.terms & 4u) ++withTerm;
            if (f > 10 && a.pending == 1u && a.rayBuildsInFlight == 1u) break;
        }
        const AsyncShaderStats held = shaders();
        std::snprintf(w, sizeof(w), "(0) the hold: the compiler quiet after %d frames (pending %u, ray builds in flight %u), "
                      "%llu permutations requested, %llu ahead passes, %d frames INCOMPLETE by bit 2", f, held.pending,
                      held.rayBuildsInFlight, held.submitted - submittedAt, held.aheadPasses - aheadAt, withTerm);
        check(f < kQuietCap && held.submitted > submittedAt && held.aheadPasses > aheadAt &&
                  (r.scene->published().completeness.terms & 4u) != 0u, w);
        // THE RELEASE: the landing and the frames after it
        const AsyncShaderStats before = shaders();
        e->setArm("rays.buildHold", 0.0);
        int drawnAt = -1;
        for (int k = 0; k < kLandingWatch; ++k) {
            enginetest::frames(e, 1);
            ++frames;
            const bool drawn = r.scene->published().gi.gather.running;
            if (drawn && drawnAt < 0) drawnAt = k + 1;
        }
        const AsyncShaderStats after = shaders();
        const unsigned long long requested = after.submitted - before.submitted;
        const unsigned long long built = after.graphicsBuiltInFrame - before.graphicsBuiltInFrame;
        const unsigned long long ph = after.placeholderDraws - before.placeholderDraws;
        const unsigned long long skips = after.pendingSkips - before.pendingSkips;
        std::snprintf(w, sizeof(w), "(2) the landing and %d frames after it: %llu graphics permutations built in a frame, "
                      "%llu placeholder draws, %llu pending skips", kLandingWatch, built, ph, skips);
        check(built == 0ull && ph == 0ull && skips == 0ull, w);
        std::printf("   (2) OPEN, printed: %llu permutations requested of the background compiler at the landing\n",
                    requested);
        std::snprintf(w, sizeof(w), "(3) the control: the released route drawn %d frames after its release (bound %d)", drawnAt,
                      kLandingBound);
        check(drawnAt > 0 && drawnAt <= kLandingBound, w);
        // AT REST: ZERO COST
        const int took = toRest(e, r.scene, frames);
        const AsyncShaderStats rest = shaders();
        enginetest::frames(e, kRestWatch);
        frames += kRestWatch;
        const AsyncShaderStats restAfter = shaders();
        std::snprintf(w, sizeof(w), "(4) at rest (after %d frames): over %d frames %llu ahead passes, %llu requests", took,
                      kRestWatch, restAfter.aheadPasses - rest.aheadPasses, restAfter.submitted - rest.submitted);
        check(took >= 0 && restAfter.aheadPasses == rest.aheadPasses && restAfter.submitted == rest.submitted, w);
        drop(e, r);
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }

    // ---- .complete: a Complete frame of an asynchronous room never joins; a synchronous room does (the control) -------
    if (mode == "complete") {
        e->setFramePace(FramePace::Complete);
        e->setArm("rays.buildHold", kHoldForever);
        e->setArm("rays.buildHoldIds", kReaderBuilds);
        for (const bool async : { true, false }) {
            Room r = buildRoom(e, async ? "build_late_complete_async" : "build_late_complete_sync");
            if (!r.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
            if (!async) r.view->setAsyncShaders(false);   // THE CONTROL: a synchronous room
            int withTerm = 0, held = 0;
            unsigned inFlightMin = ~0u;
            const unsigned long long landedBefore = e->published().shaders.rayBuildsLanded;
            for (int f = 0; f < kSilentHeldFrames; ++f) {
                enginetest::frames(e, 1);
                ++frames;
                if (f < 2) continue;   // the readers' first call and the term's note at the next frame's head
                ++held;
                const PublishedFrame::Completeness c = r.scene->published().completeness;
                if (c.incomplete && (c.terms & 4u)) ++withTerm;
                inFlightMin = std::min(inFlightMin, inFlight());
            }
            if (async) {
                std::snprintf(w, sizeof(w), "(1) the asynchronous room on the Complete pace: every held frame INCOMPLETE by "
                              "bit 2 (%d of %d), the readers' builds still in flight (at least %u) - never joined", withTerm,
                              held, inFlightMin);
                check(held > 0 && withTerm == held && inFlightMin >= 1u, w);
                const AsyncShaderStats::Burst rb = e->published().shaders.rayBurst;
                std::snprintf(w, sizeof(w), "(1) the notice: the ray burst while held reads %u of %u (the builds in flight "
                              "%u)", rb.landed, rb.total, inFlight());
                check(inFlight() >= 2u && rb.total - rb.landed == inFlight(), w);
            } else {
                std::snprintf(w, sizeof(w), "(2) the control, a synchronous room on the same pace and hold: the builds joined "
                              "(in flight at most %u), bit 2 on %d of %d frames", inFlightMin, withTerm, held);
                check(held > 0 && withTerm == 0 && inFlightMin == 0u, w);
                const AsyncShaderStats::Burst rb = e->published().shaders.rayBurst;
                const unsigned long long landed = e->published().shaders.rayBuildsLanded - landedBefore;
                std::snprintf(w, sizeof(w), "(2) the notice: joined, the ray burst reads %u of %u, %llu landed", rb.landed,
                              rb.total, landed);
                check(rb.total >= 2u && rb.landed == rb.total && landed >= 2ull, w);
            }
            drop(e, r);
        }
        e->setArm("rays.buildHold", 0.0);
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }

    // ---- .silent: the three consumers that refused silently name their scene (bit 2) ---------------------------------
    if (mode == "silent") {
        e->setArm("rays.buildHold", kHoldForever);
        e->setArm("rays.buildHoldIds", kSilentBuilds);
        Room h = buildRoom(e, "build_late_silent");
        if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        // (0) until the reached consumer has refused (it counts its wait where the door said "not this frame")
        int reachedAt = -1;
        for (int f = 0; f < kSilentReachCap && reachedAt < 0; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const GiStatus g = h.scene->published().gi;
            if (g.voxelSides.waits > 0ull) reachedAt = frames;
        }
        {
            const GiStatus g = h.scene->published().gi;
            std::snprintf(w, sizeof(w), "(0) the control: the voxel sides ran and refused (voxel side waits %llu, at frame "
                          "%d of a bound of %d; card gather waits %llu - not reached here)", g.voxelSides.waits, reachedAt,
                          kSilentReachCap, g.cards.gatherWaits);
            check(reachedAt > 0, w);
        }
        // (1) every frame after it, held, carries bit 2 (the term is noted at a frame's head)
        int heldFrames = 0, withTerm = 0;
        for (int f = 0; f < kSilentHeldFrames && reachedAt > 0; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            ++heldFrames;
            const PublishedFrame::Completeness c = h.scene->published().completeness;
            if (c.incomplete && (c.terms & 4u)) ++withTerm;
        }
        std::snprintf(w, sizeof(w), "(1) while held, every frame is published INCOMPLETE by bit 2, a ray consumer refused by "
                      "its build (%d of %d held frames)", withTerm, heldFrames);
        check(heldFrames > 0 && withTerm == heldFrames, w);
        check(inFlight() >= 1u, "the held builds are in flight to the hold's end");
        e->setArm("rays.buildHold", 0.0);   // THE BUILDS LAND
        const int rest = toRest(e, h.scene, frames);
        std::printf("   at rest %d frames after the release\n", rest);
        check(rest >= 0, "(2) released, the room comes to rest");
        checkComplete("the silent room", h.scene);
        drop(e, h);
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }

    // ---- the held rows: hold, the law in the hold, release, at rest against the control ----------------------------
    if (mode == "late" || mode == "pso" || mode == "texture") {
        Look look;
        std::function<void()> release;
        if (mode == "late") {
            e->setArm("rays.buildHold", kHoldForever);
            e->setArm("rays.buildHoldIds", kReaderBuilds);
            release = [&]() { e->setArm("rays.buildHold", 0.0); };
        } else if (mode == "pso") {
            e->setAsyncShaderPlaceholdersOnly(true);
            release = [&]() { e->setAsyncShaderPlaceholdersOnly(false); };
        } else {
            look.floor = Floor::Texture;
            e->setArm("textures.residencyHold", 1.0);
            // (an arm is LATCHED at a frame's start, OgreEngine's mArms.latch: one frame first, so the room's bind reads it)
            enginetest::frames(e, 1);
            ++frames;
            release = [&]() { e->setArm("textures.residencyHold", 0.0); };
        }
        Room h = buildRoom(e, "build_late_held", look);
        if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        if (mode != "late") {
            // THE READERS READY: every ray build launched at the first frame and WAITED FOR on its published state before
            // the hold's frames count (a build is wall-clock work: CODING_RULES § 5) - .pso is COLD, its driver cache its
            // own; a row that ended at its frame cap before a cold build landed (the gather's, ~13 s) left it unstored,
            // and every run held that reader too (the feature end's red, 2026-11-22). Under placeholders-only the wait
            // takes the placeholders' jobs alone: the room's own permutations stay unrequested.
            enginetest::frames(e, 1);
            ++frames;
            e->prepareRayPipelines();
            e->waitForAsyncShaders();
        }
        const unsigned long long phBefore = e->published().shaders.placeholderDraws;
        int builtAt = -1, heldFrames = 0, decidedInHold = 0, atRestInHold = 0, firstBad = -1;
        unsigned awaitingSeen = 0u, termsSeen = 0u;
        for (int f = 0; f < kRestCap; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const GiStatus g = h.scene->published().gi;
            awaitingSeen = std::max(awaitingSeen, e->published().streaming.materialsAwaitingTexture);
            termsSeen |= h.scene->published().completeness.terms;
            if (builtAt < 0 && g.cards.built) builtAt = frames;
            if (builtAt < 0) continue;
            ++heldFrames;
            if (g.cards.restDecisions > 0ull) { ++decidedInHold; if (firstBad < 0) firstBad = frames; }
            if (g.giAtRest) { ++atRestInHold; if (firstBad < 0) firstBad = frames; }
            if (frames - builtAt >= kHeldPastBuild) break;
        }
        const unsigned long long phHeld = e->published().shaders.placeholderDraws - phBefore;
        printCards("H, held", h.scene->published().gi, inFlight());
        std::printf("   the cache built at frame %d; %d frames held after it: %d with a rest decision, %d at rest (the first "
                    "at frame %d); placeholder draws in the hold %llu, materials awaiting a texture (most) %u\n", builtAt,
                    heldFrames, decidedInHold, atRestInHold, firstBad, phHeld, awaitingSeen);
        check(builtAt > 0, "the cache is built while the frame is held incomplete");
        {
            // the hold's own term on the published record (bit 0 a placeholder, 1 a skip, 2 a refused reader, 3 a texture)
            const unsigned want = mode == "late" ? 4u : mode == "pso" ? 1u : 8u;
            std::snprintf(w, sizeof(w), "the frame is published INCOMPLETE by the hold's own term (terms seen %u, wanted bit "
                          "%u)", termsSeen, want);
            check((termsSeen & want) != 0u, w);
        }
        if (mode == "late") check(inFlight() >= 1u, "the held builds are in flight to the hold's end");
        if (mode == "pso") check(phHeld > 0ull, "the held objects draw their placeholders through the hold");
        if (mode == "texture") check(awaitingSeen > 0u, "the floor's material awaits its texture through the hold");
        std::snprintf(w, sizeof(w), "(1) while the frame is incomplete (%s) no rest decision is taken and giAtRest is false "
                      "(%d of %d held frames decided, %d at rest)", mode.c_str(), decidedInHold, heldFrames, atRestInHold);
        check(heldFrames > 0 && decidedInHold == 0 && atRestInHold == 0, w);
        release();   // THE FRAME BECOMES COMPLETE
        const int restH = toRest(e, h.scene, frames);
        const GiStatus gh = h.scene->published().gi;
        printCards("H at rest", gh, inFlight());
        std::printf("   at rest %d frames after the release\n", restH);
        checkComplete("H", h.scene);
        drop(e, h);
        // RUN C: the same room, everything landed before its first frame
        Room c = buildRoom(e, "build_late_control", look);
        if (!c.scene) { std::printf("FAIL: the control room: %s\n", e->lastError().c_str()); return 1; }
        const int restC = toRest(e, c.scene, frames);
        const GiStatus gc = c.scene->published().gi;
        printCards("C at rest", gc, inFlight());
        std::printf("   at rest after %d frames\n", restC);
        check(restH >= 0 && restC >= 0, "both runs come to rest");
        check(gc.cards.cardsResident > 0u, "the control's readers admit cards (the room's faces read)");
        std::snprintf(w, sizeof(w), "(2) once the frame is complete the cache admits the SAME resident set as the control "
                      "(H %u cards, hash %016llx; C %u cards, hash %016llx)", gh.cards.cardsResident,
                      gh.cards.residentHash, gc.cards.cardsResident, gc.cards.residentHash);
        check(gh.cards.cardsResident > 0u && gh.cards.residentHash == gc.cards.residentHash &&
                  gh.cards.cardsResident == gc.cards.cardsResident, w);
        check(gh.cards.captures > 0ull, "the late cards are captured");
    } else if (mode == "unasked") {
        e->setArm("rays.buildHold", kHoldForever);
        e->setArm("rays.buildHoldIds", kGlassEscapeBuild);
        Room h = buildRoom(e, "build_late_unasked");
        if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        // EVERY BUILD LAUNCHED, the glass escape's among them - at the first frame, as the startup gate does (before it
        // the ray tier does not exist and the call launches nothing)
        enginetest::frames(e, 1);
        ++frames;
        e->prepareRayPipelines();
        const int rest = toRest(e, h.scene, frames);
        const GiStatus before = h.scene->published().gi;
        const unsigned flying = inFlight();
        printCards("U at rest, the glass escape's build held", before, flying);
        std::snprintf(w, sizeof(w), "(1) a build no reader asked for holds no rest: at rest after %d frames with %u ray "
                      "build(s) in flight", rest, flying);
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
        std::snprintf(w, sizeof(w), "(2) its landing is no change of the demand: changed %llu -> %llu, decisions %llu -> "
                      "%llu, hash %016llx -> %016llx, at rest %d", before.cards.demandChangeFrame,
                      after.cards.demandChangeFrame, before.cards.restDecisions, after.cards.restDecisions,
                      before.cards.residentHash, after.cards.residentHash, int(after.giAtRest));
        check(after.cards.demandChangeFrame == before.cards.demandChangeFrame &&
                  after.cards.restDecisions == before.cards.restDecisions &&
                  after.cards.residentHash == before.cards.residentHash && after.giAtRest, w);
    } else if (mode == "window") {
        Room h = buildRoom(e, "build_late_window");
        if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        // THE SCHEDULE, read on its published state (a frame late; the door latches at the next frame's start: ~7 of the
        // due's ~9 frames remain)
        int scheduledAt = -1, decidedFirst = -1;
        for (int f = 0; f < kRestCap && scheduledAt < 0 && decidedFirst < 0; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const GiStatus g = h.scene->published().gi;
            if (g.cards.restDecisions > 0ull) decidedFirst = frames;
            else if (g.cards.decisionReaders != 0u) scheduledAt = frames;
        }
        std::snprintf(w, sizeof(w), "the decision is seen SCHEDULED before it fires (scheduled at frame %d, decided first at "
                      "%d)", scheduledAt, decidedFirst);
        check(scheduledAt > 0, w);
        if (scheduledAt < 0) { std::printf("\nFAIL (%d failures, %d frames)\n", g_fail, frames); return 1; }
        e->setArm("frames.incompleteHold", 1.0);   // THE FRAMES BETWEEN TURN INCOMPLETE
        // held 3 x the due: the decision would have fired inside it
        const int kHeld = 30;
        int incompleteSeen = 0, decidedIncomplete = 0;
        unsigned termsSeen = 0u;
        for (int f = 0; f < kHeld; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const PublishedFrame::Completeness c = h.scene->published().completeness;
            termsSeen |= c.terms;
            if (!c.incomplete) continue;
            ++incompleteSeen;
            if (h.scene->published().gi.cards.restDecisions > 0ull) ++decidedIncomplete;
        }
        printCards("W, incomplete", h.scene->published().gi, inFlight());
        std::printf("   scheduled at frame %d; %d held frames, %d published incomplete (terms seen %u), %d of them decided\n",
                    scheduledAt, kHeld, incompleteSeen, termsSeen, decidedIncomplete);
        check(incompleteSeen >= kHeld - 2 && (termsSeen & 16u) != 0u,
              "the held frames are published INCOMPLETE by the door's own term (bit 4)");
        std::snprintf(w, sizeof(w), "(1) a frame that turns incomplete after the decision is scheduled takes no decision (%d "
                      "of %d incomplete frames decided)", decidedIncomplete, incompleteSeen);
        check(incompleteSeen > 0 && decidedIncomplete == 0, w);
        e->setArm("frames.incompleteHold", 0.0);   // THE FRAME BECOMES COMPLETE: a demand change, the window reopens
        const int rest = toRest(e, h.scene, frames);
        printCards("W at rest", h.scene->published().gi, inFlight());
        std::snprintf(w, sizeof(w), "(2) released, the scene comes to rest (%d frames, the cap %d)", rest, kRestCap);
        check(rest >= 0, w);
        checkComplete("W", h.scene);
    } else if (mode == "failwait") {
        e->setArm("shaders.asyncFail", 1.0);   // latched at the next frame's start: before the room's first draw
        Room h = buildRoom(e, "build_late_failwait");
        if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        // THE FIRST FRAMES: every object's own request FAILS, its placeholder's (urgent) is still compiling - a compile
        // publishes at a frame point, so the frame that requests it draws without it
        const int kFirstFrames = 10;
        unsigned termsSeen = 0u;
        int incompleteSeen = 0;
        for (int f = 0; f < kFirstFrames; ++f) {
            enginetest::frames(e, 1);
            ++frames;
            const PublishedFrame::Completeness c = h.scene->published().completeness;
            termsSeen |= c.terms;
            if (c.incomplete) ++incompleteSeen;
        }
        const AsyncShaderStats first = e->published().shaders;
        std::printf("   the first %d frames: %d published incomplete (terms seen %u); permutations failed %llu, pending skips "
                    "%llu, placeholder draws %llu\n", kFirstFrames, incompleteSeen, termsSeen,
                    (unsigned long long)first.failed, first.pendingSkips, first.placeholderDraws);
        check(first.failed > 0u, "the objects' own permutations FAIL (AsyncShaderStats::failed)");
        std::snprintf(w, sizeof(w), "(1) a failed object waiting for its placeholder is a PENDING SKIP: the frame is incomplete "
                      "(terms seen %u, wanted bit 2; %llu pending skips)", termsSeen, first.pendingSkips);
        check((termsSeen & 2u) != 0u && first.pendingSkips > 0ull, w);
        // THE READERS READY (as .pso: the ray builds and the placeholders waited for on their published state)
        e->prepareRayPipelines();
        e->waitForAsyncShaders();
        const int rest = toRest(e, h.scene, frames);
        printCards("FW at rest", h.scene->published().gi, inFlight());
        std::snprintf(w, sizeof(w), "(2) the wait ends: the scene comes to rest on its placeholders (%d frames, the cap %d)",
                      rest, kRestCap);
        check(rest >= 0, w);
        checkComplete("FW", h.scene);
    } else {
        // ---- the failure rows: a failure is final - the scene comes to rest on the picture it can draw ---------------
        Look look, control;
        if (mode == "psofail") {
            e->setArm("shaders.asyncFail", 1.0);
            control.placeholder = true;
        } else if (mode == "buildfail") {
            e->setArm("rays.buildHold", kHoldForever);
            e->setArm("rays.buildHoldIds", kReaderBuilds);
            e->setArm("rays.buildFail", kReaderBuilds);
        } else {
            look.floor = Floor::Corrupt;
        }
        Room h = buildRoom(e, "build_late_failed", look);
        if (!h.scene) { std::printf("FAIL: the room: %s\n", e->lastError().c_str()); return 1; }
        if (mode == "buildfail") {
            int builtAt = -1;
            for (int f = 0; f < kRestCap; ++f) {
                enginetest::frames(e, 1);
                ++frames;
                if (builtAt < 0 && h.scene->published().gi.cards.built) builtAt = frames;
                if (builtAt >= 0 && frames - builtAt >= kHeldPastBuild) break;
            }
            e->setArm("rays.buildHold", 0.0);   // THE LANDING, into the forced failure
        }
        const int rest = toRest(e, h.scene, frames);
        const GiStatus gh = h.scene->published().gi;
        const AsyncShaderStats as = e->published().shaders;
        printCards("F at rest", gh, inFlight());
        std::printf("   at rest after %d frames; permutations failed %llu, placeholder draws %llu, pending skips %llu\n", rest,
                    (unsigned long long)as.failed, as.placeholderDraws, as.pendingSkips);
        std::snprintf(w, sizeof(w), "(1) a failure is final: the scene comes to rest (%d frames, the cap %d)", rest, kRestCap);
        check(rest >= 0, w);
        checkComplete("F", h.scene);
        if (mode == "psofail") check(as.failed > 0u, "the forced failures are counted (AsyncShaderStats::failed)");
        if (mode == "buildfail") {
            std::snprintf(w, sizeof(w), "(2) no reader runs: no card is admitted (%u resident)", gh.cards.cardsResident);
            check(gh.cards.cardsResident == 0u, w);
        } else {
            drop(e, h);
            e->setArm("shaders.asyncFail", 0.0);
            Room c = buildRoom(e, "build_late_control", control);
            if (!c.scene) { std::printf("FAIL: the control room: %s\n", e->lastError().c_str()); return 1; }
            const int restC = toRest(e, c.scene, frames);
            const GiStatus gc = c.scene->published().gi;
            printCards("C at rest", gc, inFlight());
            std::printf("   at rest after %d frames\n", restC);
            std::snprintf(w, sizeof(w), "(2) at rest the cards of the picture it can draw: the control's (F %u cards, hash "
                          "%016llx; C %u cards, hash %016llx)", gh.cards.cardsResident, gh.cards.residentHash,
                          gc.cards.cardsResident, gc.cards.residentHash);
            check(restC >= 0 && gc.cards.cardsResident > 0u && gh.cards.residentHash == gc.cards.residentHash, w);
        }
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
