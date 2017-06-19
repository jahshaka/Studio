#ifndef TRANSFRORMSCENENODECOMMAND_H
#define TRANSFRORMSCENENODECOMMAND_H

#include <QUndoCommand>
#include <QMatrix4x4>
#include "../irisgl/src/irisglfwd.h"

class TransformSceneNodeCommand : public QUndoCommand
{
    QMatrix4x4 oldTransform;
    QMatrix4x4 newTransform;
    iris::SceneNodePtr sceneNode;

    TransformSceneNodeCommand(iris::SceneNodePtr node, QMatrix4x4 localTransform);
    void undo() override;
    void redo() override;
}

#endif // TRANSFRORMSCENENODECOMMAND_H
