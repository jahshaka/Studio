#ifndef LIGHTNODE_H
#define LIGHTNODE_H

#include "QColor"
#include <QSharedPointer>
#include "../core/scenenode.h"

namespace jah3d
{

class LightNode;
typedef QSharedPointer<LightNode> LightNodePtr;

enum LightType:int
{
    Point = 1,
    Spot = 2,
    Directional = 3
};

class LightNode:public SceneNode
{
    LightType lightType;
    QVector3D lightDir;
public:

    float radius;
    QColor color;
    float strength;

    static LightNodePtr create();

    void setLightType(LightType type);
    LightType getLightType();

private:
    LightNode()
    {
        this->sceneNodeType = SceneNodeType::Light;

        lightType = LightType::Point;

        radius = 5;
        color = QColor(255,255,255);
        strength = 1;
    }
};


}

#endif // LIGHTNODE_H
