/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/import/assetimporters.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QBuffer>
#include <QImage>
#include <QJsonObject>
#include <QTextStream>
#include <functional>

#include "zip.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/iesprofile.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/materialhelper.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/core/logger.h"

namespace {

// Convert() runs on a worker thread (ImportBatchRunner) — QImage only, never
// QPixmap (GUI-thread-bound). PNG bytes via QBuffer, the exact blob
// AssetHelper::makeBlobFromPixmap produced.
QByteArray pngBlobFromImage(const QImage &image)
{
    if (image.isNull()) return QByteArray();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QImage thumbnailImageFor(int type, const QString &filePath)
{
    switch (static_cast<ModelTypes>(type)) {
    case ModelTypes::Texture: {
        auto thumb = ThumbnailManager::createThumbnail(filePath, 256, 256);
        if (thumb && !thumb->thumb.isNull()) return thumb->thumb;
        return QImage();
    }
    case ModelTypes::Video: {
        // grabFrame hard-gates on the GUI thread (QMediaPlayer + event loop)
        // and returns null on a worker — the film icon lands in the row and
        // AssetView's post-import UI tail grabs the real frame.
        const QImage frame = VideoUtils::grabFrame(filePath);
        if (!frame.isNull())
            return frame.width() >= frame.height()
                       ? frame.scaledToWidth(256, Qt::SmoothTransformation)
                       : frame.scaledToHeight(256, Qt::SmoothTransformation);
        return QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-video.png"));
    }
    case ModelTypes::Music:
        return QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"));
    default:
        return QImage();
    }
}

} // namespace

// ============================ MeshImporter ============================

int MeshImporter::modelType() const { return static_cast<int>(ModelTypes::Mesh); }

bool MeshImporter::sniff(const QString &path) const
{
    return Constants::MODEL_EXTS.contains(QFileInfo(path).suffix().toLower());
}

bool MeshImporter::convert(const ImportRequest &request, const QString &stagingDir,
                           Database *db, Project *project, StagedAsset &out,
                           QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(db);
    const QFileInfo sourceInfo(request.sourcePath);
    const QString projectGuid = !request.projectGuid.isEmpty()
                                    ? request.projectGuid
                                    : (project ? project->getProjectGuid() : QString());

    out.mainGuid = GUIDManager::generateGUID();
    out.meshGuid = GUIDManager::generateGUID();

    if (progress && !progress(QStringLiteral("convert"), 0, 0)) {
        if (errorOut) *errorOut = QStringLiteral("cancelled");
        return false;
    }

    // ONE assimp parse: node graph, texture discovery, metadata counts.
    // Extraction from the SOURCE path (sibling .mtl/textures only exist
    // there); embedded textures and split MR maps land in stagingDir.
    QStringList textureNames, texturePaths;
    bool hasEmbedded = false;
    QJsonObject modelStats;
    // Drop anything a previous parse on this thread left behind, so the
    // warnings taken below belong to THIS model.
    iris::MaterialHelper::takeContainmentWarnings();
    // OUR SceneSource, so the aiScene survives the call and the mesh BAKE
    // (MESH_BAKE_SPEC phase 1) is written from the SAME parse — the import
    // still pays exactly one assimp parse, which import.async asserts.
    iris::SceneSource modelScene;
    auto node = AssetHelper::extractTexturesAndMaterialFromMesh(
        request.sourcePath, textureNames, texturePaths, hasEmbedded, &modelStats, stagingDir,
        &modelScene);
    // Texture references that named a file outside the model's own folder:
    // contained by MaterialHelper (the path never resolves outside), reported
    // here so the user learns their model lost a map (deep audit 2026-09 F2).
    out.warnings += iris::MaterialHelper::takeContainmentWarnings();
    if (!node) {
        if (errorOut)
            *errorOut = QStringLiteral("\"%1\" could not be imported. The file may be "
                                       "corrupt or use an unsupported feature (for example "
                                       "Draco mesh compression in a .glb/.gltf).")
                            .arg(sourceInfo.fileName());
        return false;
    }
    out.metadata = modelStats;
    out.node = node;

    // The source model file — recorded under BOTH the Object guid and the Mesh
    // member guid (same content id: the CAS dedups). Scene instantiation
    // (SceneReader::createMesh) resolves the blob's "mesh" reference — the Mesh
    // guid — through resolvePinned/resolveSource; without a file row under
    // that guid every scene-placed import silently lost its geometry.
    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });
    out.files.append({ sourceInfo.absoluteFilePath(), out.meshGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });

    // ---- THE BAKE (MESH_BAKE_SPEC phase 1) --------------------------------
    //
    // The parse we just paid, frozen: opening a world that uses this model is
    // then a file read instead of ~0.9 s of assimp per open, forever. Derived
    // data — the source above stays the truth and is never deleted; a bake
    // whose fingerprint no longer matches this build is ignored and rebuilt.
    //
    // Recorded under BOTH guids exactly as the source is, so `assets.gc`
    // reaches it through the same asset_files rows and reaps it with the
    // asset. A failure here is NEVER an import failure: the open path falls
    // back to the parse it has always done.
    out.sourceOid = AssetCas::hashFile(request.sourcePath);
    if (!out.sourceOid.isEmpty()) {
        const QString bakeName = iris::MeshBake::fileNameFor(out.sourceOid);
        const QString bakePath = QDir(stagingDir).filePath(bakeName);
        iris::MeshBake::Model baked = iris::MeshBake::buildFromScene(
            modelScene.importer.GetScene(), request.sourcePath,
            iris::MeshBake::fingerprintFor(out.sourceOid), stagingDir);
        QString bakeError;
        if (baked.valid && iris::MeshBake::write(bakePath, baked, &bakeError)) {
            out.files.append({ bakePath, out.mainGuid, iris::MeshBake::casRole(), bakeName });
            out.files.append({ bakePath, out.meshGuid, iris::MeshBake::casRole(), bakeName });
        } else if (!bakeError.isEmpty()) {
            irisLog("import: " + bakeError + " (the open path will parse instead)");
        }
    }

    // .obj sidecars: exactly the .mtl files the model names (a precise
    // manifest — the old whole-directory sweeps die here).
    if (sourceInfo.suffix().toLower() == QStringLiteral("obj")) {
        QFile obj(request.sourcePath);
        if (obj.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString objDir = sourceInfo.absolutePath();
            while (!obj.atEnd()) {
                const QString line = QString::fromUtf8(obj.readLine()).trimmed();
                if (!line.startsWith(QStringLiteral("mtllib "))) continue;
                // `mtllib` names a file, from inside the .obj — the same
                // untrusted-path problem as the texture references, and the
                // same containment (deep audit 2026-09 F2). `mtllib
                // ../../../.ssh/id_rsa` used to be staged into the store as a
                // sidecar and shipped in every export of the project.
                const QString named = line.mid(7).trimmed();
                const QString contained = iris::MaterialHelper::containedTexturePath(
                    named, objDir, QStringLiteral("material library"));
                out.warnings += iris::MaterialHelper::takeContainmentWarnings();
                if (contained.isEmpty()) continue;
                const QFileInfo mtl(contained);
                if (mtl.exists() && mtl.isFile())
                    out.files.append({ mtl.absoluteFilePath(), out.mainGuid,
                                       QStringLiteral("sidecar"), mtl.fileName() });
            }
        }
    }

    // Referenced textures — on-disk siblings and staged embedded extractions
    // alike: a member Texture row each, content recorded under BOTH the
    // member guid (its own source) and the Object (role "texture").
    struct TexEntry { QString fileName, guid, path; };
    QVector<TexEntry> textures;
    int texDone = 0;
    for (const auto &texPath : texturePaths) {
        if (progress && !progress(QStringLiteral("textures"), texDone++, texturePaths.size())) {
            if (errorOut) *errorOut = QStringLiteral("cancelled");
            return false;
        }
        const QFileInfo texInfo(texPath);
        if (!texInfo.exists() || !texInfo.isFile()) continue;
        if (std::any_of(textures.begin(), textures.end(),
                        [&](const TexEntry &t) { return t.fileName == texInfo.fileName(); }))
            continue;
        TexEntry entry{ texInfo.fileName(), GUIDManager::generateGUID(),
                        texInfo.absoluteFilePath() };
        textures.append(entry);

        out.files.append({ entry.path, out.mainGuid, QStringLiteral("texture"), entry.fileName });
        out.files.append({ entry.path, entry.guid, QStringLiteral("source"), entry.fileName });

        QImage texThumb;
        {
            auto thumb = ThumbnailManager::createThumbnail(entry.path, 72, 72);
            if (thumb && !thumb->thumb.isNull()) texThumb = thumb->thumb;
        }
        StagedRow texRow;
        texRow.guid = entry.guid;
        texRow.name = entry.fileName;
        texRow.type = static_cast<int>(ModelTypes::Texture);
        texRow.parent = out.mainGuid;
        texRow.thumbnail = pngBlobFromImage(texThumb);
        texRow.viewFilter = static_cast<int>(AssetViewFilter::Editor);
        out.rows.append(texRow);

        out.deps.append({ static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Texture),
                          out.mainGuid, entry.guid, QString() });
    }

    // Guid rewrite, node side: the mesh path becomes the Mesh row's guid and
    // every mesh node carries the Object guid. Texture references are NOT
    // rewritten on the live node - Material::setValue() eagerly calls
    // Texture2D::load() on texture properties, so writing a guid into the
    // live material both logged "error loading image: <guid>" per texture and
    // dropped the already-loaded map from the session-registered asset. The
    // guid substitution happens on the serialized blob below instead.
    std::function<void(iris::SceneNodePtr &)> rewrite = [&](iris::SceneNodePtr &n) {
        if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto meshNode = n.staticCast<iris::MeshNode>();
            if (QFileInfo(meshNode->meshPath).fileName() == sourceInfo.fileName())
                meshNode->meshPath = out.meshGuid;
            meshNode->setGUID(out.mainGuid);
        }
        for (auto &child : n->children()) rewrite(child);
    };
    rewrite(node);

    QJsonObject blob;
    SceneWriter::writeSceneNode(blob, node, false);

    // Guid rewrite, blob side: texture material values (written as paths by
    // writeSceneNodeMaterial) become member texture guids, matched by file
    // name (the member list is unique by file name). Readers resolve them
    // back through the CAS (MaterialReader/SceneReader/AssetHelper).
    std::function<QJsonObject(QJsonObject)> substituteTextureGuids =
        [&](QJsonObject nodeObj) -> QJsonObject {
        QJsonObject matObj = nodeObj.value(QStringLiteral("material")).toObject();
        QJsonObject values = matObj.value(QStringLiteral("values")).toObject();
        if (!values.isEmpty()) {
            for (auto it = values.begin(); it != values.end(); ++it) {
                if (!it.value().isString()) continue;
                const QString fileName = QFileInfo(it.value().toString()).fileName();
                if (fileName.isEmpty()) continue;
                for (const auto &tex : textures) {
                    if (tex.fileName == fileName) { it.value() = tex.guid; break; }
                }
            }
            matObj[QStringLiteral("values")] = values;
            nodeObj[QStringLiteral("material")] = matObj;
        }
        QJsonArray children = nodeObj.value(QStringLiteral("children")).toArray();
        for (int i = 0; i < children.size(); ++i)
            children[i] = substituteTextureGuids(children[i].toObject());
        if (!children.isEmpty()) nodeObj[QStringLiteral("children")] = children;
        return nodeObj;
    };
    blob = substituteTextureGuids(blob);

    // Mesh member row (no blob — matches the legacy importers' tail).
    StagedRow meshRow;
    meshRow.guid = out.meshGuid;
    meshRow.name = sourceInfo.fileName();
    meshRow.type = static_cast<int>(ModelTypes::Mesh);
    meshRow.parent = out.mainGuid;
    meshRow.viewFilter = static_cast<int>(AssetViewFilter::Editor);
    out.rows.append(meshRow);

    // The Object row — the one library tile.
    StagedRow objectRow;
    objectRow.guid = out.mainGuid;
    objectRow.name = sourceInfo.baseName();
    objectRow.type = static_cast<int>(ModelTypes::Object);
    objectRow.asset = QJsonDocument(blob).toJson();
    objectRow.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    out.rows.append(objectRow);

    out.deps.append({ static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh),
                      out.mainGuid, out.meshGuid, QString() });

    // Session registration (post-commit). The live node's texture properties
    // still point into the extraction staging dir, which dies with the import;
    // re-point them at the durable CAS object paths (resolvable post-commit)
    // so the session-registered asset renders for the rest of the session.
    const QString mainGuid = out.mainGuid;
    const QString fileName = sourceInfo.fileName();
    out.registerSession = [node, mainGuid, fileName, textures]() {
        QSqlDatabase conn = QSqlDatabase::database();
        const QString root = AssetStorePaths::root();
        std::function<void(iris::SceneNodePtr)> repoint = [&](iris::SceneNodePtr n) {
            if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                auto material = n.staticCast<iris::MeshNode>()->getMaterial();
                if (material) for (auto prop : material->properties) {
                    if (!prop || prop->type != iris::PropertyType::Texture) continue;
                    const QString fn = QFileInfo(prop->getValue().toString()).fileName();
                    if (fn.isEmpty()) continue;
                    for (const auto &tex : textures) {
                        if (tex.fileName != fn) continue;
                        const QString path = AssetCas::resolveSource(conn, root, tex.guid);
                        if (!path.isEmpty()) material->setValue(prop->name, path);
                        break;
                    }
                }
            }
            for (auto &child : n->children()) repoint(child);
        };
        repoint(node);

        auto *assetObject = new AssetNodeObject;
        assetObject->fileName = fileName;
        assetObject->assetGuid = mainGuid;
        // The session registration points at the asset's STORED bytes (the
        // CAS object), not at the retired <root>/<guid>/<name> view — this
        // path is what the drag-drop and re-open paths open (deep audit
        // 2026-09, area 6).
        assetObject->path = AssetCas::resolveSource(conn, root, mainGuid);
        assetObject->setValue(QVariant::fromValue(node));
        AssetManager::addAsset(assetObject);
    };

    Q_UNUSED(projectGuid);
    return true;
}

// ============================ MediaImporter ============================

MediaImporter::MediaImporter(int type) : mType(type) {}

QString MediaImporter::name() const
{
    switch (static_cast<ModelTypes>(mType)) {
    case ModelTypes::Texture: return QStringLiteral("image");
    case ModelTypes::Music: return QStringLiteral("audio");
    case ModelTypes::Video: return QStringLiteral("video");
    default: return QStringLiteral("media");
    }
}

bool MediaImporter::sniff(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    switch (static_cast<ModelTypes>(mType)) {
    case ModelTypes::Texture: return Constants::IMAGE_EXTS.contains(suffix);
    case ModelTypes::Music: return Constants::AUDIO_EXTS.contains(suffix);
    case ModelTypes::Video: return Constants::VIDEO_EXTS.contains(suffix);
    default: return false;
    }
}

bool MediaImporter::convert(const ImportRequest &request, const QString &stagingDir,
                            Database *db, Project *project, StagedAsset &out,
                            QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(stagingDir); Q_UNUSED(db); Q_UNUSED(project); Q_UNUSED(errorOut);
    const QFileInfo sourceInfo(request.sourcePath);
    out.mainGuid = GUIDManager::generateGUID();

    if (progress && !progress(QStringLiteral("convert"), 0, 0)) {
        if (errorOut) *errorOut = QStringLiteral("cancelled");
        return false;
    }

    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });

    // Import-time metadata: image header / wav header / video probe.
    out.metadata = (mType == static_cast<int>(ModelTypes::Texture))
                       ? AssetMetadata::forImageFile(request.sourcePath)
                       : (mType == static_cast<int>(ModelTypes::Video))
                             ? AssetMetadata::forVideoFile(request.sourcePath)
                             : AssetMetadata::forAudioFile(request.sourcePath);

    StagedRow row;
    row.guid = out.mainGuid;
    row.name = sourceInfo.fileName();
    row.type = mType;
    row.thumbnail = pngBlobFromImage(thumbnailImageFor(mType, request.sourcePath));
    row.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    out.rows.append(row);
    return true;
}

// ============================ ShaderImporter ============================

int ShaderImporter::modelType() const { return static_cast<int>(ModelTypes::Shader); }

bool ShaderImporter::sniff(const QString &path) const
{
    return QFileInfo(path).suffix().toLower() == Constants::SHADER_EXT;
}

bool ShaderImporter::convert(const ImportRequest &request, const QString &stagingDir,
                             Database *db, Project *project, StagedAsset &out,
                             QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(stagingDir); Q_UNUSED(db); Q_UNUSED(project); Q_UNUSED(progress);
    const QFileInfo sourceInfo(request.sourcePath);

    QFile shaderFile(request.sourcePath);
    if (!shaderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = QStringLiteral("cannot read %1").arg(sourceInfo.fileName());
        return false;
    }
    QJsonObject definition = QJsonDocument::fromJson(shaderFile.readAll()).object();
    if (definition.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("%1 is not a shader definition").arg(sourceInfo.fileName());
        return false;
    }

    out.mainGuid = GUIDManager::generateGUID();
    definition["name"] = sourceInfo.baseName();
    definition["guid"] = out.mainGuid;

    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });

    StagedRow row;
    row.guid = out.mainGuid;
    row.name = sourceInfo.baseName();
    row.type = static_cast<int>(ModelTypes::Shader);
    row.asset = QJsonDocument(definition).toJson();
    row.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    out.rows.append(row);

    const QString guid = out.mainGuid;
    const QString baseName = sourceInfo.baseName();
    out.registerSession = [definition, guid, baseName]() {
        auto *assetShader = new AssetShader;
        assetShader->assetGuid = guid;
        assetShader->fileName = baseName;
        assetShader->setValue(QVariant::fromValue(definition));
        AssetManager::addAsset(assetShader);
    };
    return true;
}

// ============================= IesImporter ================================

int IesImporter::modelType() const { return static_cast<int>(ModelTypes::LightProfile); }

bool IesImporter::sniff(const QString &path) const
{
    return Constants::LIGHT_PROFILE_EXTS.contains(QFileInfo(path).suffix().toLower());
}

bool IesImporter::validate(const QString &path, QString *errorOut) const
{
    const IesProfile profile = IesProfile::parse(path);
    if (!profile.ok) {
        if (errorOut) *errorOut = profile.error;
        return false;
    }
    return true;
}

bool IesImporter::convert(const ImportRequest &request, const QString &stagingDir,
                          Database *db, Project *project, StagedAsset &out,
                          QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(stagingDir); Q_UNUSED(db); Q_UNUSED(project); Q_UNUSED(progress);
    const QFileInfo sourceInfo(request.sourcePath);

    // validate() already ran in the spine, but convert() is also reachable from
    // assets.checkConsistency — re-parse rather than assume.
    const IesProfile profile = IesProfile::parse(request.sourcePath);
    if (!profile.ok) {
        if (errorOut) *errorOut = profile.error;
        return false;
    }

    out.mainGuid = GUIDManager::generateGUID();
    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });
    out.metadata = AssetMetadata::forLightProfileFile(request.sourcePath);

    StagedRow row;
    row.guid = out.mainGuid;
    row.name = sourceInfo.fileName();
    row.type = static_cast<int>(ModelTypes::LightProfile);
    // A polar plot of the candela lobe: two profiles look identical as file
    // names and completely different as light.
    row.thumbnail = pngBlobFromImage(profile.polarThumbnail(256));
    row.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    out.rows.append(row);
    return true;
}

// ============================ MaterialImporter ============================

int MaterialImporter::modelType() const { return static_cast<int>(ModelTypes::Material); }

bool MaterialImporter::sniff(const QString &path) const
{
    return Constants::MATERIAL_EXTS.contains(QFileInfo(path).suffix().toLower());
}

bool MaterialImporter::convert(const ImportRequest &request, const QString &stagingDir,
                               Database *db, Project *project, StagedAsset &out,
                               QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(stagingDir); Q_UNUSED(db); Q_UNUSED(project); Q_UNUSED(progress);
    const QFileInfo sourceInfo(request.sourcePath);

    QFile matFile(request.sourcePath);
    if (!matFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = QStringLiteral("cannot read %1").arg(sourceInfo.fileName());
        return false;
    }
    const QJsonObject materialDefinition = QJsonDocument::fromJson(matFile.readAll()).object();
    if (materialDefinition.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("%1 is not a material definition").arg(sourceInfo.fileName());
        return false;
    }

    out.mainGuid = GUIDManager::generateGUID();

    // Normalize through the shader's property set (the legacy
    // extractTexturesAndMaterialFromMaterial), collecting texture references.
    auto materialName = materialDefinition["name"].toString();
    auto shaderName = Constants::SHADER_DEFS + materialName + ".shader";
    if (materialName.isEmpty()) {
        shaderName = QStringLiteral("app/shader_defs/Default.shader");
        materialName = QStringLiteral("Default");
    }
    auto material = iris::CustomMaterial::create();
    material->generate(IrisUtils::getAbsoluteAssetPath(shaderName));
    material->setName(materialName);

    struct TexEntry { QString fileName, guid, path; };
    QVector<TexEntry> textures;
    for (const auto &prop : material->properties) {
        if (!materialDefinition.contains(prop->name)) continue;
        if (prop->type == iris::PropertyType::Texture) {
            const QString textureStr = materialDefinition[prop->name].toString();
            if (textureStr.isEmpty()) continue;
            // Textures referenced by the definition that exist beside the
            // .material file import as members; the reference becomes a guid.
            const QFileInfo texInfo(sourceInfo.dir(), QFileInfo(textureStr).fileName());
            if (texInfo.exists() && texInfo.isFile()) {
                TexEntry entry{ texInfo.fileName(), GUIDManager::generateGUID(),
                                texInfo.absoluteFilePath() };
                textures.append(entry);
                // The live material keeps the real path (setValue eagerly
                // loads texture properties; a guid would log an error and
                // drop the map). The blob substitution below writes the guid.
                material->setValue(prop->name, entry.path);
            } else {
                material->setValue(prop->name, QFileInfo(textureStr).fileName());
            }
        } else {
            material->setValue(prop->name, materialDefinition[prop->name].toVariant());
        }
    }

    QJsonObject blob;
    SceneWriter::writeSceneNodeMaterial(blob, material, false);

    // Texture values become member guids in the stored definition (readers
    // resolve them through the CAS); matched by file name, unique per import.
    {
        QJsonObject values = blob.value(QStringLiteral("values")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().isString()) continue;
            const QString fn = QFileInfo(it.value().toString()).fileName();
            if (fn.isEmpty()) continue;
            for (const auto &tex : textures) {
                if (tex.fileName == fn) { it.value() = tex.guid; break; }
            }
        }
        blob[QStringLiteral("values")] = values;
    }

    for (const auto &tex : textures) {
        out.files.append({ tex.path, out.mainGuid, QStringLiteral("texture"), tex.fileName });
        out.files.append({ tex.path, tex.guid, QStringLiteral("source"), tex.fileName });

        QImage texThumb;
        {
            auto thumb = ThumbnailManager::createThumbnail(tex.path, 72, 72);
            if (thumb && !thumb->thumb.isNull()) texThumb = thumb->thumb;
        }
        StagedRow texRow;
        texRow.guid = tex.guid;
        texRow.name = tex.fileName;
        texRow.type = static_cast<int>(ModelTypes::Texture);
        texRow.parent = out.mainGuid;
        texRow.thumbnail = pngBlobFromImage(texThumb);
        texRow.viewFilter = static_cast<int>(AssetViewFilter::Editor);
        out.rows.append(texRow);
        out.deps.append({ static_cast<int>(ModelTypes::Material), static_cast<int>(ModelTypes::Texture),
                          out.mainGuid, tex.guid, QString() });
    }

    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });

    StagedRow row;
    row.guid = out.mainGuid;
    row.name = sourceInfo.baseName();
    row.type = static_cast<int>(ModelTypes::Material);
    row.asset = QJsonDocument(blob).toJson();
    row.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    out.rows.append(row);

    const QString guid = out.mainGuid;
    out.registerSession = [material, guid]() {
        auto *assetMat = new AssetMaterial;
        assetMat->assetGuid = guid;
        assetMat->setValue(QVariant::fromValue(iris::MaterialPtr(material)));
        AssetManager::addAsset(assetMat);
    };
    return true;
}

// ============================ FileImporter ============================

int FileImporter::modelType() const { return static_cast<int>(ModelTypes::File); }

bool FileImporter::sniff(const QString &path) const
{
    return Constants::WHITELIST.contains(QFileInfo(path).suffix().toLower());
}

bool FileImporter::convert(const ImportRequest &request, const QString &stagingDir,
                           Database *db, Project *project, StagedAsset &out,
                           QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(stagingDir); Q_UNUSED(db); Q_UNUSED(project); Q_UNUSED(errorOut); Q_UNUSED(progress);
    const QFileInfo sourceInfo(request.sourcePath);
    out.mainGuid = GUIDManager::generateGUID();

    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });
    out.metadata = AssetMetadata::forGenericFile(request.sourcePath);

    StagedRow row;
    row.guid = out.mainGuid;
    row.name = sourceInfo.fileName();
    row.type = static_cast<int>(ModelTypes::File);
    row.viewFilter = static_cast<int>(AssetViewFilter::AssetsView);
    out.rows.append(row);

    const QString guid = out.mainGuid;
    const QString fileName = sourceInfo.fileName();
    out.registerSession = [guid, fileName]() {
        auto *assetFile = new AssetFile;
        assetFile->assetGuid = guid;
        assetFile->fileName = fileName;
        assetFile->path = guid;
        AssetManager::addAsset(assetFile);
    };
    return true;
}

// ============================ JafImporter ============================

int JafImporter::modelType() const { return static_cast<int>(ModelTypes::Object); }

bool JafImporter::sniff(const QString &path) const
{
    return QFileInfo(path).suffix().toLower() == Constants::ASSET_EXT;
}

bool JafImporter::validate(const QString &path, QString *errorOut) const
{
    if (!QFileInfo::exists(path)) {
        if (errorOut) *errorOut = QStringLiteral("no such file %1").arg(path);
        return false;
    }
    return true;
}

bool JafImporter::convert(const ImportRequest &request, const QString &stagingDir,
                          Database *db, Project *project, StagedAsset &out,
                          QString *errorOut, const ImportProgressFn &progress)
{
    Q_UNUSED(project);
    if (progress && !progress(QStringLiteral("extract"), 0, 0)) {
        if (errorOut) *errorOut = QStringLiteral("cancelled");
        return false;
    }

    zip_extract(request.sourcePath.toStdString().c_str(),
                stagingDir.toStdString().c_str(), nullptr, nullptr);

    const QString manifestPath = QDir(stagingDir).filePath(QStringLiteral(".manifest"));
    const QString dbPath = QDir(stagingDir).filePath(QStringLiteral("asset.db"));

    QFile manifest(manifestPath);
    if (!manifest.exists() || !db->checkIfJafModelVersionSupported(dbPath)) {
        if (errorOut)
            *errorOut = QStringLiteral("This asset was made with a deprecated version of "
                                       "Jahshaka. You can extract the contents manually and "
                                       "try importing as regular assets.");
        return false;
    }
    if (!manifest.open(QFile::ReadOnly | QFile::Text)) {
        if (errorOut) *errorOut = QStringLiteral("unreadable .manifest");
        return false;
    }
    QTextStream in(&manifest);
    QStringList lines;
    while (!in.atEnd()) lines << in.readLine();
    manifest.close();
    if (lines.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty .manifest");
        return false;
    }

    out.jaf.kind = lines.first();
    out.jaf.dbPath = dbPath;
    out.jaf.assetsDir = QDir(stagingDir).filePath(QStringLiteral("assets"));
    out.jaf.bundleLines = lines.mid(1);
    out.jafKind = out.jaf.kind;
    // Rows/files are produced by the spine's jaf commit path — the archive
    // carries its own row set (asset.db) and per-guid payload dirs.
    return true;
}
