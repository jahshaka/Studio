/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENENODE_H
#define SCENENODE_H

#include "../irisglfwd.h"
#include <QQuaternion>
#include <QMatrix4x4>
#include <QVector3D>

namespace iris
{

enum class SceneNodeType {
    Empty,
    ParticleSystem,
    Mesh,
    Light,
    Camera,
    Viewer
};

class Animation;
typedef QSharedPointer<Animation> AnimationPtr;

class SceneNode : public QEnableSharedFromThis<SceneNode>
{
public:
    // cached local and global transform
    QMatrix4x4 localTransform;
    QMatrix4x4 globalTransform;

    QVector3D pos;
    QVector3D scale;
    QQuaternion rot;

    SceneNodeType sceneNodeType;

    QString name;
    long nodeId;

    ScenePtr scene;
    SceneNodePtr parent;
    QList<SceneNodePtr> children;

    AnimationPtr animation;

    // editor specific
    bool duplicable;
    bool visible;
    bool removable;

    bool pickable;
    bool shadowEnabled;

    friend class Renderer;
    friend class Scene;

public:
    SceneNode();
    virtual ~SceneNode() {}

    static SceneNodePtr create();

    void setName(QString name);
    QString getName();

    long getNodeId();

    bool isDuplicable() {
        return duplicable;
    }

    /*
    * This function should return an exact copy of this node
    * with a few exceptions:
    * 1) The duplicate shouldnt have a parent node or be added to a scene
    */
   virtual SceneNodePtr createDuplicate(){
       qt_assert((QString("This node isnt duplicable: ") + name).toStdString().c_str(),__FILE__,__LINE__);
   }

   SceneNodePtr duplicate();

    bool isVisible() {
        return visible;
    }

    void show() {
        visible = true;
    }

    void hide() {
        visible = false;
    }

    bool isRemovable() {
        return removable;
    }

    void setPickable(bool canPick) {
        pickable = canPick;
    }

    bool isPickable() {
        return pickable;
    }

    void setShadowEnabled(bool val) {
        shadowEnabled = val;
    }

    bool getShadowEnabled() {
        return shadowEnabled;
    }

    SceneNodeType getSceneNodeType();
    /**
     * @brief addChild
     * @param node
     * @param keepTransform keeps visual transform
     */
    void addChild(SceneNodePtr node, bool keepTransform = true);
    void removeFromParent();
    void removeChild(SceneNodePtr node);

    bool isRootNode();

    QVector3D getGlobalPosition();
    QMatrix4x4 getGlobalTransform();
    QMatrix4x4 getLocalTransform();

    /*
     * This function does multiple things:
     * - Calculates the transformation of the objects
     * - Particle systems use this to update animations
     */
    virtual void update(float dt);
    virtual void updateAnimation(float time);

    /*
     * This is the function used to add render items
     * to the render queues
     * Called from inside the update(float) function
     */
    virtual void submitRenderItems(){}

private:
    void setParent(SceneNodePtr node);
    void setScene(ScenePtr scene);

    static long generateNodeId();
    static long nextId;
};


}

#endif // SCENENODE_H
