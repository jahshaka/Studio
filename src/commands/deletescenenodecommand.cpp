#include "deletescenenodecommand.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"

DeleteSceneNodeCommand::DeleteSceneNodeCommand(MainWindow *mainWindow, iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->mainWindow = mainWindow;
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
    this->position = parentNode->children.indexOf(sceneNode);
}

void DeleteSceneNodeCommand::undo()
{
    parentNode->insertChild(position, sceneNode);
    mainWindow->sceneNodeSelected(sceneNode);
    mainWindow->repopulateSceneTree();
}

void DeleteSceneNodeCommand::redo()
{
    sceneNode->removeFromParent();
    mainWindow->sceneNodeSelected(iris::SceneNodePtr());
    mainWindow->repopulateSceneTree();
}
