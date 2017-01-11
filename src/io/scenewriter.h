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
#include "../irisgl/src/irisglfwd.h"

class EditorData;

class SceneWriter : public AssetIOBase
{
public:
    void writeScene(QString filePath,iris::ScenePtr scene,EditorData* ediorData = nullptr);

private:
    void writeScene(QJsonObject& projectObj,iris::ScenePtr scene);

    void writeEditorData(QJsonObject& projectObj,EditorData* ediorData = nullptr);

    void writeSceneNode(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode);

    void writeAnimationData(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode);

    void writeMeshData(QJsonObject& sceneNodeObject,iris::MeshNodePtr meshNode);

    void writeSceneNodeMaterial(QJsonObject& matObj,iris::DefaultMaterialPtr mat);

    QJsonObject jsonColor(QColor color);

    QJsonObject jsonVector3(QVector3D vec);
    void writeLightData(QJsonObject& sceneNodeObject,iris::LightNodePtr lightNode);

    QString getSceneNodeTypeName(iris::SceneNodeType nodeType);

    QString getLightNodeTypeName(iris::LightType lightType);
};

#endif // SCENEWRITER_H
