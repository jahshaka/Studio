// commands.preset_copy — THE FIRST EDIT OF A PRESET IS ONE UNDO STEP
// (PRESET-EDIT-1; the owner, joint, 2026-09-21: "only the MASTER materials
// should be locked; if they are added to a project they should be editable
// already").
//
// A preset a project holds is editable there, and the first edit is what makes
// the project its own copy: the bundle is minted with the PRESET'S NAME, the
// project's pin moves off the shared master, the scene's use edges follow, and
// the library master and every other project are untouched. All of it is one
// command, because a half-done copy-on-write is a project pinning nothing or a
// node wearing a material that does not exist.
//
// What this suite pins, against the REAL Database and the REAL
// content-addressed store on a throwaway SQLite file (the tests/commands
// idiom — a pin and an edge are catalog facts and a stub could not tell the
// truth about them):
//
//   1. redo() mints the copy under the preset's own NAME, with the master's
//      definition and its member textures (one object, shared), and records
//      which master it came from.
//   2. THE PIN MOVES: the project holds the copy and lets go of the master.
//      The master's LIBRARY row, its definition and its bytes are untouched.
//   3. THE USE EDGES FOLLOW: a node that wore the master wears the copy.
//   4. undo() puts all three back and takes the copy's row with it — and it
//      does NOT take the master's member textures, which are the master's.
//   5. redo() after an undo re-makes THE SAME GUID. Anything else would leave
//      a redone document pointing at a material that no longer exists.
//   6. A SECOND PROJECT IS NOT TOUCHED: its pin on the master stays exactly
//      where it was, through the copy, the undo and the redo.
//
// Framework-free; non-zero exit on failure. Runs displayless.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cstdio>

#include "commands/presetcopycommand.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"
#include "services/projectassets.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString writeTempFile(const QDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(bytes);
    f.close();
    return path;
}

static void textureRow(Database &db, const QString &guid, const QString &name,
                       const QString &storeRoot, const QString &srcPath)
{
    db.createAssetEntry(guid, name, static_cast<int>(ModelTypes::Texture),
                        QString(), QString(), QString(), QString(), QByteArray(),
                        QByteArray(), QByteArray(), QByteArray(),
                        AssetViewFilter::AssetsView);
    QString oid, err;
    AssetCas::ingestFile(QSqlDatabase::database(), storeRoot, srcPath, guid,
                         QStringLiteral("source"), name, &oid, &err);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QTemporaryDir scratch;
    if (!scratch.isValid()) { printf("FAIL: no scratch dir\n"); return 1; }
    const QDir scratchDir(scratch.path());
    const QString storeRoot = scratchDir.filePath("store");
    QDir().mkpath(storeRoot);
    AssetStorePaths::setRootOverride(storeRoot);

    const QString dbPath = scratchDir.filePath("preset_copy_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    const QString projectGuid = QStringLiteral("project-0001");
    const QString otherGuid = QStringLiteral("project-0002");
    CHECK(db.createProject(projectGuid, QStringLiteral("Copy Test")), "fixture project row");
    CHECK(db.createProject(otherGuid, QStringLiteral("Somebody Else")), "a second project");
    Project project;
    project.setProjectGuid(projectGuid);
    Project other;
    other.setProjectGuid(otherGuid);

    // THE MASTER, AS THE SEEDER LEAVES IT: a Material row on the preset's own
    // RESERVED guid, with a member texture and a definition written through
    // the one door allowed to write a shipped guid (`writeShipped`).
    const QString master = QStringLiteral("00000000-0000-0000-0000-000000002022");  // Wood PBR
    const QString presetName = MaterialBundle::shippedPresetName(master);
    CHECK(presetName == QLatin1String("Wood PBR"), "the reserved guid is Wood PBR's");
    textureRow(db, "tex-wood", "wood.jpg", storeRoot,
               writeTempFile(scratchDir, "wood.jpg", QByteArray("wood-bytes")));
    db.createAssetEntry(master, presetName, static_cast<int>(ModelTypes::Material),
                        QString(), QString(), QString(), QString(), QByteArray("tile"),
                        QByteArray(), QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    {
        QJsonObject values;
        values[QStringLiteral("baseColorMap")] = QStringLiteral("tex-wood");
        values[QStringLiteral("roughness")] = 0.6;
        QJsonObject definition;
        definition[QStringLiteral("materialType")] = QStringLiteral("pbr");
        definition[QStringLiteral("name")] = presetName;
        definition[QStringLiteral("values")] = values;
        const auto written = MaterialBundle::writeShipped(&db, master, definition);
        CHECK(written.ok, "the preset bundle is seeded");
    }
    const QString masterOid = AssetCas::sourceOid(QSqlDatabase::database(), master);
    CHECK(!masterOid.isEmpty(), "…and its definition has a content id");

    // BOTH PROJECTS HOLD IT, and a node in the first one wears it — which is
    // what applying a preset does (a pin and a use edge).
    ProjectAssets::addToProject(master, &db, &project, ProjectAssets::AddKind::Direct);
    ProjectAssets::addToProject(master, &db, &other, ProjectAssets::AddKind::Direct);
    const QString nodeGuid = QStringLiteral("node-0001");
    db.createDependency(static_cast<int>(ModelTypes::Object),
                        static_cast<int>(ModelTypes::Material),
                        nodeGuid, master, projectGuid);
    CHECK(db.isAssetPinnedBy(projectGuid, master) && db.isAssetPinnedBy(otherGuid, master),
          "both projects pin the preset, and one has a mesh wearing it");

    QStringList redressed;
    auto redress = [&redressed](const QString &guid) { redressed << guid; };

    QUndoStack stack;
    stack.push(new PresetCopyCommand(&db, &project, redress, master));

    // ---- 1. the copy ------------------------------------------------------
    const QStringList copies = [&] {
        QStringList out;
        // EVERY Material row: the copy is the PROJECT's own (ASSETS-SCOPE-1),
        // which no library listing contains.
        QSqlQuery q;
        q.prepare("SELECT guid FROM assets WHERE type = ?");
        q.addBindValue(static_cast<int>(ModelTypes::Material));
        q.exec();
        while (q.next())
            if (q.value(0).toString() != master) out << q.value(0).toString();
        return out;
    }();
    CHECK(copies.size() == 1, "1: exactly one new material row");
    const QString copy = copies.value(0);
    CHECK(!copy.isEmpty() && copy != master, "1: …on a guid of its own");
    CHECK(db.fetchAsset(copy).view_filter == AssetViewFilter::Editor
              && db.fetchAsset(copy).projectGuid == projectGuid,
          "1: …the PROJECT'S OWN row (Editor, owned), never a library tile");
    CHECK(db.fetchAsset(copy).name == presetName,
          "1: …carrying the PRESET'S NAME (the user sees one material)");
    CHECK(QJsonDocument::fromJson(db.fetchAsset(copy).properties).object()
              .value(QStringLiteral("presetMaster")).toString() == master,
          "1: …and the row records which master it came from");
    const QJsonObject copyDef = MaterialBundle::read(&db, copy, &project);
    CHECK(copyDef.value("values").toObject().value("baseColorMap").toString()
              == QLatin1String("tex-wood"),
          "1: …the master's definition, naming the SAME member texture (one object, shared)");
    CHECK(db.fetchAsset(copy).thumbnail == QByteArray("tile"),
          "1: …and the preset's tile until something draws the copy");

    // ---- 2. the pin moved, the library did not ----------------------------
    CHECK(db.isAssetPinnedBy(projectGuid, copy), "2: the project pins the copy");
    CHECK(!db.isAssetPinnedBy(projectGuid, master), "2: …and has let go of the master");
    CHECK(!db.fetchAsset(master).guid.isEmpty(), "2: the master's LIBRARY row is untouched");
    CHECK(AssetCas::sourceOid(QSqlDatabase::database(), master) == masterOid,
          "2: …and so is its definition, to the byte");
    CHECK(db.isAssetPinnedBy(projectGuid, "tex-wood"),
          "2: the shared texture is still pinned (the copy names it too)");

    // ---- 3. the use edges follow ------------------------------------------
    CHECK(db.fetchDependers(copy, projectGuid).contains(nodeGuid),
          "3: the mesh that wore the master wears the copy");
    CHECK(!db.fetchDependers(master, projectGuid).contains(nodeGuid),
          "3: …and no longer the master");
    CHECK(redressed.contains(copy), "3: …and the meshes were re-dressed from it");

    // ---- 6a. the second project, mid-copy ---------------------------------
    CHECK(db.isAssetPinnedBy(otherGuid, master),
          "6: the OTHER project's pin on the master is exactly where it was");

    // ---- 4. undo ----------------------------------------------------------
    stack.undo();
    CHECK(db.isAssetPinnedBy(projectGuid, master), "4: undo restores the pin on the master");
    CHECK(!db.isAssetPinnedBy(projectGuid, copy), "4: …drops the pin on the copy");
    CHECK(db.fetchAsset(copy).guid.isEmpty(), "4: …and takes the copy's row with it");
    CHECK(db.fetchDependers(master, projectGuid).contains(nodeGuid),
          "4: …the mesh wears the master again");
    CHECK(!db.fetchAsset("tex-wood").guid.isEmpty()
              && db.isAssetPinnedBy(projectGuid, "tex-wood"),
          "4: …and the MASTER'S texture is not taken with the copy");
    CHECK(AssetCas::sourceOid(QSqlDatabase::database(), master) == masterOid,
          "4: …the master's definition still to the byte");
    CHECK(db.isAssetPinnedBy(otherGuid, master), "6: the other project, still untouched");

    // ---- 5. redo re-makes the same material -------------------------------
    stack.redo();
    CHECK(!db.fetchAsset(copy).guid.isEmpty(), "5: redo re-makes the copy on THE SAME guid");
    CHECK(db.fetchAsset(copy).view_filter == AssetViewFilter::Editor
              && db.fetchAsset(copy).projectGuid == projectGuid,
          "5: …still the project's own row");
    CHECK(db.fetchAsset(copy).name == presetName, "5: …with the same name");
    CHECK(db.isAssetPinnedBy(projectGuid, copy) && !db.isAssetPinnedBy(projectGuid, master),
          "5: …and the pin is on the copy again");
    CHECK(db.fetchDependers(copy, projectGuid).contains(nodeGuid),
          "5: …with the mesh wearing it");
    CHECK(db.isAssetPinnedBy(otherGuid, master), "6: …the other project, through all of it");

    // ---- 7. no project, nothing done --------------------------------------
    //
    // DRIVEN DIRECTLY, NOT THROUGH A STACK (the Fable read's item 3): a
    // QUndoStack OWNS what it is handed and may destroy it inside push, so
    // reading the command back afterwards is the dangling read
    // `presetedit::forEdit` itself refuses to make. The command is a local
    // here, and redo/undo are the same two calls the stack would have made.
    Project placeholder;   // the startup placeholder: a Project with no guid
    {
        PresetCopyCommand refused(&db, &placeholder, PresetCopyCommand::Redress(), master);
        refused.redo();
        CHECK(refused.copyGuid().isEmpty() && !refused.error().isEmpty(),
              "7: with no project there is nowhere to copy to, and it says so");
        refused.undo();
    }
    CHECK(db.isAssetPinnedBy(projectGuid, copy),
          "7: …and its undo changes nothing either");
    CHECK(!db.fetchAsset(copy).guid.isEmpty(), "7: …the project's own copy is still there");

    printf(failures ? "\n%d FAILURES\n" : "\nall preset-copy assertions passed\n", failures);
    return failures ? 1 : 0;
}
