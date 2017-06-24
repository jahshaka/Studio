#include "transfrormscenenodecommand.h"
#include "../irisgl/src/scenegraph/scenenode.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr node, QMatrix4x4 localTransform)
{
    sceneNode = node;
    oldTransform = node->getLocalTransform();
    newTransform = localTransform;
}

void TransformSceneNodeCommand::undo()
{
    sceneNode->setLocalTransform(oldTransform);
}

void TransformSceneNodeCommand::redo()
{
    sceneNode->setLocalTransform(newTransform);
}
