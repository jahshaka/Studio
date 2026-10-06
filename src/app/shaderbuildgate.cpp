#include "bridge/sceneworkerthreads.h"
#include "app/shaderbuildgate.h"

#include "app/versionsplashscreen.h"
#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "jah_provenance.h"   // GIT_COMMIT_HASH
#include "services/defaultfloormaterial.h"
#include "services/worldmodes.h"
#include "services/testtier.h"
#include "bridge/secondarysurfacetonemap.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "data/guidmanager.h"
#include "data/materialpreset.h"
#include "io/builtinmaterials.h"
#include "io/materialpresets.h"
#include "services/scenenodehelper.h"
#include "services/scenetemplate.h"
#include "services/scenetemplatebuilder.h"
#include "viewport/gizmooverlay.h"
#include "viewport/snapsettings.h"
#include "viewport/translationgizmo.h"

#include <functional>

#include <QApplication>
#include <QElapsedTimer>

using namespace jahshaka::engine;

namespace {

/// The World Mode a new scene of this process is born with: Epic (the product), or the
/// process's test tier (TEST-TIER-1) — the tier the warm-up must compile for.
worldmodes::Mode bornMode()
{
    return testtier::active() ? worldmodes::modeFromName(testtier::name()) : worldmodes::Mode::Epic;
}

/// THE SETTLE, IN FRAMES (SPEED-CPU, perf audit CS-2; ENGINE trap 7 — the engine
/// has no wall clock). Every compile this gate can cause runs INSIDE a frame: the
/// parallel Hlms queue is joined before renderOneFrame returns (fork
/// OgreRenderQueue.cpp, stopAndWait), HlmsDiskCache::applyTo joins its threads, and
/// a compute job compiles synchronously on its dispatch. So the build is over the
/// first time a whole frame compiles nothing — and a frame cannot pass that test
/// while anything is still being built. This used to be a 250 ms wall-clock wait
/// for the count to stand still, paid on every launch, warm or cold (measured: the
/// whole warm gate was 251-252 ms of which 250 were the wait). The bound below is a
/// hang guard for a pathological driver, not a settle.
constexpr int kSettleFrameCap = 64;

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

/// Frames the warm worlds may take: the arm binds and the compiles stop well inside it
/// (measured cold: the Basic world settles in ~60 frames).
constexpr int kWarmWorldFrameCap = 240;
/// ...and "stopped" means this many frames in a row compiled nothing (ENGINE trap 7:
/// frames, never a wall-clock wait).
constexpr int kWarmWorldQuietFrames = 30;

/// One shipped preset per sphere, in rows in front of the editor's default camera (a
/// sphere the camera cannot see is culled and compiles nothing).
void addPresetRow(const iris::ScenePtr &doc, Database *db)
{
    const QVector<MaterialPreset> &presets = MaterialPresets::all();
    for (int i = 0; i < presets.size(); ++i) {
        iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
            QStringLiteral(":/content/primitives/sphere.obj"), presets[i].name,
            GUIDManager::generateGUID(), db);
        if (!node) continue;
        node->setLocalPos(iris::Vec3(-4.0f + 2.0f * float(i % 5), 1.0f, 2.0f - 2.0f * float(i / 5)));
        node->setLocalScale(iris::Vec3(0.8f, 0.8f, 0.8f));
        node->setMaterial(BuiltinMaterials::fromPreset(presets[i]));
        doc->rootNode->addChild(node);
    }
}

/// THE EDITOR'S WORLD, AS THE EDITOR DRAWS IT (SHADER-WARM-2; see the call site). The
/// template's own document, a mirror configured as EngineSceneViewport configures its
/// own, the editor's helpers at their defaults (plus the grid and a selected node with
/// its gizmo, so their shaders are in the set too), the editor's camera pose, and the
/// world's environment and its whole post chain (the offscreen opt-in). Born LOADING like
/// a created world (the parked arm's permutations), then released so the arm builds (its
/// compute set and the lit permutations), then graded like the project tile. Returns the
/// frames rendered.
int warmEditorWorld(Engine &engine, View *view, const EngineHost::WarmUpShape &shape,
                    Database *db, SceneTemplate kind, bool presets,
                    const std::function<void()> &poll)
{
    iris::ScenePtr doc = scenetemplate::build(kind, db, nullptr);
    if (!doc || !view) return 0;
    worldmodes::setMode(doc, bornMode());
    if (presets) addPresetRow(doc, db);
    // The node the editor would have selected: the first mesh (the floor).
    iris::SceneNodePtr selected;
    for (const iris::SceneNodePtr &child : doc->rootNode->children())
        if (child && child->getSceneNodeType() == iris::SceneNodeType::Mesh) { selected = child; break; }
    iris::CameraNodePtr camera = iris::CameraNode::create();
    camera->setLocalPos(iris::Vec3(0, 5, 14));      // EngineSceneViewport's explorer pose
    camera->lookAt(iris::Vec3(0, 0, 0));
    camera->setAspectRatio(16.0f / 9.0f);

    Scene *es = engine.createScene(kind == SceneTemplate::Basic ? "startup-warmup-basic"
                                                                : "startup-warmup-world",
                                   sceneworkers::count(sceneworkers::Tier::Primary));
    if (!es) return 0;
    es->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));   // the editor's
    int frames = 0;
    {
        SceneMirror mirror(es);
        mirror.setGroundPlaneMaterial(defaultfloormaterial::createUnpinned());
        GizmoOverlay overlay(es);
        TranslationGizmo gizmo;
        if (selected) gizmo.setSelectedNode(selected);
        es->setLoading(true);
        mirror.setSource(doc);
        view->setScene(es);
        view->setShadows(shape.shadows);
        if (shape.samples > 1) view->setSampleCount(shape.samples);
        view->setVrHelpersVisible(true);
        mirror.invalidateEnvironment();
        // pushEditorHelpers at the editor's defaults, and the grid on (a View
        // Options toggle away, so its shader belongs to the set).
        mirror.setHideDefaultFloor(false);
        mirror.setGroundPlane(false);
        mirror.setLightWires(true);
        mirror.setCameraBodies(true);
        mirror.setHighlightWireframe(false);
        mirror.setHighlightedNodes(selected ? QList<iris::SceneNodePtr>{ selected }
                                            : QList<iris::SceneNodePtr>{},
                                   selected);
        mirror.setGrid(true, SnapSettings::translateSize(), SceneMirror::GridPlane::Floor);
        auto frame = [&]() {
            mirror.sync();
            gizmo.updateSize(camera);
            overlay.update(selected ? &gizmo : nullptr, camera->getGlobalPosition(),
                           iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
            mirror.applySky(view);
            mirror.applyCamera(camera, view);
            engine.renderOneFrame();
            ++frames;
            poll();
        };
        // THE WORLD'S ENVIRONMENT ONCE, AND ITS WHOLE CHAIN ON THIS OFFSCREEN VIEW.
        // An offscreen view ignores the post chain unless the description opts in
        // (PostFxDesc::allowOffscreen) — measured: the editor's first frames then
        // compiled SMAA's three passes and the PBS permutations under them. The
        // screenshot's Scene grade is that opt-in (secondaryfx::applyScene), and it
        // is pushed once: the mirror re-pushes its own description on every
        // applyEnvironment, and the two flags alternating would rebuild the
        // workspace every frame.
        mirror.sync();
        mirror.applyEnvironment(view, &engine);
        secondaryfx::applyScene(view, 0.0f);
        // (1) arriving: the parked arm, the way a create's first frames draw it.
        for (int i = 0; i < kWarmUpFrames; ++i) frame();
        // (2) on screen: the arm builds; until it is BOUND and nothing has compiled
        // for kWarmWorldQuietFrames frames, or the cap. Bound, not at rest: the
        // field's convergence after the bind (~200 frames) compiles nothing.
        es->setLoading(false);
        unsigned c = 0, f = 0, e = 0;
        engine.shaderBuildProgress(c, f, e);
        unsigned last = c;
        int quiet = 0;
        while (frames < kWarmWorldFrameCap && quiet < kWarmWorldQuietFrames) {
            frame();
            engine.shaderBuildProgress(c, f, e);
            const GiStatus gi = es->giStatus();
            const bool armed = gi.mode == GiMode::Off || gi.vctBound;
            quiet = (c == last && armed) ? quiet + 1 : 0;
            last = c;
        }
        // (3) the project tile's capture (CLOSE-SHOT-2: a save's tile is the view's
        // own frame drawn a second time without the furniture, View::
        // requestFrameCapture) — its chain instance has no shadow node and the
        // furniture baked out, and its first frame compiled HlmsAtom permutations
        // on the UI thread (measured: 5 on Showroom 2, 10 with 16 lamps, on the
        // close that took the session's first tile). Taken here, once, on this
        // view's whole chain.
        if (view->requestFrameCapture(false)) {
            frame();
            jahshaka::engine::Image tile;
            view->takeFrameCapture(tile, true);
        }
        view->setScene(nullptr);
        mirror.setSource(nullptr);
    }
    engine.destroyScene(es);
    return frames;
}

}  // namespace

unsigned holdSplashForShaderBuild(QApplication &app, VersionSplashScreen &splash, Database *db)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return 0;   // headless: no engine, no shaders, no wait

    unsigned compiled = 0, cached = 0, expected = 0;
    engine->shaderBuildProgress(compiled, cached, expected);
    const unsigned entryTotal = compiled + cached;

    QElapsedTimer total;      total.start();
    unsigned last = entryTotal;
    bool shown = false;

    auto poll = [&]() {
        engine->shaderBuildProgress(compiled, cached, expected);
        const unsigned now = compiled + cached;
        if (now == last) return;
        last = now;
        splash.showShaderBuild(int(now), int(expected));
        shown = true;
        splash.repaint();
        app.processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    };
    // THE BAR MOVES INSIDE A FRAME TOO (SHADER-WARM-2). A frame that builds the
    // warm scene's lighting arm compiles thirty-odd compute permutations in ONE
    // renderOneFrame — measured 1,667 ms on the owner's cold launch with the bar
    // standing still. The engine calls this after each compile it makes on this
    // thread; it repaints the splash and nothing else (no event processing: we
    // are inside the frame), at most every 33 ms.
    QElapsedTimer sincePaint; sincePaint.start();
    engine->setCompileObserver([&]() {
        if (sincePaint.elapsed() < 33) return;
        sincePaint.restart();
        unsigned c = 0, f = 0, e = 0;
        engine->shaderBuildProgress(c, f, e);
        splash.showShaderBuild(int(c + f), int(e));
        shown = true;
        // QWidget's repaint, NOT QSplashScreen's: the splash's own repaint()
        // calls QCoreApplication::processEvents() (qsplashscreen.cpp), and an
        // event pass from inside a compile runs the render loop's timer — a
        // frame inside a frame, which deadlocked on Ogre's log mutex (measured,
        // the first cut of this lane).
        splash.QWidget::repaint();
    });
    // ...cleared on every way out: it captures this function's locals.
    struct ClearObserver {
        Engine *engine = nullptr;
        ~ClearObserver() { if (engine) engine->setCompileObserver({}); }
    } clearObserver{ engine.get() };

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
    // cold start and 19 in the second, all on one core.
    // Worthless without mode 2 — P1 — and free the moment it lands, because the
    // per-scene warm-up on the editor scene (viewport/enginesceneviewport.cpp)
    // already runs at Tier::Primary and always satisfied the predicate.
    GiParams warmGi;            // the GI the warm scene was given (the bound frames below)
    bool haveWarmGi = false;
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
            // ...the tier a new scene of THIS PROCESS is born with: Epic, or the process's
            // test tier (TEST-TIER-1, services/testtier.h — a Low test process must not build
            // the Epic chain here either: measured, this warm-up was a 1.28 GB transient in a
            // process whose scenes then held 0.3 GB), resolved through the World Mode registry.
            const iris::ScenePtr born = iris::Scene::create();
            worldmodes::setMode(born, bornMode());
            const worldmodes::PhotonTier tier = worldmodes::photonTier(born);
            GiParams gi;
            gi.mode = worldmodes::photonEnabled(born)
                          ? GiMode(qBound(0, worldmodes::photonTechnique(tier), 2)) : GiMode::Off;
            gi.quality = GiQuality(qBound(0, worldmodes::photonQuality(tier), 3));
            gi.numBounces = worldmodes::photonBounces(tier);
            gi.ddgi = worldmodes::photonDdgi(tier) ? GiToggle::On : GiToggle::Off;
            warmScene->setGlobalIllumination(gi);
            warmGi = gi;
            haveWarmGi = true;
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
    // THE GLOBAL PASS RUNS WHEN ITS KEY MOVED, not only when Ogre's cache was cold
    // (the merge read): the cache key never names the Studio build, so a Studio-only
    // change to a preset, a template or a helper shader left Ogre's cache warm and the
    // pass skipped — and the change compiled LIVE. The key of the last COMPLETED pass is
    // the Studio commit plus the cache fingerprint; a cold cache runs it as well.
    SettingsManager *settings = SettingsManager::getDefaultManager();
    const QString passKey = QStringLiteral(GIT_COMMIT_HASH) + QLatin1Char('|') +
                            QString::fromStdString(engine->shaderCacheStats().fingerprint);
    const bool keyMoved = settings->get(settingkeys::shaderWarmPass) != passKey;
    const bool runPass = compiled > 0 || keyMoved;
    if (keyMoved && compiled == 0)
        qInfo("startup shader build: the global pass's key moved (a new Studio build) — running it "
              "over a warm cache");
    unsigned lastCompiled = compiled;
    int quiet = 0;
    // ...AND UNTIL THE LIGHTING ARM IS BOUND (TEST-TIER-1): the ungathered frames below draw the
    // box WITH the cone tracer, which means nothing before the arm is bound; "quiet" alone does not
    // promise that, so the loop also runs until the arm has been bound for kWarmUpFrames frames
    // (the log line says so when it never was).
    auto armBound = [&]() {
        if (!haveWarmGi || warmGi.mode == GiMode::Off) return true;
        // Every GI mode the tiers ship voxelises (VCT, alone or under the probes): the arm is
        // bound when the cone tracer is.
        return warmScene->giStatus().vctBound;
    };
    int boundFrames = 0;
    for (; runPass && warmScene && giFrames < kWarmUpGiFrames &&
           ((quiet < kGiQuietFrames && !warmScene->giStatus().giAtRest) || boundFrames < kWarmUpFrames);
         ++giFrames) {
        engine->renderOneFrame();
        poll();
        engine->shaderBuildProgress(compiled, cached, expected);
        quiet = compiled == lastCompiled ? quiet + 1 : 0;
        lastCompiled = compiled;
        if (armBound()) ++boundFrames;
    }
    // ...AND THE STATE A WARM CACHE DRAWS FIRST (the diagnosis: spikes/test-tier-1/coldgate/). On a
    // warm cache the arm binds inside the first frames — before the visibility buffer has taken
    // the box over and before the screen-probe gather is on — so the box is drawn once through
    // STOCK PBS with the cone tracer and the probes' cube and no prepass. A cold boot, whose arm
    // binds only after its compute set compiled, has handed the box to Atom by then and never
    // draws that permutation; the second boot after every cache rebuild compiled it and ran this
    // whole warm-up again. Drawn here on the cold boot: the id pass off, the gather parked.
    if (runPass && warmScene && haveWarmGi && warmGi.mode != GiMode::Off) {
        GiParams ungathered = warmGi;
        ungathered.gather = GiToggle::Off;
        warmScene->setAtomDrawEnabled(false);
        warmScene->setGlobalIllumination(ungathered);
        for (int i = 0; i < kWarmUpFrames; ++i) {
            engine->renderOneFrame();
            poll();
        }
        warmScene->setAtomDrawEnabled(true);
        warmScene->setGlobalIllumination(warmGi);
    }
    qInfo("startup shader build: the GI compute set's warm-up took %d frames, %lld ms%s%s", giFrames,
          static_cast<long long>(giTimer.elapsed()), runPass ? "" : " (a warm cache: skipped — the global pass's key is the recorded one)",
          runPass && boundFrames == 0 ? " (the lighting arm never bound)" : "");

    // THE EDITOR'S WORLDS, THROUGH THE EDITOR'S OWN MIRROR (SHADER-WARM-2; was ATOM
    // S3-DRAW's hand-built floor). The owner's shape: the startup gate compiles the
    // GLOBAL set — the engine's passes, the editor's own (the outline, the light
    // wires, the camera bodies, the grid, the gizmo) and the DEFAULT MATERIALS (the
    // templates' floors, the shipped presets) — so the Desktop, a new Basic or World
    // project and a preset dropped on a node compile nothing after the splash.
    //
    // MEASURED, and why the hand-built floor had to go: on a cold launch it left 19
    // compiles to the editor's first frames (the PBS permutations with the world's
    // HEIGHT FOG and atmosphere buffer — jah_height_fog, hlms_fog, jah_atmo_buf —
    // which a hand-set sky never pushes; the unlit light wires; the GI card light
    // job and its capture pass) and 15 more to a Basic create. The remedy is not a
    // closer imitation: it is the real document (scenetemplate::build, the verb's
    // own builder, with no project, so it writes nothing) pushed by a SceneMirror
    // configured as the editor configures its own (EngineSceneViewport::
    // ensureEngineScene / pushEditorHelpers), under the world's own environment
    // (applyEnvironment — the same call the viewport makes every frame).
    if (runPass && warmView && db) {
        warmView->setScene(nullptr);
        if (warmScene) engine->destroyScene(warmScene);
        warmScene = nullptr;
        const SceneTemplate kinds[] = { SceneTemplate::Basic, SceneTemplate::World };
        for (const SceneTemplate kind : kinds) {
            engine->shaderBuildProgress(compiled, cached, expected);
            const unsigned before = compiled;
            QElapsedTimer worldTimer; worldTimer.start();
            const int frames = warmEditorWorld(*engine, warmView, shape, db, kind,
                                               /*presets*/ kind == SceneTemplate::Basic, poll);
            engine->shaderBuildProgress(compiled, cached, expected);
            qInfo("startup shader build: the %s template's world took %d frames, %lld ms and "
                  "compiled %u shader(s)",
                  kind == SceneTemplate::Basic ? "Basic (and the presets')" : "World", frames,
                  static_cast<long long>(worldTimer.elapsed()), compiled - before);
        }
        // Recorded only once the pass has COMPLETED: a launch killed half way runs it again.
        settings->set(settingkeys::shaderWarmPass, passKey);
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

    // ---- Settle: until one whole frame compiles nothing -----------------------
    // The splash gets its events between frames; the count is read after each.
    for (int frame = 0; frame < kSettleFrameCap; ++frame) {
        app.processEvents(QEventLoop::AllEvents);
        engine->shaderBuildProgress(compiled, cached, expected);
        const unsigned before = compiled + cached;
        engine->renderOneFrame();
        poll();
        if (last == before) break;
        if (total.elapsed() > kDeadlineMs) {
            qWarning("shader build gate: still compiling after %d ms (%u shaders) — "
                     "showing the window anyway", kDeadlineMs, last);
            break;
        }
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
