#include "bridge/sceneworkerthreads.h"
#include "app/shaderbuildgate.h"

#include "app/versionsplashscreen.h"
#include "bridge/enginehost.h"
#include "services/defaultfloor.h"
#include "services/worldmodes.h"
#include "bridge/secondarysurfacetonemap.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/mirror/scenemirror.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>

using namespace jahshaka::engine;

namespace {

/// How long the compile count has to stand still before we call the burst over.
/// Measured shape of a cold startup on this box: 47 shaders in the first
/// second, 19 in the second, then nothing. The gaps INSIDE a burst are tens of
/// milliseconds, so 250 ms is comfortably outside them — and every millisecond
/// here is paid on every launch, warm or cold, so it is not free.
constexpr int kSettleMs = 250;

/// Hard ceiling. A gate that can hang the launch forever is worse than a launch
/// that shows the window with a few shaders still to build: if we are still
/// compiling after this long, something is wrong (a pathological driver, a
/// machine under extreme load) and the user gets their app.
constexpr int kDeadlineMs = 30000;

/// The warm-up view. Small on purpose: nothing about which SHADERS get built
/// depends on the resolution, and a 32x32 render target costs nothing.
constexpr unsigned kWarmUpSize = 32;

/// Frames to render through the warm-up view. One is enough to force the
/// compositor chain to execute; a few more cost microseconds and cover any pass
/// that only runs on a later frame.
constexpr int kWarmUpFrames = 4;

/// THE GI COMPUTE SET'S FRAMES (PHOTON-VOXEL-5): the warm scene's GI must voxelise, inject,
/// bounce, build the directional mips, integrate the field and light the cards before the
/// window shows - each compute job compiles on its first DISPATCH and nowhere else. Bounded:
/// the frames stop when the GI reports it is at rest, or at this many.
constexpr int kWarmUpGiFrames = 240;

/// One closed box (24 vertices, the four corners of each face with its own normal), for the
/// warm scene's GI to have something to voxelise.
MeshData warmUpBox()
{
    MeshData md;
    static const float n[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int f = 0; f < 6; ++f) {
        // two axes across the face, oriented so (u x v) = n: counter-clockwise from outside
        const int a = f / 2, u = (a + 1) % 3, v = (a + 2) % 3;
        const float sg = n[f][a];
        const unsigned base = unsigned(md.positions.size() / 3);
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto &c : corners) {
            float p[3];
            p[a] = 0.5f * sg; p[u] = 0.5f * c[0]; p[v] = 0.5f * c[1] * sg;
            md.positions.insert(md.positions.end(), { p[0], p[1], p[2] });
            md.normals.insert(md.normals.end(), { n[f][0], n[f][1], n[f][2] });
            md.uvs.insert(md.uvs.end(), { 0.5f + 0.5f * c[0], 0.5f + 0.5f * c[1] });
        }
        md.indices.insert(md.indices.end(), { base, base + 1u, base + 2u, base, base + 2u, base + 3u });
    }
    return md;
}

/// One more of the warm box, in `scene` (a mesh per call: the floor stands in twice).
MeshId boxMeshFor(Scene *scene) { return scene ? scene->createMesh(warmUpBox()) : MeshId(0); }

}  // namespace

unsigned holdSplashForShaderBuild(QApplication &app, VersionSplashScreen &splash)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return 0;   // headless: no engine, no shaders, no wait

    unsigned compiled = 0, cached = 0, expected = 0;
    engine->shaderBuildProgress(compiled, cached, expected);
    const unsigned entryTotal = compiled + cached;

    QElapsedTimer total;      total.start();
    QElapsedTimer sinceMove;  sinceMove.start();
    unsigned last = entryTotal;
    bool shown = false;

    auto poll = [&]() {
        engine->shaderBuildProgress(compiled, cached, expected);
        const unsigned now = compiled + cached;
        if (now == last) return;
        last = now;
        sinceMove.restart();
        splash.showShaderBuild(int(now), int(expected));
        shown = true;
        splash.repaint();
        app.processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    };

    // ---- Drive the build ---------------------------------------------------
    // MEASURED, and the reason this function is not just a wait loop: at this
    // point in startup NOTHING has compiled — not one shader of the ~66 a
    // session needs. MainWindow's constructor creates the viewport WIDGETS, but
    // EngineViewWidget only creates and enables its engine View on its first
    // showEvent (engineviewwidget.cpp:87), and the Hlms is not even registered
    // until the first View exists. So a hidden window renders nothing, compiles
    // nothing, and the whole burst would land on the first frames after the
    // window appears — exactly where the owner does not want it.
    //
    // So the gate makes the work happen itself: a tiny offscreen View and an
    // empty Scene, a handful of frames, then both destroyed. That is enough to
    // reach the state that actually compiles — Hlms registration, the low-level
    // material scripts, the SSAO/HDR/SMAA helper materials and the compositor
    // chain — using nothing but the public engine API.
    //
    // The shaders survive their view: the Hlms shader cache and the microcode
    // map are process-wide, so the editor's real views reuse everything built
    // here (and everything loaded from disk before it).
    // THE WARM VIEW MUST HAVE THE EDITOR'S PASS SHAPE (audit F1b).
    //
    // This used to be a 32x32 view over an EMPTY scene with no lights, no
    // shadows and 1x MSAA, and the recorded set was replayed into it. That is
    // the wrong world twice over: Hlms permutations are a function of the PASS
    // as much as of the renderable — shadows change the shader, and the sample
    // count is literally a shader property (HlmsBaseProp::MsaaSamples, set from
    // the target's sample description at OgreHlms.cpp:3771, consumed at :2919).
    // So the replay compiled zero-light / no-shadow / 1x variants the editor
    // never draws, cached them, and counted them into expectedShaders — the
    // splash denominator — as shaders somebody wanted.
    //
    // EngineHost::warmUpShape() is what the last session's editor view actually
    // used (recorded on the open path beside the warm-up set itself), so this
    // is measured rather than guessed. A first-ever launch gets the defaults
    // and is no worse off than the old code.
    const EngineHost::WarmUpShape shape = EngineHost::warmUpShape();
    View *warmView = engine->createOffscreenView("startup-warmup", kWarmUpSize, kWarmUpSize,
                                                 Colour(0.0f, 0.0f, 0.0f, 1.0f));
    // LIVE (View::setOffscreenContract): its frames are never read as a picture
    // (its scene's GI below is there to compile the compute set, not to be seen).
    if (warmView) warmView->setOffscreenContract(OffscreenContract::Live);
    // PRIMARY, NOT UTILITY, AND THIS IS THE WHOLE OF PHASE P4(a)
    // (SPECS/THREADING_ADOPTION_SPEC.md §3.4a, interaction I-2).
    //
    // Every other one-frame-into-a-texture scene in the app has NO worker pool
    // at all (Tier::MainThread, P5) because the barrier is pure overhead at
    // 32x32 — and that is right for a thumbnail, whose job is to CULL and DRAW
    // a handful of objects. It is exactly wrong for THIS scene, whose job is to
    // COMPILE, because Ogre forks
    // shader compilation across the same per-scene worker pool: the parallel
    // warm-up path is gated on getNumWorkerThreads() > 1
    // (OgreRenderQueue.cpp:588), and getNumWorkerThreads() returns max(n, 1)
    // (OgreSceneManager.cpp:170), so 1 AND 0 both compile serially. A "utility"
    // scene that wants threads is a contradiction in the tier's own terms, and
    // the tier list in bridge/sceneworkerthreads.h says so at the Utility entry.
    //
    // The measured shape this is aimed at: 47 shaders in the first second of a
    // cold start and 19 in the second (kSettleMs above), all on one core.
    // Worthless without mode 2 — P1 — and free the moment it lands, because the
    // per-scene warm-up on the editor scene (viewport/enginesceneviewport.cpp)
    // already runs at Tier::Primary and always satisfied the predicate.
    Scene *warmScene = warmView ? engine->createScene(
                                      "startup-warmup",
                                      sceneworkers::count(sceneworkers::Tier::Primary))
                                : nullptr;
    if (warmScene) {
        warmView->setScene(warmScene);
        warmView->setShadows(shape.shadows);
        if (shape.samples > 1) warmView->setSampleCount(shape.samples);
        // A REPRESENTATIVE LIGHT RIG, not a lit scene: one shadow-casting
        // directional plus ambient is the smallest thing that makes the pass
        // hash look like the editor's (it is also exactly what
        // tests/shadercache/test_warm_up.cpp builds for the same reason). No
        // geometry — the recorded set brings its own degenerate renderables,
        // and anything else here would compile permutations nothing draws,
        // which is the defect being fixed.
        warmScene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
        const NodeId sun = warmScene->createNode();
        if (sun) {
            // A light points down its node's -Y (the engine's convention), and
            // the identity orientation already aims it straight down — which is
            // all this needs. No transform, no rotation maths.
            LightDesc l;
            l.type = LightType::Directional;
            l.colour = Colour(1.0f, 1.0f, 1.0f);
            l.intensity = 1.0f;
            l.castShadows = shape.shadows;
            warmScene->setLight(sun, l);
        }
        // THE GI COMPUTE SET (PHOTON-VOXEL-5; owner smoke from a wiped data root = a cold cache):
        // a box and the tier a new scene is born with (Epic, the Photon table), so the voxeliser,
        // the light injection, the bounce, step 0/1, the field and the card light job DISPATCH
        // here, behind the splash - a compute job compiles on its first dispatch and nowhere else
        // (HlmsCompute::dispatch), outside the render-queue warm-up. The world's own permutations
        // differ where its GI does (another tier, a sky cube); this is the default world's set.
        const NodeId box = warmScene->createNode();
        const MeshId boxMesh = box ? warmScene->createMesh(warmUpBox()) : MeshId(0);
        PbrParams boxMat;
        boxMat.albedo = Colour(0.8f, 0.8f, 0.8f);
        const MaterialId boxMatId = boxMesh ? warmScene->createPbrMaterial(boxMat) : MaterialId(0);
        // ...TEXTURED (the voxeliser's texture-pool permutation - every world's ground has a map)
        // under the ANALYTIC SKY (the bounce, the field and the cards read its environment cube:
        // their `jah_env` permutation) - the default world's shape.
        if (boxMatId) {
            const unsigned char grey[4 * 4 * 4] = {
                200, 200, 200, 255, 180, 180, 180, 255, 200, 200, 200, 255, 180, 180, 180, 255,
                180, 180, 180, 255, 200, 200, 200, 255, 180, 180, 180, 255, 200, 200, 200, 255,
                200, 200, 200, 255, 180, 180, 180, 255, 200, 200, 200, 255, 180, 180, 180, 255,
                180, 180, 180, 255, 200, 200, 200, 255, 180, 180, 180, 255, 200, 200, 200, 255 };
            const TextureId tex = warmScene->createTexture(4u, 4u, grey, true, true);
            if (tex) warmScene->setPbrTexture(boxMatId, PbrTextureSlot::Albedo, tex);
        }
        {
            SkyDesc sky;
            sky.mode = SkyMode::Atmosphere;
            warmScene->setSky(sky);
        }
        if (box && boxMesh && boxMatId && warmScene->attachMesh(box, boxMesh, boxMatId)) {
            const worldmodes::PhotonTier tier = worldmodes::PhotonTier::Epic;
            GiParams gi;
            gi.mode = GiMode(qBound(0, worldmodes::photonTechnique(tier), 2));
            gi.quality = GiQuality(qBound(0, worldmodes::photonQuality(tier), 2));
            gi.epicTier = tier == worldmodes::PhotonTier::Epic;
            gi.numBounces = worldmodes::photonBounces(tier);
            gi.cascades = worldmodes::photonCascades(tier) > 0;
            gi.ddgi = worldmodes::photonDdgi(tier) ? GiToggle::On : GiToggle::Off;
            warmScene->setGlobalIllumination(gi);
        }
    }
    // What this covers is everything PROCESS-WIDE: Hlms registration, all the
    // low-level material scripts (sky, DPSM, depth utils, copy/resolve, ESM,
    // HDR, SSAO, SMAA) and the compositor chain. What it cannot cover on its
    // own is the Hlms permutations — the Hlms generates a shader per RENDERABLE
    // and the permutation set is a property of the CONTENT. Those are compiled
    // on the open path instead, behind the loading cover (View::warmUpShaders).
    for (int i = 0; i < kWarmUpFrames && warmView; ++i) {
        engine->renderOneFrame();
        poll();
    }
    // ...and the GI's frames, ONLY ON A COLD CACHE (the frames above compiled something: a warm
    // cache has every compute PSO already and would pay the GI's settle, ~1.1 s, on every
    // launch for nothing), until no shader has compiled for kGiQuietFrames frames - the compute
    // set dispatches in the GI's first frames; its settle after that compiles nothing - or the
    // GI is at rest, or the bound.
    constexpr int kGiQuietFrames = 30;
    int giFrames = 0;
    QElapsedTimer giTimer; giTimer.start();
    engine->shaderBuildProgress(compiled, cached, expected);
    const bool coldCache = compiled > 0;
    unsigned lastCompiled = compiled;
    int quiet = 0;
    for (; coldCache && warmScene && giFrames < kWarmUpGiFrames && quiet < kGiQuietFrames &&
           !warmScene->giStatus().giAtRest; ++giFrames) {
        engine->renderOneFrame();
        poll();
        engine->shaderBuildProgress(compiled, cached, expected);
        quiet = compiled == lastCompiled ? quiet + 1 : 0;
        lastCompiled = compiled;
    }
    qInfo("startup shader build: the GI compute set's warm-up took %d frames, %lld ms%s", giFrames,
          static_cast<long long>(giTimer.elapsed()), coldCache ? "" : " (a warm cache: skipped)");

    // THE DEFAULT WORLD'S OWN MATERIAL, IN THE PASSES A NEW PROJECT DRAWS IT IN (ATOM
    // S3-DRAW). The visibility buffer shades the ground through a DECODE TWIN of its
    // material — a permutation of its own per pass shape — and the first place those
    // passes run is `project.create`: the initial thumbnail (the Tonemap grade, GI
    // parked off) and the viewport's first frames (the world's whole chain, GI not yet
    // armed). Left to them, the twins compile inside the create (measured +500 ms,
    // threading.newproject_stall). So the floor's REAL material (defaultfloor's factory,
    // through the mirror's own conversion) stands in the warm scene, with its backdrop
    // twin (the horizon plane: the same datablock through stock PBS), under the Epic
    // world's chain with GI off, then under the thumbnail's grade. What it cannot reach
    // is anything the document adds later (a user's material, another tier).
    if (coldCache && warmScene && warmView) {
        engine->shaderBuildProgress(compiled, cached, expected);
        const unsigned before = compiled;
        QElapsedTimer worldTimer; worldTimer.start();
        // A SCENE OF ITS OWN, NEVER GI-ARMED: a new project's scene has had no GI when
        // its thumbnail and first frames draw, and a scene whose GI was switched off
        // still binds the torn-down arm's pass state for a while (measured: the box
        // scene's floor compiled irradiance-field variants nothing in a create draws).
        warmView->setScene(nullptr);
        engine->destroyScene(warmScene);
        warmScene = engine->createScene("startup-warmup-world",
                                        sceneworkers::count(sceneworkers::Tier::Primary));
        iris::ScenePtr world = iris::Scene::create();
        worldmodes::setMode(world, worldmodes::Mode::Epic);
        PbrParams floorParams;
        const iris::PbrMaterialPtr floorMat = defaultfloor::createMaterial(nullptr, nullptr);
        if (warmScene && warmView->setScene(warmScene) && floorMat &&
            SceneMirror::toPbrParams(floorMat.data(), floorParams)) {
            warmView->setShadows(shape.shadows);
            warmScene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.12f));
            if (const NodeId sun = warmScene->createNode()) {
                LightDesc l;
                l.type = LightType::Directional;
                l.castShadows = shape.shadows;
                warmScene->setLight(sun, l);
            }
            {
                SkyDesc sky;
                sky.mode = SkyMode::Atmosphere;
                warmScene->setSky(sky);
            }
            const MaterialId fm = warmScene->createPbrMaterial(floorParams);
            const TextureId tile =
                fm ? warmScene->loadTexture(defaultfloor::shippedTilePath().toStdString(), true) : TextureId(0);
            if (tile) warmScene->setPbrTexture(fm, PbrTextureSlot::Albedo, tile);
            const NodeId ground = fm ? warmScene->createNode() : NodeId(0);
            const NodeId horizon = fm ? warmScene->createNode() : NodeId(0);
            if (ground && horizon && warmScene->attachMesh(ground, boxMeshFor(warmScene), fm) &&
                warmScene->attachMesh(horizon, boxMeshFor(warmScene), fm)) {
                warmScene->setNodeBackdrop(horizon, true);
                // The tier's planar budget and rays (both are pass properties).
                PlanarReflectionParams pr;
                pr.budget = qBound(0, world->planarReflectionBudget, 8);
                pr.resolution = 256u;
                warmScene->setPlanarReflections(pr);
                warmScene->setRayTracing(RayTracingMode::Auto);
                // NO VOXELS, NO FIELD, AND THE GATHER ON — how a new project's viewport
                // draws before its deferred GI arms (the tier's gather row is live, the
                // voxel volume and the field are not built yet: the permutation compiled
                // inside the create carried jah_probe_gather and no irradiance field). The
                // gather is graph shape (a gathering view carries the prepass), so it is
                // asked for by name here: with the mode off, Auto would decline it.
                {
                    GiParams parked;
                    parked.mode = GiMode::Off;
                    parked.gather = GiToggle::On;
                    parked.epicTier = world->giTier == 3;
                    warmScene->setGlobalIllumination(parked);
                }
                // (1) the viewport: the Epic world's chain (the mirror's applyEnvironment).
                PostFxDesc fx;
                fx.allowOffscreen = true;
                fx.hdr = world->hdrEnabled;
                fx.bloom = world->bloomEnabled;
                fx.ssao = world->ssaoEnabled;
                fx.ssaoScale = world->ssaoScale;
                fx.smaaPreset = world->smaaPreset;
                fx.ssr = world->ssrMode;
                fx.ssrMarchPhase = qBound(0, world->ssrMarch, 2);
                fx.reflectionRoughnessCutoff = float(world->reflectionRoughnessCutoff) * 0.01f;
                warmView->setPostFx(fx);
                for (int i = 0; i < kWarmUpFrames; ++i) { engine->renderOneFrame(); poll(); }
                // (2) the thumbnail: the Tonemap grade.
                secondaryfx::apply(warmView, true, 0.0f);
                for (int i = 0; i < kWarmUpFrames; ++i) { engine->renderOneFrame(); poll(); }
            }
        }
        engine->shaderBuildProgress(compiled, cached, expected);
        qInfo("startup shader build: the default world's material took %lld ms and compiled %u shader(s)",
              static_cast<long long>(worldTimer.elapsed()), compiled - before);
    }

    // THE RECORDED SET IS GONE (WARMUPSET-2, 2026-09-21). A replay used to run
    // here: the previous session's permutation list, applied to degenerate
    // 4-vertex buffers so this session's Hlms permutations existed before the
    // window did. It never worked in this app and could not have — a set names
    // each permutation by one representative MATERIAL and resolves it BY NAME
    // in the next process, while this engine names datablocks from a
    // process-unique counter. Measured before the deletion: seven "Can't find
    // HLMS datablock" lines and eight shaders compiled for the DEFAULT
    // datablock on every warm launch, none of them ever bound, and eight
    // permanent entries added to the shader cache. Deleted on the owner's word
    // rather than repaired.
    //
    // What remains on this path is the PROCESS-WIDE half above, which is real:
    // Hlms registration, the low-level material scripts and the compositor
    // chain, compiled in a view shaped like the editor's (EngineHost::
    // warmUpShape — the pass shape a world OPEN writes down). The per-world
    // Hlms permutations are compiled behind the loading cover by the per-scene
    // precache instead (View::warmUpShaders).
    // The SCENE goes; the VIEW stays, disabled, for the life of the process.
    //
    // That asymmetry is not tidiness, it is a DEFECT WORKAROUND, narrowed by
    // bisection against scripting.e2e.particles:
    //
    //   destroy scene + view : a LATER editor.screenshot() reads back a
    //                          completely black image — not the clear colour,
    //                          zeroes — and the test that photographs a particle
    //                          plume sees nothing at all.
    //   destroy the view only: same failure.
    //   destroy the scene only, keep the view: correct.
    //   destroy nothing:                       correct.
    //
    // So destroying this offscreen View — at the one moment in the process when
    // it is the ONLY view, since the editor's views do not exist until the
    // window is shown — leaves engine state behind that a later offscreen
    // readback trips over. Note EngineSceneViewport::takeScreenshot creates and
    // destroys offscreen views constantly and is fine, so it is specifically
    // "the last view in the process goes away". Adding frames after the destroy
    // does not help, so it is not a pending-command flush.
    //
    // That is an engine defect and it is not this lane's to fix. The price of
    // routing around it is one disabled 32x32 view (a 4 KB render target, a
    // camera and a workspace) held for the session, which renderOneFrame skips.
    if (warmScene) engine->destroyScene(warmScene);
    if (warmView)  warmView->setEnabled(false);

    // ---- Wait for it to settle --------------------------------------------
    // Anything the warm-up kicked off asynchronously, plus whatever the render
    // driver's own frames add, has to stop moving before the window appears.
    for (;;) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
        poll();
        if (total.elapsed() > kDeadlineMs) {
            qWarning("shader build gate: still compiling after %d ms (%u shaders) — "
                     "showing the window anyway", kDeadlineMs, last);
            break;
        }
        if (sinceMove.elapsed() > kSettleMs) break;
    }

    if (shown) splash.showShaderBuild(-1, 0);
    // NOT recorded in LoadTimeline: that ledger belongs to a scene OPEN, and
    // add() is a documented no-op outside begin()/end(). The startup build's
    // numbers live in the Ogre log and, verb-side, in app.shaderCache()'s
    // compiledThisRun / loadedThisRun — which is what the e2e asserts on.
    qInfo("startup shader build: %u shaders (%u compiled, %u from cache) in %lld ms",
          last - entryTotal, compiled, cached, static_cast<long long>(total.elapsed()));
    return last;
}
