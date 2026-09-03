/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETIMPORTERS_H
#define ASSETIMPORTERS_H

// The per-type importers (ASSET_PIPELINE_SPEC §3.2.1). Each one implements
// sniff/validate/convert against the AssetImporterBase contract; the spine
// (AssetImportService) owns store/register/rollback. Adding a library type
// is ONE class here — never a new code path.

#include "services/import/assetimportservice.h"

/// obj/fbx/dae/blend/glb/gltf → Object row + Mesh member + texture members.
/// Consolidates AssetImporter::importMesh and AssetView::importModel's mesh
/// branch: extraction from the SOURCE path (sibling .mtl/textures exist only
/// there), embedded textures decoded into the staging dir (never beside the
/// source), .obj mtllib sidecars staged precisely (no directory sweeps),
/// guid-rewritten node blob, dependency edges, import-time metadata from the
/// same assimp scene (no second parse).
class MeshImporter : public AssetImporterBase
{
public:
    QString name() const override { return QStringLiteral("mesh"); }
    int version() const override { return 1; }
    int modelType() const override;
    bool sniff(const QString &path) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;
};

/// Images, audio and video: one AssetsView row at the file's guid, a type-
/// appropriate thumbnail and an import-time metadata block (consolidates
/// AssetImporter::importFile's media branch).
class MediaImporter : public AssetImporterBase
{
public:
    explicit MediaImporter(int type);   // ModelTypes::Texture / Music / Video
    QString name() const override;
    int version() const override { return 1; }
    int modelType() const override { return mType; }
    bool sniff(const QString &path) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;

private:
    int mType;
};

/// .shader: a first-class library Shader row whose blob is the definition
/// (name/guid normalized), registered in the session AssetManager.
class ShaderImporter : public AssetImporterBase
{
public:
    QString name() const override { return QStringLiteral("shader"); }
    int version() const override { return 1; }
    int modelType() const override;
    bool sniff(const QString &path) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;
};

/// .material: a library Material row; textures the definition references
/// that exist beside the file import as member Texture rows and the blob's
/// references are rewritten to their guids (the old directory-sweep's net
/// effect, made precise).
class MaterialImporter : public AssetImporterBase
{
public:
    QString name() const override { return QStringLiteral("material"); }
    int version() const override { return 1; }
    int modelType() const override;
    bool sniff(const QString &path) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;
};

/// .ies photometric profiles: a LightProfile row whose validation, metadata
/// block and polar-lobe thumbnail all come from our own IesProfile parser
/// (LIGHTS_COMPLETION_SPEC D1/D2). validate() rejects everything the renderer's
/// loader would throw on — plus, deliberately, the partial horizontal sweeps
/// that hit its cone-type mislabelling defect — so a bad file fails at import
/// with a message instead of at first draw with an exception.
class IesImporter : public AssetImporterBase
{
public:
    QString name() const override { return QStringLiteral("lightprofile"); }
    int version() const override { return 1; }
    int modelType() const override;
    bool sniff(const QString &path) const override;
    bool validate(const QString &path, QString *errorOut) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;
};

/// Whitelisted plain files (txt/frag/vert/…): one File row + session entry.
class FileImporter : public AssetImporterBase
{
public:
    QString name() const override { return QStringLiteral("file"); }
    int version() const override { return 1; }
    int modelType() const override;
    bool sniff(const QString &path) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;
};

/// .jaf archives (single assets and bundles). convert() extracts and
/// validates; the spine commits through Database::importAsset /
/// importAssetBundle and ingests the payload files CAS-first.
class JafImporter : public AssetImporterBase
{
public:
    QString name() const override { return QStringLiteral("jaf"); }
    int version() const override { return 1; }
    int modelType() const override;
    bool sniff(const QString &path) const override;
    bool validate(const QString &path, QString *errorOut) const override;
    bool convert(const ImportRequest &request, const QString &stagingDir,
                 Database *db, Project *project, StagedAsset &out,
                 QString *errorOut, const ImportProgressFn &progress) override;
};

#endif // ASSETIMPORTERS_H
