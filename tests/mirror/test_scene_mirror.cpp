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
#include <QThread>
#include "irisgl/document/assets/texture2d.h"
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"

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
    enginetest::testCameraLookAt(view, Vec3(2.2f, 1.8f, 2.6f), Vec3(0, 0, 0));

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
    // Off-origin, out of frame: a directional light's position never affects
    // lighting, but its helper icon billboard would otherwise sit at the origin
    // and trip every "centre is background" assertion below.
    light->setLocalPos(QVector3D(0.0f, 6.0f, 0.0f));
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
    }

    // ---- Glass alpha mode keeps its specular while Fade dims everything ----
    // Regression: authored glass used Ogre's Fade ("just fading out an object");
    // alphaMode 3 = Glass maps to Transparent, which preserves specular/reflections.
    {
        auto centreLum = [](const Image &im) {   // 11x11 patch: guaranteed cube pixels
            float sum = 0; int n = 0;
            for (unsigned y = im.height / 2 - 5; y <= im.height / 2 + 5; ++y)
                for (unsigned x = im.width / 2 - 5; x <= im.width / 2 + 5; ++x) {
                    const Colour c = im.at(x, y); sum += c.r + c.g + c.b; ++n;
                }
            return sum / n;
        };
        mirror.setLightWires(false);   // the white wire billboard saturates maxLum in both modes
        // Glass shows its nature via ENVIRONMENT reflections (a flat cube face rarely
        // mirrors a directional light into the camera): bright cubemap sky -> the
        // engine binds it as the PBR reflection map; Glass keeps it, Fade dims it.
        const QString skyDir = QDir::temp().filePath("jahshaka_mirror_glass_sky");
        QDir().mkpath(skyDir);
        QString facePaths[6];
        for (int i = 0; i < 6; ++i) {
            QImage f(8, 8, QImage::Format_RGBA8888); f.fill(QColor(235, 235, 235));
            facePaths[i] = skyDir + QString("/f%1.png").arg(i); f.save(facePaths[i]);
        }
        doc->setSkyTexture(iris::Texture2D::createCubeMap(facePaths[0], facePaths[1], facePaths[2],
                                                          facePaths[3], facePaths[4], facePaths[5]));
        doc->skyType = iris::SkyType::CUBEMAP;
        mirror.applySky(view);
        auto glassMat = iris::PbrMaterial::create();
        glassMat->setValue("baseColor", QColor(238, 244, 248));
        glassMat->setValue("roughness", 0.05f);
        glassMat->setValue("metallic", 0.0f);
        glassMat->setValue("alpha", 0.3f);
        glassMat->setValue("alphaMode", 2);                      // Fade first
        meshNode2->setMaterial(glassMat);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("blend (fade) glass", img);
        const float fadeLum = centreLum(img);
        glassMat->setValue("alphaMode", 3);                      // Glass
        PbrParams gp;
        CHECK(SceneMirror::toPbrParams(glassMat.data(), gp) && gp.alphaMode == PbrAlphaMode::Glass,
              "document alphaMode 3 maps to PbrAlphaMode::Glass");
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("glass (transparent)", img);
        const float glassLum = centreLum(img);
        // Fade blends 70% bright background through the surface; Transparent
        // premultiplies the diffuse and shows more of the surface itself. The exact
        // ordering is a render detail — what the regression pins is that alphaMode 3
        // takes a DIFFERENT Ogre transparency path than alphaMode 2 (it used to be
        // the same Fade, which is why authored glass looked merely faded).
        std::printf("    centre-patch luminance: fade %.3f vs glass %.3f\n", fadeLum, glassLum);
        CHECK(std::fabs(glassLum - fadeLum) > 0.25f,
              "Glass (alphaMode 3) renders through a different transparency path than Blend/Fade");
        mirror.setLightWires(true);
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        doc->skyColor = QColor(0, 0, 255);           // restore the suite's blue clear colour
        doc->setSkyTexture(iris::Texture2DPtr());
        mirror.applySky(view);
        for (int i = 0; i < 6; ++i) QFile::remove(facePaths[i]);
        QDir().rmdir(skyDir);
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

    // ---- the outline colour follows the document's preference ----
    // MainWindow pushes the Preferences outline colour onto scene->outlineColor;
    // the mirror reads it (fallback: the legacy yellow when never set).
    auto countRed = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.g < 0.3f && c.b < 0.3f) ++n; } return n; };
    doc->setOutlineColor(QColor(255, 0, 0));
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int redOutline = countRed(img);
    std::printf("    outline colour preference: %d red pixels, %d yellow\n", redOutline, countYellow(img));
    CHECK(redOutline > 10, "outline uses the document's outlineColor preference");
    CHECK(countYellow(img) == 0, "the default yellow is replaced by the preference");
    doc->setOutlineColor(QColor());   // invalid = preference never set
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countYellow(img) > 10, "an unset preference falls back to the legacy yellow");

    mirror.setHighlightedNode(nullptr);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countYellow(img) == 0, "highlight cleared");

    // ---- a GROUP selection outlines the whole subtree ----
    // Selecting a multi-part asset's ROOT must outline every descendant mesh;
    // before, only a single MeshNode selection drew anything.
    {
        cam->setLocalPos(QVector3D(0.0f, 0.8f, 5.0f)); cam->lookAt(QVector3D(0, 0, 0));
        mirror.applyCamera(cam, view);
        auto group = iris::SceneNode::create();
        auto makePart = [&](float x) {
            auto part = iris::MeshNode::create();
            part->setMesh(":assets/models/cube.obj");
            part->setLocalScale(QVector3D(s, s, s));
            part->setLocalPos(QVector3D(x, 0, 0));
            part->setMaterial(legacy);
            part->setAttached(true);
            group->addChild(part, false);
            return part;
        };
        makePart(-1.4f); makePart(1.4f);
        doc->getRootNode()->addChild(group);
        mirror.setHighlightedNode(group);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        auto countYellowIn = [&](unsigned x0, unsigned x1) { int nn = 0; for (unsigned y = 0; y < img.height; ++y) for (unsigned x = x0; x < x1; ++x) { const Colour c = img.at(x, y); if (c.r > 0.8f && c.g > 0.6f && c.b < 0.4f) ++nn; } return nn; };
        const int leftY = countYellowIn(0, img.width / 2), rightY = countYellowIn(img.width / 2, img.width);
        std::printf("    group outline: %d yellow left, %d yellow right\n", leftY, rightY);
        CHECK(leftY > 10 && rightY > 10, "group selection outlines EVERY descendant mesh (both parts)");
        mirror.setHighlightWireframe(true);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const int leftW = countYellowIn(0, img.width / 2), rightW = countYellowIn(img.width / 2, img.width);
        std::printf("    group wireframe: %d yellow left, %d yellow right\n", leftW, rightW);
        CHECK(leftW > 10 && rightW > 10, "the wireframe toggle covers the whole group");
        mirror.setHighlightWireframe(false);
        mirror.setHighlightedNode(nullptr);
        doc->getRootNode()->removeChild(group);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countYellow(img) == 0, "group highlight cleared");
        cam->setLocalPos(QVector3D(2.2f, 1.8f, 2.6f)); cam->lookAt(QVector3D(0, 0, 0));
        mirror.applyCamera(cam, view);
    }

    doc->getRootNode()->removeChild(meshNode2);
    point->color = QColor(255, 0, 255);              // magenta wires
    point->setLocalPos(QVector3D(0.0f, 0.0f, 0.0f));  // in the middle of the frame
    point->distance = 0.35f;                          // rings are drawn at radius = range now
    auto countMagenta = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.b > 0.8f && c.g < 0.3f) ++n; } return n; };
    mirror.setLightWires(true);
    // Attenuation volumes are selection-gated (the Unreal convention): an
    // UNSELECTED point light shows only its icon, no range rings.
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countMagenta(img) == 0, "unselected point light draws no rings (icon only)");
    mirror.setHighlightedNode(point);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const int wiresOn = countMagenta(img);
    std::printf("    light wires on (selected): %d magenta pixels\n", wiresOn);
    CHECK(wiresOn > 10, "the SELECTED point light draws rings in its colour");
    mirror.setLightWires(false);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countMagenta(img) == 0, "light wires off");

    // ---- light icon billboard + range-scaled wires ----
    // The icon rides the light-wires toggle (one "show light helpers" concept);
    // the rings are sized by the light's range (distance), like Unreal's
    // falloff sphere. The icon is engine-side only: white glyph, alpha-blended.
    {
        auto countWhite = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.85f && c.g > 0.85f && c.b > 0.85f) ++n; } return n; };
        auto magentaExtent = [&](const Image &im) { int minX = int(im.width), maxX = -1; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.b > 0.8f && c.g < 0.3f) { if (int(x) < minX) minX = int(x); if (int(x) > maxX) maxX = int(x); } } return maxX - minX; };
        // Icons are always-on with the helpers toggle, selected or not.
        mirror.setLightWires(true);
        mirror.setHighlightedNode(nullptr);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        std::printf("    unselected: %d white icon px, %d magenta px\n", countWhite(img), countMagenta(img));
        CHECK(countWhite(img) > 5, "unselected point light still shows its icon");
        CHECK(countMagenta(img) == 0, "unselected point light shows no rings");
        mirror.setHighlightedNode(point);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const int iconOn = countWhite(img);
        const int extentSmall = magentaExtent(img);
        std::printf("    light icon on: %d white pixels, ring extent %d px\n", iconOn, extentSmall);
        CHECK(iconOn > 5, "point light shows an icon billboard at its position");
        CHECK(extentSmall > 0, "range-scaled rings are in frame");

        // A document-supplied icon image (mainwindow loads :/icons/*.png; here a
        // real file, since Qt resources are not compiled into the tests).
        const QString iconPath = QDir::temp().filePath("jahshaka_mirror_light_icon.png");
        {
            QImage ic(16, 16, QImage::Format_RGBA8888); ic.fill(Qt::transparent);
            for (int y = 4; y < 12; ++y) for (int x = 4; x < 12; ++x) ic.setPixelColor(x, y, QColor(255, 255, 255, 255));
            ic.save(iconPath);
        }
        point->icon = iris::Texture2D::load(iconPath);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        std::printf("    document icon file: %d white pixels\n", countWhite(img));
        CHECK(countWhite(img) > 5, "the document's own icon image renders at the light");

        // Range change scales the ring wires (the visible extent grows).
        point->distance = 0.9f;
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const int extentLarge = magentaExtent(img);
        std::printf("    ring extent: range 0.35 -> %d px, range 0.9 -> %d px\n", extentSmall, extentLarge);
        CHECK(extentLarge > extentSmall + 5, "the ring wires scale with the light's range");

        // Wires OFF removes the icon too.
        mirror.setLightWires(false);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countMagenta(img) == 0 && countWhite(img) == 0, "light wires off removes the icon too");
        QFile::remove(iconPath);
        mirror.setHighlightedNode(nullptr);

        // Point-light shadow controls (the panel unhides Type/Size in engine
        // mode): the Shadow Type combo drives castShadows through toLightDesc.
        point->shadowMap->shadowType = iris::ShadowMapType::Soft;
        CHECK(SceneMirror::toLightDesc(point.data()).castShadows, "point light Shadow Type=Soft casts shadows");
        point->shadowMap->shadowType = iris::ShadowMapType::None;
        CHECK(!SceneMirror::toLightDesc(point.data()).castShadows, "point light Shadow Type=None stops casting");
    }

    // ---- area light: serialised fields flow through toLightDesc and light the scene ----
    {
        // The io round-trip: SceneWriter::writeLightData emits exactly these keys and
        // SceneReader::createLight reads them back with these defaults (linking the
        // real io stack drags the whole app in, so the suite replicates the keys —
        // the same precedent as the material serialisation block above).
        QJsonObject j;
        j["lightType"] = "area"; j["rectWidth"] = 2.0; j["rectHeight"] = 0.5;
        j["doubleSided"] = true; j["accurate"] = true;
        auto fromJson = iris::LightNode::create();
        fromJson->setLightType(j["lightType"].toString() == "area" ? iris::LightType::Area
                                                                   : iris::LightType::Point);
        fromJson->rectWidth = (float)j["rectWidth"].toDouble(1.0f);
        fromJson->rectHeight = (float)j["rectHeight"].toDouble(1.0f);
        fromJson->doubleSided = j["doubleSided"].toBool(false);
        fromJson->accurate = j["accurate"].toBool(false);
        fromJson->shadowMap->shadowType = iris::ShadowMapType::Soft;   // hostile: must be ignored
        const LightDesc ad = SceneMirror::toLightDesc(fromJson.data());
        CHECK(ad.type == LightType::Area, "document area light maps to LightType::Area");
        CHECK(std::abs(ad.rectWidth - 2.0f) < 1e-5f && std::abs(ad.rectHeight - 0.5f) < 1e-5f,
              "rect size flows through toLightDesc");
        CHECK(ad.doubleSided && ad.accurate, "doubleSided + accurate flow through toLightDesc");
        CHECK(!ad.castShadows, "area lights never cast shadows, whatever Shadow Type says");
        auto dup = fromJson->createDuplicate().staticCast<iris::LightNode>();
        CHECK(dup->rectWidth == fromJson->rectWidth && dup->doubleSided && dup->accurate,
              "duplicate keeps the area-light fields");

        // Pixel proof: a document area light above the cube lights it through the mirror.
        doc->getRootNode()->removeChild(point);
        doc->getRootNode()->addChild(meshNode2);
        target->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        // The light shines straight down: only the cube's TOP face is lit, and the
        // frame centre is its dark front face. The cube is green and the background
        // blue, so the brightest green pixel anywhere tracks the lit top face.
        auto maxGreen = [&]() { float m = 0; for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) m = std::max(m, img.at(x, y).g); return m; };
        const float unlit = maxGreen();
        auto area = iris::LightNode::create();
        area->lightType = iris::LightType::Area;          // default orientation: emits down -Y
        area->intensity = 4.0f;
        area->distance = 20.0f;
        area->rectWidth = 2.0f; area->rectHeight = 2.0f;
        area->color = QColor(255, 255, 255);
        area->setLocalPos(QVector3D(0.0f, 1.2f, 0.0f));   // just above the cube, facing down
        doc->getRootNode()->addChild(area);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const float litApprox = maxGreen();
        area->accurate = true;                             // LT_AREA_LTC
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const float litLtc = maxGreen();
        std::printf("    area light: unlit %.2f  approx %.2f  ltc %.2f\n", unlit, litApprox, litLtc);
        CHECK(litApprox > unlit + 0.1f, "document area light (approx) lights the cube");
        CHECK(litLtc > unlit + 0.1f, "accurate (LTC) area light lights the cube too");

        // The helper wire is the emitting rectangle (wire kind 3) + icon billboard.
        area->color = QColor(255, 0, 255);                 // magenta wires
        mirror.setLightWires(true);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        int magenta = 0, white = 0;
        for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            if (c.r > 0.8f && c.b > 0.8f && c.g < 0.3f) ++magenta;
            if (c.r > 0.85f && c.g > 0.85f && c.b > 0.85f) ++white;
        }
        std::printf("    area helper: %d magenta wire px, %d white icon px\n", magenta, white);
        CHECK(magenta > 10, "area light draws its rectangle outline");
        CHECK(white > 5, "area light shows the procedural rounded-rect icon");
        mirror.setLightWires(false);

        // Leave the scene as the sky tests below expect it: empty, bright ambient.
        doc->getRootNode()->removeChild(area);
        doc->getRootNode()->removeChild(meshNode2);
        target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

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

    // ---- realistic sky: the legacy Preetham shader CPU-baked to an equirect ----
    {
        doc->skyType = iris::SkyType::REALISTIC;
        doc->skyRealistic.luminance = 1.0f;               // the legacy demo defaults
        doc->skyRealistic.reileigh = 2.0f;
        doc->skyRealistic.mieCoefficient = 0.005f;
        doc->skyRealistic.mieDirectionalG = 0.8f;
        doc->skyRealistic.turbidity = 10.0f;
        doc->skyRealistic.sunPosX = 0.0f;                 // sun overhead: a blue day sky
        doc->skyRealistic.sunPosY = 450000.0f;
        doc->skyRealistic.sunPosZ = 0.0f;
        mirror.applySky(view);
        // Look toward the horizon: daytime sky pixels, blue over red, not black.
        cam->setLocalRot(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 25.0f));
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("realistic sky", img);
        const Colour day = centre(img);
        CHECK(day.b > 0.15f && day.b > day.r, "realistic sky bakes sky-like blue-dominant pixels");
        // The bake tracks the parameters (debounced ~150 ms): pull the sun to the
        // horizon and the same view must change colour once the debounce passes.
        doc->skyRealistic.sunPosY = 4000.0f;
        doc->skyRealistic.sunPosZ = -400000.0f;
        QThread::msleep(180);
        mirror.applySky(view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("realistic sky, sunset", img);
        const Colour dusk = centre(img);
        const float delta = std::fabs(dusk.r - day.r) + std::fabs(dusk.g - day.g) + std::fabs(dusk.b - day.b);
        std::printf("    parameter change moved the same pixel by %.3f\n", delta);
        CHECK(delta > 0.05f, "moving the sun re-bakes the realistic sky");
    }

    // ---- equirect sky feeds environment reflections (IBL), like cubemaps do ----
    {
        const QString eqPath = QDir::temp().filePath("jahshaka_mirror_equirect.png");
        {
            QImage eq(64, 32, QImage::Format_RGBA8888);
            eq.fill(QColor(0, 255, 0));                    // a uniformly green world
            eq.save(eqPath);
        }
        doc->setSkyTexture(iris::Texture2D::load(eqPath));
        doc->skyType = iris::SkyType::EQUIRECTANGULAR;
        mirror.applySky(view);
        // A mirror-metal cube in near-darkness: everything it shows is reflection.
        target->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
        auto chromeMat = iris::PbrMaterial::create();
        chromeMat->setValue("baseColor", QColor(255, 255, 255));
        chromeMat->setValue("metallic", 1.0f);
        chromeMat->setValue("roughness", 0.1f);
        meshNode->setMaterial(chromeMat);
        doc->getRootNode()->addChild(meshNode);            // was removed by the earlier tests
        cam->setLocalPos(QVector3D(2.2f, 1.8f, 2.6f));
        cam->lookAt(QVector3D(0, 0, 0));
        cam->angle = 45.0f;
        mirror.applyCamera(cam, view);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("metal cube, equirect IBL", img);
        const Colour c = centre(img);
        CHECK(mirror.engineNode(meshNode.data()) != 0, "the chrome cube is mirrored");
        CHECK(c.g > 0.25f && c.g > c.r * 1.5f && c.g > c.b * 1.5f,
              "a metal cube reflects the equirect sky's colour (IBL)");
        // Control: clear the sky — the reflections go with it, and the same cube
        // pixel must fall dark (ambient 0.02). Proves the green above was the
        // cube's IBL, not the sky showing through where a cube failed to mirror.
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        doc->skyColor = QColor(0, 0, 255);
        doc->setSkyTexture(iris::Texture2DPtr());
        mirror.applySky(view);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("metal cube, no sky (control)", img);
        const Colour dark = centre(img);
        CHECK(dark.g < 0.15f && dark.b < 0.15f,
              "clearing the sky clears the reflections (the cube goes dark)");
        meshNode->setMaterial(legacyOrange);
        doc->getRootNode()->removeChild(meshNode);
        QFile::remove(eqPath);
        target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        mirror.applySky(view);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

    // ---- editor ground grid (EDITOR_SHORTCUTS_SPEC §3) ----
    // Empty scene, flat blue sky: every non-blue pixel is the grid. Looking
    // straight down from y=10 the ±100-unit grid fills the frame.
    {
        cam->setLocalPos(QVector3D(0.0f, 10.0f, 0.01f));
        cam->lookAt(QVector3D(0, 0, 0));
        mirror.applyCamera(cam, view);
        auto gridPixels = [&](float minR) {
            int count = 0;
            for (unsigned y = 0; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x)
                    if (img.at(x, y).r > minR) ++count;
            return count;
        };
        mirror.setGrid(true, 1.0f);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("grid, spacing 1", img);
        const int at1 = gridPixels(0.08f);
        std::printf("    grid pixels at spacing 1: %d\n", at1);
        CHECK(at1 > 100, "grid lines render over the empty scene");
        // The two axis lines through the origin are MAJOR (every 10th, brighter).
        CHECK(gridPixels(0.22f) > 10, "major lines are visibly brighter");

        mirror.setGrid(true, 4.0f);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("grid, spacing 4", img);
        const int at4 = gridPixels(0.08f);
        std::printf("    grid pixels at spacing 4: %d\n", at4);
        CHECK(at4 > 20 && at4 < at1, "wider spacing draws fewer lines (grid re-spaces live)");

        mirror.setGrid(false, 4.0f);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("grid hidden", img);
        CHECK(gridPixels(0.08f) < 5, "hiding the grid removes every line pixel");
    }

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
