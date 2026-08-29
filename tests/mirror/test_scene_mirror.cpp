// SceneMirror characterisation: an iris:: document renders through the engine.
//
// Builds a document (Scene -> empty parent node -> MeshNode with cube.obj), mirrors
// it into an offscreen engine view and asserts on pixels through every document
// operation the editor performs: move a parent, hide, show, remove.
// No window; runs with DISPLAY reachable (Vulkan). QT_QPA_PLATFORM=offscreen.
#include <QGuiApplication>
#include <QImage>
#include <QDir>
#include <QJsonObject>
#include "graphics/texture2d.h"
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
#include "materials/custommaterial.h"
#include "core/property.h"
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

    // ---- step 4b: a diffuse TEXTURE from the document reaches the engine ----
    {
        const QString pngPath = QDir::temp().filePath("jahshaka_mirror_test_green.png");
        QImage tex(32, 32, QImage::Format_RGBA8888); tex.fill(QColor(20, 230, 40)); tex.save(pngPath);
        auto textured = iris::DefaultMaterial::create();
        textured->setDiffuseColor(QColor(255, 255, 255));                 // white tint: the texture decides
        textured->setDiffuseTexture(iris::Texture2D::load(pngPath));     // deferred: no GL, path recorded
        meshNode2->setMaterial(textured);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("diffuse texture (green)", img);
        CHECK(centre(img).g > centre(img).r * 1.5f && centre(img).g > centre(img).b * 1.5f, "document texture colours the cube");
        textured->setDiffuseTexture(iris::Texture2DPtr());
        textured->setDiffuseColor(QColor(230, 60, 20));
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("texture removed", img);
        CHECK(isMaterial(centre(img)), "removing the texture goes back to the material colour");
        QFile::remove(pngPath);
    }

    // ---- Effects-module CustomMaterial (Default.shader): colour + diffuse texture property ----
    {
        const QString shaderDef = QString(JAHSHAKA_SOURCE_DIR) + "/app/shader_defs/Default.shader";
        auto custom = iris::CustomMaterial::create();
        custom->generate(shaderDef);
        int props = 0; for (auto *p : custom->properties) if (p) ++props;
        std::printf("    Default.shader exposes %d properties\n", props);
        CHECK(props > 5, "CustomMaterial generated its properties from Default.shader without GL");
        custom->setValue("diffuseColor", QColor(230, 40, 20));
        meshNode2->setMaterial(custom);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("CustomMaterial diffuseColor", img);
        CHECK(isMaterial(centre(img)), "CustomMaterial's diffuseColor reaches the engine");
        const QString pngPath = QDir::temp().filePath("jahshaka_mirror_custom_green.png");
        QImage tex(16, 16, QImage::Format_RGBA8888); tex.fill(QColor(20, 230, 40)); tex.save(pngPath);
        custom->setValue("diffuseColor", QColor(255, 255, 255));
        custom->setValue("diffuseTexture", pngPath);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("CustomMaterial diffuseTexture", img);
        CHECK(centre(img).g > centre(img).r * 1.5f && centre(img).g > centre(img).b * 1.5f, "CustomMaterial's diffuseTexture property reaches the engine");
        QFile::remove(pngPath);
        meshNode2->setMaterial(legacy);
    }

    // ---- a grayscale texture samples grey, not red ----
    // Regression: 1-channel files (grayscale jpg/png, e.g. checker.jpg) decoded to
    // an R8 texture, so a black/white checker rendered black/red.
    {
        const QString grayPath = QDir::temp().filePath("jahshaka_mirror_gray.png");
        QImage gray(16, 16, QImage::Format_Grayscale8); gray.fill(230); gray.save(grayPath);
        auto pbr = iris::PbrMaterial::create();
        pbr->setValue("baseColor", QColor(255, 255, 255));
        pbr->setValue("baseColorMap", grayPath);
        meshNode2->setMaterial(pbr);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("grayscale base map", img);
        const Colour c = centre(img);
        std::printf("    grayscale texel at centre: %.2f %.2f %.2f\n", c.r, c.g, c.b);
        CHECK(c.g > c.r * 0.8f && c.b > c.r * 0.8f && c.r > 0.1f,
              "a grayscale image renders grey (all channels), not red");
        QFile::remove(grayPath);
        meshNode2->setMaterial(legacy);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

    // ---- normal-mapped PBR keeps its base colour texture ----
    // Regression: engine meshes carried no tangents, so HlmsPbs threw
    // "Renderable can't use normal maps" and every normal-mapped preset
    // (stone, sand, brick...) fell back to the flat grey default datablock.
    {
        const QString basePath   = QDir::temp().filePath("jahshaka_mirror_nm_base.png");
        const QString normalPath = QDir::temp().filePath("jahshaka_mirror_nm_normal.png");
        QImage base(16, 16, QImage::Format_RGBA8888); base.fill(QColor(20, 230, 40)); base.save(basePath);
        QImage normal(16, 16, QImage::Format_RGBA8888); normal.fill(QColor(128, 128, 255)); normal.save(normalPath);
        auto pbr = iris::PbrMaterial::create();
        pbr->setValue("baseColor", QColor(255, 255, 255));
        pbr->setValue("baseColorMap", basePath);
        pbr->setValue("normalMap", normalPath);
        meshNode2->setMaterial(pbr);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("PBR base + normal map", img);
        CHECK(centre(img).g > centre(img).r * 1.5f && centre(img).g > centre(img).b * 1.5f,
              "a normal-mapped PBR material still renders its base colour map (tangents exist)");
        QFile::remove(basePath); QFile::remove(normalPath);
        meshNode2->setMaterial(legacy);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

    // ---- PBR scene round-trip: params + texture maps survive save/load ----
    // The Option A regression (MATERIALS_EFFECTS_AUDIT.md §0.7a): PbrMaterial
    // declared no Texture/Int properties, so SceneWriter never wrote its maps and
    // SceneReader::readPbrMaterial never restored them — maps were lost on load.
    {
        const QString dir = QDir::temp().filePath("jahshaka_mirror_roundtrip");
        QDir().mkpath(dir);
        const QString pngPath = dir + "/albedo_green.png";
        QImage tex(32, 32, QImage::Format_RGBA8888); tex.fill(QColor(20, 230, 40)); tex.save(pngPath);
        auto saved = iris::PbrMaterial::create();
        saved->setValue("baseColor", QColor(255, 255, 255));
        saved->setValue("metallic", 0.05f);
        saved->setValue("roughness", 0.9f);
        saved->setValue("roughnessLowerBound", 0.2f);
        saved->setValue("roughnessUpperBound", 0.5f);
        saved->setValue("alphaMode", 2);
        saved->setValue("alpha", 0.5f);
        saved->setValue("alphaCutoff", 0.7f);
        saved->setValue("baseColorMap", pngPath);
        CHECK(saved->textures.contains("u_baseColorMap"), "setValue bound the base colour map");

        // Serialise exactly as SceneWriter::writeSceneNodeMaterial does: a values
        // object built from mat->properties by PropertyType, textures as
        // scene-relative paths (the writer's non-database branch). SceneReader
        // itself links half the app (Database, Globals, AssetManager), so this
        // suite replicates the writer/reader JSON contract instead of linking
        // them; the regression guarded here — PbrMaterial not DECLARING the
        // texture/int properties, so they never reach the JSON — trips either way.
        QJsonObject matObj; matObj["materialType"] = "pbr"; matObj["version"] = 2;
        QJsonObject values;
        const QDir sceneDir(dir);
        for (auto *prop : saved->properties) {
            switch (prop->type) {
            case iris::PropertyType::Bool:  values[prop->name] = prop->getValue().toBool(); break;
            case iris::PropertyType::Int:   values[prop->name] = prop->getValue().toInt(); break;
            case iris::PropertyType::Float: values[prop->name] = prop->getValue().toFloat(); break;
            case iris::PropertyType::Color: values[prop->name] = prop->getValue().value<QColor>().name(); break;
            case iris::PropertyType::Texture:
                values[prop->name] = prop->getValue().toString().isEmpty()
                    ? QString() : sceneDir.relativeFilePath(prop->getValue().toString());
                break;
            default: break;
            }
        }
        matObj["values"] = values;
        CHECK(values.contains("baseColorMap") && !values["baseColorMap"].toString().isEmpty(),
              "texture map reaches the saved values");
        CHECK(values.contains("alphaMode") && values["alphaMode"].toInt() == 2,
              "alphaMode reaches the saved values");

        // Read it back the way SceneReader::readPbrMaterial dispatches (textures
        // resolved back to absolute paths against the scene folder).
        auto reloaded = iris::PbrMaterial::create();
        const QJsonObject rvalues = matObj["values"].toObject();
        for (auto *prop : reloaded->properties) {
            if (!rvalues.contains(prop->name)) continue;
            const auto val = rvalues.value(prop->name);
            switch (prop->type) {
            case iris::PropertyType::Float:  reloaded->setValue(prop->name, static_cast<float>(val.toDouble())); break;
            case iris::PropertyType::Int:    reloaded->setValue(prop->name, val.toInt()); break;
            case iris::PropertyType::Color:  reloaded->setValue(prop->name, QColor(val.toString())); break;
            case iris::PropertyType::Bool:   reloaded->setValue(prop->name, val.toBool()); break;
            case iris::PropertyType::Texture:
                reloaded->setValue(prop->name, val.toString().isEmpty() ? QString() : sceneDir.filePath(val.toString()));
                break;
            default: break;
            }
        }
        CHECK(reloaded->useBaseColorMap && reloaded->textures.contains("u_baseColorMap"),
              "reloaded material has its base colour map");
        CHECK(std::fabs(reloaded->roughnessLowerBound - 0.2f) < 1e-4f &&
              std::fabs(reloaded->roughnessUpperBound - 0.5f) < 1e-4f, "roughness bounds round-tripped");
        CHECK(reloaded->alphaMode == 2 && std::fabs(reloaded->alpha - 0.5f) < 1e-4f &&
              std::fabs(reloaded->alphaCutoff - 0.7f) < 1e-4f, "alpha mode/value/cutoff round-tripped");
        PbrParams rp;
        CHECK(SceneMirror::toPbrParams(reloaded.data(), rp), "reloaded PbrMaterial -> PbrParams");
        CHECK(rp.alphaMode == PbrAlphaMode::Blend && std::fabs(rp.alpha - 0.5f) < 1e-4f,
              "alpha mode + value reach the engine params");
        CHECK(std::fabs(rp.roughness - 0.5f) < 1e-4f, "roughness 0.9 clamped into bounds [0.2, 0.5]");

        // Mirror it opaque (unambiguous pixels) and prove the round-tripped
        // texture colours the cube.
        reloaded->setValue("alphaMode", 0);
        meshNode2->setMaterial(reloaded);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("round-tripped PBR texture", img);
        CHECK(centre(img).g > centre(img).r * 1.5f && centre(img).g > centre(img).b * 1.5f,
              "texture from the round-tripped material colours the cube");
        meshNode2->setMaterial(legacy);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        QFile::remove(pngPath);
        QDir().rmdir(dir);
    }

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
    // A green cube under the strong white point light: nothing but the highlight can read as yellow.
    meshNode2->setMaterial(legacy);
    auto countYellow = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.g > 0.6f && c.b < 0.4f) ++n; } return n; };
    mirror.setHighlightedNode(meshNode2);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int yellowOn = countYellow(img);
    std::printf("    highlight on (outline): %d yellow pixels\n", yellowOn);
    CHECK(yellowOn > 10, "selected mesh gets a yellow silhouette outline");
    mirror.setHighlightWireframe(true);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int yellowWire = countYellow(img);
    std::printf("    highlight on (wireframe): %d yellow pixels\n", yellowWire);
    CHECK(yellowWire > 10, "the wireframe style still highlights when toggled on");
    mirror.setHighlightWireframe(false);
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

    // ---- flat sky colour from the document becomes the background ----
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(200, 30, 200);
    mirror.applySky(view);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document sky colour", img);
    CHECK(corner(img).r > 0.6f && corner(img).b > 0.6f && corner(img).g < 0.3f, "document sky colour is the clear colour");

    // ---- cubemap sky from six document face images (createCubeMap keeps the faces) ----
    {
        const QString dir = QDir::temp().filePath("jahshaka_mirror_cube");
        QDir().mkpath(dir);
        const QColor faceCols[6] = { QColor(255,0,0), QColor(0,255,0), QColor(0,0,255), QColor(255,255,0), QColor(255,0,255), QColor(0,255,255) };
        const char *names[6] = { "posx", "negx", "posy", "negy", "posz", "negz" };
        QString paths[6];
        for (int i = 0; i < 6; ++i) { QImage f(8, 8, QImage::Format_RGBA8888); f.fill(faceCols[i]); paths[i] = dir + "/" + names[i] + ".png"; f.save(paths[i]); }
        // createCubeMap(negZ, posZ, posY, negY, negX, posX) — the scene reader's order
        doc->setSkyTexture(iris::Texture2D::createCubeMap(paths[5], paths[4], paths[2], paths[3], paths[1], paths[0]));
        doc->skyType = iris::SkyType::CUBEMAP;
        CHECK(doc->skyTexture && doc->skyTexture->isCubeMap(), "document cubemap keeps its six faces without GL");
        mirror.applySky(view);
        // Look straight down +X with a narrow FOV: the +X face is red.
        cam->setLocalPos(QVector3D(0, 0, 0));
        cam->setLocalRot(QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), -90.0f));
        cam->angle = 20.0f;
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("cubemap sky +X", img);
        CHECK(centre(img).r > 0.8f && centre(img).g < 0.2f && centre(img).b < 0.2f, "+X face of the document cubemap is red");
        cam->setLocalRot(QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), 90.0f));
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("cubemap sky -X", img);
        CHECK(centre(img).g > 0.8f && centre(img).r < 0.2f && centre(img).b < 0.2f, "-X face is green");
        for (int i = 0; i < 6; ++i) QFile::remove(paths[i]);
        QDir().rmdir(dir);
    }

    // ---- gradient sky: baked to an equirect ramp (top colour up, bottom colour down) ----
    {
        doc->skyType = iris::SkyType::GRADIENT;
        doc->gradientTop = QColor(255, 0, 0);
        doc->gradientMid = QColor(0, 255, 0);
        doc->gradientBot = QColor(0, 0, 255);
        doc->gradientOffset = 0.5f;
        mirror.applySky(view);
        cam->setLocalRot(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 89.0f));   // look up
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("gradient sky zenith", img);
        CHECK(centre(img).r > 0.7f && centre(img).g < 0.35f, "gradient sky zenith is the top colour");
        cam->setLocalRot(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -89.0f));  // look down
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("gradient sky nadir", img);
        CHECK(centre(img).b > 0.7f && centre(img).g < 0.35f, "gradient sky nadir is the bottom colour");
    }

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
