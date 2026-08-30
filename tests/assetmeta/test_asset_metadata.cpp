// Headless unit test for AssetMetadata (ASSET_DRAWERS_SPEC.md addendum —
// rich per-type asset metadata): the pure per-file inspectors against the
// committed fixtures, the store-folder dispatch, and the ensure() lazy
// backfill against the REAL Database on a throwaway SQLite file (metadata
// computed once, persisted into the properties JSON, camera block preserved).
// Framework-free; non-zero exit on failure.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetmetadata.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const char *FIXTURES = JAHSHAKA_TEST_SOURCE_DIR "/tests/scripting/fixtures";
static const char *CUBE_OBJ = JAHSHAKA_TEST_SOURCE_DIR "/app/content/primitives/cube.obj";

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets

    // ---- images: header-only decode ----
    {
        const QJsonObject meta = AssetMetadata::forImageFile(QString(FIXTURES) + "/tiny.png");
        CHECK(meta["kind"].toString() == "image", "image: kind");
        CHECK(meta["format"].toString() == "png", "image: format png");
        CHECK(meta["width"].toInt() == 8 && meta["height"].toInt() == 8, "image: 8x8 resolution");
        CHECK(meta["fileSize"].toInteger() > 0, "image: file size > 0");
    }

    // ---- audio: RIFF walk ----
    {
        const QJsonObject meta = AssetMetadata::forAudioFile(QString(FIXTURES) + "/tiny.wav");
        CHECK(meta["kind"].toString() == "audio", "audio: kind");
        CHECK(meta["format"].toString() == "wav", "audio: format wav");
        CHECK(meta["sampleRate"].toInteger() == 8000, "audio: 8000 Hz");
        CHECK(meta["channels"].toInt() == 1, "audio: mono");
        CHECK(meta["bitsPerSample"].toInt() == 16, "audio: 16 bit");
        CHECK(meta["duration"].toInteger() == 50, "audio: 800 data bytes at 16000 B/s = 50 ms");
        CHECK(meta["fileSize"].toInteger() == 844, "audio: file size 844");
    }

    // ---- models: assimp-light load (triangulate only, no GPU) ----
    {
        const QJsonObject meta = AssetMetadata::forModelFile(CUBE_OBJ);
        CHECK(meta["kind"].toString() == "model", "model: kind");
        CHECK(meta["format"].toString() == "obj", "model: format obj");
        CHECK(meta["triangles"].toInteger() == 12, "model: cube = 12 triangles");
        const auto verts = meta["vertices"].toInteger();
        CHECK(verts >= 8 && verts <= 36, "model: cube vertex count sane");
        CHECK(meta["meshes"].toInt() >= 1, "model: mesh count >= 1");
        CHECK(meta["materials"].toInt() >= 1, "model: material count >= 1");
        CHECK(meta["fileSize"].toInteger() > 0, "model: file size > 0");
    }

    // ---- store-folder dispatch ----
    const QString storeRoot = QDir::currentPath() + "/assetmeta_store";
    QDir(storeRoot).removeRecursively();
    {
        QDir().mkpath(storeRoot + "/modelguid");
        QFile::copy(CUBE_OBJ, storeRoot + "/modelguid/cube.obj");
        const QJsonObject meta = AssetMetadata::computeForStore(
            static_cast<int>(ModelTypes::Object), storeRoot + "/modelguid");
        CHECK(meta["kind"].toString() == "model" && meta["triangles"].toInteger() == 12,
              "store dispatch: Object folder -> model stats");
    }
    {
        QDir().mkpath(storeRoot + "/fileguid");
        QFile::copy(QString(FIXTURES) + "/tiny.png", storeRoot + "/fileguid/a.dat");
        QFile::copy(QString(FIXTURES) + "/tiny.wav", storeRoot + "/fileguid/b.bin");
        const QJsonObject meta = AssetMetadata::computeForStore(
            static_cast<int>(ModelTypes::File), storeRoot + "/fileguid");
        CHECK(meta["kind"].toString() == "file", "store dispatch: generic folder -> file");
        CHECK(meta["files"].toInt() == 2, "store dispatch: file count 2");
        CHECK(meta["fileSize"].toInteger() == 76 + 844, "store dispatch: total bytes summed");
    }
    {
        const QJsonObject meta = AssetMetadata::computeForStore(
            static_cast<int>(ModelTypes::Object), storeRoot + "/no_such_folder");
        CHECK(meta.isEmpty(), "store dispatch: missing folder -> empty (no stub persisted)");
    }

    // ---- ensure(): the lazy backfill against the real Database ----
    const QString dbPath = "assetmeta_test.db";
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    // A legacy Object row: properties hold ONLY the viewer's camera block.
    const QString guid = "aaaaaaaa-1111-2222-3333-444444444444";
    QDir().mkpath(storeRoot + "/" + guid);
    QFile::copy(CUBE_OBJ, storeRoot + "/" + guid + "/cube.obj");
    QJsonObject camProps{ { "camera", QJsonObject{ { "distFromPivot", 5.0 } } } };
    db.createAssetEntry(guid, "cube", static_cast<int>(ModelTypes::Object), QString(),
                        QString(), QString(), QString(), QByteArray(),
                        QJsonDocument(camProps).toJson(), QByteArray(), QByteArray(),
                        AssetViewFilter::AssetsView);

    {
        const QJsonObject meta = AssetMetadata::ensure(&db, guid, storeRoot);
        CHECK(meta["triangles"].toInteger() == 12, "ensure: backfill computed model stats");

        const QJsonObject props =
            QJsonDocument::fromJson(db.fetchAsset(guid).properties).object();
        CHECK(props["metadata"].toObject()["triangles"].toInteger() == 12,
              "ensure: metadata persisted into properties");
        CHECK(props["camera"].toObject()["distFromPivot"].toDouble() == 5.0,
              "ensure: existing camera block preserved");
    }
    {
        // Second call must serve the persisted block, not recompute:
        // remove the file — the answer must still be there.
        QFile::remove(storeRoot + "/" + guid + "/cube.obj");
        const QJsonObject meta = AssetMetadata::ensure(&db, guid, storeRoot);
        CHECK(meta["triangles"].toInteger() == 12, "ensure: second call reads the stored block");
    }
    {
        const QJsonObject meta = AssetMetadata::ensure(&db, "no-such-guid", storeRoot);
        CHECK(meta.isEmpty(), "ensure: unknown guid -> empty");
    }

    printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
