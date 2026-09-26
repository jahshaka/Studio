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
#include "io/builtinmaterials.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "services/assetcas.h"
#include "services/meshbakestore.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/imagematerial.h"
#include "services/loadtimeline.h"
#include "services/materialbundle.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/meshbake.h"

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
    // project ALSO gets its companion material asset — created in the
    // library, then pinned in through this same function so it lands in the
    // bin, session-registered and droppable. BOUNDARY: only the DIRECTLY
    // added asset auto-creates — dependency textures riding an object's
    // closure never do (an object with 30 textures must not explode into 30
    // materials), a texture pinned as a BINDING (a light's mask or IES
    // profile, and later a decal's maps — AddKind::Binding) never does
    // either, and re-adding the same image is a no-op (a Material depending
    // on the texture already exists). The recursive addToProject cannot loop:
    // the companion is a Material, and Materials never auto-create.
    if (kind == AddKind::Direct
        && static_cast<ModelTypes>(record.type) == ModelTypes::Texture
        && !ImageMaterial::hasCompanionMaterial(guid)) {
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

QString ProjectAssets::objectSourcePath(const QString &guid, Database *db, Project *project)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return QString();
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return QString();
    if (static_cast<ModelTypes>(record.type) != ModelTypes::Object) return QString();
    return AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                   project->getProjectGuid(), guid);
}

bool ProjectAssets::registerSessionAsset(const QString &guid, Database *db,
                                         Project *project,
                                         const iris::MeshPrewarmPtr &prewarm)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;
    if (sessionHas(guid)) return true;
    const auto memberRecord = db->fetchAsset(guid);
    if (memberRecord.guid.isEmpty()) return false;

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString projectGuid = project->getProjectGuid();
    const QString member = guid;
    const QString path = AssetCas::resolvePinned(conn, root, projectGuid, member);

    switch (static_cast<ModelTypes>(memberRecord.type)) {
        case ModelTypes::Object: {
            if (path.isEmpty()) break;
            // The SECOND parse of the same model on an open: the session
            // entry for a pinned Object is a parsed scene fragment. The
            // threaded open hands us the scene already parsed on a worker
            // (irisgl/import/meshprewarm.h) — then this is a build, not a
            // parse.
            const auto makeMaterial = [](iris::MeshPtr, iris::MeshMaterialData &data) {
                return iris::MaterialPtr(BuiltinMaterials::fromMeshData(data));
            };
            // THE BAKE first (MESH_BAKE_SPEC phase 1): the SAME deserialized
            // model the scene reader used this open — one file read served
            // both consumers, where the old path parsed the file twice.
            // The counter spans the resolve, the read AND the fragment build —
            // the whole of what the parse branch below costs, so the two are
            // directly comparable in the ledger.
            LoadTimeline::Accumulate bakeAttempt(QStringLiteral("bake:sessionAsset"));
            // The prewarm is planned from PATHS, which is this content's
            // DEFAULT settings variant; a member whose row asks for other
            // settings must resolve its own (IMPORT-1).
            const bool prewarmUsable =
                MeshBakeStore::settingsHashFor(path, member)
                == MeshBakeStore::settingsHashFor(path, QString());
            iris::BakedModelPtr baked = (prewarm && prewarmUsable) ? prewarm->baked(path)
                                                                   : iris::BakedModelPtr();
            if (!baked) baked = MeshBakeStore::load(path, member);
            if (!baked) bakeAttempt.stop();   // a miss must not bank the parse below
            if (baked) {
                auto node = iris::MeshBake::buildFragment(*baked, path, makeMaterial);
                if (!node) break;
                const auto definition = QJsonDocument::fromJson(db->fetchAssetData(member)).object();
                AssetHelper::updateNodeMaterial(node, definition, db);
                auto *asset = new AssetNodeObject;
                asset->assetGuid = member;
                asset->fileName = memberRecord.name;
                asset->path = path;
                asset->setValue(QVariant::fromValue(node));
                AssetManager::addAsset(asset);
                break;
            }

            const iris::SceneSource *ready =
                (prewarm && prewarmUsable) ? prewarm->source(path) : nullptr;
            if (ready) {
                LoadTimeline::Accumulate hit(QStringLiteral("prewarm:sessionAssetHit"));
                auto node = iris::MeshNode::loadAsSceneFragment(path, *ready, makeMaterial);
                if (!node) break;
                const auto definition = QJsonDocument::fromJson(db->fetchAssetData(member)).object();
                AssetHelper::updateNodeMaterial(node, definition, db);
                auto *asset = new AssetNodeObject;
                asset->assetGuid = member;
                asset->fileName = memberRecord.name;
                asset->path = path;
                asset->setValue(QVariant::fromValue(node));
                AssetManager::addAsset(asset);
                break;
            }
            LoadTimeline::Accumulate parse(QStringLiteral("assimp:sessionAsset"));
            // The asset's import transform (IMPORT-1): this parse stands in for
            // the bake and must produce the geometry the bake holds.
            auto node = iris::MeshNode::loadAsSceneFragment(
                path, [](iris::MeshPtr, iris::MeshMaterialData &data) {
                    return iris::MaterialPtr(BuiltinMaterials::fromMeshData(data));
                }, nullptr, nullptr, QString(), MeshBakeStore::transformFor(path, member));
            if (!node) break;
            const auto definition = QJsonDocument::fromJson(db->fetchAssetData(member)).object();
            AssetHelper::updateNodeMaterial(node, definition, db);
            auto *asset = new AssetNodeObject;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            asset->setValue(QVariant::fromValue(node));
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::Texture: {
            auto *asset = new AssetTexture;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::Music: {
            auto *asset = new AssetMusic;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::File: {
            auto *asset = new AssetFile;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            AssetManager::addAsset(asset);
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
            auto *asset = new AssetMaterial;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::ParticleSystem: {
            auto *asset = new AssetParticleSystem;
            asset->assetGuid = member;
            asset->fileName = QFileInfo(memberRecord.name).baseName();
            asset->setValue(QVariant::fromValue(
                QJsonDocument::fromJson(db->fetchAssetData(member)).object()));
            AssetManager::addAsset(asset);
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
