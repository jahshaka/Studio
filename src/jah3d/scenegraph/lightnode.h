#ifndef LIGHTNODE_H
#define LIGHTNODE_H

#include "QColor"
#include <QSharedPointer>
#include "../core/scenenode.h"

namespace jah3d
{

class Texture2D;

class LightNode;
typedef QSharedPointer<LightNode> LightNodePtr;

enum class LightType:int
{
    Point = 0,
    Directional = 1,
    Spot = 2,
};

class LightNode:public SceneNode
{

    QVector3D lightDir;
public:

    LightType lightType;

    /**
     * light's radius. This is only used for pointlights.
     */
    float radius;
    QColor color;
    float intensity;

    /**
     * Spotlight cutoff angle in degrees.
     * This parameter is only used if the light is a spotlight
     */
    float spotCutOff;

    //editor-specific
    QSharedPointer<Texture2D> icon;
    float iconSize;

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
        intensity = 0.2f;
        spotCutOff = 30.0f;

        iconSize = 0.5f;
    }


};


}

#endif // LIGHTNODE_H
