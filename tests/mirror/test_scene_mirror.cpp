// SceneMirror characterisation: an iris:: document renders through the engine.
//
// Builds a document (Scene -> empty parent node -> MeshNode with cube.obj), mirrors
// it into an offscreen engine view and asserts on pixels through every document
// operation the editor performs: move a parent, hide, show, remove.
// No window; runs with DISPLAY reachable (Vulkan). QT_QPA_PLATFORM=offscreen.
#include "irisgl/core/math/quat.h"

#include "tests/support/testmesh.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QImage>
#include <QDir>
#include <QJsonObject>
#include <QThread>
#include "irisgl/document/assets/texture2d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/builtinmaterials.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"
#include "irisgl/document/scenegraph/scenepicking.h"
#include "irisgl/document/scenegraph/skybake.h"
#include "irisgl/document/scenegraph/nodegraph.h"

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
    meshNode->setMesh(testmesh::load(":assets/models/cube.obj"));
    auto legacyOrange = iris::DefaultMaterial::create();
    legacyOrange->setDiffuseColor(QColor(204, 76, 51));   // the document decides the colour now
    meshNode->setMaterial(legacyOrange);
    CHECK(!!meshNode->getMesh(), "cube.obj loaded into the document (no GL)");
    const float r = meshNode->getMeshRadius();
    const float s = r > 0.0f ? 1.0f / r : 1.0f;      // normalise to unit radius
    meshNode->setLocalScale(iris::Vec3(s, s, s));
    parent->addChild(meshNode);
    auto light = iris::LightNode::create();
    light->setName("sun");
    light->intensity = 1.0f;
    light->setLocalRot(iris::Quat::fromEulerAngles(-50.0f, 30.0f, 0.0f));
    // Off-origin, out of frame: a directional light's position never affects
    // lighting, but its helper icon billboard would otherwise sit at the origin
    // and trip every "centre is background" assertion below.
    light->setLocalPos(iris::Vec3(0.0f, 6.0f, 0.0f));
    doc->getRootNode()->addChild(light);

    MeshData md;
    CHECK(SceneMirror::toMeshData(meshNode->getMesh().data(), md), "iris::Mesh -> MeshData");
    std::printf("    cube.obj: %zu vertices, %zu triangles, normals=%s uvs=%s\n",
                md.vertexCount(), md.triangleCount(), md.normals.empty() ? "no" : "yes", md.uvs.empty() ? "no" : "yes");

    // ---- mirror + render ----
    SceneMirror mirror(target);
    mirror.setSource(doc);
    const int n = mirror.sync();
    CHECK(n == 3, "sync mirrored 3 document nodes (parent, cube, light)");
    CHECK(mirror.engineNode(meshNode.data()) != 0, "cube has an engine node");
    for (int i = 0; i < 3; ++i) engine->renderOneFrame();
    Image img;
    CHECK(view->readPixels(img), "readPixels");
    show("initial", img);
    CHECK(isBlue(corner(img)), "corner is the clear colour");
    CHECK(isMaterial(centre(img)), "centre is the mirrored cube");

    // ---- move the PARENT: the child must follow through the engine hierarchy ----
    parent->setLocalPos(iris::Vec3(10.0f, 0.0f, 0.0f));
    mirror.sync();
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("parent moved +10x", img);
    CHECK(isBlue(centre(img)), "cube left the view when its PARENT moved");

    parent->setLocalPos(iris::Vec3(0, 0, 0));
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("parent back", img);
    CHECK(isMaterial(centre(img)), "cube is back");

    // ---- visibility ----
    meshNode->setVisible(false);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("hidden", img);
    CHECK(isBlue(centre(img)), "hidden node renders nothing");
    meshNode->setVisible(true);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("shown", img);
    CHECK(isMaterial(centre(img)), "shown again");

    // ---- EFFECTIVE visibility through the hierarchy (RENDER_PIPELINE_AUDIT 1.1/1.2) ----
    // A node is on screen iff it AND every ancestor are visible; each node's
    // own flag is the user's and is never rewritten by an ancestor's change.
    // The mirror pushes that effective state per node — the old push of the
    // node's own flag leaned on Ogre's setVisible cascade, which re-drew a
    // child the user had hidden the moment its parent was shown again, and
    // could not see a re-parent at all.
    {
        const iris::Vec3 eye(2.2f, 1.8f, 2.6f);
        const iris::Vec3 through = eye + (iris::Vec3(0, 0, 0) - eye) * 3.0f;
        auto picksCube = [&]() {
            for (const auto &hit : iris::picking::raycastMeshes(doc.data(), eye, through, 0, false))
                if (hit.node.data() == meshNode.data()) return true;
            return false;
        };
        auto step = [&](const char *tag) {
            mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
            view->readPixels(img); show(tag, img);
        };

        // (a) the parent hides its child, on screen AND to picking.
        parent->setVisible(false);
        step("parent hidden");
        CHECK(isBlue(centre(img)), "hiding the PARENT hides its child");
        CHECK(meshNode->isVisible() && !meshNode->isVisibleInScene(),
              "the child's own flag is untouched; it is not visible IN THE SCENE");
        CHECK(!picksCube(), "a child hidden by its parent is not pickable");
        CHECK(iris::picking::lastUsedEngineBroadPhase(), "...through the engine broad phase");
        parent->setVisible(true);
        step("parent shown");
        CHECK(isMaterial(centre(img)), "showing the parent shows the child again");
        CHECK(picksCube(), "...and makes it pickable again");

        // (b) a child the user hid ITSELF survives its parent's hide/show.
        meshNode->setVisible(false);
        step("child hidden itself");
        parent->setVisible(false);
        step("...then parent hidden");
        parent->setVisible(true);
        step("...then parent shown");
        CHECK(isBlue(centre(img)),
              "a child the user hid itself STAYS hidden when its parent is shown again");
        CHECK(!meshNode->isVisible(), "...its own flag still says hidden");
        CHECK(!picksCube(), "...and it is still not pickable");
        meshNode->setVisible(true);
        step("child shown itself");
        CHECK(isMaterial(centre(img)), "showing the child itself brings it back");

        // (c) a RE-PARENT is a visibility change too: under a hidden group the
        // child is hidden without any flag of its own moving, and back out it
        // shows. (Ogre's cascade never saw this; the effective push does.)
        auto hiddenGroup = iris::SceneNode::create();
        hiddenGroup->setName("hidden-group");
        hiddenGroup->setVisible(false);
        doc->getRootNode()->addChild(hiddenGroup);
        parent->removeChild(meshNode);
        hiddenGroup->addChild(meshNode, false);
        step("moved under a hidden group");
        CHECK(isBlue(centre(img)), "moving a visible node under a HIDDEN group hides it");
        CHECK(!picksCube(), "...and it is not pickable there");
        hiddenGroup->removeChild(meshNode);
        parent->addChild(meshNode, false);
        step("moved back");
        CHECK(isMaterial(centre(img)), "moving it back out shows it again");
        doc->getRootNode()->removeChild(hiddenGroup);
        hiddenGroup.reset();
        step("hidden group removed");
        CHECK(isMaterial(centre(img)), "the cube is back where the rest of this suite expects it");
    }

    // ---- re-parent in the document: cube moves under a second, offset node ----
    auto other = iris::SceneNode::create();
    other->setLocalPos(iris::Vec3(0.0f, 10.0f, 0.0f));
    doc->getRootNode()->addChild(other);
    parent->removeChild(meshNode);
    other->addChild(meshNode, false);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("re-parented +10y", img);
    CHECK(isBlue(centre(img)), "re-parenting in the document moved the cube in the engine");
    other->setLocalPos(iris::Vec3(0, 0, 0));
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("new parent at origin", img);
    CHECK(isMaterial(centre(img)), "cube visible under its new parent");

    // ---- remove from the document ----
    other->removeChild(meshNode);
    mirror.sync();
    // mirroredNodeCount(), not sync()'s return: since the dirty set the sync
    // VISITS what changed, not what exists — "how many nodes are still
    // mirrored" is the entry map, which is what this line always meant.
    CHECK(mirror.mirroredNodeCount() == 3, "3 nodes remain (parent, other, light)");
    CHECK(mirror.engineNode(meshNode.data()) == 0, "removed node has no engine node");
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("removed", img);
    CHECK(isBlue(centre(img)), "removed node renders nothing");

    // ---- step 4: material colour comes from the DOCUMENT ----
    auto meshNode2 = iris::MeshNode::create();
    meshNode2->setMesh(testmesh::load(":assets/models/cube.obj"));
    meshNode2->setLocalScale(iris::Vec3(s, s, s));
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

    // ---- THE RETIRED "Default" BUILTIN, as the PbrMaterial preset it became ----
    //
    // This block used to build an iris::CustomMaterial from
    // app/shader_defs/Default.shader and assert that the mirror could scrape
    // diffuseColor and diffuseTexture out of its property rows. The class is
    // gone (HLMS_ADOPTION P4b) and so is the scraping: the conversion happens
    // ONCE, at load, and what the mirror sees is an ordinary PbrMaterial.
    //
    // So the assertion moved with it — this drives the CONVERSION (a legacy
    // `values{}` block naming the reserved Default guid) and then asserts the
    // resulting material reaches pixels, which is the property that actually
    // matters to a user reopening an old scene.
    {
        QJsonObject values;
        values["diffuseColor"] = QStringLiteral("#e62814");
        values["shininess"] = 0.0;
        auto converted = BuiltinMaterials::fromBuiltin(
            QStringLiteral("00000000-0000-0000-0000-000000000001"), values,
            [](const QString &p, const QString &) { return p; });
        CHECK(!converted.isNull(), "the reserved Default guid converts to a PbrMaterial");
        CHECK(converted->getName() == QStringLiteral("Default"),
              "...and it is still called Default");
        meshNode2->setMaterial(converted);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("converted Default builtin: diffuseColor", img);
        CHECK(isMaterial(centre(img)), "the converted builtin's colour reaches the engine");

        const QString pngPath = QDir::temp().filePath("jahshaka_mirror_custom_green.png");
        QImage tex(16, 16, QImage::Format_RGBA8888); tex.fill(QColor(20, 230, 40)); tex.save(pngPath);
        QJsonObject texValues;
        texValues["diffuseColor"] = QStringLiteral("#ffffff");
        texValues["diffuseTexture"] = pngPath;
        auto texConverted = BuiltinMaterials::fromBuiltin(
            QStringLiteral("00000000-0000-0000-0000-000000000001"), texValues,
            [](const QString &p, const QString &) { return p; });
        meshNode2->setMaterial(texConverted);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("converted Default builtin: diffuseTexture", img);
        CHECK(centre(img).g > centre(img).r * 1.5f && centre(img).g > centre(img).b * 1.5f,
              "the legacy diffuseTexture name became a baseColorMap and reaches the engine");
        QFile::remove(pngPath);

        // The FLAT builtin is the one whose conversion changes shading family
        // (D-P4b): it becomes an UNLIT PbrMaterial, so its colour arrives
        // unshaded. That is the whole product answer to "what is Flat?".
        QJsonObject flatValues;
        flatValues["color"] = QStringLiteral("#00cc22");
        auto flat = BuiltinMaterials::fromBuiltin(
            QStringLiteral("00000000-0000-0000-0000-000000000004"), flatValues,
            [](const QString &p, const QString &) { return p; });
        CHECK(flat->shadingModel == 1, "the Flat builtin converts to the UNLIT shading model");
        meshNode2->setMaterial(flat);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("converted Flat builtin (unlit)", img);
        // Colour components are 0..1 here, and this readback is LINEAR. #00cc22
        // is (0, 204, 34) sRGB, which is (0, 0.604, 0.033) linear — a colour a
        // user PICKED goes through iris::linearOf like every other one
        // (SKY_LIGHT_SPEC.md §4), unlit included. RE-BASELINED from 0.8: the
        // assertion is still "it renders as AUTHORED, unshaded", it is just
        // that the authored colour finally means the same thing a texture of
        // that colour means.
        CHECK(centre(img).r < 0.05f && centre(img).g > 0.55f && centre(img).g < 0.66f &&
                  centre(img).b < 0.08f,
              "Flat renders its AUTHORED colour, unshaded (decoded, like a texture)");

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
        // RE-BASELINED by the Ogre sky/IBL adoption wave. Measured here: fade
        // 2.647 vs glass 2.076 (gap 0.571) before, fade 1.980 vs glass 1.858
        // (gap 0.122) after. Both dropped and the gap narrowed for the same two
        // reasons, and neither is about transparency: AmbientSh contributes no
        // ambient SPECULAR (the hemisphere mode did), and
        // EnvFeatures_DiffuseGiFromReflectionProbe is now off (it was adding the
        // reflection cube's roughest mip to every surface's diffuse). The two
        // paths still differ, which is all this regression ever pinned.
        CHECK(std::fabs(glassLum - fadeLum) > 0.08f,
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
            // List is the ENUM row (alphaMode, brdf) and serializes as the
            // plain int it stores — same case as Int, exactly as SceneWriter does.
            case iris::PropertyType::Int:
            case iris::PropertyType::List:  values[prop->name] = prop->getValue().toInt(); break;
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
            case iris::PropertyType::Int:
            case iris::PropertyType::List:   reloaded->setValue(prop->name, val.toInt()); break;
            case iris::PropertyType::Color:  reloaded->setValue(prop->name, QColor(val.toString())); break;
            case iris::PropertyType::Bool:   reloaded->setValue(prop->name, val.toBool()); break;
            case iris::PropertyType::Texture:
                reloaded->setValue(prop->name, val.toString().isEmpty() ? QString() : sceneDir.filePath(val.toString()));
                break;
            default: break;
            }
        }
        CHECK(reloaded->textures.contains("u_baseColorMap"),
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
    point->setLocalPos(iris::Vec3(4.0f, 1.0f, 2.5f));   // camera-right of the cube
    doc->getRootNode()->addChild(point);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    auto lum = [&](unsigned x, unsigned y) { const Colour c = img.at(x, y); return c.r + c.g + c.b; };
    const float rightSide = lum(img.width * 3 / 4, img.height / 2), leftSide = lum(img.width / 4, img.height / 2);
    std::printf("    point light right: left-of-frame %.2f  right-of-frame %.2f\n", leftSide, rightSide);
    CHECK(rightSide > leftSide + 0.05f, "point light on the right lights the right side more");
    point->setLocalPos(iris::Vec3(-4.0f, 1.0f, 2.5f));   // move the light node to the left
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    const float rightSide2 = lum(img.width * 3 / 4, img.height / 2), leftSide2 = lum(img.width / 4, img.height / 2);
    std::printf("    point light left:  left-of-frame %.2f  right-of-frame %.2f\n", leftSide2, rightSide2);
    CHECK(leftSide2 > rightSide2 + 0.05f, "moving the light NODE in the document moves the light");

    // ---- document camera drives the view ----
    target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 0.0f, 4.0f));       // straight in front, looking -Z
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 100.0f;
    doc->getRootNode()->addChild(cam);
    mirror.applyCamera(cam, view);
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document camera", img);
    CHECK(!isBlue(centre(img)), "document camera sees the cube");
    cam->setLocalPos(iris::Vec3(0.0f, 20.0f, 4.0f));      // way above: cube leaves the centre
    mirror.applyCamera(cam, view);
    for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document camera moved", img);
    CHECK(isBlue(centre(img)), "moving the document camera moves the view");

    // ---- selection highlight (on-top wireframe) and light wires ----
    cam->setLocalPos(iris::Vec3(2.2f, 1.8f, 2.6f)); cam->lookAt(iris::Vec3(0, 0, 0));
    mirror.applyCamera(cam, view);
    // A green cube under the strong white point light: nothing but the highlight can read as yellow.
    meshNode2->setMaterial(legacy);
    auto countYellow = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.g > 0.6f && c.b < 0.4f) ++n; } return n; };
    mirror.setHighlightedNodes({ meshNode2 });
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

    mirror.setHighlightedNodes({});
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countYellow(img) == 0, "highlight cleared");

    // ---- a GROUP selection outlines the whole subtree ----
    // Selecting a multi-part asset's ROOT must outline every descendant mesh;
    // before, only a single MeshNode selection drew anything.
    {
        cam->setLocalPos(iris::Vec3(0.0f, 0.8f, 5.0f)); cam->lookAt(iris::Vec3(0, 0, 0));
        mirror.applyCamera(cam, view);
        auto group = iris::SceneNode::create();
        auto makePart = [&](float x) {
            auto part = iris::MeshNode::create();
            part->setMesh(testmesh::load(":assets/models/cube.obj"));
            part->setLocalScale(iris::Vec3(s, s, s));
            part->setLocalPos(iris::Vec3(x, 0, 0));
            part->setMaterial(legacy);
            part->setAttached(true);
            group->addChild(part, false);
            return part;
        };
        makePart(-1.4f); makePart(1.4f);
        doc->getRootNode()->addChild(group);
        mirror.setHighlightedNodes({ group });
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
        mirror.setHighlightedNodes({});
        doc->getRootNode()->removeChild(group);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countYellow(img) == 0, "group highlight cleared");
        cam->setLocalPos(iris::Vec3(2.2f, 1.8f, 2.6f)); cam->lookAt(iris::Vec3(0, 0, 0));
        mirror.applyCamera(cam, view);
    }

    doc->getRootNode()->removeChild(meshNode2);
    point->color = QColor(255, 0, 255);              // magenta wires
    point->setLocalPos(iris::Vec3(0.0f, 0.0f, 0.0f));  // in the middle of the frame
    point->distance = 0.35f;                          // rings are drawn at radius = range now
    auto countMagenta = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.r > 0.8f && c.b > 0.8f && c.g < 0.3f) ++n; } return n; };
    mirror.setLightWires(true);
    // Attenuation volumes are selection-gated (the Unreal convention): an
    // UNSELECTED point light shows only its icon, no range rings.
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img);
    CHECK(countMagenta(img) == 0, "unselected point light draws no rings (icon only)");
    mirror.setHighlightedNodes({ point });
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
        mirror.setHighlightedNodes({});
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        std::printf("    unselected: %d white icon px, %d magenta px\n", countWhite(img), countMagenta(img));
        CHECK(countWhite(img) > 5, "unselected point light still shows its icon");
        CHECK(countMagenta(img) == 0, "unselected point light shows no rings");
        mirror.setHighlightedNodes({ point });
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

        // THE ICON FOLLOWS THE LIGHT (perf wave B, audit F7). setBillboards
        // rewrites the whole instance buffer and is pushed on CHANGE only now,
        // keyed on the light's world-transform signature — so "the icon is
        // stuck where the light used to be" is the exact way that guard can be
        // wrong, and it is invisible to every other assertion here.
        {
            auto whiteCentroidX = [&](const Image &im) {
                double sum = 0; int n = 0;
                for (unsigned y = 0; y < im.height; ++y)
                    for (unsigned x = 0; x < im.width; ++x) {
                        const Colour c = im.at(x, y);
                        if (c.r > 0.85f && c.g > 0.85f && c.b > 0.85f) { sum += x; ++n; }
                    }
                return n ? sum / n : -1.0;
            };
            const double before = whiteCentroidX(img);
            const iris::Vec3 was = point->getLocalPos();
            point->setLocalPos(was + iris::Vec3(0.6f, 0.0f, 0.0f));
            mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
            view->readPixels(img);
            const double after = whiteCentroidX(img);
            std::printf("    icon centroid x: %.1f -> %.1f after moving the light +0.6x\n",
                        before, after);
            CHECK(before >= 0 && after >= 0 && std::fabs(after - before) > 2.0,
                  "the light icon billboard follows the light when it moves");
            point->setLocalPos(was);
            mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
            view->readPixels(img);
        }

        // ...and the WIRE COLOUR still follows an edit (the same change-guard
        // discipline, one push earlier in syncLightWires).
        {
            auto countGreen = [&](const Image &im) { int n = 0; for (unsigned y = 0; y < im.height; ++y) for (unsigned x = 0; x < im.width; ++x) { const Colour c = im.at(x, y); if (c.g > 0.7f && c.r < 0.4f && c.b < 0.4f) ++n; } return n; };
            const QColor wasColour = point->color;
            point->color = QColor(0, 255, 0);
            mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
            view->readPixels(img);
            std::printf("    recoloured light wires: %d green px, %d magenta px\n",
                        countGreen(img), countMagenta(img));
            CHECK(countGreen(img) > 0 && countMagenta(img) == 0,
                  "editing the light's colour repaints its wires (magenta rings are gone)");
            point->color = wasColour;
            mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
            view->readPixels(img);
        }

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
        mirror.setHighlightedNodes({});

        // Point-light shadow controls (the panel unhides Type/Size in engine
        // mode): the Shadow Type combo drives castShadows through toLightDesc.
        point->shadowMap->shadowType = iris::ShadowMapType::Soft;
        CHECK(SceneMirror::toLightDesc(point.data()).castShadows, "point light Shadow Type=Soft casts shadows");
        point->shadowMap->shadowType = iris::ShadowMapType::None;
        CHECK(!SceneMirror::toLightDesc(point.data()).castShadows, "point light Shadow Type=None stops casting");

        // (The STATIC SHADOW flag and the transform-write counter the mirror
        // watched for it are gone — ENGINE_CACHE_POLICY_SPEC P2/P3: every
        // point/spot map is cached and the ENGINE detects its inputs, per light.
        // What the mirror still carries is world.refreshShadows()'s serial.)
        point->shadowMap->shadowType = iris::ShadowMapType::Soft;

        // world.refreshShadows()'s serial: monotonic, and the document's
        // default is zero so a fresh scene never looks like a pending refresh.
        CHECK(doc->shadowRefreshSerial == 0, "a fresh scene has no pending shadow refresh");
        ++doc->shadowRefreshSerial;
        CHECK(doc->shadowRefreshSerial == 1, "the refresh serial is a plain monotonic counter");
        doc->shadowRefreshSerial = 0;
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
        area->setLocalPos(iris::Vec3(0.0f, 1.2f, 0.0f));   // just above the cube, facing down
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

    // ---- decals: a DecalNode becomes exactly one engine decal (DECALS_SPEC §5.3) ----
    {
        // A big flat "floor" quad made from the cube mesh, lit straight down, with
        // the camera looking down at it — the decal paints its image on the top face.
        auto floorMat = iris::PbrMaterial::create();
        floorMat->setValue("baseColor", QColor(128, 128, 128));
        floorMat->setValue("roughness", 1.0f);
        floorMat->setValue("metallic", 0.0f);
        auto floorNode = iris::MeshNode::create();
        floorNode->setName("decal floor");
        floorNode->setMesh(testmesh::load(":assets/models/cube.obj"));
        floorNode->setMaterial(floorMat);
        CHECK(!!floorNode->getMesh(), "decal: floor mesh loaded");
        floorNode->setLocalScale(iris::Vec3(8.0f, 0.2f, 8.0f));
        floorNode->setLocalPos(iris::Vec3(0, -1.1f, 0));
        doc->getRootNode()->addChild(floorNode);
        auto sun = iris::LightNode::create();
        sun->lightType = iris::LightType::Directional;   // identity = shines down -Y
        sun->intensity = 1.0f;
        sun->color = QColor(255, 255, 255);
        sun->setLocalPos(iris::Vec3(0, 5, 0));
        doc->getRootNode()->addChild(sun);

        // Overhead camera looking straight down.
        auto decalCam = iris::CameraNode::create();
        decalCam->setLocalPos(iris::Vec3(0, 6, 0));
        decalCam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -90.0f));
        decalCam->update(0.0f);
        mirror.applyCamera(decalCam, view);

        // The decal image: a solid red PNG written next to the test binary. The
        // node stores a RESOLVED PATH (the reader/edit service fills it from the
        // CAS); the mirror never sees a guid.
        const QString redPath = QDir::current().filePath("mirror_decal_red.png");
        { QImage im(32, 32, QImage::Format_RGBA8888); im.fill(QColor(255, 0, 0, 255)); im.save(redPath); }
        const QString bluePath = QDir::current().filePath("mirror_decal_blue.png");
        { QImage im(32, 32, QImage::Format_RGBA8888); im.fill(QColor(0, 0, 255, 255)); im.save(bluePath); }

        mirror.setLightWires(false);   // the wire box would colour the readback
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const Colour bare = centre(img);
        show("floor, no decal", img);

        auto decal = iris::DecalNode::create();
        CHECK(decal->getSceneNodeType() == iris::SceneNodeType::Decal,
              "DecalNode SETS its SceneNodeType (as CameraNode does since CAMERAS_SPEC)");
        decal->width = 3.0f; decal->height = 3.0f; decal->depth = 2.0f;
        decal->textureGuid = QStringLiteral("guid-red");
        decal->resolvedTexturePath = redPath;
        decal->setLocalPos(iris::Vec3(0, 0, 0));
        doc->getRootNode()->addChild(decal);
        CHECK(doc->decals.size() == 1, "Scene::decals registers the node by guid");

        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const Colour painted = centre(img);
        show("floor, red decal", img);
        CHECK(painted.r > painted.g + 0.05f && painted.r > painted.b + 0.05f,
              "a DecalNode paints its image onto the floor through the mirror");

        // Changing the bound image re-binds (a different atlas slice).
        decal->resolvedTexturePath = bluePath;
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const Colour rebound = centre(img);
        show("floor, blue decal", img);
        CHECK(rebound.b > rebound.r + 0.05f, "changing the image re-binds the decal");

        // Hiding the node hides the decal (the LAYER_VISIBILITY cascade).
        decal->setVisible(false);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(std::abs(centre(img).r - bare.r) < 0.02f, "hiding a decal node hides the projection");
        decal->setVisible(true);

        // An UNRESOLVABLE image leaves the node decal-free rather than keeping the
        // previous one bound (a stale decal would keep painting the old picture).
        decal->resolvedTexturePath = QStringLiteral("/no/such/decal/image.png");
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(std::abs(centre(img).r - bare.r) < 0.02f,
              "an unresolvable decal image projects NOTHING (no stale binding)");
        decal->resolvedTexturePath = redPath;
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();

        // The wire box: kind 4, amber while the decal projects.
        mirror.setLightWires(true);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        int amber = 0;
        for (unsigned y = 0; y < img.height; ++y) for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            if (c.r > 0.8f && c.g > 0.5f && c.g < 0.9f && c.b < 0.4f) ++amber;
        }
        std::printf("    decal wire box: %d amber px\n", amber);
        CHECK(amber > 10, "a selected-or-not decal draws its projector wire box");
        mirror.setLightWires(false);

        // Duplicate keeps every field, and the type.
        auto dup = decal->createDuplicate().staticCast<iris::DecalNode>();
        CHECK(dup->getSceneNodeType() == iris::SceneNodeType::Decal &&
              dup->textureGuid == decal->textureGuid && dup->width == decal->width &&
              dup->depth == decal->depth,
              "duplicating a decal keeps its type, image guid and box");

        // Removing the node removes the engine decal.
        doc->getRootNode()->removeChild(decal);
        CHECK(doc->decals.isEmpty(), "removing the node clears Scene::decals");
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(std::abs(centre(img).r - bare.r) < 0.02f, "removing a decal node clears the projection");

        // Leave the scene as the sky tests below expect it.
        doc->getRootNode()->removeChild(floorNode);
        doc->getRootNode()->removeChild(sun);
        mirror.applyCamera(cam, view);
        target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
    }

    // ---- flat sky colour from the document becomes the background ----
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(200, 30, 200);
    mirror.applySky(view);
    mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    view->readPixels(img); show("document sky colour", img);
    // A SINGLE_COLOR sky is a real sky now (SKY_LIGHT_SPEC.md §2) and its strip
    // is decoded like every other colour a user picks (§4), so 200,30,200 lands
    // at (0.577, 0.012, 0.577) of radiance in this linear readback rather than
    // at the raw 0.78. RE-BASELINED; the assertion is unchanged in kind.
    CHECK(corner(img).r > 0.45f && corner(img).b > 0.45f && corner(img).g < 0.1f,
          "document sky colour is what the frame shows where there is no geometry");

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
        cam->setLocalPos(iris::Vec3(0, 0, 0));
        cam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), -90.0f));
        cam->angle = 20.0f;
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("cubemap sky +X", img);
        CHECK(centre(img).r > 0.8f && centre(img).g < 0.2f && centre(img).b < 0.2f, "+X face of the document cubemap is red");
        cam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), 90.0f));
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
        cam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), 89.0f));   // look up
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("gradient sky zenith", img);
        CHECK(centre(img).r > 0.7f && centre(img).g < 0.35f, "gradient sky zenith is the top colour");
        cam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -89.0f));  // look down
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img); show("gradient sky nadir", img);
        CHECK(centre(img).b > 0.7f && centre(img).g < 0.35f, "gradient sky nadir is the bottom colour");
    }

    // ---- the analytic sky: the ENGINE's, drawn on the GPU (SKY-GPU) --------
    // There is no CPU bake to call any more: the "realistic" sky is Ogre's
    // AtmosphereNpr, five numbers and a sun direction pushed into a shader. So
    // every assertion below is a PIXEL of the rendered sky — which is what the
    // bake's pixels were a proxy for.
    {
        doc->skyType = iris::SkyType::REALISTIC;
        doc->skyRealistic = iris::SkyRealistic::defaults();
        // THE SKY'S SUN IS THE SCENE'S SUN LIGHT (SKY_LIGHT_SPEC.md §3, D15).
        // A directional light pointing straight DOWN puts the sun overhead: a
        // blue day sky. There are no sky sun dials to set any more.
        auto skySun = iris::LightNode::create();
        skySun->setName("sun");
        skySun->lightType = iris::LightType::Directional;
        skySun->setLocalRot(iris::Quat());               // travels down -Y: sun at the zenith
        doc->getRootNode()->addChild(skySun);
        mirror.sync();
        mirror.applySky(view);
        // Look toward the horizon: daytime sky pixels, blue over red, not black.
        cam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), 25.0f));
        mirror.applyCamera(cam, view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("analytic sky", img);
        const Colour day = centre(img);
        CHECK(day.b > 0.15f && day.b > day.r, "the analytic sky draws sky-like blue-dominant pixels");
        // NO DEBOUNCE ANY MORE, and that is the headline: the sky is a shader,
        // so moving the sun is a const-buffer write. Pull the SUN LIGHT down to
        // the horizon and the same view changes colour in the NEXT frame.
        skySun->setLocalRot(iris::Quat::fromEulerAngles(-89.0f, 0.0f, 0.0f));
        mirror.sync();
        mirror.applySky(view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("analytic sky, sunset", img);
        const Colour dusk = centre(img);
        const float delta = std::fabs(dusk.r - day.r) + std::fabs(dusk.g - day.g) + std::fabs(dusk.b - day.b);
        std::printf("    moving the sun moved the same pixel by %.3f\n", delta);
        CHECK(delta > 0.05f, "moving the SUN LIGHT moves the analytic sky, in one frame (D15)");

        // THE DIALS ARE REAL DIALS. Density is how much atmosphere the ray
        // crosses: at the bottom of its range the horizon is pale, at the top
        // it is deep. One parameter, one visible answer, through the document.
        cam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), 5.0f));
        mirror.applyCamera(cam, view);
        skySun->setLocalRot(iris::Quat::fromEulerAngles(-40.0f, 0.0f, 0.0f));
        doc->skyRealistic.density = 0.2f;
        mirror.sync(); mirror.applySky(view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const Colour thin = centre(img);
        doc->skyRealistic.density = 0.8f;
        mirror.sync(); mirror.applySky(view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("analytic sky, dense", img);
        const Colour dense = centre(img);
        const float densityDelta = std::fabs(dense.r - thin.r) + std::fabs(dense.g - thin.g) +
                                   std::fabs(dense.b - thin.b);
        std::printf("    density 0.2 vs 0.8 moved the horizon by %.3f\n", densityDelta);
        CHECK(densityDelta > 0.05f, "the Density dial visibly changes the sky");
        doc->skyRealistic = iris::SkyRealistic::defaults();

        // A SKY WITH NO SUN is the model's own night, not a crash and not a
        // failed texture (§3): a scene with no directional light is legal.
        doc->getRootNode()->removeChild(skySun);
        mirror.sync(); mirror.applySky(view);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("analytic sky, no sun", img);
        const Colour night = centre(img);
        std::printf("    no sun: %.3f %.3f %.3f   vs a 40 deg sun: %.3f %.3f %.3f\n",
                    night.r, night.g, night.b, dense.r, dense.g, dense.b);
        CHECK(night.r + night.g + night.b < dense.r + dense.g + dense.b,
              "an analytic sky with no directional light draws the model's night");
        mirror.sync();
    }

    // ---- sky-driven ambient / diffuse IBL (VISUAL_PARITY_SPEC item 3b) -----
    {
        // The integral itself: red above, near-black below.
        const QString redSkyPath = QDir::temp().filePath("jahshaka_mirror_redsky.png");
        {
            QImage eq(64, 32, QImage::Format_RGBA8888);
            for (int y = 0; y < 32; ++y) {
                const QRgb c = y < 16 ? qRgb(255, 0, 0) : qRgb(4, 4, 4);
                for (int x = 0; x < 64; ++x) eq.setPixel(x, y, c);
            }
            eq.save(redSkyPath);
        }
        const QString flipSkyPath = QDir::temp().filePath("jahshaka_mirror_redsky_flipped.png");
        {
            QImage eq(64, 32, QImage::Format_RGBA8888);
            for (int y = 0; y < 32; ++y) {
                const QRgb c = y < 16 ? qRgb(4, 4, 4) : qRgb(255, 0, 0);
                for (int x = 0; x < 64; ++x) eq.setPixel(x, y, c);
            }
            eq.save(flipSkyPath);
        }
        // THE INTEGRAL IS THE ENGINE'S NOW (SKY-GPU): it captures the sky it
        // drew into a cubemap and integrates that, so the assertion is made
        // where the answer lives — Scene::skyAmbientSh, after the sky has been
        // pushed and a frame has rendered (the capture runs inside a frame,
        // like the IBL convolution). Evaluate the 9 bands for +Y and -Y (the
        // basis is {1, y, z, x, ...}).
        const auto evalSh = [](const float sh[27], float x, float y, float z, int c) {
            const float b[9] = { 1.0f, y, z, x, x * y, y * z, 3.0f * z * z - 1.0f, z * x,
                                 x * x - y * y };
            float sum = 0.0f;
            for (int i = 0; i < 9; ++i) sum += sh[i * 3 + c] * b[i];
            return sum;
        };
        const auto skyShOf = [&](const QString &path, float sh[27]) {
            doc->setSkyTexture(iris::Texture2D::load(path));
            doc->skyType = iris::SkyType::EQUIRECTANGULAR;
            mirror.applySky(view);
            for (int i = 0; i < 3; ++i) engine->renderOneFrame();
            return target->skyAmbientSh(sh);
        };
        float sh[27] = { 0.0f };
        CHECK(skyShOf(redSkyPath, sh), "the engine integrates the sky it drew");
        const float upR = evalSh(sh, 0, 1, 0, 0), upG = evalSh(sh, 0, 1, 0, 1),
                    upB = evalSh(sh, 0, 1, 0, 2), loR = evalSh(sh, 0, -1, 0, 0);
        std::printf("    sky ambient SH: up %.3f %.3f %.3f   down(r) %.3f\n", upR, upG, upB, loR);
        CHECK(upR > 0.5f && upG < 0.05f && upB < 0.05f, "a normal facing up sees red");
        CHECK(loR < 0.05f, "a normal facing down stays dark (the split is oriented correctly)");
        // Row 0 of an equirect is the ZENITH: flipping the image must swap the
        // two hemispheres, not leave them alone. This is also the check that the
        // captured cube's faces are oriented the way the integral assumes —
        // upside down, it would answer exactly backwards.
        float fsh[27] = { 0.0f };
        CHECK(skyShOf(flipSkyPath, fsh), "the flipped sky integrates too");
        const float fupR = evalSh(fsh, 0, 1, 0, 0), floR = evalSh(fsh, 0, -1, 0, 0);
        std::printf("    flipped sky ambient SH: up(r) %.3f   down(r) %.3f\n", fupR, floR);
        CHECK(fupR < 0.05f && floR > 0.5f,
              "putting the red BELOW the horizon moves it to the lower hemisphere");

        // End to end: the same sky, a matte white cube, no lights touched. With a
        // SKY LIGHT in the document the cube is lit red-from-above; REMOVE the
        // Sky Light and the cube goes dark — ambient is the skylight and nothing
        // else (SKY_LIGHT_SPEC.md §2; this case used to toggle `ambientFromSky`
        // and compare against a flat grey Ambient Color, neither of which exists).
        doc->setSkyTexture(iris::Texture2D::load(redSkyPath));
        doc->skyType = iris::SkyType::EQUIRECTANGULAR;
        mirror.applySky(view);
        auto docSkyLight = iris::LightNode::create();
        docSkyLight->setName("Sky Light");
        docSkyLight->lightType = iris::LightType::Sky;
        docSkyLight->intensity = 1.0f;
        docSkyLight->color = QColor(255, 255, 255);
        doc->getRootNode()->addChild(docSkyLight);
        auto matte = iris::PbrMaterial::create();
        matte->setValue("baseColor", QColor(255, 255, 255));
        matte->setValue("metallic", 0.0f);
        matte->setValue("roughness", 1.0f);
        meshNode->setMaterial(matte);
        doc->getRootNode()->addChild(meshNode);
        cam->setLocalPos(iris::Vec3(1.6f, 2.6f, 1.6f));   // above: the top face fills the centre
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->angle = 45.0f;
        mirror.applyCamera(cam, view);
        mirror.applySky(view);
        mirror.sync();
        // THE ENVIRONMENT LANDS ONE FRAME AFTER THE SKY (SKY-GPU), exactly like
        // the IBL convolution: the capture runs inside a rendered frame, so the
        // ambient a host reads is the sky of the frame before. Hosts push it
        // every frame (applyEnvironment is per-frame work); a test that pushes
        // once and renders would be reading the PREVIOUS sky's light.
        const auto settleEnv = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                mirror.applyEnvironment(view, engine.get());
                engine->renderOneFrame();
            }
        };
        settleEnv(3);
        view->readPixels(img); show("matte cube, sky light ON", img);
        const Colour skyLit = centre(img);
        docSkyLight->setVisible(false);
        mirror.sync();
        settleEnv(3);
        view->readPixels(img); show("matte cube, sky light HIDDEN", img);
        const Colour noAmbient = centre(img);
        docSkyLight->setVisible(true);
        mirror.sync();
        std::printf("    cube top: skylight %.3f %.3f %.3f  no skylight %.3f %.3f %.3f\n",
                    skyLit.r, skyLit.g, skyLit.b, noAmbient.r, noAmbient.g, noAmbient.b);
        // The reflection cubemap contributes SPECULAR from the same red sky in
        // both frames (Ogre's cubemaps-as-diffuse-GI is off), so the ambient's
        // own colour is the honest signal.
        CHECK(skyLit.r > skyLit.b + 0.08f && skyLit.r > skyLit.g + 0.08f,
              "a red sky reddens the surface it lights (the Sky Light's SH)");
        CHECK(noAmbient.r < skyLit.r - 0.05f,
              "removing the Sky Light makes the top face dark: no Sky Light, no ambient");

        // The two hemispheres are separate colours end to end: move the red
        // BELOW the horizon (a different file, or Texture2D's path cache would
        // hand back the old image) and the top-lit face must fall dark.
        doc->setSkyTexture(iris::Texture2D::load(flipSkyPath));
        mirror.applySky(view);
        mirror.sync();
        settleEnv(3);
        view->readPixels(img); show("matte cube, red BELOW", img);
        const Colour fromBelow = centre(img);
        std::printf("    cube top with the red under it: %.3f %.3f %.3f\n",
                    fromBelow.r, fromBelow.g, fromBelow.b);
        CHECK(fromBelow.r < skyLit.r - 0.1f,
              "an upward-facing surface is lit by the UPPER hemisphere, not the lower one");

        // Restore what the following blocks expect.
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        doc->setSkyTexture(iris::Texture2DPtr());
        doc->getRootNode()->removeChild(docSkyLight);
        meshNode->setMaterial(legacyOrange);
        doc->getRootNode()->removeChild(meshNode);
        QFile::remove(redSkyPath);
        QFile::remove(flipSkyPath);
        mirror.applySky(view);
        target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        cam->setLocalPos(iris::Vec3(2.2f, 1.8f, 2.6f));
        cam->lookAt(iris::Vec3(0, 0, 0));
        mirror.applyCamera(cam, view);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
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
        cam->setLocalPos(iris::Vec3(2.2f, 1.8f, 2.6f));
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->angle = 45.0f;
        mirror.applyCamera(cam, view);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("metal cube, equirect IBL", img);
        const Colour c = centre(img);
        CHECK(mirror.engineNode(meshNode.data()) != 0, "the chrome cube is mirrored");
        CHECK(c.g > 0.25f && c.g > c.r * 1.5f && c.g > c.b * 1.5f,
              "a metal cube reflects the equirect sky's colour (IBL)");
        // Control: SWAP the sky — the reflections follow it. A colour sky is a
        // REAL sky now (SKY_LIGHT_SPEC.md §2) with a uniform environment of its
        // own, so "clear the sky" is not a thing a colour sky does any more;
        // what proves the green above was the cube's IBL is that changing the
        // sky to a BLUE one turns the same cube pixel blue.
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        doc->skyColor = QColor(0, 0, 255);
        doc->setSkyTexture(iris::Texture2DPtr());
        mirror.applySky(view);
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("metal cube, blue colour sky", img);
        const Colour swapped = centre(img);
        CHECK(swapped.b > 0.25f && swapped.b > swapped.g * 1.5f && swapped.b > swapped.r * 1.5f,
              "a colour sky is a real environment: the metal cube reflects IT now");
        meshNode->setMaterial(legacyOrange);
        doc->getRootNode()->removeChild(meshNode);
        QFile::remove(eqPath);
        target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        mirror.applySky(view);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

    // ---- pushing an UNCHANGED sky (and world) does nothing at all ----------
    //
    // ENGINEERING_DEBT_SPEC item 4's idempotency half, and the reason the whole
    // sky is one value now: the host builds a description, the boundary decides
    // whether anything has to happen. Two ways this could go wrong, and both
    // are visible from here:
    //
    //   * the MIRROR re-bakes — a fresh Preetham/gradient strip, a fresh
    //     equirect->cubemap resample, six new textures. The description is made
    //     of texture ids and ids are monotonic, so a re-bake cannot produce an
    //     equal one.
    //   * the ENGINE re-applies an equal description — which is not free and
    //     not invisible: rebuilding the reflection cubemap leaves the
    //     convolution PENDING for a frame (applyPendingIbl runs on the NEXT
    //     renderOneFrame), so the mirror-metal cube below would blink. Hence
    //     the pixel comparison is byte-exact and taken on the very next frame.
    //
    // The world push rides along: a shadow-atlas rebuild (the other half of the
    // item — three hand-written read-before-write guards became one
    // Scene::setShadowSettings) drops and re-adds every workspace, which
    // workspaceGeneration counts.
    {
        doc->skyType = iris::SkyType::GRADIENT;
        doc->gradientTop = QColor(30, 60, 220);
        doc->gradientMid = QColor(120, 160, 255);
        doc->gradientBot = QColor(230, 120, 40);
        doc->gradientOffset = 0.0f;
        // A chrome cube in near-darkness: everything it shows is the reflection
        // cubemap, so a re-convolution shows up as a changed pixel.
        target->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.02f, 0.02f, 0.02f));
        auto chrome = iris::PbrMaterial::create();
        chrome->setValue("baseColor", QColor(255, 255, 255));
        chrome->setValue("metallic", 1.0f);
        chrome->setValue("roughness", 0.1f);
        meshNode->setMaterial(chrome);
        doc->getRootNode()->addChild(meshNode);
        mirror.applySky(view);
        mirror.sync();
        mirror.applyEnvironment(view, engine.get());
        for (int i = 0; i < 4; ++i) engine->renderOneFrame();
        Image before; CHECK(view->readPixels(before), "idempotency: the settled frame reads back");
        show("gradient sky + chrome cube (settled)", before);
        const SkyDesc settled = target->sky();
        const unsigned ws0 = view->workspaceGeneration();
        float settledSh[27] = { 0.0f };
        std::printf("    sky desc: mode=%d equirect=%u  sky SH band0 %.3f %.3f %.3f  ws gen %u\n",
                    int(settled.mode), settled.equirect,
                    target->skyAmbientSh(settledSh) ? settledSh[0] : -1.0f, settledSh[1],
                    settledSh[2], ws0);
        CHECK(settled.mode == SkyMode::Equirectangular && settled.equirect != 0,
              "a gradient sky bakes to an equirect image the engine holds");
        // THE HOST NO LONGER RESAMPLES ANYTHING (SKY-GPU): the reflection half
        // of the description is empty because the ENGINE captures the sky it
        // drew into a cubemap and convolves that. The proof is the chrome cube
        // below (and the metal cube above), plus the integral being non-zero.
        CHECK(!settled.reflections && settled.reflectionFaces[0] == 0,
              "...and the host pushes no reflection faces: the engine captures its own sky");
        CHECK(target->skyAmbientSh(settledSh) && settledSh[0] > 0.0f,
              "...which it also integrates for the ambient");
        for (int i = 0; i < 5; ++i) {
            mirror.applySky(view);
            mirror.applyEnvironment(view, engine.get());
            mirror.sync();
            engine->renderOneFrame();
        }
        Image after; CHECK(view->readPixels(after), "idempotency: the frame after five re-pushes reads back");
        show("gradient sky + chrome cube (after 5 identical pushes)", after);
        CHECK(target->sky() == settled,
              "five more pushes of an unchanged sky re-bake nothing (the description, "
              "texture ids and all, is the one the engine already had)");
        CHECK(view->workspaceGeneration() == ws0,
              "...and rebuild no workspace (no shadow-atlas churn from the world push)");
        CHECK(after.rgba == before.rgba,
              "...and the pixels are byte-identical (no reflection re-convolution blink)");
        // Restore for what follows.
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        doc->skyColor = QColor(0, 0, 255);
        doc->setSkyTexture(iris::Texture2DPtr());
        meshNode->setMaterial(legacyOrange);
        doc->getRootNode()->removeChild(meshNode);
        target->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        mirror.applySky(view);
        mirror.sync(); for (int i = 0; i < 2; ++i) engine->renderOneFrame();
    }

    // ---- editor ground grid (EDITOR_SHORTCUTS_SPEC §3) ----
    // Empty scene, flat blue sky: every non-blue pixel is the grid. Looking
    // straight down from y=10 the ±100-unit grid fills the frame.
    {
        cam->setLocalPos(iris::Vec3(0.0f, 10.0f, 0.01f));
        cam->lookAt(iris::Vec3(0, 0, 0));
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

    // ---- deep audit 2026-09 (area 5): mesh swaps, detach, decal-atlas reuse ----
    //
    // Everything below runs on its own document, so the accumulated nodes of the
    // tests above cannot colour the pixel counts.
    {
        auto doc2 = iris::Scene::create();
        auto sun2 = iris::LightNode::create();
        sun2->intensity = 1.5f;
        sun2->setLocalRot(iris::Quat::fromEulerAngles(-40.0f, 20.0f, 0.0f));
        sun2->setLocalPos(iris::Vec3(0.0f, 8.0f, 0.0f));
        doc2->getRootNode()->addChild(sun2);

        auto subject = iris::MeshNode::create();
        subject->setName("subject");
        auto red = iris::PbrMaterial::create();
        red->setBaseColor(QColor(230, 60, 20));
        red->setMetallicFactor(0.0f);
        red->setRoughnessFactor(0.6f);
        subject->setMaterial(red);
        doc2->getRootNode()->addChild(subject);

        // The two shapes are chosen so that COVERAGE alone tells them apart from
        // this camera: a solid cube fills a large square, while the flat XZ
        // plane is seen edge-on and covers almost nothing. No radius
        // normalisation — the node's scale never changes, only its mesh.
        const QString kCube  = QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj");
        const QString kPlane = QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj");
        // SILHOUETTE area, not lit area: anything that is not the blue clear
        // colour. Shading varies with the mesh's normals, coverage does not —
        // and coverage is what a mesh swap changes.
        auto litPixels = [&](const Image &i) {
            int n2 = 0;
            for (unsigned y = 0; y < i.height; ++y)
                for (unsigned x = 0; x < i.width; ++x)
                    if (i.at(x, y).b < 0.5f) ++n2;
            return n2;
        };

        mirror.setSource(doc2);
        mirror.setLightWires(false);
        // Far enough back that BOTH shapes fit inside the frame with room to
        // spare — the whole assertion is a silhouette-area comparison.
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 5.5f), Vec3(0, 0, 0));

        subject->setMesh(testmesh::load(kCube));
        CHECK(!!subject->getMesh(), "mesh swap: cube loaded");
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("subject = cube", img);
        const int cubePx = litPixels(img);
        std::printf("    cube covers:  %d px\n", cubePx);
        CHECK(cubePx > 1000, "mesh swap: the cube renders");

        // THE REGRESSION: swap the MESH on a live node, keeping the material.
        // Entry::meshPtr was written and never read, so this changed nothing in
        // the engine — which is why the mesh picker is commented out in the
        // properties panel and the material preview replaced whole nodes.
        subject->setMesh(testmesh::load(kPlane));
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("subject = plane", img);
        const int planePx = litPixels(img);
        std::printf("    plane covers: %d px\n", planePx);
        CHECK(planePx < cubePx / 4,
              "mesh swap reaches the engine (the edge-on plane covers far less than the cube)");

        // ...and back, so the swap is not a one-way accident.
        subject->setMesh(testmesh::load(kCube));
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(std::abs(litPixels(img) - cubePx) < cubePx / 40,
              "mesh swap: swapping back restores the cube");

        // Clearing the mesh detaches: the node stays, the geometry goes.
        subject->setMesh(iris::MeshPtr());
        CHECK(!subject->getMesh(), "mesh detach: the document node has no mesh");
        mirror.sync();
        const quint64 stillMirrored = mirror.mirroredNodeCount();
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img); show("subject = no mesh", img);
        CHECK(stillMirrored == 2, "mesh detach: the node is still mirrored (light + subject)");
        CHECK(mirror.engineNode(subject.data()) != 0, "mesh detach: its engine node survives");
        CHECK(litPixels(img) == 0, "mesh detach: nothing renders once the mesh is cleared");

        // Re-attaching after a detach must work (the entry is reused).
        subject->setMesh(testmesh::load(kCube));
        mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(litPixels(img) > 1000, "mesh detach: giving the node a mesh again re-attaches it");
    }

    // ---- decal atlas slices survive world switches (area 5) ----
    //
    // The atlas is a fixed 32-slice, process-wide budget, and mDecalTextures
    // used to survive setSource: opening world after world exhausted it and
    // every decal in the session then projected nothing, silently. Open more
    // worlds than the atlas has slices and assert the last one still projects.
    {
        const unsigned capacity = target->decalAtlasCapacity(DecalMap::Diffuse);
        std::printf("    decal atlas capacity: %u\n", capacity);
        const QString decalImg = QDir::current().filePath("mirror_atlas_decal.png");
        { QImage px(4, 4, QImage::Format_RGBA8888); px.fill(QColor(255, 30, 30)); px.save(decalImg); }

        bool projected = false;
        Colour bare2{}, withDecal{};
        const unsigned rounds = capacity + 4u;
        for (unsigned round = 0; round < rounds; ++round) {
            auto wdoc = iris::Scene::create();
            auto wlight = iris::LightNode::create();
            wlight->intensity = 1.5f;
            wlight->setLocalRot(iris::Quat::fromEulerAngles(-90.0f, 0.0f, 0.0f));
            wlight->setLocalPos(iris::Vec3(0.0f, 8.0f, 0.0f));
            wdoc->getRootNode()->addChild(wlight);
            auto floor2 = iris::MeshNode::create();
            floor2->setMesh(testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj")));
            floor2->setLocalScale(iris::Vec3(4, 4, 4));
            auto white = iris::PbrMaterial::create();
            white->setBaseColor(QColor(240, 240, 240));
            white->setRoughnessFactor(0.9f);
            floor2->setMaterial(white);
            wdoc->getRootNode()->addChild(floor2);

            mirror.setSource(wdoc);
            mirror.setLightWires(false);
            auto topCam = iris::CameraNode::create();
            topCam->setLocalPos(iris::Vec3(0, 6, 0));
            topCam->setLocalRot(iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), -90.0f));
            topCam->update(0.0f);
            mirror.applyCamera(topCam, view);
            mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
            view->readPixels(img);
            bare2 = centre(img);

            // A DIFFERENT image path per world: same-path decals would share one
            // atlas slice and the budget would never be reached.
            const QString perWorld = QDir::current().filePath(
                QStringLiteral("mirror_atlas_decal_%1.png").arg(round));
            { QImage px(4, 4, QImage::Format_RGBA8888); px.fill(QColor(255, 30, 30)); px.save(perWorld); }
            auto d2 = iris::DecalNode::create();
            d2->width = 3.0f; d2->height = 3.0f; d2->depth = 2.0f;
            d2->textureGuid = QStringLiteral("guid-%1").arg(round);
            d2->resolvedTexturePath = perWorld;
            wdoc->getRootNode()->addChild(d2);
            mirror.sync(); for (int i = 0; i < 3; ++i) engine->renderOneFrame();
            view->readPixels(img);
            withDecal = centre(img);
            projected = withDecal.r > bare2.r + 0.05f || withDecal.g < bare2.g - 0.05f;
            if (!projected) {
                std::printf("    world %u: decal did NOT project (bare %.2f %.2f %.2f, "
                            "with %.2f %.2f %.2f)\n", round,
                            bare2.r, bare2.g, bare2.b, withDecal.r, withDecal.g, withDecal.b);
                break;
            }
        }
        std::printf("    after %u world switches: decal still projects = %s\n",
                    rounds, projected ? "yes" : "no");
        CHECK(projected, "decals still project after more world switches than the atlas has slices");
    }

    // ---- an IDLE VCT scene never re-solves its GI (the debounce contract) ----
    //
    // WHY THIS IS A GATE. applyEnvironment asks the engine to re-solve global
    // illumination whenever the lights have moved, and for VCT "re-solve" means
    // tearing the voxelizer down and rebuilding it from every item in the scene
    // — hundreds of milliseconds on a real world. It is also INVISIBLE: a
    // scene that re-voxelizes on every frame renders exactly the same picture
    // as one that does not, so nothing but a counter can tell them apart, and
    // the only symptom is a frame rate. Showroom (the one shipped VCT sample)
    // was suspected of exactly this on 2026-09-04; it was not doing it, and
    // this is the guard that keeps the answer true.
    //
    // The debounce rests on a document invariant: an idle node's CACHED
    // globalTransform is bit-stable. That is asserted here too, and with light
    // wires ON, because the helper icons read a light's world position through
    // getGlobalTransform() — which recomputes the whole parent chain and
    // overwrites the cache — while update() writes the same field from the
    // dirty flags. Two writers, one field: if they ever disagreed by an ulp the
    // signature would flip every frame and this test would say so.
    {
        auto gdoc = iris::Scene::create();
        gdoc->giMode = iris::GiMode::VCT;
        gdoc->giQuality = iris::GiQuality::LOW;     // 32^3 voxels: this is a counter test
        gdoc->giUpdateBudget = 1;
        auto gfloor = iris::MeshNode::create();
        gfloor->setName("floor");
        gfloor->setMesh(testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj")));
        gfloor->setLocalScale(iris::Vec3(4, 4, 4));
        auto gmat = iris::PbrMaterial::create();
        gmat->setBaseColor(QColor(220, 220, 220));
        gfloor->setMaterial(gmat);
        gdoc->getRootNode()->addChild(gfloor);
        // Two lights, because the mirror's VCT signature covers EVERY light, and
        // one of them is scaled and rotated so the compose is not a translation.
        auto gsun = iris::LightNode::create();
        gsun->setName("gi sun");
        gsun->lightType = iris::LightType::Directional;
        gsun->intensity = 2.0f;
        gsun->setLocalRot(iris::Quat::fromEulerAngles(-55.0f, 25.0f, 0.0f));
        gsun->setLocalPos(iris::Vec3(0.0f, 6.0f, 0.0f));
        gdoc->getRootNode()->addChild(gsun);
        auto gpanel = iris::LightNode::create();
        gpanel->setName("gi panel");
        gpanel->lightType = iris::LightType::Area;   // Showroom's shape: a scaled area panel
        gpanel->intensity = 3.0f;
        gpanel->rectWidth = 2.0f;
        gpanel->rectHeight = 2.0f;
        gpanel->setLocalPos(iris::Vec3(-1.5f, 2.6f, 0.0f));
        gpanel->setLocalScale(iris::Vec3(4.0f, 1.0f, 4.0f));
        gdoc->getRootNode()->addChild(gpanel);

        mirror.setSource(gdoc);
        mirror.setLightWires(true);        // the getGlobalTransform() path, on purpose
        auto gcam = iris::CameraNode::create();
        gcam->setLocalPos(iris::Vec3(0, 3, 5));
        gcam->update(0.0f);
        mirror.applyCamera(gcam, view);

        const quint64 push0 = mirror.giPushCount(), refresh0 = mirror.giRefreshCount();
        mirror.sync();
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
        const quint64 pushAfterFirst = mirror.giPushCount();
        CHECK(pushAfterFirst == push0 + 1, "GI idle: the first applyEnvironment pushes VCT exactly once");
        CHECK(mirror.giRefreshCount() == refresh0, "GI idle: the first push is not also a refresh");

        // The world transforms the signature is built from, captured once.
        const iris::Mat4 sun0 = gsun->getGlobalTransform(), panel0 = gpanel->getGlobalTransform();

        bool stable = true;
        for (int f = 0; f < 60; ++f) {
            mirror.sync();
            mirror.applyEnvironment(view, engine.get());
            engine->renderOneFrame();
            if (!(gsun->getGlobalTransform() == sun0) || !(gpanel->getGlobalTransform() == panel0)) stable = false;
        }
        CHECK(stable, "GI idle: every light's cached globalTransform is bit-identical after 60 frames");
        // ...and the cache agrees with a fresh recompute, so the two writers of
        // that field can never hand the signature two different answers.
        CHECK(gsun->getGlobalTransform() == sun0 && gpanel->getGlobalTransform() == panel0,
              "GI idle: getGlobalTransform() recomputes to the same bits the cache holds");
        CHECK(mirror.giPushCount() == pushAfterFirst,
              "GI idle: 60 idle frames pushed no new GI configuration");
        CHECK(mirror.giRefreshCount() == refresh0,
              "GI idle: 60 idle frames asked for ZERO GI re-solves");

        // A light that really moves must still re-solve — exactly once, but NOT
        // on the frame it moved (REFLECTIONS_ADOPTION_SPEC.md P2, re-pinned in
        // that lane). The move ARMS a pending refresh; the expensive re-solve
        // fires once the light has held still for the coalescing window, which
        // is what stops a DRAG from costing one full re-voxelize per frame.
        // Everything the old contract cared about still holds — one move, one
        // re-solve, and idleness afterwards — it just happens a few frames later.
        gpanel->setLocalPos(iris::Vec3(1.5f, 2.6f, 0.0f));
        mirror.sync();
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
        CHECK(mirror.giRefreshCount() == refresh0,
              "GI: the frame a light MOVES does not re-solve (the coalescing gate)");
        for (int f = 0; f < 20; ++f) {
            mirror.sync();
            mirror.applyEnvironment(view, engine.get());
            engine->renderOneFrame();
        }
        CHECK(mirror.giRefreshCount() == refresh0 + 1,
              "GI: once the light holds still, moving it re-solved exactly once");

        // Moving GEOMETRY is deliberately NOT a re-solve (GI_SPEC: the mirror
        // pushes every item's transform every frame; flagging that would
        // re-voxelize forever).
        for (int f = 0; f < 10; ++f) {
            gfloor->setLocalPos(iris::Vec3(0.0f, -0.01f * f, 0.0f));
            mirror.sync();
            mirror.applyEnvironment(view, engine.get());
            engine->renderOneFrame();
        }
        CHECK(mirror.giRefreshCount() == refresh0 + 1, "GI: moving a MESH does not re-solve");

        // ---- A HOVER PREVIEW COSTS NO GI (MATERIAL-PREVIEW-1) --------------
        //
        // The editor's hover preview swaps the material on the mesh under the
        // cursor and swaps it back when the drag moves on; the document is never
        // written and no undo step exists, so a GI re-solve charged to a hover
        // would be a re-solve for a state that will never be saved.
        //
        // WHAT IT REALLY COSTS, MEASURED (2026-09-19, ledger 804-805). The MIRROR
        // asks for no re-solve on a hover, flagged or not: its material term is
        // the engine's generation counter, which noteMaterialChanged bumps for a
        // material whose voxel inputs change WHILE a GI-visible item wears it — a
        // colour-only material swapped onto a node bumps nothing. (The lane's
        // first "+1 on entry" was the PREVIOUS section's pending settle firing
        // inside this arm's frames; every arm below DRAINS first, because a
        // counter read across a section boundary measures the last section's
        // debt.) The cost that IS paid is the engine's own, and this counter
        // could not see it: a material-pointer change RE-ATTACHED the item and
        // attachMesh invalidated the GI caches whole — every cascade
        // re-voxelised, on the way in and on the way out. MATERIAL-SWAP-GI-1
        // (2026-09-20) made a material-only change an IN-PLACE SWAP
        // (Scene::setNodeMaterial) that invalidates the item's own box; arm A
        // asserts the mirror takes that path, gi.material_swap measures what
        // the engine then pays. Arm B records that the flag does no harm, and
        // arm C that it is a gate and not a mute.
        {
            auto previewTarget = gfloor;
            const iris::MaterialPtr originalMat = previewTarget->getMaterial();

            auto borrowed = iris::PbrMaterial::create();
            borrowed->setBaseColor(QColor(12, 240, 33));
            borrowed->setRoughnessFactor(0.11f);

            const auto settle = [&](int frames) {
                for (int f = 0; f < frames; ++f) {
                    mirror.sync();
                    mirror.applyEnvironment(view, engine.get());
                    engine->renderOneFrame();
                }
            };
            // Read until the counter stops moving: two still windows in a row.
            const auto drain = [&]() {
                for (int round = 0; round < 8; ++round) {
                    const quint64 before = mirror.giRefreshCount();
                    settle(40);
                    if (mirror.giRefreshCount() == before) return;
                }
            };

            // ARM A: unflagged. One hover in, one hover out.
            drain();
            const quint64 beforeA = mirror.giRefreshCount();
            const quint64 swaps0 = mirror.materialSwapCount();
            const quint64 attaches0 = mirror.meshAttachCount();
            previewTarget->setMaterial(borrowed);
            settle(20);
            const quint64 afterHoverA = mirror.giRefreshCount();
            previewTarget->setMaterial(originalMat);
            settle(20);
            const quint64 afterRestoreA = mirror.giRefreshCount();
            std::printf("info: GI per hover, UNFLAGGED: enter +%llu, leave +%llu; material swaps +%llu, "
                        "mesh re-attaches +%llu\n",
                        (unsigned long long)(afterHoverA - beforeA),
                        (unsigned long long)(afterRestoreA - afterHoverA),
                        (unsigned long long)(mirror.materialSwapCount() - swaps0),
                        (unsigned long long)(mirror.meshAttachCount() - attaches0));
            CHECK(afterRestoreA == beforeA,
                  "GI preview (measured): a colour-only hover asks the MIRROR for no re-solve, in or out");
            // MATERIAL-SWAP-GI-1: the engine's half. A hover used to be two
            // RE-ATTACHES (detach + create, the GI caches invalidated whole);
            // it is two IN-PLACE SWAPS now — Scene::setNodeMaterial, which
            // invalidates the item's own box (gi.material_swap measures the
            // cascades that pays).
            CHECK(mirror.materialSwapCount() - swaps0 == 2 && mirror.meshAttachCount() - attaches0 == 0,
                  "GI preview: a hover in and out is two in-place material swaps and no re-attach");

            // ARM B: the same gesture with the scene saying a preview is on
            // screen. Nothing arms — and, the half that is easy to get wrong,
            // nothing ADOPTS either, so the restore lands back on the signature
            // the debounce already remembered and is not itself a change.
            //
            // A FRESH material, because arm A has already taught the engine
            // about `borrowed`: re-using it would prove nothing.
            auto borrowed2 = iris::PbrMaterial::create();
            borrowed2->setBaseColor(QColor(240, 12, 200));
            borrowed2->setRoughnessFactor(0.83f);

            drain();
            const quint64 beforeB = mirror.giRefreshCount();
            ++gdoc->materialPreviewDepth;
            previewTarget->setMaterial(borrowed2);
            settle(20);
            CHECK(mirror.giRefreshCount() == beforeB,
                  "GI preview: a FLAGGED hover held past the stability window asks for NO re-solve");
            previewTarget->setMaterial(originalMat);
            --gdoc->materialPreviewDepth;
            settle(20);
            CHECK(mirror.giRefreshCount() == beforeB,
                  "GI preview: ending the preview asks for no re-solve either (the signature never moved)");

            // ...and the gate is a GATE, not a mute: an edit to a material the
            // scene is wearing, after the preview has ended, still costs its
            // one re-solve. (An in-place edit, which is what the material panel
            // and `material.set` do — the path the engine's generation counter
            // is actually keyed on.)
            drain();
            const quint64 beforeC = mirror.giRefreshCount();
            if (auto pbr = originalMat.dynamicCast<iris::PbrMaterial>())
                pbr->setBaseColor(QColor(200, 30, 30));
            settle(20);
            CHECK(mirror.giRefreshCount() == beforeC + 1,
                  "GI preview: a COMMITTED material edit after the preview still re-solves, once");
        }

        // Leave the process-wide HlmsPbs VCT binding as we found it.
        gdoc->giMode = iris::GiMode::OFF;
        mirror.sync();
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
        mirror.setLightWires(false);
    }

    // ---- PICKING: the broad phase really is the engine's ------------------
    // SCENEGRAPH_SPEC §2. `iris::picking::raycastMeshes` is the ONE
    // segment/mesh implementation behind both Scene::rayCast and Studio's
    // ScenePicker (audit F13). Its broad phase is Ogre's RaySceneQuery when the
    // engine holds the scene's geometry, and the document's own bounds walk
    // when it does not — this pins BOTH, and that they agree.
    {
        auto pdoc = iris::Scene::create();
        auto cube = iris::MeshNode::create();
        cube->setMesh(testmesh::load(":assets/models/cube.obj"));
        cube->setMaterial(iris::PbrMaterial::create());
        cube->setName("pick-cube");
        cube->setLocalPos(iris::Vec3(0, 0, 0));
        pdoc->getRootNode()->addChild(cube, false);

        const iris::Vec3 a(0, 0, 20), b(0, 0, -20);

        // (1) NOT mirrored yet: no engine geometry for this document, so the
        // fallback broad phase answers — and it must still hit.
        auto pre = iris::picking::raycastMeshes(pdoc.data(), a, b);
        CHECK(!iris::picking::lastUsedEngineBroadPhase(),
              "picking: an unmirrored document uses the document broad phase");
        CHECK(!pre.isEmpty() && pre.first().node == cube,
              "picking: ...and still finds the cube, with a triangle index");
        CHECK(!pre.isEmpty() && pre.first().triangleIndex >= 0,
              "picking: the fallback path reports a triangle index");

        // (2) mirrored: the Items exist, so RaySceneQuery is the broad phase.
        SceneMirror pmirror(target);
        pmirror.setSource(pdoc);
        pmirror.sync();
        engine->renderOneFrame();   // query AABBs come from the last updateSceneGraph
        auto post = iris::picking::raycastMeshes(pdoc.data(), a, b);
        CHECK(iris::picking::lastUsedEngineBroadPhase(),
              "picking: a mirrored document uses Ogre's RaySceneQuery broad phase");
        CHECK(!post.isEmpty() && post.first().node == cube,
              "picking: the engine broad phase finds the same cube");
        CHECK(!post.isEmpty() && post.first().triangleIndex >= 0,
              "picking: ...and the triangle narrow phase is still ours");

        // A ray that misses finds nothing through either phase.
        const iris::Vec3 mA(50, 50, 20), mB(50, 50, -20);
        CHECK(iris::picking::raycastMeshes(pdoc.data(), mA, mB).isEmpty(),
              "picking: a ray past the geometry hits nothing");

        // `pickable` is honoured exactly: the flag reaches the Item as an Ogre
        // QUERY FLAG (the mirror pushes it) and is re-checked on the candidate.
        cube->setPickable(false);
        pmirror.sync();
        engine->renderOneFrame();
        CHECK(iris::picking::raycastMeshes(pdoc.data(), a, b).isEmpty(),
              "picking: an unpickable node is not a hit");
        CHECK(!iris::picking::raycastMeshes(pdoc.data(), a, b, 0, true).isEmpty(),
              "picking: ...unless the caller asks for unpickable nodes too");
        cube->setPickable(true);

        // pickingGroups stays an ALL-test on the candidates (Ogre's mask is an
        // ANY-test and cannot express it).
        cube->pickingGroups = 0x3;
        pmirror.sync();
        CHECK(!iris::picking::raycastMeshes(pdoc.data(), a, b, 0x1).isEmpty(),
              "picking: pickingGroups 0x3 satisfies a 0x1 mask");
        CHECK(iris::picking::raycastMeshes(pdoc.data(), a, b, 0x4).isEmpty(),
              "picking: ...and not a 0x4 one");
        cube->pickingGroups = 0;

        pmirror.setSource(nullptr);
    }

    // ---- LIGHTING CHANNELS through the mirror -----------------------------
    // The document field is on SceneNode and has to reach the engine twice: as
    // the object-side mask on a mesh node's Item, and inside the LightDesc for
    // a light. Both are CHANGE-GUARDED, which is the half that breaks silently:
    // a guard that never notices a change means the second edit never lands.
    // (The pixel proof that the masks then filter light is lights.masks.)
    {
        auto ldoc = iris::Scene::create();
        auto cube = iris::MeshNode::create();
        cube->setMesh(testmesh::load(":assets/models/cube.obj"));
        cube->setMaterial(iris::PbrMaterial::create());
        cube->setName("channels-cube");
        ldoc->getRootNode()->addChild(cube, false);
        auto lamp = iris::LightNode::create();
        lamp->setLightType(iris::LightType::Point);
        lamp->setName("channels-light");
        ldoc->getRootNode()->addChild(lamp, false);

        SceneMirror lmirror(target);
        lmirror.setSource(ldoc);
        lmirror.sync();
        const auto cubeNode = lmirror.engineNode(cube.data());
        const auto lampNode = lmirror.engineNode(lamp.data());
        CHECK(cubeNode != 0 && lampNode != 0, "channels: both nodes reached the engine");
        CHECK(target->nodeLightMask(cubeNode) == 0xFFFFFFFFu,
              "channels: a node the user never touched is on EVERY channel");

        cube->setLightMask(0x2u);
        lmirror.sync();
        CHECK(target->nodeLightMask(cubeNode) == 0x2u,
              "channels: the document's mask reached the Item");
        // The change guard: a SECOND edit must land too.
        cube->setLightMask(0x4u);
        lmirror.sync();
        CHECK(target->nodeLightMask(cubeNode) == 0x4u,
              "channels: a second edit is not swallowed by the push guard");
        // And an idle sync must not undo it.
        lmirror.sync();
        CHECK(target->nodeLightMask(cubeNode) == 0x4u, "channels: an idle sync changes nothing");

        // The LIGHT half rides the LightDesc, whose operator== had to grow the
        // field — a comparison that ignores the mask would drop this edit.
        lamp->setLightMask(0x8u);
        lmirror.sync();
        CHECK(lamp->getLightMask() == 0x8u, "channels: the light carries its own mask");
        // The engine records the mask on the node either way (a light node has
        // no Item, so this is the record, not the Item's flag).
        CHECK(target->nodeLightMask(lampNode) == 0x8u,
              "channels: the light node's mask reached the engine too");
        // A duplicate carries the channels: a copied object that silently
        // rejoined every channel would be the classic "why is this one lit?".
        auto copy = cube->duplicate();
        CHECK(copy->getLightMask() == 0x4u, "channels: a duplicate keeps its channels");

        lmirror.setSource(nullptr);
    }

    // ---- A LAMP THAT MOVES IS A LIGHT WRITE, WHOEVER MOVED IT -------------
    //
    // (MIRROR-LAMPSIG-1; the refused half of LAMPREST-3, ledger §716.)
    //
    // The renderer's voxel light injection reads each light's DERIVED pose at
    // the moment it runs, and two of its optimisations ask "have the lights
    // changed since the injection I am about to trust?" — the in-motion light
    // tick, which skips a cascade a rebuild already injected with the same
    // lights, and the incremental settle, which restarts when a light moves
    // under it. Both read Scene::lightWriteSerial().
    //
    // NOTHING COULD ADVANCE THAT SERIAL FOR A LAMP THAT SIMPLY MOVED. Since the
    // scene-graph adoption the document's nodes ARE the renderer's nodes, so a
    // dragged, carried or animated lamp writes its transform straight into the
    // graph: setNodeTransform is never called (it refuses an adopted node
    // outright), and setLight is not called either because the light's
    // DESCRIPTION did not change — the mirror's own comment says so. The
    // renderer's pose was right and its serial stood still, so an injection was
    // skipped or absorbed by a settle that believed it had finished, and the
    // bounce kept the pose the lamp had at the last rebuild. That is what broke
    // `scripting.e2e.movable_lamp_rest` at 3/255 when the mirror-skip half of
    // LAMPREST-3 trusted it.
    //
    // The mirror hashes every lamp's WORLD transform (ancestors included) and
    // says so once per changed frame; these are the three cases that matters
    // for — still, moved itself, carried by a parent.
    {
        auto ldoc = iris::Scene::create();
        ldoc->giMode = iris::GiMode::VCT;
        ldoc->giQuality = iris::GiQuality::LOW;   // 32^3: this is a counter test
        ldoc->giUpdateBudget = 1;
        auto lfloor = iris::MeshNode::create();
        lfloor->setName("lamp-floor");
        lfloor->setMesh(testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj")));
        lfloor->setLocalScale(iris::Vec3(4, 4, 4));
        lfloor->setMaterial(iris::PbrMaterial::create());
        ldoc->getRootNode()->addChild(lfloor, false);
        // The carrier is the whole point: an ordinary empty with a lamp under
        // it — a torch in a hand, a lamp on a rig, a light parented to a prop.
        auto carrier = iris::SceneNode::create();
        carrier->setName("carrier");
        ldoc->getRootNode()->addChild(carrier, false);
        auto torch = iris::LightNode::create();
        torch->setName("torch");
        torch->setLightType(iris::LightType::Point);
        torch->intensity = 2.0f;
        torch->distance = 12.0f;
        torch->setLocalPos(iris::Vec3(0.0f, 2.0f, 0.0f));
        torch->setMobility(iris::Mobility::Movable);   // owner decision O2's lamp
        carrier->addChild(torch, false);

        SceneMirror lampMirror(target);
        lampMirror.setSource(ldoc);
        auto lcam = iris::CameraNode::create();
        lcam->setLocalPos(iris::Vec3(0, 3, 5));
        lcam->update(0.0f);
        lampMirror.applyCamera(lcam, view);
        const auto tick = [&]() {
            lampMirror.sync();
            lampMirror.applyEnvironment(view, engine.get());
            engine->renderOneFrame();
        };
        tick();
        tick();
        const unsigned long long s0 = target->lightWriteSerial();
        tick();
        tick();
        CHECK(target->lightWriteSerial() == s0,
              "light writes: a still scene reports none (two idle frames)");

        torch->setLocalPos(iris::Vec3(1.0f, 2.0f, 0.0f));
        tick();
        const unsigned long long s1 = target->lightWriteSerial();
        CHECK(s1 > s0, "light writes: a lamp that moves ITSELF is a light write");
        tick();
        CHECK(target->lightWriteSerial() == s1,
              "light writes: ...once for the move, not once per frame after it");

        // THE CASE THAT WAS INVISIBLE: the lamp's own transform never changes.
        carrier->setLocalPos(iris::Vec3(0.0f, 0.0f, 3.0f));
        tick();
        const unsigned long long s2 = target->lightWriteSerial();
        CHECK(s2 > s1, "light writes: a lamp CARRIED by its parent is a light write too");
        tick();
        CHECK(target->lightWriteSerial() == s2,
              "light writes: ...and that one settles back to still as well");
        std::printf("    light-write serial: still %llu, own move %llu, carried %llu\n",
                    (unsigned long long)s0, (unsigned long long)s1, (unsigned long long)s2);

        lampMirror.setSource(nullptr);
    }

    // ---- SCENE_STATIC through the mirror ----------------------------------
    // SCENEGRAPH_SPEC §6 rule 3: the engine creates a node's Item in the NODE's
    // memory-manager class, because SceneNode::attachObject throws when the two
    // disagree. Marked BEFORE the mirror attaches, and again after, since both
    // orders happen in the editor.
    {
        auto sdoc = iris::Scene::create();
        auto pre = iris::MeshNode::create();
        pre->setMesh(testmesh::load(":assets/models/cube.obj"));
        pre->setMaterial(iris::PbrMaterial::create());
        pre->setName("static-before-attach");
        sdoc->getRootNode()->addChild(pre, false);
        pre->setMobility(iris::Mobility::Static);
        CHECK(pre->isStaticInGraph(), "static: marked before the mirror ever saw it");

        auto post = iris::MeshNode::create();
        post->setMesh(testmesh::load(":assets/models/cube.obj"));
        post->setMaterial(iris::PbrMaterial::create());
        post->setName("static-after-attach");
        post->setLocalPos(iris::Vec3(3, 0, 0));
        sdoc->getRootNode()->addChild(post, false);

        SceneMirror smirror(target);
        smirror.setSource(sdoc);
        smirror.sync();
        engine->renderOneFrame();
        CHECK(smirror.engineNode(pre.data()) != 0 && smirror.engineNode(post.data()) != 0,
              "static: both nodes reached the engine");

        // Now switch one that ALREADY has an Item: the Item has to travel.
        post->setMobility(iris::Mobility::Static);
        CHECK(post->isStaticInGraph(),
              "static: a node with a live Item still switches (the Item goes with it)");
        smirror.sync();
        engine->renderOneFrame();

        // ...and moving it puts both node and Item back (rule 4) — the GRAPH
        // class only: the user's mobility setting survives a drag since
        // REALTIME_REFLECTIONS_SPEC §3.3.3.
        post->setLocalPos(iris::Vec3(4, 0, 0));
        CHECK(!post->isStaticInGraph(), "static: the move demoted it again");
        CHECK(post->mobility() == iris::Mobility::Static,
              "mobility: ...and the move did NOT clear the user's setting");
        smirror.sync();
        engine->renderOneFrame();
        CHECK(true, "static: the scene still renders after both switches");
        smirror.setSource(nullptr);
    }

    // ---- MOBILITY REACHES THE ENGINE (REALTIME_REFLECTIONS_SPEC §3.3) ------
    // The document resolves "does this move?"; the mirror pushes the answer per
    // node, and the engine records it (lane R2 spends it). What is asserted
    // here is the SEAM: the resolution the document computed is the one the
    // engine holds, including rule 2's inheritance and the play-time soft
    // promotion — none of which is visible in a pixel, which is why it needs a
    // counter.
    {
        auto mdoc = iris::Scene::create();
        auto still = iris::MeshNode::create();
        still->setMesh(testmesh::load(":assets/models/cube.obj"));
        still->setMaterial(iris::PbrMaterial::create());
        still->setName("mobility-still");
        mdoc->getRootNode()->addChild(still, false);

        auto carrier = iris::SceneNode::create();
        carrier->setName("mobility-carrier");
        auto rider = iris::MeshNode::create();
        rider->setMesh(testmesh::load(":assets/models/cube.obj"));
        rider->setMaterial(iris::PbrMaterial::create());
        rider->setName("mobility-rider");
        rider->setLocalPos(iris::Vec3(3, 0, 0));
        carrier->addChild(rider, false);
        mdoc->getRootNode()->addChild(carrier, false);

        SceneMirror mmirror(target);
        mmirror.setSource(mdoc);
        mmirror.sync();
        engine->renderOneFrame();
        const NodeId stillId = mmirror.engineNode(still.data());
        const NodeId riderId = mmirror.engineNode(rider.data());
        CHECK(stillId != 0 && riderId != 0, "mobility: both nodes reached the engine");
        CHECK(!target->nodeMovable(stillId) && !target->nodeMovable(riderId),
              "mobility: a still scene pushes NOTHING as movable");
        CHECK(mmirror.movableNodeCount() == 0, "mobility: ...and the mirror counts zero");
        CHECK(target->mobilityStatus().movableNodes == 0,
              "mobility: ...and so does the engine");

        // The carrier becomes a SIMULATED physics body: it and everything under
        // it move. (Type matters — a body with no type is what the panel's
        // Collision Shape row alone produces, and that does not move.)
        carrier->isPhysicsBody = true;
        carrier->physicsProperty.type = iris::PhysicsType::RigidBody;
        mmirror.sync();
        engine->renderOneFrame();
        CHECK(target->nodeMovable(riderId),
              "mobility: a driver on the PARENT makes the child movable in the engine (rule 2)");
        CHECK(!target->nodeMovable(stillId), "mobility: ...and leaves the still node alone");
        CHECK(mmirror.movableNodeCount() == 2,
              "mobility: the mirror counts the carrier and its rider");
        CHECK(target->mobilityStatus().movableNodes == 2,
              "mobility: the ENGINE holds the same count (the push landed)");
        CHECK(target->mobilityStatus().movableItems == 1,
              "mobility: one of them carries geometry");

        // ...and back. Nothing here is automatic in the other direction for a
        // node that MOVED (§3.3.3), but removing the DRIVER is authoring.
        carrier->isPhysicsBody = false;
        carrier->physicsProperty.type = iris::PhysicsType::None;
        mmirror.sync();
        CHECK(!target->nodeMovable(riderId) && mmirror.movableNodeCount() == 0,
              "mobility: removing the driver puts the branch back (authoring, not automatic)");

        // ---- THE SURPRISE MOVER (O3): play only, warned once, no rebuild ---
        const quint64 refreshesBefore = mmirror.giRefreshCount();
        const quint64 pushesBefore = mmirror.giPushCount();
        mdoc->setPlaying(true);
        mmirror.sync();                       // the frame that sees it standing still
        CHECK(mmirror.mobilityMissCount() == 0, "mobility: standing still during play is not a miss");
        still->setLocalPos(iris::Vec3(0, 2, 0));
        mmirror.sync();
        CHECK(target->nodeMovable(stillId),
              "mobility: a node that starts moving during play is movable from that frame");
        CHECK(mmirror.mobilityMissCount() == 1 &&
              mmirror.lastMobilityMiss() == QStringLiteral("mobility-still"),
              "mobility: ...counted once, naming the node");
        still->setLocalPos(iris::Vec3(0, 3, 0));
        mmirror.sync();
        still->setLocalPos(iris::Vec3(0, 4, 0));
        mmirror.sync();
        CHECK(mmirror.mobilityMissCount() == 1,
              "mobility: ...and NOT once per frame it keeps moving (one warning per play session)");
        CHECK(mmirror.giRefreshCount() == refreshesBefore && mmirror.giPushCount() == pushesBefore,
              "mobility: the soft promotion costs NO GI refresh (that is the whole point of O3)");

        // Play stop clears it: the document drops the soft flag, the mirror
        // re-resolves, and the engine is told.
        mdoc->setPlaying(false);
        mmirror.sync();
        CHECK(!target->nodeMovable(stillId) && mmirror.mobilityMissCount() == 1,
              "mobility: play stop clears the promotion (the ghost bounce goes with it)");

        // An explicit setting reaches the engine the same way.
        still->setMobility(iris::Mobility::Movable);
        mmirror.sync();
        CHECK(target->nodeMovable(stillId), "mobility: an explicit Movable reaches the engine");
        mmirror.setSource(nullptr);
    }

    // ---- THE SUN DRIVES THE SKY, not the other way round (D15) --------------
    // INVERTED from what this block used to assert. The sky's Azimuth/Elevation
    // dials used to rotate the directional light every frame
    // (Scene::applySunCoupling), which is backwards: the world's sun is a light
    // an author places, and the sky is drawn around it. Both are deleted
    // (SKY_LIGHT_SPEC.md §3), so the case is now: rotate the LIGHT, and both the
    // pixels it lights AND the sky's own bake follow.
    {
        auto sdoc = iris::Scene::create();
        // No Sky Light: only the directional lights this scene, so the floor's
        // luminance is the sun's alone.
        sdoc->skyType = iris::SkyType::SINGLE_COLOR; // no sky bake in the picture
        sdoc->skyColor = QColor(0, 0, 255);

        auto floorMat = iris::PbrMaterial::create();
        floorMat->setValue("baseColor", QColor(220, 220, 220));
        floorMat->setValue("roughness", 1.0f);
        floorMat->setValue("metallic", 0.0f);
        auto floor = iris::MeshNode::create();
        floor->setName("sun floor");
        floor->setMesh(testmesh::load(":assets/models/cube.obj"));
        floor->setMaterial(floorMat);
        floor->setLocalScale(iris::Vec3(8.0f, 0.2f, 8.0f));
        floor->setLocalPos(iris::Vec3(0, -1.1f, 0));
        sdoc->getRootNode()->addChild(floor);

        auto sunLight = iris::LightNode::create();
        sunLight->setName("sun");
        sunLight->lightType = iris::LightType::Directional;
        // Deliberately BELOW saturation: a blown-out floor reads 255 at every
        // sun angle and would make the pixel assertion below meaningless.
        sunLight->intensity = 0.9f;
        sunLight->color = QColor(255, 255, 255);
        sunLight->setLocalPos(iris::Vec3(0, 5, 0));
        // A deliberately WRONG starting rotation: the coupling has to be what
        // puts the light where the sun is, not the authored value.
        sunLight->setLocalRot(iris::Quat::fromEulerAngles(0.0f, 0.0f, 0.0f));
        sdoc->getRootNode()->addChild(sunLight);

        // THE SUN IS THE SCENE'S ONE DIRECTIONAL LIGHT (the resolver, unchanged).
        CHECK(sdoc->sunLight() == sunLight, "the sun: the scene's directional IS its sun");
        // Overhead to start with: the light travels straight down.
        sunLight->setLocalRot(iris::Quat());
        const auto travelIs = [&](const char *what, float wx, float wy, float wz) {
            const iris::Vec3 have = sunLight->getLightDir().normalized();
            const float dot = have.x() * wx + have.y() * wy + have.z() * wz;
            std::printf("    %-34s light dir %.3f %.3f %.3f\n", what, have.x(), have.y(), have.z());
            CHECK(dot > 0.999f, what);
        };
        travelIs("the sun: overhead -> light points down", 0.0f, -1.0f, 0.0f);

        // Render it in a CLEAN ROOM: its own engine scene and view, so none of
        // the geometry the blocks above left in `target` can reach the probe.
        Scene *sunScene = engine->createScene("sun-coupling");
        View *sunView = engine->createOffscreenView("sun-coupling", 96, 96, Colour(0, 0, 1));
        CHECK(sunScene && sunView, "sun coupling: clean-room scene + view");
        sunView->setScene(sunScene);
        sunScene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        SceneMirror sunMirror(sunScene);
        sunMirror.setLightWires(false);
        sunMirror.setSource(sdoc);
        enginetest::testCameraLookAt(sunView, Vec3(0.0f, 4.0f, 4.0f), Vec3(0, -1, 0));
        sunMirror.sync();
        sunMirror.applyEnvironment(sunView);
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        sunView->readPixels(img);
        const Colour high = centre(img);
        show("sun overhead", img);

        // Move the sun down to the horizon: same scene, same camera, only the
        // LIGHT rotated.
        sunLight->setLocalRot(iris::Quat::fromEulerAngles(-88.0f, 0.0f, 0.0f));
        travelIs("the sun: low -> light points sideways", 0.0f, -0.035f, 0.999f);
        sunMirror.sync();
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        sunView->readPixels(img);
        const Colour low = centre(img);
        show("sun at the horizon", img);

        // (b) PIXELS: a grazing sun cannot light a horizontal face the way an
        // overhead one does.
        const float dropped = high.r - low.r;
        std::printf("    %-34s high %.3f -> low %.3f (drop %.3f)\n",
                    "sun coupling: floor luminance", high.r, low.r, dropped);
        CHECK(high.r > 0.2f, "the sun: overhead, it lights the floor");
        CHECK(dropped > 0.1f, "the sun: dropping it to the horizon darkens the floor");

        // NOTHING WRITES THE LIGHT'S ROTATION BUT THE AUTHOR (D15). The old
        // coupling rewrote it from the sky every frame; a sync must now leave an
        // authored rotation exactly where it was, whatever the sky is.
        sdoc->skyType = iris::SkyType::REALISTIC;
        const iris::Quat parked = sunLight->getGlobalRotation();
        sunMirror.sync();
        sunMirror.applyEnvironment(sunView);
        const iris::Quat after = sunLight->getGlobalRotation();
        CHECK(qFuzzyCompare(parked.x(), after.x()) && qFuzzyCompare(parked.y(), after.y()) &&
              qFuzzyCompare(parked.z(), after.z()) && qFuzzyCompare(parked.scalar(), after.scalar()),
              "the sun: a realistic sky never moves the light (the steering is gone)");

        sunMirror.setSource(nullptr);
        engine->destroyView(sunView);
        engine->destroyScene(sunScene);
    }

    // ---- ONE gradient ramp, shared with the exporter (F10) -----------------
    // iris::bakeGradientSky is what the mirror's gradient sky path uploads and
    // what src/export/gltfexporter.cpp writes into the web sky; the ramp used
    // to be written out twice. Pin its contract here (row 0 = zenith = the top
    // stop, last row = the bottom stop, the middle stop at `offset`) so the two
    // callers cannot silently disagree again.
    {
        const QColor top(255, 0, 0), mid(0, 255, 0), bot(0, 0, 255);
        const QImage strip = iris::bakeGradientSky(top, mid, bot, 0.5f, 4, 256);
        CHECK(!strip.isNull() && strip.width() == 4 && strip.height() == 256,
              "gradient bake: 4x256 equirect strip");
        CHECK(strip.format() == QImage::Format_RGBA8888,
              "gradient bake: RGBA8888, so constBits() is a straight upload");
        const QColor first = strip.pixelColor(0, 0);
        const QColor last  = strip.pixelColor(0, 255);
        const QColor middle = strip.pixelColor(0, 128);
        std::printf("    gradient bake rows: top %d,%d,%d  mid %d,%d,%d  bottom %d,%d,%d\n",
                    first.red(), first.green(), first.blue(),
                    middle.red(), middle.green(), middle.blue(),
                    last.red(), last.green(), last.blue());
        CHECK(first.red() > 250 && first.green() < 5 && first.blue() < 5,
              "gradient bake: row 0 is the ZENITH stop");
        CHECK(last.blue() > 250 && last.red() < 5 && last.green() < 5,
              "gradient bake: the last row is the nadir stop");
        CHECK(middle.green() > 250 && middle.red() < 8 && middle.blue() < 8,
              "gradient bake: offset 0.5 puts the middle stop at the equator");
        // Degenerate offsets are clamped, never divided by zero.
        CHECK(!iris::bakeGradientSky(top, mid, bot, 0.0f).isNull() &&
              !iris::bakeGradientSky(top, mid, bot, 1.0f).isNull(),
              "gradient bake: offsets 0 and 1 are clamped, not division by zero");
    }

    // ---- ONE DOCUMENT MESH IS ONE ENGINE MESH (ADD-1, 2026-09-15) ----------
    //
    // The mirror keys its engine meshes by the DOCUMENT Mesh pointer
    // (SceneMirror::meshFor), so what decides how many vertex buffers reach the
    // GPU is how many distinct iris::Mesh objects the document holds. It used
    // to hold one per node: every `scene.addPrimitive` ran assimp again, so a
    // scene of 64 spheres was 64 identical parses AND 64 identical v2 uploads
    // (measured through the app: 73 engine meshes for 68 scene nodes; 10 after).
    //
    // Mesh::loadMesh caches its parse now (document.mesh_cache), which is only
    // worth anything if this side really collapses. Both halves are asserted
    // here: the document hands out one MeshPtr for one path, and the engine
    // builds one mesh for it however many nodes carry it.
    {
        auto sdoc2 = iris::Scene::create();
        SceneMirror smirror2(target);
        smirror2.setSource(sdoc2);
        smirror2.sync();
        engine->renderOneFrame();

        jahshaka::engine::ObjectCounts before{};
        CHECK(engine->objectCounts(before), "census: the engine reports its object counts");

        iris::MeshPtr shared;
        QVector<iris::MeshNodePtr> copies;
        for (int i = 0; i < 16; ++i) {
            auto n2 = iris::MeshNode::create();
            n2->setName(QStringLiteral("shared-%1").arg(i));
            n2->setMesh(testmesh::load(":assets/models/cube.obj"));    // the same path, every time
            n2->setMaterial(iris::PbrMaterial::create());
            n2->setLocalPos(iris::Vec3(0.0f, -50.0f - i, 0.0f));   // out of frame
            sdoc2->getRootNode()->addChild(n2, false);
            if (i == 0) shared = n2->getMesh();
            copies.append(n2);
        }
        bool allShared = !shared.isNull();
        for (const auto &n2 : copies) if (n2->getMesh() != shared) allShared = false;
        CHECK(allShared, "document: 16 nodes naming one file hold ONE iris::Mesh");

        smirror2.sync();
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        jahshaka::engine::ObjectCounts after{};
        engine->objectCounts(after);
        std::printf("    census: meshes %u -> %u, nodes %u -> %u for 16 added mesh nodes\n",
                    before.meshes, after.meshes, before.nodes, after.nodes);
        CHECK(after.meshes - before.meshes <= 1,
              "engine: 16 nodes sharing one document mesh cost at most ONE engine mesh");
        CHECK(after.nodes - before.nodes >= 16,
              "engine: ...and they are really there, as 16 engine nodes");

        smirror2.setSource(nullptr);
    }

    mirror.setSource(nullptr);
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
