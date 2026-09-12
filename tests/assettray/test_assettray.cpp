/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE TRAY LISTING AND THE LIBRARY GRID, AS QUERIES (small-items round B).
//
// Two things are pinned here, on a realistic library built in a throwaway
// SQLite file with the REAL Database class (the tests/drawers idiom):
//
//   1. THE RULES. services/assettray.h's five collapse rules and the Assets
//      page's library grid (Database::fetchAssetsForAssetView). A USE edge —
//      a texture on a material slot, on the ground, on a decal, on a particle
//      — never hides anything from either listing; an import MEMBER is hidden
//      from both.
//
//   2. THE COST. The tray listing runs on every edge write and on every
//      SEARCH KEYSTROKE (AssetWidget::searchAssets lists every folder per
//      key), so its cost is per-keypress. The suite COUNTS the SQL statements
//      one listing executes (sqlite3_trace_v2 on the driver's own handle) and
//      asserts a per-row-independent bound: the listing must not grow a query
//      per row. That bound is what would have caught the N+1 shape this round
//      replaced — 1 + 4*pins + 2*rows statements for a listing.
//
// Headless (offscreen platform), no display, never touches the user's library.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <cstdio>

#ifdef JAH_HAVE_SQLITE3
#include <sqlite3.h>
#endif

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "services/assettray.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
// The statement counter. Qt's SQLite driver hands out the sqlite3* it opened
// (QSqlDriver::handle), and sqlite3_trace_v2 counts PREPARED STATEMENT starts —
// exactly "how many queries did that call run", with no instrumentation in the
// production path.
static int gStatements = 0;
static bool gCounting = false;

#ifdef JAH_HAVE_SQLITE3
static int traceCb(unsigned, void *, void *, void *)
{
    if (gCounting) ++gStatements;
    return 0;
}
static bool installTrace()
{
    QVariant handle = QSqlDatabase::database().driver()->handle();
    if (!handle.isValid() || qstrcmp(handle.typeName(), "sqlite3*") != 0) return false;
    sqlite3 *h = *static_cast<sqlite3 **>(handle.data());
    if (!h) return false;
    sqlite3_trace_v2(h, SQLITE_TRACE_STMT, traceCb, nullptr);
    return true;
}
#else
static bool installTrace() { return false; }
#endif

struct Measured { int statements = 0; qint64 micros = 0; int rows = 0; };

template <typename Fn>
static Measured measure(Fn fn)
{
    Measured m;
    QElapsedTimer timer;
    gStatements = 0;
    gCounting = true;
    timer.start();
    m.rows = fn();
    m.micros = timer.nsecsElapsed() / 1000;
    gCounting = false;
    m.statements = gStatements;
    return m;
}

// ---------------------------------------------------------------------------
static QString newGuid() { return GUIDManager::generateGUID(); }

static void pin(const QString &projectGuid, const QString &assetGuid)
{
    QSqlQuery q;
    q.prepare("INSERT INTO project_assets (project_guid, asset_guid, oid_pin) VALUES (?, ?, '')");
    q.addBindValue(projectGuid);
    q.addBindValue(assetGuid);
    q.exec();
}

static bool listed(const QVector<AssetRecord> &records, const QString &guid)
{
    for (const auto &r : records) if (r.guid == guid) return true;
    return false;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets (QMessageBox)

    const QString dbPath = "assettray_test.db";
    QFile::remove(dbPath);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();
    const bool tracing = installTrace();
    if (!tracing) printf("note: no sqlite3 trace handle — counts are not asserted\n");

    // ---------------------------------------------------------------------
    // A REALISTIC LIBRARY. 12 imported models (an Object row with a Mesh and
    // two Texture MEMBERS each), 40 standalone library textures, and a project
    // that holds 60 scene-node rows of its own and pins 30 library assets.
    // ---------------------------------------------------------------------
    const QString projectGuid = newGuid();
    CHECK(db.createProject(projectGuid, "Tray Cost"), "project created");

    const QString otherProject = newGuid();
    db.createProject(otherProject, "Someone Else");

    QStringList models, meshes, memberTextures, standalone, sceneNodes;

    for (int i = 0; i < 12; ++i) {
        const QString object = newGuid();
        db.createAssetEntry(object, QStringLiteral("model_%1.glb").arg(i),
                            static_cast<int>(ModelTypes::Object), QString(), QString(),
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QByteArray(), AssetViewFilter::AssetsView);
        models << object;

        const QString mesh = newGuid();
        db.createAssetEntry(mesh, QStringLiteral("model_%1.mesh").arg(i),
                            static_cast<int>(ModelTypes::Mesh), object, QString(),
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QByteArray(), AssetViewFilter::AssetsView);
        db.createDependency(static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh),
                            object, mesh, QString());
        meshes << mesh;

        for (int t = 0; t < 2; ++t) {
            const QString tex = newGuid();
            db.createAssetEntry(tex, QStringLiteral("model_%1_tex%2.png").arg(i).arg(t),
                                static_cast<int>(ModelTypes::Texture), object, QString(),
                                QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                                QByteArray(), AssetViewFilter::AssetsView);
            db.createDependency(static_cast<int>(ModelTypes::Object),
                                static_cast<int>(ModelTypes::Texture), object, tex, QString());
            memberTextures << tex;
        }
    }

    for (int i = 0; i < 40; ++i) {
        const QString tex = newGuid();
        db.createAssetEntry(tex, QStringLiteral("image_%1.png").arg(i),
                            static_cast<int>(ModelTypes::Texture), QString(), QString(),
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QByteArray(), AssetViewFilter::AssetsView);
        standalone << tex;
    }

    // The project's own rows: scene-node markers (rule 4) and plain rows.
    for (int i = 0; i < 60; ++i) {
        const QString guid = newGuid();
        const bool builtin = (i % 3 == 0);
        QJsonObject props;
        if (builtin) props["type"] = "builtin";
        db.createAssetEntry(guid, QStringLiteral("node_%1").arg(i),
                            static_cast<int>(ModelTypes::Object), projectGuid, projectGuid,
                            QString(), QString(), QByteArray(),
                            QJsonDocument(props).toJson(), QByteArray(), QByteArray(),
                            AssetViewFilter::Editor);
        sceneNodes << guid;
    }

    // 30 pins: the models the scene uses and the images its materials bind.
    for (int i = 0; i < 12; ++i) pin(projectGuid, models[i]);
    for (int i = 0; i < 18; ++i) pin(projectGuid, standalone[i]);

    // USE EDGES: every scene-node row binds one library image on a material
    // slot (what MaterialPropertyWidget::updateTextureDependency writes).
    for (int i = 0; i < sceneNodes.size(); ++i)
        db.createDependency(static_cast<int>(ModelTypes::Object),
                            static_cast<int>(ModelTypes::Texture),
                            sceneNodes[i], standalone[i % 18], projectGuid);

    // A COMPANION: an image added directly, plus the material minted for it.
    const QString companionImage = standalone[0];
    const QString companion = newGuid();
    {
        QJsonObject definition;
        definition["companionOf"] = companionImage;
        db.createAssetEntry(companion, "image_0 material",
                            static_cast<int>(ModelTypes::Material), QString(), QString(),
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QJsonDocument(definition).toJson(), AssetViewFilter::AssetsView);
        db.createDependency(static_cast<int>(ModelTypes::Material),
                            static_cast<int>(ModelTypes::Texture),
                            companion, companionImage, projectGuid);
        pin(projectGuid, companion);
    }
    // ...and one the project uses ONLY through its companion (no scene node
    // binds it), which rule 5 folds away.
    const QString foldedImage = standalone[25];
    const QString foldedCompanion = newGuid();
    {
        QJsonObject definition;
        definition["companionOf"] = foldedImage;
        db.createAssetEntry(foldedCompanion, "image_25 material",
                            static_cast<int>(ModelTypes::Material), QString(), QString(),
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QJsonDocument(definition).toJson(), AssetViewFilter::AssetsView);
        db.createDependency(static_cast<int>(ModelTypes::Material),
                            static_cast<int>(ModelTypes::Texture),
                            foldedCompanion, foldedImage, projectGuid);
        pin(projectGuid, foldedImage);
        pin(projectGuid, foldedCompanion);
    }

    // ---------------------------------------------------------------------
    // 1. THE LIBRARY GRID (item 2). A USE edge is not membership.
    // ---------------------------------------------------------------------
    {
        const QVector<AssetRecord> grid = db.fetchAssetsForAssetView();
        // standalone[0..17] are all bound by a scene node's material slot.
        CHECK(listed(grid, standalone[3]),
              "a library image a scene node's material slot uses is STILL in the library grid");
        CHECK(listed(grid, companionImage),
              "an image with a companion material is still in the library grid");
        CHECK(listed(grid, standalone[30]), "an unused library image is in the grid");
        for (const QString &model : models)
            if (!listed(grid, model)) { CHECK(false, "every imported model is in the grid"); break; }
        CHECK(listed(grid, models[0]), "an imported model is in the grid");
        CHECK(!listed(grid, meshes[0]), "an import's MESH member is not a library tile");
        CHECK(!listed(grid, memberTextures[0]),
              "an import's TEXTURE member is not a library tile");
        CHECK(!listed(grid, sceneNodes[1]),
              "a project's own scene-node row is not a library tile (view filter)");
    }

    // ---------------------------------------------------------------------
    // 2. THE TRAY RULES (unchanged by this round — the guard that says so).
    // ---------------------------------------------------------------------
    {
        const QVector<AssetRecord> tray =
            assettray::list(&db, projectGuid, projectGuid);
        CHECK(listed(tray, models[0]), "a pinned model is a tray tile");
        CHECK(listed(tray, standalone[3]), "an image a material slot uses is a tray tile");
        CHECK(!listed(tray, meshes[0]), "a mesh member is never a tray tile");
        CHECK(!listed(tray, memberTextures[0]), "an import member is never a tray tile");
        CHECK(!listed(tray, sceneNodes[0]), "a builtin scene-node row is not a tray tile");
        CHECK(listed(tray, companion), "the companion material is a tile");
        CHECK(listed(tray, companionImage),
              "an image a scene node also uses is a tile of its own, companion or not");
        CHECK(listed(tray, foldedCompanion), "the folded image's companion is a tile");
        CHECK(!listed(tray, foldedImage),
              "an image used ONLY by its companion material folds into it");
    }

    // ---------------------------------------------------------------------
    // 3. THE COST (item 1). One listing, and one SEARCH KEYSTROKE (which
    //    lists the root folder and every subfolder).
    // ---------------------------------------------------------------------
    int rowCount = 0;
    Measured one = measure([&] {
        const auto records = assettray::list(&db, projectGuid, projectGuid);
        rowCount = records.size();
        return records.size();
    });
    printf("measure: one root listing -> %d tiles, %d statements, %lld us\n",
           one.rows, one.statements, static_cast<long long>(one.micros));

    // Six folders, as a project with a handful of drawers has: one keystroke
    // of AssetWidget::searchAssets lists every one of them.
    QStringList folders;
    for (int i = 0; i < 5; ++i) {
        const QString folder = newGuid();
        db.createFolder(QStringLiteral("Folder %1").arg(i), projectGuid, folder, projectGuid, true);
        folders << folder;
        for (int j = 0; j < 6; ++j) {
            const QString guid = newGuid();
            db.createAssetEntry(guid, QStringLiteral("filed_%1_%2.png").arg(i).arg(j),
                                static_cast<int>(ModelTypes::Texture), folder, projectGuid,
                                QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                                QByteArray(), AssetViewFilter::Editor);
        }
    }
    Measured keystroke = measure([&] {
        int n = assettray::list(&db, projectGuid, projectGuid).size();
        for (const QString &folder : folders)
            n += assettray::list(&db, projectGuid, folder).size();
        return n;
    });
    printf("measure: one SEARCH KEYSTROKE (6 folders) -> %d tiles, %d statements, %lld us\n",
           keystroke.rows, keystroke.statements, static_cast<long long>(keystroke.micros));

    if (tracing) {
        // THE SHAPE, not a magic number: a listing is a FIXED number of
        // queries plus a batch per kind of question, never one per row. The
        // N+1 shape this round replaced cost 1 + 4*pins + 2*rows statements
        // (≈ 300 here); anything of that order fails this bound.
        CHECK(one.statements <= 20,
              QStringLiteral("one root listing costs <= 20 statements (measured %1, %2 tiles)")
                  .arg(one.statements).arg(rowCount).toUtf8().constData());
        CHECK(keystroke.statements <= 120,
              QStringLiteral("one search keystroke costs <= 120 statements (measured %1)")
                  .arg(keystroke.statements).toUtf8().constData());
    }

    // ---------------------------------------------------------------------
    // 3b. THE EDITOR'S HIDDEN FOLDERS (item 3). Database::ensureFolder is the
    //     method behind "file this row under the project's Systems/Presets
    //     folder": the writers used to mint a fresh guid every call and create
    //     the folder only the first time, so every emitter after the first and
    //     every applied preset after the first was filed under a guid nothing
    //     owned — a row that exists, resolves, and appears in no folder.
    // ---------------------------------------------------------------------
    {
        const QString first = db.ensureFolder(QStringLiteral("Systems"), projectGuid, false);
        CHECK(!first.isEmpty(), "ensureFolder created the project's Systems folder");
        const QString second = db.ensureFolder(QStringLiteral("Systems"), projectGuid, false);
        CHECK(second == first, "...and answers with the SAME guid the second time");
        QSqlQuery count;
        count.prepare("SELECT COUNT(*) FROM folders WHERE name = 'Systems' AND project_guid = ?");
        count.addBindValue(projectGuid);
        count.exec(); count.next();
        CHECK(count.value(0).toInt() == 1, "...and there is exactly ONE Systems folder");
        CHECK(db.ensureFolder(QStringLiteral("Systems"), otherProject, false) != first,
              "another project gets its own Systems folder");
        CHECK(db.ensureFolder(QString(), projectGuid).isEmpty(), "a nameless folder is refused");

        // A row filed in that folder lists in the FOLDER, and not at the root —
        // even when the project also pins it, which a project archive's import
        // does for every row it brings in.
        const QString filed = newGuid();
        db.createAssetEntry(filed, "Emitter Row", static_cast<int>(ModelTypes::Material),
                            first, projectGuid, QString(), QString(), QByteArray(),
                            QByteArray(), QByteArray(), QByteArray(), AssetViewFilter::Editor);
        pin(projectGuid, filed);
        CHECK(listed(assettray::list(&db, projectGuid, first), filed),
              "a row filed in Systems/ lists in that folder");
        CHECK(!listed(assettray::list(&db, projectGuid, projectGuid), filed),
              "...and a PIN on it does not also put it at the tray root");
    }

    // ---------------------------------------------------------------------
    // 4. THE PROJECT IMPORT'S PARENT REMAP (item 4). A member row's parent is
    //    its OBJECT, and must name the object's NEW guid on the other side.
    // ---------------------------------------------------------------------
    {
        const QString archive = QDir::current().filePath("tray_import_archive");
        QFile::remove(archive + ".db");
        const QString oldProject = newGuid();
        const QString oldObject = newGuid();
        const QString oldMesh = newGuid();
        const QString oldFolder = newGuid();
        const QString oldFiled = newGuid();
        {
            QSqlDatabase src = QSqlDatabase::addDatabase("QSQLITE", "TrayArchiveWrite");
            src.setDatabaseName(archive + ".db");
            CHECK(src.open(), "archive database created");
            QSqlQuery q(src);
            q.exec("CREATE TABLE projects (name TEXT, scene BLOB, thumbnail BLOB, version TEXT, "
                   "last_written DATETIME, last_accessed DATETIME, guid TEXT)");
            q.exec("CREATE TABLE assets (guid TEXT, type INTEGER, name TEXT, collection INTEGER, "
                   "times_used INTEGER, project_guid TEXT, date_created DATETIME, "
                   "last_updated DATETIME, author TEXT, license TEXT, hash TEXT, version TEXT, "
                   "parent TEXT, tags BLOB, properties BLOB, asset BLOB, thumbnail BLOB)");
            q.exec("CREATE TABLE folders (guid TEXT, name TEXT, parent TEXT, count INTEGER, "
                   "project_guid TEXT, date_created DATETIME, last_updated DATETIME, "
                   "visible INTEGER)");
            q.exec("CREATE TABLE dependencies (id TEXT, depender_type INTEGER, "
                   "dependee_type INTEGER, project_guid TEXT, depender TEXT, dependee TEXT)");
            QSqlQuery ins(src);
            ins.prepare("INSERT INTO projects (name, scene, guid) VALUES (?, ?, ?)");
            ins.addBindValue("Imported Project");
            ins.addBindValue(QByteArray("{}"));
            ins.addBindValue(oldProject);
            ins.exec();
            auto asset = [&](const QString &guid, int type, const QString &name,
                             const QString &parent) {
                QSqlQuery a(src);
                a.prepare("INSERT INTO assets (guid, type, name, collection, times_used, "
                          "project_guid, parent) VALUES (?, ?, ?, 0, 0, ?, ?)");
                a.addBindValue(guid); a.addBindValue(type); a.addBindValue(name);
                a.addBindValue(oldProject); a.addBindValue(parent);
                a.exec();
            };
            asset(oldObject, static_cast<int>(ModelTypes::Object), "imported.glb", oldProject);
            asset(oldMesh, static_cast<int>(ModelTypes::Mesh), "imported.mesh", oldObject);
            asset(oldFiled, static_cast<int>(ModelTypes::Texture), "filed.png", oldFolder);
            QSqlQuery f(src);
            f.prepare("INSERT INTO folders (guid, name, parent, count, project_guid, visible) "
                      "VALUES (?, 'Textures', ?, 0, ?, 1)");
            f.addBindValue(oldFolder); f.addBindValue(oldProject); f.addBindValue(oldProject);
            f.exec();
            src.close();
        }
        QSqlDatabase::removeDatabase("TrayArchiveWrite");

        const QString newProject = newGuid();
        QString worldName;
        QMap<QString, QString> guidMap;
        CHECK(db.importProject(archive, newProject, worldName, guidMap), "project imported");

        const QString newObject = guidMap.value(oldObject);
        const QString newMesh = guidMap.value(oldMesh);
        const QString newFiled = guidMap.value(oldFiled);
        CHECK(!newObject.isEmpty() && !newMesh.isEmpty(), "the imported rows have new guids");
        CHECK(db.fetchAsset(newMesh).parent == newObject,
              "a MEMBER row's parent is the importing library's own Object guid");
        CHECK(db.fetchAsset(newObject).parent == newProject,
              "a root row's parent is the new project guid");
        const QString filedParent = db.fetchAsset(newFiled).parent;
        CHECK(!filedParent.isEmpty() && filedParent != oldFolder,
              "a FILED row's parent is the imported folder's new guid");
        QSqlQuery folderQ;
        folderQ.prepare("SELECT COUNT(*) FROM folders WHERE guid = ? AND project_guid = ?");
        folderQ.addBindValue(filedParent);
        folderQ.addBindValue(newProject);
        folderQ.exec(); folderQ.next();
        CHECK(folderQ.value(0).toInt() == 1, "and that folder exists in the importing library");
        QFile::remove(archive + ".db");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall assertions passed\n", failures);
    return failures ? 1 : 0;
}
