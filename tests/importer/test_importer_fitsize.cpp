// FIT TO SIZE — the import-time size policy (services/fitsize.h).
//
// Owner report 2026-09-09: "everything is coming in messed up in the asset
// module and the avatars". Honouring an FBX's declared unit fixed the
// correctly-declared files (importer.fbx §2) and the Avatar module normalizes
// a character it spawns, but a MIS-declared file dropped into the Assets
// module or a scene still arrived raw — the owner's Dreyar download says
// centimetres, is authored in millimetres and comes in 17.25 m tall; a
// downloaded mask measures 526 units.
//
// So the ONE import pipeline measures every model and records the measurement
// plus the fit it infers. This suite drives that through the REAL pipeline
// (AssetImportService, the assets.importFile path) into a throwaway
// store/database. No engine, no display, no GPU.
//
// Sections:
//   1. MEASUREMENT. The three unit_cube fixtures are one cube declared m / cm
//      / mm, so the extent is arithmetic — and `unitScale` reports what the
//      FILE declared (1 / 0.01 / 0.001), which is the number that made the
//      100x class of defect invisible for a year.
//   2. THE POLICY, both directions and the control: rig2_giant.glb (17.25 m,
//      the Dreyar case) fits DOWN to 1.75, rig2_tiny.glb (0.10 m) fits UP to
//      1.75, rig2.glb (2.00 m) is left exactly alone. Every claim is checked
//      on the PLACED result: the fit applied at the root node and the subtree
//      re-measured, which is what a scene actually gets.
//   3. KIND. A skeleton makes it a character (measured on HEIGHT); everything
//      else is an object (measured on its LARGEST extent), with the wider
//      0.01-100 m band an unclassifiable file has to be given.
//   4. THE OVERRIDE (AssetMetadata::writeFit — the one write assets.setFit
//      makes): a manual scale, a reset back to the automatic fit, and a
//      re-measure that rebuilds the block from the stored source.
//   5. NO DOUBLE NORMALISATION. A fitted character measures inside the Avatar
//      module's plausible band, so avatar::normalizeCharacterHeight — which
//      runs AFTER the fit, on the fitted node — finds nothing to do. The two
//      rules share one set of constants; this asserts the ORDER works.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSqlDatabase>
#include <cmath>
#include <cstdio>

#include "../support/documentgraph.h"
#include "data/database/database.h"
#include "services/assetservice.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "data/project.h"
#include "modules/avatar/avatarpreviewmodel.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/fitsize.h"
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

/// One import through the real pipeline. Returns the row guid and keeps the
/// document fragment the importer produced (what a placement instantiates).
struct Imported
{
    QString guid;
    QJsonObject meta;
    iris::SceneNodePtr node;
    iris::ScenePtr scene;      ///< keeps `node` in a scene while it is measured
};

static Imported importModel(AssetImportService &service, const QString &projectGuid,
                            Database &db, const QString &path)
{
    Imported out;
    ImportRequest request;
    request.sourcePath = path;
    request.typeHint = static_cast<int>(ModelTypes::Mesh);
    request.projectGuid = projectGuid;
    const ImportResult result = service.import(request);
    if (!result.ok()) {
        std::printf("FAIL: import of %s: %s\n", qPrintable(QFileInfo(path).fileName()),
                    qPrintable(result.error));
        ++failures;
        return out;
    }
    out.guid = result.assetGuid;
    out.node = result.node;
    out.meta = AssetMetadata::ensure(&db, out.guid);
    return out;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // A document node IS an engine node (SCENEGRAPH_SPEC D2): the fragments
    // this suite loads and measures need a scene graph to live in. Headless —
    // Ogre's NULL render system, no display, no GPU. Declared first so it dies
    // last, after every node built on it.
    enginetest::DocumentGraph graph("fitsize-document-ogre.log");
    CHECK(graph.ok(), "the headless document graph booted");
    if (!graph.ok()) return 1;

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/fitsize.db";
    const QString root = cwd + "/fitsize_store";
    QFile::remove(dbPath);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fresh database opened");
    db.createAllTables();
    AssetStorePaths::setRootOverride(root);

    const QString projectGuid = QStringLiteral("proj-fitsize-0001");
    Project project;
    project.setProjectGuid(projectGuid);
    AssetImportService service(&db, &project);

    // ---- 1. MEASUREMENT ---------------------------------------------------
    std::printf("--- section 1: the measurement\n");
    struct UnitCase { const char *file; double side; double unitScale; };
    const UnitCase units[] = {
        { "tests/importer/fixtures/unit_cube_m.fbx",  1.0,   1.0   },
        { "tests/importer/fixtures/unit_cube_cm.fbx", 0.01,  0.01  },
        { "tests/importer/fixtures/unit_cube_mm.fbx", 0.001, 0.001 },
    };
    Imported cubes[3];
    for (int i = 0; i < 3; ++i) {
        cubes[i] = importModel(service, projectGuid, db, fixture(units[i].file));
        if (cubes[i].guid.isEmpty()) continue;
        const fitsize::Extent extent = fitsize::extentOf(cubes[i].meta);
        CHECK(extent.valid, QString("%1: an extent was recorded")
                                .arg(units[i].file).toUtf8().constData());
        CHECK(approx(extent.x, units[i].side, 1e-2) && approx(extent.y, units[i].side, 1e-2)
                  && approx(extent.z, units[i].side, 1e-2),
              QString("%1: measures %2 m on a side (recorded %3 x %4 x %5)")
                  .arg(units[i].file).arg(units[i].side)
                  .arg(extent.x).arg(extent.y).arg(extent.z).toUtf8().constData());
        CHECK(approx(cubes[i].meta.value("unitScale").toDouble(), units[i].unitScale, 1e-2),
              QString("%1: unitScale reports the file's declaration (%2)")
                  .arg(units[i].file).arg(units[i].unitScale).toUtf8().constData());
    }

    // ---- 2. THE POLICY, on the PLACED node --------------------------------
    std::printf("--- section 2: the policy, measured on the placed node\n");
    struct RigCase { const char *file; double imported; double placed; bool fitted; };
    const RigCase rigs[] = {
        // the Dreyar case: 17.25 m down to the 1.75 m target
        { "tests/avatar/fixtures/rig2_giant.glb", 17.25, 1.75, true  },
        // the other end: 0.10 m up to the same target
        { "tests/avatar/fixtures/rig2_tiny.glb",   0.10, 1.75, true  },
        // THE CONTROL: 2.00 m is a plausible character and must not move
        { "tests/avatar/fixtures/rig2.glb",        2.00, 2.00, false },
    };
    Imported placedGiant;
    for (const RigCase &rig : rigs) {
        Imported imported = importModel(service, projectGuid, db, fixture(rig.file));
        if (imported.guid.isEmpty()) continue;

        const fitsize::Extent extent = fitsize::extentOf(imported.meta);
        CHECK(approx(extent.height(), rig.imported, 1e-2),
              QString("%1: imported %2 m tall").arg(rig.file).arg(rig.imported)
                  .toUtf8().constData());

        const double scale = fitsize::fitScaleOf(imported.meta);
        CHECK(fitsize::isFitted(scale) == rig.fitted,
              QString("%1: %2").arg(rig.file,
                      rig.fitted ? "a fit was inferred" : "no fit was inferred")
                  .toUtf8().constData());
        CHECK(imported.meta.contains("fitReason") == rig.fitted,
              QString("%1: fitReason is present exactly when a fit was inferred")
                  .arg(rig.file).toUtf8().constData());
        CHECK(imported.meta.value("fitSource").toString() == QLatin1String("auto"),
              QString("%1: fitSource is 'auto'").arg(rig.file).toUtf8().constData());

        // THE PLACEMENT: exactly what SceneEditService::addMaterialMesh does —
        // multiply the fit into the root node's local scale — and then measure
        // the subtree, which is the size a scene actually gets.
        CHECK(!imported.node.isNull(),
              QString("%1: the import produced a document fragment").arg(rig.file)
                  .toUtf8().constData());
        if (imported.node.isNull()) continue;
        fitsize::applyFit(imported.node, scale);
        // A fragment only has world transforms once it is IN a scene (a
        // document node is an engine node — SCENEGRAPH_SPEC D2), exactly as
        // addNodeToScene puts it there before anything measures it.
        imported.scene = iris::Scene::create();
        imported.scene->rootNode->addChild(imported.node);
        imported.scene->refresh();
        const fitsize::Extent placed = fitsize::measureNode(imported.node);
        CHECK(approx(placed.height(), rig.placed, 1e-2),
              QString("%1: the PLACED node measures %2 m tall (got %3)")
                  .arg(rig.file).arg(rig.placed).arg(placed.height()).toUtf8().constData());
        if (QString(rig.file).contains("giant")) placedGiant = imported;
    }

    // ---- 3. KIND ----------------------------------------------------------
    std::printf("--- section 3: kind and envelope\n");
    {
        Imported rig = importModel(service, projectGuid, db,
                                   fixture("tests/avatar/fixtures/rig2.glb"));
        CHECK(rig.meta.value("fitKind").toString() == QLatin1String("character"),
              "a file with a skeleton is a character");
        CHECK(rig.meta.value("hasSkeleton").toBool(),
              "the classification reads the model block's own hasSkeleton");
    }
    {
        // cube.obj is a 2 m unrigged cube: an object, plausible, untouched.
        Imported cube = importModel(service, projectGuid, db,
                                    fixture("app/content/primitives/cube.obj"));
        CHECK(cube.meta.value("fitKind").toString() == QLatin1String("object"),
              "a file with no skeleton is an object");
        CHECK(!fitsize::isFitted(fitsize::fitScaleOf(cube.meta)),
              "a 2 m unrigged cube is plausible and is left at fitScale 1");
    }
    // A correctly-declared CENTIMETRE-scale prop survives: unit_cube_cm.fbx is
    // a 1 cm cube, which is what a coin or a screw measures. The object floor
    // is 1 cm for exactly this reason.
    CHECK(!cubes[1].guid.isEmpty()
              && !fitsize::isFitted(fitsize::fitScaleOf(cubes[1].meta)),
          "a correctly-declared 1 cm object stays at fitScale 1");
    // A millimetre cube is below any plausible asset size: fitted to the
    // object target on its largest extent.
    CHECK(!cubes[2].guid.isEmpty()
              && approx(fitsize::extentOf(cubes[2].meta).largest()
                          * fitsize::fitScaleOf(cubes[2].meta),
                      fitsize::kObject.target, 1e-2),
          "a 1 mm object is fitted to the object target (1 m on its largest side)");
    // The pure policy, at the boundaries — no I/O, just the rule.
    {
        fitsize::Extent e; e.valid = true; e.x = e.y = e.z = 100.0;
        CHECK(!fitsize::compute(e, fitsize::Kind::Object).applied,
              "an object at exactly 100 m (the environment top) is left alone");
        e.x = e.y = e.z = 100.5;
        CHECK(fitsize::compute(e, fitsize::Kind::Object).applied,
              "an object above 100 m is fitted");
        fitsize::Extent none;
        CHECK(!fitsize::compute(none, fitsize::Kind::Character).applied
                  && fitsize::compute(none, fitsize::Kind::Character).scale == 1.0,
              "a file with no geometry to measure is never fitted");
    }

    // ---- 4. THE OVERRIDE --------------------------------------------------
    std::printf("--- section 4: the override (AssetMetadata::writeFit)\n");
    {
        const QString guid = cubes[2].guid;   // the mm cube, auto-fitted x1000
        QString error;
        const QJsonObject manual = AssetMetadata::writeFit(
            &db, guid, AssetMetadata::FitChange::Manual, 3.0, &error, root);
        CHECK(!manual.isEmpty() && error.isEmpty(), "a manual fit is accepted");
        CHECK(approx(fitsize::fitScaleOf(manual), 3.0),
              "the manual scale is what the block now carries");
        CHECK(manual.value("fitSource").toString() == QLatin1String("manual"),
              "fitSource records that a human set it");
        CHECK(approx(fitsize::fitScaleOf(AssetMetadata::ensure(&db, guid, root)), 3.0),
              "the manual fit is PERSISTED (a fresh read serves it)");

        const QJsonObject reset = AssetMetadata::writeFit(
            &db, guid, AssetMetadata::FitChange::Reset, 0.0, &error, root);
        CHECK(!reset.isEmpty() && reset.value("fitSource").toString() == QLatin1String("auto"),
              "reset goes back to the automatic fit");
        CHECK(approx(fitsize::extentOf(reset).largest() * fitsize::fitScaleOf(reset),
                   fitsize::kObject.target, 1e-2),
              "the automatic fit is the one the import computed");

        const QJsonObject remeasured = AssetMetadata::writeFit(
            &db, guid, AssetMetadata::FitChange::Remeasure, 0.0, &error, root);
        CHECK(!remeasured.isEmpty() && error.isEmpty(),
              "re-measure rebuilds the block from the stored source");
        CHECK(approx(fitsize::extentOf(remeasured).largest(),
                   fitsize::extentOf(cubes[2].meta).largest(), 1e-2),
              "a re-measure of unchanged bytes reproduces the import's extent");

        // Refusals: a non-model row and a nonsense scale.
        error.clear();
        CHECK(AssetMetadata::writeFit(&db, QStringLiteral("no-such-guid"),
                                      AssetMetadata::FitChange::Reset, 0.0, &error, root)
                      .isEmpty()
                  && !error.isEmpty(),
              "an unknown guid is refused with a message");
        error.clear();
        CHECK(AssetMetadata::writeFit(&db, guid, AssetMetadata::FitChange::Manual,
                                      -1.0, &error, root).isEmpty()
                  && !error.isEmpty(),
              "a non-positive scale is refused with a message");
    }

    // ---- 5. NO DOUBLE NORMALISATION ---------------------------------------
    std::printf("--- section 5: the avatar path does not normalize a fitted character\n");
    {
        CHECK(!placedGiant.node.isNull(), "the fitted giant rig is available to measure");
        if (!placedGiant.node.isNull()) {
            // The node has already had the asset's fit applied (section 2) —
            // exactly the state avatar.spawn hands to normalizeCharacterHeight.
            const float before = avatar::measureCharacterHeight(placedGiant.node);
            const avatar::HeightNormalization norm =
                avatar::normalizeCharacterHeight(placedGiant.node);
            const float after = avatar::measureCharacterHeight(placedGiant.node);
            CHECK(approx(before, 1.75, 1e-2),
                  "the fitted character measures 1.75 m before the avatar rule runs");
            CHECK(!norm.applied, "the avatar rule finds nothing to do — no second scale");
            CHECK(approx(after, before, 1e-4), "the height is unchanged by the avatar rule");
            // The two rules are ONE band, by construction.
            CHECK(double(avatar::kMinPlausibleHeight) == fitsize::kCharacter.min
                      && double(avatar::kMaxPlausibleHeight) == fitsize::kCharacter.max
                      && double(avatar::kTargetCharacterHeight) == fitsize::kCharacter.target,
                  "the avatar band IS fitsize::kCharacter (one set of constants)");
        }
    }

    // ---- 6. THE LIBRARY ANNOUNCES ITS IMPORTS (lead, 2026-09-09) -----------
    // A verb-driven import (script, MCP, the avatar module) used to be
    // invisible on the Assets page until the next launch: only the page's own
    // dialog added tiles. AssetService::importMesh/importFile now announce the
    // new asset on onLibraryChanged; the page subscribes. Asserted here on the
    // service itself, headless.
    {
        std::printf("--- section 6: the library announces its imports\n");
        AssetService assets(&db, &project);
        QStringList announced;
        assets.onLibraryChanged([&announced](const QString &guid) { announced << guid; });
        const auto ok = assets.importMesh(fixture("tests/importer/fixtures/unit_cube_m.fbx"));
        CHECK(ok.ok(), "a model import through the service succeeds");
        CHECK(announced.size() == 1 && announced.first() == ok.objectGuid,
              "exactly one announcement, carrying the new object's guid");
        const auto bad = assets.importMesh(QStringLiteral("/nonexistent/no-such-model.fbx"));
        CHECK(!bad.ok(), "a failed import fails");
        CHECK(announced.size() == 1, "a failed import announces nothing");
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
