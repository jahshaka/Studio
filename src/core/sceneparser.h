/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEPARSER_H
#define SCENEPARSER_H

#include <Qt>
#include <QXmlStreamWriter>
#include <qvector.h>
#include <qquaternion.h>
#include "../scenegraph/scenemanager.h"
#include "../scenegraph/scenenodes.h"
#include <Qt3DCore/QEntity>

#include "keyframes.h"
#include <QQuaternion>
#include <QVector3D>
#include <qdebug.h>
#include <QDir>

#include "../materials/materialproxy.h"


class SceneManager;
class QXmlStreamWriter;
class SceneNode;

class SceneParser
{
    //holds the directory for the file being saved or loaded
    //used for creating relative file paths for assets upon saving scene
    //used for creating absolute file paths for assets upon loading scene
    QDir dir;
public:

    //returns QDir containing filename's parent folder
    static QDir getDirFromFileName(QString filename);

    //gets relative path for filename
    //assumes dir has already been assigned a value from saveScene or loadScene
    //should be called inside a SceneNode's writeData function
    //if path is resource, return original path
    QString getRelativePath(QString filename);

    //gets absolute path for filename
    //assumes dir has already been assigned a value from saveScene or loadScene
    //should be called inside a SceneNode's readData function
    //returns original string if filepath is a resource
    //returns null string if path doesnt exist
    QString getAbsolutePath(QString filename);

    /* WRITE SCENE TO FILe */
    void saveScene(QString fileName,SceneManager* scene);
    //writing functions
    void writeScene(QXmlStreamWriter& xml,SceneManager* scene);

    void writeSceneData(QXmlStreamWriter& xml,SceneManager* scene);

    void writeNodes(QXmlStreamWriter& xml,SceneNode* node);

    void writeNode(QXmlStreamWriter& xml,SceneNode* node);

    void writeNodeData(QXmlStreamWriter& xml,SceneNode* node);

    void writeTransform(QXmlStreamWriter& xml,SceneNode* node);

    void writeKeyFrameAnimation(QXmlStreamWriter& xml,SceneNode* node);

    void writeVector3KeyFrame(QString name,QXmlStreamWriter& xml,Vector3DKeyFrame* frame);

    void writeQuaternionKeyFrame(QString name,QXmlStreamWriter& xml,QuaternionKeyFrame* frame);

    void writeVector2KeyFrame(QString name,QXmlStreamWriter& xml,Vector2DKeyFrame* frame);

    void writeFloatKeyFrame(QString name,QXmlStreamWriter& xml,FloatKeyFrame* frame);

    void writeIntKeyFrame(QString name,QXmlStreamWriter& xml,IntKeyFrame* frame);

    void writeStringKeyFrame(QString name,QXmlStreamWriter& xml,StringKeyFrame* frame);

    void writeMaterial(QXmlStreamWriter& xml,SceneNode* node);

    void writeColorElement(QString name,QXmlStreamWriter& xml,const QColor& val);

    void writeFloatElement(QString name,QXmlStreamWriter& xml,const float val);

    void writeStringElement(QString name,QXmlStreamWriter& xml,const QString val);

    QString getMaterialName(MaterialType type);

    QString getScenNodeTypeName(SceneNodeType type);


    /* READ SCENE FROM FILE */
    SceneManager* loadScene(QString fileName,Qt3DCore::QEntity* rootEntity);

    //NOTE: ALL READ OPS SHOULD END ON ENDELEMENT - do not read after
    SceneManager* readScene(QXmlStreamReader& xml,Qt3DCore::QEntity* rootEntity);

    SceneNode* readNode(QXmlStreamReader& xml);

    void readTransform(QXmlStreamReader& xml,SceneNode* node);

    Vector3DKeyFrame* readVector3KeyFrame(QString frameName,QXmlStreamReader& xml);

    Vector2DKeyFrame* readVector2KeyFrame(QString frameName,QXmlStreamReader& xml);

    FloatKeyFrame* readFloatKeyFrame(QString frameName,QXmlStreamReader& xml);

    QuaternionKeyFrame* readRotKeyFrame(QString frameName,QXmlStreamReader& xml);

    void readTransformAnimation(QXmlStreamReader& xml,SceneNode* node);

    void readMaterial(QXmlStreamReader& xml,SceneNode* node);

    QColor readColorElement(QXmlStreamReader& xml);

    float readFloatElement(QXmlStreamReader& xml);

    QString readStringElement(QXmlStreamReader& xml);

    //returns MaterialType::Default if a name cant be evaluated
    MaterialType getMaterialType(QString typeName);

    SceneNode* createSceneNodeFromTypeName(QString typeName,QString name);
};


#endif // SCENEPARSER_H
