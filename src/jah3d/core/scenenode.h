#ifndef SCENENODE_H
#define SCENENODE_H

#include <QSharedPointer>
//#include "scene.h"
#include <QQuaternion>
#include <QMatrix4x4>
#include <QVector3D>

namespace jah3d
{

class Scene;
class SceneNode;
typedef QSharedPointer<SceneNode> SceneNodePtr;

enum class SceneNodeType
{
    Empty,
    Mesh,
    Light,
    Camera
};

class SceneNode:public QEnableSharedFromThis<SceneNode>
{
public:
    //cached local and global transform
    QMatrix4x4 localTransform;
    QMatrix4x4 globalTransform;

    QVector3D pos;
    QVector3D scale;
    QQuaternion rot;

    SceneNodeType sceneNodeType;

    QString name;

    QSharedPointer<Scene> scene;
    SceneNodePtr parent;
    QList<SceneNodePtr> children;

    friend class Renderer;
    friend class Scene;


    SceneNode();

    static SceneNodePtr create();
    QString getName();

    SceneNodeType getSceneNodeType();
    void addChild(SceneNodePtr node);
    void removeFromParent();
    void removeChild(SceneNodePtr node);

    void update(float dt);

private:
    void setParent(SceneNodePtr node);
    void setScene(QSharedPointer<Scene> scene);
};


}

#endif // SCENENODE_H
