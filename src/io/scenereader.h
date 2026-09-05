/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEREADER_H
#define SCENEREADER_H

#include <QSharedPointer>
#include "io/assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>
#include <QMap>

#include "data/project.h"

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/import/meshprewarm.h"

class EditorData;
class aiScene;

class Database;	// this is a temp way to get this working, remove later

class SceneReader : public AssetIOBase
{
    QHash<QString,QList<iris::MeshPtr>> meshes;
    QSet<QString> assimpScenes;
    QHash<QString,QMap<QString, iris::SkeletalAnimationPtr>> animations;

	// Never left indeterminate: several call sites construct a SceneReader
	// without a handle, and createMesh/readMaterial dereference it
	// (ASSETS_AUDIT.md finding 6 — that was live UB).
	Database *handle = nullptr;

	// The live Project, injected by every construction site (Phase 4: was the
	// Globals::project static, which assetDirectory's member initialiser also
	// read). Never left indeterminate for the same reason as `handle`.
	Project *project = nullptr;

	iris::MeshPrewarmPtr prewarm;
    // We can choose to load assets from a flat file or from those already cached
    // TODO - also cache assets in the viewer
public:
	void setDatabaseHandle(Database *db) {
		this->handle = db;
	}

	/// Model files already parsed on another thread (irisgl/import/meshprewarm.h).
	/// When a mesh source is in here the reader consumes the prewarmed aiScene
	/// instead of calling assimp — the whole point of the threaded open. Null
	/// (the default) keeps the synchronous behaviour exactly.
	void setPrewarm(const iris::MeshPrewarmPtr &prewarm) { this->prewarm = prewarm; }

	/// Every model path this blob's mesh nodes reference, resolved the way
	/// createMesh() will resolve them. The plan the prewarm worker is given.
	QStringList collectMeshSources(const QJsonObject &projectObj);

	/// Injecting the project also seeds assetDirectory, which used to be
	/// initialised from Globals::project in its member initialiser. The
	/// isEmpty() guard reproduces the old initialiser-then-setBaseDirectory
	/// ordering whichever order the two setters are called in.
	void setProject(Project *p) {
		project = p;
		if (p && assetDirectory.isEmpty()) assetDirectory = p->getProjectFolder();
	}

    QString assetDirectory;
    bool useAlternativeLocation;
    void setBaseDirectory(const QString &location) {
        assetDirectory = location;
        useAlternativeLocation = true;
    };

    /// Resolve guids against the LIBRARY source rather than the open
    /// project's pins, with no directory fallback — what a store-asset
    /// preview wants. The callers that used to say
    /// setBaseDirectory(<storeRoot>/<guid>/) say this instead: the retired
    /// legacy view is gone (deep audit 2026-09, area 6) and that directory
    /// was only ever a pre-CAS fallback keyed on the WRONG asset's guid.
    void setLibrarySource() { useAlternativeLocation = true; }

    /// Pin-world byte resolution (ASSET_PIPELINE_SPEC §3.1.5, phase 4):
    /// project loads resolve guid → project pin → CAS object; preview loads
    /// (useAlternativeLocation) resolve guid → library source, then the
    /// explicit directory by recorded name. The flat
    /// join(projectFolder, name) resolution is GONE.
    QString resolveAssetPath(const QString &guid);

public:
    iris::ScenePtr readScene(const QString &projectPath,
                             const QByteArray &sceneBlob,
                             iris::PostProcessManagerPtr postMan,
                             EditorData **editorData = nullptr);
    iris::ScenePtr readScene(QJsonObject &projectObj);
    EditorData* readEditorData(QJsonObject &projectObj);
    void readPostProcessData(QJsonObject &projectObj, iris::PostProcessManagerPtr postMan);

    /**
     * Creates scene node from json data
     * @param nodeObj
     * @return
     */
    iris::SceneNodePtr readSceneNode(QJsonObject &nodeObj);

    void readAnimationData(QJsonObject &nodeObj, iris::SceneNodePtr sceneNode);

    /**
     * Reads pos, rot and scale properties from json object
     * if scale isnt available then it's set to (1,1,1) by default
     * @param nodeObj
     * @param sceneNode
     */
    void readSceneNodeTransform(QJsonObject &nodeObj, iris::SceneNodePtr sceneNode);

    /**
     * Creates mesh using scene node data
     * @param nodeObj
     * @return
     */
    iris::MeshNodePtr createMesh(QJsonObject &nodeObj);

    /**
     * Creates light from light node data
     * @param nodeObj
     * @return
     */
    iris::LightNodePtr createLight(QJsonObject &nodeObj);
    /// Decals (DECALS_SPEC): the stored guid resolves pin-first through the CAS.
    iris::DecalNodePtr createDecal(QJsonObject &nodeObj);
    /// Scene-graph cameras (CAMERAS_SPEC §3). The project's `editor.camera`
    /// block is a different thing entirely and is read by readEditorData.
    iris::CameraNodePtr createCamera(QJsonObject &nodeObj);

    iris::ViewerNodePtr createViewer(QJsonObject &nodeObj);

	iris::ParticleSystemNodePtr createParticleSystem(QJsonObject &nodeObj);

    iris::LightType getLightTypeFromName(QString lightType);
    iris::TangentType getTangentTypeFromName(QString tangentType);
    iris::HandleMode getHandleModeFromName(QString handleMode);

    /**
     * Extracts material from node's json object.
     * Creates default material if one isnt defined in nodeObj
     * @param nodeObj
     * @return
     */
    iris::MaterialPtr readMaterial(QJsonObject &nodeObj);
    iris::MaterialPtr readPbrMaterial(const QJsonObject& matObj);

    // extracts meshes and animations from model file
    void extractAssetsFromAssimpScene(QString filePath);

    /**
     * Returns mesh from mesh file at index
     * if the mesh doesnt exist, nullptr is returned
     * @param filePath
     * @param index
     * @return
     */
    iris::MeshPtr getMesh(QString filePath, int index);

    iris::SkeletalAnimationPtr getSkeletalAnimation(QString filePath, QString animName);
};

#endif // SCENEREADER_H
