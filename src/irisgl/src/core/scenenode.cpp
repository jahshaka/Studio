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
    duplicable = false;
    removable = true;

    localTransform.setToIdentity();
    globalTransform.setToIdentity();
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

        auto diff = initialGlobalTransform * thisGlobalTransform.inverted();

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
    node->scene = QSharedPointer<Scene>(nullptr);
    scene->removeNode(node);
}

bool SceneNode::isRootNode()
{
    if(this->scene->getRootNode().data()==this)
        return true;

    return false;
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
}

void SceneNode::setParent(SceneNodePtr node)
{
    this->parent = node;
}

void SceneNode::setScene(QSharedPointer<Scene> scene)
{
    this->scene = scene;

    //if have children, set scene as well
    for(auto child:children)
        child->setScene(scene);
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
        return localTransform;
    } else {
        return parent->getGlobalTransform() * localTransform;
    }
}

long SceneNode::nextId = 0;


}
