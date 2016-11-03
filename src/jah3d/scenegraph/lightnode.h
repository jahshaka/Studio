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
    Point = 1,
    Directional = 2,
    Spot = 3,
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

    void setLightType(LightType type);
    LightType getLightType();

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
