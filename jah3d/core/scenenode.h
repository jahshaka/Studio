#ifndef SCENENODE_H
#define SCENENODE_H

#include <QSharedPointer>

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

    SceneNodeType getSceneNodeType()
    {
        return sceneNodeType;
    }
};


}

#endif // SCENENODE_H
