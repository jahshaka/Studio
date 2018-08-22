/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTNODE_H
#define LIGHTNODE_H

#include "QColor"
#include "../irisglfwd.h"
#include "../scenegraph/scenenode.h"
#include "../graphics/shadowmap.h"

namespace iris
{

class ShadowMap;

enum class LightType:int
{
    Point = 0,
    Directional = 1,
    Spot = 2,
};

class LightNode:public SceneNode
{

public:
    QVector3D lightDir;

    LightType lightType;

    ShadowMap* shadowMap;

    /**
     * light's radius. This is only used for pointlights.
     */
    float distance;
    QColor color;
    float intensity;

	/*
	Shadow's color and trasnsparency
	*/
	QColor shadowColor;
	float shadowAlpha;

    /**
     * Spotlight cutoff angle in degrees.
     * This parameter is only used if the light is a spotlight
     */
    float spotCutOff;

    /**
     * Spotlight's softness
     * This is added to the spotlight's outer radius to give more
     * smooth cutoff edges
     */
    float spotCutOffSoftness;

    //editor-specific
    QSharedPointer<Texture2D> icon;
    float iconSize;

    static LightNodePtr create()
    {
        return LightNodePtr(new LightNode());
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
        // this is the default rotation for directional and spotlights - pointing down
        QVector4D defaultDir(0, -1, 0, 0);

        QVector4D dir = (globalTransform * defaultDir);

        return dir.toVector3D();
    }

    virtual QList<Property*> getProperties() override;
    virtual QVariant getPropertyValue(QString valueName) override;

    void updateAnimation(float time) override;

	ShadowMap* getShadowMap()
	{
		return shadowMap;
	}

	void setShadowMapType(ShadowMapType shadowType)
	{
		shadowMap->shadowType = shadowType;
	}

	ShadowMapType getShadowMapType()
	{
		return shadowMap->shadowType;
	}

	void setShadowMapResolution(int size)
	{
		shadowMap->setResolution(size);
	}

	int getShadowMapResolution()
	{
		return shadowMap->resolution;
	}

	SceneNodePtr createDuplicate() override;

private:
    LightNode();
};


}

#endif // LIGHTNODE_H
