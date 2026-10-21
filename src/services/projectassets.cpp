/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectassets.h"
#include "services/projectmembership.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/assetmanager.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/imagematerial.h"
#include "services/materialbundle.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"

namespace {

QString sourceOidOf(QSqlDatabase conn, const QString &guid)
{
    // ONE implementation (AssetCas::sourceOid): the library's current source
    // oid is the "what version is this?" answer, and the avatar module asks it
    // too. A second transcription of the same ORDER BY is a place for the two
    // to disagree about which row is the source.
    return AssetCas::sourceOid(conn, guid);
}

bool sessionHas(const QString &guid)
{
    for (auto *asset : AssetManager::getAssets())
        if (asset && asset->assetGuid == guid) return true;
    return false;
}

} // namespace

ProjectAssets::Result ProjectAssets::addToProject(const QString &guid, Database *db,
                                                  Project *project, AddKind kind)
{
    Result result;
    if (!db || !project || project->getProjectGuid().isEmpty()) {
        result.error = QStringLiteral("no open project");
        return result;
    }
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        result.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return result;
    }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString projectGuid = project->getProjectGuid();

    // ANOTHER PROJECT'S COPY OF A PRESET IS NOT PINNABLE (PRESET-FOLD-1): it
    // would become this project's copy too, and two projects would edit one
    // material. The library views never offer it (they show the master); a
    // verb or a stale drag that names it gets the reason.
    // (Any OTHER project's own row MAY be pinned — ASSETS-SCOPE-1 F3: a
    // cross-project paste pins the guids its nodes name. The row lives while
    // any project pins it and goes with the last pin, whoever let go.)
    const QString foreign = MaterialBundle::foreignCopyRefusal(db, guid, projectGuid);
    if (!foreign.isEmpty()) {
        result.error = foreign;
        return result;
    }

    // The membership = the asset plus its dependency closure, each pinned at
    // its CURRENT source content. No files move; no rows clone.
    const QStringList members = AssetHelper::fetchAssetAndAllDependencies(guid, db);
    for (const QString &member : members) {
        AssetCas::writePin(conn, projectGuid, member, sourceOidOf(conn, member));
        result.pinnedGuids.append(member);
    }

    // Session registrations — the ORIGINAL guids, bytes CAS-resolved through
    // the fresh pins (identical to what the readers will resolve).
    for (const QString &member : members)
        registerSessionAsset(member, db, project);

    // Owner call (IMAGE_PLANE_SPEC §8.1, 2026-08-31): an image added to a
    // project ALSO gets its companion material asset — THE PROJECT'S OWN row
    // (ASSETS-SCOPE-1), then pinned in through this same function so it lands in the
    // bin, session-registered and droppable. BOUNDARY: only the DIRECTLY
    // added asset auto-creates — dependency textures riding an object's
    // closure never do (an object with 30 textures must not explode into 30
    // materials), a texture pinned as a BINDING (a light's mask or IES
    // profile, and later a decal's maps — AddKind::Binding) never does
    // either, and re-adding the same image is a no-op (a Material THIS
    // project holds already depends on the texture). The recursive addToProject cannot loop:
    // the companion is a Material, and Materials never auto-create.
    if (kind == AddKind::Direct
        && static_cast<ModelTypes>(record.type) == ModelTypes::Texture
        && !ImageMaterial::hasCompanionMaterial(guid, projectGuid)) {
        const QString materialGuid = ImageMaterial::createMaterialAsset(
            guid, db, project, assethome::project(projectGuid));
        if (!materialGuid.isEmpty()) {
            const Result companion = addToProject(materialGuid, db, project, AddKind::Direct);
            result.pinnedGuids.append(companion.pinnedGuids);
            result.pinnedGuids.removeDuplicates();
        }
    }

    result.guid = guid;
    ProjectMembership::instance()->announce(projectGuid);
    return result;
}

bool ProjectAssets::registerSessionAsset(const QString &guid, Database *db, Project *project)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;
    if (sessionHas(guid)) return true;
    const auto memberRecord = db->fetchAsset(guid);
    if (memberRecord.guid.isEmpty()) return false;
    SessionRow row;
    row.guid = guid;
    row.type = memberRecord.type;
    row.name = memberRecord.name;
    row.asset = memberRecord.asset;
    row.path = AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                       project->getProjectGuid(), guid);
    return registerSessionRow(row);
}

QVector<int> ProjectAssets::sessionTypes()
{
    // The open's order: the four legacy membership types first (as the
    // per-type membership sweep always listed them), then the types only a pin
    // can bring.
    return { static_cast<int>(ModelTypes::File), static_cast<int>(ModelTypes::Texture),
             static_cast<int>(ModelTypes::ParticleSystem), static_cast<int>(ModelTypes::Material),
             static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Music) };
}

QVector<ProjectAssets::SessionRow> ProjectAssets::readSessionRows(QSqlDatabase conn, const QString &root,
                                                                  const QString &projectGuid, int type,
                                                                  int *queries)
{
    QVector<SessionRow> rows;
    if (projectGuid.isEmpty()) return rows;
    // THE MEMBERSHIP: the project's pins of this type, and — for the legacy
    // membership types only — the rows the project owns (assets.project_guid).
    // Objects and music are members by pin alone, as they always were.
    const auto t = static_cast<ModelTypes>(type);
    const bool legacy = t == ModelTypes::File || t == ModelTypes::Texture ||
                        t == ModelTypes::ParticleSystem || t == ModelTypes::Material;
    // THE BYTES, IN THE SAME STATEMENT (AssetCas::resolvePinned, batched): the
    // pinned object when the pin names one the files table knows, else the
    // library source (asset_files, the 'source' role first, then by name).
    // Existence on disk is checked below, exactly as the resolver does.
    QSqlQuery query(conn);
    query.prepare(QStringLiteral(
        "SELECT A.guid, A.name, A.asset, PA.asset_guid IS NOT NULL, PF.oid, PF.ext, "
        "  (SELECT AF.oid FROM asset_files AF WHERE AF.asset_guid = A.guid "
        "     ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name LIMIT 1), "
        "  (SELECT F.ext FROM asset_files AF LEFT JOIN files F ON F.oid = AF.oid WHERE AF.asset_guid = A.guid "
        "     ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name LIMIT 1) "
        "FROM assets A "
        "LEFT JOIN project_assets PA ON PA.asset_guid = A.guid AND PA.project_guid = ? "
        "LEFT JOIN files PF ON PF.oid = PA.oid_pin AND PA.oid_pin <> '' "
        "WHERE A.type = ? AND (PA.asset_guid IS NOT NULL OR (? AND A.project_guid = ?)) "
        "ORDER BY PA.asset_guid IS NOT NULL, A.rowid"));
    query.addBindValue(projectGuid);
    query.addBindValue(type);
    query.addBindValue(legacy ? 1 : 0);
    query.addBindValue(projectGuid);
    if (queries) ++*queries;
    if (!query.exec()) return rows;
    while (query.next()) {
        SessionRow row;
        row.guid = query.value(0).toString();
        if (row.guid.isEmpty()) continue;
        row.type = type;
        row.name = query.value(1).toString();
        if (t == ModelTypes::ParticleSystem) row.asset = query.value(2).toByteArray();
        if (query.value(3).toBool() && !query.value(4).isNull()) {
            const QString pinned = AssetStorePaths::objectPathIn(root, query.value(4).toString(),
                                                                 query.value(5).toString());
            if (QFileInfo::exists(pinned)) row.path = pinned;
        }
        if (row.path.isEmpty() && !query.value(6).isNull()) {
            const QString source = AssetStorePaths::objectPathIn(root, query.value(6).toString(),
                                                                 query.value(7).toString());
            if (QFileInfo::exists(source)) row.path = source;
        }
        rows.append(row);
    }
    return rows;
}

bool ProjectAssets::registerSessionRow(const SessionRow &row)
{
    if (row.guid.isEmpty()) return false;
    if (sessionHas(row.guid)) return true;
    switch (static_cast<ModelTypes>(row.type)) {
        case ModelTypes::Object: {
            if (row.path.isEmpty()) break;
            // A REGISTRATION, NOT A COPY OF THE MODEL (BISTRO-ENGINE-1 stage 5d).
            // This used to build the object's whole node fragment from its bake —
            // every node and material, again, after the scene reader had already
            // built the scene's — and store it as the registration's value, which
            // nothing in the program reads (the session list, the drag lookups and
            // assets.list read the guid, the name and the path). It was 1,039 ms
            // of the Bistro Exterior's open on the UI thread. The model is read
            // by whoever places it (libraryasset::fromLibrary, the scene reader),
            // from its bake, when it is placed.
            auto asset = std::make_unique<AssetNodeObject>();
            asset->assetGuid = row.guid;
            asset->fileName = row.name;
            asset->path = row.path;
            AssetManager::addAsset(std::move(asset));
            break;
        }
        case ModelTypes::Texture: {
            auto asset = std::make_unique<AssetTexture>();
            asset->assetGuid = row.guid;
            asset->fileName = row.name;
            asset->path = row.path;
            AssetManager::addAsset(std::move(asset));
            break;
        }
        case ModelTypes::Music: {
            auto asset = std::make_unique<AssetMusic>();
            asset->assetGuid = row.guid;
            asset->fileName = row.name;
            asset->path = row.path;
            AssetManager::addAsset(std::move(asset));
            break;
        }
        case ModelTypes::File: {
            auto asset = std::make_unique<AssetFile>();
            asset->assetGuid = row.guid;
            asset->fileName = row.name;
            asset->path = row.path;
            AssetManager::addAsset(std::move(asset));
            break;
        }
        // ModelTypes::Shader IS NOT HYDRATED (fix round F12). It was the
        // Materials module's separate graph asset; nothing mints one and
        // nothing can read one since MATERIAL_BUNDLE_SPEC phase 2, so the
        // session entry served exactly one consumer — the Material blade's
        // combo, where picking it applied a flat default instead of the
        // graph's colours, which is worse than not offering it. A legacy row
        // in an old library keeps its pin and its bytes; it simply is not a
        // thing this session can use. (No case at all, so it falls to the
        // default below: "no session shape for this type".)
        case ModelTypes::Material: {
            // THE MATERIAL IS NOT HYDRATED HERE ANY MORE (MATERIAL-PREVIEW-1
            // item c). Registering the row is what the session registry is for
            // — assets.list, the rename sweep and the delete scrub all read the
            // guid and the name. PARSING the material and loading its textures
            // on every project open, for every material the project owns, was
            // work done for exactly ONE reader, the viewport's hover preview,
            // and that reader now resolves through SceneEditService (which
            // parses the one material a gesture actually touches, once).
            auto asset = std::make_unique<AssetMaterial>();
            asset->assetGuid = row.guid;
            asset->fileName = row.name;
            AssetManager::addAsset(std::move(asset));
            break;
        }
        case ModelTypes::ParticleSystem: {
            auto asset = std::make_unique<AssetParticleSystem>();
            asset->assetGuid = row.guid;
            asset->fileName = QFileInfo(row.name).baseName();
            asset->setValue(QVariant::fromValue(QJsonDocument::fromJson(row.asset).object()));
            AssetManager::addAsset(std::move(asset));
            break;
        }
        case ModelTypes::Animation:
            // NO SESSION SHAPE, deliberately (as for LightProfile and Avatar):
            // nothing in a live session holds a clip file as an object. Every
            // consumer — avatar.loadClip, the module's Load Animation… list —
            // reads the row's bytes through the CAS resolver at the moment it
            // applies the clip to a rig, so a session entry would be a second
            // copy of the truth with no reader.
            return false;
        default:
            return false;   // no session shape for this type (Mesh rows etc.)
        }
    return true;
}

bool ProjectAssets::updatePinToLatest(const QString &guid, Database *db, Project *project)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;
    QSqlDatabase conn = QSqlDatabase::database();
    // THE WHOLE CLOSURE, not one pin (bundles audit G3). "Update from Library"
    // used to move the asset's own pin and stop, so a project that took a
    // newer version of a bundle kept pointing at the members the OLD version
    // named — and a member the new version adds was pinned by nobody, which an
    // archive then shipped without. Taking the new version means taking what
    // it is made of; `addToProject` already computes exactly that set, so the
    // two cannot disagree.
    //
    // BUT A MEMBER'S OWN PIN IS NOT THE ASKED-FOR THING (F20, phase 1's code
    // review — a real data loss). Re-pinning the WHOLE closure to each
    // member's LIBRARY oid threw away every per-project version the project
    // had: a texture this project copied on write (ProjectAssets::copyOnWrite
    // — the user painted on it, for this project only) silently went back to
    // the library's copy because its material was updated. The user asked for
    // a newer MATERIAL, not for their edited texture to be discarded. So:
    //
    //   * the asset itself moves to the library's current version — that IS
    //     the gesture;
    //   * a member with NO pin yet is pinned (the closure gap G3 named: what
    //     the new version adds must travel);
    //   * a member the project ALREADY pins keeps its pin, whatever it points
    //     at. It is either the same bytes (nothing to do) or this project's
    //     own version (not ours to throw away). Updating THAT member is its
    //     own gesture on its own tile.
    //
    // And an EMPTY source oid never overwrites a real pin: an empty oid means
    // "a DB-only asset" (assetcas.h), so writing one over a pin that names
    // bytes is how a pinned member silently became unpinned.
    const QString projectGuid = project->getProjectGuid();
    bool ok = true;
    const QStringList closure = AssetHelper::fetchAssetAndAllDependencies(guid, db);
    for (const QString &member : closure) {
        const QString latest = sourceOidOf(conn, member);
        const bool isSubject = (member == guid);
        if (!isSubject && !AssetCas::pinnedOid(conn, projectGuid, member).isEmpty())
            continue;   // the project's own version of a member stays
        if (latest.isEmpty() && !AssetCas::pinnedOid(conn, projectGuid, member).isEmpty())
            continue;   // never turn a real pin into "no bytes"
        ok = AssetCas::writePin(conn, projectGuid, member, latest) && ok;
    }
    return ok;
}

QString ProjectAssets::copyOnWrite(const QString &guid, const QString &newContentPath,
                                   Database *db, Project *project, QString *errorOut)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no open project");
        return QString();
    }
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    // The edited bytes become a new object recorded under the asset. The
    // asset_files PK (guid, role, name) keeps the LIBRARY mapping on its
    // original oid — only this project's pin moves (I3: content immutable,
    // catalog moves pointers).
    QString oid;
    if (!AssetCas::ingestFile(conn, root, newContentPath, guid,
                              QStringLiteral("source"),
                              QFileInfo(newContentPath).fileName(), &oid, errorOut))
        return QString();
    if (!AssetCas::writePin(conn, project->getProjectGuid(), guid, oid)) {
        if (errorOut) *errorOut = QStringLiteral("could not move the project pin");
        return QString();
    }
    // The new object is reachable ONLY through the pin (the asset_files link
    // insert above is ignored when the edited file keeps its name), so the
    // store's recovery record has to be rewritten right here or the edited
    // bytes exist in no sidecar at all — item 1c'. writeSidecar now carries
    // the pins; a failure to rewrite it does not invalidate the edit itself,
    // so it is reported through the log rather than failing the write.
    QString sidecarError;
    if (!AssetCas::writeSidecar(conn, root, guid, &sidecarError)) {
        qWarning("ProjectAssets::copyOnWrite: could not refresh the sidecar for %s (%s)",
                 qUtf8Printable(guid), qUtf8Printable(sidecarError));
    }
    return oid;
}
