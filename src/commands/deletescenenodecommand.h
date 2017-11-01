#ifndef DELETESCENENODECOMMAND_H
#define DELETESCENENODECOMMAND_H

#include <QUndoCommand>
#include "../irisglfwd.h"

class MainWindow;
class SceneHierarchyWidget;

class DeleteSceneNodeCommand : public QUndoCommand
{
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    int position;
public:
    DeleteSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode);

    void undo() override;
    void redo() override;
};


#endif // DELETESCENENODECOMMAND_H
