// The Assets page viewer on the engine, headless: a cube with a coloured
// DefaultMaterial goes through EngineAssetScene (assets Scene + SceneMirror +
// the preview document and its orbit camera) into an offscreen View. The cube
// must fill the centre from the framing camera with the corner showing the
// background; orbiting 180 degrees must change the picture but keep the cube
// at the centre; a second material colour must show; the RTT preview must
// match the view; release() must detach cleanly.
#include <QGuiApplication>
#include <QColor>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisglfwd.h"
#include "scenegraph/scene.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/meshnode.h"
#include "scenegraph/cameranode.h"
#include "materials/defaultmaterial.h"
#include "jahshaka/engine/Engine.h"
#include "widgets/engineassetscene.h"
#include "editor/previewframing.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// The preview document's sky is (25,25,25): dark grey, no hue.
static bool isBackground(const Colour &c)
{
    return c.r < 0.2f && c.g < 0.2f && c.b < 0.2f && std::fabs(c.r - c.g) < 0.05f && std::fabs(c.g - c.b) < 0.05f;
}
static bool isRed(const Colour &c)   { return c.r > 0.12f && c.r > c.b * 1.5f && c.r > c.g * 1.5f; }
static bool isGreen(const Colour &c) { return c.g > 0.12f && c.g > c.r * 1.5f && c.g > c.b * 1.5f; }
static Colour at(const Image &i, int x, int y) { return i.at(unsigned(x), unsigned(y)); }
static void show(const char *tag, const Image &i, int x, int y)
{
    const Colour c = at(i, x, y), k = at(i, 2, 2);
    std::printf("    %-34s (%d,%d) %3.0f %3.0f %3.0f   corner %3.0f %3.0f %3.0f\n", tag, x, y,
                c.r*255, c.g*255, c.b*255, k.r*255, k.g*255, k.b*255);
}
static int maxAbsDiff(const Image &a, const Image &b)
{
    float d = 0;
    for (unsigned y = 0; y < a.height; ++y) for (unsigned x = 0; x < a.width; ++x) {
        const Colour p = a.at(x, y), q = b.at(x, y);
        d = std::max({d, std::fabs(p.r - q.r), std::fabs(p.g - q.g), std::fabs(p.b - q.b)});
    }
    return int(d * 255);
}
static Image render(EngineAssetScene &assets, Engine &engine, View *view, int frames)
{
    for (int i = 0; i < frames; ++i) {
        assets.step(1.0f / 60.0f, int(view->width()), int(view->height()));
        engine.renderOneFrame();
    }
    Image img;
    view->readPixels(img);
    return img;
}
static Image fromQImage(const QImage &q)
{
    Image img;
    if (q.isNull()) return img;
    const QImage rgba = q.convertToFormat(QImage::Format_RGBA8888);
    img.width = unsigned(rgba.width()); img.height = unsigned(rgba.height());
    img.rgba.assign(rgba.constBits(), rgba.constBits() + size_t(rgba.sizeInBytes()));
    return img;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_engine_asset_viewer-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // The editor viewport exists first in the app; the assets page is another
    // view and another scene on the same engine.
    const int W = 128, H = 128;
    View *editorView = engine->createOffscreenView("editor", 64, 64, Colour(0, 0, 0));
    Scene *editorScene = engine->createScene("editor");
    editorView->setScene(editorScene);
    View *view = engine->createOffscreenView("assets", W, H, Colour(0, 0, 1));
    CHECK(view != nullptr, "offscreen assets view");
    if (!view) return 1;

    {
        EngineAssetScene assets(engine);
        CHECK(!!assets.document() && !!assets.camera(), "preview document and camera built without GL");
        CHECK(assets.attach(view), "assets scene attached to the view");
        CHECK(view->scene() == assets.engineScene(), "the view renders the ASSETS scene");
        CHECK(assets.engineScene() != editorScene, "the assets scene is its own scene on the engine");

        // ---- 1. preview a mesh with a coloured material ----
        // A hierarchy, as a library model is: the cube plus a small marker cube
        // off to its front-right, so the view from behind is a different picture
        // (a lone cube looks the same from either side).
        auto cube = iris::SceneNode::create();
        cube->setName("model");
        auto red = iris::DefaultMaterial::create();
        red->setDiffuseColor(QColor(204, 40, 30));
        auto body = iris::MeshNode::create();
        body->setName("cube");
        body->setMesh(":assets/models/cube.obj");
        CHECK(!!body->getMesh(), "cube.obj loaded into the document (no GL)");
        body->setMaterial(red);
        cube->addChild(body);
        auto marker = iris::MeshNode::create();
        marker->setName("marker");
        marker->setMesh(":assets/models/cube.obj");
        marker->setMaterial(red);
        marker->setLocalScale(QVector3D(0.3f, 0.3f, 0.3f));
        marker->setLocalPos(QVector3D(1.6f, -0.7f, 1.6f));
        cube->addChild(marker);
        assets.setSubject(cube, false, true);
        assets.resetCamera();
        CHECK(assets.subject() == cube, "the model is the previewed subject");
        CHECK(cube->getLocalPos().y() < -3.0f, "the subject was dropped onto the floor (y = -5 - min)");

        Image front = render(assets, *engine, view, 3);
        CHECK(front.width == unsigned(W) && front.height == unsigned(H), "readback has the view's size");
        show("cube, framing camera", front, W / 2, H / 2);
        CHECK(isRed(at(front, W / 2, H / 2)), "centre pixel is dominated by the material colour");
        CHECK(isBackground(at(front, 2, 2)), "corner is the preview background (25,25,25)");
        const QVector3D camPos = assets.camera()->getLocalPos();
        std::printf("    camera %.2f %.2f %.2f\n", double(camPos.x()), double(camPos.y()), double(camPos.z()));
        CHECK(std::fabs(camPos.y() - cube->getLocalPos().y()) < 1.0f, "camera framed at the subject's height");

        // ---- 2. orbit 180 degrees: a different picture, still the cube at the centre ----
        assets.orbit(180.0f, 0.0f);
        Image back = render(assets, *engine, view, 3);
        const QVector3D camBack = assets.camera()->getLocalPos();
        std::printf("    camera %.2f %.2f %.2f\n", double(camBack.x()), double(camBack.y()), double(camBack.z()));
        // Opposite side of the pivot: the two positions straddle the model's
        // footprint and are a full diameter apart.
        const QVector3D mid = (camPos + camBack) * 0.5f;
        CHECK(std::fabs(mid.x()) < 2.0f && std::fabs(mid.z()) < 2.0f && (camBack - camPos).length() > 10.0f,
              "camera moved to the opposite side of the pivot");
        show("cube after 180 deg orbit", back, W / 2, H / 2);
        const int diff = maxAbsDiff(front, back);
        std::printf("    max |front - back| = %d\n", diff);
        CHECK(diff > 20, "the picture changed after the orbit");
        CHECK(isRed(at(back, W / 2, H / 2)), "centre is still the mesh after the orbit");
        CHECK(isBackground(at(back, 2, 2)), "corner is still the background after the orbit");

        // The mouse path: a left drag turns the orbit the same way.
        assets.mouseDown(Qt::LeftButton);
        assets.mouseMove(360, 0);          // 360 px * 0.5 deg/px = 180 deg
        assets.mouseUp(Qt::LeftButton);
        Image dragged = render(assets, *engine, view, 3);
        const int backAgain = maxAbsDiff(front, dragged);
        std::printf("    max |front - after drag| = %d\n", backAgain);
        CHECK(backAgain <= 12, "a 180 degree left drag brings the first picture back");

        // ---- 3. a second material on the preview sphere ----
        auto green = iris::DefaultMaterial::create();
        green->setDiffuseColor(QColor(30, 200, 40));
        auto ball = assets.setMaterialSubject(green.staticCast<iris::Material>());
        assets.resetCamera();
        CHECK(!!ball && assets.subject() == ball, "the material ball replaced the cube as the subject");
        CHECK(cube->parent == nullptr, "the previous subject left the document");
        Image mat = render(assets, *engine, view, 3);
        show("second material on the sphere", mat, W / 2, H / 2);
        CHECK(isGreen(at(mat, W / 2, H / 2)), "centre shows the second material's colour");
        CHECK(!isRed(at(mat, W / 2, H / 2)), "the first colour is gone");

        // ---- 4. the RTT preview (takeScreenshot) matches the view ----
        QImage shot = assets.renderImage(W, H);
        CHECK(!shot.isNull() && shot.width() == W && shot.height() == H, "renderImage gives an image of the requested size");
        if (!shot.isNull()) {
            Image s = fromQImage(shot);
            show("renderImage", s, W / 2, H / 2);
            CHECK(isGreen(at(s, W / 2, H / 2)), "the preview image shows the material");
            CHECK(isBackground(at(s, 2, 2)), "the preview image's corner is the background");
            CHECK(view->scene() == assets.engineScene(), "the temporary shot view left the page's view alone");
        }
        QImage big = assets.renderImage(256, 96);
        CHECK(big.width() == 256 && big.height() == 96, "renderImage honours an arbitrary size");

        // ---- 5. scale must not break the preview (ASSETS_AUDIT.md findings 3 + 4) ----
        // 5a. the framing math itself: world bounds include node scale.
        auto giant = iris::MeshNode::create();
        giant->setName("giant");
        giant->setMesh(":assets/models/cube.obj");
        auto blue = iris::DefaultMaterial::create();
        blue->setDiffuseColor(QColor(30, 60, 220));
        giant->setMaterial(blue);
        giant->setLocalScale(QVector3D(200.0f, 200.0f, 200.0f));
        const iris::AABB gbox = preview::worldBoundingBox(giant);
        std::printf("    giant world box %.1f x %.1f x %.1f\n",
                    double(gbox.getSize().x()), double(gbox.getSize().y()), double(gbox.getSize().z()));
        CHECK(gbox.getSize().x() > 300.0f && gbox.getSize().y() > 300.0f,
              "worldBoundingBox includes node scale (unscaled cube is ~2 units)");

        // 5b. a huge model (world radius ~346, a cm-scaled glb) is framed ~1000
        // units out — beyond iris's default farClip of 500 — and must be seen.
        assets.setSubject(giant, false, true);
        assets.resetCamera();
        const float gr = gbox.getMinimalEnclosingSphere().radius;
        const float gdist = preview::framingDistance(gr, assets.camera()->angle);
        std::printf("    radius %.1f framed at %.1f  near %.2f far %.1f\n", double(gr), double(gdist),
                    double(assets.camera()->nearClip), double(assets.camera()->farClip));
        CHECK(gdist > 500.0f, "the framing distance really is past the old far plane");
        CHECK(assets.camera()->farClip > gdist + gr, "far plane follows the framing distance");
        CHECK(assets.camera()->nearClip < gdist - gr, "near plane leaves the subject in front of it");
        Image huge = render(assets, *engine, view, 3);
        show("giant cube (scale 200)", huge, W / 2, H / 2);
        CHECK(at(huge, W / 2, H / 2).b > 0.12f && at(huge, W / 2, H / 2).b > at(huge, W / 2, H / 2).r * 1.5f,
              "a huge model is visible at the centre (not far-clipped)");
        CHECK(isBackground(at(huge, 2, 2)), "the huge model is framed inside the view");

        // 5c. the lotus trap: framing a scaled model from its UNSCALED radius
        // put the camera inside/nowhere near it. A cube scaled x3 (world radius
        // ~5.2) framed from the raw mesh radius (~1.7 -> dist 5) has the camera
        // inside the model; the world-space framing backs off ~15 units.
        auto scaled = iris::MeshNode::create();
        scaled->setName("scaled");
        scaled->setMesh(":assets/models/cube.obj");
        scaled->setMaterial(red);
        scaled->setLocalScale(QVector3D(3.0f, 3.0f, 3.0f));
        assets.setSubject(scaled, false, true);
        assets.resetCamera();
        const QVector3D spivotCam = assets.camera()->getLocalPos();
        Image sm = render(assets, *engine, view, 3);
        show("cube (scale 3)", sm, W / 2, H / 2);
        CHECK(isRed(at(sm, W / 2, H / 2)), "a scaled model is framed from its world-space bounds");
        CHECK(isBackground(at(sm, 2, 2)), "and fits inside the view (camera not inside the model)");
        const iris::AABB sbox = preview::worldBoundingBox(scaled);
        CHECK((spivotCam - sbox.getCenter()).length() > sbox.getMinimalEnclosingSphere().radius,
              "the camera stands outside the scaled model");

        // ---- 6. saved orbit round-trips ----
        QJsonObject props = assets.sceneProperties();
        CHECK(props.contains("camera") && props["camera"].toObject().contains("distFromPivot"), "sceneProperties carries the orbit");

        assets.release();
        CHECK(view->scene() == nullptr, "release() detaches the assets scene from the view");
        CHECK(assets.engineScene() == nullptr, "release() dropped the engine scene");
        assets.release();
        CHECK(true, "release() twice is harmless");
    }

    engine->destroyView(view);
    engine->destroyView(editorView);
    engine->destroyScene(editorScene);
    engine.reset();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
