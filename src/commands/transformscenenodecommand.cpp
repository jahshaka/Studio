/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/


#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "commands/transformscenenodecommand.h"

#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/core/math/mathhelper.h"
#include "services/services.h"
#include "services/sceneeditservice.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node, iris::Mat4 localTransform)
{
    sceneNode = node;
    auto oldTransform = node->getLocalTransform();
    auto newTransform = localTransform;
	iris::MathHelper::decomposeMatrix(oldTransform, oldPos, oldRot, oldScale);
	iris::MathHelper::decomposeMatrix(newTransform, newPos, newRot, newScale);
}

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node, iris::Vec3 pos, iris::Quat rot, iris::Vec3 scale)
{
	sceneNode = node;
	newPos = pos; newRot = rot; newScale = scale;
	oldPos = node->getLocalPos();
	oldRot = node->getLocalRot();
	oldScale = node->getLocalScale();
}

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node,
	iris::Vec3 oldPos, iris::Quat oldRot, iris::Vec3 oldScale,
	iris::Vec3 newPos, iris::Quat newRot, iris::Vec3 newScale)
{
	sceneNode = node;
	this->newPos = newPos;
	this->newRot = newRot;
	this->newScale = newScale;
	this->oldPos = oldPos;
	this->oldRot = oldRot;
	this->oldScale = oldScale;
}

// `services` is null-checked (the headless-safe contract): scripts push this
// command with no UI wired, and the notification simply has no listeners.
void TransformSceneNodeCommand::undo()
{
	sceneNode->setLocalPos(oldPos);
	sceneNode->setLocalRot(oldRot);
	sceneNode->setLocalScale(oldScale);
	if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}

void TransformSceneNodeCommand::redo()
{
	sceneNode->setLocalPos(newPos);
	sceneNode->setLocalRot(newRot);
	sceneNode->setLocalScale(newScale);
	if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}
