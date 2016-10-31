#ifndef SCENENODE_H
#define SCENENODE_H

#include <QSharedPointer>
#include "scene.h"
#include <QQuaternion>
#include <QMatrix4x4>
#include <QVector3D>

namespace jah3d
{

class SceneNode;
typedef QSharedPointer<SceneNode> SceneNodePtr;

enum SceneNodeType
{
    Empty,
    Mesh,
    Light,
    Camera
};

class SceneNode:public QEnableSharedFromThis<SceneNode>
{
    //cached local and global transform
    QMatrix4x4 localTransform;
    QMatrix4x4 globalTransform;

    QVector3D pos;
    QVector3D scale;
    QQuaternion rot;

    SceneNodeType sceneNodeType;

    QString name;

    ScenePtr scene;
    SceneNodePtr parent;
    QList<SceneNodePtr> children;

    friend class Renderer;
    friend class Scene;

public:
    SceneNode();

    static SceneNodePtr create();
    QString getName();

    SceneNodeType getSceneNodeType();
    void addChild(SceneNodePtr node);
    void removeFromParent();
    void removeChild(SceneNodePtr node);

private:
    void setParent(SceneNodePtr node);
    void setScene(ScenePtr scene);
};


}

#endif // SCENENODE_H
