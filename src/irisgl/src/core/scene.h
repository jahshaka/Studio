/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENE_H
#define SCENE_H

#include <QList>
#include "../irisglfwd.h"
#include "../graphics/texture2d.h"
#include "../materials/defaultskymaterial.h"

namespace iris
{

class Scene: public QEnableSharedFromThis<Scene>
{
public:
    CameraNodePtr camera;
    SceneNodePtr rootNode;

    /*
     * This is the default viewer that the scene
     * will use when playing in vr mode
     */
    ViewerNodePtr vrViewer;

    QList<LightNodePtr> lights;

    Mesh* skyMesh;
    Texture2DPtr skyTexture;
    QColor skyColor;
    QColor ambientColor;

    // should be MaterialPtr
    DefaultSkyMaterialPtr skyMaterial;

    // fog properties
    QColor fogColor;
    float fogStart;
    float fogEnd;
    bool fogEnabled;

    /*
     * customizations that can be passed in and applied to a scene. ideally these
     * should or can be GLOBAL but a scene is the highest prioritized obj atm...
     * @future maybe have a __GlobalWorldSettings__ object?
     * @future todo could include camera speed, motion blur px, clipping (near/far plane) pos
     */
    int outlineWidth;
    QColor outlineColor;

    Scene();
public:
    static ScenePtr create();

    /**
     * Returns the scene's root node. A scene should always have a root node so it should be assumed
     * that the returned value is never null.
     * @return
     */
    SceneNodePtr getRootNode() {
        return rootNode;
    }

    void setSkyTexture(Texture2DPtr tex);
    QString getSkyTextureSource();
    void clearSkyTexture();
    void setSkyColor(QColor color);
    void setAmbientColor(QColor color);

    void updateSceneAnimation(float time);
    void update(float dt);
    void render();

    /**
     * Adds node to scene. If node is a LightNode then it is added to a list of lights.
     * @param node
     */
    void addNode(SceneNodePtr node);

    /**
     *  Removes node from scene. If node is a LightNode then it is removed to a list of lights.
     * @param node
     */
    void removeNode(SceneNodePtr node);

    /**
     * Sets the active camera of the scene
     * @param cameraNode
     */
    void setCamera(CameraNodePtr cameraNode);

    /**
     * Sets the viewport stencil width
     * @param width
     */
    void setOutlineWidth(int width);

    /**
     * Sets the viewport stencil color
     * @param color
     */
    void setOutlineColor(QColor color);
};

}


#endif // SCENE_H
