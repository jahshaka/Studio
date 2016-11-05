#ifndef LIGHTNODE_H
#define LIGHTNODE_H

#include "QColor"
#include <QSharedPointer>
#include "../core/scenenode.h"

namespace jah3d
{

class LightNode;
typedef QSharedPointer<LightNode> LightNodePtr;

enum class LightType:int
{
    Point = 0,
    Directional = 1,
    Spot = 2,
};

class LightNode:public SceneNode,public QEnableSharedFromThis<LightNode>
{

    QVector3D lightDir;
public:

    LightType lightType;
    float radius;
    QColor color;
    float intensity;

    static LightNodePtr create()
    {
        return QSharedPointer<LightNode>(new LightNode());
    }

    void setLightType(LightType type)
    {
        this->lightType = type;
    }

    LightType getLightType()
    {
        return lightType;
    }

    QVector3D getLightDir()
    {
        QVector4D defaultDir(0,-1,0,0);

        QVector4D dir = (globalTransform * defaultDir);

        return dir.toVector3D();

    }

private:
    LightNode()
    {
        this->sceneNodeType = SceneNodeType::Light;

        lightType = LightType::Point;

        radius = 5;
        color = QColor(255,255,255);
        intensity = 1;
    }


};


}

#endif // LIGHTNODE_H
