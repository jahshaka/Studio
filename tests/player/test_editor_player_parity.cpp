// EDITOR/PLAYER PARITY — the owner's law: the same scene must look the same in
// the editor viewport and in the player. They differ only by editor helpers
// (grid, light wires, gizmos, selection outline) being hidden.
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
#include <QGuiApplication>
#include <QColor>
#include <QQuaternion>
#include <QVector3D>
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

// One frame of BOTH paths, exactly as the app drives them:
//   player: EnginePlayerScene::step()      (the whole player frame)
//   editor: sync + applySky + applyEnvironment + applyCamera
//           (EngineSceneViewport::syncFrame, minus the editor-helper calls)
// The player runs first so both sides see the same document state — PlayBack
// moves the camera and ticks animation inside step().
static void frame(EnginePlayerScene &player, SceneMirror &editorMirror, Engine &engine,
                  View *editorView, const iris::CameraNodePtr &camera, float dt)
{
    player.step(dt, W, H);
    editorMirror.sync();
    editorMirror.applySky(editorView);
    editorMirror.applyEnvironment(editorView, &engine);
    editorMirror.applyCamera(camera, editorView);
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

    // ---- the document: a lit cube on a ground plane, a bright world ----------
    // Every World-panel dial that applyEnvironment owns is moved OFF its default
    // so that a path which skips applyEnvironment cannot accidentally agree.
    auto doc = iris::Scene::create();
    doc->setSkyColor(QColor(30, 40, 70));
    doc->setAmbientColor(QColor(220, 60, 40));    // strongly tinted: flat-ambient parity
    doc->ambientFromSky = false;                  // single-colour sky has nothing to integrate
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
    light->setLocalRot(QQuaternion::fromEulerAngles(-55, 25, 0));
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
    cube->setLocalScale(QVector3D(s, s, s));
    cube->setLocalPos(QVector3D(0, 1, 0));
    doc->getRootNode()->addChild(cube);

    auto camera = iris::CameraNode::create();
    camera->setLocalPos(QVector3D(0, 2, 6));
    camera->lookAt(QVector3D(0, 1, 0));
    camera->angle = 45.0f;
    camera->nearClip = 0.1f;
    camera->farClip = 100.0f;
    doc->update(0);

    // ---- the two halves, each on its own view + engine scene ----------------
    View *editorView = engine->createOffscreenView("parity-editor", W, H, kBackground);
    View *playerView = engine->createOffscreenView("parity-player", W, H, kBackground);
    CHECK(editorView && playerView, "two offscreen views of the same size and background");
    if (!editorView || !playerView) return 1;
    Scene *editorScene = engine->createScene("parity-editor");
    editorView->setScene(editorScene);
    // The editor's engine scene starts at the same hardcoded default the
    // player's does (EngineSceneViewport::ensureEngineScene) — otherwise frame 1
    // would differ for a reason that is not the defect.
    editorScene->setAmbient(Colour(0.25f, 0.27f, 0.32f), Colour(0.15f, 0.15f, 0.18f));

    int rc = 0;
    {
        SceneMirror editorMirror(editorScene);
        editorMirror.setLightWires(false);     // the player never shows them; parity means no helpers
        editorMirror.setSource(doc);

        EnginePlayerScene player(engine);
        CHECK(player.attach(playerView), "player scene attached to its view");
        CHECK(player.engineScene() != editorScene, "the player is a second engine scene");
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
