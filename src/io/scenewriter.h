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

class SceneWriter : public AssetIOBase
{
public:
    void writeScene(QString filePath,iris::ScenePtr scene);

private:
    void writeScene(QJsonObject& projectObj,iris::ScenePtr scene);

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
