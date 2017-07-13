#ifndef ADDSCENENODECOMMAND_H
#define ADDSCENENODECOMMAND_H

#include <QUndoCommand>
#include "../irisglfwd.h"

class MainWindow;

class AddSceneNodeCommand : public QUndoCommand
{
    //iris::ScenePtr scene;
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    MainWindow* mainWindow;
public:
    AddSceneNodeCommand(MainWindow* mainWindow, iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode);

    void undo() override;
    void redo() override;
};

#endif // ADDSCENENODECOMMAND_H
