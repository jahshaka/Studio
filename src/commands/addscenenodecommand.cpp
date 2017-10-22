#include "addscenenodecommand.h"
#include "../uimanager.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"
#include "../widgets/scenehierarchywidget.h"


AddSceneNodeCommand::AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
}

void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    UiManager::sceneHierarchyWidget->removeChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode, false);
    UiManager::sceneHierarchyWidget->insertChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(sceneNode);
}
