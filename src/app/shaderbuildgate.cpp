#include "bridge/sceneworkerthreads.h"
#include "app/shaderbuildgate.h"

#include "app/versionsplashscreen.h"
#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "jah_startup_pass_id.h"   // JAH_STARTUP_PASS_CODE_ID (cmake/startup_pass_id.py)
#include "app/startuppasskey.h"
#include "app/gatesettle.h"   // the settle on published state (COLD-BOOT-1 stage 4)
#include <QCoreApplication>
#include "services/defaultfloormaterial.h"
#include "irisgl/document/scenegraph/worldmodes.h"
#include "services/testtier.h"
#include "bridge/secondarysurfacetonemap.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/core/guid.h"
#include "data/materialpreset.h"
#include "io/builtinmaterials.h"
#include "io/materialpresets.h"
#include "services/scenenodehelper.h"
#include "services/scenetemplate.h"
#include "services/scenetemplatebuilder.h"
#include "viewport/gizmooverlay.h"
#include "viewport/snapsettings.h"
#include "viewport/translationgizmo.h"
#include "viewport/recordingview.h"

#include <algorithm>
#include <functional>
#include <future>
#include <optional>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cstdio>
#include "jahshaka/engine/Camera.h"
#include "jahshaka/engine/FrameInput.h"
#include "jahshaka/engine/GiModes.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/GiStatus.h"
#include "jahshaka/engine/Ids.h"
#include "jahshaka/engine/Lights.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Math.h"
#include "jahshaka/engine/Mesh.h"
#include "jahshaka/engine/Monitor.h"
#include "jahshaka/engine/Pictures.h"
#include "jahshaka/engine/PostFx.h"
#include "jahshaka/engine/Published.h"
#include "jahshaka/engine/Shaders.h"
#include "jahshaka/engine/Sky.h"

using namespace jahshaka::engine;

namespace {

/// Puts `doc` on the World a new scene of this process is born with: Epic (the product), or
/// the process's test tier with every switchable feature its JAHSHAKA_TEST_NEEDS does not name
/// off (TEST-TIER-1, TEST-NEEDS-1: worldmodes::applyTestTier, the same call MainWindow::setScene
/// makes) — the state the warm-up must compile for, so a test process neither compiles in its
/// test what the warm-up skipped nor warms what it never draws. (WORLD-MODE-1's shape.)
void applyBornWorld(const iris::ScenePtr &doc)
{
    if (testtier::active()) worldmodes::applyTestTier(doc);
    else worldmodes::setMode(doc, worldmodes::Mode::Epic);
}

/// THE SETTLE, ON PUBLISHED STATE (COLD-BOOT-1 stage 4; app/gatesettle.h). The gate's scene is ASYNCHRONOUS for the
/// whole gate: its permutations build on the background compiler and its ray pipelines on their builders (all started
/// at the gate's first frame, Engine::prepareRayPipelines), so a frame never waits for a compile and the splash's events
/// are pumped between frames - the window answers throughout. What the gate waits for is the engine's published state
/// saying the work is done (nothing pending, no dispatch deferred, no placeholder drawn): never a join on the UI thread.
/// (It used to compile INSIDE its frames, and the reflection's two cold driver compiles were one 100-s freeze of the
/// window: spikes/cold-boot-1/S1b.) Every frame is on the Streaming pace (gateFrame). The final settle's bound is a frame
/// count over SETTLED frames; every wait of the gate ends only by gatesettle::Settle::waitOver - settled, NOTHING LANDING
/// for kStallMs, or the gate's ceiling kCeilingMs - the last two with a FAIL line naming what is pending.
constexpr int kSettleFrameCap = 64;

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
/// THE RECORDER'S VIEW (VIDEO-REC-2): drawn until no shader has compiled for
/// this many frames (its chain compiles on its first frames; the NV12 job on
/// the first ARMED frame), at most kWarmRecordFrameCap.
constexpr int kWarmRecordQuietFrames = 8;
constexpr int kWarmRecordFrameCap = 48;

/// One shipped preset per sphere, in rows in front of the editor's default camera (a
/// sphere the camera cannot see is culled and compiles nothing).
void addPresetRow(const iris::ScenePtr &doc, Database *db)
{
    const QVector<MaterialPreset> &presets = MaterialPresets::all();
    for (int i = 0; i < presets.size(); ++i) {
        iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
            QStringLiteral(":/content/primitives/sphere.obj"), presets[i].name,
            QString::fromStdString(iris::newGuid()), db);
        if (!node) continue;
        node->setLocalPos(iris::Vec3(-4.0f + 2.0f * float(i % 5), 1.0f, 2.0f - 2.0f * float(i / 5)));
        node->setLocalScale(iris::Vec3(0.8f, 0.8f, 0.8f));
        node->setMaterial(BuiltinMaterials::fromPreset(presets[i]));
        doc->rootNode->addChild(node);
    }
}

/// THE GATE's FRAME (COLD-BOOT-1 stage 4): the gate's frames build up on the STREAMING pace. A frame on the
/// Complete pace owes the whole picture, so every consumer JOINS what it needs (OgreScene::compilesInBackground is false
/// under it: a ray pipeline build is waited for in the frame - the 47-s join of spikes/cold-boot-1/S4arm - and the lighting
/// arm runs whole); that is the reveal law of every view a user sees, and the editor's frames keep it (the viewport sets
/// its own pace). The gate's frames are shown to nobody but the splash: here, and only here, a compile lands frames later
/// and the gate waits for it on the published state (gatesettle.h). The pace is IN FORCE UNTIL SET AGAIN (Engine::
/// setFramePace; the engine never lowers it), so the gate gives it back with everything else it changed: GateScope.
/// A world phase ENDS on COMPLETE frames (gatesettle::Tail, completeTail below): the frames the editor will draw.
/// (source.gate_pace: nothing else sets Streaming, every gate frame is this one, and the gate's scope restores the pace.)
void gateFrame(Engine &engine, FramePace pace = FramePace::Streaming)
{
    engine.setFramePace(pace);
    engine.renderOneFrame();
}

/// THE GATE LEAVES THE ENGINE AS IT FOUND IT (COLD-BOOT-1, the leak of stage 4's gate): on every way out, the pace back
/// to Complete (the state of an engine no driver has paced - the gate runs before any viewport frame) and the kept warm
/// view back to blocking compiles (its state at creation). Left as they were, every later frame of the session - the
/// editor's, a script's, the selftest's six - rendered on the Streaming pace, its lighting arm built one stage a frame
/// (PhotonComponent: a Complete frame builds it whole), and the kept view counted as compiling in the background.
struct GateScope {
    Engine &engine;
    View *warmView = nullptr;
    ~GateScope()
    {
        engine.setFramePace(FramePace::Complete);
        if (warmView) warmView->setAsyncShaders(false);
    }
};

/// THE EDITOR'S WORLD, AS THE EDITOR DRAWS IT (SHADER-WARM-2; see the call site). The
/// template's own document, a mirror configured as EngineSceneViewport configures its
/// own, the editor's helpers at their defaults (plus the grid and a selected node with
/// its gizmo, so their shaders are in the set too), the editor's camera pose, and the
/// world's environment and its whole post chain (the offscreen opt-in). Born LOADING like
/// a created world (the parked arm's permutations), then released so the arm builds (its
/// compute set and the lit permutations), then graded like the project tile. Returns the
/// frames rendered.
int warmEditorWorld(Engine &engine, View *view,
                    const std::function<WindowTargetShape(const iris::Scene &)> &windowShape,
                    Database *db, SceneTemplate kind, bool presets,
                    const std::function<void()> &poll, const gatesettle::Settle &settle)
{
    const auto settled = [&](bool needRays) { return settle.settled(needRays); };
    iris::ScenePtr doc = scenetemplate::build(kind, db, nullptr);
    if (!doc || !view) return 0;
    applyBornWorld(doc);
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
        // THE EDITOR WINDOW's PASS FOR THIS WORLD (SceneMirror::windowPass through the window's validation): the
        // offscreen view is not a presented one, so the mirror pushes it no sample count of its own.
        view->setShadows(SceneMirror::windowPass(*doc).shadows);
        view->setSampleCount(windowShape(*doc).samples);
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
        // ONE FRAME: the sky and the camera on the warm-up view (and the recorder's view when one is warming),
        // in the frame's input (U0 3a).
        SceneMirror::FrameView recorderView;   // empty unless the recorder warms (3)
        FramePace pace = FramePace::Streaming;   // the build-up's; a phase's COMPLETE TAIL sets Complete (completeTail)
        // THE RAY TIER's BUILDS IN FLIGHT AT THE CACHE's BUILD (the card line, completeTail): read in the first frame
        // whose snapshot says the world's cards are built - a card reader whose build is still in flight then cannot
        // stamp inside the demand window that build opened.
        bool cardsBuiltSeen = false;
        unsigned buildsAtCardBuild = 0u;
        // ...AND THE LAST FRAME THAT WAS INCOMPLETE (the card line): the cache's frame of the last gate frame whose views
        // drew a placeholder, and of the last that skipped a pending draw (AsyncShaderStats: the views' draws, the
        // capture's excluded) - a reader on a placeholder (rough 0.8) traces no reflection ray and stamps nothing
        unsigned long long placeholdersSeen = 0ull, skipsSeen = 0ull, lastPlaceholderFrame = 0ull, lastSkipFrame = 0ull;
        auto frame = [&]() {
            SceneMirror::FrameView fv;
            fv.view = view;
            fv.sky = true;
            fv.camera = camera;
            SceneMirror::FrameViews views{ { fv } };
            if (recorderView.view) views.views.push_back(recorderView);
            mirror.sync(views);
            gizmo.updateSize(camera);
            overlay.update(selected ? &gizmo : nullptr, camera->getGlobalPosition(),
                           iris::Vec3(0, 0, -1), iris::Vec3(0, 0, -1));
            gateFrame(engine, pace);
            if (!cardsBuiltSeen && es->published().gi.cards.built) {
                cardsBuiltSeen = true;
                buildsAtCardBuild = engine.published().shaders.rayBuildsInFlight;
            }
            {
                const AsyncShaderStats &as = engine.published().shaders;
                const unsigned long long cacheFrame = es->published().gi.cards.cacheFrame;
                if (as.placeholderDraws != placeholdersSeen) lastPlaceholderFrame = cacheFrame;
                if (as.pendingSkips != skipsSeen) lastSkipFrame = cacheFrame;
                placeholdersSeen = as.placeholderDraws;
                skipsSeen = as.pendingSkips;
            }
            ++frames;
            poll();
        };
        // THE WORLD'S ENVIRONMENT ONCE, AND ITS WHOLE CHAIN ON THIS OFFSCREEN VIEW.
        // An offscreen view ignores the post chain unless the description opts in
        // (PostFxDesc::allowOffscreen) — measured: the editor's first frames then
        // compiled SMAA's three passes and the PBS permutations under them. The
        // screenshot's Scene grade is that opt-in (secondaryfx::applyScene), and it
        // rides the environment's own record (FrameView::finish): one description per
        // record, so the two flags never alternate and the workspace is built once.
        const auto environmentOnce = [&]() {
            SceneMirror::FrameView ev;
            ev.view = view;
            ev.environment = true;
            ev.finish = [](jahshaka::engine::FrameInputView &part, jahshaka::engine::View *v) {
                jahshaka::engine::PostFxDesc fx = part.postFx ? *part.postFx : v->pushedPostFx();
                secondaryfx::applyScene(fx, 0.0f);
                part.postFx = fx;
            };
            mirror.sync(SceneMirror::FrameViews{ { ev } });
        };
        environmentOnce();
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
        const auto untilQuiet = [&](bool needRays) {
            int phaseFrames = 0;
            quiet = 0;
            while (phaseFrames < kWarmWorldFrameCap && quiet < kWarmWorldQuietFrames) {
                frame();
                ++phaseFrames;
                engine.shaderBuildProgress(c, f, e);
                const GiStatus &gi = es->published().gi;   // the snapshot (U3 stage 2)
                const bool armed = gi.mode == GiMode::Off || gi.vctBound;
                quiet = (c == last && armed && settled(needRays)) ? quiet + 1 : 0;
                last = c;
            }
        };
        // THE COMPLETE TAIL (COLD-BOOT-1; gatesettle::Tail): the Streaming build-up gone quiet, the phase ends on COMPLETE
        // frames - the frames the editor draws: every consumer joins what it needs (all built by now), the lighting arm
        // whole - until 30 of them in a row compiled nothing and settled, a compile resetting the count. So what those
        // frames ask for is built inside them, on every launch alike: the cards a reader demanded, admitted and captured,
        // and their capture permutations (the Streaming phase alone left them to timing: a cold launch built one, the warm
        // launch after it six - shadercache.app.warm, spikes/cold-boot-1/repro-warm; the old gate, every frame Complete,
        // built them on every launch). Its frames, wall and longest frame are logged: a Complete frame that compiles holds
        // the window for that compile. `oneFrame` is the phase's own frame (the recorder's arms its video frame).
        const auto completeTail = [&](const std::function<void()> &oneFrame, const char *what) {
            gatesettle::Tail tail;
            QElapsedTimer wall; wall.start();
            qint64 longest = 0;
            pace = FramePace::Complete;
            while (!tail.done()) {
                engine.shaderBuildProgress(c, f, e);
                const unsigned before = c + f;
                QElapsedTimer one; one.start();
                oneFrame();
                longest = std::max(longest, one.elapsed());
                engine.shaderBuildProgress(c, f, e);
                tail.feed(c + f - before, settled(true));
            }
            pace = FramePace::Streaming;
            last = c;
            const GiStatus &gi = es->published().gi;
            // ...and THE DEMAND THE CARDS WERE DECIDED ON (CardCacheStatus: the cache's frame, the last change, each
            // reader's first and newest stamp since it - 0 the reflection, 1 the gather - the rest decisions taken, the
            // readers and the admissions of the last one) with the ray builds in flight at the cache's build: a line
            // reading "admitted 0" says on its own whether a reader stamped inside the window
            qInfo("startup shader build: the %s world's %s: a Complete tail of %d frames, %lld ms, the longest frame %lld ms;"
                  " its cards %s, admitted %llu, captured %llu, queued %u, resident %u; demand: cache frame %llu, changed"
                  " %llu, first %llu/%llu, newest %llu/%llu, rest decisions %llu (readers %u, admitted %u), ray builds in"
                  " flight at the cache's build %u, the last placeholder at %llu, the last pending skip at %llu; the frame"
                  " %s (terms %u, %llu incomplete in a row, completion serial %llu)",
                  es->name().c_str(), what, tail.frames(), static_cast<long long>(wall.elapsed()),
                  static_cast<long long>(longest), gi.cards.built ? "built" : "NOT BUILT", gi.cards.admitted,
                  gi.cards.captures, gi.cards.queueLength, gi.cards.cardsResident, gi.cards.cacheFrame,
                  gi.cards.demandChangeFrame, gi.cards.demandFirst[0], gi.cards.demandFirst[1], gi.cards.demandNewest[0],
                  gi.cards.demandNewest[1], gi.cards.restDecisions, gi.cards.decisionReaders, gi.cards.decisionAdmitted,
                  buildsAtCardBuild, lastPlaceholderFrame, lastSkipFrame,
                  es->published().completeness.incomplete ? "INCOMPLETE" : "complete", es->published().completeness.terms,
                  es->published().completeness.incompleteFrames, es->published().completeness.completionSerial);
            if (tail.failed())   // the cap, never a silent pass
                qWarning("FAIL: shader build gate: the %s world's %s Complete tail did not go quiet in %d frames - %s",
                         es->name().c_str(), what, gatesettle::Tail::kCap, settle.describe().c_str());
        };
        untilQuiet(false);
        // (2a) THE RAY PIPELINES' CONSUMERS (COLD-BOOT-1 stage 4): the builds started at the gate's first frame are waited
        // for HERE, on the published state, frames flowing and the window answering (never a join): then this world's
        // frames run the reflection, the gather, the cards' and the glass's passes on built pipelines, and what those
        // compile is built before the phase ends. The wait ENDS (gatesettle::Settle::waitOver): settled, or nothing
        // landing for kStallMs, or the gate's ceiling - the second and third loudly (a gate never spins).
        while (!settle.waitOver(true)) frame();
        if (settle.failed(true))
            qWarning("FAIL: shader build gate: the world's ray pipelines did not settle - %s", settle.describe().c_str());
        untilQuiet(true);
        completeTail(frame, "view");
        // (2b) EVERY OTHER TIER'S PLACEHOLDERS (ASYNC-SHADERS-1). A tier is a new PASS, and the
        // grey placeholder an object waits under is itself a permutation of that pass — so
        // unless it is built here, a tier change has nothing to draw its waiting objects with
        // until the placeholder's own compile lands (measured on a cold cache, a Basic world's
        // change to Low: 90 frames held). Drawn with the view asynchronous and the compiler in
        // its placeholders-only mode, every object requests the placeholder for the new pass
        // and nothing else; the frame waits for the background compiler until the tier
        // compiles nothing. (Building the worlds' own permutations at every tier too was
        // measured: +29 s on the cold gate. Their compile is what the placeholder covers.)
        {
            const worldmodes::PhotonTier born = worldmodes::photonTier(doc);
            const bool photonOn = worldmodes::photonEnabled(doc);
            // SCOPED: the mode is process-wide, and left on it would leave every asynchronous
            // object of the session on its placeholder. Whatever leaves this block turns it off.
            // (The view is asynchronous for the whole gate: COLD-BOOT-1 stage 4.)
            struct PlaceholdersOnly {
                Engine &e;
                explicit PlaceholdersOnly(Engine &engine_) : e(engine_) { e.setAsyncShaderPlaceholdersOnly(true); }
                ~PlaceholdersOnly() { e.setAsyncShaderPlaceholdersOnly(false); }
            };
            std::optional<PlaceholdersOnly> sweep(std::in_place, engine);
            for (int t = 0; t <= 3; ++t) {
                const worldmodes::PhotonTier tier = worldmodes::PhotonTier(t);
                if (tier == born) continue;
                worldmodes::setPhoton(doc, photonOn, tier);
                environmentOnce();
                AsyncShaderStats as = engine.published().shaders;
                unsigned long long lastDone = as.completed;
                engine.shaderBuildProgress(c, f, e);
                last = c;
                quiet = 0;
                for (int tf = 0; tf < kWarmWorldFrameCap && quiet < kWarmWorldQuietFrames; ++tf) {
                    frame();   // (settled() below: the placeholders built - never a wait on the UI thread)
                    as = engine.published().shaders;
                    engine.shaderBuildProgress(c, f, e);
                    const GiStatus &gi = es->published().gi;   // the snapshot (U3 stage 2)
                    const bool armed = gi.mode == GiMode::Off || gi.vctBound;
                    quiet = (c == last && as.completed == lastDone && armed && settled(false)) ? quiet + 1 : 0;
                    last = c;
                    lastDone = as.completed;
                }
            }
            sweep.reset();
            worldmodes::setPhoton(doc, photonOn, born);
            environmentOnce();
            for (int i = 0; i < kWarmUpFrames; ++i) frame();
        }
        // (3) the project tile's capture (CLOSE-SHOT-2: a save's tile is the view's
        // own frame drawn a second time without the furniture, View::
        // requestFrameCapture) — its chain instance has no shadow node and the
        // furniture baked out, and its first frame compiled HlmsAtom permutations
        // on the UI thread (measured: 5 on Showroom 2, 10 with 16 lamps, on the
        // close that took the session's first tile). Taken here, once, on this
        // view's whole chain.
        bool captureArmed = false;   // REQUESTS (U3 stage 4a): the capture's arm and its take
        engine.request([&](Engine &) { captureArmed = view->requestFrameCapture(false); });
        if (captureArmed) {
            frame();
            jahshaka::engine::Image tile;
            engine.request([&](Engine &) { view->takeFrameCapture(tile, true); });
        }
        // (4) THE VIDEO RECORDER'S VIEW (VIDEO-REC-2): the view a recording draws
        // (recordingview::create — the recorder's own builder, at the recording's
        // size), pushed as the recorder pushes it, every frame ARMED so the NV12
        // compute job dispatches. Measured in VIDEO-REC-1: the session's FIRST
        // recording compiled this on the UI thread (a 585 ms click). Scene only —
        // the switch's default; the helpers' shaders are the editor view's above.
        {
            QString why;
            View *rv = recordingview::create(engine, es, "startup-warmup-recording", recordingview::kWidth,
                                             recordingview::kHeight, /*helpers*/ false, &why);
            if (rv) {
                // ...asynchronous like the warm view (a view hands its flag to its scene: OgreView::setScene), so the
                // world's scene stays asynchronous for the whole gate (COLD-BOOT-1 stage 4)
                rv->setAsyncShaders(true);
                unsigned c = 0, f = 0, e = 0;
                engine.shaderBuildProgress(c, f, e);
                unsigned last = c + f;
                int quiet = 0;
                jahshaka::engine::VideoFrameNv12 picture;
                recorderView = recordingview::frameView(rv, camera, 0.0f);
                quint64 armed = 0;
                const auto recordedFrame = [&]() {   // one ARMED frame, as the recorder draws it
                    engine.request([&](Engine &) { rv->armVideoFrame(armed++); });   // REQUESTS (U3 4a)
                    frame();
                    engine.request([&](Engine &) { while (rv->takeVideoFrame(picture, false)) {} });
                };
                for (int i = 0; i < kWarmRecordFrameCap && quiet < kWarmRecordQuietFrames; ++i) {
                    recordedFrame();
                    engine.shaderBuildProgress(c, f, e);
                    quiet = (c + f == last && settled(false)) ? quiet + 1 : 0;
                    last = c + f;
                }
                // ...and ITS COMPLETE TAIL: the recorder's view gathers at its own size and pose, so its readers may demand
                // cards the warm view's did not; those cards' captures and their light job are built here, in the frames a
                // recording draws.
                completeTail(recordedFrame, "recorder view");
                engine.request([&](Engine &) { while (rv->takeVideoFrame(picture, true)) {} });
                recorderView = SceneMirror::FrameView();
                engine.destroyView(rv);
            } else {
                qWarning("startup shader build: the recorder's view could not be made: %s", qPrintable(why));
            }
        }
        view->setScene(nullptr);
        mirror.setSource(nullptr);
    }
    engine.destroyScene(es);
    return frames;
}

// THE GLOBAL PASS's RECORD LIVES IN THE CACHE IT DESCRIBES (URGENT #104, 2026-10-28). It was the setting
// shader_warm_pass, in the SETTINGS file - and every test row boots on a fresh home (tests/support/fresh_home.sh wipes
// it) with the tree's shared cache linked in, so the record never survived: every app row ran the whole pass over a
// warm cache, 25-31 s under the validation layer (a control on the same exe skipped it in 1.1 s). Here it is a file
// in the shader cache directory: linked and promoted with the cache (fresh_home's generation names it), deleted with
// it (a wipe at a fingerprint change, app.clearShaderCache, the preference's clear), absent when the cache is off -
// a missing record runs the pass. Written by temp + rename, never synced: derived data on the UI thread
// (SHADER-WARM-2), and a lost record costs one pass.
QString warmPassRecordPath()
{
    if (!EngineHost::shaderCacheEnabled()) return QString();
    const QString dir = EngineHost::shaderCacheDirectory();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("warm-pass.key"));
}

QString readWarmPassRecord()
{
    QFile f(warmPassRecordPath());
    if (f.fileName().isEmpty() || !f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll()).trimmed();
}

void writeWarmPassRecord(const QString &key)
{
    const QString path = warmPassRecordPath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return;
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    const QByteArray bytes = key.toUtf8() + '\n';
    const bool written = f.write(bytes) == bytes.size();
    f.close();
    if (!written || std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(path).constData()) != 0)
        QFile::remove(tmp);
}

}  // namespace

QString startupPassKey(const QString &mediaDigest)
{
    return startuppass::passKey(QStringLiteral(JAH_STARTUP_PASS_CODE_ID), mediaDigest,
                                QString::fromStdString(jahshaka::engine::shaderIdentity().fingerprint));
}

int printShaderIdentity(const QString &mediaDir)
{
    const std::string media = jahshaka::engine::mediaDigest(mediaDir.toStdString());
    std::printf("%spass-code %s\nidentity %s\n", jahshaka::engine::shaderIdentityText(media).c_str(),
                JAH_STARTUP_PASS_CODE_ID, qPrintable(startupPassKey(QString::fromStdString(media))));
    std::fflush(stdout);
    return 0;
}

unsigned holdSplashForShaderBuild(QApplication &app, VersionSplashScreen &splash, Database *db)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return 0;   // headless: no engine, no shaders, no wait
    // THE STAGED MEDIA's DIGEST, a term of the global pass's key (below), read OFF the UI thread (~4 MB, 17 ms measured in
    // Debug: shadercache.startup_pass_key prints it) while the warm-up's frames run; joined where the key is formed
    std::future<std::string> mediaDigest = std::async(std::launch::async, jahshaka::engine::mediaDigest,
                                                      (QCoreApplication::applicationDirPath() + QStringLiteral("/media")).toStdString());

    unsigned compiled = 0, cached = 0, expected = 0;
    engine->shaderBuildProgress(compiled, cached, expected);
    const unsigned entryTotal = compiled + cached;

    QElapsedTimer total;      total.start();
    unsigned last = entryTotal;
    bool shown = false;

    // AFTER EVERY FRAME: the settle fed from the published state, the splash moved when the count did, and the window's
    // events pumped - every frame, so the window answers throughout (COLD-BOOT-1 stage 4). User input stays out: a click
    // on a QSplashScreen hides it.
    gatesettle::Settle settle;
    auto poll = [&]() {
        const AsyncShaderStats as = engine->published().shaders;
        engine->shaderBuildProgress(compiled, cached, expected);
        gatesettle::Sample smp;
        smp.rayBuildsInFlight = as.rayBuildsInFlight;
        smp.hlmsOutstanding = as.pending - std::min(as.pending, as.rayBuildsInFlight);
        smp.computeDeferred = as.computeDeferred;
        smp.placeholderDraws = as.placeholderDraws;
        smp.pendingSkips = as.pendingSkips;
        smp.completed = unsigned(as.completed);
        smp.compiled = compiled + cached;
        settle.feed(smp, total.elapsed(), false);
        const unsigned now = compiled + cached;
        if (now != last) {
            last = now;
            splash.showShaderBuild(int(now), int(expected));
            shown = true;
            splash.repaint();
        }
        app.processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    };
    const auto settled = [&](bool needRays) { return settle.settled(needRays); };
    // BEFORE A SCENE OR A TIER GOES: its requests land first. Destroying a compute job CANCELS its request on the
    // background compiler (fork HlmsCompute::destroyComputeJob -> cancelKey): a phase that ended on its frame cap with
    // its permutations still building would lose them (built live in the editor later). Frames until the settle's
    // wait is over; a stall or the ceiling ends it, loudly.
    const auto drain = [&](const char *what) {
        while (!settle.waitOver(false)) {
            gateFrame(*engine);
            poll();
        }
        if (settle.failed(false))
            qWarning("FAIL: shader build gate: %s did not settle - %s", what, settle.describe().c_str());
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
    // THE SHAPE COMES FROM WHERE THE EDITOR's OWN VIEW TAKES IT AT THIS LAUNCH (SHADER-SYSTEM-1 stage 7a; the
    // remembered `shader_warmup_samples` key it replaces warmed 1 sample on a first launch while the editor drew 2):
    // the pass from the document through the mirror's one function (SceneMirror::windowPass - the editor's window
    // view is pushed exactly that), and the TARGET from the window system (Engine::windowTargetShape: the swapchain's
    // own format choice and sample validation on a temporary surface - a final pass's pipeline keys on both: the
    // display encode and SMAA's blend built in the editor's first frame when the gate drew into RGBA8). The window
    // asked about is the SPLASH's: the host's one display connection and the default visual the editor's window also
    // gets (neither is translucent), and a surface's format list follows its window's visual - so the editor's
    // viewport widget never makes its native window early for this.
    const iris::ScenePtr born = iris::Scene::create();
    applyBornWorld(born);
    const auto windowShape = [&](const iris::Scene &doc) {
        WindowTargetShape t;
        t.samples = SceneMirror::windowPass(doc).samples;
        if (!engine->windowTargetShape(NativeWindowHandle(splash.winId()), t.samples, t))
            qWarning("startup shader build: the window's target shape is unknown (%s) - the warm view draws RGBA8 at "
                     "%u sample(s)", engine->lastError().c_str(), t.samples);
        return t;
    };
    const WindowTargetShape target = windowShape(*born);
    View *warmView = engine->createOffscreenView("startup-warmup", kWarmUpSize, kWarmUpSize,
                                                 Colour(0.0f, 0.0f, 0.0f, 1.0f), OffscreenContract::Live,
                                                 target.format);
    const GateScope gateScope{ *engine, warmView };   // before the gate's first frame: every way out gives it all back
    // THE RAY PIPELINES START NOW, ALL OF THEM, the reflection's first (COLD-BOOT-1 stage 4): their cold driver compiles
    // (~100 s of a first launch, serial at their consumers' first frames before) run on their builders beside everything
    // below; the world phases wait for them on the published state before their consumers' frames. And the warm view's
    // scenes compile in the BACKGROUND for the whole gate: a frame never waits for a compile.
    if (warmView) {
        engine->prepareRayPipelines();
        warmView->setAsyncShaders(true);
    }
    // LIVE (View::setOffscreenContract): its frames are never read as a picture
    // (its scene's GI below is there to compile the compute set, not to be seen).
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
        warmView->setShadows(SceneMirror::windowPass(*born).shadows);
        warmView->setSampleCount(target.samples);
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
            l.castShadows = SceneMirror::windowPass(*born).shadows;
            warmScene->setLight(sun, l);
        }
        // THE SKY's ENVIRONMENT LANDS BEFORE ANY OBJECT IS DRAWN (SHADER-SYSTEM-1, the final read's item 3b): the
        // analytic sky's capture, its convolution and its SH become every PBS datablock's reflection cube
        // (OgreScene::landEnvironmentIfComplete -> applyReflectionToAll), and the cube is in a permutation's hash and
        // root layout - in the stock depth prepass too. It used to land when the GI frames ran on a cold cache (the
        // convolution compiling in the background: ~frame 52) but at frame 1 on a warm one, while the box was still
        // in its first, stock frames: a warm launch drew a state - the box in the stock depth pass WITH the cube - no
        // cleared launch reached, and compiled it in a frame (hlms.1 #45, shadercache.app.warm). So the sky first, and
        // the boxes only once the scene PUBLISHES its environment landed (environmentPending false, the SH valid): on
        // every cache the box's first frames draw under the cube. A stall or the ceiling ends the wait, loudly.
        {
            SkyDesc sky;
            sky.mode = SkyMode::Atmosphere;
            warmScene->setSky(sky);
        }
        const auto environmentOwed = [&]() {
            return warmScene->published().environmentPending || !warmScene->published().skyAmbientShValid;
        };
        int envFrames = 0;
        QElapsedTimer envTimer; envTimer.start();
        while (environmentOwed() && !settle.stalled() && !settle.pastCeiling()) {
            gateFrame(*engine);
            poll();
            ++envFrames;
        }
        if (environmentOwed())
            qWarning("FAIL: shader build gate: the warm-up scene's environment did not land - %s",
                     settle.describe().c_str());
        qInfo("startup shader build: the warm-up scene's sky environment landed after %d frames, %lld ms (before its "
              "boxes are drawn)", envFrames, (long long)envTimer.elapsed());
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
        // under the ANALYTIC SKY set above (the bounce, the field and the cards read its environment cube:
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
        // THE VOXELISER'S OTHER TWO VARIANTS (ASYNC-SHADERS-1). The voxeliser compiles one
        // compute variant per texture combination it meets (none, albedo, emissive, both —
        // PhotonVoxelizer.cpp's c_numVctProperties) and a scene's first GI build dispatches
        // the ones its materials need inside one frame: measured, the Tornado sample's
        // open compiled two of them on the UI thread after its dialog. The box above is the
        // albedo variant; two more boxes carry an emissive map, with and without an albedo,
        // so the cold gate builds all four.
        if (boxMesh) {
            const unsigned char glow[4 * 4 * 4] = {
                40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255,
                40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255,
                40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255,
                40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255, 40, 30, 20, 255 };
            for (int withAlbedo = 0; withAlbedo < 2; ++withAlbedo) {
                const NodeId extra = warmScene->createNode();
                PbrParams m;
                m.albedo = Colour(0.5f, 0.5f, 0.5f);
                const MaterialId mat = extra ? warmScene->createPbrMaterial(m) : MaterialId(0);
                if (!mat) continue;
                const TextureId emissive = warmScene->createTexture(4u, 4u, glow, true, true);
                if (emissive) warmScene->setPbrTexture(mat, PbrTextureSlot::Emissive, emissive);
                if (withAlbedo) {
                    const TextureId albedo = warmScene->createTexture(4u, 4u, glow, true, true);
                    if (albedo) warmScene->setPbrTexture(mat, PbrTextureSlot::Albedo, albedo);
                }
                warmScene->attachMesh(extra, boxMesh, mat);
                warmScene->setNodeTransform(extra, Vec3(withAlbedo ? 3.0f : -3.0f, 0.0f, 0.0f),
                                            Quat(), Vec3(1.0f, 1.0f, 1.0f));
            }
        }
        if (box && boxMesh && boxMatId && warmScene->attachMesh(box, boxMesh, boxMatId)) {
            // ...the tier a new scene of THIS PROCESS is born with: Epic, or the process's
            // test tier (TEST-TIER-1, services/testtier.h — a Low test process must not build
            // the Epic chain here either: measured, this warm-up was a 1.28 GB transient in a
            // process whose scenes then held 0.3 GB), resolved through the World Mode registry.
            const worldmodes::PhotonTier tier = worldmodes::photonTier(born);
            GiParams gi;
            gi.mode = worldmodes::photonEnabled(born)
                          ? GiMode(qBound(0, worldmodes::photonTechnique(tier), 2)) : GiMode::Off;
            gi.quality = GiQuality(qBound(0, worldmodes::photonQuality(tier), 3));
            gi.bounces = worldmodes::photonBounces(tier) > 0;
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
        gateFrame(*engine);
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
    // (the merge read): the cache key never names the Studio code, so a Studio-only
    // change to a preset, a template or a helper shader left Ogre's cache warm and the
    // pass skipped — and the change compiled LIVE. The key of the last COMPLETED pass is
    // WHAT DECIDES ITS BYTES (TEST-STREAMLINE-1 point 3; it named the Studio COMMIT, so every
    // commit re-ran it): this pass's own sources and the engine's full build id
    // (JAH_STARTUP_PASS_CODE_ID), the staged media by content and the cache fingerprint
    // (app/startuppasskey.h, the one definition the test seeds read too); a cold cache runs it as well.
    // ...AND WHEN THE CACHE IT FILLS IS NOT WHOLE (SHADER-SYSTEM-1 stage 6a): the key is CONTENT (startupPassKey: no
    // device term, so `--print-shader-identity` prints it with no GPU), and a driver or a card change is seen where the
    // cache sees it - the layers whose key holds the GPU are dropped at the load (layersNotLoaded). The compile count
    // does not see it: the dropped recipes regenerate from a whole microcode layer and compile nothing.
    const QString passKey = startupPassKey(QString::fromStdString(mediaDigest.get()));
    std::vector<std::string> notLoaded;
    engine->request([&](jahshaka::engine::Engine &e) { notLoaded = e.shaderCacheStats().layersNotLoaded; });   // a REQUEST (U3 stage 3)
    const bool keyMoved = readWarmPassRecord() != passKey;   // the record in the cache it describes (URGENT #104)
    const bool runPass = compiled > 0 || keyMoved || !notLoaded.empty();
    if (keyMoved && compiled == 0)
        qInfo("startup shader build: the global pass's key moved (what it compiles changed: its code, "
              "the engine, the media or the cache) — running it over a warm cache");
    if (!notLoaded.empty()) {
        std::string names;
        for (const std::string &n : notLoaded) names += (names.empty() ? "" : ", ") + n;
        qInfo("startup shader build: the cache's load left %zu layer(s) empty (%s) - running the global pass",
              notLoaded.size(), names.c_str());
    }
    unsigned lastCompiled = compiled;
    int quiet = 0;
    // ...AND UNTIL THE LIGHTING ARM IS BOUND (TEST-TIER-1): the Atom-off frames below draw the
    // box WITH the cone tracer, which means nothing before the arm is bound; "quiet" alone does not
    // promise that, so the loop also runs until the arm has been bound for kWarmUpFrames frames
    // (the log line says so when it never was).
    auto armBound = [&]() {
        if (!haveWarmGi || warmGi.mode == GiMode::Off) return true;
        // Every GI mode the tiers ship voxelises (VCT, alone or under the probes): the arm is
        // bound when the cone tracer is.
        return warmScene->published().gi.vctBound;
    };
    int boundFrames = 0;
    for (; runPass && warmScene && giFrames < kWarmUpGiFrames &&
           ((quiet < kGiQuietFrames && !warmScene->published().gi.giAtRest) || boundFrames < kWarmUpFrames);
         ++giFrames) {
        gateFrame(*engine);
        poll();
        engine->shaderBuildProgress(compiled, cached, expected);
        quiet = (compiled == lastCompiled && settled(false)) ? quiet + 1 : 0;
        lastCompiled = compiled;
        if (armBound()) ++boundFrames;
    }
    // ...AND THE STATE A WARM CACHE DRAWS FIRST (the diagnosis: spikes/test-tier-1/coldgate/). On a
    // warm cache the arm binds inside the first frames — before the visibility buffer has taken the
    // box over — so the box is drawn once through STOCK PBS on the warm view's GI route (the editor's:
    // it gathers where the scene traces, as every view does - ROUTE-ROWS-1). A cold boot, whose arm binds
    // only after its compute set compiled, has handed the box to Atom by then and never draws that
    // permutation; the second boot after every cache rebuild compiled it and ran this whole warm-up
    // again. Drawn here on the cold boot: the id pass off.
    if (runPass && warmScene && haveWarmGi && warmGi.mode != GiMode::Off) {
        warmScene->setAtomDrawEnabled(false);
        for (int i = 0; i < kWarmUpFrames; ++i) {
            gateFrame(*engine);
            poll();
        }
        warmScene->setAtomDrawEnabled(true);
    }
    // ...AND THE DELTA INJECTION (PHOTON-7 stage 4): a cascade rebuilt where it stands, a lamp's change and a whole
    // relight inject their direct term as a DELTA over the kept voxels (the feedback keeps the bounce), and the first one
    // after a project is open compiled VCT/LightInjection's delta permutation on the UI thread (shader.async_after_open,
    // 0.67 s). The arm's whole relight (Scene::refreshGiLighting) dispatches that permutation here, per tier, until the
    // compiles go quiet again.
    const auto warmDelta = [&]() {
        if (!warmScene || !warmScene->refreshGiLighting()) return 0;
        int frames = 0, deltaQuiet = 0;
        engine->shaderBuildProgress(compiled, cached, expected);
        unsigned deltaLast = compiled;
        for (; frames < kWarmUpGiFrames && deltaQuiet < kGiQuietFrames; ++frames) {
            gateFrame(*engine);
            poll();
            engine->shaderBuildProgress(compiled, cached, expected);
            deltaQuiet = (compiled == deltaLast && settled(false)) ? deltaQuiet + 1 : 0;
            deltaLast = compiled;
        }
        return frames;
    };
    if (runPass && warmScene && haveWarmGi && warmGi.mode != GiMode::Off) giFrames += warmDelta();
    // EVERY OTHER TIER'S COMPUTE SET TOO (ASYNC-SHADERS-1). After a project is open a tier
    // change must not compile on the UI thread, and the voxel arm's compute permutations
    // (the injection, the bounce, the field's integration, the card light — per cascade
    // count, anisotropy and bounce count) cannot be drawn with a placeholder: the arm is
    // rebuilt from scratch and dispatches them inside one frame. Measured on a cold cache:
    // a Basic world's change to Low compiled five of them in the frame (1.25 s). So the
    // cold gate builds each tier's arm once over the warm-up box, until its compiles go
    // quiet; a warm launch skips it with the rest of the pass.
    if (runPass && warmScene && haveWarmGi) {
        for (int t = 0; t <= 3; ++t) {
            const worldmodes::PhotonTier tier = worldmodes::PhotonTier(t);
            GiParams gi = warmGi;
            gi.mode = GiMode(qBound(0, worldmodes::photonTechnique(tier), 2));
            gi.quality = GiQuality(qBound(0, worldmodes::photonQuality(tier), 3));
            gi.bounces = worldmodes::photonBounces(tier) > 0;
            gi.ddgi = worldmodes::photonDdgi(tier) ? GiToggle::On : GiToggle::Off;
            if (gi.mode == warmGi.mode && gi.quality == warmGi.quality &&
                gi.bounces == warmGi.bounces && gi.ddgi == warmGi.ddgi)
                continue;  // the born tier: built above
            warmScene->setGlobalIllumination(gi);
            int tierQuiet = 0, tierFrames = 0;
            engine->shaderBuildProgress(compiled, cached, expected);
            unsigned tierLast = compiled;
            for (; tierFrames < kWarmUpGiFrames &&
                   (tierQuiet < kGiQuietFrames || !warmScene->published().gi.vctBound);
                 ++tierFrames) {
                gateFrame(*engine);
                poll();
                engine->shaderBuildProgress(compiled, cached, expected);
                tierQuiet = (compiled == tierLast && settled(false)) ? tierQuiet + 1 : 0;
                tierLast = compiled;
            }
            giFrames += tierFrames;
            if (gi.mode != GiMode::Off) giFrames += warmDelta();
            drain("a tier's compute set");   // before the next tier rebuilds the arm (its jobs, its requests)
        }
        // NO RESTORE (BUGS-7 (c)): the born tier's arm was built above, and the warm-up scene is destroyed next — a
        // push back to it built a lighting arm nothing drew (four frames of the gate).
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
    // (buildEnvironment — the same call the viewport makes every frame).
    if (runPass && warmView && db) {
        drain("the GI compute set's warm-up");   // before its scene goes (its jobs, its requests)
        warmView->setScene(nullptr);
        if (warmScene) engine->destroyScene(warmScene);
        warmScene = nullptr;
        const SceneTemplate kinds[] = { SceneTemplate::Basic, SceneTemplate::World };
        for (const SceneTemplate kind : kinds) {
            engine->shaderBuildProgress(compiled, cached, expected);
            const unsigned before = compiled;
            QElapsedTimer worldTimer; worldTimer.start();
            const int frames = warmEditorWorld(*engine, warmView, windowShape, db, kind,
                                               /*presets*/ kind == SceneTemplate::Basic, poll, settle);
            engine->shaderBuildProgress(compiled, cached, expected);
            qInfo("startup shader build: the %s template's world took %d frames, %lld ms and "
                  "compiled %u shader(s)",
                  kind == SceneTemplate::Basic ? "Basic (and the presets')" : "World", frames,
                  static_cast<long long>(worldTimer.elapsed()), compiled - before);
        }
        // Recorded only once the pass has COMPLETED: a launch killed half way runs it again.
        writeWarmPassRecord(passKey);
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
    // chain, compiled in a view shaped like the editor's window (SceneMirror::
    // windowPass + Engine::windowTargetShape, above). The per-world
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
    // THE COVER, ONCE, IN THE WINDOW's SHAPE (SHADER-SYSTEM-1 7a): the editor's window presents its first frames
    // scene-less - the blank chain, the clear and the cover (EngineSceneViewport::showEvent) - so the blank chain's
    // pipelines and the cover's HUD pair (Jahshaka/OverlayFill, Fonts/DebugFont) are drawn here first, on this view
    // in the window's format and samples (offscreen views draw an overlay only when they opt in: allowOffscreen).
    if (warmView) {
        if (warmView->scene()) warmView->setScene(nullptr);
        ViewOverlayDesc cover;
        cover.cover = ViewOverlayDesc::Cover::Loading;
        cover.coverTitle = "Loading world";
        cover.coverSubtitle = "startup";
        cover.allowOffscreen = true;
        warmView->setOverlay(cover);
        gateFrame(*engine);
        poll();
        drain("the cover's frame");
        warmView->setOverlay(ViewOverlayDesc());
    }
    if (warmView)  warmView->setEnabled(false);

    // ---- Settle: until a whole frame's work is there, the ray builds joined -----------
    // On the published state (gatesettle.h), the window's events pumped between frames (poll); counted in SETTLED frames
    // - a frame that settles and compiled nothing ends it. The hang guard is the settle's (nothing LANDING for kStallMs,
    // or the gate's ceiling): a FAIL line, and the editor opens with the work still landing - never a freeze or a spin.
    for (int settledFrames = 0; settledFrames < kSettleFrameCap;) {
        engine->shaderBuildProgress(compiled, cached, expected);
        const unsigned before = compiled + cached;
        gateFrame(*engine);
        poll();
        if (settle.failed(true)) {
            qWarning("FAIL: shader build gate: the final settle did not settle (%u shaders) - %s - showing the window; "
                     "the rest lands in the background", last, settle.describe().c_str());
            break;
        }
        if (!settled(true)) continue;
        ++settledFrames;
        if (last == before) break;
    }

    // THE GATE's WORK IS SAVED WHEN IT RETURNS (SHADER-SYSTEM-1 stage 7a, call 9). The host's watchdog saves a burst only
    // after three quiet seconds, and it starts after the gate (main.cpp): a first launch killed 2.4 s after its gate lost
    // all 318 compiles (spikes/cold-wipe L1). The gate has settled here - the watchdog's own condition - so a gate that
    // COMPILED posts its save now, ONCE: the cache's own job (its copy in chunks on the frames below, its write on the
    // cache's writer), and the gate returns when it has landed (ShaderCacheStats::saveInFlight), frames flowing and the
    // window answering; the watchdog takes it as its baseline (EngineHost::shaderCacheSaved). A gate that compiled
    // nothing saves nothing and adds nothing (its stamps are the watchdog's, as before: the eviction law writes them in
    // the sidecar alone). The wait is BOUNDED by kGateSaveWaitMs - a hang guard on the writer's wall-clock work (an
    // fsync measured up to 403 ms behind writeback), never a settle: past it a FAIL line and the gate opens, the write
    // still landing. A gate that did not settle leaves the save to the watchdog: its work is still landing.
    constexpr long long kGateSaveWaitMs = 10000;
    engine->shaderBuildProgress(compiled, cached, expected);
    if (compiled == 0) {
        qInfo("startup shader build: the gate compiled nothing - no save at the gate (0 frames)");
    } else if (!settle.failed(true)) {
        engine->saveShaderCache();
        bool inFlight = true;
        int saveFrames = 0;
        QElapsedTimer saveTimer; saveTimer.start();
        while (inFlight && saveTimer.elapsed() < kGateSaveWaitMs && !settle.pastCeiling()) {
            gateFrame(*engine);
            poll();
            ++saveFrames;
            engine->request([&](Engine &e) { inFlight = e.shaderCacheStats().saveInFlight; });   // a REQUEST (U3 stage 3)
            // ...and A YIELD, NOT A SETTLE: while the writer's job runs (its fsync is wall-clock work no frame
            // advances) the UI thread gives up 1 ms between frames instead of spinning empty ones (measured: 3,506
            // frames for a 153-ms save); the copy's chunks still get a frame each, the window's events are pumped
            // (poll), and the end is read from the published state above, never from this wait
            if (inFlight) QThread::msleep(1);
        }
        if (inFlight)
            qWarning("FAIL: shader build gate: its save did not land in %lld ms (%d frames) - the gate opens, the write "
                     "still in flight", kGateSaveWaitMs, saveFrames);
        else
            EngineHost::instance().shaderCacheSaved();
        qInfo("startup shader build: the gate's save of %u compiled shader(s) took %d frames, %lld ms", compiled,
              saveFrames, static_cast<long long>(saveTimer.elapsed()));
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
