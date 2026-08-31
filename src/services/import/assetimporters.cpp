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
#include <QJsonObject>
#include <QPixmap>
#include <QTextStream>
#include <functional>

#include "zip.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"
#include "services/assethelper.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

namespace {

QPixmap thumbnailFor(int type, const QString &filePath)
{
    switch (static_cast<ModelTypes>(type)) {
    case ModelTypes::Texture: {
        auto thumb = ThumbnailManager::createThumbnail(filePath, 256, 256);
        if (thumb && thumb->thumb) return QPixmap::fromImage(*thumb->thumb);
        return QPixmap();
    }
    case ModelTypes::Video:
        return VideoUtils::thumbnailFor(filePath);
    case ModelTypes::Music:
        return QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"));
    default:
        return QPixmap();
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
    auto node = AssetHelper::extractTexturesAndMaterialFromMesh(
        request.sourcePath, textureNames, texturePaths, hasEmbedded, &modelStats, stagingDir);
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

    // The source model file.
    out.files.append({ sourceInfo.absoluteFilePath(), out.mainGuid,
                       QStringLiteral("source"), sourceInfo.fileName() });

    // .obj sidecars: exactly the .mtl files the model names (a precise
    // manifest — the old whole-directory sweeps die here).
    if (sourceInfo.suffix().toLower() == QStringLiteral("obj")) {
        QFile obj(request.sourcePath);
        if (obj.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!obj.atEnd()) {
                const QString line = QString::fromUtf8(obj.readLine()).trimmed();
                if (!line.startsWith(QStringLiteral("mtllib "))) continue;
                const QFileInfo mtl(sourceInfo.dir(), line.mid(7).trimmed());
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
    for (const auto &texPath : texturePaths) {
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

        QPixmap texThumb;
        {
            auto thumb = ThumbnailManager::createThumbnail(entry.path, 72, 72);
            if (thumb && thumb->thumb) texThumb = QPixmap::fromImage(*thumb->thumb);
        }
        StagedRow texRow;
        texRow.guid = entry.guid;
        texRow.name = entry.fileName;
        texRow.type = static_cast<int>(ModelTypes::Texture);
        texRow.parent = out.mainGuid;
        texRow.thumbnail = AssetHelper::makeBlobFromPixmap(texThumb);
        texRow.viewFilter = static_cast<int>(AssetViewFilter::Editor);
        out.rows.append(texRow);

        out.deps.append({ static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Texture),
                          out.mainGuid, entry.guid, QString() });
    }

    // Guid rewrite: the mesh path becomes the Mesh row's guid, every mesh
    // node carries the Object guid, texture material properties become
    // member texture guids.
    std::function<void(iris::SceneNodePtr &)> rewrite = [&](iris::SceneNodePtr &n) {
        if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto meshNode = n.staticCast<iris::MeshNode>();
            if (QFileInfo(meshNode->meshPath).fileName() == sourceInfo.fileName())
                meshNode->meshPath = out.meshGuid;
            meshNode->setGUID(out.mainGuid);

            auto material = meshNode->getMaterial();
            if (material) {
                for (auto prop : material->properties) {
                    if (prop->type != iris::PropertyType::Texture) continue;
                    const QString fileName = QFileInfo(prop->getValue().toString()).fileName();
                    for (const auto &tex : textures) {
                        if (tex.fileName == fileName)
                            material->setValue(prop->name, tex.guid);
                    }
                }
            }
        }
        for (auto &child : n->children) rewrite(child);
    };
    rewrite(node);

    QJsonObject blob;
    SceneWriter::writeSceneNode(blob, node, false);

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

    // Session registration (post-commit).
    const QString mainGuid = out.mainGuid;
    const QString fileName = sourceInfo.fileName();
    out.registerSession = [node, mainGuid, fileName]() {
        auto *assetObject = new AssetNodeObject;
        assetObject->fileName = fileName;
        assetObject->assetGuid = mainGuid;
        assetObject->path = AssetStorePaths::legacyFilePath(mainGuid, fileName);
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
    row.thumbnail = AssetHelper::makeBlobFromPixmap(thumbnailFor(mType, request.sourcePath));
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
                material->setValue(prop->name, entry.guid);
            } else {
                material->setValue(prop->name, QFileInfo(textureStr).fileName());
            }
        } else {
            material->setValue(prop->name, materialDefinition[prop->name].toVariant());
        }
    }

    QJsonObject blob;
    SceneWriter::writeSceneNodeMaterial(blob, material, false);

    for (const auto &tex : textures) {
        out.files.append({ tex.path, out.mainGuid, QStringLiteral("texture"), tex.fileName });
        out.files.append({ tex.path, tex.guid, QStringLiteral("source"), tex.fileName });

        QPixmap texThumb;
        {
            auto thumb = ThumbnailManager::createThumbnail(tex.path, 72, 72);
            if (thumb && thumb->thumb) texThumb = QPixmap::fromImage(*thumb->thumb);
        }
        StagedRow texRow;
        texRow.guid = tex.guid;
        texRow.name = tex.fileName;
        texRow.type = static_cast<int>(ModelTypes::Texture);
        texRow.parent = out.mainGuid;
        texRow.thumbnail = AssetHelper::makeBlobFromPixmap(texThumb);
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
