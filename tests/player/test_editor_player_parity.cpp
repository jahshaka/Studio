// EDITOR/PLAYER PARITY — the owner's law: the same scene must look the same in
// the editor viewport and in the player. They differ only by editor helpers
// (grid, light wires, gizmos, selection outline) being hidden.
//
// ONE SCENE (owner decision 2026-09-14, lane PLAYER-1). The Player is a second
// VIEW on the editor's engine scene, drawn by the editor's mirror, with the
// furniture masked out per view. This suite therefore asserts two things the
// two-scene shape could not even express:
//
//   * THE SWITCH IS FREE. Round-tripping editor -> player -> editor moves
//     neither Scene::giStatus().rebuilds nor shadowStatus().atlasRebuilds. On
//     the old shape each switch migrated the document's graph between two Ogre
//     scene managers, released every engine object and full-walked the arriving
//     mirror, which is a from-scratch GI build on both sides of every switch
//     (audit F1).
//   * THE MIRRORS SURVIVE IT. The HlmsPbs planar-reflection binding is
//     process-wide and was owned by whichever SCENE armed last, so after a
//     switch one space's mirrors sampled the other's arm against the other's
//     plane — or vanished, because HlmsPbs only samples when the bound arm's
//     last update camera MATCHES the one rendering (audit F2). With one scene
//     there is one arm; the case below renders the two spaces ALTERNATELY,
//     exactly as a page switch does, and compares the mirror region.
//
// The defect this suite exists to lock down (owner-sighted 2026-09-03, "dark in
// the editor, blown out white in the player"): EnginePlayerScene::step() pushed
// only sync + applySky + applyCamera, while EngineSceneViewport::syncFrame()
// pushes sync + applySky + applyEnvironment + applyCamera. applyEnvironment is
// the World panel — ambient, shadows, MSAA, fog, GI, planar reflections AND the
// post-processing chain (PostFxDesc: HDR + filmic tonemap, bloom, SSAO). With it
// missing the player view kept a DEFAULT PostFxDesc, i.e. the passthrough
// workspace: the scene's HDR-range values reached the LDR window with no
// tonemap and clipped to white, and the player also lost fog and the document's
// ambient.
//
// Both halves are driven the way the app drives them, but into offscreen views
// so the readback is exact. Two assertions, both on pixels:
//
//   1. NEUTRAL (chain off, both sides): fog + ambient + shadow-toggle parity.
//      These are scene/view state that an offscreen view honours, so a missing
//      applyEnvironment shows up directly in the readback.
//   2. CHAIN ON (PostFxDesc::allowOffscreen forced on both views, the only way
//      to pixel-test the chain at all — see the field's doc comment): the
//      tonemap parity that IS the owner's symptom. What is forced is each view's
//      OWN pushed description, so if the two descriptions differ, so do the
//      pixels.
//
// Plus the shape contract itself: playerView->postFx() == editorView->postFx().
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QColor>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "player/engineplayerscene.h"
#include "player/playback.h"
#include "viewport/freecamerapolicy.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const int W = 160, H = 160;
static const Colour kBackground(0.10f, 0.11f, 0.14f);

static int maxAbsDiff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return 255;
    float d = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            d = std::max({ d, std::fabs(p.r - q.r), std::fabs(p.g - q.g), std::fabs(p.b - q.b) });
        }
    return int(d * 255.0f + 0.5f);
}

static void probe(const char *tag, const Image &i)
{
    const Colour c = i.at(unsigned(W / 2), unsigned(H / 2)), k = i.at(3, 3);
    std::printf("    %-28s centre %3.0f %3.0f %3.0f   corner %3.0f %3.0f %3.0f\n", tag,
                double(c.r * 255), double(c.g * 255), double(c.b * 255),
                double(k.r * 255), double(k.g * 255), double(k.b * 255));
}

/// How much CONTENT a picture has: the largest channel spread across the whole
/// frame. A parity comparison of two identical FLAT frames is vacuous, and the
/// suite says so rather than passing (the mirror region's comparison below
/// leans on this).
static int spread(const Image &i)
{
    float lo = 1.0f, hi = 0.0f;
    for (unsigned y = 0; y < i.height; ++y)
        for (unsigned x = 0; x < i.width; ++x) {
            const Colour c = i.at(x, y);
            lo = std::min({ lo, c.r, c.g, c.b });
            hi = std::max({ hi, c.r, c.g, c.b });
        }
    return int((hi - lo) * 255.0f + 0.5f);
}

// THE EDITOR'S FRAME, exactly as EngineSceneViewport::syncFrame drives it
// (minus the editor-helper calls, which this document has none of).
static void editorFrame(SceneMirror &editorMirror, Engine &engine, View *editorView,
                        const iris::CameraNodePtr &camera)
{
    editorMirror.sync();
    editorMirror.applySky(editorView);
    editorMirror.applyEnvironment(editorView, &engine);
    editorMirror.applyCamera(camera, editorView, freecam::kFreeCameraFramingAspect);
    engine.renderOneFrame();
}

// One frame of BOTH paths at once — what the ORIGINAL parity case needs (the
// two descriptions must agree, and the cheapest way to see that is to push both
// in one frame). The player runs first so both sides see the same document
// state: PlayBack moves the camera and ticks animation inside step().
static void frame(EnginePlayerScene &player, SceneMirror &editorMirror, Engine &engine,
                  View *editorView, const iris::CameraNodePtr &camera, float dt)
{
    player.step(dt, W, H);
    editorMirror.sync();
    editorMirror.applySky(editorView);
    editorMirror.applyEnvironment(editorView, &engine);
    editorMirror.applyCamera(camera, editorView, freecam::kFreeCameraFramingAspect);
    engine.renderOneFrame();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_editor_player_parity-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // THE VIEWS COME FIRST, AND THAT IS LOAD-BEARING (found by this lane).
    //
    // A document node's transform, its children and its very existence live in
    // an Ogre scene node (SCENEGRAPH_SPEC D2). Until a document is bound to an
    // engine scene those nodes sit in the process-wide STAGING manager — and
    // `graph::stagingScene()` cannot build that manager before a render window
    // exists and the Hlms is registered, so before the first View it answers
    // NULL and every addChild/setLocalPos silently does nothing
    // (nodegraph.cpp:397-415, "the suites always create their View before their
    // first document node").
    //
    // This suite used to build its document first. The result was a document
    // with an EMPTY root: both views rendered the bare sky, the comparisons all
    // read 0/255, and the suite passed on two identical blank frames for as
    // long as it has existed. The `spread()` assertion below is the guard that
    // would have caught it.
    // ---- ONE SCENE, two views (lane PLAYER-1) -------------------------------
    View *editorView = engine->createOffscreenView("parity-editor", W, H, kBackground);
    View *playerView = engine->createOffscreenView("parity-player", W, H, kBackground);
    CHECK(editorView && playerView, "two offscreen views of the same size and background");
    if (!editorView || !playerView) return 1;
    Scene *editorScene = engine->createScene("parity-editor");
    editorView->setScene(editorScene);
    // The editor's engine scene starts at the same hardcoded default
    // EngineSceneViewport::ensureEngineScene sets.
    editorScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));

    // ---- the document: a lit cube on a ground plane, a bright world ----------
    // Every World-panel dial that applyEnvironment owns is moved OFF its default
    // so that a path which skips applyEnvironment cannot accidentally agree.
    auto doc = iris::Scene::create();
    doc->setSkyColor(QColor(30, 40, 70));
    // STRONGLY TINTED AMBIENT, which is now a Sky Light (SKY_LIGHT_SPEC.md §2):
    // the tint multiplies the sky's own integral, so a red-tinted skylight over
    // a blue sky is still a value applyEnvironment owns and a path that skips
    // applyEnvironment still cannot agree by accident.
    {
        auto skyLight = iris::LightNode::create();
        skyLight->setLightType(iris::LightType::Sky);
        skyLight->setName("Sky Light");
        skyLight->color = QColor(220, 60, 40);
        skyLight->intensity = 2.0f;
        doc->rootNode->addChild(skyLight);
    }
    doc->shadowEnabled  = true;
    doc->fogEnabled     = true;                   // fog is applyEnvironment's, not applySky's
    doc->fogColor       = QColor(40, 220, 120);
    doc->fogDensity     = 0.06f;
    doc->hdrEnabled     = true;                   // THE symptom: filmic tonemap + auto exposure
    doc->exposure       = 0.0f;
    doc->bloomEnabled   = true;
    doc->bloomThreshold = 5.0f;
    doc->ssaoEnabled    = false;                  // AO is a separate lane's pixel gate
    doc->antiAliasing   = 1;                      // offscreen views stay 1x either way

    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Directional);
    light->setName("sun");
    light->color = QColor(255, 255, 255);
    light->intensity = 3.0f;                      // bright enough to clip without a tonemap
    light->setLocalRot(iris::Quat::fromEulerAngles(-55, 25, 0));
    doc->getRootNode()->addChild(light);

    auto cube = iris::MeshNode::create();
    cube->setName("cube");
    cube->setMesh(":assets/models/cube.obj");
    auto grey = iris::DefaultMaterial::create();
    grey->setDiffuseColor(QColor(230, 230, 230));
    cube->setMaterial(grey);
    CHECK(!!cube->getMesh(), "cube.obj loaded into the document");
    const float r = cube->getMeshRadius();
    const float s = r > 0.0f ? 1.0f / r : 1.0f;
    cube->setLocalScale(iris::Vec3(s, s, s));
    cube->setLocalPos(iris::Vec3(0, 1, 0));
    doc->getRootNode()->addChild(cube);

    // A MIRROR FLOOR (PLANAR_REFLECTIONS_SPEC §7) — the whole point of the
    // switch case below: the HlmsPbs planar binding is process-wide and used to
    // belong to whichever SCENE armed last.
    auto floor = iris::MeshNode::create();
    floor->setName("mirror floor");
    floor->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj"));
    {
        auto mirrorMat = iris::DefaultMaterial::create();
        mirrorMat->setDiffuseColor(QColor(30, 30, 34));
        floor->setMaterial(mirrorMat);
    }
    floor->setLocalScale(iris::Vec3(8, 1, 8));
    floor->setLocalPos(iris::Vec3(0, 0, 0));
    floor->setPlanarReflector(true);
    doc->getRootNode()->addChild(floor);
    // Budget 2 (shadows inside the reflection) and the HYBRID GI mode: the two
    // scene-level systems a page switch used to rebuild from scratch.
    doc->planarReflectionBudget = 2;
    doc->giMode = iris::GiMode::VCT_PCC_HYBRID;

    auto camera = iris::CameraNode::create();
    camera->setLocalPos(iris::Vec3(0, 2, 6));
    camera->lookAt(iris::Vec3(0, 1, 0));
    camera->angle = 45.0f;
    camera->nearClip = 0.1f;
    camera->farClip = 100.0f;
    doc->refresh();
    // The document really MATERIALISED (see "THE VIEWS COME FIRST" above): with
    // no staging manager every addChild is a silent no-op and the root stays
    // empty, which is exactly how this suite came to compare two blank frames.
    CHECK(doc->getRootNode()->childCount() > 0, "the document's root has children");

    int rc = 0;
    {
        SceneMirror editorMirror(editorScene);
        editorMirror.setLightWires(false);     // the player never shows them; parity means no helpers
        editorMirror.setSource(doc);

        EnginePlayerScene player(engine);
        player.setEditorScene(editorScene, &editorMirror);
        CHECK(player.attach(playerView), "player view bound to the editor's scene");
        CHECK(player.engineScene() == editorScene, "ONE SCENE: the player draws the editor's");
        CHECK(!playerView->helpersVisible() && editorView->helpersVisible(),
              "the furniture is hidden per VIEW, not per scene");
        player.setDocument(doc, camera);
        player.begin();

        // Settle: the chain rebuild, the shadow atlas and auto-exposure all need
        // a few frames, and auto-exposure ADAPTS — the two views share the
        // process-global luminance history, so they converge together.
        for (int i = 0; i < 12; ++i) frame(player, editorMirror, *engine, editorView, camera, 1.0f / 60.0f);

        // ---- 1. the shape contract ------------------------------------------
        // COPIES: postFx() returns a reference to the view's live description,
        // which step 3 below rewrites.
        const PostFxDesc efx = editorView->postFx();
        const PostFxDesc pfx = playerView->postFx();
        std::printf("    editor postFx: hdr=%d bloom=%d ssao=%d exposure=%.2f\n",
                    int(efx.hdr), int(efx.bloom), int(efx.ssao), double(efx.exposure));
        std::printf("    player postFx: hdr=%d bloom=%d ssao=%d exposure=%.2f\n",
                    int(pfx.hdr), int(pfx.bloom), int(pfx.ssao), double(pfx.exposure));
        CHECK(pfx.hdr, "the player view was told the scene is HDR (it was not, before this fix)");
        CHECK(pfx == efx, "ONE chain shape: player postFx == editor postFx");
        CHECK(playerView->shadows() == editorView->shadows(), "shadow flag matches");

        // ---- 2. neutral pixels: fog, ambient, shadows ------------------------
        Image e, p;
        CHECK(editorView->readPixels(e), "editor readback");
        CHECK(playerView->readPixels(p), "player readback");
        probe("editor (chain off)", e);
        probe("player (chain off)", p);
        const int neutral = maxAbsDiff(e, p);
        std::printf("    max |editor - player|, chain off = %d/255\n", neutral);
        CHECK(neutral <= 4, "editor and player render the same pixels (fog/ambient/shadows)");

        // ---- 3. WITH THE POST CHAIN: the owner's symptom ---------------------
        // Offscreen views discard the chain by construction so pixel suites stay
        // exact; allowOffscreen is the documented opt-in, and forcing each view's
        // OWN pushed description is what makes this a parity test rather than a
        // chain test. If the two descriptions differ, the two pictures differ.
        {
            PostFxDesc ef = efx; ef.allowOffscreen = true; editorView->setPostFx(ef);
            PostFxDesc pf = pfx; pf.allowOffscreen = true; playerView->setPostFx(pf);
            for (int i = 0; i < 8; ++i) {
                engine->renderOneFrame();
                // The mirrors must not push the un-forced description back.
                editorMirror.applyCamera(camera, editorView);
            }
            Image ec, pc;
            CHECK(editorView->readPixels(ec), "editor readback, chain on");
            CHECK(playerView->readPixels(pc), "player readback, chain on");
            probe("editor (chain on)", ec);
            probe("player (chain on)", pc);
            const int lit = maxAbsDiff(ec, pc);
            std::printf("    max |editor - player|, chain on  = %d/255\n", lit);
            CHECK(lit <= 4, "editor and player tonemap identically (the blown-out-player defect)");
            // The chain must actually be doing something, or the test above is
            // vacuous: a filmic tonemap of this scene is not its passthrough.
            const int chainMoved = maxAbsDiff(e, ec);
            std::printf("    max |editor chain off - chain on| = %d/255\n", chainMoved);
            CHECK(chainMoved > 8, "the post chain changed the picture (the comparison is not vacuous)");
        }

        // ---- 4. THE SWITCH (lane PLAYER-1) ----------------------------------
        // The two spaces rendered ALTERNATELY, exactly as a page switch does:
        // one view enabled at a time, the leaving host's end() and the arriving
        // host's begin() in between. This is the pattern that used to migrate
        // the document's graph between two scene managers.
        {
            // Put both views back on their own pushed descriptions (step 3 left
            // allowOffscreen forced on both, which is fine — it is what lets an
            // offscreen view have a chain at all — but the mirror must be able
            // to push again).
            editorMirror.invalidateEnvironment();

            const GiStatus gi0 = editorScene->giStatus();
            const ShadowStatus sh0 = engine->shadowStatus();
            std::printf("    before the switch: gi.rebuilds=%llu atlasRebuilds=%u\n",
                        (unsigned long long)gi0.rebuilds, sh0.atlasRebuilds);

            // --- the EDITOR owns the screen ---------------------------------
            playerView->setEnabled(false);
            editorView->setEnabled(true);
            editorMirror.invalidateEnvironment();          // EngineSceneViewport::begin()
            for (int i = 0; i < 24; ++i) editorFrame(editorMirror, *engine, editorView, camera);
            Image eSwitch;
            CHECK(editorView->readPixels(eSwitch), "editor readback after taking the screen");
            const int reflectorsEditor = editorScene->activePlanarReflectors();

            // --- the PLAYER takes it ----------------------------------------
            editorView->setEnabled(false);
            playerView->setEnabled(true);
            player.begin();                                 // EnginePlayerView::start()
            // The app seeds the arriving view's adaptation history from the one
            // handing over; do the same here or the two pictures differ by a
            // second of auto-exposure and nothing else.
            playerView->seedExposureHistory(editorView->measuredExposureScale());
            for (int i = 0; i < 24; ++i) {
                player.step(1.0f / 60.0f, W, H);
                engine->renderOneFrame();
            }
            Image pSwitch;
            CHECK(playerView->readPixels(pSwitch), "player readback after taking the screen");
            const int reflectorsPlayer = editorScene->activePlanarReflectors();
            std::printf("    planar reflectors rendered: editor %d, player %d\n",
                        reflectorsEditor, reflectorsPlayer);
            // WITHOUT THIS the mirror comparison below is vacuous — two frames
            // with no reflection in them agree perfectly.
            CHECK(reflectorsEditor > 0 && reflectorsPlayer > 0,
                  "the mirror floor really rendered a reflection in BOTH spaces");

            probe("editor (owns the screen)", eSwitch);
            probe("player (owns the screen)", pSwitch);
            std::printf("    content spread: editor %d/255, player %d/255\n",
                        spread(eSwitch), spread(pSwitch));
            CHECK(spread(eSwitch) > 24 && spread(pSwitch) > 24,
                  "both pictures have content (the comparison is not vacuous)");
            const int acrossSwitch = maxAbsDiff(eSwitch, pSwitch);
            std::printf("    max |editor - player| across the switch = %d/255\n", acrossSwitch);
            // TOLERANCE, justified: the two pictures are the same scene, the
            // same camera and the same play state through the same chain, so
            // the only honest sources of difference are the two views' own
            // auto-exposure histories (seeded equal a few lines up, then
            // adapting independently for 24 frames) and the planar mirror's
            // RTT, which is re-rendered per view from that view's camera. 8/255
            // is twice the 4/255 the two static comparisons above hold to, and
            // it is a CEILING, not a target: the measured value is printed.
            CHECK(acrossSwitch <= 8,
                  "the same world looks the same in both spaces across a page switch");

            // THE MIRROR REGION specifically (audit F2). The reflection lives in
            // the bottom half of the frame — the floor plate at y=0 seen from
            // y=2 — and a lost planar binding is not a subtle shift there: the
            // reflection either vanishes (HlmsPbs refuses to sample when the
            // bound arm's camera does not match) or shows the other arm's
            // content. Compared as a REGION so a few pixels of shadow-edge
            // dither cannot pass for a reflection.
            int mirrorWorst = 0;
            for (unsigned y = unsigned(H * 55 / 100); y < unsigned(H * 95 / 100); ++y)
                for (unsigned x = unsigned(W / 5); x < unsigned(W * 4 / 5); ++x) {
                    const Colour a = eSwitch.at(x, y), b = pSwitch.at(x, y);
                    mirrorWorst = std::max({ mirrorWorst,
                                             int(std::fabs(a.r - b.r) * 255.0f + 0.5f),
                                             int(std::fabs(a.g - b.g) * 255.0f + 0.5f),
                                             int(std::fabs(a.b - b.b) * 255.0f + 0.5f) });
                }
            std::printf("    max |editor - player| over the mirror region = %d/255\n", mirrorWorst);
            CHECK(mirrorWorst <= 8, "the planar mirror shows the same thing in both spaces");

            // --- and back to the EDITOR --------------------------------------
            player.end();
            playerView->setEnabled(false);
            editorView->setEnabled(true);
            editorMirror.invalidateEnvironment();
            for (int i = 0; i < 24; ++i) editorFrame(editorMirror, *engine, editorView, camera);

            const GiStatus gi1 = editorScene->giStatus();
            const ShadowStatus sh1 = engine->shadowStatus();
            std::printf("    after the round trip: gi.rebuilds=%llu atlasRebuilds=%u\n",
                        (unsigned long long)gi1.rebuilds, sh1.atlasRebuilds);
            // THE WHOLE POINT OF ONE SCENE. On the two-scene shape this was four
            // from-scratch GI builds per round trip (audit F1/F5).
            CHECK(gi1.rebuilds == gi0.rebuilds,
                  "a page round trip costs NO from-scratch GI rebuild");
            CHECK(sh1.atlasRebuilds == sh0.atlasRebuilds,
                  "a page round trip costs NO shadow-atlas rebuild");

            Image eBack;
            CHECK(editorView->readPixels(eBack), "editor readback after the round trip");
            const int returned = maxAbsDiff(eSwitch, eBack);
            std::printf("    max |editor before - editor after| = %d/255\n", returned);
            CHECK(returned <= 8, "the editor comes back to the picture it left");
        }

        // ---- 5. THE EXPOSURE HAND-OVER (lead review round 2) ----------------
        //
        // Auto-exposure is per view and it ADAPTS, so a second on-screen view
        // of one scene must not start at the authored midpoint and walk to the
        // room's real luminance in front of the user. The hand-over cannot be a
        // plain write at page-show time: the arriving view's chain is still the
        // PASSTHROUGH one, which has no seed pass, so the call used to be a
        // silent no-op and the HDR chain the host's first world push then built
        // seeded ITSELF at 1.0 — the very walk this is meant to remove.
        //
        // The engine remembers a value the graph cannot take yet and spends it
        // on the chain it next builds. Two fresh views prove it, each starting
        // with a brand-new passthrough chain: one handed the editor's converged
        // value BEFORE it has an HDR chain, one not. Then the HDR chain is
        // built and ONE frame is rendered.
        {
            // The editor's own chain must be the HDR one to have a converged
            // value at all; an OFFSCREEN view only keeps a chain with
            // allowOffscreen, which the mirror's own description clears on
            // every push (POST_CHAIN_SPEC §7.3). So force it, settle, read.
            const auto force = [](View *v, PostFxDesc fx) {
                fx.allowOffscreen = true; v->setPostFx(fx);
            };
            force(editorView, editorView->postFx());
            for (int i = 0; i < 24; ++i) engine->renderOneFrame();
            const float editorScale = editorView->measuredExposureScale();
            std::printf("    editor converged exposure = %.4f\n", double(editorScale));
            CHECK(editorScale > 0.0f, "the editor view has a converged exposure to hand over");

            View *seeded  = engine->createOffscreenView("parity-seeded",  W, H, kBackground);
            View *control = engine->createOffscreenView("parity-control", W, H, kBackground);
            CHECK(seeded && control, "two fresh views for the hand-over case");
            if (seeded && control) {
                seeded->setScene(editorScene);
                control->setScene(editorScene);
                editorMirror.applyCamera(camera, seeded, freecam::kFreeCameraFramingAspect);
                editorMirror.applyCamera(camera, control, freecam::kFreeCameraFramingAspect);

                // A BRAND-NEW VIEW HAS THE PASSTHROUGH CHAIN and therefore no
                // seed pass: this write cannot land yet, and there is nothing
                // to read back. It must be REMEMBERED, not dropped.
                seeded->seedExposureHistory(editorScale);
                CHECK(seeded->measuredExposureScale() == 0.0f,
                      "a passthrough view has no exposure to read (the write cannot land yet)");

                // NOW the HDR chain is built on both — in the app this is the
                // host's first applyEnvironment, here the same description
                // pushed by hand (a fresh view's own PostFxDesc has no HDR in
                // it at all) — and one frame is rendered.
                const PostFxDesc worldFx = editorView->postFx();
                force(seeded, worldFx);
                force(control, worldFx);
                engine->renderOneFrame();

                const float seededScale  = seeded->measuredExposureScale();
                const float controlScale = control->measuredExposureScale();
                std::printf("    first HDR frame: seeded %.4f, unseeded %.4f, editor %.4f\n",
                            double(seededScale), double(controlScale), double(editorScale));
                CHECK(seededScale > 0.0f && controlScale > 0.0f,
                      "both fresh views built an HDR chain and graded a frame");
                const float seededErr  = std::fabs(seededScale  - editorScale);
                const float controlErr = std::fabs(controlScale - editorScale);
                // WITHIN 5% on the FIRST HDR frame. Not EQUAL: the frame the
                // seed is spent on also runs one step of the adaptation filter
                // over it, which is what a history is for.
                CHECK(seededErr <= editorScale * 0.05f,
                      "the seeded view's FIRST HDR frame grades at the editor's exposure");
                // ...and the comparison is not vacuous: with nothing handed
                // over the same view starts somewhere else entirely.
                CHECK(controlErr > seededErr * 4.0f,
                      "an unseeded view does NOT (the hand-over is what makes the difference)");

                seeded->setScene(nullptr);
                control->setScene(nullptr);
                engine->destroyView(seeded);
                engine->destroyView(control);
            }
        }

        // ---- 6. THE HOST SCENE IS DESTROYED UNDER THE PLAYER ---------------
        //
        // (lead review round 2.) A project close or an asynchronous open
        // DESTROYS the editor's engine Scene and its SceneMirror, and both are
        // reachable from the Player page — whose View is drawing through them.
        // Engine::destroyScene detaches every view bound to that scene, so
        // nothing looks wrong from the outside: what is left is a host holding
        // two freed pointers and a driver tick about to call setScene() on one
        // and sync() on the other.
        //
        // Driven here rather than in a script because a script cannot make the
        // window happen deterministically (the synchronous open ends in
        // switchSpace(EDITOR); the asynchronous one needs event-loop turns a
        // --script run does not have). Here the scene can simply be destroyed
        // under the host, in the order EngineSceneViewport::clearScene uses.
        {
            View *orphanView = engine->createOffscreenView("parity-orphan", W, H, kBackground);
            Scene *temp = engine->createScene("parity-temp");
            CHECK(orphanView && temp, "a throwaway view and host scene");
            // Its OWN document: binding this suite's `doc` to a second mirror
            // is precisely the graph migration this lane removed.
            auto doc2 = iris::Scene::create();
            auto cam2 = iris::CameraNode::create();
            cam2->setLocalPos(iris::Vec3(0, 2, 6));
            cam2->lookAt(iris::Vec3(0, 0, 0));
            doc2->refresh();
            if (orphanView && temp) {
                EnginePlayerScene orphan(engine);
                {
                    SceneMirror tempMirror(temp);
                    tempMirror.setSource(doc2);
                    orphan.setEditorScene(temp, &tempMirror);
                    CHECK(orphan.attach(orphanView), "the orphan player bound to its host scene");
                    orphan.setDocument(doc2, cam2);
                    orphan.begin();
                    orphan.step(1.0f / 60.0f, W, H);
                    engine->renderOneFrame();
                    CHECK(orphanView->scene() == temp, "...and is drawing it");
                    // THE TEARDOWN, in clearScene's order: mirror first, then
                    // the scene. The host is told nothing.
                    tempMirror.setSource(nullptr);
                }
                engine->destroyScene(temp);
                CHECK(orphanView->scene() == nullptr,
                      "destroyScene detached the view — nothing looks wrong from outside");

                // THE DRIVER TICK. syncFrame re-adopts first, which is this
                // pair of null pointers; everything after it must go quiet
                // rather than touch freed memory.
                orphan.setEditorScene(nullptr, nullptr);
                CHECK(orphan.engineScene() == nullptr, "the player let go of the freed scene");
                CHECK(!orphan.attach(orphanView), "...refuses to bind to nothing");
                orphan.step(1.0f / 60.0f, W, H);
                engine->renderOneFrame();
                CHECK(true, "a frame after the host scene was destroyed is survivable");

                // ...and handed a LIVE scene it draws again, which is what the
                // frame after a project open has to do.
                orphan.setEditorScene(editorScene, &editorMirror);
                CHECK(orphan.attach(orphanView), "bound to the new host scene");
                // THE DOCUMENT GUARD. The mirror now holds this suite's
                // document, but the player was last given doc2 — the window
                // between a project open and the host's push. Stepping it would
                // drive the view from a camera whose graph node lives in a
                // scene manager that has just been destroyed; step() must
                // return before it touches that camera at all. Observed on the
                // first thing step() writes to it, its aspect ratio.
                cam2->setAspectRatio(3.0f);
                orphan.step(1.0f / 60.0f, W, H);
                CHECK(cam2->aspectRatio == 3.0f,
                      "a document the mirror does not hold is NOT stepped");
                orphan.setDocument(doc, camera);
                orphan.begin();
                for (int i = 0; i < 4; ++i) {
                    orphan.step(1.0f / 60.0f, W, H);
                    engine->renderOneFrame();
                }
                Image revived;
                CHECK(orphanView->readPixels(revived), "readback after the re-adopt");
                std::printf("    orphan after re-adopt: spread %d/255\n", spread(revived));
                CHECK(spread(revived) > 24, "the player renders the NEW world");
                orphan.release();
            }
            if (orphanView) engine->destroyView(orphanView);
        }

        player.release();
    }

    engine->destroyView(playerView);
    engine->destroyView(editorView);
    engine->destroyScene(editorScene);
    engine.reset();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    rc = failures ? 1 : 0;
    return rc;
}
