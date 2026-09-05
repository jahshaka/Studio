// Import-security suite — the containment and bounds half of the 2026-09 deep
// audit's List A item 1 (SPECS/DEEP_AUDIT_2026_09.md, area 4).
//
// Everything a model, a light profile or an archive says about itself is FILE
// CONTENT. Three findings in that audit are all the same sentence said three
// ways, and this suite is the executable version of each:
//
//   1. F2 (high) — "no containment on model texture paths": a mesh file names
//      its own textures, so `../../../.ssh/id_rsa` and `/etc/passwd` are legal
//      texture names. They were resolved verbatim, opened, hashed into the
//      content-addressed store as textures of the imported asset, and written
//      into every export of the project: one-hop exfiltration out of any
//      downloaded model. Same for an .obj's `mtllib` line.
//   2. "IES parser unbounded reserve on attacker counts" — the declared angle
//      counts sized two reserve()s and a loop, and the whole file was read
//      into memory first with no cap at all.
//   3. "no size caps anywhere" — the pipeline hashed and parsed whatever it
//      was handed.
//
// Sections:
//   1. MaterialHelper::containedTexturePath as a unit — the rule itself.
//   2. traversal.obj through the ONE pipeline (AssetImportService): the import
//      SUCCEEDS, the escaping reference is gone, a warning says so, and the
//      planted file outside the model's directory is not among the CAS objects
//      the import wrote.
//   3. The .obj `mtllib` line, same treatment.
//   4. IES bounds: declared counts and file size — with the real ring profile
//      as the control that says the limits do not reject genuine content.
//   5. AssetImportService's per-type source size cap.
//   6. Constants: .blend is gone from MODEL_EXTS (its importer is not compiled,
//      so every dialog that offered it promised a runtime failure).
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QTextStream>
#include <cstdio>

#include "irisgl/import/materialhelper.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/iesprofile.h"
#include "services/import/assetimportservice.h"
#include "services/import/importtypes.h"

#include "../support/documentgraph.h"
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString fixture(const char *name)
{
    return QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/") + name;
}

static bool writeText(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << text;
    return true;
}

/// The fixture layout every containment section shares:
///
///   <tmp>/outer/outside_secret.png   the planted "secret" ../../ points at
///   <tmp>/outer/outside_secret.mtl   the same, for the mtllib case
///   <tmp>/outer/inner/model/         traversal.obj + traversal.mtl + beside.png
///
/// Returns the model directory, or an empty string on failure.
static QString buildTraversalTree(const QString &root)
{
    const QString modelDir = root + "/outer/inner/model";
    if (!QDir().mkpath(modelDir)) return QString();

    // The planted files: real, readable, and outside the model's folder.
    QImage secret(4, 4, QImage::Format_RGBA8888);
    secret.fill(QColor(255, 0, 0));
    if (!secret.save(root + "/outer/outside_secret.png", "PNG")) return QString();
    if (!writeText(root + "/outer/outside_secret.mtl",
                   QStringLiteral("newmtl secret\nKd 1 0 0\n"))) return QString();

    // The innocent sibling the basename fallback is allowed to find.
    QImage beside(4, 4, QImage::Format_RGBA8888);
    beside.fill(QColor(0, 255, 0));
    if (!beside.save(modelDir + "/beside.png", "PNG")) return QString();

    for (const char *name : { "traversal.obj", "traversal.mtl", "traversal_mtllib.obj" }) {
        if (!QFile::copy(fixture((QString("traversal/") + name).toUtf8().constData()),
                         modelDir + "/" + name))
            return QString();
    }
    return modelDir;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("importer-security-ogre.log");
    if (!graph.require()) return 1;
    // An .obj's material is a CustomMaterial built from app/shader_defs/
    // Default.shader, which IrisUtils resolves relative to applicationDirPath.
    // Without it the material has NO properties, so no texture value can be
    // set and section 2's positive control (the contained texture still
    // imports) would pass vacuously. Link the source tree's app/ next to the
    // test binary — the same layout the real app has.
    const QString appLink = QCoreApplication::applicationDirPath() + "/app";
    if (!QFileInfo::exists(appLink))
        QFile::link(QString(JAHSHAKA_TEST_SOURCE_DIR "/app"), appLink);
    CHECK(QFileInfo::exists(appLink + "/shader_defs/Default.shader"),
          "0: app/shader_defs/Default.shader is reachable from the test binary");

    // ================= 1. the containment rule itself =================
    {
        QTemporaryDir tmp;
        const QString modelDir = buildTraversalTree(tmp.path());
        CHECK(!modelDir.isEmpty(), "1: traversal tree built");
        if (modelDir.isEmpty()) return 1;
        const QString outerSecret =
            QFileInfo(tmp.path() + "/outer/outside_secret.png").canonicalFilePath();

        iris::MaterialHelper::takeContainmentWarnings();

        const QString plain = iris::MaterialHelper::containedTexturePath("beside.png", modelDir);
        CHECK(QFileInfo(plain).canonicalFilePath() ==
                  QFileInfo(modelDir + "/beside.png").canonicalFilePath(),
              "1: an ordinary relative name resolves beside the model, unchanged");
        CHECK(iris::MaterialHelper::takeContainmentWarnings().isEmpty(),
              "1: and warns about nothing");

        const QString escaped =
            iris::MaterialHelper::containedTexturePath("../../outside_secret.png", modelDir);
        CHECK(escaped != outerSecret && !escaped.startsWith(tmp.path() + "/outer/outside"),
              "1: ../../ NEVER resolves to the file outside the model's folder");
        CHECK(escaped.isEmpty() || QFileInfo(escaped).absolutePath() ==
                                       QFileInfo(modelDir).absoluteFilePath(),
              "1: whatever it does resolve to is inside the model's folder");
        QStringList warnings = iris::MaterialHelper::takeContainmentWarnings();
        CHECK(warnings.size() == 1 && warnings.first().contains("outside_secret.png"),
              "1: the escape is reported once, naming the reference");

        const QString absolute =
            iris::MaterialHelper::containedTexturePath(outerSecret, modelDir);
        CHECK(absolute != outerSecret,
              "1: an ABSOLUTE path outside the folder is contained too "
              "(DCC tools write these constantly)");
        iris::MaterialHelper::takeContainmentWarnings();

        // The innocent case the fallback exists for: an authoring-machine path
        // for a texture that actually ships beside the model.
        const QString fallback = iris::MaterialHelper::containedTexturePath(
            "/some/other/machine/beside.png", modelDir);
        CHECK(QFileInfo(fallback).canonicalFilePath() ==
                  QFileInfo(modelDir + "/beside.png").canonicalFilePath(),
              "1: an escaping name whose BASENAME sits beside the model falls back to it");
        warnings = iris::MaterialHelper::takeContainmentWarnings();
        CHECK(warnings.size() == 1 && warnings.first().contains("instead"),
              "1: and says so");

        CHECK(iris::MaterialHelper::containedTexturePath("*0", modelDir) ==
                  QStringLiteral("*0"),
              "1: assimp's embedded reference \"*0\" is not a path and passes through");
        CHECK(iris::MaterialHelper::containedTexturePath(QString(), modelDir).isEmpty(),
              "1: an empty name stays empty");
        iris::MaterialHelper::takeContainmentWarnings();
    }

    // ================= 2. traversal.obj through the ONE pipeline =================
    {
        QTemporaryDir tmp;
        const QString modelDir = buildTraversalTree(tmp.path());
        if (modelDir.isEmpty()) { std::printf("FAIL: 2: traversal tree\n"); return ++failures; }
        const QString secretPath = tmp.path() + "/outer/outside_secret.png";
        const QString secretOid = AssetCas::hashFile(secretPath);

        const QString cwd = QDir::currentPath();
        const QString dbPath = cwd + "/importsecurity.db";
        const QString root = cwd + "/importsecurity_store";
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
        QDir().mkpath(root);

        Database db;
        CHECK(db.initializeDatabase(dbPath), "2: fresh database opened");
        db.createAllTables();
        AssetStorePaths::setRootOverride(root);

        AssetImportService service(&db, nullptr);
        ImportRequest request;
        request.sourcePath = modelDir + "/traversal.obj";
        const ImportResult result = service.import(request);

        CHECK(result.ok(),
              QString("2: the model still IMPORTS (containment drops references, it does "
                      "not fail the import): %1")
                  .arg(result.ok() ? QStringLiteral("ok") : result.error).toUtf8().constData());

        bool warned = false;
        for (const QString &w : result.warnings)
            if (w.contains(QStringLiteral("outside_secret.png"))) warned = true;
        CHECK(warned,
              QString("2: a warning names the dropped reference (warnings: %1)")
                  .arg(result.warnings.join(QStringLiteral(" | "))).toUtf8().constData());

        CHECK(!secretOid.isEmpty() && !result.objectOids.contains(secretOid),
              "2: the file outside the model's folder is NOT among the CAS objects "
              "this import wrote — nothing outside was staged");

        // Nothing named after it reached the catalog either.
        QSqlDatabase conn = QSqlDatabase::database();
        QSqlQuery q(conn);
        q.prepare("SELECT COUNT(*) FROM assets WHERE name LIKE '%outside_secret%'");
        q.exec();
        CHECK(q.next() && q.value(0).toInt() == 0,
              "2: no catalog row was created for it");

        // The innocent sibling still made it: containment is not a blanket
        // refusal to import textures.
        q.prepare("SELECT COUNT(*) FROM assets WHERE name = 'beside.png'");
        q.exec();
        CHECK(q.next() && q.value(0).toInt() == 1,
              "2: the texture that DOES sit beside the model imported normally "
              "(via the basename fallback)");

        AssetStorePaths::setRootOverride(QString());
        db.closeDatabase();
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
    }

    // ================= 3. the mtllib line =================
    {
        QTemporaryDir tmp;
        const QString modelDir = buildTraversalTree(tmp.path());
        if (modelDir.isEmpty()) { std::printf("FAIL: 3: traversal tree\n"); return ++failures; }
        const QString secretMtlOid = AssetCas::hashFile(tmp.path() + "/outer/outside_secret.mtl");

        const QString cwd = QDir::currentPath();
        const QString dbPath = cwd + "/importsecurity_mtl.db";
        const QString root = cwd + "/importsecurity_mtl_store";
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
        QDir().mkpath(root);

        Database db;
        db.initializeDatabase(dbPath);
        db.createAllTables();
        AssetStorePaths::setRootOverride(root);

        AssetImportService service(&db, nullptr);
        ImportRequest request;
        request.sourcePath = modelDir + "/traversal_mtllib.obj";
        const ImportResult result = service.import(request);

        CHECK(result.ok(),
              QString("3: the model imports: %1")
                  .arg(result.ok() ? QStringLiteral("ok") : result.error).toUtf8().constData());
        CHECK(!secretMtlOid.isEmpty() && !result.objectOids.contains(secretMtlOid),
              "3: `mtllib ../../outside_secret.mtl` did not stage the file outside "
              "the model's folder");
        bool warned = false;
        for (const QString &w : result.warnings)
            if (w.contains(QStringLiteral("outside_secret.mtl"))) warned = true;
        CHECK(warned,
              QString("3: and the escape is reported (warnings: %1)")
                  .arg(result.warnings.join(QStringLiteral(" | "))).toUtf8().constData());

        AssetStorePaths::setRootOverride(QString());
        db.closeDatabase();
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
    }

    // ================= 4. IES bounds =================
    {
        QTemporaryDir tmp;

        // The control FIRST: a real profile must still parse, or the limits
        // below are just a way of refusing user content.
        const IesProfile good = IesProfile::parse(
            QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/scripting/fixtures/ring_profile.ies"));
        CHECK(good.ok && good.numVerticalAngles == 19,
              QString("4: the real ring profile still parses (%1)")
                  .arg(good.ok ? QStringLiteral("ok") : good.error).toUtf8().constData());

        // Declared counts are file content. 250,000 x 250,000 is fifty bytes of
        // text asking for two reserve()s and an int multiply that overflows.
        const QString hostile = tmp.path() + "/hostile_counts.ies";
        writeText(hostile,
                  QStringLiteral("IESNA:LM-63-1995\n[TEST] hostile counts\nTILT=NONE\n"
                                 "1 1000 1 250000 250000 1 2 0 0 0\n1 1 60\n0 90\n0\n1 1\n"));
        const IesProfile counts = IesProfile::parse(hostile);
        CHECK(!counts.ok && counts.error.contains(QStringLiteral("limit")),
              QString("4: an .ies declaring 250000 angles per axis is REJECTED (%1)")
                  .arg(counts.error).toUtf8().constData());

        // And the whole-file read is capped before it happens.
        const QString huge = tmp.path() + "/huge.ies";
        {
            QFile f(huge);
            CHECK(f.open(QIODevice::WriteOnly), "4: the oversized .ies fixture is writable");
            f.write("IESNA:LM-63-1995\nTILT=NONE\n");
            f.resize(9 * 1024 * 1024);   // sparse: no 9 MB of disk traffic
        }
        const IesProfile big = IesProfile::parse(huge);
        CHECK(!big.ok && big.error.contains(QStringLiteral("kilobytes")),
              QString("4: a 9 MB .ies is refused before readAll() (%1)")
                  .arg(big.error).toUtf8().constData());
    }

    // ================= 5. the source size cap =================
    {
        QTemporaryDir tmp;
        const QString cwd = QDir::currentPath();
        const QString dbPath = cwd + "/importsecurity_cap.db";
        const QString root = cwd + "/importsecurity_cap_store";
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
        QDir().mkpath(root);

        Database db;
        db.initializeDatabase(dbPath);
        db.createAllTables();
        AssetStorePaths::setRootOverride(root);
        AssetImportService service(&db, nullptr);

        // A "shader" the size of a game. ShaderImporter reads its whole file
        // into a QJsonDocument; the cap is what stops that.
        const QString fat = tmp.path() + "/fat.shader";
        {
            QFile f(fat);
            CHECK(f.open(QIODevice::WriteOnly), "5: the oversized .shader fixture is writable");
            f.write("{\"name\":\"fat\"}");
            f.resize(65ll * 1024 * 1024);   // sparse
        }
        ImportRequest request;
        request.sourcePath = fat;
        const ImportResult result = service.import(request);
        CHECK(!result.ok() && result.error.contains(QStringLiteral("limit")),
              QString("5: a 65 MB .shader is refused by the size cap (%1)")
                  .arg(result.error).toUtf8().constData());

        AssetStorePaths::setRootOverride(QString());
        db.closeDatabase();
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
    }

    // ================= 6. the extension list =================
    {
        CHECK(!Constants::MODEL_EXTS.contains(QStringLiteral("blend")),
              "6: .blend is not a model extension (ASSIMP_BUILD_BLEND_IMPORTER is not in "
              "irisgl's allowlist, so offering it in a dialog promised a runtime failure)");
        CHECK(!Constants::ANIMATION_EXTS.contains(QStringLiteral("blend")),
              "6: nor an animation extension (ANIMATION_EXTS is derived from MODEL_EXTS)");
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall import-security checks passed\n",
                failures);
    return failures ? 1 : 0;
}
