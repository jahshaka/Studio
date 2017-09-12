#include "deletescenenodecommand.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"
#include "../widgets/sceneheirarchywidget.h"

DeleteSceneNodeCommand::DeleteSceneNodeCommand(MainWindow *mainWindow, SceneHeirarchyWidget* sceneWidget, iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->mainWindow = mainWindow;
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
    this->position = parentNode->children.indexOf(sceneNode);
    this->sceneWidget = sceneWidget;
}

void DeleteSceneNodeCommand::undo()
{
    parentNode->insertChild(position, sceneNode);
    sceneWidget->insertChild(sceneNode);
    mainWindow->sceneNodeSelected(sceneNode);
}

void DeleteSceneNodeCommand::redo()
{
    sceneWidget->removeChild(sceneNode);
    sceneNode->removeFromParent();// important that this is done after!
    mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}
