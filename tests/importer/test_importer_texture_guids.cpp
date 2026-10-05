// THE GLB TEXTURE-LOSS SUITE (owner: "the glb imports still don't work — they
// import without the textures, including Mixamo avatars", 2026-09-09).
//
// The defect was not in the importer: an imported model's textures are present
// and correct right after import, and disappear on SAVE + REOPEN. Between
// 2026-09-03 and this suite, SceneWriter recovered the guid behind a resolved
// texture path through AssetCas::guidForStorePath, whose ORDER BY broke ties
// only on pinnedness — and BOTH candidate rows are pinned: an imported model
// records its texture bytes twice, {Object, role 'texture'} and {member
// Texture, role 'source'} (assetimporters.cpp), and ProjectAssets pins the
// whole closure. The Object's row, inserted first, therefore won, the scene
// stored the .glb's own guid in baseColorMap, and the reader (source-role
// first) resolved that back to the .glb. Every map on every imported model
// came back empty.
//
// Sections:
//   1. The import plan itself: one Object, one Mesh member, three Texture
//      members, texture bytes recorded under BOTH guids (the ambiguity).
//   (2. the writer's path -> guid lookup is DELETED with TEX-REF-1: a map row
//      carries its Texture asset's guid from the import on, and the writer
//      writes that — there is no tie to break.)
//   3. ROUND TRIP: a PbrMaterial whose maps hold resolved store paths, written
//      the way a scene save writes it, reopens with every map resolving to a
//      Texture asset whose bytes are the texture — never the .glb.
//   (4./5. — the load-time slot REPAIR and its dirty flag — are deleted with
//   the repair: FORWARD-ONLY-1.)
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUndoStack>
#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "io/scenewriter.h"
#include "services/assethelper.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/import/importtypes.h"
#include "services/undoservice.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QStringList memberGuids(QSqlDatabase conn, const QString &parent, ModelTypes type)
{
    QStringList out;
    QSqlQuery q(conn);
    q.prepare("SELECT guid FROM assets WHERE parent = ? AND type = ? ORDER BY name");
    q.addBindValue(parent);
    q.addBindValue(static_cast<int>(type));
    if (q.exec()) while (q.next()) out.append(q.value(0).toString());
    return out;
}

static QString assetName(QSqlDatabase conn, const QString &guid)
{
    QSqlQuery q(conn);
    q.prepare("SELECT name FROM assets WHERE guid = ?");
    q.addBindValue(guid);
    return (q.exec() && q.next()) ? q.value(0).toString() : QString();
}

static int assetType(QSqlDatabase conn, const QString &guid)
{
    QSqlQuery q(conn);
    q.prepare("SELECT type FROM assets WHERE guid = ?");
    q.addBindValue(guid);
    return (q.exec() && q.next()) ? q.value(0).toInt() : -1;
}

static QString sourceOid(QSqlDatabase conn, const QString &guid)
{
    QSqlQuery q(conn);
    q.prepare("SELECT oid FROM asset_files WHERE asset_guid = ? "
              "ORDER BY CASE role WHEN 'source' THEN 0 ELSE 1 END, name");
    q.addBindValue(guid);
    return (q.exec() && q.next()) ? q.value(0).toString() : QString();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/texture_guids.db";
    const QString root = cwd + "/texture_guids_store";
    QFile::remove(dbPath);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fresh database opened");
    db.createAllTables();
    AssetStorePaths::setRootOverride(root);
    QSqlDatabase conn = QSqlDatabase::database();

    // The project the scene is saved against. Only the guid matters here (the
    // writer resolves guids in the project's pin context).
    const QString projectGuid = QStringLiteral("proj-glbtex-0001");
    Project project;
    project.setProjectGuid(projectGuid);

    const QString glb =
        QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/textured_pbr_quad.glb");
    CHECK(QFileInfo::exists(glb), "GLB fixture present (3 embedded maps, 1 material)");

    // ---- 1. the import plan ----------------------------------------------
    AssetImportService service(&db, &project);
    ImportRequest request;
    request.sourcePath = glb;
    request.typeHint = static_cast<int>(ModelTypes::Mesh);
    request.projectGuid = projectGuid;
    const ImportResult result = service.import(request);
    CHECK(result.ok(), "GLB imported through the one pipeline");

    const QString objectGuid = result.assetGuid;
    const QStringList textures = memberGuids(conn, objectGuid, ModelTypes::Texture);
    CHECK(assetType(conn, objectGuid) == static_cast<int>(ModelTypes::Object),
          "the imported row is an Object");
    // FOUR, not three: the importer splits a glTF metallic-roughness map into
    // its two channels (quad_mr_metallic.png / quad_mr_roughness.png).
    CHECK(textures.size() == 4, "four Texture members registered (base, normal, metallic, rough)");

    // The ambiguity this suite exists for: the SAME oid under two asset guids.
    bool sharedOids = !textures.isEmpty();
    for (const QString &tex : textures) {
        QSqlQuery q(conn);
        q.prepare("SELECT COUNT(*) FROM asset_files WHERE oid = ? AND asset_guid = ?");
        q.addBindValue(sourceOid(conn, tex));
        q.addBindValue(objectGuid);
        sharedOids = sharedOids && q.exec() && q.next() && q.value(0).toInt() == 1;
    }
    CHECK(sharedOids, "every texture's bytes are ALSO recorded under the Object");

    // What ProjectAssets::addToProject does when a model is dropped in a
    // project: pin the whole dependency closure at its current source content.
    // Both the Object and its texture members end up pinned — which is exactly
    // why pinnedness could not break the writer's tie.
    AssetCas::writePin(conn, projectGuid, objectGuid, sourceOid(conn, objectGuid));
    for (const QString &tex : textures)
        AssetCas::writePin(conn, projectGuid, tex, sourceOid(conn, tex));

    // ---- 3. the save/reopen round trip ------------------------------------
    //
    // A material as the session holds it after import: each map row holds its
    // RESOLVED PATH and carries its Texture asset's guid (TEX-REF-1). Saving
    // writes the carried guid; reopening turns it back into the bytes.
    const QString baseTex = textures.value(0);
    QMap<QString, QString> slotToTexture;   // slot name -> the texture we set
    {
        auto mat = iris::PbrMaterial::create();
        // (`slots` is a Qt keyword macro — hence slotNames.)
        const QStringList slotNames = { QStringLiteral("baseColorMap"),
                                        QStringLiteral("normalMap"),
                                        QStringLiteral("roughnessMap") };
        for (int i = 0; i < slotNames.size(); ++i) {
            const QString tex = textures.value(i);
            mat->setValue(slotNames[i], iris::Material::textureRef(
                AssetCas::resolvePinned(conn, root, projectGuid, tex), tex));
            slotToTexture.insert(slotNames[i], tex);
        }
        // A FILE NOBODY NAMED has no identity a scene can keep: bound by path
        // alone, the row is written as absent — never as a path.
        mat->setValue(QStringLiteral("emissiveMap"),
                      AssetCas::resolvePinned(conn, root, projectGuid, textures.value(3)));

        QJsonObject matObj;
        SceneWriter::writeSceneNodeMaterial(matObj, mat);
        const QJsonObject values = matObj.value(QStringLiteral("values")).toObject();

        bool everySlotSaved = true, everySlotReopens = true, noneIsTheModel = true;
        const QString modelPath = AssetCas::resolvePinned(conn, root, projectGuid, objectGuid);
        for (auto it = slotToTexture.constBegin(); it != slotToTexture.constEnd(); ++it) {
            const QString stored = values.value(it.key()).toString();
            everySlotSaved = everySlotSaved && (stored == it.value());
            noneIsTheModel = noneIsTheModel && (stored != objectGuid);
            // The reopen: guid -> Texture asset -> the texture's bytes.
            const QString reopened = AssetCas::resolvePinned(conn, root, projectGuid, stored);
            everySlotReopens = everySlotReopens
                && assetType(conn, stored) == static_cast<int>(ModelTypes::Texture)
                && !reopened.isEmpty() && reopened != modelPath
                && reopened == AssetCas::resolvePinned(conn, root, projectGuid, it.value());
        }
        CHECK(everySlotSaved, "the save writes each map's own Texture guid");
        CHECK(values.value(QStringLiteral("emissiveMap")).toString().isEmpty(),
              "a map bound by path alone is written as absent, never as a path (TEX-REF-1)");
        CHECK(noneIsTheModel, "no map is saved as the .glb Object guid (the defect)");
        CHECK(everySlotReopens, "every map reopens as a Texture whose bytes are the texture");
    }

    // ---- 6. the material exporter finds an imported texture's bytes -------
    // (smoke L10 item 7). The exporter's asset-path helper (Exporter::getAssetPath
    // until this lane, AssetHelper::storedFilePath now) spelled an EDITOR-filtered
    // asset's path inline as <Documents>/Jahshaka/<name> — the flat per-project
    // copy the reference-with-pin program deleted (2026-08-31), under a root
    // the data-root override does not even move. A model's textures are
    // registered Editor-filtered (assetimporters.cpp), so a material exported
    // with one of them shipped without it, silently. Every filter now resolves
    // through the store, by guid.
    {
        bool everyEditorFiltered = !textures.isEmpty();
        bool everyResolved = !textures.isEmpty();
        for (const QString &tex : textures) {
            const AssetRecord rec = db.fetchAsset(tex);
            everyEditorFiltered = everyEditorFiltered
                && rec.view_filter == static_cast<int>(AssetViewFilter::Editor);
            const QString path = AssetHelper::storedFilePath(rec);
            QFile got(path), want(AssetCas::resolvePinned(conn, root, projectGuid, tex));
            const bool same = got.open(QIODevice::ReadOnly) && want.open(QIODevice::ReadOnly)
                              && got.readAll() == want.readAll();
            if (!same) std::printf("    %s -> '%s'\n", qPrintable(rec.name), qPrintable(path));
            everyResolved = everyResolved && same;
        }
        CHECK(everyEditorFiltered, "an imported model's textures are Editor-filtered rows");
        CHECK(everyResolved,
              "the material exporter resolves every one of them to its stored bytes "
              "(not a <Documents>/Jahshaka/<name> path that no longer exists)");
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
