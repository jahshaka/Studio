/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MESHNODE_H
#define MESHNODE_H

#include "../irisglfwd.h"
#include "../core/scenenode.h"
#include "../core/irisutils.h"
#include "../graphics/texture2d.h"

namespace iris
{

class MeshNode : public SceneNode
{
public:
    Mesh* mesh;

    QString meshPath;

    /**
     * This holds the index of the mesh
     */
    int meshIndex;

    MaterialPtr material;

    static MeshNodePtr create() {
        return MeshNodePtr(new MeshNode());
    }

    /**
     * Some model contains multiple meshes with child-parent relationships. This funtion Loads the model as a scene
     * itself. If only one mesh is in the scene, it returns it as an MeshNode rather than a SceneNode. Otherwise, it
     * returns a null shared pointer.
     * @param path
     * @return
     */
    static SceneNodePtr loadAsSceneFragment(QString path);

    void setMesh(QString source);
    void setMesh(Mesh* mesh);

    Mesh* getMesh();

    void setMaterial(MaterialPtr material);

    MaterialPtr getMaterial() {
        return material;
    }

    // not needed because this guy likes public members...
    void setNodeType(SceneNodeType type) {
        sceneNodeType = type;
    }

    // temporary boy...
    bool isEmitter;
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
    MeshNode() : m_index(index++) {
        mesh = nullptr;
        sceneNodeType = SceneNodeType::Mesh;

        texture = iris::Texture2D::load(
                    IrisUtils::getAbsoluteAssetPath("assets/textures/default_particle.jpg")
        );
        pps = 24;
        speed = 12;
        gravity = .0f;
        particleLife = 5;

        scaleFac = 0.1f;
        lifeFac = 0.0f;
        speedFac = 0.0f;

        useAdditive = false;
        randomRotation = true;
        dissipate = false;
    }
};

}

#endif // MESHNODE_H
