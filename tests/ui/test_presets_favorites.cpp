// ui.presets_favorites — THE FAVOURITES ARE READ WHEN THE DATABASE ARRIVES.
//
// The Models and Materials presets tabs (src/ui/panels/presets/) are built by
// MainWindow and given their Database AFTERWARDS:
//
//     assetModelPanel = new AssetModelPanel;          // mainwindow.cpp
//     assetModelPanel->setMainWindow(this);
//     assetModelPanel->setDatabaseHandle(db);
//
// Both constructors used to call addFavorites() anyway, i.e. read the library
// through `AssetPanel::handle` before anything had assigned it. That is
// undefined behaviour which HAPPENED to work — Database::fetchFavorites uses a
// default-constructed QSqlQuery on the default connection and touches no member
// of `this` — so it went unnoticed for years, and merely null-guarding the read
// would have made every user's saved favourites silently vanish from both tabs
// for the whole session (setDatabaseHandle only stored the pointer).
//
// So the READ moved to where the pointer arrives. This suite pins that order
// from both ends, which is the only way to catch either mistake:
//   1. Freshly constructed, with no database yet, a panel lists its starter
//      tiles and NO favourites, and has asked the database nothing.
//   2. setDatabaseHandle() lists them — the saved favourite appears.
//   3. The panel filters by type: a Material favourite never lands in the
//      Models tab and an Object favourite never lands in Materials.
//
// Link stubs for Database (the tests/ui idiom): the subject is the CALL ORDER,
// so a fake library that counts its reads proves more here than a real one
// would, and the suite stays free of Sql and the io/ layer.
#include <QApplication>
#include <QListWidget>
#include <cstdio>

#include "data/database/database.h"
#include "ui/panels/presets/assetmaterialpanel.h"
#include "ui/panels/presets/assetmodelpanel.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { ++checks; if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---- the fake library ------------------------------------------------------
namespace {
const char *kObjectFavorite   = "the favourite model";
const char *kMaterialFavorite = "the favourite material";
int gFetchCalls = 0;
}

QVector<AssetRecord> Database::fetchFavorites()
{
    ++gFetchCalls;
    QVector<AssetRecord> rows;
    AssetRecord object;
    object.guid = QStringLiteral("object-favorite-guid");
    object.name = QString::fromLatin1(kObjectFavorite);
    object.type = static_cast<int>(ModelTypes::Object);
    rows.append(object);
    AssetRecord material;
    material.guid = QStringLiteral("material-favorite-guid");
    material.name = QString::fromLatin1(kMaterialFavorite);
    material.type = static_cast<int>(ModelTypes::Material);
    rows.append(material);
    return rows;
}

QString Database::fetchObjectMesh(const QString &, const int, const int)
{
    return QStringLiteral("mesh-guid");
}

namespace {

/// Does the panel's list carry an item whose display text is `text`?
bool lists(QWidget *panel, const QString &text)
{
    for (QListWidget *view : panel->findChildren<QListWidget *>())
        for (int i = 0; i < view->count(); ++i)
            if (view->item(i)->data(Qt::DisplayRole).toString() == text) return true;
    return false;
}

int tileCount(QWidget *panel)
{
    int total = 0;
    for (QListWidget *view : panel->findChildren<QListWidget *>()) total += view->count();
    return total;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    Database db;   // never initialized: the stubs above are the whole library

    // -----------------------------------------------------------------------
    // 1. CONSTRUCTION ASKS THE LIBRARY NOTHING.
    // -----------------------------------------------------------------------
    gFetchCalls = 0;
    AssetModelPanel models;
    const int modelDefaults = tileCount(&models);
    CHECK(gFetchCalls == 0,
          "a freshly built Models panel has not read the library (it has no handle yet)");
    CHECK(modelDefaults > 0, "...but its starter model tiles are there");
    CHECK(!lists(&models, QString::fromLatin1(kObjectFavorite)),
          "...and no favourite is listed yet");

    gFetchCalls = 0;
    AssetMaterialPanel materials;
    const int materialDefaults = tileCount(&materials);
    CHECK(gFetchCalls == 0,
          "a freshly built Materials panel has not read the library either");
    CHECK(!lists(&materials, QString::fromLatin1(kMaterialFavorite)),
          "...and no favourite is listed there yet");
    // No count assertion on the Materials starters: they are .material files
    // read from the app's materialpresets folder relative to the working
    // directory, so a suite that runs in its own build dir legitimately has
    // none. The DELTA below is the assertion that matters either way.

    // -----------------------------------------------------------------------
    // 2. THE DATABASE ARRIVING IS WHAT LISTS THEM (the real wiring order).
    // -----------------------------------------------------------------------
    gFetchCalls = 0;
    models.setDatabaseHandle(&db);
    CHECK(gFetchCalls == 1, "setDatabaseHandle reads the favourites, exactly once");
    CHECK(lists(&models, QString::fromLatin1(kObjectFavorite)),
          "...and the saved model favourite is now in the Models tab");
    CHECK(tileCount(&models) == modelDefaults + 1,
          "...one tile more than the starters, not two");

    gFetchCalls = 0;
    materials.setDatabaseHandle(&db);
    CHECK(gFetchCalls == 1, "the Materials tab reads them when its handle arrives too");
    CHECK(lists(&materials, QString::fromLatin1(kMaterialFavorite)),
          "...and the saved material favourite is listed");
    CHECK(tileCount(&materials) == materialDefaults + 1,
          "...one tile more than it had, not two");

    // -----------------------------------------------------------------------
    // 3. EACH TAB TAKES ONLY ITS OWN TYPE.
    // -----------------------------------------------------------------------
    CHECK(!lists(&models, QString::fromLatin1(kMaterialFavorite)),
          "a Material favourite never lands in the Models tab");
    CHECK(!lists(&materials, QString::fromLatin1(kObjectFavorite)),
          "an Object favourite never lands in the Materials tab");

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
