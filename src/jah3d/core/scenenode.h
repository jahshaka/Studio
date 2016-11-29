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
    long nodeId;

    QSharedPointer<Scene> scene;
    SceneNodePtr parent;
    QList<SceneNodePtr> children;

    //editor specific
    bool duplicable;
    bool visible;
    bool removable;

    friend class Renderer;
    friend class Scene;

public:
    SceneNode();
    virtual ~SceneNode()
    {

    }

    static SceneNodePtr create();

    void setName(QString name);
    QString getName();

    long getNodeId();

    bool isDuplicable()
    {
        return duplicable;
    }

    bool isVisible()
    {
        return visible;
    }

    void show()
    {
        visible = true;
    }

    void hide()
    {
        visible = false;
    }

    bool isRemovable()
    {
        return removable;
    }

    SceneNodeType getSceneNodeType();
    void addChild(SceneNodePtr node);
    void removeFromParent();
    void removeChild(SceneNodePtr node);

    bool isRootNode();

    QVector3D getGlobalPosition();

    virtual void update(float dt);

private:
    void setParent(SceneNodePtr node);
    void setScene(QSharedPointer<Scene> scene);

    static long generateNodeId();
    static long nextId;
};


}

#endif // SCENENODE_H
