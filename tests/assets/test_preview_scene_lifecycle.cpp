// EnginePreviewScene, the base class, on its own (ENGINEERING_DEBT_SPEC item 6).
//
// Four preview surfaces (asset viewer, material dock, thumbnail renderer,
// avatar page) now share ONE Scene + mirror + View lifecycle, so the rules that
// used to be re-derived in each copy are asserted here exactly once:
//
//   * the Ogre order — a View exists BEFORE createScene(), and teardown runs
//     views before scenes (Engine.h; getting it wrong segfaults);
//   * attach() is idempotent, and re-binding to a DIFFERENT View moves the
//     Scene instead of rebuilding it (a widget whose native window was
//     recreated must not lose its world);
//   * release() is idempotent, leaves a caller's View alive, destroys an
//     ADOPTED one, and can be followed by another attach();
//   * the hooks fire exactly once per Scene, in the documented places;
//   * renderOffscreen() works on a scene that was NEVER attached (the shot
//     View becomes the first View) and restores the previous one afterwards;
//   * many create/attach/release/destroy cycles leak nothing — this suite runs
//     under ASan/LSan, which is the actual assertion.
//
// Headless: offscreen Views, no widget, no database.
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <cstdio>
#include <memory>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "bridge/enginepreviewscene.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// The smallest possible subclass: a cube under a key light, plus counters for
// every hook so the ORDER and the COUNTS are assertable, not assumed.
class ProbeScene : public EnginePreviewScene
{
public:
    explicit ProbeScene(const std::shared_ptr<Engine> &engine)
        : EnginePreviewScene(engine, "probe", sceneworkers::Tier::MainThread)
    {
        mDocument = iris::Scene::create();
        mDocument->setSkyColor(QColor(20, 20, 20));
        mDocument->setAmbientColor(QColor(190, 190, 190));
        mDocument->shadowEnabled = false;

        auto light = iris::LightNode::create();
        light->setLightType(iris::LightType::Directional);
        light->color = QColor(255, 255, 255);
        light->intensity = 1.0f;
        light->setLocalRot(iris::Quat::fromEulerAngles(45, 45, 0));
        mDocument->rootNode->addChild(light);

        auto cube = iris::MeshNode::create();
        cube->setMesh(":assets/models/cube.obj");
        auto mat = iris::DefaultMaterial::create();
        mat->setDiffuseColor(QColor(230, 40, 40));
        cube->setMaterial(mat);
        mDocument->rootNode->addChild(cube);

        mCamera = iris::CameraNode::create();
        mCamera->setLocalPos(iris::Vec3(0, 0, 5));
        mCamera->lookAt(iris::Vec3(0, 0, 0));
        mCamera->update(0);
        mDocument->setCamera(mCamera);
        mDocument->update(0);
    }
    ~ProbeScene() override { release(); }   // the rule: the subclass tears itself down

    /// The thumbnail renderer's shape: make an offscreen View and hand it to
    /// the base, which then owns it.
    bool makeOwnView(int w, int h)
    {
        auto e = engine();
        if (!e) return false;
        View *own = e->createOffscreenView("probe-own", unsigned(w), unsigned(h), Colour(0, 0, 0, 1));
        if (!own) return false;
        own->setEnabled(false);
        adoptView(own);
        return attach(view());
    }

    QImage shot(int w, int h) { return renderOffscreen("probe-shot", w, h, Colour(0.08f, 0.08f, 0.08f, 1), false); }

    iris::CameraNodePtr camera() const { return mCamera; }
    int scenes = 0, mirrors = 0, views = 0, releases = 0, releasesAlive = 0, prepares = 0;

protected:
    void configureScene(Scene *scene) override
    {
        ++scenes;
        // The mirror must not exist yet: this hook is for once-only Scene state.
        if (mirror()) { std::printf("FAIL: configureScene ran AFTER the mirror was made\n"); ++failures; }
        scene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
    }
    void configureMirror(SceneMirror *m) override
    {
        ++mirrors;
        if (!engineScene()) { std::printf("FAIL: configureMirror ran with no Scene\n"); ++failures; }
        m->setSource(mDocument);
    }
    void configureView(View *v) override { ++views; v->setShadows(false); }
    void releaseSubject(bool sceneAlive) override
    {
        ++releases;
        if (sceneAlive) ++releasesAlive;
        // Everything must still be standing here.
        if (sceneAlive && !mirror()) { std::printf("FAIL: releaseSubject ran after the mirror went\n"); ++failures; }
    }
    void prepareOffscreen(View *v, int w, int h) override
    {
        ++prepares;
        if (view() != v) { std::printf("FAIL: prepareOffscreen: the shot is not the current view\n"); ++failures; }
        mDocument->update(0);
        pushFrame(mCamera, w, h);
    }

private:
    iris::ScenePtr mDocument;
    iris::CameraNodePtr mCamera;
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_preview_scene_lifecycle-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // The editor's viewport exists first in the app: a preview is never the
    // engine's first render target.
    View *primary = engine->createOffscreenView("primary", 64, 64, Colour(0, 0, 0, 1));
    Scene *primaryScene = engine->createScene("primary");
    primary->setScene(primaryScene);

    View *viewA = engine->createOffscreenView("probe-a", 96, 96, Colour(0, 0, 0.5f, 1));
    View *viewB = engine->createOffscreenView("probe-b", 96, 96, Colour(0, 0.5f, 0, 1));
    CHECK(viewA && viewB, "two offscreen views for the probe");
    if (!viewA || !viewB) return 1;

    // ---- 1. attach: the order, the hooks, idempotence ----
    {
        ProbeScene probe(engine);
        CHECK(!probe.attach(nullptr), "attach(nullptr) is refused");
        CHECK(probe.engineScene() == nullptr && probe.view() == nullptr,
              "a refused attach builds nothing");

        CHECK(probe.attach(viewA), "attach to a live View succeeds");
        Scene *const first = probe.engineScene();
        CHECK(first != nullptr, "the engine Scene exists after attach");
        CHECK(probe.view() == viewA, "the bound View is the one asked for");
        CHECK(viewA->scene() == first, "the View renders the probe's Scene");
        CHECK(first != primaryScene, "the probe's Scene is its own");
        CHECK(probe.scenes == 1 && probe.mirrors == 1 && probe.views == 1,
              "each hook ran exactly once");

        CHECK(probe.attach(viewA), "attach to the SAME View again succeeds");
        CHECK(probe.engineScene() == first, "...and does not rebuild the Scene");
        CHECK(probe.scenes == 1 && probe.mirrors == 1 && probe.views == 1,
              "...and runs no hook a second time");

        // A widget whose native window was recreated hands over a new View.
        CHECK(probe.attach(viewB), "re-bind to a DIFFERENT View succeeds");
        CHECK(probe.engineScene() == first, "...the Scene MOVES, it is not rebuilt");
        CHECK(probe.scenes == 1 && probe.mirrors == 1, "...no Scene or mirror hook re-ran");
        CHECK(probe.views == 2, "...but the View hook did");
        CHECK(viewA->scene() == nullptr, "...and the old View was unbound");
        CHECK(viewB->scene() == first, "...and the new one carries the Scene");

        // ---- 2. a frame really renders through the base's push ----
        engine->renderOneFrame();
        Image img;
        CHECK(viewB->readPixels(img) && img.width == 96 && img.height == 96,
              "the probe's Scene renders into the bound View");

        // ---- 3. release ----
        probe.release();
        CHECK(probe.engineScene() == nullptr && probe.view() == nullptr,
              "release() drops the Scene and the View");
        CHECK(probe.releases == 1 && probe.releasesAlive == 1,
              "releaseSubject ran once, while the Scene was still alive");
        CHECK(viewB->scene() == nullptr, "release() unbinds the caller's View");
        // The caller's View is NOT destroyed: it must still be usable.
        viewB->setScene(primaryScene);
        engine->renderOneFrame();
        CHECK(viewB->scene() == primaryScene, "release() left the caller's View alive");
        viewB->setScene(nullptr);

        probe.release();
        CHECK(probe.releases == 2 && probe.releasesAlive == 1,
              "release() is idempotent and the second pass knows the Scene has gone");

        // ---- 4. attach again after release rebuilds ----
        CHECK(probe.attach(viewA), "attach after release succeeds");
        CHECK(probe.engineScene() != nullptr, "...and builds a NEW Scene");
        CHECK(probe.scenes == 2 && probe.mirrors == 2, "...running the Scene hooks again");
        probe.release();
    }
    std::printf("    (probe destroyed)\n");

    // ---- 4b. THE RE-BIND PRODUCTION ACTUALLY PERFORMS ----
    //
    // Case 1 re-binds while BOTH views are alive, which no widget ever does.
    // EngineViewWidget::recreateViewForNewWindow() DESTROYS the old View and
    // then makes the new one (a Vulkan surface cannot be re-pointed), so the
    // Scene's pointer to it is dangling before attach() is ever called again —
    // and attach()'s re-bind branch would unbind freed memory. forgetView(),
    // called from viewAboutToBeDestroyed(), is what makes that path safe, and
    // this is the shape of a Materials Display dock being torn off.
    //
    // MEASURED, with the forgetView() call below removed: the engine hands the
    // replacement View the freed one's address, so the stale setScene(nullptr)
    // lands on the NEW View and "the new View carries the Scene" fails — i.e.
    // the real symptom is a preview that goes BLANK after a re-dock, not a
    // crash. Under ASan it is a use-after-free as well.
    {
        ProbeScene probe(engine);
        View *torn = engine->createOffscreenView("probe-torn", 96, 96, Colour(0, 0, 0.5f, 1));
        CHECK(torn != nullptr, "a View to tear away");
        CHECK(probe.attach(torn), "the preview attaches to its widget's View");
        Scene *const kept = probe.engineScene();

        // The widget's WinIdChange: tell the Scene first, THEN destroy.
        probe.forgetView();
        CHECK(probe.view() == nullptr, "forgetView drops the View");
        CHECK(probe.engineScene() == kept, "...and keeps the Scene and its mirror");
        engine->destroyView(torn);

        View *rebuilt = engine->createOffscreenView("probe-rebuilt", 96, 96, Colour(0, 0.5f, 0, 1));
        CHECK(rebuilt != nullptr, "the widget's new View exists");
        CHECK(probe.attach(rebuilt), "the preview re-attaches to the new View");
        CHECK(probe.engineScene() == kept, "...the same Scene, never rebuilt");
        CHECK(probe.scenes == 1 && probe.mirrors == 1,
              "...and no Scene or mirror hook re-ran across the window swap");
        CHECK(rebuilt->scene() == kept, "...the new View carries the Scene");
        engine->renderOneFrame();
        Image img;
        CHECK(rebuilt->readPixels(img) && img.width == 96,
              "...and it renders, so the swap really completed");
        probe.release();
        engine->destroyView(rebuilt);
    }

    // ---- 4c. forgetView refuses to disown a View this object OWNS ----
    //
    // Nobody else may destroy an adopted View, so "forget it" cannot be
    // honoured: release() is still the only way out, and it must still work.
    {
        ProbeScene probe(engine);
        CHECK(probe.makeOwnView(64, 64), "a probe that owns its View attaches");
        View *const own = probe.view();
        probe.forgetView();
        CHECK(probe.view() == own, "forgetView leaves an OWNED View bound");
        probe.release();
        CHECK(probe.view() == nullptr, "release still destroys the owned View");
    }

    // ---- 5. the destructor tears down through the hooks ----
    {
        ProbeScene *probe = new ProbeScene(engine);
        CHECK(probe->attach(viewA), "heap probe attached");
        Scene *const s = probe->engineScene();
        CHECK(s != nullptr, "heap probe has a Scene");
        delete probe;    // ~ProbeScene -> release() -> the hooks, then ~EnginePreviewScene
        CHECK(viewA->scene() == nullptr, "the destructor unbound the View");
        engine->renderOneFrame();
        std::printf("ok:   a frame after the destructor does not touch the dead Scene\n");
    }

    // ---- 6. renderOffscreen on a NEVER-attached scene ----
    {
        ProbeScene probe(engine);
        const QImage img = probe.shot(64, 48);
        CHECK(!img.isNull() && img.width() == 64 && img.height() == 48,
              "renderOffscreen works before anything was ever attached");
        CHECK(probe.prepares == 1, "...through prepareOffscreen, exactly once");
        CHECK(probe.engineScene() != nullptr,
              "...leaving the Scene it had to create (the shot View was the first View)");
        CHECK(probe.view() == nullptr, "...and no bound View, since the shot one is gone");

        // With a real View bound, the shot must give it back afterwards.
        CHECK(probe.attach(viewA), "bind a real View");
        const QImage second = probe.shot(64, 48);
        CHECK(!second.isNull(), "a second shot renders");
        CHECK(probe.view() == viewA, "...and the previous View is restored");
        CHECK(second == img, "...and the same scene from the same camera is byte-identical");
        probe.release();
    }

    // ---- 7. an ADOPTED View is destroyed by release() ----
    {
        ProbeScene probe(engine);
        CHECK(probe.makeOwnView(80, 80), "a probe that makes its own View attaches");
        CHECK(probe.view() != nullptr && probe.engineScene() != nullptr, "own View and Scene exist");
        probe.release();
        CHECK(probe.view() == nullptr && probe.engineScene() == nullptr,
              "release() dropped the adopted View and the Scene");
        // If the adopted View had leaked, LeakSanitizer would say so at exit;
        // if it had been double-destroyed, this would already have crashed.
        CHECK(probe.makeOwnView(80, 80), "and the probe can make another one");
        probe.release();
    }

    // ---- 8. cycles: create / attach / release / destroy, many times ----
    for (int i = 0; i < 12; ++i) {
        ProbeScene probe(engine);
        if (!probe.attach(i % 2 ? viewB : viewA)) { std::printf("FAIL: cycle %d attach\n", i); ++failures; break; }
        engine->renderOneFrame();
        probe.release();
    }
    std::printf("ok:   12 create/attach/release/destroy cycles\n");
    engine->renderOneFrame();
    {
        Image img;
        CHECK(primary->readPixels(img) && img.width == 64,
              "the primary view still renders after all of it");
    }

    // ---- 9. toQImage rejects a short buffer ----
    {
        struct Peek : EnginePreviewScene {
            using EnginePreviewScene::toQImage;
        };
        Image broken;
        broken.width = 8; broken.height = 8; broken.rgba.resize(4);
        CHECK(Peek::toQImage(broken).isNull(), "toQImage refuses a short pixel buffer");
        Image empty;
        CHECK(Peek::toQImage(empty).isNull(), "toQImage refuses an empty image");
    }

    engine->destroyView(viewA);
    engine->destroyView(viewB);
    primary->setScene(nullptr);
    engine->destroyScene(primaryScene);
    engine->destroyView(primary);
    engine.reset();

    std::printf(failures ? "\n%d FAILURES\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
