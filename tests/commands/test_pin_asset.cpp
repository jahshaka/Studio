// commands.pin_asset — AN APPLY THAT PINS IS AN APPLY THAT UNPINS ON UNDO
// (MATERIAL_BUNDLE_SPEC §4 / the materials audit's F5, phase 3).
//
// Applying a library material makes the project USE it, so the project pins
// it — that is what makes the project render the version it was built with.
// The pin was a bare service call AFTER the undo macro closed, so undoing the
// apply left it behind: the material stayed in the project's tray with its
// textures pinned, describing a scene that no longer existed. With presets it
// was the visible half of the same defect — every apply left something the
// user could not take back.
//
// What this suite pins, against the REAL Database and the REAL
// content-addressed store on a throwaway SQLite file:
//
//   1. redo() pins the asset AND its member closure (the texture a material
//      names rides with it — that is what the closure walk is for).
//   2. undo() takes both back, and touches the LIBRARY not at all: the row,
//      its bytes and its definition are still there afterwards.
//   3. redo() after an undo puts the membership back — the command is a real
//      undo step, not a one-shot.
//   4. IT ONLY TAKES BACK WHAT IT MADE. On an asset the project ALREADY
//      pinned the command is inert: its undo must not remove a membership
//      the user created deliberately, and it must not move that pin either
//      (addToProject re-pins to the library's CURRENT version, which would
//      silently upgrade an older pin — the F20 loss, from the other side).
//   5. A SHARED MEMBER SURVIVES. A texture two materials use keeps its pin
//      when one of them is unpinned — the closure rule is "no depender
//      outside the set being removed", which is the conservative half.
//
// Framework-free; non-zero exit on failure. Runs displayless.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cstdio>

#include "commands/pinassetcommand.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"

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

static QString materialRow(Database &db, const QString &name, const QString &textureGuid,
                           const QString &secondGuid = QString())
{
    QJsonObject values;
    values[QStringLiteral("baseColorMap")] = textureGuid;
    if (!secondGuid.isEmpty()) values[QStringLiteral("normalMap")] = secondGuid;
    QJsonObject definition;
    definition[QStringLiteral("materialType")] = QStringLiteral("pbr");
    definition[QStringLiteral("values")] = values;
    QString error;
    const QString guid = MaterialBundle::create(&db, name, definition, QByteArray(), &error);
    if (guid.isEmpty()) printf("info: material '%s' not created: %s\n",
                               qPrintable(name), qPrintable(error));
    return guid;
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

    const QString dbPath = scratchDir.filePath("pin_asset_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    const QString projectGuid = QStringLiteral("project-0001");
    CHECK(db.createProject(projectGuid, QStringLiteral("Pin Test")), "fixture project row");
    Project project;
    project.setProjectGuid(projectGuid);

    // TWO BUNDLES, ONE SHARED MEMBER: `tex-wood` belongs to woody alone,
    // `tex-shared` to both. That is the whole point of membership by
    // reference, and it is what the closure rules have to get right.
    textureRow(db, "tex-wood", "wood.jpg", storeRoot,
               writeTempFile(scratchDir, "wood.jpg", QByteArray("wood-bytes")));
    textureRow(db, "tex-shared", "shared.jpg", storeRoot,
               writeTempFile(scratchDir, "shared.jpg", QByteArray("shared-bytes")));
    const QString woody = materialRow(db, "woody", "tex-wood", "tex-shared");
    const QString brick = materialRow(db, "brick", "tex-shared");
    CHECK(!woody.isEmpty() && !brick.isEmpty(), "two library bundles, one shared texture");

    QUndoStack stack;

    // ---- 1. the pin, and the closure with it -------------------------------
    CHECK(!db.isAssetPinnedBy(projectGuid, woody), "1: the project does not hold it yet");
    stack.push(new PinAssetCommand(&db, &project, woody));
    CHECK(db.isAssetPinnedBy(projectGuid, woody), "1: pushing the command pins the material");
    CHECK(db.isAssetPinnedBy(projectGuid, "tex-wood")
              && db.isAssetPinnedBy(projectGuid, "tex-shared"),
          "1: ...and both its member textures ride the closure");

    // ---- 2. undo takes back exactly that -----------------------------------
    stack.undo();
    CHECK(!db.isAssetPinnedBy(projectGuid, woody), "2: undo removes the pin it made (F5)");
    CHECK(!db.isAssetPinnedBy(projectGuid, "tex-wood"),
          "2: ...and the member's, for a member only it uses");
    // …and only that. The shared texture stays, because ANOTHER material
    // names it: `assetdelete::removeFromProject`'s rule is "a member goes
    // only when no depender remains outside the set being removed", which
    // is deliberately conservative — the cost of keeping a pin the user
    // could drop by hand is nothing, and the cost of dropping one something
    // still uses is a project that renders wrong.
    CHECK(db.isAssetPinnedBy(projectGuid, "tex-shared"),
          "2: ...but NOT a member another material names");
    CHECK(!db.fetchAsset(woody).guid.isEmpty(), "2: the LIBRARY row is untouched");
    CHECK(!MaterialBundle::read(&db, woody).isEmpty(), "2: ...definition and all");

    // ---- 3. redo puts it back ----------------------------------------------
    stack.redo();
    CHECK(db.isAssetPinnedBy(projectGuid, woody), "3: redo pins it again");
    CHECK(db.isAssetPinnedBy(projectGuid, "tex-wood")
              && db.isAssetPinnedBy(projectGuid, "tex-shared"), "3: ...with the members");

    // ---- 4. a membership somebody else made is not this command's ----------
    //
    // The user added the material on purpose (or an earlier apply did): a
    // later apply must neither move that pin nor hand an undo the right to
    // remove it.
    const QString heldOid = AssetCas::pinnedOid(QSqlDatabase::database(), projectGuid, woody);
    QUndoStack second;
    second.push(new PinAssetCommand(&db, &project, woody));
    CHECK(AssetCas::pinnedOid(QSqlDatabase::database(), projectGuid, woody) == heldOid,
          "4: a second pin command on a held asset does not move the pin");
    second.undo();
    CHECK(db.isAssetPinnedBy(projectGuid, woody),
          "4: ...and its undo leaves the membership it did not create");

    // ---- 5. a shared member survives one bundle leaving --------------------
    QUndoStack third;
    third.push(new PinAssetCommand(&db, &project, brick));
    CHECK(db.isAssetPinnedBy(projectGuid, brick), "5: the second bundle is pinned too");
    third.undo();
    CHECK(!db.isAssetPinnedBy(projectGuid, brick), "5: and unpinned by its undo");
    CHECK(db.isAssetPinnedBy(projectGuid, "tex-shared"),
          "5: the texture the OTHER pinned material shares keeps its pin");
    CHECK(db.isAssetPinnedBy(projectGuid, "tex-wood"),
          "5: ...and so does the first material's own");

    // ---- 6. no project, no pin, no crash -----------------------------------
    Project placeholder;   // the startup placeholder: a Project with no guid
    QUndoStack fourth;
    fourth.push(new PinAssetCommand(&db, &placeholder, woody));
    fourth.undo();
    CHECK(db.isAssetPinnedBy(projectGuid, woody),
          "6: a command with no project does nothing, and undoes nothing");

    printf(failures ? "\n%d FAILURES\n" : "\nall pin-asset assertions passed\n", failures);
    return failures ? 1 : 0;
}
