/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "transfrormscenenodecommand.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/math/mathhelper.h"
#include "../uimanager.h"
#include "../widgets/scenenodepropertieswidget.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node, QMatrix4x4 localTransform)
{
    sceneNode = node;
    auto oldTransform = node->getLocalTransform();
    auto newTransform = localTransform;
	iris::MathHelper::decomposeMatrix(oldTransform, oldPos, oldRot, oldScale);
	iris::MathHelper::decomposeMatrix(newTransform, newPos, newRot, newScale);
}

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node, QVector3D pos, QQuaternion rot, QVector3D scale)
{
	sceneNode = node;
	newPos = pos; newRot = rot; newScale = scale;
	oldPos = node->getLocalPos();
	oldRot = node->getLocalRot();
	oldScale = node->getLocalScale();
}

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node,
	QVector3D oldPos, QQuaternion oldRot, QVector3D oldScale,
	QVector3D newPos, QQuaternion newRot, QVector3D newScale)
{
	sceneNode = node;
	this->newPos = newPos;
	this->newRot = newRot;
	this->newScale = newScale;
	this->oldPos = oldPos;
	this->oldRot = oldRot;
	this->oldScale = oldScale;
}

void TransformSceneNodeCommand::undo()
{
	sceneNode->setLocalPos(oldPos);
	sceneNode->setLocalRot(oldRot);
	sceneNode->setLocalScale(oldScale);
	UiManager::propertyWidget->refreshTransform();
}

void TransformSceneNodeCommand::redo()
{
	sceneNode->setLocalPos(newPos);
	sceneNode->setLocalRot(newRot);
	sceneNode->setLocalScale(newScale);
	UiManager::propertyWidget->refreshTransform();
}
