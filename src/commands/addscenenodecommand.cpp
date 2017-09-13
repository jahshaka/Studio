#include "addscenenodecommand.h"
#include "../uimanager.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"
#include "../widgets/sceneheirarchywidget.h"


AddSceneNodeCommand::AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
}

void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    UiManager::sceneHeirarchyWidget->removeChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode, false);
    UiManager::sceneHeirarchyWidget->insertChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(sceneNode);
}
