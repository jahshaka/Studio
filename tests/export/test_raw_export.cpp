// Raw export + manifest v2 (ASSET_PIPELINE_SPEC §3.3 phase-5 front half):
// LegacyStoreContentSource over a fixture store, RawExporter round-trip
// (export -> files on disk -> manifest parses back), content dedup by oid,
// name-collision dedup, DB-only rows, the flat-folder fallback, manifest-only
// mode, and v1/v2 manifest reader acceptance. Pure QtCore, no scene.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

#include "export/exportcontentsource.h"
#include "export/exportmanifest.h"
#include "export/rawexporter.h"

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { std::printf("ok: %s\n", msg); }                          \
        else      { std::printf("FAIL: %s\n", msg); ++failures; }            \
    } while (0)

static bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(bytes) == bytes.size();
}

static QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).toLower();
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");

    // ---- fixture store: today's per-guid layout ----
    const QString store = tmp.filePath("store");
    const QByteArray modelBytes = "MODEL-BYTES-0123456789";
    const QByteArray texBytes = "TEXTURE-BYTES-abcdef";
    const QByteArray otherBytes = "DIFFERENT-CONTENT";
    CHECK(writeFile(store + "/guidA/model.glb", modelBytes), "fixture guidA/model.glb");
    CHECK(writeFile(store + "/guidA/skin.png", texBytes), "fixture guidA/skin.png");
    CHECK(writeFile(store + "/guidB/skin.png", texBytes), "fixture guidB/skin.png (same bytes as A's)");
    CHECK(writeFile(store + "/guidC/skin.png", otherBytes), "fixture guidC/skin.png (different bytes)");
    // guidD has no folder at all (a DB-only row)

    // ---- content source ----
    {
        LegacyStoreContentSource src(store);
        const auto a = src.filesForAsset("guidA");
        CHECK(a.size() == 2, "guidA lists 2 files");
        CHECK(!a.isEmpty() && a.first().name == "model.glb", "name-sorted listing");
        CHECK(!a.isEmpty() && a.first().size == modelBytes.size(), "size recorded");
        CHECK(!a.isEmpty() && a.first().oid == sha256(modelBytes), "oid = sha256 of bytes");
        CHECK(src.filesForAsset("guidD").isEmpty(), "missing folder = zero files, not an error");

        LegacyStoreContentSource noHash(store, false);
        const auto nh = noHash.filesForAsset("guidA");
        CHECK(!nh.isEmpty() && nh.first().oid.isEmpty(), "hashing can be skipped (oid empty)");
    }

    // ---- flat-folder fallback (project rows) ----
    {
        const QString projDir = tmp.filePath("project");
        CHECK(writeFile(projDir + "/loose.obj", modelBytes), "fixture project/loose.obj");
        LegacyStoreContentSource src(store, true, projDir);
        const auto hit = src.filesForAsset("guidNoFolder", "loose.obj");
        CHECK(hit.size() == 1 && hit.first().name == "loose.obj",
              "fallback resolves name-keyed in the flat folder");
        CHECK(src.filesForAsset("guidNoFolder", "absent.obj").isEmpty(),
              "fallback miss = zero files");
        const auto a = src.filesForAsset("guidA", "loose.obj");
        CHECK(a.size() == 2, "store folder wins over the fallback");
    }

    // ---- raw export round-trip ----
    const QString outDir = tmp.filePath("out");
    {
        LegacyStoreContentSource src(store);
        QVector<RawExporter::AssetInfo> assets;
        assets.append({ "guidA", "model.glb", "object", 5, { "guidB", "guidC" } });
        assets.append({ "guidB", "skin.png", "texture", 2, {} });
        assets.append({ "guidC", "skin.png", "texture", 2, {} });
        assets.append({ "guidD", "ghost", "material", 1, {} });

        const auto r = RawExporter::exportAssets(assets, src, outDir);
        CHECK(r.ok, "exportAssets ok");
        CHECK(r.assetCount == 4, "4 manifest entries");
        // model.glb + A's skin.png; B's skin.png deduped by oid into A's copy;
        // C's skin.png collides by name with different bytes -> "skin 1.png".
        CHECK(r.exportedFiles.size() == 3, "3 files written (1 deduped by content)");
        CHECK(r.exportedFiles.contains("skin 1.png"), "name collision deduped as 'skin 1.png'");
        CHECK(readFile(outDir + "/model.glb") == modelBytes, "model bytes round-trip");
        CHECK(readFile(outDir + "/skin.png") == texBytes, "texture bytes round-trip");
        CHECK(readFile(outDir + "/skin 1.png") == otherBytes, "collided texture bytes round-trip");
        CHECK(QFile::exists(r.manifestPath), "manifest written");

        // parse the manifest back
        QString err;
        const auto m = exportformat::ExportManifest::fromFile(r.manifestPath, &err);
        CHECK(m.isValid() && m.version == 2, "manifest parses as v2");
        CHECK(m.kind == "raw", "manifest kind 'raw'");
        CHECK(m.assets.size() == 4, "manifest has 4 assets");
        const auto &a0 = m.assets.at(0);
        CHECK(a0.guid == "guidA" && a0.type == "object" && a0.typeId == 5,
              "asset identity round-trips (guid/type/typeId)");
        CHECK(a0.dependencies == QStringList({ "guidB", "guidC" }),
              "dependency edges round-trip");
        CHECK(a0.files.size() == 2 && a0.files.first().oid == sha256(modelBytes),
              "file oids round-trip");
        // guidB's entry references the deduped file A wrote
        CHECK(m.assets.at(1).files.size() == 1 && m.assets.at(1).files.first().name == "skin.png",
              "content-deduped entry references the shared file");
        CHECK(m.assets.at(3).files.isEmpty(), "DB-only row keeps a file-less manifest entry");
    }

    // ---- manifest-only mode (project.exportManifest's path) ----
    {
        LegacyStoreContentSource src(store);
        QVector<RawExporter::AssetInfo> assets;
        assets.append({ "guidA", "model.glb", "object", 5, {} });
        const QString mdir = tmp.filePath("manifest-only");
        const auto r = RawExporter::exportAssets(assets, src, mdir, "project", false);
        CHECK(r.ok, "manifest-only export ok");
        CHECK(r.exportedFiles.isEmpty(), "manifest-only writes no payload files");
        CHECK(QDir(mdir).entryList(QDir::Files).size() == 1, "only the manifest in the dir");
        const auto m = exportformat::ExportManifest::fromFile(r.manifestPath);
        CHECK(m.isValid() && m.kind == "project", "manifest-only manifest parses, kind 'project'");
        CHECK(m.assets.size() == 1 && m.assets.first().files.size() == 2,
              "manifest-only still describes the stored files");
    }

    // ---- error paths ----
    {
        LegacyStoreContentSource src(store);
        const auto r = RawExporter::exportAssets({}, src, tmp.filePath("x"));
        CHECK(!r.ok, "empty asset list refused");
        const auto r2 = RawExporter::exportAssets({ { "g", "n", "object", 5, {} } }, src, "");
        CHECK(!r2.ok, "empty destination refused");
    }

    // ---- manifest reader: v1 acceptance + rejects ----
    {
        QString err;
        auto v1 = exportformat::ExportManifest::fromBytes("object\n", &err);
        CHECK(v1.isValid() && v1.version == 1 && v1.kind == "object", "v1 'object' accepted");
        v1 = exportformat::ExportManifest::fromBytes("bundle", &err);
        CHECK(v1.isValid() && v1.version == 1 && v1.kind == "bundle", "v1 'bundle' accepted");
        auto bad = exportformat::ExportManifest::fromBytes("garbage-word", &err);
        CHECK(!bad.isValid(), "unknown v1 word rejected");
        bad = exportformat::ExportManifest::fromBytes("{\"format\":\"other\"}", &err);
        CHECK(!bad.isValid(), "foreign JSON rejected");
        bad = exportformat::ExportManifest::fromBytes("", &err);
        CHECK(!bad.isValid(), "empty manifest rejected");
    }

    std::printf(failures ? "test_raw_export: %d FAILURES\n"
                         : "test_raw_export: ALL OK\n", failures);
    return failures ? 1 : 0;
}
