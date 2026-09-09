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
//   2. WRITER: guidForStorePath answers the TEXTURE for a texture lookup and
//      is deterministic; the Any preference still answers for a model file.
//   3. ROUND TRIP: a PbrMaterial whose maps hold resolved store paths, written
//      the way a scene save writes it, reopens with every map resolving to a
//      Texture asset whose bytes are the texture — never the .glb.
//   4. REPAIR (scenes already saved wrong): the object guid in a texture slot
//      resolves back to the member texture the slot must have meant, by the
//      object's stored blob, and never invents one.
//   5. A repaired load makes the document dirty (UndoService::markContentRepaired)
//      so the next save writes the corrected scene.
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
    SceneWriter::setProject(&project);

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

    // ---- 2. the writer ----------------------------------------------------
    bool everyLookupIsTexture = true, everyLookupStable = true;
    for (const QString &tex : textures) {
        const QString path = AssetCas::resolvePinned(conn, root, projectGuid, tex);
        const QString found = AssetCas::guidForStorePath(conn, root, path, projectGuid,
                                                         AssetCas::GuidPreference::Texture);
        everyLookupIsTexture = everyLookupIsTexture && (found == tex);
        // Deterministic: the same question answers the same way every time,
        // whatever order SQLite happens to walk the index in.
        for (int i = 0; i < 4; ++i)
            everyLookupStable = everyLookupStable
                && AssetCas::guidForStorePath(conn, root, path, projectGuid,
                                              AssetCas::GuidPreference::Texture) == found;
    }
    CHECK(everyLookupIsTexture,
          "a texture path resolves to its Texture asset, never to the .glb Object");
    CHECK(everyLookupStable, "the lookup is deterministic across repeats");

    // The model file itself still resolves — the Any preference (skeletal clip
    // sources ask this question) is unchanged in meaning.
    {
        const QString modelPath = AssetCas::resolvePinned(conn, root, projectGuid, objectGuid);
        const QString found = AssetCas::guidForStorePath(conn, root, modelPath, projectGuid,
                                                         AssetCas::GuidPreference::Any);
        CHECK(!found.isEmpty()
                  && AssetCas::resolvePinned(conn, root, projectGuid, found) == modelPath,
              "the model file still resolves to an asset holding those bytes");
    }

    // ---- 3. the save/reopen round trip ------------------------------------
    //
    // A material as the session holds it after import: maps are RESOLVED
    // PATHS. Saving turns them into guids; reopening turns them back.
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
            mat->setValue(slotNames[i], AssetCas::resolvePinned(conn, root, projectGuid, tex));
            slotToTexture.insert(slotNames[i], tex);
        }

        QJsonObject matObj;
        SceneWriter::writeSceneNodeMaterial(matObj, mat, /*relative*/ true);
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
        CHECK(noneIsTheModel, "no map is saved as the .glb Object guid (the defect)");
        CHECK(everySlotReopens, "every map reopens as a Texture whose bytes are the texture");
    }

    // ---- 4. the repair (scenes already saved wrong) -----------------------
    //
    // What a scene saved between 2026-09-03 and the fix holds: the OBJECT guid
    // in every texture slot. The reader heals it per slot.
    {
        QJsonObject brokenValues;   // exactly what such a scene file carries
        brokenValues[QStringLiteral("baseColorMap")] = objectGuid;
        brokenValues[QStringLiteral("normalMap")] = objectGuid;
        brokenValues[QStringLiteral("roughnessMap")] = objectGuid;

        // Which texture each slot MUST come back as — the import named the
        // extracted files after the glTF images, so this is checkable by name
        // rather than by "some texture or other".
        const QMap<QString, QString> expected = {
            { QStringLiteral("baseColorMap"), QStringLiteral("quad_base.png") },
            { QStringLiteral("normalMap"),    QStringLiteral("quad_normal.png") },
            { QStringLiteral("roughnessMap"), QStringLiteral("quad_mr_roughness.png") },
        };

        bool everySlotRepaired = true;
        QSet<QString> repairedTo;
        for (auto it = brokenValues.constBegin(); it != brokenValues.constEnd(); ++it) {
            const QString repaired = AssetCas::textureGuidForSlot(conn, it.value().toString(),
                                                                  it.key());
            const bool right = !repaired.isEmpty() && textures.contains(repaired)
                && assetType(conn, repaired) == static_cast<int>(ModelTypes::Texture)
                && assetName(conn, repaired) == expected.value(it.key());
            if (!right)
                std::printf("info: %s repaired to '%s', expected '%s'\n",
                            qPrintable(it.key()), qPrintable(assetName(conn, repaired)),
                            qPrintable(expected.value(it.key())));
            everySlotRepaired = everySlotRepaired && right;
            if (!repaired.isEmpty()) repairedTo.insert(repaired);
        }
        CHECK(everySlotRepaired,
              "each broken slot repairs to the very texture it was authored with");
        CHECK(repairedTo.size() == brokenValues.size(),
              "the three slots repair to three DIFFERENT textures");

        // Never invents content.
        CHECK(AssetCas::textureGuidForSlot(conn, baseTex, QStringLiteral("baseColorMap")).isEmpty(),
              "a slot that already names a Texture is left alone");
        CHECK(AssetCas::textureGuidForSlot(conn, QStringLiteral("not-in-this-catalog"),
                                           QStringLiteral("baseColorMap")).isEmpty(),
              "a guid this catalog never heard of is left alone");
        CHECK(AssetCas::textureGuidForSlot(conn, objectGuid, QString()).isEmpty()
                  || true, "an empty slot name never crashes");
    }

    // ---- 4b. the name route, when there is no blob to read ---------------
    //
    // A model imported before the blob carried member guids (or by a path that
    // never wrote one) still has to be repairable: the member FILE NAMES are
    // then the only evidence, and one texture on the whole object needs none.
    {
        auto row = [&](const QString &guid, ModelTypes type, const QString &name,
                       const QString &parent) {
            QSqlQuery q(conn);
            q.prepare("INSERT INTO assets (guid, type, name, parent) VALUES (?, ?, ?, ?)");
            q.addBindValue(guid);
            q.addBindValue(static_cast<int>(type));
            q.addBindValue(name);
            q.addBindValue(parent);
            q.exec();
        };
        auto link = [&](const QString &guid, const QString &role, const QString &oid,
                        const QString &name) {
            QSqlQuery q(conn);
            q.prepare("INSERT INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
            q.addBindValue(guid);
            q.addBindValue(role);
            q.addBindValue(oid);
            q.addBindValue(name);
            q.exec();
        };

        // Two maps, no blob.
        row("obj-noblob", ModelTypes::Object, "wall.fbx", QString());
        row("tex-diffuse", ModelTypes::Texture, "wall_diffuse.png", "obj-noblob");
        row("tex-normal", ModelTypes::Texture, "wall_normal.png", "obj-noblob");
        link("obj-noblob", "texture", "oid-diffuse", "wall_diffuse.png");
        link("tex-diffuse", "source", "oid-diffuse", "wall_diffuse.png");
        link("obj-noblob", "texture", "oid-normal", "wall_normal.png");
        link("tex-normal", "source", "oid-normal", "wall_normal.png");

        CHECK(AssetCas::textureGuidForSlot(conn, "obj-noblob", "baseColorMap")
                  == QStringLiteral("tex-diffuse"),
              "no blob: baseColorMap follows the diffuse file name");
        CHECK(AssetCas::textureGuidForSlot(conn, "obj-noblob", "normalMap")
                  == QStringLiteral("tex-normal"),
              "no blob: normalMap follows the normal file name");
        CHECK(AssetCas::textureGuidForSlot(conn, "obj-noblob", "emissiveMap").isEmpty(),
              "no blob: a slot no file name claims stays empty (never a guess)");

        // One map, no blob: it can only have been that one.
        row("obj-single", ModelTypes::Object, "prop.fbx", QString());
        row("tex-single", ModelTypes::Texture, "prop_atlas.png", "obj-single");
        link("obj-single", "texture", "oid-single", "prop_atlas.png");
        link("tex-single", "source", "oid-single", "prop_atlas.png");
        CHECK(AssetCas::textureGuidForSlot(conn, "obj-single", "baseColorMap")
                  == QStringLiteral("tex-single"),
              "one texture on the object answers any slot");
    }

    // ---- 5. a repaired load is a dirty document ---------------------------
    {
        QUndoStack stack;
        UndoService undo(&stack);
        undo.markSaved();
        CHECK(!undo.isDirty() && undo.savedCountMatchesCurrent(),
              "a freshly loaded, unrepaired scene is clean");
        undo.markContentRepaired();
        CHECK(undo.isDirty() && !undo.savedCountMatchesCurrent(),
              "a repaired load reads dirty (the close prompt offers the corrected save)");
        undo.markSaved();
        CHECK(!undo.isDirty() && undo.savedCountMatchesCurrent(),
              "saving clears the repair flag");
    }

    SceneWriter::setProject(nullptr);
    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
