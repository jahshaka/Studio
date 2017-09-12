#ifndef DELETESCENENODECOMMAND_H
#define DELETESCENENODECOMMAND_H

#include <QUndoCommand>
#include "../irisglfwd.h"

class MainWindow;
class SceneHeirarchyWidget;

class DeleteSceneNodeCommand : public QUndoCommand
{
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    int position;
    MainWindow* mainWindow;
    SceneHeirarchyWidget* sceneWidget;
public:
    DeleteSceneNodeCommand(MainWindow* mainWindow, SceneHeirarchyWidget* sceneWidget, iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode);

    void undo() override;
    void redo() override;
};


#endif // DELETESCENENODECOMMAND_H
