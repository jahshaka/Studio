#include "transfrormscenenodecommand.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node, QMatrix4x4 localTransform)
{
    sceneNode = node;
    oldTransform = node->getLocalTransformMatrix();
    newTransform = localTransform;
}

void TransformSceneNodeCommand::undo()
{
    node->setLocalTransform(oldTransform);
}

void TransformSceneNodeCommand::redo()
{
    node->setLocalTransform(newTransform);
}
