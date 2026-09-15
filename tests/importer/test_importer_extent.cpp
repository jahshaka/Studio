// IMPORT SETTINGS AND THE MEASUREMENT — SPECS/IMPORT_DIALOG_SPEC.md §9.
//
// An asset's scale, orientation and origin are decided ONCE, at import, and
// BAKED into the asset; every instance is then placed at scale 1. This suite
// drives that through the REAL pipeline (AssetImportService — the
// assets.import path) into a throwaway store/database. No engine, no display,
// no GPU.
//
// It replaces importer.fitsize, whose POLICY half (envelopes, an inferred
// fitScale, a per-instance multiply, assets.setFit, and the ordering rule that
// kept the Avatar module from scaling a fitted character twice) retired with
// the fit itself. What survives here is the MEASUREMENT, plus the settings
// that now decide it.
//
// Sections:
//   1. THE UNIT. The three unit_cube fixtures are one cube declared m / cm /
//      mm, so the extent is arithmetic — and `unitScale` reports what the FILE
//      declared (1 / 0.01 / 0.001), which is the number that made the 100x
//      class of defect invisible for a year.
//   2. THE SETTINGS. `{units:"m"}` on the CENTIMETRE cube overrides the file's
//      declaration and it imports at 1 m; `{scale:2}` doubles it; a rotated
//      import measures the SWAPPED axes. Every claim is checked on the placed
//      fragment AND on the recorded extent, which must agree because the
//      transform is in the geometry and not on a node.
//   3. THE SKINNED CASE (§10's risk, measured by the lane's spike and asserted
//      here through the document): under a uniform k a rig's bind translations
//      scale by k and its bind ROTATIONS are bit-identical, and a clip's
//      position keys scale by k while its rotation keys are bit-identical.
//   4. THE RECORD. The settings hash of an absent record, a `{}` record and a
//      fully-defaulted one are ONE constant — so every row imported before the
//      dialog keys exactly as it always did — and a bad record is refused
//      rather than silently imported at the wrong size.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../support/documentgraph.h"
#include "data/database/database.h"
#include "data/project.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/import/importsettings.h"
#include "irisgl/import/meshbake.h"
#include "services/assethelper.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/extentmeasure.h"
#include "services/import/assetimportservice.h"
#include "services/import/importtypes.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool approx(double a, double b, double tol = 1e-3)
{
    return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b));
}

static QString fixture(const char *relative)
{
    return QString(JAHSHAKA_TEST_SOURCE_DIR) + "/" + relative;
}

/// One import through the real pipeline, with settings. Returns the row guid,
/// the metadata block the pipeline recorded, and the model's document fragment
/// — parsed through the SAME choke point and the same transform, so the
/// fragment and the record describe one thing.
struct Imported
{
    QString guid;
    QJsonObject meta;
    QJsonObject settings;
    iris::SceneNodePtr node;
    iris::ScenePtr scene;      ///< keeps `node` in a scene while it is measured
};

static Imported importModel(AssetImportService &service, const QString &projectGuid,
                            Database &db, const QString &path,
                            const QJsonObject &settings = QJsonObject())
{
    Imported out;
    ImportRequest request;
    request.sourcePath = path;
    request.typeHint = static_cast<int>(ModelTypes::Mesh);
    request.projectGuid = projectGuid;
    request.settings = settings;
    const ImportResult result = service.import(request);
    if (!result.ok()) {
        std::printf("FAIL: import of %s: %s\n", qPrintable(QFileInfo(path).fileName()),
                    qPrintable(result.error));
        ++failures;
        return out;
    }
    out.guid = result.assetGuid;
    out.meta = AssetMetadata::ensure(&db, out.guid);
    out.settings = service.importSettings(out.guid).value("settings").toObject();

    QTemporaryDir extract;
    QStringList names, paths;
    bool embedded = false;
    const iris::ImportTransform xf =
        iris::ImportSettings::fromJson(out.settings).transform();
    out.node = AssetHelper::extractTexturesAndMaterialFromMesh(path, names, paths, embedded,
                                                               nullptr, extract.path(),
                                                               nullptr, xf);
    return out;
}

/// The placed size of an imported fragment, in a scene of its own so global
/// transforms are real.
static extent::Extent placedExtent(Imported &imported)
{
    if (!imported.node) return extent::Extent();
    imported.scene = iris::Scene::create();
    imported.scene->getRootNode()->addChild(imported.node);
    imported.node->update(0.0f);
    return extent::measureNode(imported.node);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // A document node IS an engine node (SCENEGRAPH_SPEC D2): the fragments
    // this suite loads and measures need a scene graph to live in. Headless —
    // Ogre's NULL render system, no display, no GPU.
    enginetest::DocumentGraph graph("extent-document-ogre.log");
    CHECK(graph.ok(), "the headless document graph booted");
    if (!graph.ok()) return 1;

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/extent.db";
    const QString root = cwd + "/extent_store";
    QFile::remove(dbPath);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fresh database opened");
    db.createAllTables();
    AssetStorePaths::setRootOverride(root);

    const QString projectGuid = QStringLiteral("proj-extent-0001");
    Project project;
    project.setProjectGuid(projectGuid);
    AssetImportService service(&db, &project);

    // ---- 1. THE UNIT ------------------------------------------------------
    std::printf("--- section 1: the file's own unit\n");
    struct UnitCase { const char *file; double side; double unitScale; };
    const UnitCase units[] = {
        { "tests/importer/fixtures/unit_cube_m.fbx",  1.0,   1.0   },
        { "tests/importer/fixtures/unit_cube_cm.fbx", 0.01,  0.01  },
        { "tests/importer/fixtures/unit_cube_mm.fbx", 0.001, 0.001 },
    };
    for (const UnitCase &unit : units) {
        Imported imported = importModel(service, projectGuid, db, fixture(unit.file));
        if (imported.guid.isEmpty()) continue;
        const extent::Extent recorded = extent::extentOf(imported.meta);
        CHECK(recorded.valid, QString("%1: an extent was recorded")
                                  .arg(unit.file).toUtf8().constData());
        CHECK(approx(recorded.x, unit.side, 1e-2) && approx(recorded.y, unit.side, 1e-2)
                  && approx(recorded.z, unit.side, 1e-2),
              QString("%1: measures %2 m on a side (recorded %3 x %4 x %5)")
                  .arg(unit.file).arg(unit.side)
                  .arg(recorded.x).arg(recorded.y).arg(recorded.z).toUtf8().constData());
        CHECK(approx(imported.meta.value("unitScale").toDouble(), unit.unitScale, 1e-2),
              QString("%1: unitScale reports the file's declaration (%2)")
                  .arg(unit.file).arg(unit.unitScale).toUtf8().constData());
        // NOTHING is on the node: the transform is in the geometry.
        const extent::Extent placed = placedExtent(imported);
        CHECK(approx(placed.largest(), recorded.largest(), 1e-2),
              QString("%1: the PLACED fragment measures what the record says")
                  .arg(unit.file).toUtf8().constData());
        CHECK(approx(double(imported.node->getLocalScale().x()), 1.0, 1e-6),
              QString("%1: and it is placed at scale 1").arg(unit.file).toUtf8().constData());
    }

    // ---- 2. THE SETTINGS --------------------------------------------------
    std::printf("--- section 2: the import settings\n");
    const QString cmCube = fixture("tests/importer/fixtures/unit_cube_cm.fbx");
    {
        // The file says centimetres and it is a 1 cm cube. "This file is in
        // metres" is the user overriding the declaration: 1 m.
        QJsonObject settings;
        settings["units"] = "m";
        Imported imported = importModel(service, projectGuid, db, cmCube, settings);
        const extent::Extent recorded = extent::extentOf(imported.meta);
        CHECK(approx(recorded.largest(), 1.0, 1e-2),
              QString("{units:'m'} on the cm cube imports at 1 m (got %1)")
                  .arg(recorded.largest()).toUtf8().constData());
        CHECK(imported.settings.value("units").toString() == QLatin1String("m"),
              "and the record reads back the unit that was applied");
    }
    {
        QJsonObject settings;
        settings["scale"] = 2.0;
        Imported imported = importModel(service, projectGuid, db, cmCube, settings);
        const extent::Extent recorded = extent::extentOf(imported.meta);
        CHECK(approx(recorded.largest(), 0.02, 1e-2),
              QString("{scale:2} doubles the cm cube to 0.02 m (got %1)")
                  .arg(recorded.largest()).toUtf8().constData());
        const extent::Extent placed = placedExtent(imported);
        CHECK(approx(placed.largest(), 0.02, 1e-2)
                  && approx(double(imported.node->getLocalScale().x()), 1.0, 1e-6),
              "the scale is in the GEOMETRY: the placed node is still scale 1");
    }
    {
        // A tall, thin subject so the axis swap is visible: rig2.glb is 2 m on
        // Y and ~0 on Z. 90 degrees about +X carries +Y onto +Z.
        const QString rig = fixture("tests/avatar/fixtures/rig2.glb");
        Imported plain = importModel(service, projectGuid, db, rig);
        QJsonObject settings;
        QJsonArray rotate; rotate.append(90); rotate.append(0); rotate.append(0);
        settings["rotate"] = rotate;
        Imported turned = importModel(service, projectGuid, db, rig, settings);
        const extent::Extent a = extent::extentOf(plain.meta);
        const extent::Extent b = extent::extentOf(turned.meta);
        CHECK(a.valid && b.valid && approx(b.z, a.y, 1e-2) && approx(b.y, a.z, 1e-2),
              QString("a rotated import measures the SWAPPED axes "
                      "(%1x%2x%3 -> %4x%5x%6)")
                  .arg(a.x).arg(a.y).arg(a.z).arg(b.x).arg(b.y).arg(b.z)
                  .toUtf8().constData());
    }
    {
        // The origin: +1.5 m on Y is 1.5 METRES, whatever k is (the lane's
        // spike proved the ordering; this is the document-level statement).
        const QString rig = fixture("tests/avatar/fixtures/rig2.glb");
        QJsonObject settings;
        QJsonArray translate; translate.append(0); translate.append(1.5); translate.append(0);
        settings["translate"] = translate;
        settings["scale"] = 2.0;
        Imported imported = importModel(service, projectGuid, db, rig, settings);
        const extent::Extent placed = placedExtent(imported);
        CHECK(placed.valid && approx(placed.minv[1], 1.5, 1e-2),
              QString("{scale:2, translate:[0,1.5,0]} stands the rig on y = 1.5 m, "
                      "not 3 m (got %1)").arg(placed.minv[1]).toUtf8().constData());
        CHECK(approx(placed.y, 4.0, 1e-2),
              QString("and it is 4 m tall at k = 2 (got %1)").arg(placed.y)
                  .toUtf8().constData());
    }

    // ---- 3. THE SKINNED CASE ---------------------------------------------
    std::printf("--- section 3: a rig under a uniform scale\n");
    {
        const QString rig = fixture("tests/avatar/fixtures/rig2.glb");
        const auto bakeOf = [&](double k) {
            iris::ImportSettings settings;
            settings.scale = k;
            return iris::MeshBake::buildFromFile(rig, QStringLiteral("fp"), QString(),
                                                 settings.transform());
        };
        const iris::MeshBake::Model plain = bakeOf(1.0);
        const iris::MeshBake::Model scaled = bakeOf(2.0);
        CHECK(plain.valid && scaled.valid, "the skinned fixture bakes at k = 1 and k = 2");
        if (plain.valid && scaled.valid && !plain.meshes.isEmpty()
            && plain.meshes.size() == scaled.meshes.size()) {
            const iris::SkeletonPtr a = plain.meshes.first()->getSkeleton();
            const iris::SkeletonPtr b = scaled.meshes.first()->getSkeleton();
            CHECK(!!a && !!b && a->bones.size() == b->bones.size(),
                  "both bakes carry the same skeleton");
            bool posScaled = !!a && !!b && a->bones.size() == b->bones.size();
            bool rotIdentical = posScaled;
            if (posScaled) {
                for (int i = 0; i < a->bones.size(); ++i) {
                    const iris::Vec3 pa = a->bones[i]->bindingPos;
                    const iris::Vec3 pb = b->bones[i]->bindingPos;
                    if (!approx(double(pb.x()), double(pa.x()) * 2.0, 1e-4)
                        || !approx(double(pb.y()), double(pa.y()) * 2.0, 1e-4)
                        || !approx(double(pb.z()), double(pa.z()) * 2.0, 1e-4))
                        posScaled = false;
                    const iris::Quat ra = a->bones[i]->bindingRot;
                    const iris::Quat rb = b->bones[i]->bindingRot;
                    if (std::memcmp(&ra, &rb, sizeof(iris::Quat)) != 0) rotIdentical = false;
                }
            }
            CHECK(posScaled, "every bind TRANSLATION scales by k");
            CHECK(rotIdentical, "every bind ROTATION is bit-identical");

            bool keysScaled = !plain.animations.isEmpty()
                              && plain.animations.size() == scaled.animations.size();
            CHECK(keysScaled, "both bakes carry the same clips");
            for (auto it = plain.animations.constBegin();
                 keysScaled && it != plain.animations.constEnd(); ++it) {
                const auto other = scaled.animations.constFind(it.key());
                if (other == scaled.animations.constEnd()) { keysScaled = false; break; }
                for (auto boneIt = it.value()->boneAnimations.constBegin();
                     boneIt != it.value()->boneAnimations.constEnd(); ++boneIt) {
                    const auto otherBone = other.value()->boneAnimations.constFind(boneIt.key());
                    if (otherBone == other.value()->boneAnimations.constEnd()) {
                        keysScaled = false; break;
                    }
                    const auto &ka = boneIt.value()->posKeys->keys;
                    const auto &kb = otherBone.value()->posKeys->keys;
                    if (ka.size() != kb.size()) { keysScaled = false; break; }
                    for (int i = 0; i < ka.size(); ++i) {
                        if (!approx(double(kb[i]->value.y()), double(ka[i]->value.y()) * 2.0,
                                    1e-4))
                            keysScaled = false;
                    }
                }
            }
            CHECK(keysScaled, "every clip POSITION key scales by k");
        }
    }

    // ---- 4. THE RECORD ----------------------------------------------------
    std::printf("--- section 4: the settings record and its hash\n");
    {
        const QString identity = iris::ImportSettings::identityHash();
        CHECK(iris::ImportSettings::hashOf(QJsonObject()) == identity,
              "an ABSENT record hashes as identity");
        CHECK(iris::ImportSettings::hashOf(iris::ImportSettings().toJson()) == identity,
              "a fully-defaulted record hashes as the SAME constant");
        QJsonObject twice;
        twice["scale"] = 2.0;
        CHECK(iris::ImportSettings::hashOf(twice) != identity,
              "different settings hash differently");
        CHECK(iris::MeshBake::fileNameFor(QString(64, 'a')).endsWith(identity + ".jmb"),
              "the bake's NAME carries the settings hash");
        CHECK(iris::MeshBake::fileNameFor(QString(64, 'a'))
                  == iris::MeshBake::fileNameFor(QString(64, 'a'), identity),
              "an empty settings hash names the same bake an identity record does");

        QString error;
        QJsonObject bad;
        bad["scale"] = -1.0;
        iris::ImportSettings::fromJson(bad, &error);
        CHECK(!error.isEmpty(), "a non-positive scale is refused");
        error.clear();
        QJsonObject unknown;
        unknown["siz"] = 2.0;
        iris::ImportSettings::fromJson(unknown, &error);
        CHECK(!error.isEmpty(), "an unknown key is refused");
        error.clear();
        QJsonObject axes;
        QJsonObject pair;
        pair["up"] = "+Y";
        pair["forward"] = "-Y";
        axes["axes"] = pair;
        iris::ImportSettings::fromJson(axes, &error);
        CHECK(!error.isEmpty(), "a non-perpendicular axis pair is refused");

        // A refused record must not reach the pipeline as a silent identity.
        ImportRequest request;
        request.sourcePath = cmCube;
        request.projectGuid = projectGuid;
        request.settings = bad;
        const ImportResult refused = service.import(request);
        CHECK(!refused.ok() && refused.error.contains("scale"),
              "an import with a refused record FAILS rather than importing at the wrong size");
    }

    std::printf(failures ? "\nimporter.extent: %d FAILURES\n" : "\nimporter.extent: all ok\n",
                failures);
    return failures ? 1 : 0;
}
