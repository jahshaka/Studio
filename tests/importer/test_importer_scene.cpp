// THE FBX SCENE IMPORT — BISTRO-IMPORT-1 (SPECS/BISTRO-FIXES-1.md stages 1-3).
//
// The Bistro import (spikes/bistro/findings.md, 2026-10-13) found an FBX scene's
// specular maps silently unbound, its cutouts opaque, its coloured glass white, its
// root called "RootNode", its lights and camera dropped and the whole scene MOVABLE
// because of a "Take 001" that moves one camera. The demo scene is never a test: this
// suite imports OUR fixture (fixtures/make_scene_fbx.py — one of each shape) through
// the REAL pipeline (AssetImportService, the assets.import path) into a throwaway
// store and database, and asserts each behaviour as a COUNT. No display, no GPU: the
// document graph is the headless engine (tests/support/documentgraph.h).
//
// The base's numbers (82d38ccb5, measured by this suite before the fix) are in each
// row's comment: every row but the 2 m box and the opaque cube was RED there
// (14 reds of the 16 rows on the base).
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <functional>

#include "../support/documentgraph.h"
#include "data/database/database.h"
#include "data/project.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/assethelper.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/extentmeasure.h"
#include "services/import/assetimportservice.h"
#include "services/import/importtypes.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", qPrintable(msg)); \
    else { std::printf("FAIL: %s\n", qPrintable(msg)); ++failures; } } while (0)

/// The blob node named `name`, depth first (the blob is the asset's definition: what a
/// placement instantiates).
static QJsonObject findNode(const QJsonObject &node, const QString &name)
{
    if (node.value("name").toString() == name) return node;
    for (const QJsonValue &c : node.value("children").toArray()) {
        const QJsonObject hit = findNode(c.toObject(), name);
        if (!hit.isEmpty()) return hit;
    }
    return QJsonObject();
}

static int countType(const QJsonObject &node, const QString &type)
{
    int n = node.value("type").toString() == type ? 1 : 0;
    for (const QJsonValue &c : node.value("children").toArray()) n += countType(c.toObject(), type);
    return n;
}

static QJsonObject materialOf(const QJsonObject &blob, const QString &meshName)
{
    return findNode(blob, meshName).value("material").toObject().value("values").toObject();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("scene-import-ogre.log");
    CHECK(graph.ok(), QStringLiteral("the headless document graph booted"));
    if (!graph.ok()) return 1;

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/scene_import.db";
    const QString root = cwd + "/scene_import_store";
    QFile::remove(dbPath);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    Database db;
    CHECK(db.initializeDatabase(dbPath), QStringLiteral("fresh database opened"));
    db.createAllTables();
    AssetStorePaths::setRootOverride(root);
    Project project;
    project.setProjectGuid(QStringLiteral("proj-scene-import-0001"));
    AssetImportService service(&db, &project);

    const QString fbx = QString(JAHSHAKA_TEST_SOURCE_DIR) + "/tests/importer/fixtures/scene_features.fbx";
    ImportRequest request;
    request.sourcePath = fbx;
    request.typeHint = static_cast<int>(ModelTypes::Mesh);
    request.projectGuid = project.getProjectGuid();
    const ImportResult result = service.import(request);
    CHECK(result.ok(), QStringLiteral("the fixture imported through the one pipeline (%1)").arg(result.error));
    if (!result.ok()) return 1;
    const QJsonObject blob = QJsonDocument::fromJson(db.fetchAssetData(result.assetGuid)).object();
    const QJsonObject meta = AssetMetadata::ensure(&db, result.assetGuid);
    const QJsonObject summary = meta.value("importSummary").toObject();
    std::printf("summary: %s\n", QJsonDocument(summary).toJson(QJsonDocument::Compact).constData());

    // ---- STAGE 1: THE MATERIALS ------------------------------------------------
    std::printf("--- stage 1: the materials\n");
    // (a) the prepared split pair binds when the named _Specular map is absent.
    //     Base: 0 of 2 bound.
    const QJsonObject cube = materialOf(blob, "SpecCube");
    const int pairBound = int(!cube.value("roughnessMap").toString().isEmpty())
                        + int(!cube.value("metallicMap").toString().isEmpty());
    CHECK(pairBound == 2, QStringLiteral("SpecCube: the split pair binds roughness + metallic (%1 of 2)").arg(pairBound));
    // ...and every map the importer could NOT resolve is said: the summary's count and
    // a warning naming it. The fixture misses exactly one (TintGlass_Normal.png); the
    // absent CubeMat_Specular.png is resolved by its pair and is NOT missing.
    // Base: no summary (-1), no warning (0).
    const int missing = summary.contains("missingMaps") ? summary.value("missingMaps").toArray().size() : -1;
    CHECK(missing == 1, QStringLiteral("the import summary counts ONE missing map (%1)").arg(missing));
    int warned = 0;
    for (const QString &w : result.warnings)
        if (w.contains("TintGlass_Normal.png")) ++warned;
    CHECK(warned == 1, QStringLiteral("the import's warnings name the missing map once (%1)").arg(warned));
    // (b) the BaseColor's alpha holes make a cutout. Base: alphaMode 0.
    const int leafMode = materialOf(blob, "LeafPlane").value("alphaMode").toInt(-1);
    CHECK(leafMode == 1, QStringLiteral("LeafPlane: alphaMode Masked from the BaseColor's alpha (%1)").arg(leafMode));
    // ...and a cut card is seen from both sides. Base: "back".
    const QString leafCull = findNode(blob, "LeafPlane").value("faceCullingMode").toString();
    CHECK(leafCull == "none", QStringLiteral("LeafPlane: the cutout is two-sided (%1)").arg(leafCull));
    // ...and an opaque BaseColor stays opaque. Base: 0 (green there too).
    const int cubeMode = cube.value("alphaMode").toInt(-1);
    CHECK(cubeMode == 0, QStringLiteral("SpecCube: an opaque BaseColor stays opaque (%1)").arg(cubeMode));
    // (c) the FBX's own diffuse colour and opacity. Base: #ffffff, alpha 1, mode 0.
    const QJsonObject glass = materialOf(blob, "GlassBox");
    const QColor glassColor(glass.value("baseColor").toString());
    const int glassMode = glass.value("alphaMode").toInt(-1);
    const double glassAlpha = glass.value("alpha").toDouble(-1);
    // 0.6 / 0.1 linear -> sRGB 203 / 89 (the document's colours are sRGB).
    CHECK(glassColor.red() == 203 && glassColor.green() == 89 && glassColor.blue() == 89,
          QStringLiteral("GlassBox: the FBX diffuse colour is the base colour (%1)").arg(glassColor.name()));
    CHECK(glassMode == 3 && std::fabs(glassAlpha - 0.25) < 1e-4,
          QStringLiteral("GlassBox: opacity 0.25 imports as Glass at alpha 0.25 (mode %1, alpha %2)")
              .arg(glassMode).arg(glassAlpha));

    // ---- STAGE 2: THE SCENE --------------------------------------------------
    std::printf("--- stage 2: the scene\n");
    // (b) the root takes the asset's name. Base: "RootNode".
    CHECK(blob.value("name").toString() == "scene_features",
          QStringLiteral("the root is named after the asset (%1)").arg(blob.value("name").toString()));
    // (c) the file's light and camera are scene nodes. Base: 0 and 0.
    const int lights = countType(blob, "light"), cameras = countType(blob, "camera");
    CHECK(lights == 1, QStringLiteral("the FBX light is a scene light (%1)").arg(lights));
    CHECK(cameras == 1, QStringLiteral("the FBX camera is a camera node (%1)").arg(cameras));
    int directional = 0;
    std::function<void(const QJsonObject &)> dirs = [&](const QJsonObject &n) {
        if (n.value("type").toString() == "light" && n.value("lightType").toString() == "directional")
            ++directional;
        for (const QJsonValue &c : n.value("children").toArray()) dirs(c.toObject());
    };
    dirs(blob);
    CHECK(directional == 1, QStringLiteral("...and it is a directional light (%1)").arg(directional));
    // ...in the engine's unit: the file's key light is the sun of 1 (its 300 % FBX
    // Intensity is a DCC unit; the ratios between a file's lights are what is kept),
    // with the file's colour. Base: no light at all.
    QJsonObject sunLight;
    std::function<void(const QJsonObject &)> findSun = [&](const QJsonObject &n) {
        if (n.value("type").toString() == "light") sunLight = n;
        for (const QJsonValue &c : n.value("children").toArray()) findSun(c.toObject());
    };
    findSun(blob);
    const double sunI = sunLight.value("intensity").toDouble(-1);
    CHECK(std::fabs(sunI - 1.0) < 1e-4, QStringLiteral("...a sun of intensity 1 in the engine's unit (%1)").arg(sunI));

    // The placed document: the fragment the importer builds, in a scene of its own.
    QTemporaryDir extract;
    QStringList names, paths;
    bool embedded = false;
    iris::SceneNodePtr node = AssetHelper::extractTexturesAndMaterialFromMesh(
        fbx, names, paths, embedded, nullptr, extract.path());
    CHECK(!!node, QStringLiteral("the fragment builds"));
    if (!node) return 1;
    auto scene = iris::Scene::create();
    scene->getRootNode()->addChild(node);
    node->update(0.0f);
    // (a) one scale per file: the Null scaled 0.01 over a 200-unit box carrying a pivot
    //     chain is exactly 2 m. Base: 2.000 (the importer composes the file's own chain
    //     once — green on the base; the Wine 1.461x is AUTHORED, see the report).
    iris::SceneNodePtr box;
    std::function<void(const iris::SceneNodePtr &)> findBox = [&](const iris::SceneNodePtr &n) {
        if (n->getName() == "Box2m") box = n;
        for (const auto &c : n->children()) findBox(c);
    };
    findBox(node);
    const extent::Extent e = box ? extent::measureNode(box) : extent::Extent();
    CHECK(e.valid && std::fabs(e.x - 2.0) < 1e-3 && std::fabs(e.y - 2.0) < 1e-3 && std::fabs(e.z - 2.0) < 1e-3,
          QStringLiteral("Box2m under its pivot chain is 2 m on a side (%1 x %2 x %3)").arg(e.x).arg(e.y).arg(e.z));

    // ---- STAGE 3: STATIC BY DEFAULT -------------------------------------------
    std::printf("--- stage 3: mobility from what the Take animates\n");
    // A node the clip KEYS is movable, every other node static. Base: every node
    // movable (the root carried the clip; 16 of 16).
    int movable = 0, fixed = 0;
    QString movers;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
        if (n->resolvedMobility() == iris::Mobility::Movable) { ++movable; movers += n->getName() + " "; }
        else ++fixed;
        for (const auto &c : n->children()) walk(c);
    };
    walk(node);
    CHECK(movable == 1 && movers.trimmed() == "Spinner",
          QStringLiteral("ONE movable node, the Spinner (%1 movable: %2; %3 static)")
              .arg(movable).arg(movers.trimmed()).arg(fixed));
    CHECK(summary.value("movable").toInt(-1) == movable && summary.value("static").toInt(-1) == fixed,
          QStringLiteral("the import summary says how many of each (%1 movable / %2 static)")
              .arg(summary.value("movable").toInt(-1)).arg(summary.value("static").toInt(-1)));

    node.reset();
    scene.reset();
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
