// SceneMirror characterisation: an iris:: document renders through the engine.
//
// Builds a document (Scene -> empty parent node -> MeshNode with cube.obj), mirrors
// it into an offscreen engine view and asserts on pixels through every document
// operation the editor performs: move a parent, hide, show, remove.
// No window; runs with DISPLAY reachable (Vulkan). QT_QPA_PLATFORM=offscreen.
#include <QGuiApplication>
#include <QVector3D>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisglfwd.h"
#include "scenegraph/scene.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/meshnode.h"
#include "scenegraph/lightnode.h"
#include "graphics/mesh.h"
#include "materials/defaultmaterial.h"
#include "materials/pbrmaterial.h"
#include "scenegraph/cameranode.h"
#include "jahshaka/engine/Engine.h"
#include "engine/scenemirror.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static Colour centre(const Image &i) { return i.at(i.width / 2, i.height / 2); }
static Colour corner(const Image &i) { return i.at(2, 2); }
static bool isBlue(const Colour &c) { return c.b > 0.8f && c.r < 0.15f && c.g < 0.15f; }
// The document material is orange/red: red must dominate and be clearly lit.
static bool isMaterial(const Colour &c) { return c.r > 0.12f && c.r > c.b * 1.5f && c.r > c.g * 1.5f; }
static void show(const char *tag, const Image &i) {
    const Colour c = centre(i), k = corner(i);
    std::printf("    %-28s centre %3.0f %3.0f %3.0f   corner %3.0f %3.0f %3.0f\n", tag,
                c.r*255, c.g*255, c.b*255, k.r*255, k.g*255, k.b*255);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_scene_mirror-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("mirror", 96, 96, Colour(0, 0, 1));
    Scene *target = engine->createScene("mirror");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    view->setCameraPosition(Vec3(2.2f, 1.8f, 2.6f));
    view->lookAt(Vec3(0, 0, 0));

    // ---- the document ----
    auto doc = iris::Scene::create();
    auto parent = iris::SceneNode::create();
    parent->setName("parent");
    doc->getRootNode()->addChild(parent);
    auto meshNode = iris::MeshNode::create();
    meshNode->setName("cube");
    meshNode->setMesh(":assets/models/cube.obj");
    auto legacyOrange = iris::DefaultMaterial::create();
    legacyOrange->setDiffuseColor(QColor(204, 76, 51));   // the document decides the colour now
    meshNode->setMaterial(legacyOrange);
    CHECK(!!meshNode->getMesh(), "cube.obj loaded into the document (no GL)");
    const float r = meshNode->getMeshRadius();
    const float s = r > 0.0f ? 1.0f / r : 1.0f;      // normalise to unit radius
    meshNode->setLocalScale(QVector3D(s, s, s));
    parent->addChild(meshNode);
    auto light = iris::LightNode::create();
    light->setName("sun");
    light->intensity = 1.0f;
    light->setLocalRot(QQuaternion::fromEulerAngles(-50.0f, 30.0f, 0.0f));
    doc->getRootNode()->addChild(light);

    MeshData md;
    CHECK(SceneMirror::toMeshData(meshNode->getMesh().data(), md), "iris::Mesh -> MeshData");
    std::printf("    cube.obj: %zu vertices, %zu triangles, normals=%s uvs=%s\n",
                md.vertexCount(), md.triangleCount(), md.normals.empty() ? "no" : "yes", md.uvs.empty() ? "no" : "yes");

    // ---- mirror + render ----
    SceneMirror mirror(target);
    mirror.setSource(doc);
    int n = mirror.sync();
    CHECK(n == 3, "sync mirrored 3 document nodes (parent, cube, light)");
    CHECK(mirror.engineNode(meshNode.data()) != 0, "cube has an engine node");
    for (int i = 0; i < 3; ++i) engine->renderOneFrame();
    Image img;
    CHECK(view->readPixels(img), "readPixels");
    show("initial", img);
    CHECK(isBlue(corner(img)), "corner is the clear colour");
    CHECK(isMaterial(centre(img)), "centre is the mirrored cube");

    // ---- move the PARENT: the child must follow through the engine hierarchy ----
    parent->setLocalPos(QVector3D(10.0f, 0.0f, 0.0f));
    mirror.sync();
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("parent moved +10x", img);
    CHECK(isBlue(centre(img)), "cube left the view when its PARENT moved");

    parent->setLocalPos(QVector3D(0, 0, 0));
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("parent back", img);
    CHECK(isMaterial(centre(img)), "cube is back");

    // ---- visibility ----
    meshNode->visible = false;
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("hidden", img);
    CHECK(isBlue(centre(img)), "hidden node renders nothing");
    meshNode->visible = true;
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("shown", img);
    CHECK(isMaterial(centre(img)), "shown again");

    // ---- re-parent in the document: cube moves under a second, offset node ----
    auto other = iris::SceneNode::create();
    other->setLocalPos(QVector3D(0.0f, 10.0f, 0.0f));
    doc->getRootNode()->addChild(other);
    parent->removeChild(meshNode);
    other->addChild(meshNode, false);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("re-parented +10y", img);
    CHECK(isBlue(centre(img)), "re-parenting in the document moved the cube in the engine");
    other->setLocalPos(QVector3D(0, 0, 0));
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("new parent at origin", img);
    CHECK(isMaterial(centre(img)), "cube visible under its new parent");

    // ---- remove from the document ----
    other->removeChild(meshNode);
    n = mirror.sync();
    CHECK(n == 3, "3 nodes remain (parent, other, light)");
    CHECK(mirror.engineNode(meshNode.data()) == 0, "removed node has no engine node");
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("removed", img);
    CHECK(isBlue(centre(img)), "removed node renders nothing");

    // ---- step 4: material colour comes from the DOCUMENT ----
    auto meshNode2 = iris::MeshNode::create();
    meshNode2->setMesh(":assets/models/cube.obj");
    meshNode2->setLocalScale(QVector3D(s, s, s));
    auto pbr = iris::PbrMaterial::create();
    pbr->setBaseColor(QColor(30, 80, 230));      // blue-ish
    pbr->setMetallicFactor(0.0f);
    pbr->setRoughnessFactor(0.7f);
    meshNode2->setMaterial(pbr);
    doc->getRootNode()->addChild(meshNode2);
    PbrParams pp;
    CHECK(SceneMirror::toPbrParams(pbr.data(), pp), "PbrMaterial -> PbrParams");
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document PbrMaterial", img);
    CHECK(centre(img).b > centre(img).r, "centre takes the document material's colour (blue)");
    // Edit the material in the document (what the property panel does) -> engine follows.
    pbr->setBaseColor(QColor(230, 60, 20));
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("material edited -> red", img);
    CHECK(isMaterial(centre(img)), "runtime material edit reached the engine");
    // Legacy DefaultMaterial maps too.
    auto legacy = iris::DefaultMaterial::create();
    legacy->setDiffuseColor(QColor(20, 200, 40));
    meshNode2->setMaterial(legacy);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("DefaultMaterial green", img);
    CHECK(centre(img).g > centre(img).r && centre(img).g > centre(img).b, "DefaultMaterial diffuse -> albedo");

    // ---- step 5: a POINT light on a document node lights the side it is on ----
    // Remove the sun so only the point light matters; drop ambient to make it obvious.
    doc->getRootNode()->removeChild(light);
    target->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
    auto point = iris::LightNode::create();
    point->lightType = iris::LightType::Point;
    point->intensity = 4.0f;
    point->distance = 20.0f;
    point->color = QColor(255, 255, 255);
    point->setLocalPos(QVector3D(4.0f, 1.0f, 2.5f));   // camera-right of the cube
    doc->getRootNode()->addChild(point);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    auto lum = [&](unsigned x, unsigned y) { const Colour c = img.at(x, y); return c.r + c.g + c.b; };
    const float rightSide = lum(img.width * 3 / 4, img.height / 2), leftSide = lum(img.width / 4, img.height / 2);
    std::printf("    point light right: left-of-frame %.2f  right-of-frame %.2f\n", leftSide, rightSide);
    CHECK(rightSide > leftSide + 0.05f, "point light on the right lights the right side more");
    point->setLocalPos(QVector3D(-4.0f, 1.0f, 2.5f));   // move the light node to the left
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const float rightSide2 = lum(img.width * 3 / 4, img.height / 2), leftSide2 = lum(img.width / 4, img.height / 2);
    std::printf("    point light left:  left-of-frame %.2f  right-of-frame %.2f\n", leftSide2, rightSide2);
    CHECK(leftSide2 > rightSide2 + 0.05f, "moving the light NODE in the document moves the light");

    // ---- document camera drives the view ----
    target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(0.0f, 0.0f, 4.0f));       // straight in front, looking -Z
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 100.0f;
    doc->getRootNode()->addChild(cam);
    mirror.applyCamera(cam, view);
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document camera", img);
    CHECK(!isBlue(centre(img)), "document camera sees the cube");
    cam->setLocalPos(QVector3D(0.0f, 20.0f, 4.0f));      // way above: cube leaves the centre
    mirror.applyCamera(cam, view);
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document camera moved", img);
    CHECK(isBlue(centre(img)), "moving the document camera moves the view");

    // ---- selection highlight (on-top wireframe) and light wires ----
    cam->setLocalPos(QVector3D(2.2f, 1.8f, 2.6f)); cam->lookAt(QVector3D(0, 0, 0));
    mirror.applyCamera(cam, view);
    auto countYellow = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.g > 0.6f && c.b < 0.4f) ++n; } return n; };
    mirror.setHighlightedNode(meshNode2);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int yellowOn = countYellow(img);
    std::printf("    highlight on: %d yellow wireframe pixels\n", yellowOn);
    CHECK(yellowOn > 10, "selected mesh gets a yellow wireframe highlight");
    mirror.setHighlightedNode(nullptr);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countYellow(img) == 0, "highlight cleared");

    doc->getRootNode()->removeChild(meshNode2);
    point->color = QColor(255, 0, 255);              // magenta wires
    point->setLocalPos(QVector3D(0.0f, 0.0f, 0.0f));  // in the middle of the frame
    auto countMagenta = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.b > 0.8f && c.g < 0.3f) ++n; } return n; };
    mirror.setLightWires(true);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int wiresOn = countMagenta(img);
    std::printf("    light wires on: %d magenta pixels\n", wiresOn);
    CHECK(wiresOn > 10, "point light draws rings in its colour");
    mirror.setLightWires(false);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countMagenta(img) == 0, "light wires off");

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
