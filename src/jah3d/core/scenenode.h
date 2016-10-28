#ifndef SCENENODE_H
#define SCENENODE_H

#include <QSharedPointer>
#include "scene.h"

namespace jah3d
{

typedef QSharedPointer<SceneNode> SceneNodePtr;

enum SceneNodeType
{
    Empty,
    Mesh,
    Light,
    Camera
};

class SceneNode
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

public:
    SceneNode():
        pos(QVector3D(0)),
        scale(QVector3D(0)),
        rot(QQuaternion)

    {
        sceneNodeType = SceneNodeType::Empty;
    }

    static SceneNodePtr create()
    {
        return QSharedPointer(new SceneNode());
    }

    QString getName()
    {
        return name;
    }

    SceneNodeType getSceneNodeType()
    {
        return sceneNodeType;
    }

    void addChild(SceneNodePtr node)
    {
        //todo: check if child is already a node
        children.append(node);
        node->setParent(this);
        node->setScene(this->scene);
        scene->addNode(node);
    }

    void removeFromParent()
    {
        if(!parent.isNull())
            this->parent->removeChild(this);
    }

    void removeChild(SceneNodePtr node)
    {
        children.removeOne(node);
        node->parent = QSharedPointer<SceneNode>(nullptr);
        node->scene = QSharedPointer<Scene>(nullptr);
        scene->removeNode(node);
    }

private:
    void setParent(SceneNodePtr node)
    {
        this->parent = node;
    }

    void setScene(ScenePtr scene)
    {
        this->scene = scene;
    }
};


}

#endif // SCENENODE_H
