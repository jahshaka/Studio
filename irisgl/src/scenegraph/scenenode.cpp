/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scenenode.h"

#include <functional>

#include "animation/animation.h"
#include "animation/animableproperty.h"
#include "animation/keyframeanimation.h"
#include "animation/keyframeset.h"
#include "animation/propertyanim.h"
#include "animation/skeletalanimation.h"
#include "core/property.h"
#include "graphics/mesh.h"
#include "graphics/skeleton.h"
#include "math/mathhelper.h"
#include "scene.h"
#include "scenegraph/meshnode.h"

#include <QUuid>

namespace iris
{

SceneNode::SceneNode():
    pos(QVector3D(0,0,0)),
    scale(QVector3D(1,1,1)),
    rot(QQuaternion())

{
    sceneNodeType = SceneNodeType::Empty;
    nodeId = generateNodeId();
    setName(QString("SceneNode%1").arg(nodeId));

    visible = true;
    duplicable = true;
    removable = true;
	exportable = true;
    isBuiltIn = false;
	isPhysicsBody = false;

    pickable = true;
    castShadow = true;

    localTransform.setToIdentity();
    globalTransform.setToIdentity();

    attached = false;

    transformDirty = true;
    hasDirtyChildren = true;

    //keyFrameSet = KeyFrameSet::create();
    //animation = iris::Animation::create("");
}

SceneNodePtr SceneNode::create()
{
    return QSharedPointer<SceneNode>(new SceneNode());
}

QString SceneNode::getName()
{
    return name;
}

void SceneNode::setName(QString name)
{
    this->name = name;
}

long SceneNode::getNodeId()
{
    return nodeId;
}

void SceneNode::rotate(QQuaternion rot, bool global)
{
	if (global)
		this->rot = this->rot * rot;
	else
		this->rot = rot * this->rot;
	setTransformDirty();
}

void SceneNode::setLocalPos(QVector3D pos)
{
    this->pos = pos;
    setTransformDirty();
}

void SceneNode::setLocalRot(QQuaternion rot)
{
    this->rot = rot;
    setTransformDirty();
}

void SceneNode::setLocalScale(QVector3D scale)
{
    this->scale = scale;
    setTransformDirty();
}

void SceneNode::setLocalTransform(QMatrix4x4 transformMatrix)
{
    MathHelper::decomposeMatrix(transformMatrix, pos, rot, scale);
    setTransformDirty();
}

void SceneNode::setTransformDirty()
{
    transformDirty = true;
    if (!!parent)
    {
        parent->setHasDirtyChildren();
    }
}

void SceneNode::setHasDirtyChildren()
{
    hasDirtyChildren = true;
    if (!!parent)
    {
        parent->setHasDirtyChildren();
    }
}

bool SceneNode::isAttached()
{
    return attached;
}

void SceneNode::setAttached(bool attached)
{
    this->attached = attached;
}

void SceneNode::addAnimation(AnimationPtr anim)
{
    animations.append(anim);
}

QList<AnimationPtr> SceneNode::getAnimations()
{
    return animations;
}

void SceneNode::setAnimation(AnimationPtr anim)
{
    animation = anim;
}

AnimationPtr SceneNode::getAnimation()
{
    return animation;
}

bool SceneNode::hasActiveAnimation()
{
    return !!animation;
}

void SceneNode::deleteAnimation(int index)
{
    animations.removeAt(index);
}

void SceneNode::deleteAnimation(AnimationPtr anim)
{
    animations.removeOne(anim);
}

QList<Property*> SceneNode::getProperties()
{
    auto props = QList<Property*>();

    auto prop = new Vec3Property();
    prop->displayName = "Position";
    prop->name = "position";
    prop->value = pos;
    props.append(prop);

    prop = new Vec3Property();
    prop->displayName = "Rotation";
    prop->name = "rotation";
    prop->value = rot.toEulerAngles();
    props.append(prop);

    prop = new Vec3Property();
    prop->displayName = "Scale";
    prop->name = "scale";
    prop->value = scale;
    props.append(prop);

    return props;
}

QVariant SceneNode::getPropertyValue(QString valueName)
{
    if (valueName == "position") return pos;
    if (valueName == "rotation") return rot.toEulerAngles();
    if (valueName == "scale")	 return scale;

    return QVariant();
}

SceneNodeType SceneNode::getSceneNodeType()
{
    return sceneNodeType;
}

void SceneNode::addChild(SceneNodePtr node, bool keepTransform)
{
    insertChild(children.size(), node, keepTransform);
}

void SceneNode::insertChild(int position, SceneNodePtr node, bool keepTransform)
{
    auto initialGlobalTransform = node->getGlobalTransform();

    if (!!node->parent) {
        node->removeFromParent();
    }

    // @TODO: check if child is already a node
    auto self = sharedFromThis();

    children.insert(position, node);
    node->setParent(self);
    if (!!scene) {
        node->setScene(self->scene);
        //scene->addNode(node);
    }

    if (keepTransform) {
        // @TODO: ensure global transform is calculated
        // this->update(0);///shortcut for now
        auto thisGlobalTransform = this->getGlobalTransform();

        //auto diff = initialGlobalTransform * thisGlobalTransform.inverted();
        auto diff = thisGlobalTransform.inverted() * initialGlobalTransform;

        auto pos = diff.column(3).toVector3D();
        node->pos = pos;
        node->rot = QQuaternion::fromRotationMatrix(diff.normalMatrix()).normalized();

        auto data = diff.data();

        // extracts the scale from the transform
        //node->scale = QVector3D(data[0], data[5], data[10]);
        node->scale.setX(diff.column(0).toVector3D().length());
        node->scale.setY(diff.column(1).toVector3D().length());
        node->scale.setZ(diff.column(2).toVector3D().length());
    }
}

void SceneNode::removeFromParent()
{
    auto self = sharedFromThis();

    if (!parent.isNull()) this->parent->removeChild(self);
}

void SceneNode::removeChild(SceneNodePtr node)
{
    children.removeOne(node);
    node->parent = QSharedPointer<SceneNode>(Q_NULLPTR);
    node->removeFromScene();
}

bool SceneNode::isRootNode()
{
    if (this->scene->getRootNode().data() == this) return true;
    return false;
}

void SceneNode::updateAnimation(float time)
{
    if (!!animation) {

        time = animation->getSampleTime(time);
        if (animation->hasPropertyAnim("position")) {
            pos = animation->getVector3PropertyAnim("position")->getValue(time);
        }
        if (animation->hasPropertyAnim("rotation")) {
            auto r = animation->getVector3PropertyAnim("rotation")->getValue(time);
            rot = QQuaternion::fromEulerAngles(r);
        }
        if (animation->hasPropertyAnim("scale")) {
            scale = animation->getVector3PropertyAnim("scale")->getValue(time);
        }

        if (animation->hasSkeletalAnimation()) {
            QMap<QString, QMatrix4x4> skeletonSpaceMatrices;
            // The skeleton begins at this node, the root

            // recursively update the animation for each node
            std::function<void(SkeletalAnimationPtr anim, SceneNodePtr node, QMatrix4x4 parentTransform)> animateHierarchy;
            animateHierarchy = [&animateHierarchy, time, &skeletonSpaceMatrices](SkeletalAnimationPtr anim, SceneNodePtr node, QMatrix4x4 parentTransform)
            {
                // skeleton-space transform of current node
                QMatrix4x4 skelTrans;
                skelTrans.setToIdentity();

                if (anim->boneAnimations.contains(node->name)) {
                    auto boneAnim = anim->boneAnimations[node->name];

                    node->pos = boneAnim->posKeys->getValueAt(time);
                    node->rot = boneAnim->rotKeys->getValueAt(time).normalized();
                    //node->scale = QVector3D(1,1,1);
                    node->scale = boneAnim->scaleKeys->getValueAt(time);
                }

                auto localTrans = node->getLocalTransform(); //calculates the local transform matrix
                skelTrans = parentTransform * localTrans; //skeleton space transform
                skeletonSpaceMatrices.insert(node->name, skelTrans);

                for(auto child : node->children) {
                    animateHierarchy(anim, child, skelTrans);
                }
            };

            QMatrix4x4 rootTransform;
            //rootTransform.setToIdentity();
            rootTransform = this->getLocalTransform();
            animateHierarchy(animation->getSkeletalAnimation(), this->sharedFromThis(), rootTransform);

            applyAnimationPose(this->sharedFromThis(), skeletonSpaceMatrices);
        }
    }

    for (auto child : children) {
        child->updateAnimation(time);
    }
}

void SceneNode::applyDefaultPose()
{
    if (!!animation) {
    if (animation->hasSkeletalAnimation()) {
        QMap<QString, QMatrix4x4> skeletonSpaceMatrices;
        // The skeleton begins at this node, the root

        // recursively update the animation for each node
        std::function<void(SkeletalAnimationPtr anim, SceneNodePtr node, QMatrix4x4 parentTransform)> animateHierarchy;
        animateHierarchy = [&animateHierarchy, &skeletonSpaceMatrices](SkeletalAnimationPtr anim, SceneNodePtr node, QMatrix4x4 parentTransform)
        {
            // skeleton-space transform of current node
            QMatrix4x4 skelTrans;
            skelTrans.setToIdentity();

            auto localTrans = node->getLocalTransform(); //calculates the local transform matrix
            skelTrans = parentTransform * localTrans; //skeleton space transform
            skeletonSpaceMatrices.insert(node->name, skelTrans);

            for(auto child : node->children) {
                animateHierarchy(anim, child, skelTrans);
            }
        };

        QMatrix4x4 rootTransform;
        rootTransform.setToIdentity();
        rootTransform = this->getLocalTransform();
        animateHierarchy(animation->getSkeletalAnimation(), this->sharedFromThis(), rootTransform);

        applyAnimationPose(this->sharedFromThis(), skeletonSpaceMatrices);
    }
    }

    for (auto child : children) {
        child->applyDefaultPose();
    }
}

void SceneNode::applyAnimationPose(SceneNodePtr node, QMap<QString, QMatrix4x4> skeletonSpaceMatrices)
{
    if (skeletonSpaceMatrices.contains(node->name)) {
        if (node->sceneNodeType == SceneNodeType::Mesh) {
            auto meshNode = node.staticCast<MeshNode>();
            auto mesh = meshNode->getMesh();
            if (mesh != nullptr && mesh->hasSkeleton()) {
                auto inverseMeshMatrix = skeletonSpaceMatrices[node->name].inverted();
                mesh->getSkeleton()->applyAnimation(inverseMeshMatrix, skeletonSpaceMatrices);
            }
        }
    }

    for (auto child : node->children) {
        applyAnimationPose(child, skeletonSpaceMatrices);
    }
}

void SceneNode::update(float dt)
{
    if (transformDirty) {
        localTransform.setToIdentity();

        localTransform.translate(pos);
        localTransform.rotate(rot);
        localTransform.scale(scale);

        if (!!parent) {
            globalTransform = this->parent->globalTransform * localTransform;
        } else {
            globalTransform = localTransform;
        }
    }

    if (hasDirtyChildren) {
        for (auto child : children) {
            child->update(dt);
        }
    }
}

void SceneNode::setParent(SceneNodePtr node)
{
    this->parent = node;
}

void SceneNode::setScene(ScenePtr scene)
{
    // should not already be a part of scene
    Q_ASSERT(!this->scene);

    this->scene = scene;
    this->scene->addNode(this->sharedFromThis());

    // add children
    for (auto &child : children) {
        child->setScene(scene);
    }
}

void SceneNode::removeFromScene()
{
    // should already have a scene to be removed from
    Q_ASSERT(!!this->scene);

    this->scene->removeNode(this->sharedFromThis());
    this->scene.clear();

    // add children
    for (auto &child : children) {
        child->removeFromScene();
    }
}

long SceneNode::generateNodeId()
{
    return nextId++;
}

QQuaternion SceneNode::getGlobalRotation()
{
	if (!!parent) return parent->getGlobalRotation() * rot;
	return rot;
}

QVector3D SceneNode::getGlobalPosition()
{
    return globalTransform.column(3).toVector3D();
}

QMatrix4x4 SceneNode::getGlobalTransform()
{
    localTransform.setToIdentity();

    localTransform.translate(pos);
    localTransform.rotate(rot);
    localTransform.scale(scale);

    if (parent.isNull()) {
        // this is a check for the root node
        globalTransform = localTransform;
    } else {
        globalTransform = parent->getGlobalTransform() * localTransform;
    }

    return globalTransform;
}

QMatrix4x4 SceneNode::getLocalTransform()
{
    localTransform.setToIdentity();

    localTransform.translate(pos);
    localTransform.rotate(rot);
    localTransform.scale(scale);

    return localTransform;
}

void SceneNode::setGlobalPos(QVector3D pos)
{
	if (!parent) {
		this->pos = pos;
		return;
	}

	auto globInv = this->parent->getGlobalTransform().inverted();

	auto res = globInv * pos;

	this->pos = res;
	this->setTransformDirty();
}

void SceneNode::setGlobalRot(QQuaternion rot)
{
	if (!parent) {
		this->rot = rot;
		return;
	}

	auto globInv = this->parent->getGlobalRotation().inverted();
	auto res = globInv * rot;

	this->rot = res;
	this->setTransformDirty();
}

void SceneNode::setGlobalTransform(QMatrix4x4 transform)
{
	if (!parent) {
		this->setLocalTransform(transform);
		return;
	}

	auto globInv = this->parent->getGlobalTransform().inverted();
	auto res = globInv * transform;
	this->setLocalTransform(res);

	this->setTransformDirty();
}


SceneNodePtr SceneNode::duplicate()
{
    if (!duplicable) return SceneNodePtr();

    auto node = this->createDuplicate();

    node->setName(this->getName());
    node->setLocalPos(this->pos);
    node->setLocalScale(this->scale);
    node->setLocalRot(this->rot);
	node->castShadow	= this->castShadow;
	node->duplicable	= this->duplicable;
	node->visible		= this->visible;
	node->removable		= this->removable;
	node->pickable		= this->pickable;
	node->castShadow	= this->castShadow;
	node->attached		= this->attached;

    auto id = QUuid::createUuid();
    auto guid = id.toString().remove(0, 1);
    guid.chop(1);
    node->setGUID(guid);

    for (auto &child : this->children) {
        if (child->isDuplicable()) {
            node->addChild(child->duplicate(), false);
        }
    }

    return node.staticCast<SceneNode>();
}

long SceneNode::nextId = 0;

}
