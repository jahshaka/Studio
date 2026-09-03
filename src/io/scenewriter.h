/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include <QSharedPointer>
#include "io/assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
//#include "../irisgl/src/core/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/irisglfwd.h"

class EditorData;
class Database;	// this is a temp way to get this working, remove later
class Project;

class SceneWriter : public AssetIOBase
{
	static Database *handle;

	// The live Project (Phase 4: was the Globals::project static). Static to
	// mirror `handle` above: both project reads live in *static* writer methods
	// (writeParticleData / writeSceneNodeMaterial) that a dozen call sites
	// invoke unqualified, so there is no instance to hang it off. Wired once by
	// the shell in MainWindow::setupServices.
	static Project *projectHandle;
public:
	void setDatabaseHandle(Database *db) {
		this->handle = db;
	}
	static void setProject(Project *p) {
		projectHandle = p;
	}
    void writeScene(QString filePath,iris::ScenePtr scene, iris::PostProcessManagerPtr postMan, EditorData* ediorData = nullptr);
    QByteArray getSceneObject(QString projectPath,
                              iris::ScenePtr scene,
                              iris::PostProcessManagerPtr postMan,
                              EditorData *editorData);

public:
    void writeScene(QJsonObject& projectObj, iris::ScenePtr scene);
    void writePostProcessData(QJsonObject& projectObj, iris::PostProcessManagerPtr postMan);
    void writeEditorData(QJsonObject& projectObj, EditorData* ediorData = nullptr);

    static void writeSceneNode(QJsonObject& sceneNodeObj, iris::SceneNodePtr node, bool relative = true);
	static void writeAnimationData(QJsonObject& sceneNodeObj, iris::SceneNodePtr node);
    static void writeMeshData(QJsonObject& sceneNodeObject, iris::MeshNodePtr node, bool relative = true);
	static void writeViewerData(QJsonObject& sceneNodeObject, iris::ViewerNodePtr node);
	static void writeParticleData(QJsonObject& sceneNodeObject, iris::ParticleSystemNodePtr node);
	static void writeSceneNodeMaterial(QJsonObject& matObj, iris::MaterialPtr mat, bool relative = true);
    static void writeLightData(QJsonObject& sceneNodeObject, iris::LightNodePtr node);
    /// Decals (DECALS_SPEC): the IMAGE is stored as a guid, never a path — the
    /// reader resolves it pin-first through the CAS like every other asset.
    static void writeDecalData(QJsonObject& sceneNodeObject, iris::DecalNodePtr node);

	static QJsonObject jsonColor(QColor color);
	static QJsonObject jsonVector2(QVector2D vec);
	static QJsonObject jsonVector3(QVector3D vec);
	static QJsonObject jsonVector4(QVector4D vec);

    static QString getSceneNodeTypeName(iris::SceneNodeType nodeType);
	static QString getLightNodeTypeName(iris::LightType lightType);
	static QString getKeyTangentTypeName(iris::TangentType tangentType);
	static QString getKeyHandleModeName(iris::HandleMode handleMode);
};

#endif // SCENEWRITER_H
