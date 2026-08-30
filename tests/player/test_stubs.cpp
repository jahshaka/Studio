// Link stubs: PlayBack's controllers can push undo commands through the
// services. The test drives the mouse controller only; none of this is
// exercised, so the command is a do-nothing stand-in.
#include <QUndoCommand>
#include "irisgl/irisglfwd.h"
#include "commands/transformscenenodecommand.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr, QVector3D, QQuaternion, QVector3D) {}
void TransformSceneNodeCommand::undo() {}
void TransformSceneNodeCommand::redo() {}
