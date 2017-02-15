/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PARTICLESYSTEMNODE_H
#define PARTICLESYSTEMNODE_H

#include "../irisglfwd.h"
#include "../core/scenenode.h"
#include "../core/irisutils.h"
#include "../graphics/texture2d.h"

namespace iris
{

class RenderItem;

class ParticleSystemNode : public SceneNode
{
public:
    static ParticleSystemNodePtr create() {
        return ParticleSystemNodePtr(new ParticleSystemNode());
    }

    virtual void submitRenderItems() override;

    int m_index;
    static int index;
    float pps;
    float speed;
    float particleLife;
    float gravity;
    QSharedPointer<iris::Texture2D> texture;
    bool dissipate;

    bool randomRotation;

    float lifeFac;
    float scaleFac;
    bool useAdditive;
    float speedFac;

private:
    MaterialPtr material;

    RenderItem* renderItem;
    Mesh* boundsMesh;
    RenderItem* boundsRenderItem;

    ParticleSystemNode();
    ~ParticleSystemNode();
};

}

#endif // PARTICLESYSTEMNODE_H
