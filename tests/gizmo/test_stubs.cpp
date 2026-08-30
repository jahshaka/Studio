// Link stubs: the gizmo classes push undo commands through the services from
// createUndoAction(). The test never drags (and wires no services), so none of
// this is exercised — the command is a do-nothing stand-in.
#include <QUndoCommand>
#include "irisgl/irisglfwd.h"
#include "commands/transformscenenodecommand.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr, QVector3D, QQuaternion, QVector3D) {}
void TransformSceneNodeCommand::undo() {}
void TransformSceneNodeCommand::redo() {}

// The gizmos notify through the services; the test wires none, but the
// linker still wants the symbols.
#include "services/undoservice.h"
#include "services/sceneeditservice.h"
void UndoService::push(QUndoCommand *cmd) { delete cmd; }
void SceneEditService::notifyTransformChanged() {}
