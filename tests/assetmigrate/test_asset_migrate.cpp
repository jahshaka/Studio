// CAS correctness harness (ASSET_PIPELINE_SPEC §3.1.3 + §3.1.5). The
// phase-2 migration machinery is GONE (owner decision 2026-08-31: Jahshaka
// ships as a NEW app, there is no old user data to migrate) — this suite
// exercises the primitives the ONE import pipeline and the pin world are
// built on, against a fixture store:
//
//   - ingestFile: CAS-first single-file ingest — 2-char fan-out objects,
//     files/asset_files rows with trigger-maintained refcounts, extension
//     canonicalization for known content, idempotency;
//   - dedup: identical bytes under two names/extensions = ONE object;
//   - resolveFile / resolveSource: guid-first resolution, byte-identical;
//   - reference-with-pin: writePin/pinnedOid/resolvePinned — a pin freezes
//     content; new bytes under the same asset move only the pin that is
//     explicitly moved (copy-on-write, invariant I3);
//   - verify: clean store, then detected bit-rot on a corrupted object;
//   - sidecars + rebuildCatalog: a fresh DB reconstructed from sidecars
//     matches (the honest I2 test);
//   - the RETIRED legacy view: no writer creates <root>/<guid>/ any more,
//     but the resolver still reads one.
//
// Headless (offscreen platform); throwaway files in the test working dir.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstdio>

#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetmigration.h"
#include "services/assetstorepaths.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(bytes);
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}

static void insertAsset(const QString &guid, int type, const QString &name, int viewFilter)
{
    QSqlQuery q;
    q.prepare("INSERT INTO assets (guid, type, name, view_filter, collection, author, license, properties) "
              "VALUES (?, ?, ?, ?, 0, 'tester', 'MIT', '{\"metadata\":{\"kind\":\"test\"}}')");
    q.addBindValue(guid);
    q.addBindValue(type);
    q.addBindValue(name);
    q.addBindValue(viewFilter);
    q.exec();
}

static int countRows(QSqlDatabase conn, const QString &sql)
{
    QSqlQuery q(conn);
    q.exec(sql);
    return q.next() ? q.value(0).toInt() : -1;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/cas_test.db";
    const QString root = cwd + "/cas_store_root";
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    const QByteArray contentX(1500, 'X');
    const QByteArray contentY = QByteArray("PNGISH-").repeated(200);
    const QByteArray contentZ = QByteArray("EDITED-").repeated(300);
    CHECK(AssetCas::hashFile(QString()).isEmpty(), "hashFile of a missing path returns empty");

    // ---- fixture: sources on disk + a fresh full-schema database ----
    const QString srcDir = cwd + "/cas_sources";
    QDir(srcDir).removeRecursively();
    writeFile(srcDir + "/a.glb", contentX);
    writeFile(srcDir + "/b.png", contentY);
    writeFile(srcDir + "/dup.bin", contentX);   // same bytes as a.glb — dedup
    writeFile(srcDir + "/edited.glb", contentZ);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fixture database opened");
    db.createAllTables();   // fresh-DB bootstrap creates the FULL final schema

    insertAsset("guidA", 1, "a.glb", 2);
    insertAsset("guidB", 7, "b.png", 3);
    insertAsset("guidC", 1, "ghost.glb", 2);    // DB-only row, no bytes

    QSqlDatabase conn = QSqlDatabase::database();
    {
        QSqlQuery version(conn);
        version.exec("PRAGMA user_version");
        version.next();
        CHECK(version.value(0).toInt() >= 2, "fresh DB bootstraps user_version >= 2 (pin schema)");
        CHECK(countRows(conn, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'project_assets'") == 1,
              "fresh DB bootstraps project_assets directly");
    }

    // ---- ingestFile: CAS-first, roles, dedup ----
    QString oidX, oidY, oidDup, error;
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/a.glb", "guidA", "source", "a.glb", &oidX, &error),
          "ingestFile a.glb");
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/b.png", "guidB", "source", "b.png", &oidY, &error),
          "ingestFile b.png");
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/dup.bin", "guidB", "file", "dup.bin", &oidDup, &error),
          "ingestFile dup.bin");
    CHECK(!AssetCas::ingestFile(conn, root, srcDir + "/missing.bin", "guidA", "file", "missing.bin", nullptr, &error),
          "ingestFile of a missing path fails with a message");

    CHECK(oidX.size() == 64 && oidX == oidX.toLower(), "sha256 oid is 64 lowercase hex chars");
    CHECK(oidX == oidDup, "identical bytes = identical oid (dedup by construction)");
    CHECK(QFileInfo::exists(AssetStorePaths::objectPathIn(root, oidX, "glb")),
          "object for X at objects/<aa>/<oid>.glb");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidX, "glb")) == contentX,
          "object bytes are identical to the source");

    CHECK(countRows(conn, "SELECT COUNT(*) FROM files") == 2, "2 files rows (dedup)");
    CHECK(countRows(conn, "SELECT COUNT(*) FROM asset_files") == 3, "3 asset_files rows");
    CHECK(countRows(conn, "SELECT refcount FROM files WHERE oid = '" + oidX + "'") == 2,
          "dedup'd object has refcount 2 (trigger-maintained)");

    // idempotency: re-ingest changes nothing
    QString oidAgain;
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/a.glb", "guidA", "source", "a.glb", &oidAgain, &error),
          "re-ingest succeeds");
    CHECK(oidAgain == oidX, "re-ingest yields the same oid");
    CHECK(countRows(conn, "SELECT COUNT(*) FROM asset_files") == 3, "idempotent: still 3 asset_files rows");
    CHECK(countRows(conn, "SELECT refcount FROM files WHERE oid = '" + oidX + "'") == 2,
          "idempotent: refcount unchanged on re-ingest");

    // extension canonicalization: same bytes under another extension reuse
    // the recorded object, no sibling copy
    writeFile(srcDir + "/alias.jpeg", contentY);
    QString oidAlias;
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/alias.jpeg", "guidB", "file", "alias.jpeg", &oidAlias, &error),
          "ingest of aliased content succeeds");
    CHECK(oidAlias == oidY, "aliased content maps to the same oid");
    CHECK(countRows(conn, "SELECT COUNT(*) FROM files") == 2, "no new files row for aliased content");

    // ---- resolution ----
    const QString resolved = AssetCas::resolveFile(conn, root, "guidB", "dup.bin");
    CHECK(!resolved.isEmpty() && readFile(resolved) == contentX,
          "resolveFile returns byte-identical content for a dedup'd name");
    QString sourceName;
    const QString source = AssetCas::resolveSource(conn, root, "guidA", &sourceName);
    CHECK(!source.isEmpty() && readFile(source) == contentX && sourceName == "a.glb",
          "resolveSource returns the source-role file + display name");
    CHECK(AssetCas::resolveSource(conn, root, "guidC").isEmpty(),
          "resolveSource of a DB-only row is empty (not an error)");

    // ---- reference-with-pin (phase 4) ----
    CHECK(AssetCas::writePin(conn, "projP", "guidA", oidX), "writePin");
    CHECK(AssetCas::pinnedOid(conn, "projP", "guidA") == oidX, "pinnedOid reads back");
    CHECK(readFile(AssetCas::resolvePinned(conn, root, "projP", "guidA")) == contentX,
          "resolvePinned returns the pinned bytes");
    CHECK(readFile(AssetCas::resolvePinned(conn, root, "projQ", "guidA")) == contentX,
          "an unpinned project falls back to the current source");

    // copy-on-write: new bytes ingest under the SAME guid; only the moved
    // pin sees them (the library mapping keeps its original oid — the
    // asset_files PK holds it)
    QString oidZ;
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/edited.glb", "guidA", "source", "a.glb", &oidZ, &error),
          "COW ingest of edited bytes");
    CHECK(oidZ != oidX, "edited bytes are a new object");
    CHECK(AssetCas::writePin(conn, "projQ", "guidA", oidZ), "projQ pins the edit");
    CHECK(readFile(AssetCas::resolvePinned(conn, root, "projQ", "guidA")) == contentZ,
          "projQ renders the edited bytes");
    CHECK(readFile(AssetCas::resolvePinned(conn, root, "projP", "guidA")) == contentX,
          "projP still renders its ORIGINAL pinned bytes (I3)");
    CHECK(readFile(AssetCas::resolveSource(conn, root, "guidA")) == contentX,
          "the library mapping is untouched by the project edit");

    // ---- sidecars + store info ----
    CHECK(AssetCas::writeSidecar(conn, root, "guidA", &error), "sidecar for guidA");
    CHECK(AssetCas::writeSidecar(conn, root, "guidB", &error), "sidecar for guidB");
    CHECK(AssetCas::writeSidecar(conn, root, "guidC", &error), "sidecar for the DB-only row");
    CHECK(AssetCas::writeStoreInfo(root, &error), "store.json written");
    CHECK(QFileInfo::exists(AssetStorePaths::sidecarPathIn(root, "guidC")),
          "file-less row still gets a sidecar");

    // ---- the RETIRED legacy view: still READ, never written ----
    // materializeLegacyView is gone (deep audit 2026-09, area 6) — nothing
    // hardlinks <root>/<guid>/ any more. The resolver keeps reading such a
    // folder so an old store, and the writers that still create one outside
    // the ONE import pipeline (the materials module's texture import), stay
    // resolvable until assets.gc reclaims it.
    {
        // (i) an asset with NO asset_files row at all, bytes only in the
        //     legacy folder — the materials-module shape
        insertAsset("guidLegacy", 2, "legacy.png", 3);
        writeFile(root + "/guidLegacy/legacy.png", contentY);
        QString legacyName;
        const QString legacyPath = AssetCas::resolveSource(conn, root, "guidLegacy", &legacyName);
        CHECK(readFile(legacyPath) == contentY && legacyName == "legacy.png",
              "resolveSource reads a legacy folder for a row with no asset_files");
        CHECK(readFile(AssetCas::resolveFile(conn, root, "guidLegacy", "legacy.png")) == contentY,
              "resolveFile reads the same legacy folder by name");
        // and nothing materialized a view for the CAS-backed assets
        CHECK(!QFileInfo::exists(root + "/guidB/b.png"),
              "no per-guid view is created for a CAS-backed asset any more");
    }

    // AssetMigration::verify/rebuildCatalog open the database file themselves,
    // so hand the default connection back first. `conn` is a COPY of that
    // connection and must die before removeDatabase(), or Qt warns
    // "connection 'qt_sql_default_connection' is still in use" and disowns the
    // handle out from under it. (Before the Lane 6b fix, closeDatabase()
    // read the name after invalidating and so never removed anything — the
    // copy was harmless and the connection stayed registered, which is also
    // why the section further down could keep using QSqlDatabase::database()
    // without reopening. Both of those were accidents of the bug.)
    conn = QSqlDatabase();
    db.closeDatabase();

    // ---- verify: clean, then corrupted ----
    auto verifyReport = AssetMigration::verify(dbPath, root);
    CHECK(verifyReport.ok, "verify: clean store");
    CHECK(verifyReport.objects == 3, "verify walked all three objects");

    const QString objectY = AssetStorePaths::objectPathIn(root, oidY, "png");
    {
        // Break the hardlink first so the corruption can't reach back into
        // the source file.
        const QByteArray original = readFile(objectY);
        QFile::remove(objectY);
        writeFile(objectY, original + "CORRUPTED");
    }
    verifyReport = AssetMigration::verify(dbPath, root);
    CHECK(!verifyReport.ok && verifyReport.corrupt.size() == 1 && verifyReport.corrupt[0] == oidY,
          "verify detects the corrupted object");
    {
        QFile::remove(objectY);
        writeFile(objectY, contentY);   // restore
    }
    CHECK(AssetMigration::verify(dbPath, root).ok, "verify clean again after restore");

    // ---- rebuildCatalog into a FRESH database (the honest I2 test) ----
    const QString rebuiltDb = cwd + "/cas_rebuilt.db";
    QFile::remove(rebuiltDb);
    const auto rebuild = AssetMigration::rebuildCatalog(rebuiltDb, root);
    CHECK(rebuild.ok, "rebuildCatalog succeeded");
    CHECK(rebuild.assets == 3, "rebuild recovered 3 asset rows");
    // 2, not 3: the COW edit's object is referenced only by a project PIN,
    // and sidecars record the asset_files mapping — a rebuilt catalog
    // recovers the library truth; pin-only objects stay on disk and verify
    // still walks them (recorded limitation of sidecar-based recovery).
    CHECK(rebuild.files == 2, "rebuild recovered the 2 library-mapped files rows");
    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "RebuildCheck");
        check.setDatabaseName(rebuiltDb);
        check.open();
        QSqlQuery q(check);
        q.exec("SELECT name, type, view_filter FROM assets WHERE guid = 'guidB'");
        CHECK(q.next() && q.value(0).toString() == "b.png" && q.value(1).toInt() == 7
                  && q.value(2).toInt() == 3,
              "rebuilt row matches (name/type/view_filter)");
        check.close();
    }
    QSqlDatabase::removeDatabase("RebuildCheck");

    // ---- updateNodeMaterial resolves guid texture references (2026-08-31) ----
    // The library rebuild path (ProjectAssets::addToProject in a fresh
    // session) feeds stored definitions into AssetHelper::updateNodeMaterial.
    // Texture values are member asset GUIDS; unresolved they reach
    // Texture2D::load as fake paths ("error loading image: <guid>") and every
    // map silently drops (the mottled-import defect).
    {
        AssetStorePaths::setRootOverride(root);
        // Re-open the default connection: this section talks to the library
        // again (insertAsset, AssetCas::ingestFile, updateNodeMaterial) and
        // closeDatabase() above genuinely removed it.
        CHECK(db.initializeDatabase(dbPath), "default connection reopened");

        const QString texPng = srcDir + "/resolve_me.png";
        {
            QImage img(4, 4, QImage::Format_RGBA8888);
            img.fill(QColor(10, 200, 30));
            img.save(texPng, "PNG");
        }
        const QString texGuid = "11111111-2222-3333-4444-555555555555";
        insertAsset(texGuid, 2 /* Texture */, "resolve_me.png", 3);
        QString oid, err;
        CHECK(AssetCas::ingestFile(QSqlDatabase::database(), root, texPng,
                                   texGuid, "source", "resolve_me.png", &oid, &err),
              "texture ingested for updateNodeMaterial");

        QJsonObject values;
        values["baseColorMap"] = texGuid;   // guid reference, as the importer stores it
        values["roughness"] = 0.25;
        QJsonObject material;
        material["materialType"] = "pbr";
        material["values"] = values;
        QJsonObject definition;
        definition["material"] = material;

        auto meshNode = iris::MeshNode::create();
        auto sceneNode = meshNode.staticCast<iris::SceneNode>();
        AssetHelper::updateNodeMaterial(sceneNode, definition, &db);

        auto pbr = meshNode->getMaterial().dynamicCast<iris::PbrMaterial>();
        CHECK(!pbr.isNull(), "pbr definition rebuilds a PbrMaterial");
        if (pbr) {
            QString stored;
            for (auto prop : pbr->properties)
                if (prop->name == "baseColorMap") stored = prop->getValue().toString();
            CHECK(stored != texGuid, "texture property is NOT the raw guid");
            CHECK(stored.contains("/objects/") && QFileInfo::exists(stored),
                  "texture property resolves to an existing CAS object file");
            CHECK(pbr->useBaseColorMap, "the map actually loaded (no guid reached Texture2D::load)");
        }

        AssetStorePaths::setRootOverride(QString());
    }

    // ---- cleanup ----
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QFile::remove(rebuiltDb);
    QDir(root).removeRecursively();
    QDir(srcDir).removeRecursively();

    printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
