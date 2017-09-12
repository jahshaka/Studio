#include "addscenenodecommand.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"
#include "../widgets/sceneheirarchywidget.h"


AddSceneNodeCommand::AddSceneNodeCommand(MainWindow* mainWindow, SceneHeirarchyWidget* sceneWidget, iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->mainWindow = mainWindow;
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
    this->sceneWidget = sceneWidget;
}

void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    sceneWidget->removeChild(sceneNode);
    mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode);
    sceneWidget->insertChild(sceneNode);
    mainWindow->sceneNodeSelected(sceneNode);
}
