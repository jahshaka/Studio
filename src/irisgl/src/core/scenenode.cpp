/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scenenode.h"
#include "scene.h"
#include "../animation/keyframeset.h"
#include "../animation/animation.h"
#include "../animation/keyframeanimation.h"

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

    pickable = true;
    shadowEnabled = true;

    localTransform.setToIdentity();
    globalTransform.setToIdentity();

    //keyFrameSet = KeyFrameSet::create();
    animation = iris::Animation::create();
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

SceneNodeType SceneNode::getSceneNodeType()
{
    return sceneNodeType;
}

void SceneNode::addChild(SceneNodePtr node, bool keepTransform)
{
    auto initialGlobalTransform = node->getGlobalTransform();

    if (!!node->parent) {
        node->removeFromParent();
    }

    // @TODO: check if child is already a node
    auto self = sharedFromThis();

    children.append(node);
    node->setParent(self);
    node->setScene(self->scene);
    scene->addNode(node);

    if (keepTransform) {
        // @TODO: ensure global transform is calculated
        // this->update(0);///shortcut for now
        auto thisGlobalTransform = this->getGlobalTransform();

        //auto diff = initialGlobalTransform * thisGlobalTransform.inverted();
        auto diff = thisGlobalTransform.inverted() * initialGlobalTransform;

        auto pos = diff.column(3).toVector3D();
        node->pos = pos;
        node->rot = QQuaternion::fromRotationMatrix(diff.normalMatrix());

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

    if(!parent.isNull())
        this->parent->removeChild(self);
}

void SceneNode::removeChild(SceneNodePtr node)
{
    children.removeOne(node);
    node->parent = QSharedPointer<SceneNode>(nullptr);
    node->setScene(QSharedPointer<Scene>(nullptr));
    scene->removeNode(node);
}

bool SceneNode::isRootNode()
{
    if(this->scene->getRootNode().data()==this)
        return true;

    return false;
}

void SceneNode::updateAnimation(float time)
{
    //@todo: cache transformation animations for faster lookup
    auto keyFrameSet = animation->keyFrameSet;

    if(keyFrameSet->hasKeyFrame("Translation X"))
        pos.setX(keyFrameSet->getKeyFrame("Translation X")->getValueAt(time));
    if(keyFrameSet->hasKeyFrame("Translation Y"))
        pos.setY(keyFrameSet->getKeyFrame("Translation Y")->getValueAt(time));
    if(keyFrameSet->hasKeyFrame("Translation Z"))
        pos.setZ(keyFrameSet->getKeyFrame("Translation Z")->getValueAt(time));

    auto rotEuler = rot.toEulerAngles();
    if(keyFrameSet->hasKeyFrame("Rotation X"))
        rotEuler.setX(keyFrameSet->getKeyFrame("Rotation X")->getValueAt(time));
    if(keyFrameSet->hasKeyFrame("Rotation Y"))
        rotEuler.setY(keyFrameSet->getKeyFrame("Rotation Y")->getValueAt(time));
    if(keyFrameSet->hasKeyFrame("Rotation Z"))
        rotEuler.setZ(keyFrameSet->getKeyFrame("Rotation Z")->getValueAt(time));
    rot = QQuaternion::fromEulerAngles(rotEuler);

    if(keyFrameSet->hasKeyFrame("Scale X"))
        scale.setX(keyFrameSet->getKeyFrame("Scale X")->getValueAt(time));
    if(keyFrameSet->hasKeyFrame("Scale Y"))
        scale.setY(keyFrameSet->getKeyFrame("Scale Y")->getValueAt(time));
    if(keyFrameSet->hasKeyFrame("Scale Z"))
        scale.setZ(keyFrameSet->getKeyFrame("Scale Z")->getValueAt(time));

    //update children
    for (auto child : children) {
        child->updateAnimation(time);
    }
}

void SceneNode::update(float dt)
{
    localTransform.setToIdentity();

    localTransform.translate(pos);
    localTransform.rotate(rot);
    localTransform.scale(scale);

    if (!!parent) {
        globalTransform = this->parent->globalTransform * localTransform;
    } else {
        globalTransform = localTransform;
    }

    for (auto child : children) {
        child->update(dt);
    }

    if (this->visible)
        submitRenderItems();
}

void SceneNode::setParent(SceneNodePtr node)
{
    this->parent = node;
}

void SceneNode::setScene(ScenePtr scene)
{
    this->scene = scene;

    // the scene could be null, as in the case of a tree being built
    // before being added to the scene
    // @WARN -- this actually breaks the tree...
    // if (!!scene) {
    //     scene->addNode(sharedFromThis());
    // }

    // add children
    for (auto& child : children) {
        child->setScene(scene);
    }
}

long SceneNode::generateNodeId()
{
    return nextId++;
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


SceneNodePtr SceneNode::duplicate()
{
    if (!duplicable) {
        return SceneNodePtr();
    }

    auto node = this->createDuplicate();

    node->setName(this->getName());
    node->pos = this->pos;
    node->scale = this->scale;
    node->rot = this->rot;

    for (auto& child : this->children) {
        if (child->isDuplicable()) {
            node->addChild(child->duplicate(), false);
        }
    }

    return node.staticCast<SceneNode>();
}

long SceneNode::nextId = 0;

}
