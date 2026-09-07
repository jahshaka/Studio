/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALREADER_HPP
#define MATERIALREADER_HPP

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonValueRef>
#include <QJsonDocument>
#include <QSharedPointer>

#include "irisgl/irisglfwd.h"
#include "io/assetiobase.h"
#include "irisgl/irisglfwd.h"

class Database;
class Project;
enum class TextureSource
{
	Project,
	GlobalAssets
};

class MaterialReader : public AssetIOBase
{
	TextureSource textureSource;
	QString globalSourceFolder;

	// The live Project, injected by every construction site (Phase 4: was the
	// Globals::project static). Only read when textureSource == Project.
	Project *project = nullptr;
public:
    MaterialReader(TextureSource texSrc = TextureSource::Project, QString globalSourceFolder = "");
	void setSource(TextureSource texSrc, QString globalSrcFolder);
	void setProject(Project *p) { project = p; }

	/// Pin-world texture resolution (phase 4): guid → project pin → library
	/// source → the explicit global folder by recorded name (preview loads).
	/// The flat join(projectFolder, name) resolution is GONE.
	QString resolveTextureGuid(const QString &guid, Database *db);

	/// A saved material definition (shaderGuid + values) as a PbrMaterial.
	///
	/// It returns a PbrMaterial because since HLMS_ADOPTION P4b there IS no
	/// other material class: the six reserved builtins are PbrMaterial PRESETS
	/// (io/builtinmaterials.h), and anything else legacy has its recognisable
	/// uniform names mapped across. A definition this cannot understand yields
	/// a DEFAULT PbrMaterial, never null and never a load failure — the reserved
	/// guids are permanent, and a scene naming one must open.
	iris::PbrMaterialPtr parseMaterial(QJsonObject matObject, Database* handle, bool loadTextures = true);

	// Dispatches on the "materialType" tag SceneWriter stamps on every saved
	// material: "pbr" rebuilds a PbrMaterial from its own rows; a graph-backed
	// material loads as the shader's baked PbrMaterial; anything else — a
	// reserved builtin guid, or a legacy shader material — goes through
	// parseMaterial's conversion. EVERY branch now yields a PbrMaterial.
	iris::MaterialPtr parseMaterialTyped(QJsonObject matObject, Database* handle, bool loadTextures = true);

	/// A SHADER asset (a stored graph definition) as the baked PbrMaterial the
	/// evaluator wrote into it — the one conversion every shader preview and
	/// thumbnail goes through (VISUAL_PARITY_SPEC item 5). Null when the guid
	/// has no stored definition, when the definition predates the evaluator
	/// (no "pbrMaterial" block — materials.regenerate rebuilds those), or when
	/// it carries baked maps that cannot be resolved without an open project
	/// (a half-textured render is worse than none). The CustomMaterial-from-
	/// GLSL route these call sites used died with the evaluator's phase 5.
	iris::MaterialPtr parseShaderAsPbr(const QString &shaderGuid, Database* db);

	/// The database-free half of parseShaderAsPbr: a stored definition plus the
	/// project folder its BakedMaps/ paths resolve against ("" = no project).
	static iris::MaterialPtr shaderDefinitionAsPbr(const QJsonObject &definition,
	                                               const QString &projectFolder);

	iris::PbrMaterialPtr parsePbrMaterial(QJsonObject matObject, Database* handle, bool loadTextures = true);
	/// The PbrMaterial a reserved BUILTIN guid now stands for, with the saved
	/// values applied — the reader half of the builtin retirement.
	iris::PbrMaterialPtr createMaterialFromShaderGuid(QString shaderGuid, Database* db,
	                                                  const QJsonObject &values = QJsonObject());
	QJsonObject getShaderObjectFromId(QString shaderGuid, Database* db);
	QJsonObject convertV1MaterialToV2(QJsonObject mat);

	int getMaterialVersion(QJsonObject oldMatObj);

// (readJahShader/getParsedShader and the `parsedShader` member they filled
// were removed with the deep-audit 2026-09 pass: zero callers, and
// readJahShader was the last writer of the once-static AssetIOBase::dir that
// did not belong to a load in progress.)
};

// (ShaderHandler is GONE with HLMS_ADOPTION P4b. It existed to turn a `.shader`
// definition into an iris::CustomMaterial: V1 compiled its GLSL, V2 kept its
// uniforms as editable properties. The GLSL half died with the materials
// evaluator and the class itself died here — there is nothing left for a
// "material built from a shader" to be that a PbrMaterial preset is not.)

#endif // MATERIALREADER_HPP
