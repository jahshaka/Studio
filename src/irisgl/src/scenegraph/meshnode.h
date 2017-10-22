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

#include <functional>
#include "../irisglfwd.h"
#include "../scenegraph/scenenode.h"
#include "../core/irisutils.h"
#include "../graphics/texture2d.h"
#include "../graphics/renderitem.h"

namespace iris
{

class RenderItem;
struct MeshMaterialData;

class MeshNode : public SceneNode
{
public:
    MeshPtr mesh;

    QString meshPath;

    /**
     * This holds the index of the mesh
     */
    int meshIndex;

    MaterialPtr material;
    MaterialPtr customMaterial;

    FaceCullingMode faceCullingMode;

    RenderItem* renderItem;

    // For animated meshes, the rootBone's transform is what will be used as its transform
    // Since all its animations are based at the rootBone
    SceneNodePtr rootBone;

    static MeshNodePtr create() {
        return MeshNodePtr(new MeshNode());
    }

    virtual QList<Property*> getProperties() override;

    /**
     * Some model contains multiple meshes with child-parent relationships. This funtion Loads the model as a scene
     * itself. If only one mesh is in the scene, it returns it as an MeshNode rather than a SceneNode. Otherwise, it
     * returns a null shared pointer.
     * @param path
     * @return
     */
    static SceneNodePtr loadAsSceneFragment(QString path, std::function<MaterialPtr(MeshPtr mesh, MeshMaterialData& data)> createMaterialFunc);

    static SceneNodePtr loadAsAnimatedModel(QString path);

    void setMesh(QString source);
    void setMesh(MeshPtr mesh);

    MeshPtr getMesh();

    void setMaterial(MaterialPtr material);
    void setCustomMaterial(MaterialPtr material);

    void setActiveMaterial(int type);

    MaterialPtr getMaterial() {
        return material;
    }

    MaterialPtr getCustomMaterial() {
        return customMaterial;
    }

    // not needed because this guy likes public members...
    // shouldnt be here at all, the value is already set in the constructor...
    void setNodeType(SceneNodeType type) {
        sceneNodeType = type;
    }

    SceneNodePtr createDuplicate() override;
    virtual void submitRenderItems() override;
    float getMeshRadius();
    BoundingSphere getTransformedBoundingSphere();

    //void updateAnimation(float time) override;

    FaceCullingMode getFaceCullingMode() const;
    void setFaceCullingMode(const FaceCullingMode &value);

private:
    MeshNode();
};

}

#endif // MESHNODE_H
