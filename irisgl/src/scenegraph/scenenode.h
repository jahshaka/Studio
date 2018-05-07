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

class Property;
class Animation;
class PropertyAnim;
typedef QSharedPointer<Animation> AnimationPtr;

class SceneNode : public QEnableSharedFromThis<SceneNode>
{
protected:
    QList<AnimationPtr> animations;
    AnimationPtr animation;

    QVector3D pos;
    QVector3D scale;
    QQuaternion rot;

    bool transformDirty;
    bool hasDirtyChildren;
public:
    // cached local and global transform
    QMatrix4x4 localTransform;
    QMatrix4x4 globalTransform;

    SceneNodeType sceneNodeType;

    QString name;
    long nodeId;

    ScenePtr scene;
    SceneNodePtr parent;
    QList<SceneNodePtr> children;

    // editor specific
    bool duplicable;
	bool exportable;
    bool visible;
    bool removable;
    bool isBuiltIn;

    bool pickable;
    bool castShadow;

	mutable QString guid;

    friend class Renderer;
    friend class Scene;

    // If a node is attached to parents then it inherits animations
    // It also cant have its own animation
    // Attached nodes are usually nodes created from model files
    // The scenenode is a part of the heirarchy of the model's structure
    // so its attached to the root node of the model
    bool attached;

public:
    SceneNode();
	SceneNode(const SceneNode&);
    virtual ~SceneNode() {}

    static SceneNodePtr create();

    void setName(QString name);
    QString getName();

    long getNodeId();

	void setGUID(const QString &id) const {
		guid = id;
	}

	const QString getGUID() const {
		return guid;
	}

    bool isDuplicable() {
        return duplicable;
    }

	bool isExportable() {
		return exportable;
	}

    QVector3D getLocalPos() {
        return pos;
    }

    QQuaternion getLocalRot() {
        return rot;
    }

    QVector3D getLocalScale() {
        return scale;
    }

	void rotate(QQuaternion rot, bool global = false);
    
    bool hasChildren() {
        return !children.empty();
    }

    void setLocalPos(QVector3D pos);
    void setLocalRot(QQuaternion rot);
    void setLocalScale(QVector3D scale);

    void setLocalTransform(QMatrix4x4 transformMatrix);

    void setTransformDirty();
    void setHasDirtyChildren();

    bool isAttached();
    void setAttached(bool attached);

    void addAnimation(AnimationPtr anim);
    QList<AnimationPtr> getAnimations();
    void setAnimation(AnimationPtr anim);
    AnimationPtr getAnimation();
    bool hasActiveAnimation();
    void deleteAnimation(int index);
    void deleteAnimation(AnimationPtr anim);

    virtual QList<Property*> getProperties();
    virtual QVariant getPropertyValue(QString valueName);

    /*
    * This function should return an exact copy of this node
    * with a few exceptions:
    * 1) The duplicate shouldnt have a parent node or be added to a scene
    */
   virtual SceneNodePtr createDuplicate() {
       //qt_assert((QString("This node isnt duplicable: ") + name).toStdString().c_str(),__FILE__,__LINE__);
	   return SceneNodePtr(new SceneNode());
   }

   SceneNodePtr duplicate();

    bool isVisible() {
        return visible;
    }

	void setVisible(bool flag = true) {
		visible = flag;
	}

    void show(bool hideChildren = false) {
        visible = true;
		if (hideChildren) {
			for (auto child : children)
				child->show(hideChildren);
		}
    }

    void hide(bool hideChildren = false) {
        visible = false;
		if (hideChildren) {
			for (auto child : children)
				child->hide(hideChildren);
		}
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

    void setShadowCastingEnabled(bool val) {
        castShadow = val;
    }

    bool getShadowCastingEnabled() {
        return castShadow;
    }

    SceneNodeType getSceneNodeType();
    /**
     * @brief addChild
     * @param node
     * @param keepTransform keeps visual transform
     */
    void addChild(SceneNodePtr node, bool keepTransform = true);
    void insertChild(int position, SceneNodePtr node, bool keepTransform = true);
    void removeFromParent();
    void removeChild(SceneNodePtr node);

    bool isRootNode();

	QQuaternion getGlobalRotation();
    QVector3D getGlobalPosition();
    QMatrix4x4 getGlobalTransform();
    QMatrix4x4 getLocalTransform();

	void setGlobalPos(QVector3D pos);
	void setGlobalRot(QQuaternion rot);
	void setGlobalTransform(QMatrix4x4 transform);

    /*
     * This function does multiple things:
     * - Calculates the transformation of the objects
     * - Particle systems use this to update animations
     */
    virtual void update(float dt);
    virtual void updateAnimation(float time);
    void applyDefaultPose();
    void applyAnimationPose(SceneNodePtr node, QMap<QString, QMatrix4x4> skeletonSpaceMatrices);

    /*
     * This is the function used to add render items
     * to the render queues
     * Called from inside the update(float) function
     */
    virtual void submitRenderItems(){}

private:
    void setParent(SceneNodePtr node);
    void setScene(ScenePtr scene);
    void removeFromScene();

    static long generateNodeId();
    static long nextId;
};

}

Q_DECLARE_METATYPE(iris::SceneNode)
Q_DECLARE_METATYPE(iris::SceneNodePtr)
#endif // SCENENODE_H
