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
#include "../geometry/frustum.h"

namespace iris
{

class RenderItem;
class RenderList;
class Environment;

enum class SceneRenderFlags : int
{
    Vr = 0x1
};

struct PickingResult
{
    iris::SceneNodePtr hitNode;
    QVector3D hitPoint;

    float distanceFromStartSqrd;
};

class Scene: public QEnableSharedFromThis<Scene>
{
    QSharedPointer<Environment> environment;

public:
    CameraNodePtr camera;
    SceneNodePtr rootNode;

    QSharedPointer<Environment> getPhysicsEnvironment() {
        return environment;
    }

    /*
     * This is the default viewer that the scene
     * will use when playing in vr mode
     */
    ViewerNodePtr vrViewer;

    QList<LightNodePtr> lights;
    QList<MeshNodePtr> meshes;
    QList<ParticleSystemNodePtr> particleSystems;
    QList<ViewerNodePtr> viewers;
	QList<GrabNodePtr> grabbers;

    QColor clearColor;
    bool renderSky;
    MeshPtr skyMesh;
    Texture2DPtr skyTexture;
    QColor skyColor;
    QColor ambientColor;
    DefaultSkyMaterialPtr skyMaterial;
    RenderItem* skyRenderItem;

    // fog properties
    QColor fogColor;
    float fogStart;
    float fogEnd;
    bool fogEnabled;

    float gravity;
    bool shadowEnabled;

    RenderList* geometryRenderList;
    RenderList* shadowRenderList;
    RenderList* gizmoRenderList;// for gizmos and lines

    QString skyBoxTextures[6];

    /*
     * customizations that can be passed in and applied to a scene. ideally these
     * should or can be GLOBAL but a scene is the highest prioritized obj atm...
     * @future maybe have a __GlobalWorldSettings__ object?
     * @future todo could include camera speed, motion blur px, clipping (near/far plane) pos
     */
    int outlineWidth;
    QColor outlineColor;

	// time counter to pass to shaders that do time-based animation
	float time;

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
    void setSkyTextureSource(QString src) {
        skyTexture->source = src;
    }

	float getRunningTime()
	{
		return time;
	}

    QString getSkyTextureSource();
    void clearSkyTexture();
    void setSkyColor(QColor color);
    void setAmbientColor(QColor color);

    void updateSceneAnimation(float time);
    void update(float dt);
    void render();

    void rayCast(const QVector3D& segStart,
                 const QVector3D& segEnd,
                 QList<PickingResult>& hitList);

    void rayCast(const QSharedPointer<iris::SceneNode>& sceneNode,
                 const QVector3D& segStart,
                 const QVector3D& segEnd,
                 QList<iris::PickingResult>& hitList);

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

    void cleanup();
};

}


#endif // SCENE_H
