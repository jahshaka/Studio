// THE SHIPPED PRESETS' MAPS GET THEIR STAMP IN A LIBRARY THAT ALREADY EXISTS
// — SEED-RESTAMP-1, against the REAL Database and the REAL content-addressed
// store on a throwaway SQLite file. No UI, no engine.
//
// THE DEFECT, from the owner's #56 smoke: his library was reset on build #53;
// the stamping seed (SEED-STAMP-1) shipped in #54. The seed writes the member
// stamp as it MINTS each map, and it is idempotent — so the thirty-five maps
// minted by the older build carry none for ever, and his project tray showed
// them as loose tiles beside the seven preset bundles they came in through
// (MATERIAL_BUNDLE_SPEC V-2: a picture that arrived INSIDE a material folds
// into the bundle's tile; a picture the USER imported is always a tile).
//
// The library this suite builds is that shape, with the real rows: preset
// bundles whose definitions name their maps, every map unstamped, a picture
// the user imported themselves beside them, and — the case the seed itself
// recorded — a SECOND row over one map's bytes that no definition names.
//
// What each section pins, each an owner-visible claim:
//   1. THE SYMPTOM. Nothing folds: every map is a tile, which is the tray the
//      owner described.
//   2. THE REPAIR, BY CONTENT. Every row whose stored object is a map a
//      shipped preset's definition names is stamped with that preset — the
//      duplicate row included, because identity is the bytes — and the user's
//      own picture is untouched. The maps fold; their pins do not move.
//   3. IDEMPOTENT. A second pass writes nothing and moves no origin.
//   4. V-2's OTHER HALF, the gate. A preset one of whose maps is already
//      stamped was minted by a stamping seed, so the unstamped map beside it
//      is the USER's copy of that picture and the repair leaves it alone.
//   5. THE PRE-GATE. A library with no unstamped texture row at all is
//      answered by one query: the pass reports `ran == false` and reads not
//      one definition.
//
// Framework-free; non-zero exit on failure. Runs displayless.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"
#include "services/materialmembers.h"
#include "services/memberstamp.h"
#include "services/presetrestamp.h"
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

/// A library texture row over `srcPath`'s bytes — the shape every import
/// leaves behind: a catalog row plus the stored source object.
static QString textureRow(Database &db, const QString &guid, const QString &name,
                          const QString &storeRoot, const QString &srcPath)
{
    db.createAssetEntry(guid, name, static_cast<int>(ModelTypes::Texture),
                        QString(), QString(), QString(), QString(), QByteArray(),
                        QByteArray(), QByteArray(), QByteArray(),
                        AssetViewFilter::AssetsView);
    QString oid, err;
    AssetCas::ingestFile(QSqlDatabase::database(), storeRoot, srcPath, guid,
                         QStringLiteral("source"), name, &oid, &err);
    return oid;
}

/// A material bundle naming `maps` (slot -> texture guid) — a stand-in for a
/// shipped preset's seeded bundle. The reserved preset guids cannot be used
/// here: `MaterialBundle::write` refuses one by name, which is the read-only
/// rule (MATERIAL_BUNDLE_SPEC R18). The pass takes the shipped SET as its
/// argument for exactly this reason — what makes a guid a preset is the
/// shipped table, and the table is the caller's business.
static QString bundle(Database &db, const QString &name, const QVariantMap &maps)
{
    // (`slots` is Qt's own keyword — the parameter cannot be called that.)
    QJsonObject values;
    for (auto it = maps.constBegin(); it != maps.constEnd(); ++it)
        values.insert(it.key(), it.value().toString());
    QJsonObject definition;
    definition["materialType"] = "pbr";
    definition["values"] = values;
    QString error;
    const QString guid = MaterialBundle::create(&db, name, definition, assethome::library(), QByteArray(), &error);
    if (guid.isEmpty()) printf("info: bundle '%s' failed: %s\n", qPrintable(name), qPrintable(error));
    return guid;
}

static bool stamped(Database &db, const QString &guid)
{
    return memberstamp::isStamped(&db, guid);
}

static QString originOf(Database &db, const QString &guid)
{
    return memberstamp::originOf(db.fetchAsset(guid).properties);
}

static int pinCount(const QString &projectGuid)
{
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM project_assets WHERE project_guid = ?");
    q.addBindValue(projectGuid);
    if (!q.exec()) { printf("info: pin query error: %s\n", qPrintable(q.lastError().text())); return -1; }
    return q.next() ? q.value(0).toInt() : -1;
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

    const QString dbPath = scratchDir.filePath("presetrestamp_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    // --- the #53 shape ----------------------------------------------------
    // Three maps of one preset, one of another, one picture of the user's, and
    // a SECOND row over the first map's bytes (the pre-PATHCLEAN-1 seeds minted
    // one for three of the shipped maps: two spellings of one path, both told
    // "the library does not have these bytes" before either was imported).
    const QString colourSrc = writeTempFile(scratchDir, "brick_COLOR.png", QByteArray("brick-colour-bytes"));
    const QString normalSrc = writeTempFile(scratchDir, "brick_NRM.png",   QByteArray("brick-normal-bytes"));
    const QString roughSrc  = writeTempFile(scratchDir, "brick_SPEC.png",  QByteArray("brick-rough-bytes"));
    const QString metalSrc  = writeTempFile(scratchDir, "metal_COLOR.png", QByteArray("metal-colour-bytes"));
    const QString mineSrc   = writeTempFile(scratchDir, "holiday.png",     QByteArray("a-photo-of-mine"));

    const QString colourOid = textureRow(db, "tex-colour", "brick_COLOR.png", storeRoot, colourSrc);
    textureRow(db, "tex-normal", "brick_NRM.png",   storeRoot, normalSrc);
    textureRow(db, "tex-rough",  "brick_SPEC.png",  storeRoot, roughSrc);
    textureRow(db, "tex-metal",  "metal_COLOR.png", storeRoot, metalSrc);
    textureRow(db, "tex-mine",   "holiday.png",     storeRoot, mineSrc);
    const QString dupOid = textureRow(db, "tex-colour-2", "brick_COLOR.png", storeRoot, colourSrc);
    CHECK(!colourOid.isEmpty() && dupOid == colourOid,
          "fixture: the duplicate row stores the SAME object (identity is the bytes)");

    // …and one map that carries a SECOND stored object: `asset_files` is keyed
    // by (guid, role, NAME), so a project that painted on a member under
    // another name leaves the row with two source objects. It is still ONE row.
    {
        const QString paintedSrc = writeTempFile(scratchDir, "brick_SPEC_edit.png",
                                                 QByteArray("brick-rough-painted"));
        QString oid, err;
        AssetCas::ingestFile(QSqlDatabase::database(), storeRoot, paintedSrc, "tex-rough",
                             QStringLiteral("source"), QStringLiteral("brick_SPEC_edit.png"),
                             &oid, &err);
        CHECK(!oid.isEmpty(), "fixture: one map carries a second source object");
    }

    const QString presetA = bundle(db, "Brick PBR (shipped)",
                                   { { "baseColorMap", "tex-colour" },
                                     { "normalMap",    "tex-normal" },
                                     { "roughnessMap", "tex-rough"  } });
    const QString presetB = bundle(db, "Metal PBR (shipped)", { { "baseColorMap", "tex-metal" } });
    const QString mineMat = bundle(db, "My Material", { { "baseColorMap", "tex-mine" } });
    CHECK(!presetA.isEmpty() && !presetB.isEmpty() && !mineMat.isEmpty(),
          "fixture: two shipped bundles and one of the user's own");
    const QStringList shipped = { presetA, presetB };

    // A project holds the preset and its maps, as the owner's does (seven
    // preset pins, twelve loose texture pins).
    Project project;
    project.setProjectGuid("proj-1");
    CHECK(db.createProject("proj-1", "Restamp Project"), "fixture: a project row");
    CHECK(ProjectAssets::addToProject(presetA, &db, &project,
                                      ProjectAssets::AddKind::Direct).ok(),
          "fixture: the project pins the preset and its closure");
    const int pinsBefore = pinCount("proj-1");
    CHECK(pinsBefore >= 4, "fixture: the bundle and its three maps are pinned");

    // =======================================================================
    // 1. THE SYMPTOM — nothing folds, so every map is a tray tile
    // =======================================================================
    CHECK(!stamped(db, "tex-colour") && !stamped(db, "tex-normal")
              && !stamped(db, "tex-rough") && !stamped(db, "tex-metal"),
          "1: the maps of a library minted before the stamping seed carry NO stamp");
    CHECK(!materialmembers::hiddenAsMember(&db, "tex-colour"),
          "1: …so a preset's picture stands in the tray as one of the user's own");

    // =======================================================================
    // 2. THE REPAIR
    // =======================================================================
    const presetrestamp::Report first = presetrestamp::restamp(&db, shipped);
    CHECK(first.error.isEmpty(), "2: the pass ran against the live library");
    CHECK(first.ran, "2: …and had something to look at (the pre-gate did not answer)");
    CHECK(first.scanned == 6,
          qPrintable(QStringLiteral("2: six texture ROWS scanned — the row with two stored "
                                    "objects is one of them (%1)").arg(first.scanned)));
    CHECK(first.stamped == 5, qPrintable(QStringLiteral("2: FIVE ROWS STAMPED — four maps and the "
                                                        "duplicate over one map's bytes (%1)")
                                             .arg(first.stamped)));
    CHECK(first.presets == 2, "2: …across both shipped bundles");
    CHECK(first.skipped == 0, "2: …and no preset was left alone");

    CHECK(stamped(db, "tex-colour") && stamped(db, "tex-normal") && stamped(db, "tex-rough"),
          "2: every map of the first preset is a member now");
    CHECK(originOf(db, "tex-colour") == presetA && originOf(db, "tex-rough") == presetA,
          "2: …stamped with the material they came in through");
    CHECK(first.stamped == 5,
          "2: …and the row with two stored objects was stamped ONCE, not twice");
    CHECK(stamped(db, "tex-metal") && originOf(db, "tex-metal") == presetB,
          "2: …and the second preset's map with ITS bundle, not the first");

    // BY CONTENT, NEVER BY NAME — both directions.
    CHECK(stamped(db, "tex-colour-2") && originOf(db, "tex-colour-2") == presetA,
          "2: THE DUPLICATE ROW no definition names is repaired too: its bytes are a preset's map");
    CHECK(!stamped(db, "tex-mine"),
          "2: THE USER'S OWN PICTURE IS UNTOUCHED — its bytes are no shipped map's");
    CHECK(originOf(db, "tex-mine").isEmpty(), "2: …and it carries no origin");
    CHECK(!materialmembers::hiddenAsMember(&db, "tex-mine"),
          "2: …so it is still their tile, used by their own material");

    // …and the fold the owner was missing.
    CHECK(materialmembers::hiddenAsMember(&db, "tex-colour")
              && materialmembers::hiddenAsMember(&db, "tex-normal")
              && materialmembers::hiddenAsMember(&db, "tex-rough")
              && materialmembers::hiddenAsMember(&db, "tex-metal"),
          "2: THE MAPS FOLD INTO THEIR BUNDLES' TILES — the tray shows the material, not the pictures");

    // NOTHING WAS PINNED, UNPINNED OR MOVED (the brief's item 2): a pinned map
    // that is now a member stays pinned, and the fold is what hides it.
    CHECK(pinCount("proj-1") == pinsBefore,
          qPrintable(QStringLiteral("2: the project's pins are untouched (%1 -> %2)")
                         .arg(pinsBefore).arg(pinCount("proj-1"))));
    CHECK(db.isAssetPinnedBy("proj-1", "tex-colour"),
          "2: …the pinned map is still pinned, and folded under the bundle's tile");
    {
        const auto members = materialmembers::describe(&db, &project, presetA);
        int hidden = 0, pinned = 0;
        for (const auto &m : members) { if (m.hidden) ++hidden; if (m.pinned) ++pinned; }
        CHECK(members.size() == 3 && hidden == 3 && pinned == 3,
              "2: the Members panel lists all three, folded and still pinned");
    }

    // =======================================================================
    // 3. IDEMPOTENT
    // =======================================================================
    const QByteArray colourProps = db.fetchAsset("tex-colour").properties;
    const presetrestamp::Report second = presetrestamp::restamp(&db, shipped);
    CHECK(second.stamped == 0, qPrintable(QStringLiteral("3: a second pass stamps nothing (%1)")
                                              .arg(second.stamped)));
    CHECK(second.skipped == 2, "3: …because both presets now show a stamping seed's work");
    CHECK(db.fetchAsset("tex-colour").properties == colourProps,
          "3: …and not one row was rewritten");
    CHECK(originOf(db, "tex-colour") == presetA, "3: an origin never moves");

    // =======================================================================
    // 4. V-2's OTHER HALF — a preset a stamping seed minted is left alone
    // =======================================================================
    //
    // The library cannot record who ASKED for an import, so "unstamped because
    // the seed was too old" and "unstamped because the user imported that
    // picture themselves" look identical on the row. The evidence that does
    // exist is the batch: a seed that stamps stamps every map it mints.
    const QString tileSrc = writeTempFile(scratchDir, "grass_COLOR.png", QByteArray("grass-colour"));
    const QString userSrc = writeTempFile(scratchDir, "grass_NRM.png",   QByteArray("grass-normal"));
    textureRow(db, "tex-grass",     "grass_COLOR.png", storeRoot, tileSrc);
    textureRow(db, "tex-grass-nrm", "grass_NRM.png",   storeRoot, userSrc);
    const QString presetC = bundle(db, "Grass PBR (shipped)",
                                   { { "baseColorMap", "tex-grass" },
                                     { "normalMap",    "tex-grass-nrm" } });
    CHECK(!presetC.isEmpty(), "4: a third shipped bundle, seeded by a STAMPING seed");
    CHECK(memberstamp::stamp(&db, "tex-grass", presetC),
          "4: …which stamped the one map it minted");
    // …and the other map is the picture the user imported before the seed ran:
    // the seed found it by content and minted nothing, so it carries no stamp.

    const presetrestamp::Report third = presetrestamp::restamp(&db, { presetA, presetB, presetC });
    CHECK(third.stamped == 0,
          qPrintable(QStringLiteral("4: THE USER'S OWN COPY OF A PRESET'S MAP IS NOT CLAIMED (%1 stamped)")
                         .arg(third.stamped)));
    CHECK(!stamped(db, "tex-grass-nrm"), "4: …it carries no stamp");
    CHECK(third.skipped == 3, "4: …and all three presets were left alone");
    CHECK(!materialmembers::hiddenAsMember(&db, "tex-grass-nrm"),
          "4: …so it is still a tile of theirs");

    // =======================================================================
    // 5. THE PRE-GATE — a library with nothing to repair costs one query
    // =======================================================================
    CHECK(memberstamp::stamp(&db, "tex-mine", mineMat),
          "5: the user picks their picture into their own material (the picker stamps)");
    CHECK(memberstamp::stamp(&db, "tex-grass-nrm", presetC),
          "5: …and the last unstamped row is given one too");
    const presetrestamp::Report quiet = presetrestamp::restamp(&db, { presetA, presetB, presetC });
    CHECK(!quiet.ran, "5: with no unstamped texture row the pass STOPS at the pre-gate");
    CHECK(quiet.stamped == 0 && quiet.presets == 0 && quiet.skipped == 0,
          "5: …reporting nothing done");
    CHECK(quiet.scanned == 8, qPrintable(QStringLiteral("5: …having only counted the rows (%1)")
                                             .arg(quiet.scanned)));

    CHECK(originOf(db, "tex-mine") == mineMat,
          "5: …and the user's stamp still names THEIR material, never a preset");

    // A pass with no shipped set is not a pass.
    const presetrestamp::Report none = presetrestamp::restamp(&db, QStringList());
    CHECK(!none.ran && none.stamped == 0, "5: an empty shipped set does nothing at all");

    if (failures) printf("\nFAILED (%d)\n", failures);
    else printf("\nALL PASS\n");
    return failures ? 1 : 0;
}
