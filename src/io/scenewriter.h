/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include <QSharedPointer>
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
//#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/irisglfwd.h"

class EditorData;
class Database;	// this is a temp way to get this working, remove later

class SceneWriter : public AssetIOBase
{
	static Database *handle;
public:
	void setDatabaseHandle(Database *db) {
		this->handle = db;
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
	static void writeGrabNodeData(QJsonObject& sceneNodeObject, iris::GrabNodePtr node);
	static void writeSceneNodeMaterial(QJsonObject& matObj, iris::CustomMaterialPtr mat, bool relative = true);
    static void writeLightData(QJsonObject& sceneNodeObject, iris::LightNodePtr node);

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
