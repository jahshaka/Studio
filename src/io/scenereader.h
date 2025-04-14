/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEREADER_H
#define SCENEREADER_H

#include <QSharedPointer>
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>
#include <QMap>

#include "globals.h"
#include "core/project.h"

#include "../irisgl/src/irisglfwd.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/animation/keyframeanimation.h"

class EditorData;
class aiScene;

class Database;	// this is a temp way to get this working, remove later

class SceneReader : public AssetIOBase
{
    QHash<QString,QList<iris::MeshPtr>> meshes;
    QSet<QString> assimpScenes;
    QHash<QString,QMap<QString, iris::SkeletalAnimationPtr>> animations;

	Database *handle;
    // We can choose to load assets from a flat file or from those already cached
    // TODO - also cache assets in the viewer
public:
	void setDatabaseHandle(Database *db) {
		this->handle = db;
	}

    QString assetDirectory = Globals::project->getProjectFolder();
    bool useAlternativeLocation;
    void setBaseDirectory(const QString &location) {
        assetDirectory = location;
        useAlternativeLocation = true;
    };

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

    iris::ViewerNodePtr createViewer(QJsonObject &nodeObj);

	iris::ParticleSystemNodePtr createParticleSystem(QJsonObject &nodeObj);
	iris::GrabNodePtr createGrab(QJsonObject &nodeObj);

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
