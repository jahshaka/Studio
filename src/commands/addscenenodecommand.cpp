#include "addscenenodecommand.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"


AddSceneNodeCommand::AddSceneNodeCommand(MainWindow* mainWindow, iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->mainWindow = mainWindow;
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
}

void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    mainWindow->sceneNodeSelected(iris::SceneNodePtr());
    mainWindow->repopulateSceneTree();
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode);
    mainWindow->sceneNodeSelected(sceneNode);
    mainWindow->repopulateSceneTree();
}
